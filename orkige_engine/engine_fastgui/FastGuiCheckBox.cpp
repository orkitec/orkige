/********************************************************************
created:    Tuesday 2010/11/02 at 17:50
filename:   FastGuiCheckBox.cpp
author:     hicham.allaoui  
notice:		This source file is part of orkige (orkitec Game engine)
			For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiCheckBox.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_event/GlobalEventManager.h>

#define FASTGUICHECKBOX_MARGING 10.f

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(FastGuiCheckBox, CheckBoxToggledEvent);
    //----------------------------------------------------
    //- public: ------------------------------------------
    //----------------------------------------------------
    FastGuiCheckBox::FastGuiCheckBox(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool useCheckbox) : 
		FastGuiWidget(id, atlas, z), 
		checked(false),
		baseSpriteName(spriteName)
    {
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		if (useCheckbox)
		{
			// needs "name.png" and "checkbox_off.png" and "checkbox_on.png"
			this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
			this->checkbox = onew(new FastGuiDecorWidget("checkSymbol.decor", spriteName + "_off", position, size, atlas, z+2));
		}
		else
		{
			// needs "name_off.png" and "name_on.png"
			this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName + "_off", position, size, atlas, z));
		}

		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		//this->label->setAlignment(textAlignment);

		//this->setPosition(this->getPosition().x, this->getPosition().y);
		//this->setChecked(this->checked);
    }
    //----------------------------------------------------
    FastGuiCheckBox::~FastGuiCheckBox()
    {
	}   
	//----------------------------------------------------
	void FastGuiCheckBox::setPosition( Ogre::Real left, Ogre::Real top )
	{
		// TODO when executing this code the sprite appear completely white!
		oAssertDesc(false, "CheckBox position not implemented");


		this->decor->setPosition(left, top);
		if (this->checkbox != NULL)
		{
			Ogre::Real xPosition = this->decor->getPosition().x + (this->decor->getSize().x) - (this->checkbox->getSize().x) - FASTGUICHECKBOX_MARGING;
			Ogre::Real yPosition = this->decor->getPosition().y + (this->decor->getSize().y/2.f) - (this->checkbox->getSize().y/2.f);
			this->checkbox->setPosition(xPosition, yPosition);
			xPosition = this->decor->getPosition().x + FASTGUICHECKBOX_MARGING;
			this->label->setPosition(xPosition, yPosition);
		}
		else
		{
			this->label->setPosition(left, top); //?
		}
	}
	//----------------------------------------------------
	void FastGuiCheckBox::setSize( Ogre::Real width, Ogre::Real height )
	{
		this->decor->setSize(width, height);
		this->label->setSize(width, height);
		if (this->checkbox != NULL)
		{
			this->checkbox->setSize(width, height);
		}
	}
	//----------------------------------------------------
	Ogre::Vector2 FastGuiCheckBox::getSize()
	{
		return this->decor->getSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 FastGuiCheckBox::getPosition()
	{
		return this->decor->getPosition();
	}
	//----------------------------------------------------
	void FastGuiCheckBox::onCursorPressed( Ogre::Vector2 const & cursorPos )
	{
		if (this->decor->getRectangle()->intersects(cursorPos)) 
		{
			this->toggle();
		}
	}
	//----------------------------------------------------
	void FastGuiCheckBox::onCursorReleased( Ogre::Vector2 const & cursorPos )
	{

	}
	//----------------------------------------------------
	void FastGuiCheckBox::setChecked( bool checked, bool notifyListener /*= true*/ )
	{
		this->checked = checked;

		if (this->checkbox != NULL)
		{
			Orkige::String spriteName("checkbox");
			spriteName += (this->checked ? "_on" : "_off");
			this->checkbox->setSprite(spriteName);
		}
		else
		{
			Orkige::String spriteName(this->baseSpriteName);
			spriteName += (this->checked ? "_on" : "_off");
			this->decor->setSprite(spriteName);
		}
		if (notifyListener)
		{
			FastGuiManager::getSingleton().getEventManager()->trigger(Event(FastGuiCheckBox::CheckBoxToggledEvent, oBadPointer(this)));
		}
	}
	//----------------------------------------------------
	void FastGuiCheckBox::toggle( bool notifyListener /*= true*/ )
	{
		this->setChecked(!this->isChecked(), notifyListener);
	}

    //----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------

    //----------------------------------------------------
    //- private: -----------------------------------------
    //----------------------------------------------------
	OABSTRACT_IMPL(FastGuiCheckBox)
		OOBJECT_END
} 