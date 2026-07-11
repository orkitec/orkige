/********************************************************************
	created:	Wednesday 2010/10/27 at 13:19
	filename: 	GuiDecorWidget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiDecorWidget::GuiDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z) : GuiWidget(id, atlas, z)
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
	GuiDecorWidget::~GuiDecorWidget()
	{
		this->layer->destroyRectangle(this->rect);
	}
	//---------------------------------------------------------
	void GuiDecorWidget::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->rect->left(left);
		this->rect->top(top);
	}
	//---------------------------------------------------------
	void GuiDecorWidget::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->rect->width(width);
		this->rect->height(height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiDecorWidget::getSize()
	{
		return Ogre::Vector2(this->rect->width(), this->rect->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiDecorWidget::getPosition()
	{
		return this->rect->position();
	}
	//---------------------------------------------------------
	void GuiDecorWidget::setSprite(String const & spriteName)
	{
		this->rect->background_image(spriteName);
	}
	//---------------------------------------------------------
	void GuiDecorWidget::setColour(float red, float green, float blue,
		float alpha)
	{
		this->rect->background_colour(Color(red, green, blue, alpha));
	}
	//---------------------------------------------------------
	Color GuiDecorWidget::getColour() const
	{
		return this->rect ? this->rect->colour() : Color(1, 1, 1, 1);
	}
	//---------------------------------------------------------
	void GuiDecorWidget::setAlpha(float alpha)
	{
		this->rect->setAlpha(alpha);
	}
	//---------------------------------------------------------
	void GuiDecorWidget::setNineSlice(bool enable)
	{
		this->rect->setDrawMode(enable ? UiRect::DM_NineSlice
			: UiRect::DM_Stretch);
	}
	//---------------------------------------------------------
	void GuiDecorWidget::setTiled(bool enable)
	{
		this->rect->setDrawMode(enable ? UiRect::DM_Tiled : UiRect::DM_Stretch);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void GuiDecorWidget::applyRenderTransform(Ui2DTransform const & transform)
	{
		if(this->rect)
		{
			this->rect->renderTransform(transform);
		}
	}
	//---------------------------------------------------------
	void GuiDecorWidget::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->rect)
		{
			this->rect->renderAlpha(alphaMultiplier);
		}
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiDecorWidget)
		// position/size accessors are inherited from GuiWidget; only the
		// decor-specific sprite/colour/alpha controls register here
		OFUNC(setSprite)
		OFUNC(setColour)
		OFUNC(setAlpha)
		OFUNC(setNineSlice)
		OFUNC(setTiled)
	OOBJECT_END
}