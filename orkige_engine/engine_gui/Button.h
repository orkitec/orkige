/********************************************************************
	created:	Wednesday 2010/08/04 at 15:05
	filename: 	Button.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __Button_h__4_8_2010__15_05_33__
#define __Button_h__4_8_2010__15_05_33__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Basic Button Widget
	class Button : public Widget
	{
		OOBJECT(Button, Widget);
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
		ButtonState state;								//!< current button state
		Ogre::BorderPanelOverlayElement* borderPanel;	//!< button border
		Ogre::TextAreaOverlayElement* textArea;			//!< button text
		bool fitToContents;								//!< if true button size depends on Textlength
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create a button
		//! @copydoc Widget
		Button(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width);
		//! destructor
		virtual ~Button();
		//! get button caption
		const Ogre::DisplayString& getCaption();
		//! set button caption
		void setCaption(const Ogre::DisplayString& caption);
		//! get current ButtonState
		const ButtonState& getState();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
		virtual void onFocusLost();
	protected:
		//! set current ButtonState 
		void setState(const ButtonState& bs);
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__Button_h__4_8_2010__15_05_33__