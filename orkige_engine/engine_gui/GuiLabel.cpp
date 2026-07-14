/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	GuiLabel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiLabel.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiLabel::GuiLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled) : 
		GuiWidget(id, atlas, z)
	{
		this->caption = this->layer->createCaption(defaultGlyphIndex, position.x, position.y, text);
		this->caption->scaled(scaled);
	}
	//---------------------------------------------------------
	GuiLabel::~GuiLabel()
	{
		// the layer is gone if this widget outlived its view (a Lua-orphaned
		// widget finalising after its manager) - the screen already released
		// the caption, so cleaning up would be a use-after-free
		if(this->isLayerAlive())
		{
			this->layer->destroyCaption(this->caption);
		}
	}
	//---------------------------------------------------------
	void GuiLabel::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->caption->left(left);
		this->caption->top(top);
	}
	//---------------------------------------------------------
	void GuiLabel::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->caption->width(width);
		this->caption->height(height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiLabel::getSize()
	{
		Ogre::Vector2 size;
		this->caption->_calculateDrawSize(size);
		return size;
		//return Ogre::Vector2(this->caption->width(), this->caption->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiLabel::getPosition()
	{
		return Ogre::Vector2(this->caption->left(), this->caption->top());
	}
	//---------------------------------------------------------
	void GuiLabel::setText(String const & text)
	{
		this->caption->text(text);
	}
	//---------------------------------------------------------
	void GuiLabel::setAlignment(LabelAlignment alignment)
	{
		switch(alignment)
		{
		case LA_TOPLEFT:
			{
				this->caption->vertical_align(VerticalAlign_Top);
				this->caption->align(TextAlign_Left);
			} break;
		case LA_TOP:
			{
				this->caption->vertical_align(VerticalAlign_Top);
				this->caption->align(TextAlign_Centre);
			} break;
		case LA_TOPRIGHT:
			{
				this->caption->vertical_align(VerticalAlign_Top);
				this->caption->align(TextAlign_Right);
			} break;
		case LA_LEFT:
			{
				this->caption->vertical_align(VerticalAlign_Middle);
				this->caption->align(TextAlign_Left);
			} break;
		case LA_CENTER:
			{
				this->caption->vertical_align(VerticalAlign_Middle);
				this->caption->align(TextAlign_Centre);
			} break;
		case LA_RIGHT:
			{
				this->caption->vertical_align(VerticalAlign_Middle);
				this->caption->align(TextAlign_Right);
			} break;
		case LA_BOTTOMLEFT:
			{
				this->caption->vertical_align(VerticalAlign_Bottom);
				this->caption->align(TextAlign_Left);
			} break;
		case LA_BOTTOM:
			{
				this->caption->vertical_align(VerticalAlign_Bottom);
				this->caption->align(TextAlign_Centre);
			} break;
		case LA_BOTTOMRIGHT:
			{
				this->caption->vertical_align(VerticalAlign_Bottom);
				this->caption->align(TextAlign_Right);
			} break;
		}
	}
	//---------------------------------------------------------
	void GuiLabel::setAlpha(float alpha)
	{
		Color colour = this->caption->colour();
		colour.a = alpha;
		this->caption->colour(colour);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void GuiLabel::applyRenderTransform(Ui2DTransform const & transform)
	{
		if(this->caption)
		{
			this->caption->renderTransform(transform);
		}
	}
	//---------------------------------------------------------
	void GuiLabel::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->caption)
		{
			this->caption->renderAlpha(alphaMultiplier);
		}
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiLabel)
		OFUNC(setText)
		OFUNC(setAlignment)
		OFUNC(setAlpha)
		OENUM_START(LabelAlignment)
			OENUM_VALUE(LA_TOPLEFT)
			OENUM_VALUE(LA_TOP)
			OENUM_VALUE(LA_TOPRIGHT)
			OENUM_VALUE(LA_LEFT)
			OENUM_VALUE(LA_CENTER)
			OENUM_VALUE(LA_RIGHT)
			OENUM_VALUE(LA_BOTTOMLEFT)
			OENUM_VALUE(LA_BOTTOM)
			OENUM_VALUE(LA_BOTTOMRIGHT)
		OENUM_END
	OOBJECT_END
}