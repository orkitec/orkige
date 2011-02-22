/********************************************************************
created:    Tuesday 2010/11/02 at 17:50
filename:   FastGuiCheckBox.cpp
author:     hicham.allaoui  
notice:		This source file is part of orkige (orkitec Game engine)
			For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2010 orkitec
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
    FastGuiCheckBox::FastGuiCheckBox(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z) : FastGuiWidget(id, atlas, z)
    {
		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		this->checkSymbol	 = onew(new FastGuiDecorWidget("checkSymbol.decor", "checkbox_off", position, size, atlas, z+2));

		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		//this->label->setAlignment(textAlignment);

		Ogre::Real xPosition = this->decor->getPosition().x+(this->decor->getSize().x )- (this->checkSymbol->getSize().x) - FASTGUICHECKBOX_MARGING ;
		Ogre::Real yPosition = this->decor->getPosition().y+(this->decor->getSize().y/2.f )- (this->checkSymbol->getSize().y/2.f) ;
		this->checkSymbol->setPosition(xPosition, yPosition);

		xPosition = this->decor->getPosition().x + FASTGUICHECKBOX_MARGING;
		this->label->setPosition(xPosition,yPosition);


		this->baseSpriteName = spriteName;
		this->checked = false;
    }
    //----------------------------------------------------
    FastGuiCheckBox::~FastGuiCheckBox()
    {
	}   
	//----------------------------------------------------
	void FastGuiCheckBox::setPosition( Ogre::Real left, Ogre::Real top )
	{
		this->decor->setPosition(left, top);
		//this->label->setPosition(left, top);
		Ogre::Real xPosition = this->decor->getPosition().x+(this->decor->getSize().x )- (this->checkSymbol->getSize().x) - FASTGUICHECKBOX_MARGING ;
		Ogre::Real yPosition = this->decor->getPosition().y+(this->decor->getSize().y/2.f )- (this->checkSymbol->getSize().y/2.f) ;
		this->checkSymbol->setPosition(xPosition, yPosition);
		xPosition = this->decor->getPosition().x + FASTGUICHECKBOX_MARGING;
		this->label->setPosition(xPosition,yPosition);



	}
	//----------------------------------------------------
	void FastGuiCheckBox::setSize( Ogre::Real width, Ogre::Real height )
	{
		this->decor->setSize(width, height);
		this->label->setSize(width, height);
		this->checkSymbol->setSize(width, height);
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
	bool FastGuiCheckBox::isChecked()
	{
		return this->checked ;
	}
	//----------------------------------------------------
	void FastGuiCheckBox::setChecked( bool checked, bool notifyListener /*= true*/ )
	{
		this->checked = checked;
		if (this->checked)
		{
			this->checkSymbol->setSprite("checkbox_on");
		}
		else
		{
			this->checkSymbol->setSprite("checkbox_off");
		}
		if (notifyListener)
		{
			FastGuiManager::getSingleton().getEventManager()->trigger(Event(FastGuiCheckBox::CheckBoxToggledEvent, oBadPointer(this)));
		}
	}
	//----------------------------------------------------
	void FastGuiCheckBox::toggle( bool notifyListener /*= true*/ )
	{
		setChecked(!isChecked(),notifyListener);
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