/********************************************************************
	created:	Monday 2010/11/01 at 13:45
	filename: 	GuiTextbox.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiTextbox.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiTextbox::GuiTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled) : GuiWidget(id, atlas, z)
	{
		this->markupText = this->layer->createMarkupText(defaultGlyphIndex, position.x, position.y, text);
		this->markupText->scaled(scaled);
	}
	//---------------------------------------------------------
	GuiTextbox::~GuiTextbox()
	{
		// the layer is gone if this widget outlived its view (a Lua-orphaned
		// widget finalising after its manager) - the screen already released
		// the markup text, so cleaning up would be a use-after-free
		if(this->isLayerAlive())
		{
			this->layer->destroyMarkupText(this->markupText);
		}
	}
	//---------------------------------------------------------
	void GuiTextbox::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->markupText->left(left);
		this->markupText->top(top);
	}
	//---------------------------------------------------------
	void GuiTextbox::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->markupText->width(width);
		this->markupText->height(height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiTextbox::getSize()
	{
		return Ogre::Vector2(this->markupText->width(), this->markupText->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiTextbox::getPosition()
	{
		return Ogre::Vector2(this->markupText->left(), this->markupText->top());
	}
	//---------------------------------------------------------
	void GuiTextbox::setText(String const & text)
	{
		this->markupText->text(text);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void GuiTextbox::applyRenderTransform(Ui2DTransform const & transform)
	{
		if(this->markupText)
		{
			this->markupText->renderTransform(transform);
		}
	}
	//---------------------------------------------------------
	void GuiTextbox::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->markupText)
		{
			this->markupText->renderAlpha(alphaMultiplier);
		}
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiTextbox)
	OOBJECT_END
}