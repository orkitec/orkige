/********************************************************************
	created:	Monday 2010/11/01 at 13:45
	filename: 	FastGuiTextbox.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiTextbox.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiTextbox::FastGuiTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled) : FastGuiWidget(id, atlas, z)
	{
		this->markupText = this->layer->createMarkupText(defaultGlyphIndex, position.x, position.y, text);
		this->markupText->scaled(scaled);
	}
	//---------------------------------------------------------
	FastGuiTextbox::~FastGuiTextbox()
	{
		this->layer->destroyMarkupText(this->markupText);
	}
	//---------------------------------------------------------
	void FastGuiTextbox::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->markupText->left(left);
		this->markupText->top(top);
	}
	//---------------------------------------------------------
	void FastGuiTextbox::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->markupText->width(width);
		this->markupText->height(height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiTextbox::getSize()
	{
		return Ogre::Vector2(this->markupText->width(), this->markupText->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiTextbox::getPosition()
	{
		return Ogre::Vector2(this->markupText->left(), this->markupText->top());
	}
	//---------------------------------------------------------
	void FastGuiTextbox::setText(String const & text)
	{
		this->markupText->text(text);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiTextbox)
	OOBJECT_END
}