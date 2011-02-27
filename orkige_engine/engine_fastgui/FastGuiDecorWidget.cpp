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
		if(size == Ogre::Vector2::ZERO)
		{
			Gorilla::Sprite* sprite = this->view.lock()->getScreen()->getAtlas()->getSprite(spriteName);
			oAssert(sprite);
			this->rect = this->layer->createRectangle(position.x, position.y , sprite->spriteWidth, sprite->spriteHeight);
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
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiDecorWidget)
	OOBJECT_END
}