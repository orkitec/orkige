/********************************************************************
	created:	Wednesday 2010/10/27 at 13:19
	filename: 	FastGuiDecorWidget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiDecorWidget::FastGuiDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z) : FastGuiWidget(id, atlas, z)
	{
		oAssert(this->layer);
		// empty / "none" sprite -> a SOLID whitepixel fill (panels, dimmed
		// pause/menu backdrops); a named sprite -> the atlas region
		const bool solid = spriteName.empty() || spriteName == "none";
		if(solid)
		{
			oAssertDesc(size != Ogre::Vector2::ZERO,
				"A spriteless (solid) decor widget needs an explicit size");
			this->rect = this->layer->createRectangle(position, size);
			oAssert(this->rect);
			this->rect->background_image("none");	// solid whitepixel fill
			return;
		}
		UiSprite const * sprite = this->view.lock()->getScreen()->getAtlas()->getSprite(spriteName);
		oAssertDesc(sprite, "Warning: sprite not found: " << spriteName);

		if(size == Ogre::Vector2::ZERO)
		{
			this->rect = this->layer->createRectangle(position.x, position.y, sprite->spriteWidth, sprite->spriteHeight);
		}
		else
		{
			this->rect = this->layer->createRectangle(position, size);
		}

		oAssert(this->rect);
		this->rect->background_image(spriteName);
	}
	//---------------------------------------------------------
	FastGuiDecorWidget::~FastGuiDecorWidget()
	{
		this->layer->destroyRectangle(this->rect);
	}
	//---------------------------------------------------------
	void FastGuiDecorWidget::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->rect->left(left);
		this->rect->top(top);
	}
	//---------------------------------------------------------
	void FastGuiDecorWidget::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->rect->width(width);
		this->rect->height(height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiDecorWidget::getSize()
	{
		return Ogre::Vector2(this->rect->width(), this->rect->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiDecorWidget::getPosition()
	{
		return this->rect->position();
	}
	//---------------------------------------------------------
	void FastGuiDecorWidget::setSprite(String const & spriteName)
	{
		this->rect->background_image(spriteName);
	}
	//---------------------------------------------------------
	void FastGuiDecorWidget::setColour(float red, float green, float blue,
		float alpha)
	{
		this->rect->background_colour(Color(red, green, blue, alpha));
	}
	//---------------------------------------------------------
	void FastGuiDecorWidget::setAlpha(float alpha)
	{
		this->rect->setAlpha(alpha);
	}
	//---------------------------------------------------------
	void FastGuiDecorWidget::setNineSlice(bool enable)
	{
		this->rect->setDrawMode(enable ? UiRect::DM_NineSlice
			: UiRect::DM_Stretch);
	}
	//---------------------------------------------------------
	void FastGuiDecorWidget::setTiled(bool enable)
	{
		this->rect->setDrawMode(enable ? UiRect::DM_Tiled : UiRect::DM_Stretch);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiDecorWidget)
		// position/size accessors are inherited from FastGuiWidget; only the
		// decor-specific sprite/colour/alpha controls register here
		OFUNC(setSprite)
		OFUNC(setColour)
		OFUNC(setAlpha)
		OFUNC(setNineSlice)
		OFUNC(setTiled)
	OOBJECT_END
}