/**************************************************************
	created:	2010/08/30 at 11:01
	filename: 	InputManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __InputManager_h__30_8_2010__11_01_11__
#define __InputManager_h__30_8_2010__11_01_11__

#include <core_event/GlobalEventManager.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_input/MouseEventData.h"
#include "engine_input/KeyEventData.h"
#include "engine_input/TouchEventData.h"
#include "engine_input/AccelerationEventData.h"
#include "engine_input/GestureEventData.h"

// forward declaration of the SDL3 event union (SDL3 declares it as
// "typedef union SDL_Event SDL_Event;" so this stays compatible)
union SDL_Event;

namespace Orkige
{
	//! Keyboard, Mouse and Multitouch Input Management.
	//! Since the OIS to SDL3 port the InputManager no longer polls devices
	//! itself: the application owns the SDL event loop and feeds every
	//! polled event into injectEvent(), which translates it to the Orkige
	//! input events below and triggers them through the GlobalEventManager.
	class ORKIGE_ENGINE_DLL InputManager : public Singleton<InputManager>, public Interface
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
		//! triggered when a finger touches the screen
		DECL_EVENTTYPE(TouchPressedEvent);
		//! triggered when a finger stops touching the screen
		DECL_EVENTTYPE(TouchReleasedEvent);
		//! triggered when finger is moved
		DECL_EVENTTYPE(TouchMovedEvent);
		//! The system cancelled tracking for the touch, as when (for example) the user puts the device to his or her face.
		DECL_EVENTTYPE(TouchCancelledEvent);
		//! triggered when a gesture starts
		DECL_EVENTTYPE(GestureBeganEvent);
		//! triggered when a gesture ends
		DECL_EVENTTYPE(GestureEndedEvent);
		//! triggered when a gesture is cancelled
		DECL_EVENTTYPE(GestureCancelledEvent);
		//! triggered when Accelerometer changes
		DECL_EVENTTYPE(AccelerationEvent);
		//! @brief triggered when SDL delivers committed text (SDL_EVENT_TEXT_INPUT)
		//! while text input is active - the composed, locale/shift-aware UTF-8
		//! characters a TextEntry widget inserts. The KeyEventData carries the
		//! text in its `textInput` field (its `key` is unset).
		DECL_EVENTTYPE(TextInputEvent);
		/** @} End of "addtogroup EngineEvents"*/

		//--- tilt (accelerometer + desktop simulation) --------
		//! radians-per-second the simulated tilt turns while a steer key is held
		static const float TILT_SIM_RATE;
		//! clamp of the simulated tilt angle (radians, ~70 degrees)
		static const float TILT_SIM_MAX_ANGLE;
	protected:
	private:
		bool sharedMouse;
		bool enabled;
		optr<EventListener>	frameListener;
		class InputManagerImpl* impl;
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! construct InputManager. Both parameters are kept for API compatibility
		//! with the OIS era: SDL3 never grabs the mouse exclusively, so shareMouse
		//! is implicit, and enableNativeInput only controls whether the window
		//! extents get read from the Engine RenderWindow on construction.
		InputManager(bool shareMouse = false, bool enableNativeInput = true);
		//! destructor
		virtual ~InputManager();
		//! enable input updates
		bool enable();
		//! disable input updates
		bool disable();
		//! Translates one SDL event into the matching Orkige input event(s) and
		//! triggers them through the GlobalEventManager. Feed every event of the
		//! applications SDL poll loop in here.
		//! @returns true if the event was translated, false if it was ignored
		bool injectEvent(SDL_Event const & event);
		//!	Translates KeyCode to String representation. For example, KC_RETURN will be "Return" - Locale	specific of course.
		//! @param kc KeyCode to convert
		//! @returns The String as determined from the current locale
		String const & getAsString(KeyEventData::KeyCode kc);
		//! check if given key is pressed
		bool isKeyDown(KeyEventData::KeyCode kc);

		//--- text input (SDL text-input session; the TextEntry widget) --------
		//! @brief begin an SDL text-input session on the active window: SDL then
		//! delivers SDL_EVENT_TEXT_INPUT (routed to TextInputEvent) and raises the
		//! on-screen keyboard on mobile. Idempotent; a no-op without an active
		//! window (e.g. the editor never registers one for the player).
		void startTextInput();
		//! @brief end the text-input session (hides the mobile keyboard). A no-op
		//! when no session is active.
		void stopTextInput();
		//! is a text-input session currently active
		bool isTextInputActive() const;
		//! get current mouse data
		optr<MouseEventData> const & getMouseData() const;
		//! get last touch event data
		optr<TouchEventData> const & getLastTouchData() const;
		//! Set mouse region / touch scaling (if window resizes, we should alter this to reflect as well)
		void setWindowExtents( int width, int height );

		//--- tilt: the "gravity direction" input of tilt-controlled games ----
		//! @brief the current tilt as a NORMALIZED gravity direction in
		//! screen/world space; (0,-1,0) = device upright / no tilt, z always 0.
		//! @remarks SENSOR-BACKED where an accelerometer exists (SDL3
		//! SDL_SENSOR_ACCEL, opened in initialise(); every SDL_EVENT_SENSOR_UPDATE
		//! also feeds the classic AccelerationEvent) and SIMULATION-BACKED on
		//! desktops: holding LEFT/A / RIGHT/D turns a virtual tilt angle at
		//! TILT_SIM_RATE (advanced once per frame on FrameStartedEvent), clamped
		//! to +-TILT_SIM_MAX_ANGLE. Games poll this and derive gravity, e.g.
		//! physics:setGravity(tilt * 9.81) - the tilt-gravity mechanic.
		Ogre::Vector3 getTilt() const;
		//! is a real accelerometer feeding getTilt (false = key simulation)
		bool isTiltSensorAvailable() const;
		//! @brief force the SIMULATED tilt angle in radians (0 = upright);
		//! no effect while a real sensor drives the tilt
		void setTiltAngle(float radians);
		//! the current simulated tilt angle in radians
		float getTiltAngle() const;
		//! @brief pure simulation step: turn angleRadians toward the held
		//! steer key at TILT_SIM_RATE, clamped to +-TILT_SIM_MAX_ANGLE
		static float advanceTiltAngle(float angleRadians, bool steerLeft,
			bool steerRight, float deltaTime);
		//! @brief pure mapping: tilt angle -> normalized gravity direction;
		//! 0 = (0,-1,0), positive angles tilt toward +X
		static Ogre::Vector3 tiltVectorFromAngle(float angleRadians);

		//--- tilt calibration (neutral-pose capture) ----------
		//! @brief capture the CURRENT (raw, un-calibrated) tilt pose as the new
		//! neutral: every subsequent getTilt() rotates the raw gravity direction
		//! so this pose reads as upright (0,-1,0). Works on a device (captures
		//! the live accelerometer pose) and on desktop (captures the current
		//! simulated angle). Auto-persists when a calibration save file is set.
		void calibrateTilt();
		//! @brief drop the calibration - getTilt() returns the raw pose again
		//! (auto-persists the cleared state when a save file is set)
		void clearTiltCalibration();
		//! the current calibration offset in radians (0 = none); for a settings UI
		float getTiltCalibration() const;
		//! set the calibration offset directly (persistence load path)
		void setTiltCalibration(float radians);
		//! @brief set the file the calibration persists to ("" disables it);
		//! mirrors LevelManager::setSaveFile - the player sets a per-device path,
		//! the editor never does (so calibration persistence is an honest no-op
		//! in edit mode). Calibration is engine-input state, kept in its own file.
		void setCalibrationSaveFile(String const & path);
		//! @brief write the calibration save (magic + version + the offset);
		//! false (no-op) when no save file is set
		bool saveCalibration() const;
		//! @brief load the calibration save; a missing/garbage/too-new file is an
		//! honest fallback to no calibration and returns false without disturbing
		//! anything else
		bool loadCalibration();
	protected:
	private:
		bool onFrameStarted(Event const & event);
		void initialise();
		void capture( void );
		//! the raw (un-calibrated) gravity direction; getTilt applies the
		//! calibration offset to it, calibrateTilt captures it
		Ogre::Vector3 rawTilt() const;
	};
	//---------------------------------------------------------
}

#endif //__InputManager_h__30_8_2010__11_01_11__
