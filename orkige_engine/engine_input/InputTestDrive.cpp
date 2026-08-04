/**************************************************************
	created:	2026/08/04 at 09:32
	filename: 	InputTestDrive.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_input/InputTestDrive.h"

#include "engine_input/InputActionMap.h"
#include "engine_input/InputInjection.h"
#include "engine_input/InputManager.h"

#include <algorithm>

namespace Orkige
{
	namespace
	{
		//! the four direction suffixes, as (spelling, component, sign)
		struct DirectionRow
		{
			const char *	suffix;
			int				component;
			int				sign;
		};
		const DirectionRow DIRECTION_TABLE[] =
		{
			{ "+x", 0, +1 },
			{ "-x", 0, -1 },
			{ "+y", 1, +1 },
			{ "-y", 1, -1 }
		};

		//! the direction row a suffix spells, or NULL
		DirectionRow const * findDirection(String const & direction)
		{
			for(DirectionRow const & row : DIRECTION_TABLE)
			{
				if(direction.size() == 2 &&
					direction[0] == row.suffix[0] &&
					(direction[1] == row.suffix[1] ||
						direction[1] == row.suffix[1] - ('a' - 'A')))
				{
					return &row;
				}
			}
			return NULL;
		}

		//! the first key of a list, or KC_UNASSIGNED for an empty one. ONE key
		//! is all a press needs: every key of a binding reads the same +1, so
		//! pressing more of them would only make the release ambiguous.
		KeyEventData::KeyCode firstKey(
			std::vector<KeyEventData::KeyCode> const & keys)
		{
			return keys.empty() ? KeyEventData::KC_UNASSIGNED : keys.front();
		}

		//! @brief what an action is ACTUALLY bound to, for the refusal message:
		//! a test author who named something unpressable needs to read why in
		//! one line instead of opening the action file.
		String bindingSummary(InputAction const & action)
		{
			StringVector kinds;
			for(InputActionBinding const & binding : action.bindings)
			{
				String kind;
				switch(binding.type)
				{
				case InputActionBinding::Key:			kind = "keys"; break;
				case InputActionBinding::KeyAxis:		kind = "a key axis"; break;
				case InputActionBinding::TiltAxis:		kind = "tilt"; break;
				case InputActionBinding::GamepadButton:	kind = "a gamepad button"; break;
				case InputActionBinding::GamepadAxis:	kind = "a gamepad axis"; break;
				}
				if(!kind.empty() &&
					std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
				{
					kinds.push_back(kind);
				}
			}
			if(kinds.empty())
			{
				return "nothing";
			}
			String summary = kinds.front();
			for(std::size_t index = 1; index < kinds.size(); ++index)
			{
				summary += " and " + kinds[index];
			}
			return summary;
		}
	}

	namespace InputTestDrive
	{
		//---------------------------------------------------------
		void splitTarget(String const & target, String & outName,
			String & outDirection)
		{
			outName = target;
			outDirection.clear();
			if(target.size() <= 2)
			{
				// "+x" alone is a direction with nothing to steer: leave it
				// whole so the refusal names the target the caller wrote
				return;
			}
			const String tail = target.substr(target.size() - 2);
			if(findDirection(tail) != NULL)
			{
				outName = target.substr(0, target.size() - 2);
				outDirection = tail;
			}
		}
		//---------------------------------------------------------
		Target resolveKeys(InputAction const * action, String const & name,
			String const & direction)
		{
			Target target;
			if(action == NULL)
			{
				if(!direction.empty())
				{
					// a direction is an ACTION concept - a raw key has no
					// components to push, so this is a typo worth naming
					target.error = "'" + name + direction + "': '" + name +
						"' is not an action, and a direction only applies to "
						"one";
					return target;
				}
				const KeyEventData::KeyCode key = KeyCodeNames::fromName(name);
				if(key == KeyEventData::KC_UNASSIGNED)
				{
					target.error = "'" + name + "' is neither an action nor a "
						"key name";
					return target;
				}
				target.key = key;
				return target;
			}
			if(direction.empty())
			{
				// "press this action" means its DIGITAL button shape
				for(InputActionBinding const & binding : action->bindings)
				{
					if(binding.type == InputActionBinding::Key)
					{
						const KeyEventData::KeyCode key = firstKey(binding.keys);
						if(key != KeyEventData::KC_UNASSIGNED)
						{
							target.key = key;
							return target;
						}
					}
				}
				target.error = "the action '" + name + "' has no key binding "
					"to press (it is bound to " + bindingSummary(*action) +
					") - name a direction ('" + name + "+x') or press a key by "
					"name";
				return target;
			}
			DirectionRow const * row = findDirection(direction);
			if(row == NULL)
			{
				target.error = "'" + direction + "' is not a direction (+x, "
					"-x, +y or -y)";
				return target;
			}
			for(InputActionBinding const & binding : action->bindings)
			{
				if(binding.outputComponent != row->component)
				{
					continue;
				}
				if(binding.type == InputActionBinding::KeyAxis)
				{
					const KeyEventData::KeyCode key = firstKey(row->sign > 0
						? binding.positiveKeys : binding.negativeKeys);
					if(key != KeyEventData::KC_UNASSIGNED)
					{
						target.key = key;
						return target;
					}
				}
				else if(binding.type == InputActionBinding::Key &&
					row->sign > 0)
				{
					// a Key binding contributes +1 to its component, so it
					// answers the POSITIVE direction and nothing else
					const KeyEventData::KeyCode key = firstKey(binding.keys);
					if(key != KeyEventData::KC_UNASSIGNED)
					{
						target.key = key;
						return target;
					}
				}
			}
			target.error = "the action '" + name + "' has no key pushing " +
				direction + " (it is bound to " + bindingSummary(*action) + ")";
			return target;
		}
		//---------------------------------------------------------
		bool HeldKeys::hold(KeyEventData::KeyCode key)
		{
			if(this->holds(key))
			{
				return false;
			}
			this->mKeys.push_back(key);
			return true;
		}
		//---------------------------------------------------------
		bool HeldKeys::letGo(KeyEventData::KeyCode key)
		{
			std::vector<KeyEventData::KeyCode>::iterator it =
				std::find(this->mKeys.begin(), this->mKeys.end(), key);
			if(it == this->mKeys.end())
			{
				return false;
			}
			this->mKeys.erase(it);
			return true;
		}
		//---------------------------------------------------------
		bool HeldKeys::holds(KeyEventData::KeyCode key) const
		{
			return std::find(this->mKeys.begin(), this->mKeys.end(), key) !=
				this->mKeys.end();
		}
		//---------------------------------------------------------
		std::vector<KeyEventData::KeyCode> HeldKeys::takeAll()
		{
			std::vector<KeyEventData::KeyCode> keys;
			keys.swap(this->mKeys);
			return keys;
		}
		//---------------------------------------------------------
	}
	//---------------------------------------------------------
	InputTestDrive::Target InputTestDriver::resolve(String const & target) const
	{
		String name;
		String direction;
		InputTestDrive::splitTarget(target, name, direction);
		InputActionMap const * actions = InputActionMap::getSingletonPtr();
		InputAction const * action =
			actions != NULL ? actions->getAction(name) : NULL;
		return InputTestDrive::resolveKeys(action, name, direction);
	}
	//---------------------------------------------------------
	bool InputTestDriver::press(String const & target, String & outError)
	{
		const InputTestDrive::Target resolved = this->resolve(target);
		if(!resolved.ok())
		{
			outError = resolved.error;
			return false;
		}
		InputManager* input = InputManager::getSingletonPtr();
		if(input == NULL)
		{
			outError = "there is no input system to press '" + target + "' on";
			return false;
		}
		if(!this->mHeld.hold(resolved.key))
		{
			// already held: a second press would be a second down edge for a
			// key that never came up, which no real keyboard produces
			return true;
		}
		if(!input->injectKey(resolved.key, true))
		{
			this->mHeld.letGo(resolved.key);
			outError = "'" + target + "' has no key this platform can press";
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool InputTestDriver::release(String const & target, String & outError)
	{
		const InputTestDrive::Target resolved = this->resolve(target);
		if(!resolved.ok())
		{
			outError = resolved.error;
			return false;
		}
		if(!this->mHeld.letGo(resolved.key))
		{
			// not held: releasing what was never pressed is a no-op, not an
			// error - a test may release defensively
			return true;
		}
		InputManager* input = InputManager::getSingletonPtr();
		if(input != NULL)
		{
			input->injectKey(resolved.key, false);
		}
		return true;
	}
	//---------------------------------------------------------
	void InputTestDriver::releaseAll()
	{
		const std::vector<KeyEventData::KeyCode> keys = this->mHeld.takeAll();
		InputManager* input = InputManager::getSingletonPtr();
		if(input == NULL)
		{
			return;
		}
		for(KeyEventData::KeyCode key : keys)
		{
			input->injectKey(key, false);
		}
	}
	//---------------------------------------------------------
}
