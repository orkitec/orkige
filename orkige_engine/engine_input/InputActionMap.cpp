/**************************************************************
	created:	2026/07/09 at 10:10
	filename: 	InputActionMap.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_input/InputActionMap.h"
#include "engine_input/InputManager.h"
#include "core_project/Project.h"
#include "core_serialization/XMLArchive.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	IMPL_OSINGLETON(InputActionMap);

	const String InputActionMap::ACTIONS_SETTING_KEY = "input.actions";
	const String InputActionMap::ACTIONS_FILE_EXTENSION = ".oactions";
	const String InputActionMap::ACTIONS_FILE_MAGIC = "orkige.oactions";
	// v2 carries the controller binding shapes (gamepad buttons, a deadzoned
	// gamepad axis). CLEAN CUTOVER: the loader accepts exactly this version and
	// refuses anything else with an honest message - no per-version field gates.
	const int InputActionMap::ACTIONS_FORMAT_VERSION = 2;

	//---------------------------------------------------------
	namespace
	{
		//! @brief digital button action from one or more keys
		InputAction makeDigital(String const & name,
			std::vector<KeyEventData::KeyCode> const & keys)
		{
			InputAction action;
			action.name = name;
			action.kind = InputActionKind::Digital;
			InputActionBinding binding;
			binding.type = InputActionBinding::Key;
			binding.keys = keys;
			binding.outputComponent = 0;
			action.bindings.push_back(binding);
			return action;
		}
		//! @brief a keyAxis binding (the promoted jumper axis() helper)
		InputActionBinding makeKeyAxis(
			std::vector<KeyEventData::KeyCode> const & negativeKeys,
			std::vector<KeyEventData::KeyCode> const & positiveKeys,
			int outputComponent)
		{
			InputActionBinding binding;
			binding.type = InputActionBinding::KeyAxis;
			binding.negativeKeys = negativeKeys;
			binding.positiveKeys = positiveKeys;
			binding.outputComponent = outputComponent;
			return binding;
		}
		//! @brief a tiltAxis binding reading a component of getTilt()
		InputActionBinding makeTiltAxis(int tiltComponent, int outputComponent)
		{
			InputActionBinding binding;
			binding.type = InputActionBinding::TiltAxis;
			binding.tiltComponent = tiltComponent;
			binding.outputComponent = outputComponent;
			return binding;
		}
		//! @brief a digital controller-button binding (any of them held -> +1)
		InputActionBinding makeGamepadButton(
			std::vector<Gamepad::Button> const & buttons, int outputComponent)
		{
			InputActionBinding binding;
			binding.type = InputActionBinding::GamepadButton;
			binding.gamepadButtons = buttons;
			binding.outputComponent = outputComponent;
			return binding;
		}
		//! @brief a deadzoned controller-axis binding
		InputActionBinding makeGamepadAxis(Gamepad::Axis axis,
			int outputComponent)
		{
			InputActionBinding binding;
			binding.type = InputActionBinding::GamepadAxis;
			binding.gamepadAxis = axis;
			binding.outputComponent = outputComponent;
			return binding;
		}
		//! any of the keys held?
		bool anyKeyDown(std::vector<KeyEventData::KeyCode> const & keys,
			InputManager & input)
		{
			for(KeyEventData::KeyCode key : keys)
			{
				if(input.isKeyDown(key))
				{
					return true;
				}
			}
			return false;
		}
		//! any of the controller buttons held?
		bool anyGamepadButtonDown(std::vector<Gamepad::Button> const & buttons,
			InputManager & input)
		{
			for(Gamepad::Button button : buttons)
			{
				if(input.isGamepadButtonDown(button))
				{
					return true;
				}
			}
			return false;
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	InputActionMap::InputActionMap()
	{
		// a fresh map already answers every default action - a runtime that
		// never loads a project (hello_orkige, the render selfchecks) still gets
		// the reference bindings for free
		this->loadDefaults();
	}
	//---------------------------------------------------------
	InputActionMap::~InputActionMap()
	{
	}
	//---------------------------------------------------------
	void InputActionMap::loadDefaults()
	{
		// the built-in default set: a SUPERSET covering both reference games.
		// A game queries only the actions it uses, so overlapping key bindings
		// (arrows drive move.x AND menu_* AND steer) never conflict.
		mActions.clear();

		// EVERY default action also answers a CONTROLLER, so a reference
		// project is controller-playable with zero authoring: the analog
		// actions gain a stick binding beside their keys (max magnitude picks
		// whichever pushes harder), the digital ones a face/dpad/start button.

		// jumper: 2D movement on WASD/arrows or the left stick, jump on SPACE
		// or the bottom face button. move.x = the left/right axis (A/LEFT
		// negative, D/RIGHT positive), move.y = the depth axis (W/UP negative,
		// S/DOWN positive) - matches the original axis() helper in
		// projects/jumper-lua/scripts/player.lua, and matches the stick's own
		// sign convention (a controller's +y is DOWN).
		{
			InputAction move;
			move.name = "move";
			move.kind = InputActionKind::Analog2D;
			move.bindings.push_back(makeKeyAxis(
				{ KeyEventData::KC_A, KeyEventData::KC_LEFT },
				{ KeyEventData::KC_D, KeyEventData::KC_RIGHT }, 0));
			move.bindings.push_back(makeKeyAxis(
				{ KeyEventData::KC_W, KeyEventData::KC_UP },
				{ KeyEventData::KC_S, KeyEventData::KC_DOWN }, 1));
			move.bindings.push_back(makeGamepadAxis(Gamepad::GA_LEFTX, 0));
			move.bindings.push_back(makeGamepadAxis(Gamepad::GA_LEFTY, 1));
			mActions.push_back(move);
		}
		{
			InputAction jump = makeDigital("jump", { KeyEventData::KC_SPACE });
			jump.bindings.push_back(
				makeGamepadButton({ Gamepad::GB_SOUTH }, 0));
			mActions.push_back(jump);
		}

		// roller: steer on the tilt X component OR LEFT/RIGHT arrows OR the
		// left stick (max magnitude), and the move-world menu keys (TAB
		// toggles, arrows slide) mirrored on start + the dpad.
		{
			InputAction steer;
			steer.name = "steer";
			steer.kind = InputActionKind::Analog1D;
			steer.bindings.push_back(makeTiltAxis(0, 0));
			steer.bindings.push_back(makeKeyAxis(
				{ KeyEventData::KC_LEFT }, { KeyEventData::KC_RIGHT }, 0));
			steer.bindings.push_back(makeGamepadAxis(Gamepad::GA_LEFTX, 0));
			mActions.push_back(steer);
		}
		// the digital menu set: keys plus the matching controller button
		struct MenuDefault
		{
			char const *			name;
			KeyEventData::KeyCode	key;
			Gamepad::Button			button;
		};
		const MenuDefault menuDefaults[] =
		{
			{ "menu_toggle",	KeyEventData::KC_TAB,	Gamepad::GB_START },
			{ "menu_left",		KeyEventData::KC_LEFT,	Gamepad::GB_DPAD_LEFT },
			{ "menu_right",		KeyEventData::KC_RIGHT,	Gamepad::GB_DPAD_RIGHT },
			{ "menu_up",		KeyEventData::KC_UP,	Gamepad::GB_DPAD_UP },
			{ "menu_down",		KeyEventData::KC_DOWN,	Gamepad::GB_DPAD_DOWN }
		};
		for(MenuDefault const & entry : menuDefaults)
		{
			InputAction action = makeDigital(entry.name, { entry.key });
			action.bindings.push_back(
				makeGamepadButton({ entry.button }, 0));
			mActions.push_back(action);
		}
	}
	//---------------------------------------------------------
	bool InputActionMap::loadActions(String const & fileName)
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startReading(fileName))
		{
			oDebugMsg("core",0,"InputActionMap: could not open action file: "<<fileName);
			return false;
		}

		String magic;
		ar >> magic;
		if(magic != ACTIONS_FILE_MAGIC)
		{
			oDebugMsg("core",0,"InputActionMap: "<<fileName
				<<" is not an orkige action file (magic: \""<<magic<<"\")");
			ar->stopReading();
			return false;
		}
		int version = 0;
		ar >> version;
		if(version != ACTIONS_FORMAT_VERSION)
		{
			// clean cutover: exactly the current version loads, anything else
			// is refused by name (re-save the file from this build)
			oDebugMsg("core",0,"InputActionMap: action file "<<fileName
				<<" has unsupported version "<<version<<" (supported: "
				<<ACTIONS_FORMAT_VERSION<<")");
			ar->stopReading();
			return false;
		}

		// build into a scratch vector; the live set is only replaced on success
		std::vector<InputAction> loaded;
		unsigned int actionCount = 0;
		ar >> actionCount;
		for(unsigned int actionIndex = 0; actionIndex < actionCount; ++actionIndex)
		{
			InputAction action;
			ar >> action.name;
			int kind = 0;
			ar >> kind;
			action.kind = static_cast<InputActionKind>(kind);

			unsigned int bindingCount = 0;
			ar >> bindingCount;
			for(unsigned int bindingIndex = 0; bindingIndex < bindingCount; ++bindingIndex)
			{
				InputActionBinding binding;
				int type = 0;
				ar >> type;
				binding.type = static_cast<InputActionBinding::Type>(type);
				ar >> binding.outputComponent;
				ar >> binding.tiltComponent;
				// three key groups: keys / negativeKeys / positiveKeys
				auto readKeys = [&](std::vector<KeyEventData::KeyCode> & keys)
				{
					unsigned int keyCount = 0;
					ar >> keyCount;
					keys.clear();
					for(unsigned int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						int keyCode = 0;
						ar >> keyCode;
						keys.push_back(static_cast<KeyEventData::KeyCode>(keyCode));
					}
				};
				readKeys(binding.keys);
				readKeys(binding.negativeKeys);
				readKeys(binding.positiveKeys);
				// the controller half of the binding
				unsigned int gamepadButtonCount = 0;
				ar >> gamepadButtonCount;
				binding.gamepadButtons.clear();
				for(unsigned int buttonIndex = 0;
					buttonIndex < gamepadButtonCount; ++buttonIndex)
				{
					int buttonCode = 0;
					ar >> buttonCode;
					binding.gamepadButtons.push_back(
						static_cast<Gamepad::Button>(buttonCode));
				}
				int gamepadAxis = 0;
				ar >> gamepadAxis;
				binding.gamepadAxis = static_cast<Gamepad::Axis>(gamepadAxis);
				ar >> binding.deadzone;
				ar >> binding.invert;
				action.bindings.push_back(binding);
			}
			loaded.push_back(action);
		}

		ar->stopReading();
		mActions.swap(loaded);
		oDebugMsg("core",0,"InputActionMap: loaded "<<mActions.size()
			<<" action(s) from "<<fileName);
		return true;
	}
	//---------------------------------------------------------
	bool InputActionMap::saveActions(String const & fileName)
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startWriting(fileName))
		{
			oDebugMsg("core",0,"InputActionMap: could not start writing action file: "<<fileName);
			return false;
		}

		ar << ACTIONS_FILE_MAGIC;
		int version = ACTIONS_FORMAT_VERSION;
		ar << version;

		unsigned int actionCount = static_cast<unsigned int>(mActions.size());
		ar << actionCount;
		for(InputAction const & action : mActions)
		{
			String name = action.name;
			ar << name;
			int kind = static_cast<int>(action.kind);
			ar << kind;

			unsigned int bindingCount = static_cast<unsigned int>(action.bindings.size());
			ar << bindingCount;
			for(InputActionBinding const & binding : action.bindings)
			{
				int type = static_cast<int>(binding.type);
				ar << type;
				int outputComponent = binding.outputComponent;
				ar << outputComponent;
				int tiltComponent = binding.tiltComponent;
				ar << tiltComponent;
				auto writeKeys = [&](std::vector<KeyEventData::KeyCode> const & keys)
				{
					unsigned int keyCount = static_cast<unsigned int>(keys.size());
					ar << keyCount;
					for(KeyEventData::KeyCode key : keys)
					{
						int keyCode = static_cast<int>(key);
						ar << keyCode;
					}
				};
				writeKeys(binding.keys);
				writeKeys(binding.negativeKeys);
				writeKeys(binding.positiveKeys);
				unsigned int gamepadButtonCount =
					static_cast<unsigned int>(binding.gamepadButtons.size());
				ar << gamepadButtonCount;
				for(Gamepad::Button button : binding.gamepadButtons)
				{
					int buttonCode = static_cast<int>(button);
					ar << buttonCode;
				}
				int gamepadAxis = static_cast<int>(binding.gamepadAxis);
				ar << gamepadAxis;
				float deadzone = binding.deadzone;
				ar << deadzone;
				bool invert = binding.invert;
				ar << invert;
			}
		}

		bool written = ar->stopWriting();
		if(!written)
		{
			oDebugMsg("core",0,"InputActionMap: error while writing action file: "<<fileName);
		}
		return written;
	}
	//---------------------------------------------------------
	void InputActionMap::loadForProject(Project const & project)
	{
		const String reference = project.getSetting(ACTIONS_SETTING_KEY);
		if(reference.empty())
		{
			// no override authored: the built-in defaults stand
			this->loadDefaults();
			return;
		}
		const String path = project.resolvePath(reference);
		if(!this->loadActions(path))
		{
			// a referenced-but-broken file must not leave the game input-less:
			// fall back to the defaults (loadActions already logged the reason)
			oDebugMsg("core",0,"InputActionMap: action override '"<<reference
				<<"' could not be loaded - keeping the built-in defaults");
			this->loadDefaults();
		}
	}
	//---------------------------------------------------------
	void InputActionMap::update(float deltaTime)
	{
		(void)deltaTime;	// edge state is frame-discrete, not time-integrated
		InputManager* input = InputManager::getSingletonPtr();
		for(InputAction & action : mActions)
		{
			// recompute the two components from every binding (max-magnitude)
			float value[2] = { 0.0f, 0.0f };
			if(input)
			{
				for(InputActionBinding const & binding : action.bindings)
				{
					const int component = (binding.outputComponent == 1) ? 1 : 0;
					value[component] = combineMaxMagnitude(value[component],
						evaluateBinding(binding, *input));
				}
			}
			action.value[0] = value[0];
			action.value[1] = value[1];

			// the ONE edge snapshot of the frame: down from a magnitude
			// threshold (digital keys give exactly 1, analog axes cross 0.5),
			// pressed/released from the transition since last frame
			const bool wasDown = action.down;
			const bool nowDown =
				std::sqrt(value[0] * value[0] + value[1] * value[1]) > 0.5f;
			action.pressed = nowDown && !wasDown;
			action.released = !nowDown && wasDown;
			action.down = nowDown;
		}
	}
	//---------------------------------------------------------
	bool InputActionMap::down(String const & name) const
	{
		InputAction const * action = this->findAction(name);
		return action ? action->down : false;
	}
	//---------------------------------------------------------
	bool InputActionMap::pressed(String const & name) const
	{
		InputAction const * action = this->findAction(name);
		return action ? action->pressed : false;
	}
	//---------------------------------------------------------
	bool InputActionMap::released(String const & name) const
	{
		InputAction const * action = this->findAction(name);
		return action ? action->released : false;
	}
	//---------------------------------------------------------
	float InputActionMap::value(String const & name) const
	{
		InputAction const * action = this->findAction(name);
		return action ? action->value[0] : 0.0f;
	}
	//---------------------------------------------------------
	Vec2 InputActionMap::value2(String const & name) const
	{
		InputAction const * action = this->findAction(name);
		if(!action)
		{
			return Vec2(0.0f, 0.0f);
		}
		return Vec2(action->value[0], action->value[1]);
	}
	//---------------------------------------------------------
	bool InputActionMap::hasAction(String const & name) const
	{
		return this->findAction(name) != NULL;
	}
	//---------------------------------------------------------
	void InputActionMap::setAction(InputAction const & action)
	{
		for(InputAction & existing : mActions)
		{
			if(existing.name == action.name)
			{
				existing = action;
				return;
			}
		}
		mActions.push_back(action);
	}
	//---------------------------------------------------------
	float InputActionMap::axisFromKeys(bool negativeDown, bool positiveDown)
	{
		return (positiveDown ? 1.0f : 0.0f) - (negativeDown ? 1.0f : 0.0f);
	}
	//---------------------------------------------------------
	float InputActionMap::combineMaxMagnitude(float current, float candidate)
	{
		return (std::fabs(candidate) >= std::fabs(current)) ? candidate : current;
	}
	//---------------------------------------------------------
	float InputActionMap::applyDeadzone(float raw, float deadzone)
	{
		if(!std::isfinite(raw) || !std::isfinite(deadzone))
		{
			return 0.0f;
		}
		const float magnitude = std::fabs(raw);
		if(deadzone <= 0.0f)
		{
			return std::clamp(raw, -1.0f, 1.0f);
		}
		if(deadzone >= 1.0f || magnitude <= deadzone)
		{
			return 0.0f;
		}
		// rescale the live band onto the full range: the first movement past
		// the zone starts at 0 instead of jumping to the zone's width
		const float scaled = (magnitude - deadzone) / (1.0f - deadzone);
		const float clamped = std::min(scaled, 1.0f);
		return (raw < 0.0f) ? -clamped : clamped;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	InputAction const * InputActionMap::findAction(String const & name) const
	{
		for(InputAction const & action : mActions)
		{
			if(action.name == name)
			{
				return &action;
			}
		}
		return NULL;
	}
	//---------------------------------------------------------
	float InputActionMap::evaluateBinding(InputActionBinding const & binding,
		InputManager & input)
	{
		switch(binding.type)
		{
		case InputActionBinding::Key:
			return anyKeyDown(binding.keys, input) ? 1.0f : 0.0f;
		case InputActionBinding::KeyAxis:
			return axisFromKeys(anyKeyDown(binding.negativeKeys, input),
				anyKeyDown(binding.positiveKeys, input));
		case InputActionBinding::TiltAxis:
		{
			// tilt is (0,-1,0) at rest: read the COMPONENT (0 at rest), never
			// the vector's -1 y (see InputManager::getTilt)
			const Vec3 tilt = input.getTilt();
			return (binding.tiltComponent == 1) ? tilt.y : tilt.x;
		}
		case InputActionBinding::GamepadButton:
			return anyGamepadButtonDown(binding.gamepadButtons, input)
				? 1.0f : 0.0f;
		case InputActionBinding::GamepadAxis:
		{
			// the raw reading is UNDEADZONED (InputManager keeps it that way,
			// so one stick can feed several actions at different tolerances):
			// the binding's own zone is applied here
			const float value = applyDeadzone(
				input.getGamepadAxis(binding.gamepadAxis), binding.deadzone);
			return binding.invert ? -value : value;
		}
		default:
			return 0.0f;
		}
	}
	//---------------------------------------------------------
	// the Lua face is registered in engine_module/module.cpp (OSIMPLEEXPORT as
	// "InputActions"): scripts reach it via InputActions.getSingleton().
}
