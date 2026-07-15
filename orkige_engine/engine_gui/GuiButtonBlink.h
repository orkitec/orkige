/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	GuiButtonBlink.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec	
*********************************************************************/
#ifndef __GuiButtonBlink_h__29_10_2010__18_16_18__
#define __GuiButtonBlink_h__29_10_2010__18_16_18__

#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"
#include "engine_gui/GuiButton.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL GuiButtonBlink : public GuiButton
	{
		OOBJECT(GuiButtonBlink, GuiButton);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a button is released
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(ButtonHitEvent);

		//! enumerator values for button states
		enum ButtonBlinkState   
		{
			BBLINK_NONE,
			BBLINK_BASE,					// fade decor
			BBLINK_BASE_AND_HIGHLIGHT,		// blend decor and decor2
			BBLINK_HIGHLIGHT,				// fade decor2
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		static float blinkingTime;
	protected:
		optr<GuiDecorWidget> decor2;	//!< current button image
		ButtonBlinkState blinkState;
	private:
		//--- Methods -----------------------------------------------
	public:
		GuiButtonBlink(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool _nostate = false, int blinkState=0);
		virtual ~GuiButtonBlink();

		//! set current ButtonState, will be overwritten by cursor events
		virtual void setState(const ButtonState& bs);
		//! called every frame, set blinking
		virtual bool onFrameStarted(FrameEventData const & data);
		//! get image ui element
		inline woptr<GuiDecorWidget> getDecor2();

		//! get blinking setting
		const ButtonBlinkState& getBlinkState();
		//! set blinking setting, resets the state
		void setBlinkState(const ButtonBlinkState& blinkState);

		//! global blink duration
		static void setBlinkingTime(float blinkingTime);

	protected:
	private:
	};

}

#endif //__GuiButtonBlink_h__29_10_2010__18_16_18__