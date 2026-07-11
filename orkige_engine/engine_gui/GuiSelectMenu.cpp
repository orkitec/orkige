/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   GuiSelectMenu.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiSelectMenu.h"
#include "engine_gui/GuiManager.h"
#include <core_event/GlobalEventManager.h>
// boost string algorithms dropped (no-boost rule); the only use below is commented out

namespace Orkige 
{
	IMPL_OWNED_EVENTTYPE(GuiSelectMenu, SelectMenuEvent);
    //----------------------------------------------------
    //- public: ------------------------------------------
    //----------------------------------------------------
	GuiSelectMenu::GuiSelectMenu(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z) 
		: GuiWidget(id, atlas, z)
	{
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		this->label = onew(new GuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		this->label->setAlignment(GuiLabel::LA_TOP);

		this->leftArrow = onew(new GuiDecorWidget("leftArrow.decor", "select_menu_field_left", position, Ogre::Vector2::ZERO, atlas, z));
		this->rightArrow = onew(new GuiDecorWidget("rightArrow.decor", "select_menu_field_right", position, Ogre::Vector2::ZERO, atlas, z));
		
		this->buttonMainSelection = onew(new GuiButton(buttonId, "select_menu_field", defaultGlyphIndex, "dunnoooooo!", position, GuiLabel::LA_LEFT, Ogre::Vector2::ZERO, atlas, z));
		this->buttonMainSelection->getLabel().lock()->getCaption()->colour(Orkige::Colours::webcolour(Orkige::Colours::Black));
		if (size != Ogre::Vector2::ZERO)
		{
			this->updateSize();
		}
		this->updatePosition();

		this->selectedIndex = -1;
		this->showItem();
	}
	//----------------------------------------------------
    GuiSelectMenu::~GuiSelectMenu()
    {
    }
	//----------------------------------------------------
	void GuiSelectMenu::setPosition( Ogre::Real left, Ogre::Real top )
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
		this->updatePosition();
	}
	//----------------------------------------------------
	void GuiSelectMenu::setSize( Ogre::Real width, Ogre::Real height )
	{
		this->decor->setSize(width, height);
		this->label->setSize(width, height);
		this->updateSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 GuiSelectMenu::getSize()
	{
		return this->decor->getSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 GuiSelectMenu::getPosition()
	{
		return this->decor->getPosition();
	}
	//----------------------------------------------------
	void GuiSelectMenu::onCursorPressed( Ogre::Vector2 const & cursorPos )
	{
		if (this->buttonMainSelection->getDecor().lock()->getRectangle()->intersects(cursorPos)) 
		{
			this->buttonMainSelection->onCursorPressed(cursorPos);
		}
		if (this->leftArrow->getRectangle()->intersects(cursorPos))
		{
			this->selectItemIndex(this->selectedIndex - 1);
		}
		if (this->rightArrow->getRectangle()->intersects(cursorPos))
		{
			this->selectItemIndex(this->selectedIndex + 1);
		}
	}
	//----------------------------------------------------
	void GuiSelectMenu::onCursorReleased( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorReleased(cursorPos);
	}
	//----------------------------------------------------
	void GuiSelectMenu::onCursorMoved( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorMoved(cursorPos);
	}
	//----------------------------------------------------
	void GuiSelectMenu::setItems( const Ogre::StringVector& items )
	{
		this->items = items;
		this->selectItemIndex(0, false);
	}
	//----------------------------------------------------
	void GuiSelectMenu::showItem()
	{
		if (!this->items.empty())
		{
			//Orkige::String text = this->items.at(this->selectedIndex);
			//boost::replace_all(text, "\\n", "\n");
			//this->buttonMainSelection->setCaption(text);
			
			this->buttonMainSelection->setCaption(this->items.at(this->selectedIndex));
		}
		else
		{
			this->buttonMainSelection->setCaption("Empty!");
		}
	}
	//---------------------------------------------------------------
	void GuiSelectMenu::selectItemIndex(std::size_t index, bool throwEvent)
	{
		if (index < this->items.size())
		{
			if (this->selectedIndex != index)
			{
				this->selectedIndex = index;
				this->showItem();

				if (throwEvent)
				{
					GlobalEventManager::getSingleton().trigger(Event(GuiSelectMenu::SelectMenuEvent, oBadPointer(this)));
				}
			}
		}
	}
	//----------------------------------------------------
	void GuiSelectMenu::selectItem(String item)
	{
		for (unsigned int i = 0; i < this->items.size(); i++)
		{
			if (item == this->items[i])
			{
				this->selectItemIndex(i);
				return;
			}
		}

		OGRE_EXCEPT(Ogre::Exception::ERR_ITEM_NOT_FOUND, item, "SelectMenu::selectItem");
	}
	//----------------------------------------------------
	Orkige::String GuiSelectMenu::getSelectedItem()
	{
		return this->items[this->selectedIndex];
	}
	//----------------------------------------------------
	Orkige::String GuiSelectMenu::getCaption()
	{
		return this->label->getCaption()->text();	
	}
	//----------------------------------------------------
	void GuiSelectMenu::setCaption( String const & text )
	{
		this->label->setText(text);
	}
    //----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------
	void GuiSelectMenu::updatePosition()
	{
		this->leftArrow->setPosition(this->decor->getPosition().x - this->leftArrow->getSize().x,
			this->leftArrow->getPosition().y + (this->decor->getSize().y / 2.0f) - (this->leftArrow->getSize().y / 2.0f));
		this->rightArrow->setPosition(this->decor->getPosition().x + this->decor->getSize().x,
			this->rightArrow->getPosition().y + (this->decor->getSize().y / 2.0f) - (this->rightArrow->getSize().y / 2.0f));

		float positionX = this->buttonMainSelection->getPosition().x + (this->decor->getSize().x / 2.0f) - (this->buttonMainSelection->getSize().x / 2.0f);
		float positionY = this->decor->getPosition().y + (this->decor->getSize().y / 2.0f);
		this->buttonMainSelection->setPosition(floor(positionX), floor(positionY));
	}
	//----------------------------------------------------
	void GuiSelectMenu::updateSize()
	{
		this->buttonMainSelection->setSize(this->decor->getSize().x * 0.8f, this->decor->getSize().y * 0.3f);
		this->leftArrow->setSize(this->decor->getSize().x * 0.2f, this->decor->getSize().y * 0.9f);
		this->rightArrow->setSize(this->decor->getSize().x * 0.2f, this->decor->getSize().y * 0.9f);
	}
	//----------------------------------------------------
	void GuiSelectMenu::onEnabledChanged(bool enable)
	{
		const float alpha = enable ? 1.0f : GuiWidget::DISABLED_ALPHA;
		if(this->decor)		this->decor->setAlpha(alpha);
		if(this->leftArrow)	this->leftArrow->setAlpha(alpha);
		if(this->rightArrow)this->rightArrow->setAlpha(alpha);
		if(this->label)		this->label->setAlpha(alpha);
		// the value field is an internal button; flip its enabled state so it
		// dims with the same convention (it is not in the manager dispatch, so
		// this only changes its look)
		if(this->buttonMainSelection)
		{
			this->buttonMainSelection->setEnabled(enable);
		}
	}
	//----------------------------------------------------
	//- private: -----------------------------------------
	//----------------------------------------------------
	OABSTRACT_IMPL(GuiSelectMenu)
		// option cycler / settings value: scripts poll getSelectedItemIndex()
		// and drive it via selectItemIndex/selectItem; setItems takes a Lua
		// array of option strings. GuiSlider inherits all of this - its
		// grip value IS the selected item index.
		OFUNC(setItems)
		OFUNC(getSelectedItemIndex)
		OFUNC(getSelectedItem)
		OFUNC(selectItemIndex)
		OFUNC(selectItem)
		OFUNC(getCaption)
		OFUNC(setCaption)
	OOBJECT_END


} 