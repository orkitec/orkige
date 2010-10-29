/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiButton.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __FastGuiButton_h__29_10_2010__18_16_18__
#define __FastGuiButton_h__29_10_2010__18_16_18__

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiLabel.h"

namespace Orkige
{
	class FastGuiButton : public FastGuiWidget
	{
		OOBJECT(FastGuiButton, FastGuiWidget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a button is released
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(ButtonHitEvent);

		//! enumerator values for button states
		enum ButtonState   
		{
			BS_UP,
			BS_OVER,
			BS_DOWN
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<FastGuiLabel> label;
		optr<FastGuiDecorWidget> decor;
		Ogre::Vector2 textOffset;
	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, Ogre::Vector2 const & textOffset, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~FastGuiButton();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__FastGuiButton_h__29_10_2010__18_16_18__