/**************************************************************
	created:	2010/08/30 at 11:05
	filename: 	InputManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
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
		//! @brief key-down state per SDL scancode, fed from the INJECTED event
		//! stream (not SDL_GetKeyboardState): the application pumps every SDL
		//! event through injectEvent, so this covers hardware input AND
		//! synthetic SDL_PushEvent input (selfchecks, scripted test runs) alike
		bool keyDownState[SDL_SCANCODE_COUNT];
		//--- tilt state (see InputManager::getTilt) -----------
		float tiltAngle;				//!< simulated tilt angle in radians
		bool tiltSensorAvailable;		//!< a real accelerometer is open
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
			}
			for (int each = 0; each < SDL_SCANCODE_COUNT; each++)
			{
				this->keyDownState[each] = false;
			}
			this->tiltAngle = 0.0f;
			this->tiltSensorAvailable = false;
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
			if (existing != -1)
			{
				return existing;
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
			return -1;
		}
		//! stop tracking a finger and return the slot it had
		inline int releaseTouchSequenceId(SDL_FingerID fingerId)
		{
			int sequenceId = this->findTouchSequenceId(fingerId);
			if (sequenceId != -1)
			{
				this->touchSequenceUsed[sequenceId] = false;
				this->touchSequences[sequenceId] = 0;
			}
			return sequenceId;
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
			GlobalEventManager::getSingleton().trigger(this->impl->mousePressedEvent);
			return true;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			this->impl->sdlMouseButtonToOrkige(event.button);
			GlobalEventManager::getSingleton().trigger(this->impl->mouseReleasedEvent);
			return true;
		case SDL_EVENT_MOUSE_WHEEL:
			this->impl->sdlMouseWheelToOrkige(event.wheel);
			GlobalEventManager::getSingleton().trigger(this->impl->mouseMovedEvent);
			return true;
		case SDL_EVENT_FINGER_DOWN:
			this->impl->sdlTouchToOrkige(event.tfinger, this->impl->acquireTouchSequenceId(event.tfinger.fingerID));
			GlobalEventManager::getSingleton().trigger(this->impl->touchPressedEvent);
			return true;
		case SDL_EVENT_FINGER_MOTION:
			this->impl->sdlTouchToOrkige(event.tfinger, this->impl->findTouchSequenceId(event.tfinger.fingerID));
			GlobalEventManager::getSingleton().trigger(this->impl->touchMovedEvent);
			return true;
		case SDL_EVENT_FINGER_UP:
			this->impl->sdlTouchToOrkige(event.tfinger, this->impl->releaseTouchSequenceId(event.tfinger.fingerID));
			GlobalEventManager::getSingleton().trigger(this->impl->touchReleasedEvent);
			return true;
		case SDL_EVENT_FINGER_CANCELED:
			this->impl->sdlTouchToOrkige(event.tfinger, this->impl->releaseTouchSequenceId(event.tfinger.fingerID));
			GlobalEventManager::getSingleton().trigger(this->impl->touchCancelledEvent);
			return true;
		case SDL_EVENT_SENSOR_UPDATE:
			// the accelerometer opened in openTiltSensor: feed the classic
			// (2012) AccelerationEventData AND the getTilt() sample
			if (this->impl->accelSensor &&
				event.sensor.which == this->impl->accelSensorId)
			{
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
	Ogre::Vector3 InputManager::rawTilt() const
	{
		if(this->impl->tiltSensorAvailable)
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
		return this->impl->tiltSensorAvailable;
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
		// advance the desktop tilt simulation once per frame; with a real
		// accelerometer the sensor stream drives the tilt instead
		if(!this->impl->tiltSensorAvailable)
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
