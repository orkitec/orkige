/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	GuiButton.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec	
*********************************************************************/
#ifndef __GuiButton_h__29_10_2010__18_16_18__
#define __GuiButton_h__29_10_2010__18_16_18__
#include "engine_module/EnginePrerequisites.h"
#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL GuiButton : public GuiWidget
	{
		OOBJECT(GuiButton, GuiWidget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a button is released
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(ButtonHitEvent);

		//! enumerator values for button states
		enum ButtonState   
		{
			BS_DISABLED,
			BS_UP,
			BS_OVER,
			BS_DOWN
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<GuiLabel> label;			//!< current button text
		optr<GuiDecorWidget> decor;		//!< current button image
		ButtonState state;					//!< current button state
		String baseSpriteName;				//!< base name of the button state sprite;
		bool nostate;
		bool clicked;						//!< a completed click since the last wasClicked poll
		bool pressFeedback;					//!< opt-in scale-down-on-press juice
	private:
		//--- Methods -----------------------------------------------
	public:
		GuiButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z,bool _nostate = false);
		virtual ~GuiButton();

		//! get current ButtonState
		const ButtonState& getState();
		//! set current ButtonState, will be overwritten by cursor events
		virtual void setState(const ButtonState& bs);

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);

		//! get text holding ui element
		inline woptr<GuiLabel> getLabel();
		//! get image ui element
		inline woptr<GuiDecorWidget> getDecor();
		//! get button text
		String getCaption();
		//! set button text
		void setCaption(String const & text);
		//! @brief poll-and-consume the click state: true once after every
		//! completed press+release on the button (the polled alternative to
		//! listening for ButtonHitEvent - scripts poll this every frame)
		bool wasClicked();
		//! @brief draw the button image nine-sliced so it resizes without
		//! distorting its rounded corners (needs slice insets on the sprite)
		void setNineSlice(bool enable);
		//! @brief draw the button image tiled across its rect
		void setTiled(bool enable);
		//! @brief opt in to built-in press feedback: the button scales down a
		//! touch on press and springs back (a slight overshoot) on release - the
		//! standard tactile "juice". Off by default.
		void setPressFeedback(bool enable);
		inline bool getPressFeedback() const { return this->pressFeedback; }
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);

	protected:
		//! disabled -> the BS_DISABLED sprite (`_disabled`), enabled -> BS_UP
		virtual void onEnabledChanged(bool enable);
	private:
	};
	//---------------------------------------------------------------
	inline woptr<GuiLabel> GuiButton::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<GuiDecorWidget> GuiButton::getDecor()
	{
		return this->decor;
	}
}

#endif //__GuiButton_h__29_10_2010__18_16_18__