/********************************************************************
	created:	Wednesday 2010/10/27 at 13:18
	filename: 	GuiDecorWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __GuiDecorWidget_h__27_10_2010__13_18_38__
#define __GuiDecorWidget_h__27_10_2010__13_18_38__

#include "engine_gui/GuiWidget.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL GuiDecorWidget : public GuiWidget
	{
		OOBJECT(GuiDecorWidget, GuiWidget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		UiRect* rect;
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create a decor rect. An empty or "none" spriteName makes a
		//! SOLID whitepixel fill (size REQUIRED) instead of an atlas sprite -
		//! the building block for panels and dimmed pause/menu backdrops; tint
		//! and fade it with setColour/setAlpha.
		GuiDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiDecorWidget();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		void setSprite(String const & spriteName);
		//! @brief tint colour (multiplies a sprite; the solid fill colour for a
		//! spriteless decor - e.g. a semi-transparent pause scrim). 0..1 channels.
		void setColour(float red, float green, float blue, float alpha = 1.0f);
		//! @brief overall transparency of the rect (0..1); 0 = fully clear
		void setAlpha(float alpha);
		//! @brief current tint colour (the read-back a colour tween starts from)
		Color getColour() const;
		//! @brief draw the sprite nine-sliced (fixed corners, stretched edges)
		//! so a panel/button resizes without distorting its rounded corners.
		//! Needs a sprite carrying slice insets in the atlas; a no-op otherwise.
		void setNineSlice(bool enable);
		//! @brief draw the sprite tiled (repeated) across the rect
		void setTiled(bool enable);
		inline UiRect* getRectangle();
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline UiRect* GuiDecorWidget::getRectangle()
	{
		return this->rect;
	}
	//---------------------------------------------------------------
}

#endif //__GuiDecorWidget_h__27_10_2010__13_18_38__