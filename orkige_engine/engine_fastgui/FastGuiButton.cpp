/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiButton.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiButton.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_event/GlobalEventManager.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(FastGuiButton, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiButton::FastGuiButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z,bool _nostate) : FastGuiWidget(id, atlas, z)
	{
		this->nostate = _nostate ;
		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		
		if(!text.empty())
		{
			this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z));
			this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
			this->label->setAlignment(textAlignment);
		}


		this->state = BS_UP;
		this->baseSpriteName = spriteName;
	}
	//---------------------------------------------------------
	FastGuiButton::~FastGuiButton()
	{
	}
	//---------------------------------------------------------
	const FastGuiButton::ButtonState& FastGuiButton::getState() 
	{ 
		return this->state; 
	}
	//---------------------------------------------------------
	void FastGuiButton::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		if(label) 
			this->label->setPosition(left, top);
	}
	//---------------------------------------------------------
	void FastGuiButton::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
		if(label) 
			this->label->setSize(width, height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiButton::getSize()
	{
		return this->decor->getSize();
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiButton::getPosition()
	{
		return this->decor->getPosition();
	}
	//---------------------------------------------------------
	void FastGuiButton::onCursorPressed(Ogre::Vector2 const & cursorPos)
	{
		if (this->decor->getRectangle()->intersects(cursorPos)) 
		{
			this->setState(FastGuiButton::BS_DOWN);
		}
	}
	//---------------------------------------------------------
	void FastGuiButton::onCursorReleased(Ogre::Vector2 const & cursorPos)
	{
		if (this->state == FastGuiButton::BS_DOWN)
		{
			this->setState(FastGuiButton::BS_OVER);
			GlobalEventManager::getSingleton().trigger(Event(FastGuiButton::ButtonHitEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	void FastGuiButton::onCursorMoved(Ogre::Vector2 const & cursorPos)
	{
		if(this->decor->getRectangle()->intersects(cursorPos))
		{
			if (this->state == FastGuiButton::BS_UP) 
			{
				this->setState(FastGuiButton::BS_OVER);
			}
		}
		else
		{
			if (this->state != FastGuiButton::BS_UP)
			{
				this->setState(FastGuiButton::BS_UP);
			}
		}
	}
	//---------------------------------------------------------
	String FastGuiButton::getCaption()
	{
		if(label)
			return this->label->getCaption()->text();	
		else
			return StringUtil::BLANK;
	}
	//---------------------------------------------------------
	void FastGuiButton::setCaption(String const & text)
	{
		if(label)
			this->label->setText(text);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void FastGuiButton::setState(const FastGuiButton::ButtonState& bs)
	{
		
		if (bs == FastGuiButton::BS_OVER)
		{
			if (!this->nostate)
			{
				this->decor->setSprite(this->baseSpriteName + "_Over");
			}
			
		}
		else if (bs == FastGuiButton::BS_UP)
		{
			if (!this->nostate)
			{
				this->decor->setSprite(this->baseSpriteName);
			}
			
		}
		else
		{
			if (!this->nostate)
			{
				this->decor->setSprite(this->baseSpriteName + "_Down");
			}
			
		}

		this->state = bs;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiButton)
	OOBJECT_END
}