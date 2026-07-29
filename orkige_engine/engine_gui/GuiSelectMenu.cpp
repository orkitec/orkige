/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   GuiSelectMenu.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiSelectMenu.h"
#include "engine_gui/GuiManager.h"
#include <core_event/GlobalEventManager.h>
#include <OgreString.h>
#include <algorithm>
#include <cmath>
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
		this->markInteractive();	// a select-menu (and its slider subclass) consumes input
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		this->label = onew(new GuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		// the title reads on the leading side of the row (the value field and its
		// arrows own the trailing side) - a centred title would sit under them
		this->label->setAlignment(GuiLabel::LA_LEFT);

		this->leftArrow = onew(new GuiDecorWidget("leftArrow.decor", "select_menu_field_left", position, Ogre::Vector2::ZERO, atlas, z));
		this->rightArrow = onew(new GuiDecorWidget("rightArrow.decor", "select_menu_field_right", position, Ogre::Vector2::ZERO, atlas, z));
		
		this->buttonMainSelection = onew(new GuiButton(buttonId, "select_menu_field", defaultGlyphIndex, EMPTY_VALUE_CAPTION, position, GuiLabel::LA_CENTER, Ogre::Vector2::ZERO, atlas, z));
		if(optr<GuiLabel> valueLabel = this->buttonMainSelection->getLabel().lock())
		{
			valueLabel->getCaption()->colour(
				Orkige::Colours::webcolour(Orkige::Colours::Black));
		}
		this->arrangeParts();

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
		this->arrangeParts();
	}
	//----------------------------------------------------
	void GuiSelectMenu::setSize( Ogre::Real width, Ogre::Real height )
	{
		this->decor->setSize(width, height);
		this->arrangeParts();
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
	void GuiSelectMenu::setItemsString(String const & pipeDelimited)
	{
		// split on '|' (labels may hold spaces), trimming each piece - the
		// script-friendly path (the seam cannot pass a Lua table as a vector)
		Ogre::StringVector items = Ogre::StringUtil::split(pipeDelimited, "|");
		for(String & item : items)
		{
			Ogre::StringUtil::trim(item);
		}
		this->setItems(items);
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
			this->buttonMainSelection->setCaption(EMPTY_VALUE_CAPTION);
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
					// gui.valueChanged onto the ONE engine event bus (queued on
					// GlobalEventManager); select-menu / slider polling stays
					// valid. The new selected index is the changed value.
					if (GuiManager::getSingletonPtr())
					{
						GuiManager::getSingleton().emitGuiValueChanged(
							this->getObjectID(),
							static_cast<double>(this->selectedIndex));
					}
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
	Ogre::Vector4 GuiSelectMenu::arrangeParts()
	{
		// the row: [ title .......... < value > ], every part inside the field
		const Ogre::Vector2 origin = this->decor->getPosition();
		const Ogre::Vector2 size = this->decor->getSize();
		const Ogre::Real inset = std::floor(size.y * 0.12f);
		const Ogre::Real partHeight = std::max(4.0f, size.y - 2.0f * inset);
		const Ogre::Real arrowWidth = std::floor(std::min(partHeight,
			std::max(0.0f, size.x - 2.0f * inset) * 0.15f));
		const Ogre::Real valueWidth = std::max(4.0f, std::floor((size.x
			- 2.0f * inset - 2.0f * arrowWidth) * 0.5f));
		const Ogre::Real partTop = std::floor(origin.y
			+ (size.y - partHeight) * 0.5f);
		const Ogre::Real rightArrowLeft = std::floor(origin.x + size.x - inset
			- arrowWidth);
		const Ogre::Real valueLeft = rightArrowLeft - valueWidth;
		const Ogre::Real leftArrowLeft = valueLeft - arrowWidth;

		this->leftArrow->setSize(arrowWidth, partHeight);
		this->rightArrow->setSize(arrowWidth, partHeight);
		this->leftArrow->setPosition(leftArrowLeft, partTop);
		this->rightArrow->setPosition(rightArrowLeft, partTop);
		this->buttonMainSelection->setSize(valueWidth, partHeight);
		this->buttonMainSelection->setPosition(valueLeft, partTop);
		// the title reads in the space left of the arrows, never under the value
		// (its own margin is doubled so the glyphs do not touch the frame)
		const Ogre::Real textInset = inset * 2.0f;
		this->label->setPosition(std::floor(origin.x + textInset), origin.y);
		this->label->setSize(std::max(0.0f, leftArrowLeft - origin.x
			- 2.0f * textInset), size.y);
		return Ogre::Vector4(valueLeft, partTop, valueWidth, partHeight);
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
	void GuiSelectMenu::applyRenderTransform(Ui2DTransform const & transform)
	{
		if(this->decor)					this->decor->applyRenderTransform(transform);
		if(this->leftArrow)				this->leftArrow->applyRenderTransform(transform);
		if(this->rightArrow)			this->rightArrow->applyRenderTransform(transform);
		if(this->label)					this->label->applyRenderTransform(transform);
		if(this->buttonMainSelection)	this->buttonMainSelection->applyRenderTransform(transform);
	}
	//----------------------------------------------------
	void GuiSelectMenu::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->decor)					this->decor->applyRenderAlpha(alphaMultiplier);
		if(this->leftArrow)				this->leftArrow->applyRenderAlpha(alphaMultiplier);
		if(this->rightArrow)			this->rightArrow->applyRenderAlpha(alphaMultiplier);
		if(this->label)					this->label->applyRenderAlpha(alphaMultiplier);
		if(this->buttonMainSelection)	this->buttonMainSelection->applyRenderAlpha(alphaMultiplier);
	}
	//----------------------------------------------------
	OABSTRACT_IMPL(GuiSelectMenu)
		// option cycler / settings value: scripts poll getSelectedItemIndex()
		// and drive it via selectItemIndex/selectItem; setItems takes a Lua
		// array of option strings. GuiSlider inherits all of this - its
		// grip value IS the selected item index.
		OFUNC(setItems)
		OFUNC(setItemsString)
		OFUNC(getSelectedItemIndex)
		OFUNC(getSelectedItem)
		OFUNC(selectItemIndex)
		OFUNC(selectItem)
		OFUNC(getCaption)
		OFUNC(setCaption)
	OOBJECT_END


} 