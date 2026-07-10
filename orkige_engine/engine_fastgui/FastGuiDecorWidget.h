/********************************************************************
	created:	Wednesday 2010/10/27 at 13:18
	filename: 	FastGuiDecorWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __FastGuiDecorWidget_h__27_10_2010__13_18_38__
#define __FastGuiDecorWidget_h__27_10_2010__13_18_38__

#include "engine_fastgui/FastGuiWidget.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL FastGuiDecorWidget : public FastGuiWidget
	{
		OOBJECT(FastGuiDecorWidget, FastGuiWidget);
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
		FastGuiDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~FastGuiDecorWidget();

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
		inline UiRect* getRectangle();
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline UiRect* FastGuiDecorWidget::getRectangle()
	{
		return this->rect;
	}
	//---------------------------------------------------------------
}

#endif //__FastGuiDecorWidget_h__27_10_2010__13_18_38__