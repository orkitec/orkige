/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	SelectMenu.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "engine_gui/SelectMenu.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(SelectMenu, SelectMenuItemSelectedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SelectMenu::SelectMenu(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width,
		Ogre::Real boxWidth, unsigned int maxItemsShown)
		: Widget(name, materialGroup)
		, highlightIndex(0)
		, displayIndex(0)
		, dragOffset(0.0f)
	{
		this->selectionIndex = -1;
		this->fitToContents = false;
		this->cursorOver = false;
		this->expanded = false;
		this->dragging = false;
		this->maxItemsShown = maxItemsShown;
		this->itemsShown = 0;
		this->overlayElement = (Ogre::BorderPanelOverlayElement*)Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/SelectMenu", "BorderPanel", name);
		this->textArea = (Ogre::TextAreaOverlayElement*)((Ogre::OverlayContainer*)this->overlayElement)->getChild(name + "/MenuCaption");
		this->smallBox = (Ogre::BorderPanelOverlayElement*)((Ogre::OverlayContainer*)this->overlayElement)->getChild(name + "/MenuSmallBox");
		this->smallBox->setWidth(width - 10);
		this->smallTextArea = (Ogre::TextAreaOverlayElement*)this->smallBox->getChild(name + "/MenuSmallBox/MenuSmallText");
		this->overlayElement->setWidth(width);
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
		this->smallTextArea->setCharHeight(this->smallTextArea->getCharHeight() - 3);
#endif

		if (boxWidth > 0)  // long style
		{
			if (width <= 0) this->fitToContents = true;
			this->smallBox->setWidth(boxWidth);
			this->smallBox->setTop(2);
			this->smallBox->setLeft(width - boxWidth - 5);
			this->overlayElement->setHeight(this->smallBox->getHeight() + 4);
			this->textArea->setHorizontalAlignment(Ogre::GHA_LEFT);
			this->textArea->setAlignment(Ogre::TextAreaOverlayElement::Left);
			this->textArea->setLeft(12);
			this->textArea->setTop(10);
		}

		this->expandedBox = (Ogre::BorderPanelOverlayElement*)((Ogre::OverlayContainer*)this->overlayElement)->getChild(name + "/MenuExpandedBox");
		this->expandedBox->setWidth(this->smallBox->getWidth() + 10);
		this->expandedBox->hide();
		this->scrollTrack = (Ogre::BorderPanelOverlayElement*)this->expandedBox->getChild(this->expandedBox->getName() + "/MenuScrollTrack");
		this->scrollHandle = (Ogre::PanelOverlayElement*)this->scrollTrack->getChild(this->scrollTrack->getName() + "/MenuScrollHandle");

		this->setCaption(caption);
	}
	//---------------------------------------------------------
	bool SelectMenu::isExpanded()
	{
		return this->expanded;
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& SelectMenu::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void SelectMenu::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);
		if (this->fitToContents)
		{
			this->overlayElement->setWidth(OverlayUtil::getCaptionWidth(caption, this->textArea) + this->smallBox->getWidth() + 23);
			this->smallBox->setLeft(this->overlayElement->getWidth() - this->smallBox->getWidth() - 5);
		}
	}
	//---------------------------------------------------------
	const Ogre::StringVector& SelectMenu::getItems()
	{
		return this->items;
	}
	//---------------------------------------------------------
	unsigned int SelectMenu::getNumItems()
	{
		return this->items.size();
	}
	//---------------------------------------------------------
	void SelectMenu::setItems(const Ogre::StringVector& items)
	{
		this->items = items;
		this->selectionIndex = -1;

		for (unsigned int i = 0; i < this->itemOverlayElements.size(); i++)   // destroy all the item elements
		{
			OverlayUtil::nukeOverlayElement(this->itemOverlayElements[i]);
		}
		this->itemOverlayElements.clear();

		this->itemsShown = std::max<int>(2, std::min<int>(this->maxItemsShown, this->items.size()));

		for (unsigned int i = 0; i < this->itemsShown; i++)   // create all the item elements
		{
			Ogre::BorderPanelOverlayElement* e = (Ogre::BorderPanelOverlayElement*)Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/SelectMenuItem", "BorderPanel",
				this->expandedBox->getName() + "/Item" + Ogre::StringConverter::toString(i + 1));

			e->setTop(6 + i * (this->smallBox->getHeight() - 8));
			e->setWidth(this->expandedBox->getWidth() - 32);

			this->expandedBox->addChild(e);
			this->itemOverlayElements.push_back(e);
		}

		if (!items.empty()) selectItem(0, false);
		else this->smallTextArea->setCaption("");
	}
	//---------------------------------------------------------
	void SelectMenu::addItem(const Ogre::DisplayString& item)
	{
		this->items.push_back(item);
		this->setItems(this->items);
	}
	//---------------------------------------------------------
	void SelectMenu::removeItem(const Ogre::DisplayString& item)
	{
		Ogre::StringVector::iterator it;

		for (it = this->items.begin(); it != this->items.end(); it++)
		{
			if (item == *it) break;
		}

		if (it != this->items.end())
		{
			this->items.erase(it);
			if (this->items.size() < this->itemsShown)
			{
				this->itemsShown = this->items.size();
				OverlayUtil::nukeOverlayElement(this->itemOverlayElements.back());
				this->itemOverlayElements.pop_back();
			}
		}
		else 
		{
			Ogre::String desc = "Menu \"" + this->getName() + "\" contains no item \"" + item + "\".";
			OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "SelectMenu::removeItem");
		}
	}
	//---------------------------------------------------------
	void SelectMenu::removeItem(unsigned int index)
	{
		Ogre::StringVector::iterator it;
		unsigned int i = 0;

		for (it = this->items.begin(); it != this->items.end(); it++)
		{
			if (i == index) break;
			i++;
		}

		if (it != this->items.end())
		{
			this->items.erase(it);
			if (this->items.size() < this->itemsShown)
			{
				this->itemsShown = this->items.size();
				OverlayUtil::nukeOverlayElement(this->itemOverlayElements.back());
				this->itemOverlayElements.pop_back();
			}
		}
		else 
		{
			Ogre::String desc = "Menu \"" + getName() + "\" contains no item at position " + Ogre::StringConverter::toString(index) + ".";
			OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "SelectMenu::removeItem");
		}
	}
	//---------------------------------------------------------
	void SelectMenu::clearItems()
	{
		this->items.clear();
		this->selectionIndex = -1;
		this->smallTextArea->setCaption("");
	}
	//---------------------------------------------------------
	void SelectMenu::selectItem(unsigned int index, bool notifyListener)
	{
		if (index < 0 || index >= this->items.size())
		{
			Ogre::String desc = "Menu \"" + getName() + "\" contains no item at position " + Ogre::StringConverter::toString(index) + ".";
			OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "SelectMenu::selectItem");
		}

		this->selectionIndex = index;
		OverlayUtil::fitCaptionToArea(this->items[index], this->smallTextArea, this->smallBox->getWidth() - this->smallTextArea->getLeft() * 2);

		if (notifyListener)
		{ 
			GuiManager::getSingleton().getEventManager()->trigger(Event(SelectMenu::SelectMenuItemSelectedEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	void SelectMenu::selectItem(const Ogre::DisplayString& item, bool notifyListener)
	{
		for (unsigned int i = 0; i < this->items.size(); i++)
		{
			if (item == this->items[i])
			{
				this->selectItem(i, notifyListener);
				return;
			}
		}

		Ogre::String desc = "Menu \"" + getName() + "\" contains no item \"" + item + "\".";
		OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "SelectMenu::selectItem");
	}
	//---------------------------------------------------------
	Ogre::DisplayString SelectMenu::getSelectedItem()
	{
		if (this->selectionIndex == -1)
		{
			Ogre::String desc = "Menu \"" + getName() + "\" has no item selected.";
			OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, desc, "SelectMenu::getSelectedItem");
			return "";
		}

		return this->items[this->selectionIndex];
	}
	//---------------------------------------------------------
	int SelectMenu::getSelectionIndex()
	{
		return this->selectionIndex;
	}
	//---------------------------------------------------------
	void SelectMenu::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();

		if (this->expanded)
		{
			if (this->scrollHandle->isVisible())   // check for scrolling
			{
				Ogre::Vector2 co = OverlayUtil::cursorOffset(this->scrollHandle, cursorPos);

				if (co.squaredLength() <= 81)
				{
					this->dragging = true;
					this->dragOffset = co.y;
					return;
				}
				else if (OverlayUtil::isCursorOver(this->scrollTrack, cursorPos))
				{
					Ogre::Real newTop = this->scrollHandle->getTop() + co.y;
					Ogre::Real lowerBoundary = this->scrollTrack->getHeight() - this->scrollHandle->getHeight();
					this->scrollHandle->setTop((Ogre::Real)Ogre::Math::Clamp<int>((int)newTop, 0, (int)lowerBoundary));

					Ogre::Real scrollPercentage = Ogre::Math::Clamp<Ogre::Real>(newTop / lowerBoundary, 0, 1);
					this->setDisplayIndex((unsigned int)(scrollPercentage * (this->items.size() - this->itemOverlayElements.size()) + 0.5));
					return;
				}
			}

			if (!OverlayUtil::isCursorOver(this->expandedBox, cursorPos, 3)) retract();
			else
			{
				Ogre::Real l = this->itemOverlayElements.front()->_getDerivedLeft() * om.getViewportWidth() + 5;
				Ogre::Real t = this->itemOverlayElements.front()->_getDerivedTop() * om.getViewportHeight() + 5;
				Ogre::Real r = l + this->itemOverlayElements.back()->getWidth() - 10;
				Ogre::Real b = this->itemOverlayElements.back()->_getDerivedTop() * om.getViewportHeight() + this->itemOverlayElements.back()->getHeight() - 5;

				if (cursorPos.x >= l && cursorPos.x <= r && cursorPos.y >= t && cursorPos.y <= b)
				{
					if (this->highlightIndex != this->selectionIndex) selectItem(this->highlightIndex);
					this->retract();
				}
			}
		}
		else
		{
			if (this->items.size() < 2) return;   // don't waste time showing a menu if there's no choice

			if (OverlayUtil::isCursorOver(this->smallBox, cursorPos, 4))
			{
				this->expandedBox->show();
				this->smallBox->hide();

				// calculate how much vertical space we need
				Ogre::Real idealHeight = this->itemsShown * (this->smallBox->getHeight() - 8) + 20;
				this->expandedBox->setHeight(idealHeight);
				this->scrollTrack->setHeight(this->expandedBox->getHeight() - 20);

				this->expandedBox->setLeft(this->smallBox->getLeft() - 4);

				// if the expanded menu goes down off the screen, make it go up instead
				if (this->smallBox->_getDerivedTop() * om.getViewportHeight() + idealHeight > om.getViewportHeight())
				{
					this->expandedBox->setTop(this->smallBox->getTop() + this->smallBox->getHeight() - idealHeight + 3);
					// if we're in thick style, hide the caption because it will interfere with the expanded menu
					if (this->textArea->getHorizontalAlignment() == Ogre::GHA_CENTER) this->textArea->hide();
				}
				else this->expandedBox->setTop(this->smallBox->getTop() + 3);

				this->expanded = true;
				this->highlightIndex = this->selectionIndex;
				this->setDisplayIndex(this->highlightIndex);

				if (this->itemsShown < this->items.size())  // update scrollbar position
				{
					this->scrollHandle->show();
					Ogre::Real lowerBoundary = this->scrollTrack->getHeight() - this->scrollHandle->getHeight();
					this->scrollHandle->setTop((Ogre::Real)(int)(this->displayIndex * lowerBoundary / (this->items.size() - this->itemOverlayElements.size())));
				}
				else this->scrollHandle->hide();
			}
		}
	}
	//---------------------------------------------------------
	void SelectMenu::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		this->dragging = false;
	}
	//---------------------------------------------------------
	void SelectMenu::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();

		if (this->expanded)
		{
			if (this->dragging)
			{
				Ogre::Vector2 co = OverlayUtil::cursorOffset(this->scrollHandle, cursorPos);
				Ogre::Real newTop = this->scrollHandle->getTop() + co.y - this->dragOffset;
				Ogre::Real lowerBoundary = this->scrollTrack->getHeight() - this->scrollHandle->getHeight();
				this->scrollHandle->setTop((Ogre::Real)Ogre::Math::Clamp<int>((int)newTop, 0, (int)lowerBoundary));

				Ogre::Real scrollPercentage = Ogre::Math::Clamp<Ogre::Real>(newTop / lowerBoundary, 0, 1);
				int newIndex = (int) (scrollPercentage * (this->items.size() - this->itemOverlayElements.size()) + 0.5);
				if (newIndex != this->displayIndex) setDisplayIndex(newIndex);
				return;
			}

			Ogre::Real l = this->itemOverlayElements.front()->_getDerivedLeft() * om.getViewportWidth() + 5;
			Ogre::Real t = this->itemOverlayElements.front()->_getDerivedTop() * om.getViewportHeight() + 5;
			Ogre::Real r = l + this->itemOverlayElements.back()->getWidth() - 10;
			Ogre::Real b = this->itemOverlayElements.back()->_getDerivedTop() * om.getViewportHeight() +
				this->itemOverlayElements.back()->getHeight() - 5;

			if (cursorPos.x >= l && cursorPos.x <= r && cursorPos.y >= t && cursorPos.y <= b)
			{
				int newIndex = (int)(this->displayIndex + (cursorPos.y - t) / (b - t) * this->itemOverlayElements.size());
				if (this->highlightIndex != newIndex)
				{
					this->highlightIndex = newIndex;
					this->setDisplayIndex(this->displayIndex);
				}
			}
		}
		else
		{
			if (OverlayUtil::isCursorOver(this->smallBox, cursorPos, 4))
			{
				this->smallBox->setMaterialName(this->materialGroup + "/MiniTextBox/Over");
				this->smallBox->setBorderMaterialName(this->materialGroup + "/MiniTextBox/Over");
				this->cursorOver = true;
			}
			else
			{
				if (this->cursorOver)
				{
					this->smallBox->setMaterialName(this->materialGroup + "/MiniTextBox");
					this->smallBox->setBorderMaterialName(this->materialGroup + "/MiniTextBox");
					this->cursorOver = false;
				}
			}
		}
	}
	//---------------------------------------------------------
	void SelectMenu::onFocusLost()
	{
		if (this->expandedBox->isVisible()) 
		{
			this->retract();
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SelectMenu::setDisplayIndex(unsigned int index)
	{
		index = std::min<int>(index, this->items.size() - this->itemOverlayElements.size());
		this->displayIndex = index;
		Ogre::BorderPanelOverlayElement* ie;
		Ogre::TextAreaOverlayElement* ta;

		for (int i = 0; i < (int)this->itemOverlayElements.size(); i++)
		{
			ie = this->itemOverlayElements[i];
			ta = (Ogre::TextAreaOverlayElement*)ie->getChild(ie->getName() + "/MenuItemText");

			OverlayUtil::fitCaptionToArea(this->items[this->displayIndex + i], ta, ie->getWidth() - 2 * ta->getLeft());

			if ((this->displayIndex + i) == this->highlightIndex)
			{
				ie->setMaterialName(this->materialGroup + "/MiniTextBox/Over");
				ie->setBorderMaterialName(this->materialGroup + "/MiniTextBox/Over");
			}
			else
			{
				ie->setMaterialName(this->materialGroup + "/MiniTextBox");
				ie->setBorderMaterialName(this->materialGroup + "/MiniTextBox");
			}
		}
	}
	//---------------------------------------------------------
	void SelectMenu::retract()
	{
		this->dragging = false;
		this->expanded = false;
		this->expandedBox->hide();
		this->textArea->show();
		this->smallBox->show();
		this->smallBox->setMaterialName(this->materialGroup + "/MiniTextBox");
		this->smallBox->setBorderMaterialName(this->materialGroup + "/MiniTextBox");
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(SelectMenu)
	OOBJECT_END
}