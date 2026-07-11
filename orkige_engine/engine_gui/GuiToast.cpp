/********************************************************************
	created:	Saturday 2026/07/11 at 18:45
	filename: 	GuiToast.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiToast.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	GuiToast::GuiToast(String const & id, String const & spriteName,
		uint fontIndex, String const & text, Ogre::Vector2 const & position,
		Ogre::Vector2 const & size, String const & atlas, uint z)
		: GuiWidget(id, atlas, z)
	{
		this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName,
			position, size, atlas, z));
		// a sprite-backed toast nine-slices so it resizes cleanly; a solid one
		// (empty sprite) just fills
		if(!(spriteName.empty() || spriteName == "none"))
		{
			this->decor->setNineSlice(true);
		}
		this->label = onew(new GuiLabel(id + ".label", fontIndex, text, position,
			atlas, z, true));
		this->label->setSize(size.x, size.y);
		this->label->setAlignment(GuiLabel::LA_CENTER);
	}
	//---------------------------------------------------------
	GuiToast::~GuiToast()
	{
	}
	//---------------------------------------------------------
	void GuiToast::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
	}
	//---------------------------------------------------------
	void GuiToast::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
		this->label->setSize(width, height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiToast::getSize()
	{
		return this->decor->getSize();
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiToast::getPosition()
	{
		return this->decor->getPosition();
	}
	//---------------------------------------------------------
	void GuiToast::setText(String const & text)
	{
		this->label->setText(text);
	}
	//---------------------------------------------------------
	void GuiToast::setToastAlpha(float alpha)
	{
		this->decor->setAlpha(alpha);
		this->label->setAlpha(alpha);
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiToast)
	OOBJECT_END
}
