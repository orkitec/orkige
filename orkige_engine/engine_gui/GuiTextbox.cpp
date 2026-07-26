/********************************************************************
	created:	Monday 2010/11/01 at 13:45
	filename: 	GuiTextbox.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiTextbox.h"
#include "engine_gui/GuiManager.h"
#include <functional>

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
		// wrap fits the box width (markupText->width is an OUTPUT extent, so the
		// wrap width rides a dedicated channel the layout resolver feeds)
		this->markupText->wrapWidth(width);
	}
	//---------------------------------------------------------
	void GuiTextbox::setWrap(bool wrap)
	{
		this->markupText->setWrap(wrap);
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	bool GuiTextbox::getWrap() const
	{
		return this->markupText->getWrap();
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiTextbox::getPreferredSize()
	{
		this->markupText->_calculateCharacters();
		Ogre::Vector2 size(this->markupText->width(), this->markupText->height());
		if(this->markupText->getWrap())
		{
			size.y = this->markupText->measureWrappedHeight(
				this->markupText->wrapWidth());
		}
		return size;
	}
	//---------------------------------------------------------
	std::function<float(float)> GuiTextbox::getHeightForWidthMeasurer()
	{
		if(!this->markupText->getWrap())
		{
			return {};
		}
		UiMarkupText* markup = this->markupText;
		return [markup](float width) -> float
		{
			return float(markup->measureWrappedHeight(Ogre::Real(width)));
		};
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
		OFUNC(setWrap)
	OOBJECT_END
}