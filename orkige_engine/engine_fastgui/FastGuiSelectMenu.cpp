/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   FastGuiSelectMenu.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiSelectMenu.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_event/GlobalEventManager.h>
// boost string algorithms dropped (no-boost rule); the only use below is commented out

namespace Orkige 
{
	IMPL_OWNED_EVENTTYPE(FastGuiSelectMenu, SelectMenuEvent);
    //----------------------------------------------------
    //- public: ------------------------------------------
    //----------------------------------------------------
	FastGuiSelectMenu::FastGuiSelectMenu(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z) 
		: FastGuiWidget(id, atlas, z)
	{
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		this->label->setAlignment(FastGuiLabel::LA_TOP);

		this->leftArrow = onew(new FastGuiDecorWidget("leftArrow.decor", "select_menu_field_left", position, Ogre::Vector2::ZERO, atlas, z));
		this->rightArrow = onew(new FastGuiDecorWidget("rightArrow.decor", "select_menu_field_right", position, Ogre::Vector2::ZERO, atlas, z));
		
		this->buttonMainSelection = onew(new FastGuiButton(buttonId, "select_menu_field", defaultGlyphIndex, "dunnoooooo!", position, FastGuiLabel::LA_LEFT, Ogre::Vector2::ZERO, atlas, z));
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
    FastGuiSelectMenu::~FastGuiSelectMenu()
    {
    }
	//----------------------------------------------------
	void FastGuiSelectMenu::setPosition( Ogre::Real left, Ogre::Real top )
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
		this->updatePosition();
	}
	//----------------------------------------------------
	void FastGuiSelectMenu::setSize( Ogre::Real width, Ogre::Real height )
	{
		this->decor->setSize(width, height);
		this->label->setSize(width, height);
		this->updateSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 FastGuiSelectMenu::getSize()
	{
		return this->decor->getSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 FastGuiSelectMenu::getPosition()
	{
		return this->decor->getPosition();
	}
	//----------------------------------------------------
	void FastGuiSelectMenu::onCursorPressed( Ogre::Vector2 const & cursorPos )
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
	void FastGuiSelectMenu::onCursorReleased( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorReleased(cursorPos);
	}
	//----------------------------------------------------
	void FastGuiSelectMenu::onCursorMoved( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorMoved(cursorPos);
	}
	//----------------------------------------------------
	void FastGuiSelectMenu::setItems( const Ogre::StringVector& items )
	{
		this->items = items;
		this->selectItemIndex(0, false);
	}
	//----------------------------------------------------
	void FastGuiSelectMenu::showItem()
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
	void FastGuiSelectMenu::selectItemIndex(std::size_t index, bool throwEvent)
	{
		if (index < this->items.size())
		{
			if (this->selectedIndex != index)
			{
				this->selectedIndex = index;
				this->showItem();

				if (throwEvent)
				{
					GlobalEventManager::getSingleton().trigger(Event(FastGuiSelectMenu::SelectMenuEvent, oBadPointer(this)));
				}
			}
		}
	}
	//----------------------------------------------------
	void FastGuiSelectMenu::selectItem(String item)
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
	Orkige::String FastGuiSelectMenu::getSelectedItem()
	{
		return this->items[this->selectedIndex];
	}
	//----------------------------------------------------
	Orkige::String FastGuiSelectMenu::getCaption()
	{
		return this->label->getCaption()->text();	
	}
	//----------------------------------------------------
	void FastGuiSelectMenu::setCaption( String const & text )
	{
		this->label->setText(text);
	}
    //----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------
	void FastGuiSelectMenu::updatePosition()
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
	void FastGuiSelectMenu::updateSize()
	{
		this->buttonMainSelection->setSize(this->decor->getSize().x * 0.8f, this->decor->getSize().y * 0.3f);
		this->leftArrow->setSize(this->decor->getSize().x * 0.2f, this->decor->getSize().y * 0.9f);
		this->rightArrow->setSize(this->decor->getSize().x * 0.2f, this->decor->getSize().y * 0.9f);
	}
	//----------------------------------------------------
	//- private: -----------------------------------------
	//----------------------------------------------------
	OABSTRACT_IMPL(FastGuiSelectMenu)
		OOBJECT_END


} 