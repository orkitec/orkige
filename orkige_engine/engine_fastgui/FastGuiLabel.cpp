/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiLabel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiLabel.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiLabel::FastGuiLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z) : 
		FastGuiWidget(id, atlas, z)
	{
		this->caption = this->layer->createCaption(defaultGlyphIndex, position.x, position.y, text);
	}
	//---------------------------------------------------------
	FastGuiLabel::~FastGuiLabel()
	{
		this->layer->destroyCaption(this->caption);
		//this->layer->destroyAllCaptions();
	}
	//---------------------------------------------------------
	void FastGuiLabel::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->caption->left(left);
		this->caption->top(top);
	}
	//---------------------------------------------------------
	void FastGuiLabel::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->caption->width(width);
		this->caption->height(height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiLabel::getSize()
	{
		Ogre::Vector2 size;
		this->caption->_calculateDrawSize(size);
		return size;
		//return Ogre::Vector2(this->caption->width(), this->caption->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiLabel::getPosition()
	{
		return Ogre::Vector2(this->caption->left(), this->caption->top());
	}
	//---------------------------------------------------------
	void FastGuiLabel::setText(String const & text)
	{
		this->caption->text(text);
	}
	//---------------------------------------------------------
	void FastGuiLabel::setAlignment(LabelAlignment alignment)
	{
		switch(alignment)
		{
		case LA_TOPLEFT:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Top);
				this->caption->align(Gorilla::TextAlign_Left);
			} break;
		case LA_TOP:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Top);
				this->caption->align(Gorilla::TextAlign_Centre);
			} break;
		case LA_TOPRIGHT:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Top);
				this->caption->align(Gorilla::TextAlign_Right);
			} break;
		case LA_LEFT:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Middle);
				this->caption->align(Gorilla::TextAlign_Left);
			} break;
		case LA_CENTER:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Middle);
				this->caption->align(Gorilla::TextAlign_Centre);
			} break;
		case LA_RIGHT:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Middle);
				this->caption->align(Gorilla::TextAlign_Right);
			} break;
		case LA_BOTTOMLEFT:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Bottom);
				this->caption->align(Gorilla::TextAlign_Left);
			} break;
		case LA_BOTTOM:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Bottom);
				this->caption->align(Gorilla::TextAlign_Centre);
			} break;
		case LA_BOTTOMRIGHT:
			{
				this->caption->vertical_align(Gorilla::VerticalAlign_Bottom);
				this->caption->align(Gorilla::TextAlign_Right);
			} break;
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiLabel)
	OOBJECT_END
}