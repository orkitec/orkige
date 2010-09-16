/**************************************************************
	created:	2010/08/30 at 11:01
	filename: 	InputManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __InputManager_h__30_8_2010__11_01_11__
#define __InputManager_h__30_8_2010__11_01_11__

#include <core_event/GlobalEventManager.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_input/MouseEventData.h"
#include "engine_input/KeyEventData.h"

namespace Orkige
{
	//! Keyboard, Mouse and Multitouch Input Managemenet
	class ORKIGE_DLL InputManager : public Singleton<InputManager>, public Interface
	{
		OOBJECT(InputManager,Interface);
		DECL_OSINGLETON(InputManager);
		//--- Types -------------------------------------------
	public:
		/** \addtogroup EngineEvents
		*  @{ */
		//! triggered when a keyboard key is pressed
		DECL_EVENTTYPE(KeyPressedEvent);
		//! triggered when a keyboard key is released
		DECL_EVENTTYPE(KeyReleasedEvent);
		//! triggered when a mouse button key is pressed
		DECL_EVENTTYPE(MousePressedEvent);
		//! triggered when a mouse button key is released
		DECL_EVENTTYPE(MouseReleasedEvent);
		//! triggered when mouse is moved
		DECL_EVENTTYPE(MouseMovedEvent);
		/** @} End of "addtogroup EngineEvents"*/
	protected:
	private:
		bool sharedMouse;
		optr<EventListener>	frameListener;
		class InputManagerImpl* impl;
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! construct InputManager if shareMouse is true mouse input will be not exclusive to RenderWindow (for EditorMode etc.)
		InputManager(bool shareMouse = false);
		//! destructor
		virtual ~InputManager();
		//! enable input updates
		bool enable();
		//! disable input updates
		bool disable();
		//!	Translates KeyCode to String representation. For example, KC_ENTER will be "Enter" - Locale	specific of course.
		//! @param kc KeyCode to convert
		//! @returns The String as determined from the current locale
		String const & getAsString(KeyEventData::KeyCode kc);
		//! check if given key is pressed
		bool isKeyDown(KeyEventData::KeyCode kc);
		//! get current mouse data
		optr<MouseEventData> const & getMouseData() const;
		//! Set mouse region (if window resizes, we should alter this to reflect as well)
		void setWindowExtents( int width, int height );
	protected:
	private:
		bool onFrameStarted(Event const & event);
		void initialise();
		void capture( void );
	};
	//---------------------------------------------------------
}

#endif //__InputManager_h__30_8_2010__11_01_11__
