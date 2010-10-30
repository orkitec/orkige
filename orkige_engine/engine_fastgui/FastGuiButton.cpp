/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiButton.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiButton.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(FastGuiButton, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiButton::FastGuiButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z) : FastGuiWidget(id, atlas, z)
	{
		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z));
		this->label->setSize(size.x, size.y);
		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		this->label->SetAlignment(textAlignment);
	}
	//---------------------------------------------------------
	FastGuiButton::~FastGuiButton()
	{
	}
	//---------------------------------------------------------
	void FastGuiButton::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
	}
	//---------------------------------------------------------
	void FastGuiButton::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
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
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiButton)
	OOBJECT_END
}