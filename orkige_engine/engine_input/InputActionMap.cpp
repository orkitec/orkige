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

#include <cmath>

namespace Orkige
{
	IMPL_OSINGLETON(InputActionMap);

	const String InputActionMap::ACTIONS_SETTING_KEY = "input.actions";
	const String InputActionMap::ACTIONS_FILE_EXTENSION = ".oactions";
	const String InputActionMap::ACTIONS_FILE_MAGIC = "orkige.oactions";
	const int InputActionMap::ACTIONS_FORMAT_VERSION = 1;

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

		// jumper: 2D movement on WASD/arrows, jump on SPACE. move.x = the
		// left/right axis (A/LEFT negative, D/RIGHT positive), move.y = the
		// depth axis (W/UP negative, S/DOWN positive) - matches the original
		// axis() helper in projects/jumper-lua/scripts/player.lua.
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
			mActions.push_back(move);
		}
		mActions.push_back(makeDigital("jump", { KeyEventData::KC_SPACE }));

		// roller: steer on the tilt X component OR LEFT/RIGHT arrows (max
		// magnitude), and the move-world menu keys (TAB toggles, arrows slide).
		{
			InputAction steer;
			steer.name = "steer";
			steer.kind = InputActionKind::Analog1D;
			steer.bindings.push_back(makeTiltAxis(0, 0));
			steer.bindings.push_back(makeKeyAxis(
				{ KeyEventData::KC_LEFT }, { KeyEventData::KC_RIGHT }, 0));
			mActions.push_back(steer);
		}
		mActions.push_back(makeDigital("menu_toggle", { KeyEventData::KC_TAB }));
		mActions.push_back(makeDigital("menu_left", { KeyEventData::KC_LEFT }));
		mActions.push_back(makeDigital("menu_right", { KeyEventData::KC_RIGHT }));
		mActions.push_back(makeDigital("menu_up", { KeyEventData::KC_UP }));
		mActions.push_back(makeDigital("menu_down", { KeyEventData::KC_DOWN }));
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
		if(version > ACTIONS_FORMAT_VERSION)
		{
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
		default:
			return 0.0f;
		}
	}
	//---------------------------------------------------------
	// the Lua face is registered in engine_module/module.cpp (OSIMPLEEXPORT as
	// "InputActions"): scripts reach it via InputActions.getSingleton().
}
