/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiLabel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiLabel.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiLabel::FastGuiLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z) : FastGuiWidget(id, atlas, z)
	{
		this->caption = this->layer->createCaption(defaultGlyphIndex, position.x, position.y, text );
	}
	//---------------------------------------------------------
	FastGuiLabel::~FastGuiLabel()
	{
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
		return Ogre::Vector2(this->caption->width(), this->caption->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiLabel::getPosition()
	{
		return Ogre::Vector2(this->caption->left(), this->caption->top());
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