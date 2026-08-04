/**************************************************************
	created:	2010/08/30 at 11:05
	filename: 	InputManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// SDL3 port (2026): the abandoned OIS library is gone. The application owns
// the SDL event loop and feeds events in via InputManager::injectEvent();
// device polling (OIS "capture") no longer exists. The legacy TUIO, browser
// plugin, iPhone (UIKit) and Android injection paths died with OIS - SDL3
// delivers touch as SDL_EVENT_FINGER_* on every platform. Gesture and
// acceleration events are still declared but currently have no SDL3 source
// (SDL3 exposes accelerometers as sensors, to be wired in the mobile phase).
#include "engine_input/InputManager.h"
#include <SDL3/SDL.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"
#include "engine_graphic/FrameEventData.h"
#include "engine_render/RenderSystem.h"
#include "engine_util/StringUtil.h"
#include "engine_util/PlatformWindow.h"
#include "core_util/TiltCalibration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

//! maximum number of simultaneously tracked touch sequences (OIS tracked 4)
#define ORKIGE_MAX_NUM_TOUCHES 10
//! maximum number of simultaneously open gamepads
#define ORKIGE_MAX_NUM_GAMEPADS 4

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(InputManager, KeyPressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, KeyReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MousePressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MouseReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MouseMovedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchPressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchMovedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchCancelledEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, GestureBeganEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, GestureEndedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, GestureCancelledEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, AccelerationEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TextInputEvent);

	IMPL_OSINGLETON(InputManager);

	//! @brief the instance id injected (synthetic) gamepad events carry: a
	//! value no platform assigns to a real device, so a virtual pad and a
	//! plugged-in one never collide in the tracking table
	static const SDL_JoystickID INJECTED_GAMEPAD_ID = 0x0F00CAFEu;

	// simulated tilt tuning: a held steer key sweeps from upright to the
	// clamp in ~0.7s; the clamp keeps "gravity" from ever pointing upward
	const float InputManager::TILT_SIM_RATE = 1.6f;			// rad/s
	const float InputManager::TILT_SIM_MAX_ANGLE = 1.2f;	// ~69 degrees

	//---------------------------------------------------------
	//! translates a SDL3 scancode to the legacy KeyEventData::KeyCode
	//! (OIS/DirectInput numbering) that the rest of the engine keeps using -
	//! this way IngameConsole, gui and the game branches compile unchanged
	static KeyEventData::KeyCode sdlScancodeToKeyCode(SDL_Scancode scancode)
	{
		switch(scancode)
		{
		case SDL_SCANCODE_ESCAPE:				return KeyEventData::KC_ESCAPE;
		case SDL_SCANCODE_1:					return KeyEventData::KC_1;
		case SDL_SCANCODE_2:					return KeyEventData::KC_2;
		case SDL_SCANCODE_3:					return KeyEventData::KC_3;
		case SDL_SCANCODE_4:					return KeyEventData::KC_4;
		case SDL_SCANCODE_5:					return KeyEventData::KC_5;
		case SDL_SCANCODE_6:					return KeyEventData::KC_6;
		case SDL_SCANCODE_7:					return KeyEventData::KC_7;
		case SDL_SCANCODE_8:					return KeyEventData::KC_8;
		case SDL_SCANCODE_9:					return KeyEventData::KC_9;
		case SDL_SCANCODE_0:					return KeyEventData::KC_0;
		case SDL_SCANCODE_MINUS:				return KeyEventData::KC_MINUS;
		case SDL_SCANCODE_EQUALS:				return KeyEventData::KC_EQUALS;
		case SDL_SCANCODE_BACKSPACE:			return KeyEventData::KC_BACK;
		case SDL_SCANCODE_TAB:					return KeyEventData::KC_TAB;
		case SDL_SCANCODE_Q:					return KeyEventData::KC_Q;
		case SDL_SCANCODE_W:					return KeyEventData::KC_W;
		case SDL_SCANCODE_E:					return KeyEventData::KC_E;
		case SDL_SCANCODE_R:					return KeyEventData::KC_R;
		case SDL_SCANCODE_T:					return KeyEventData::KC_T;
		case SDL_SCANCODE_Y:					return KeyEventData::KC_Y;
		case SDL_SCANCODE_U:					return KeyEventData::KC_U;
		case SDL_SCANCODE_I:					return KeyEventData::KC_I;
		case SDL_SCANCODE_O:					return KeyEventData::KC_O;
		case SDL_SCANCODE_P:					return KeyEventData::KC_P;
		case SDL_SCANCODE_LEFTBRACKET:			return KeyEventData::KC_LBRACKET;
		case SDL_SCANCODE_RIGHTBRACKET:			return KeyEventData::KC_RBRACKET;
		case SDL_SCANCODE_RETURN:				return KeyEventData::KC_RETURN;
		case SDL_SCANCODE_LCTRL:				return KeyEventData::KC_LCONTROL;
		case SDL_SCANCODE_A:					return KeyEventData::KC_A;
		case SDL_SCANCODE_S:					return KeyEventData::KC_S;
		case SDL_SCANCODE_D:					return KeyEventData::KC_D;
		case SDL_SCANCODE_F:					return KeyEventData::KC_F;
		case SDL_SCANCODE_G:					return KeyEventData::KC_G;
		case SDL_SCANCODE_H:					return KeyEventData::KC_H;
		case SDL_SCANCODE_J:					return KeyEventData::KC_J;
		case SDL_SCANCODE_K:					return KeyEventData::KC_K;
		case SDL_SCANCODE_L:					return KeyEventData::KC_L;
		case SDL_SCANCODE_SEMICOLON:			return KeyEventData::KC_SEMICOLON;
		case SDL_SCANCODE_APOSTROPHE:			return KeyEventData::KC_APOSTROPHE;
		case SDL_SCANCODE_GRAVE:				return KeyEventData::KC_GRAVE;
		case SDL_SCANCODE_LSHIFT:				return KeyEventData::KC_LSHIFT;
		case SDL_SCANCODE_BACKSLASH:			return KeyEventData::KC_BACKSLASH;
		case SDL_SCANCODE_Z:					return KeyEventData::KC_Z;
		case SDL_SCANCODE_X:					return KeyEventData::KC_X;
		case SDL_SCANCODE_C:					return KeyEventData::KC_C;
		case SDL_SCANCODE_V:					return KeyEventData::KC_V;
		case SDL_SCANCODE_B:					return KeyEventData::KC_B;
		case SDL_SCANCODE_N:					return KeyEventData::KC_N;
		case SDL_SCANCODE_M:					return KeyEventData::KC_M;
		case SDL_SCANCODE_COMMA:				return KeyEventData::KC_COMMA;
		case SDL_SCANCODE_PERIOD:				return KeyEventData::KC_PERIOD;
		case SDL_SCANCODE_SLASH:				return KeyEventData::KC_SLASH;
		case SDL_SCANCODE_RSHIFT:				return KeyEventData::KC_RSHIFT;
		case SDL_SCANCODE_KP_MULTIPLY:			return KeyEventData::KC_MULTIPLY;
		case SDL_SCANCODE_LALT:					return KeyEventData::KC_LMENU;
		case SDL_SCANCODE_SPACE:				return KeyEventData::KC_SPACE;
		case SDL_SCANCODE_CAPSLOCK:				return KeyEventData::KC_CAPITAL;
		case SDL_SCANCODE_F1:					return KeyEventData::KC_F1;
		case SDL_SCANCODE_F2:					return KeyEventData::KC_F2;
		case SDL_SCANCODE_F3:					return KeyEventData::KC_F3;
		case SDL_SCANCODE_F4:					return KeyEventData::KC_F4;
		case SDL_SCANCODE_F5:					return KeyEventData::KC_F5;
		case SDL_SCANCODE_F6:					return KeyEventData::KC_F6;
		case SDL_SCANCODE_F7:					return KeyEventData::KC_F7;
		case SDL_SCANCODE_F8:					return KeyEventData::KC_F8;
		case SDL_SCANCODE_F9:					return KeyEventData::KC_F9;
		case SDL_SCANCODE_F10:					return KeyEventData::KC_F10;
		case SDL_SCANCODE_NUMLOCKCLEAR:			return KeyEventData::KC_NUMLOCK;
		case SDL_SCANCODE_SCROLLLOCK:			return KeyEventData::KC_SCROLL;
		case SDL_SCANCODE_KP_7:					return KeyEventData::KC_NUMPAD7;
		case SDL_SCANCODE_KP_8:					return KeyEventData::KC_NUMPAD8;
		case SDL_SCANCODE_KP_9:					return KeyEventData::KC_NUMPAD9;
		case SDL_SCANCODE_KP_MINUS:				return KeyEventData::KC_SUBTRACT;
		case SDL_SCANCODE_KP_4:					return KeyEventData::KC_NUMPAD4;
		case SDL_SCANCODE_KP_5:					return KeyEventData::KC_NUMPAD5;
		case SDL_SCANCODE_KP_6:					return KeyEventData::KC_NUMPAD6;
		case SDL_SCANCODE_KP_PLUS:				return KeyEventData::KC_ADD;
		case SDL_SCANCODE_KP_1:					return KeyEventData::KC_NUMPAD1;
		case SDL_SCANCODE_KP_2:					return KeyEventData::KC_NUMPAD2;
		case SDL_SCANCODE_KP_3:					return KeyEventData::KC_NUMPAD3;
		case SDL_SCANCODE_KP_0:					return KeyEventData::KC_NUMPAD0;
		case SDL_SCANCODE_KP_PERIOD:			return KeyEventData::KC_DECIMAL;
		case SDL_SCANCODE_NONUSBACKSLASH:		return KeyEventData::KC_OEM_102;
		case SDL_SCANCODE_F11:					return KeyEventData::KC_F11;
		case SDL_SCANCODE_F12:					return KeyEventData::KC_F12;
		case SDL_SCANCODE_F13:					return KeyEventData::KC_F13;
		case SDL_SCANCODE_F14:					return KeyEventData::KC_F14;
		case SDL_SCANCODE_F15:					return KeyEventData::KC_F15;
		case SDL_SCANCODE_INTERNATIONAL1:		return KeyEventData::KC_ABNT_C1;
		case SDL_SCANCODE_INTERNATIONAL3:		return KeyEventData::KC_YEN;
		case SDL_SCANCODE_KP_EQUALS:			return KeyEventData::KC_NUMPADEQUALS;
		case SDL_SCANCODE_MEDIA_PREVIOUS_TRACK:	return KeyEventData::KC_PREVTRACK;
		case SDL_SCANCODE_MEDIA_NEXT_TRACK:		return KeyEventData::KC_NEXTTRACK;
		case SDL_SCANCODE_KP_ENTER:				return KeyEventData::KC_NUMPADENTER;
		case SDL_SCANCODE_RCTRL:				return KeyEventData::KC_RCONTROL;
		case SDL_SCANCODE_MUTE:					return KeyEventData::KC_MUTE;
		case SDL_SCANCODE_MEDIA_PLAY:			return KeyEventData::KC_PLAYPAUSE;
		case SDL_SCANCODE_MEDIA_STOP:			return KeyEventData::KC_MEDIASTOP;
		case SDL_SCANCODE_VOLUMEDOWN:			return KeyEventData::KC_VOLUMEDOWN;
		case SDL_SCANCODE_VOLUMEUP:				return KeyEventData::KC_VOLUMEUP;
		case SDL_SCANCODE_AC_HOME:				return KeyEventData::KC_WEBHOME;
		case SDL_SCANCODE_KP_COMMA:				return KeyEventData::KC_NUMPADCOMMA;
		case SDL_SCANCODE_KP_DIVIDE:			return KeyEventData::KC_DIVIDE;
		case SDL_SCANCODE_PRINTSCREEN:			return KeyEventData::KC_SYSRQ;
		case SDL_SCANCODE_RALT:					return KeyEventData::KC_RMENU;
		case SDL_SCANCODE_PAUSE:				return KeyEventData::KC_PAUSE;
		case SDL_SCANCODE_HOME:					return KeyEventData::KC_HOME;
		case SDL_SCANCODE_UP:					return KeyEventData::KC_UP;
		case SDL_SCANCODE_PAGEUP:				return KeyEventData::KC_PGUP;
		case SDL_SCANCODE_LEFT:					return KeyEventData::KC_LEFT;
		case SDL_SCANCODE_RIGHT:				return KeyEventData::KC_RIGHT;
		case SDL_SCANCODE_END:					return KeyEventData::KC_END;
		case SDL_SCANCODE_DOWN:					return KeyEventData::KC_DOWN;
		case SDL_SCANCODE_PAGEDOWN:				return KeyEventData::KC_PGDOWN;
		case SDL_SCANCODE_INSERT:				return KeyEventData::KC_INSERT;
		case SDL_SCANCODE_DELETE:				return KeyEventData::KC_DELETE;
		case SDL_SCANCODE_LGUI:					return KeyEventData::KC_LWIN;
		case SDL_SCANCODE_RGUI:					return KeyEventData::KC_RWIN;
		case SDL_SCANCODE_APPLICATION:			return KeyEventData::KC_APPS;
		case SDL_SCANCODE_POWER:				return KeyEventData::KC_POWER;
		case SDL_SCANCODE_SLEEP:				return KeyEventData::KC_SLEEP;
		case SDL_SCANCODE_AC_SEARCH:			return KeyEventData::KC_WEBSEARCH;
		case SDL_SCANCODE_AC_BOOKMARKS:			return KeyEventData::KC_WEBFAVORITES;
		case SDL_SCANCODE_AC_REFRESH:			return KeyEventData::KC_WEBREFRESH;
		case SDL_SCANCODE_AC_STOP:				return KeyEventData::KC_WEBSTOP;
		case SDL_SCANCODE_AC_FORWARD:			return KeyEventData::KC_WEBFORWARD;
		case SDL_SCANCODE_AC_BACK:				return KeyEventData::KC_WEBBACK;
		case SDL_SCANCODE_MEDIA_SELECT:			return KeyEventData::KC_MEDIASELECT;
		default:								return KeyEventData::KC_UNASSIGNED;
		}
	}
	//---------------------------------------------------------
	//! reverse lookup, built once from sdlScancodeToKeyCode so both stay in sync
	static SDL_Scancode keyCodeToSdlScancode(KeyEventData::KeyCode kc)
	{
		static SDL_Scancode table[256];
		static bool initialized = false;
		if(!initialized)
		{
			for(int each = 0; each < 256; each++)
			{
				table[each] = SDL_SCANCODE_UNKNOWN;
			}
			for(int each = 0; each < SDL_SCANCODE_COUNT; each++)
			{
				KeyEventData::KeyCode mapped = sdlScancodeToKeyCode(static_cast<SDL_Scancode>(each));
				if(mapped != KeyEventData::KC_UNASSIGNED && table[mapped] == SDL_SCANCODE_UNKNOWN)
				{
					table[mapped] = static_cast<SDL_Scancode>(each);
				}
			}
			initialized = true;
		}
		return table[static_cast<unsigned int>(kc) & 0xFF];
	}
	//---------------------------------------------------------
	//! the engine's positional gamepad button for an SDL one (GB_COUNT = a
	//! button outside the standard layout, e.g. a vendor's extra paddle)
	static Gamepad::Button sdlGamepadButton(SDL_GamepadButton button)
	{
		switch(button)
		{
		case SDL_GAMEPAD_BUTTON_SOUTH:			return Gamepad::GB_SOUTH;
		case SDL_GAMEPAD_BUTTON_EAST:			return Gamepad::GB_EAST;
		case SDL_GAMEPAD_BUTTON_WEST:			return Gamepad::GB_WEST;
		case SDL_GAMEPAD_BUTTON_NORTH:			return Gamepad::GB_NORTH;
		case SDL_GAMEPAD_BUTTON_BACK:			return Gamepad::GB_BACK;
		case SDL_GAMEPAD_BUTTON_GUIDE:			return Gamepad::GB_GUIDE;
		case SDL_GAMEPAD_BUTTON_START:			return Gamepad::GB_START;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK:		return Gamepad::GB_LEFTSTICK;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK:	return Gamepad::GB_RIGHTSTICK;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:	return Gamepad::GB_LEFTSHOULDER;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:	return Gamepad::GB_RIGHTSHOULDER;
		case SDL_GAMEPAD_BUTTON_DPAD_UP:		return Gamepad::GB_DPAD_UP;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:		return Gamepad::GB_DPAD_DOWN;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:		return Gamepad::GB_DPAD_LEFT;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:		return Gamepad::GB_DPAD_RIGHT;
		default:								return Gamepad::GB_COUNT;
		}
	}
	//---------------------------------------------------------
	//! the SDL button an engine one names (SDL_GAMEPAD_BUTTON_INVALID when the
	//! value is out of range) - the injectGamepadButton synthesis side
	static SDL_GamepadButton gamepadButtonToSdl(Gamepad::Button button)
	{
		for(int each = 0; each < SDL_GAMEPAD_BUTTON_COUNT; each++)
		{
			if(sdlGamepadButton(static_cast<SDL_GamepadButton>(each)) == button)
			{
				return static_cast<SDL_GamepadButton>(each);
			}
		}
		return SDL_GAMEPAD_BUTTON_INVALID;
	}
	//---------------------------------------------------------
	//! the engine's axis for an SDL one (GA_COUNT = not a standard axis)
	static Gamepad::Axis sdlGamepadAxis(SDL_GamepadAxis axis)
	{
		switch(axis)
		{
		case SDL_GAMEPAD_AXIS_LEFTX:			return Gamepad::GA_LEFTX;
		case SDL_GAMEPAD_AXIS_LEFTY:			return Gamepad::GA_LEFTY;
		case SDL_GAMEPAD_AXIS_RIGHTX:			return Gamepad::GA_RIGHTX;
		case SDL_GAMEPAD_AXIS_RIGHTY:			return Gamepad::GA_RIGHTY;
		case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:		return Gamepad::GA_LEFTTRIGGER;
		case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:	return Gamepad::GA_RIGHTTRIGGER;
		default:								return Gamepad::GA_COUNT;
		}
	}
	//---------------------------------------------------------
	//! the SDL axis an engine one names (SDL_GAMEPAD_AXIS_INVALID when out of
	//! range) - the injectGamepadAxis synthesis side
	static SDL_GamepadAxis gamepadAxisToSdl(Gamepad::Axis axis)
	{
		for(int each = 0; each < SDL_GAMEPAD_AXIS_COUNT; each++)
		{
			if(sdlGamepadAxis(static_cast<SDL_GamepadAxis>(each)) == axis)
			{
				return static_cast<SDL_GamepadAxis>(each);
			}
		}
		return SDL_GAMEPAD_AXIS_INVALID;
	}
	//---------------------------------------------------------
	//! @brief SDL's raw signed-short axis reading as the engine's normalized
	//! one: sticks -1..+1, triggers 0..+1 (SDL reports those as 0..32767)
	static float normalizeGamepadAxis(Gamepad::Axis axis, Sint16 raw)
	{
		const float value = static_cast<float>(raw) / 32767.0f;
		const float clamped = std::clamp(value, -1.0f, 1.0f);
		if(axis == Gamepad::GA_LEFTTRIGGER || axis == Gamepad::GA_RIGHTTRIGGER)
		{
			return std::max(0.0f, clamped);
		}
		return clamped;
	}
	//---------------------------------------------------------
	//! hidden inputmanager translates SDL3 input to Orkige input
	class InputManagerImpl : public Singleton<InputManagerImpl>
	{
		DECL_OSINGLETON(InputManagerImpl)
	public:
		friend class InputManager;

		Event keyPressedEvent;
		Event keyReleasedEvent;
		Event mousePressedEvent;
		Event mouseReleasedEvent;
		Event mouseMovedEvent;
		Event touchPressedEvent;
		Event touchReleasedEvent;
		Event touchMovedEvent;
		Event touchCancelledEvent;
		Event gestureBeganEvent;
		Event gestureEndedEvent;
		Event gestureCancelledEvent;
		Event accelerationEvent;
		Event textInputEvent;

		optr<KeyEventData> keyData;
		optr<MouseEventData> mouseData;
		optr<TouchEventData> touchData;
		optr<GestureEventData> gestureData;
		optr<AccelerationEventData> accelerationData;

		//! window extents used to scale SDLs normalized touch coordinates
		int windowWidth;
		int windowHeight;
		//! backing storage for InputManager::getAsString
		String keyName;
		//! active touch sequences: SDL finger id per slot, slot index = sequenceId
		SDL_FingerID touchSequences[ORKIGE_MAX_NUM_TOUCHES];
		bool touchSequenceUsed[ORKIGE_MAX_NUM_TOUCHES];
		//! @brief the RAW per-slot touch state the event stream writes, folded
		//! into the frame snapshot below by updateFrameState(). Positions are
		//! window pixels (SDL delivers normalized coordinates - scaled on
		//! arrival, exactly like touchData).
		struct TouchSlot
		{
			float	x = 0.0f;			//!< latest position, window pixels
			float	y = 0.0f;
			float	snapX = 0.0f;		//!< position at the last snapshot
			float	snapY = 0.0f;
			bool	pendingDown = false;	//!< a down edge arrived since the snapshot
			bool	pendingUp = false;		//!< an up/cancel edge arrived since it
			bool	endedReported = false;	//!< the TP_ENDED frame has been published
		};
		TouchSlot touchSlots[ORKIGE_MAX_NUM_TOUCHES];
		//! the frame snapshot: the touch points published to game code
		TouchPoint touchFrame[ORKIGE_MAX_NUM_TOUCHES];
		int touchFrameCount;
		//! @brief pointer button state: the LIVE held mask plus the edge masks
		//! ACCUMULATED since the last snapshot, so a press and its release
		//! inside one frame are both still seen (a fast tap is not swallowed),
		//! and the three the frame snapshot publishes
		int pointerButtons;
		int pointerPressedRaw;
		int pointerReleasedRaw;
		int pointerButtonsFrame;
		int pointerPressedFrame;
		int pointerReleasedFrame;
		//--- gamepads ----------------------------------------
		SDL_Gamepad* gamepads[ORKIGE_MAX_NUM_GAMEPADS];
		SDL_JoystickID gamepadIds[ORKIGE_MAX_NUM_GAMEPADS];
		int gamepadCount;
		//! @brief button/axis state per the standard layout, MERGED across
		//! pads and fed from the INJECTED event stream (never SDL_GetGamepad*),
		//! so synthetic pad events are as real as hardware ones - the same
		//! reason keyDownState exists
		bool gamepadButtonState[Gamepad::GB_COUNT];
		float gamepadAxisValue[Gamepad::GA_COUNT];
		//! @brief key-down state per SDL scancode, fed from the INJECTED event
		//! stream (not SDL_GetKeyboardState): the application pumps every SDL
		//! event through injectEvent, so this covers hardware input AND
		//! synthetic SDL_PushEvent input (selfchecks, scripted test runs) alike
		bool keyDownState[SDL_SCANCODE_COUNT];
		//--- tilt state (see InputManager::getTilt) -----------
		float tiltAngle;				//!< simulated tilt angle in radians
		bool tiltSensorAvailable;		//!< a real accelerometer is open
		//! the open accelerometer has delivered a finite, gravity-bearing
		//! sample - only then does it DRIVE the tilt. A browser exposes a
		//! devicemotion "accelerometer" even on desktops and in headless
		//! runs, where no sample (or a null -> NaN one) ever arrives; until
		//! the sensor actually speaks, the key simulation stays in charge.
		bool tiltSensorLive;
		SDL_Sensor* accelSensor;		//!< the opened SDL accelerometer or NULL
		SDL_SensorID accelSensorId;		//!< instance id matched in injectEvent
		float sensorAccel[3];			//!< latest raw accelerometer sample (m/s^2)
		float tiltCalibAngle;			//!< neutral-pose calibration offset (radians)
		String calibSaveFile;			//!< calibration save path ("" = no persistence)
		bool textInputActive;			//!< an SDL text-input session is running

		InputManagerImpl()
			: keyPressedEvent(InputManager::KeyPressedEvent),
			keyReleasedEvent(InputManager::KeyReleasedEvent),
			mousePressedEvent(InputManager::MousePressedEvent),
			mouseReleasedEvent(InputManager::MouseReleasedEvent),
			mouseMovedEvent(InputManager::MouseMovedEvent),
			touchPressedEvent(InputManager::TouchPressedEvent),
			touchReleasedEvent(InputManager::TouchReleasedEvent),
			touchMovedEvent(InputManager::TouchMovedEvent),
			touchCancelledEvent(InputManager::TouchCancelledEvent),
			gestureBeganEvent(InputManager::GestureBeganEvent),
			gestureEndedEvent(InputManager::GestureEndedEvent),
			gestureCancelledEvent(InputManager::GestureCancelledEvent),
			accelerationEvent(InputManager::AccelerationEvent),
			textInputEvent(InputManager::TextInputEvent),
			windowWidth(0), windowHeight(0)
		{
			this->keyData = onew(new KeyEventData());
			this->mouseData = onew(new MouseEventData());
			this->touchData = onew(new TouchEventData());
			this->gestureData = onew(new GestureEventData());
			this->accelerationData = onew(new AccelerationEventData());

			this->keyPressedEvent.setData(this->keyData);
			this->keyReleasedEvent.setData(this->keyData);

			this->mousePressedEvent.setData(this->mouseData);
			this->mouseReleasedEvent.setData(this->mouseData);
			this->mouseMovedEvent.setData(this->mouseData);

			this->touchPressedEvent.setData(this->touchData);
			this->touchReleasedEvent.setData(this->touchData);
			this->touchMovedEvent.setData(this->touchData);
			this->touchCancelledEvent.setData(this->touchData);

			this->gestureBeganEvent.setData(this->gestureData);
			this->gestureEndedEvent.setData(this->gestureData);
			this->gestureCancelledEvent.setData(this->gestureData);

			this->accelerationEvent.setData((this->accelerationData));
			// the text-input event reuses the shared key data - its UTF-8 payload
			// rides KeyEventData::textInput (the `key` field stays unset)
			this->textInputEvent.setData(this->keyData);

			for (int each = 0; each < ORKIGE_MAX_NUM_TOUCHES; each++)
			{
				this->touchSequences[each] = 0;
				this->touchSequenceUsed[each] = false;
				this->touchSlots[each] = TouchSlot();
				this->touchFrame[each] = TouchPoint();
			}
			this->touchFrameCount = 0;
			this->pointerButtons = 0;
			this->pointerPressedRaw = 0;
			this->pointerReleasedRaw = 0;
			this->pointerButtonsFrame = 0;
			this->pointerPressedFrame = 0;
			this->pointerReleasedFrame = 0;
			for (int each = 0; each < ORKIGE_MAX_NUM_GAMEPADS; each++)
			{
				this->gamepads[each] = NULL;
				this->gamepadIds[each] = 0;
			}
			this->gamepadCount = 0;
			for (int each = 0; each < Gamepad::GB_COUNT; each++)
			{
				this->gamepadButtonState[each] = false;
			}
			for (int each = 0; each < Gamepad::GA_COUNT; each++)
			{
				this->gamepadAxisValue[each] = 0.0f;
			}
			for (int each = 0; each < SDL_SCANCODE_COUNT; each++)
			{
				this->keyDownState[each] = false;
			}
			this->tiltAngle = 0.0f;
			this->tiltSensorAvailable = false;
			this->tiltSensorLive = false;
			this->accelSensor = NULL;
			this->accelSensorId = 0;
			this->sensorAccel[0] = 0.0f;
			this->sensorAccel[1] = 0.0f;
			this->sensorAccel[2] = 0.0f;
			this->tiltCalibAngle = 0.0f;
			this->textInputActive = false;
		}
		~InputManagerImpl()
		{
			if (this->accelSensor)
			{
				SDL_CloseSensor(this->accelSensor);
				this->accelSensor = NULL;
			}
			for (int each = 0; each < this->gamepadCount; each++)
			{
				if (this->gamepads[each])
				{
					SDL_CloseGamepad(this->gamepads[each]);
				}
				this->gamepads[each] = NULL;
			}
			this->gamepadCount = 0;
		}
		//! @brief open the gamepad subsystem and every pad already plugged in.
		//! Pads connected LATER arrive as SDL_EVENT_GAMEPAD_ADDED through
		//! injectEvent, so hot-plugging works with no polling.
		void openGamepads()
		{
			if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
			{
				return;
			}
			int padCount = 0;
			SDL_JoystickID* padIds = SDL_GetGamepads(&padCount);
			if (!padIds)
			{
				return;
			}
			for (int each = 0; each < padCount; each++)
			{
				this->addGamepad(padIds[each]);
			}
			SDL_free(padIds);
		}
		//! is this instance id already tracked
		bool hasGamepad(SDL_JoystickID instanceId) const
		{
			for (int each = 0; each < this->gamepadCount; each++)
			{
				if (this->gamepadIds[each] == instanceId)
				{
					return true;
				}
			}
			return false;
		}
		//! start tracking one gamepad by instance id (ignores duplicates and
		//! anything past the tracked bound)
		void addGamepad(SDL_JoystickID instanceId)
		{
			if (this->hasGamepad(instanceId) ||
				this->gamepadCount >= ORKIGE_MAX_NUM_GAMEPADS)
			{
				return;
			}
			SDL_Gamepad* pad = SDL_OpenGamepad(instanceId);
			if (!pad)
			{
				return;
			}
			this->gamepads[this->gamepadCount] = pad;
			this->gamepadIds[this->gamepadCount] = instanceId;
			this->gamepadCount++;
			char const * name = SDL_GetGamepadName(pad);
			oDebugMsg("core", 0, "InputManager: gamepad '"
				<< (name ? name : "controller") << "' connected ("
				<< this->gamepadCount << " open)");
		}
		//! @brief track a pad that DELIVERS events without an open SDL handle:
		//! an injected (synthetic) controller. A pad speaking IS a pad present,
		//! so a scripted run or an agent's injected gesture makes
		//! isGamepadConnected() true exactly like plugging one in - the same
		//! rule that makes injected keys as real as pressed ones.
		void addVirtualGamepad(SDL_JoystickID instanceId)
		{
			if (this->hasGamepad(instanceId) ||
				this->gamepadCount >= ORKIGE_MAX_NUM_GAMEPADS)
			{
				return;
			}
			this->gamepads[this->gamepadCount] = NULL;
			this->gamepadIds[this->gamepadCount] = instanceId;
			this->gamepadCount++;
		}
		//! stop tracking one gamepad; the last pad leaving CLEARS the merged
		//! state so an unplugged held button can never stay stuck down
		void removeGamepad(SDL_JoystickID instanceId)
		{
			for (int each = 0; each < this->gamepadCount; each++)
			{
				if (this->gamepadIds[each] != instanceId)
				{
					continue;
				}
				if (this->gamepads[each])
				{
					SDL_CloseGamepad(this->gamepads[each]);
				}
				for (int shift = each; shift + 1 < this->gamepadCount; shift++)
				{
					this->gamepads[shift] = this->gamepads[shift + 1];
					this->gamepadIds[shift] = this->gamepadIds[shift + 1];
				}
				this->gamepadCount--;
				this->gamepads[this->gamepadCount] = NULL;
				this->gamepadIds[this->gamepadCount] = 0;
				oDebugMsg("core", 0, "InputManager: gamepad disconnected ("
					<< this->gamepadCount << " open)");
				break;
			}
			if (this->gamepadCount == 0)
			{
				for (int each = 0; each < Gamepad::GB_COUNT; each++)
				{
					this->gamepadButtonState[each] = false;
				}
				for (int each = 0; each < Gamepad::GA_COUNT; each++)
				{
					this->gamepadAxisValue[each] = 0.0f;
				}
			}
		}
		//! open the first accelerometer, if the machine has one (phones/tablets;
		//! desktops usually have none and getTilt falls back to the simulation)
		void openTiltSensor()
		{
			if (this->accelSensor || !SDL_InitSubSystem(SDL_INIT_SENSOR))
			{
				return;
			}
			int sensorCount = 0;
			SDL_SensorID* sensorIds = SDL_GetSensors(&sensorCount);
			if (!sensorIds)
			{
				return;
			}
			for (int each = 0; each < sensorCount; each++)
			{
				if (SDL_GetSensorTypeForID(sensorIds[each]) == SDL_SENSOR_ACCEL)
				{
					this->accelSensor = SDL_OpenSensor(sensorIds[each]);
					if (this->accelSensor)
					{
						this->accelSensorId = sensorIds[each];
						this->tiltSensorAvailable = true;
						oDebugMsg("core", 0, "InputManager: accelerometer '"
							<< SDL_GetSensorNameForID(sensorIds[each])
							<< "' drives getTilt()");
						break;
					}
				}
			}
			SDL_free(sensorIds);
		}
		//! find the slot of a tracked finger, -1 if unknown
		inline int findTouchSequenceId(SDL_FingerID fingerId) const
		{
			for (int each = 0; each < ORKIGE_MAX_NUM_TOUCHES; each++)
			{
				if (this->touchSequenceUsed[each] && this->touchSequences[each] == fingerId)
				{
					return each;
				}
			}
			return -1;
		}
		//! start tracking a finger in the first free slot (reuses its slot if already tracked)
		inline int acquireTouchSequenceId(SDL_FingerID fingerId)
		{
			int existing = this->findTouchSequenceId(fingerId);
			if (existing != -1 && !this->touchSlots[existing].pendingUp)
			{
				return existing;
			}
			if (existing != -1)
			{
				// the platform reused a finger id before the frame snapshot
				// published the previous sequence's release: drop the id
				// mapping so the NEW sequence gets its own slot and the
				// pending release still reaches the game
				this->touchSequences[existing] = 0;
			}
			for (int each = 0; each < ORKIGE_MAX_NUM_TOUCHES; each++)
			{
				if (!this->touchSequenceUsed[each])
				{
					this->touchSequenceUsed[each] = true;
					this->touchSequences[each] = fingerId;
					return each;
				}
			}
			// every slot is taken: reclaim one already lifted whose release the
			// frame snapshot has not published yet (a host that never calls
			// updateFrameState would otherwise run out of slots)
			for (int each = 0; each < ORKIGE_MAX_NUM_TOUCHES; each++)
			{
				if (this->touchSlots[each].pendingUp)
				{
					this->touchSequences[each] = fingerId;
					return each;
				}
			}
			return -1;
		}
		//! @brief stop tracking a finger and return the slot it had. The slot
		//! stays RESERVED until the frame snapshot has published its TP_ENDED
		//! frame, so a release is never lost between two frames.
		inline int releaseTouchSequenceId(SDL_FingerID fingerId)
		{
			return this->findTouchSequenceId(fingerId);
		}
		//! free a slot the frame snapshot has finished reporting
		inline void retireTouchSequence(int sequenceId)
		{
			this->touchSequenceUsed[sequenceId] = false;
			this->touchSequences[sequenceId] = 0;
			this->touchSlots[sequenceId] = TouchSlot();
		}
		inline void sdlKeyToOrkige(SDL_KeyboardEvent const & e)
		{
			this->keyData->key = sdlScancodeToKeyCode(e.scancode);
			// OIS delivered the locale-translated character here. SDL3 only
			// carries the unshifted key symbol with key events - shifted or
			// composed text entry needs SDL_StartTextInput and the
			// SDL_EVENT_TEXT_INPUT stream, which is not wired up yet.
			this->keyData->text = (e.key & SDLK_SCANCODE_MASK) ? 0 : static_cast<unsigned int>(e.key);
		}
		inline void sdlMouseButtonToOrkige(SDL_MouseButtonEvent const & e)
		{
			this->mouseData->relX = 0;
			this->mouseData->relY = 0;
			this->mouseData->relZ = 0;
			this->mouseData->absX = static_cast<int>(e.x);
			this->mouseData->absY = static_cast<int>(e.y);
			switch(e.button)
			{
			case SDL_BUTTON_LEFT:	this->mouseData->button = MouseEventData::MB_Left; break;
			case SDL_BUTTON_MIDDLE:	this->mouseData->button = MouseEventData::MB_Middle; break;
			case SDL_BUTTON_RIGHT:	this->mouseData->button = MouseEventData::MB_Right; break;
			case SDL_BUTTON_X1:		this->mouseData->button = MouseEventData::MB_Button3; break;
			case SDL_BUTTON_X2:		this->mouseData->button = MouseEventData::MB_Button4; break;
			default:				this->mouseData->button = MouseEventData::MB_Button7; break;
			}
			if(e.down)
			{
				this->mouseData->buttons |= 1 << this->mouseData->button;	//turn the bit flag on
			}
			else
			{
				this->mouseData->buttons &= ~(1 << this->mouseData->button);	//turn the bit flag off
			}
		}
		inline void sdlMouseMotionToOrkige(SDL_MouseMotionEvent const & e)
		{
			this->mouseData->relX = static_cast<int>(e.xrel);
			this->mouseData->relY = static_cast<int>(e.yrel);
			this->mouseData->relZ = 0;
			this->mouseData->absX = static_cast<int>(e.x);
			this->mouseData->absY = static_cast<int>(e.y);
		}
		inline void sdlMouseWheelToOrkige(SDL_MouseWheelEvent const & e)
		{
			// OIS reported the wheel on the mouse Z axis, 120 units per notch
			this->mouseData->relX = 0;
			this->mouseData->relY = 0;
			this->mouseData->relZ = static_cast<int>(e.y * 120.f);
			this->mouseData->absX = static_cast<int>(e.mouse_x);
			this->mouseData->absY = static_cast<int>(e.mouse_y);
			this->mouseData->absZ += this->mouseData->relZ;
		}
		inline void sdlTouchToOrkige(SDL_TouchFingerEvent const & e, int sequenceId)
		{
			// SDL3 finger coordinates are normalized - scale by the window extents
			this->touchData->relX = static_cast<int>(e.dx * this->windowWidth);
			this->touchData->relY = static_cast<int>(e.dy * this->windowHeight);
			this->touchData->relZ = 0;
			this->touchData->absX = static_cast<int>(e.x * this->windowWidth);
			this->touchData->absY = static_cast<int>(e.y * this->windowHeight);
			this->touchData->absZ = 0;
			this->touchData->sequenceId = sequenceId;
		}
		//! @brief record one raw finger edge into its slot for the next frame
		//! snapshot (the positions stay in window pixels, like touchData)
		inline void noteTouch(SDL_TouchFingerEvent const & e, int sequenceId,
			bool down, bool up)
		{
			if (sequenceId < 0 || sequenceId >= ORKIGE_MAX_NUM_TOUCHES)
			{
				return;	// untracked finger (past the slot bound) - nothing to fold
			}
			TouchSlot & slot = this->touchSlots[sequenceId];
			if (down)
			{
				// a fresh sequence starts where it landed: no phantom delta
				slot = TouchSlot();
				slot.snapX = e.x * this->windowWidth;
				slot.snapY = e.y * this->windowHeight;
				slot.pendingDown = true;
			}
			slot.x = e.x * this->windowWidth;
			slot.y = e.y * this->windowHeight;
			if (up)
			{
				slot.pendingUp = true;
			}
		}
	};
	IMPL_OSINGLETON(InputManagerImpl);

	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	InputManager::InputManager(bool shareMouse, bool enableNativeInput)
	{
		this->frameListener = GlobalEventManager::getSingleton().bind(Engine::FrameStartedEvent,&InputManager::onFrameStarted,this);
		this->impl = new InputManagerImpl();
		this->sharedMouse = shareMouse;
		this->enabled = true;
		if(enableNativeInput)
		{
			this->initialise();
		}
	}
	//---------------------------------------------------------
	InputManager::~InputManager( void )
	{
		if(GlobalEventManager::getSingletonPtr())
		{
			GlobalEventManager::getSingleton().delListener(this->frameListener,Engine::FrameStartedEvent);
		}
		delete this->impl;
		this->impl = NULL;
	}
	//---------------------------------------------------------
	bool InputManager::enable()
	{
		this->enabled = true;
		return GlobalEventManager::getSingleton().addListener(this->frameListener,Engine::FrameStartedEvent);
	}
	//---------------------------------------------------------
	bool InputManager::disable()
	{
		this->enabled = false;
		return GlobalEventManager::getSingleton().delListener(this->frameListener,Engine::FrameStartedEvent);
	}
	//---------------------------------------------------------
	bool InputManager::injectEvent(SDL_Event const & event)
	{
		if(!this->enabled)
		{
			return false;
		}
		switch(event.type)
		{
		case SDL_EVENT_KEY_DOWN:
			// note: unlike OIS, SDL3 also delivers OS key repeats
			// (event.key.repeat) - they are forwarded on purpose
			if (event.key.scancode < SDL_SCANCODE_COUNT)
			{
				this->impl->keyDownState[event.key.scancode] = true;
			}
			this->impl->sdlKeyToOrkige(event.key);
			GlobalEventManager::getSingleton().trigger(this->impl->keyPressedEvent);
			return true;
		case SDL_EVENT_KEY_UP:
			if (event.key.scancode < SDL_SCANCODE_COUNT)
			{
				this->impl->keyDownState[event.key.scancode] = false;
			}
			this->impl->sdlKeyToOrkige(event.key);
			GlobalEventManager::getSingleton().trigger(this->impl->keyReleasedEvent);
			return true;
		case SDL_EVENT_TEXT_INPUT:
			// committed text while a text-input session is active (SDL composes
			// shifted/dead-key/IME text here, not on the key events): hand the
			// UTF-8 string to listeners via the shared key data's textInput field
			this->impl->keyData->textInput =
				event.text.text ? event.text.text : "";
			GlobalEventManager::getSingleton().trigger(this->impl->textInputEvent);
			return true;
		case SDL_EVENT_MOUSE_MOTION:
			this->impl->sdlMouseMotionToOrkige(event.motion);
			GlobalEventManager::getSingleton().trigger(this->impl->mouseMovedEvent);
			return true;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			this->impl->sdlMouseButtonToOrkige(event.button);
			this->impl->pointerButtons = this->impl->mouseData->buttons;
			this->impl->pointerPressedRaw |=
				1 << this->impl->mouseData->button;
			GlobalEventManager::getSingleton().trigger(this->impl->mousePressedEvent);
			return true;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			this->impl->sdlMouseButtonToOrkige(event.button);
			this->impl->pointerButtons = this->impl->mouseData->buttons;
			this->impl->pointerReleasedRaw |=
				1 << this->impl->mouseData->button;
			GlobalEventManager::getSingleton().trigger(this->impl->mouseReleasedEvent);
			return true;
		case SDL_EVENT_MOUSE_WHEEL:
			this->impl->sdlMouseWheelToOrkige(event.wheel);
			GlobalEventManager::getSingleton().trigger(this->impl->mouseMovedEvent);
			return true;
		case SDL_EVENT_FINGER_DOWN:
		{
			const int downSequence =
				this->impl->acquireTouchSequenceId(event.tfinger.fingerID);
			this->impl->sdlTouchToOrkige(event.tfinger, downSequence);
			this->impl->noteTouch(event.tfinger, downSequence, true, false);
			GlobalEventManager::getSingleton().trigger(this->impl->touchPressedEvent);
			return true;
		}
		case SDL_EVENT_FINGER_MOTION:
		{
			const int moveSequence =
				this->impl->findTouchSequenceId(event.tfinger.fingerID);
			this->impl->sdlTouchToOrkige(event.tfinger, moveSequence);
			this->impl->noteTouch(event.tfinger, moveSequence, false, false);
			GlobalEventManager::getSingleton().trigger(this->impl->touchMovedEvent);
			return true;
		}
		case SDL_EVENT_FINGER_UP:
		{
			const int upSequence =
				this->impl->releaseTouchSequenceId(event.tfinger.fingerID);
			this->impl->sdlTouchToOrkige(event.tfinger, upSequence);
			this->impl->noteTouch(event.tfinger, upSequence, false, true);
			GlobalEventManager::getSingleton().trigger(this->impl->touchReleasedEvent);
			return true;
		}
		case SDL_EVENT_FINGER_CANCELED:
		{
			const int cancelSequence =
				this->impl->releaseTouchSequenceId(event.tfinger.fingerID);
			this->impl->sdlTouchToOrkige(event.tfinger, cancelSequence);
			this->impl->noteTouch(event.tfinger, cancelSequence, false, true);
			GlobalEventManager::getSingleton().trigger(this->impl->touchCancelledEvent);
			return true;
		}
		case SDL_EVENT_GAMEPAD_ADDED:
			// hot-plug: open the pad so its buttons/axes start arriving
			this->impl->addGamepad(event.gdevice.which);
			return true;
		case SDL_EVENT_GAMEPAD_REMOVED:
			this->impl->removeGamepad(event.gdevice.which);
			return true;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		{
			const Gamepad::Button button = sdlGamepadButton(
				static_cast<SDL_GamepadButton>(event.gbutton.button));
			if(button == Gamepad::GB_COUNT)
			{
				return false;	// outside the standard layout - nothing to bind
			}
			if(!this->impl->hasGamepad(event.gbutton.which))
			{
				this->impl->addVirtualGamepad(event.gbutton.which);
			}
			this->impl->gamepadButtonState[button] =
				(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
			return true;
		}
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		{
			const Gamepad::Axis axis = sdlGamepadAxis(
				static_cast<SDL_GamepadAxis>(event.gaxis.axis));
			if(axis == Gamepad::GA_COUNT)
			{
				return false;
			}
			if(!this->impl->hasGamepad(event.gaxis.which))
			{
				this->impl->addVirtualGamepad(event.gaxis.which);
			}
			this->impl->gamepadAxisValue[axis] =
				normalizeGamepadAxis(axis, event.gaxis.value);
			return true;
		}
		case SDL_EVENT_SENSOR_UPDATE:
			// the accelerometer opened in openTiltSensor: feed the classic
			// (2012) AccelerationEventData AND the getTilt() sample
			if (this->impl->accelSensor &&
				event.sensor.which == this->impl->accelSensorId)
			{
				// a browser's devicemotion "accelerometer" can deliver null
				// fields (desktop browsers, headless runs, permission not
				// granted) which arrive as NaN - discard those samples
				// entirely so they never poison the tilt or the classic
				// acceleration event
				if (!tiltSampleUsable(event.sensor.data[0],
					event.sensor.data[1], event.sensor.data[2]))
				{
					return true;
				}
				// the first real, gravity-bearing sample puts the sensor in
				// charge of the tilt (see rawTilt; before that the key
				// simulation drives, so a sensor that never speaks - the
				// desktop-browser web case - leaves the game playable)
				if (!this->impl->tiltSensorLive &&
					tiltSampleGravityBearing(event.sensor.data[0],
						event.sensor.data[1], event.sensor.data[2]))
				{
					this->impl->tiltSensorLive = true;
					oDebugMsg("core", 0, "InputManager: the accelerometer "
						"delivered its first sample - it drives getTilt() "
						"from here");
				}
				optr<AccelerationEventData> const & acceleration =
					this->impl->accelerationData;
				acceleration->relX = event.sensor.data[0] - acceleration->absX;
				acceleration->relY = event.sensor.data[1] - acceleration->absY;
				acceleration->relZ = event.sensor.data[2] - acceleration->absZ;
				acceleration->absX = event.sensor.data[0];
				acceleration->absY = event.sensor.data[1];
				acceleration->absZ = event.sensor.data[2];
				this->impl->sensorAccel[0] = event.sensor.data[0];
				this->impl->sensorAccel[1] = event.sensor.data[1];
				this->impl->sensorAccel[2] = event.sensor.data[2];
				GlobalEventManager::getSingleton().trigger(this->impl->accelerationEvent);
				return true;
			}
			return false;
		default:
			return false;
		}
	}
	//---------------------------------------------------------
	bool InputManager::injectKey(KeyEventData::KeyCode kc, bool down)
	{
		const SDL_Scancode scancode = keyCodeToSdlScancode(kc);
		if(scancode == SDL_SCANCODE_UNKNOWN)
		{
			return false;
		}
		SDL_Event event{};
		event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		event.key.scancode = scancode;
		event.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
		event.key.down = down;
		event.key.repeat = false;
		return this->injectEvent(event);
	}
	//---------------------------------------------------------
	bool InputManager::injectTouch(int fingerId, TouchPhase phase, float x,
		float y)
	{
		if(this->impl->windowWidth <= 0 || this->impl->windowHeight <= 0)
		{
			// without extents the pixel -> normalized conversion is a guess:
			// refuse rather than inject a finger at a made-up position
			return false;
		}
		SDL_Event event{};
		switch(phase)
		{
		case TP_BEGAN:	event.type = SDL_EVENT_FINGER_DOWN; break;
		case TP_MOVED:	event.type = SDL_EVENT_FINGER_MOTION; break;
		case TP_ENDED:	event.type = SDL_EVENT_FINGER_UP; break;
		default:		return false;
		}
		// SDL delivers finger coordinates NORMALIZED; the engine's touch
		// vocabulary is window pixels, so the conversion happens here - the one
		// synthesis path, matching what sdlTouchToOrkige undoes on arrival
		event.tfinger.fingerID = static_cast<SDL_FingerID>(fingerId + 1);
		event.tfinger.x = x / static_cast<float>(this->impl->windowWidth);
		event.tfinger.y = y / static_cast<float>(this->impl->windowHeight);
		event.tfinger.dx = 0.0f;
		event.tfinger.dy = 0.0f;
		event.tfinger.pressure = (phase == TP_ENDED) ? 0.0f : 1.0f;
		return this->injectEvent(event);
	}
	//---------------------------------------------------------
	bool InputManager::injectGamepadButton(Gamepad::Button button, bool down)
	{
		const SDL_GamepadButton sdlButton = gamepadButtonToSdl(button);
		if(sdlButton == SDL_GAMEPAD_BUTTON_INVALID)
		{
			return false;
		}
		SDL_Event event{};
		event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN
			: SDL_EVENT_GAMEPAD_BUTTON_UP;
		event.gbutton.which = INJECTED_GAMEPAD_ID;
		event.gbutton.button = static_cast<Uint8>(sdlButton);
		event.gbutton.down = down;
		return this->injectEvent(event);
	}
	//---------------------------------------------------------
	bool InputManager::injectGamepadAxis(Gamepad::Axis axis, float value)
	{
		const SDL_GamepadAxis sdlAxis = gamepadAxisToSdl(axis);
		if(sdlAxis == SDL_GAMEPAD_AXIS_INVALID)
		{
			return false;
		}
		const float clamped = std::clamp(value, -1.0f, 1.0f);
		SDL_Event event{};
		event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
		event.gaxis.which = INJECTED_GAMEPAD_ID;
		event.gaxis.axis = static_cast<Uint8>(sdlAxis);
		event.gaxis.value = static_cast<Sint16>(clamped * 32767.0f);
		return this->injectEvent(event);
	}
	//---------------------------------------------------------
	bool InputManager::isGamepadButtonDown(Gamepad::Button button) const
	{
		if(button < 0 || button >= Gamepad::GB_COUNT)
		{
			return false;
		}
		return this->impl->gamepadButtonState[button];
	}
	//---------------------------------------------------------
	float InputManager::getGamepadAxis(Gamepad::Axis axis) const
	{
		if(axis < 0 || axis >= Gamepad::GA_COUNT)
		{
			return 0.0f;
		}
		return this->impl->gamepadAxisValue[axis];
	}
	//---------------------------------------------------------
	int InputManager::getGamepadCount() const
	{
		return this->impl->gamepadCount;
	}
	//---------------------------------------------------------
	bool InputManager::isGamepadConnected() const
	{
		return this->impl->gamepadCount > 0;
	}
	//---------------------------------------------------------
	void InputManager::updateFrameState()
	{
		// --- touch: fold the raw slot edges into the published frame ---
		this->impl->touchFrameCount = 0;
		for(int slotIndex = 0; slotIndex < ORKIGE_MAX_NUM_TOUCHES; slotIndex++)
		{
			if(!this->impl->touchSequenceUsed[slotIndex])
			{
				continue;
			}
			InputManagerImpl::TouchSlot & slot =
				this->impl->touchSlots[slotIndex];
			// a slot whose TP_ENDED frame has been published is done: retire it
			// so the finger stops being reported at all
			if(slot.endedReported)
			{
				this->impl->retireTouchSequence(slotIndex);
				continue;
			}
			TouchPoint point;
			point.id = slotIndex;
			point.x = slot.x;
			point.y = slot.y;
			point.deltaX = slot.x - slot.snapX;
			point.deltaY = slot.y - slot.snapY;
			// a down edge always reads BEGAN first, even when the release
			// arrived in the same frame - the up stays pending and is published
			// as ENDED next frame, so a one-frame tap is never swallowed
			if(slot.pendingDown)
			{
				point.phase = TP_BEGAN;
				slot.pendingDown = false;
			}
			else if(slot.pendingUp)
			{
				point.phase = TP_ENDED;
				slot.endedReported = true;
			}
			else
			{
				point.phase = TP_MOVED;
			}
			slot.snapX = slot.x;
			slot.snapY = slot.y;
			this->impl->touchFrame[this->impl->touchFrameCount] = point;
			this->impl->touchFrameCount++;
		}

		// --- pointer: the held mask plus the edges seen since the last snapshot
		this->impl->pointerButtonsFrame = this->impl->pointerButtons;
		this->impl->pointerPressedFrame = this->impl->pointerPressedRaw;
		this->impl->pointerReleasedFrame = this->impl->pointerReleasedRaw;
		this->impl->pointerPressedRaw = 0;
		this->impl->pointerReleasedRaw = 0;
	}
	//---------------------------------------------------------
	int InputManager::getTouchCount() const
	{
		return this->impl->touchFrameCount;
	}
	//---------------------------------------------------------
	TouchPoint InputManager::getTouchPoint(int index) const
	{
		if(index < 0 || index >= this->impl->touchFrameCount)
		{
			return TouchPoint();
		}
		return this->impl->touchFrame[index];
	}
	//---------------------------------------------------------
	Vec2 InputManager::getPointerPosition() const
	{
		return Vec2(static_cast<float>(this->impl->mouseData->absX),
			static_cast<float>(this->impl->mouseData->absY));
	}
	//---------------------------------------------------------
	bool InputManager::isPointerDown(
		MouseEventData::MouseButtonID button) const
	{
		return (this->impl->pointerButtonsFrame & (1 << button)) != 0;
	}
	//---------------------------------------------------------
	bool InputManager::isPointerPressed(
		MouseEventData::MouseButtonID button) const
	{
		return (this->impl->pointerPressedFrame & (1 << button)) != 0;
	}
	//---------------------------------------------------------
	bool InputManager::isPointerReleased(
		MouseEventData::MouseButtonID button) const
	{
		return (this->impl->pointerReleasedFrame & (1 << button)) != 0;
	}
	//---------------------------------------------------------
	String const & InputManager::getAsString(KeyEventData::KeyCode kc)
	{
		SDL_Scancode scancode = keyCodeToSdlScancode(kc);
		if(scancode == SDL_SCANCODE_UNKNOWN)
		{
			return StringUtil::BLANK;
		}
		this->impl->keyName = SDL_GetKeyName(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false));
		return this->impl->keyName;
	}
	//---------------------------------------------------------
	bool InputManager::isKeyDown(KeyEventData::KeyCode kc)
	{
		SDL_Scancode scancode = keyCodeToSdlScancode(kc);
		if(scancode == SDL_SCANCODE_UNKNOWN)
		{
			return false;
		}
		// read the injectEvent-fed state, NOT SDL_GetKeyboardState: the SDL
		// state array ignores application-pushed events, which would make
		// scripted/synthetic input (selfchecks) invisible here
		return this->impl->keyDownState[scancode];
	}
	//---------------------------------------------------------
	void InputManager::startTextInput()
	{
		SDL_Window* window =
			static_cast<SDL_Window*>(PlatformWindow::getActiveWindow());
		if(!window)
		{
			return;	// no window registered (editor never registers one) - no-op
		}
		if(!this->impl->textInputActive)
		{
			SDL_StartTextInput(window);
			this->impl->textInputActive = true;
		}
	}
	//---------------------------------------------------------
	void InputManager::stopTextInput()
	{
		if(!this->impl->textInputActive)
		{
			return;
		}
		this->impl->textInputActive = false;
		SDL_Window* window =
			static_cast<SDL_Window*>(PlatformWindow::getActiveWindow());
		if(window)
		{
			SDL_StopTextInput(window);
		}
	}
	//---------------------------------------------------------
	bool InputManager::isTextInputActive() const
	{
		return this->impl->textInputActive;
	}
	//---------------------------------------------------------
	void InputManager::setWindowExtents( int width, int height )
	{
		this->impl->windowWidth = width;
		this->impl->windowHeight = height;
	}
	//---------------------------------------------------------
	optr<MouseEventData> const & InputManager::getMouseData() const
	{
		// always current: injectEvent keeps it up to date
		return impl->mouseData;
	}
	//---------------------------------------------------------
	optr<TouchEventData> const & InputManager::getLastTouchData() const
	{
		return impl->touchData;
	}
	//---------------------------------------------------------
	Ogre::Vector3 InputManager::getTilt() const
	{
		Ogre::Vector3 tilt = this->rawTilt();
		// apply the neutral-pose calibration (a planar rotation about Z); the
		// raw pose the calibration was captured at reads as upright afterwards
		if(this->impl->tiltCalibAngle != 0.0f)
		{
			TiltCalibration::apply(tilt.x, tilt.y, this->impl->tiltCalibAngle);
			if(tilt.length() >= 0.5f)
			{
				tilt = tilt.normalisedCopy();
			}
		}
		return tilt;
	}
	//---------------------------------------------------------
	bool InputManager::tiltSampleUsable(float x, float y, float z)
	{
		return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
	}
	//---------------------------------------------------------
	bool InputManager::tiltSampleGravityBearing(float x, float y, float z)
	{
		return tiltSampleUsable(x, y, z) &&
			std::sqrt(x * x + y * y + z * z) >= 1.0f;
	}
	//---------------------------------------------------------
	Ogre::Vector3 InputManager::rawTilt() const
	{
		if(this->impl->tiltSensorAvailable && this->impl->tiltSensorLive)
		{
			// SDL accelerometers report the reaction to gravity (device at
			// rest, screen up: ~(0, 0, +9.81); held upright: y ~ +9.81) - the
			// on-screen gravity direction is the NEGATED x/y, z projected out
			Ogre::Vector3 tilt(-this->impl->sensorAccel[0],
				-this->impl->sensorAccel[1], 0.0f);
			// a device in free fall (or no sample yet) has no usable gravity
			// direction - report "upright" instead of a garbage normalization
			if(tilt.length() < 0.5f)
			{
				return Ogre::Vector3(0.0f, -1.0f, 0.0f);
			}
			return tilt.normalisedCopy();
		}
		return tiltVectorFromAngle(this->impl->tiltAngle);
	}
	//---------------------------------------------------------
	void InputManager::calibrateTilt()
	{
		// the pose captured is the RAW (pre-calibration) gravity direction, so
		// re-calibrating from an already-calibrated state captures the new
		// physical pose, never the corrected one
		Ogre::Vector3 pose = this->rawTilt();
		this->impl->tiltCalibAngle =
			TiltCalibration::angleForPose(pose.x, pose.y);
		this->saveCalibration();
	}
	//---------------------------------------------------------
	void InputManager::clearTiltCalibration()
	{
		this->impl->tiltCalibAngle = 0.0f;
		this->saveCalibration();
	}
	//---------------------------------------------------------
	float InputManager::getTiltCalibration() const
	{
		return this->impl->tiltCalibAngle;
	}
	//---------------------------------------------------------
	void InputManager::setTiltCalibration(float radians)
	{
		this->impl->tiltCalibAngle = radians;
	}
	//---------------------------------------------------------
	void InputManager::setCalibrationSaveFile(String const & path)
	{
		this->impl->calibSaveFile = path;
	}
	//---------------------------------------------------------
	bool InputManager::saveCalibration() const
	{
		if(this->impl->calibSaveFile.empty())
		{
			return false;
		}
		std::ofstream file(this->impl->calibSaveFile.c_str(), std::ios::binary);
		if(!file)
		{
			return false;
		}
		// magic + version + the offset, so a garbage/foreign file is rejected
		file << "orkige.tilt\n1\n" << this->impl->tiltCalibAngle << "\n";
		return file.good();
	}
	//---------------------------------------------------------
	bool InputManager::loadCalibration()
	{
		if(this->impl->calibSaveFile.empty())
		{
			return false;
		}
		std::ifstream file(this->impl->calibSaveFile.c_str(), std::ios::binary);
		if(!file)
		{
			return false;
		}
		std::string magic;
		int version = 0;
		float angle = 0.0f;
		if(!(file >> magic >> version >> angle) ||
			magic != "orkige.tilt" || version != 1)
		{
			return false;	// honest fallback: keep the current (no) calibration
		}
		this->impl->tiltCalibAngle = angle;
		return true;
	}
	//---------------------------------------------------------
	bool InputManager::isTiltSensorAvailable() const
	{
		// "available" to gameplay means the sensor actually DELIVERS: an
		// open accelerometer that never speaks (a desktop browser's
		// devicemotion shim) reads as unavailable, so games offer the
		// key/tilt-simulation experience instead
		return this->impl->tiltSensorAvailable && this->impl->tiltSensorLive;
	}
	//---------------------------------------------------------
	void InputManager::setTiltAngle(float radians)
	{
		this->impl->tiltAngle = std::clamp(radians,
			-InputManager::TILT_SIM_MAX_ANGLE, InputManager::TILT_SIM_MAX_ANGLE);
	}
	//---------------------------------------------------------
	float InputManager::getTiltAngle() const
	{
		return this->impl->tiltAngle;
	}
	//---------------------------------------------------------
	float InputManager::advanceTiltAngle(float angleRadians, bool steerLeft,
		bool steerRight, float deltaTime)
	{
		float steer = 0.0f;
		if(steerLeft)
		{
			steer -= 1.0f;
		}
		if(steerRight)
		{
			steer += 1.0f;
		}
		// no auto-centering: a tilted phone stays tilted until tilted back
		return std::clamp(angleRadians + steer * InputManager::TILT_SIM_RATE * deltaTime,
			-InputManager::TILT_SIM_MAX_ANGLE, InputManager::TILT_SIM_MAX_ANGLE);
	}
	//---------------------------------------------------------
	Ogre::Vector3 InputManager::tiltVectorFromAngle(float angleRadians)
	{
		// already normalized by construction; positive angles = gravity
		// swings toward +X (tilting the "device" to the right)
		return Ogre::Vector3(std::sin(angleRadians), -std::cos(angleRadians), 0.0f);
	}

	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	bool InputManager::onFrameStarted(Event const & event)
	{
		this->capture();
		// advance the desktop tilt simulation once per frame; once a real
		// accelerometer DELIVERS samples, the sensor stream drives the tilt
		// instead (an open-but-silent sensor must not disable the keys)
		if(!(this->impl->tiltSensorAvailable && this->impl->tiltSensorLive))
		{
			optr<FrameEventData> frameData = event.getDataPtr<FrameEventData>();
			if(frameData)
			{
				this->impl->tiltAngle = advanceTiltAngle(this->impl->tiltAngle,
					this->isKeyDown(KeyEventData::KC_LEFT) || this->isKeyDown(KeyEventData::KC_A),
					this->isKeyDown(KeyEventData::KC_RIGHT) || this->isKeyDown(KeyEventData::KC_D),
					frameData->timeSinceLastFrame);
			}
		}
		return false;
	}
	//---------------------------------------------------------
	void InputManager::initialise()
	{
		// SDL3 feeds us events through injectEvent() - all that is left to set
		// up is the window extents used to scale normalized touch coordinates
		// (facade-routed, so this works on every render flavor)
		if(RenderSystem::get())
		{
			unsigned int width = 0;
			unsigned int height = 0;
			RenderSystem::get()->getWindowSize(width, height);
			this->setWindowExtents( static_cast<int>(width), static_cast<int>(height) );
			oDebugMsg("core", 0, "Input initialized! width, height: " << width << ", " << height);
		}
		// tilt: open the accelerometer where one exists (phones/tablets)
		this->impl->openTiltSensor();
		// controllers: open every pad already plugged in (later ones arrive as
		// SDL_EVENT_GAMEPAD_ADDED through injectEvent)
		this->impl->openGamepads();
	}
	//---------------------------------------------------------
	void InputManager::capture( void )
	{
		// no-op since the SDL3 port: the application pumps the SDL event loop
		// and feeds events in via injectEvent(); kept because the frame
		// listener wiring (enable/disable) is part of the public API
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(InputManager)
		OCONSTRUCTOR1(bool)
		OSINGLETON()
		OFUNC(enable)
		OFUNC(disable)
		OFUNC(isKeyDown)
		// tilt input (sensor-backed on devices, key simulation on desktops):
		// input:getTilt() answers the normalized gravity direction
		OFUNC(getTilt)
		OFUNC(isTiltSensorAvailable)
		OFUNC(setTiltAngle)
		OFUNC(getTiltAngle)
		// tilt calibration: input:calibrateTilt() makes the current pose the
		// neutral (0,-1,0); clear reverts to the raw pose; get reads the offset
		OFUNC(calibrateTilt)
		OFUNC(clearTiltCalibration)
		OFUNC(getTiltCalibration)
	OOBJECT_END
}
