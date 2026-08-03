/**************************************************************
	created:	2026/07/30 at 09:12
	filename: 	InputInjection.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_input/InputInjection.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace Orkige
{
	namespace
	{
		//! one row of the key-name table: the canonical name and its code
		struct KeyNameRow
		{
			const char *			name;
			KeyEventData::KeyCode	key;
		};

		//! @brief the canonical name table: the keys a GAME reads, in the order
		//! a doc or an error message should list them. Deliberately a curated
		//! subset of the OIS-era enum (the exotic Japanese/media/NEC codes carry
		//! no gameplay meaning); every entry here has an SDL scancode, so a
		//! named key really reaches InputManager::isKeyDown.
		const KeyNameRow KEY_NAME_TABLE[] =
		{
			{ "LEFT",		KeyEventData::KC_LEFT },
			{ "RIGHT",		KeyEventData::KC_RIGHT },
			{ "UP",			KeyEventData::KC_UP },
			{ "DOWN",		KeyEventData::KC_DOWN },
			{ "SPACE",		KeyEventData::KC_SPACE },
			{ "RETURN",		KeyEventData::KC_RETURN },
			{ "ESCAPE",		KeyEventData::KC_ESCAPE },
			{ "TAB",		KeyEventData::KC_TAB },
			{ "BACK",		KeyEventData::KC_BACK },
			{ "DELETE",		KeyEventData::KC_DELETE },
			{ "INSERT",		KeyEventData::KC_INSERT },
			{ "HOME",		KeyEventData::KC_HOME },
			{ "END",		KeyEventData::KC_END },
			{ "PGUP",		KeyEventData::KC_PGUP },
			{ "PGDOWN",		KeyEventData::KC_PGDOWN },
			{ "LSHIFT",		KeyEventData::KC_LSHIFT },
			{ "RSHIFT",		KeyEventData::KC_RSHIFT },
			{ "LCONTROL",	KeyEventData::KC_LCONTROL },
			{ "RCONTROL",	KeyEventData::KC_RCONTROL },
			{ "LMENU",		KeyEventData::KC_LMENU },
			{ "RMENU",		KeyEventData::KC_RMENU },
			{ "MINUS",		KeyEventData::KC_MINUS },
			{ "EQUALS",		KeyEventData::KC_EQUALS },
			{ "COMMA",		KeyEventData::KC_COMMA },
			{ "PERIOD",		KeyEventData::KC_PERIOD },
			{ "SLASH",		KeyEventData::KC_SLASH },
			{ "GRAVE",		KeyEventData::KC_GRAVE },
			//! the Android hardware back button (trapped and delivered as a key)
			{ "WEBBACK",	KeyEventData::KC_WEBBACK },
			{ "A",			KeyEventData::KC_A },
			{ "B",			KeyEventData::KC_B },
			{ "C",			KeyEventData::KC_C },
			{ "D",			KeyEventData::KC_D },
			{ "E",			KeyEventData::KC_E },
			{ "F",			KeyEventData::KC_F },
			{ "G",			KeyEventData::KC_G },
			{ "H",			KeyEventData::KC_H },
			{ "I",			KeyEventData::KC_I },
			{ "J",			KeyEventData::KC_J },
			{ "K",			KeyEventData::KC_K },
			{ "L",			KeyEventData::KC_L },
			{ "M",			KeyEventData::KC_M },
			{ "N",			KeyEventData::KC_N },
			{ "O",			KeyEventData::KC_O },
			{ "P",			KeyEventData::KC_P },
			{ "Q",			KeyEventData::KC_Q },
			{ "R",			KeyEventData::KC_R },
			{ "S",			KeyEventData::KC_S },
			{ "T",			KeyEventData::KC_T },
			{ "U",			KeyEventData::KC_U },
			{ "V",			KeyEventData::KC_V },
			{ "W",			KeyEventData::KC_W },
			{ "X",			KeyEventData::KC_X },
			{ "Y",			KeyEventData::KC_Y },
			{ "Z",			KeyEventData::KC_Z },
			{ "0",			KeyEventData::KC_0 },
			{ "1",			KeyEventData::KC_1 },
			{ "2",			KeyEventData::KC_2 },
			{ "3",			KeyEventData::KC_3 },
			{ "4",			KeyEventData::KC_4 },
			{ "5",			KeyEventData::KC_5 },
			{ "6",			KeyEventData::KC_6 },
			{ "7",			KeyEventData::KC_7 },
			{ "8",			KeyEventData::KC_8 },
			{ "9",			KeyEventData::KC_9 },
			{ "F1",			KeyEventData::KC_F1 },
			{ "F2",			KeyEventData::KC_F2 },
			{ "F3",			KeyEventData::KC_F3 },
			{ "F4",			KeyEventData::KC_F4 },
			{ "F5",			KeyEventData::KC_F5 },
			{ "F6",			KeyEventData::KC_F6 },
			{ "F7",			KeyEventData::KC_F7 },
			{ "F8",			KeyEventData::KC_F8 },
			{ "F9",			KeyEventData::KC_F9 },
			{ "F10",		KeyEventData::KC_F10 },
			{ "F11",		KeyEventData::KC_F11 },
			{ "F12",		KeyEventData::KC_F12 }
		};

		//! friendly spellings that resolve to a canonical row (never listed by
		//! allNames - one canonical name per key keeps the docs honest)
		const KeyNameRow KEY_ALIAS_TABLE[] =
		{
			{ "ENTER",		KeyEventData::KC_RETURN },
			{ "ESC",		KeyEventData::KC_ESCAPE },
			{ "BACKSPACE",	KeyEventData::KC_BACK },
			{ "DEL",		KeyEventData::KC_DELETE },
			{ "SHIFT",		KeyEventData::KC_LSHIFT },
			{ "CTRL",		KeyEventData::KC_LCONTROL },
			{ "CONTROL",	KeyEventData::KC_LCONTROL },
			{ "ALT",		KeyEventData::KC_LMENU },
			{ "PAGEUP",		KeyEventData::KC_PGUP },
			{ "PAGEDOWN",	KeyEventData::KC_PGDOWN }
		};

		//! one row of the gamepad button-name table
		struct GamepadButtonRow
		{
			const char *	name;
			Gamepad::Button	button;
		};

		//! the canonical (POSITIONAL) button names, in layout order
		const GamepadButtonRow GAMEPAD_BUTTON_TABLE[] =
		{
			{ "SOUTH",			Gamepad::GB_SOUTH },
			{ "EAST",			Gamepad::GB_EAST },
			{ "WEST",			Gamepad::GB_WEST },
			{ "NORTH",			Gamepad::GB_NORTH },
			{ "BACK",			Gamepad::GB_BACK },
			{ "GUIDE",			Gamepad::GB_GUIDE },
			{ "START",			Gamepad::GB_START },
			{ "LEFTSTICK",		Gamepad::GB_LEFTSTICK },
			{ "RIGHTSTICK",		Gamepad::GB_RIGHTSTICK },
			{ "LEFTSHOULDER",	Gamepad::GB_LEFTSHOULDER },
			{ "RIGHTSHOULDER",	Gamepad::GB_RIGHTSHOULDER },
			{ "DPUP",			Gamepad::GB_DPAD_UP },
			{ "DPDOWN",			Gamepad::GB_DPAD_DOWN },
			{ "DPLEFT",			Gamepad::GB_DPAD_LEFT },
			{ "DPRIGHT",		Gamepad::GB_DPAD_RIGHT }
		};

		//! lettered / friendly spellings resolving to a canonical row (never
		//! listed by allButtonNames - one canonical name per button)
		const GamepadButtonRow GAMEPAD_BUTTON_ALIAS_TABLE[] =
		{
			{ "A",			Gamepad::GB_SOUTH },
			{ "B",			Gamepad::GB_EAST },
			{ "X",			Gamepad::GB_WEST },
			{ "Y",			Gamepad::GB_NORTH },
			{ "SELECT",		Gamepad::GB_BACK },
			{ "MENU",		Gamepad::GB_START },
			{ "L1",			Gamepad::GB_LEFTSHOULDER },
			{ "R1",			Gamepad::GB_RIGHTSHOULDER },
			{ "L3",			Gamepad::GB_LEFTSTICK },
			{ "R3",			Gamepad::GB_RIGHTSTICK },
			{ "DPADUP",		Gamepad::GB_DPAD_UP },
			{ "DPADDOWN",	Gamepad::GB_DPAD_DOWN },
			{ "DPADLEFT",	Gamepad::GB_DPAD_LEFT },
			{ "DPADRIGHT",	Gamepad::GB_DPAD_RIGHT }
		};

		//! one row of the gamepad axis-name table
		struct GamepadAxisRow
		{
			const char *	name;
			Gamepad::Axis	axis;
		};

		//! the canonical axis names, in layout order
		const GamepadAxisRow GAMEPAD_AXIS_TABLE[] =
		{
			{ "LEFTX",			Gamepad::GA_LEFTX },
			{ "LEFTY",			Gamepad::GA_LEFTY },
			{ "RIGHTX",			Gamepad::GA_RIGHTX },
			{ "RIGHTY",			Gamepad::GA_RIGHTY },
			{ "LEFTTRIGGER",	Gamepad::GA_LEFTTRIGGER },
			{ "RIGHTTRIGGER",	Gamepad::GA_RIGHTTRIGGER }
		};

		//! friendly axis spellings (never listed by allAxisNames)
		const GamepadAxisRow GAMEPAD_AXIS_ALIAS_TABLE[] =
		{
			{ "LX",	Gamepad::GA_LEFTX },
			{ "LY",	Gamepad::GA_LEFTY },
			{ "RX",	Gamepad::GA_RIGHTX },
			{ "RY",	Gamepad::GA_RIGHTY },
			{ "LT",	Gamepad::GA_LEFTTRIGGER },
			{ "RT",	Gamepad::GA_RIGHTTRIGGER }
		};

		//! upper-case an ASCII token (the grammar is case-insensitive)
		String upperCase(String const & text)
		{
			String out = text;
			for (char & each : out)
			{
				if (each >= 'a' && each <= 'z')
				{
					each = static_cast<char>(each - 'a' + 'A');
				}
			}
			return out;
		}

		//! split a step string on ASCII whitespace (empty tokens dropped)
		StringVector tokenize(String const & step)
		{
			StringVector tokens;
			String current;
			for (char each : step)
			{
				if (each == ' ' || each == '\t' || each == '\n' ||
					each == '\r')
				{
					if (!current.empty())
					{
						tokens.push_back(current);
						current.clear();
					}
				}
				else
				{
					current += each;
				}
			}
			if (!current.empty())
			{
				tokens.push_back(current);
			}
			return tokens;
		}

		//! strict float parse: the WHOLE token must be a finite number
		bool parseFloat(String const & token, float & out)
		{
			if (token.empty())
			{
				return false;
			}
			char * end = NULL;
			const double value = std::strtod(token.c_str(), &end);
			if (end == NULL || *end != '\0' || !std::isfinite(value))
			{
				return false;
			}
			out = static_cast<float>(value);
			return true;
		}

		//! strict frame-count parse: a positive decimal integer
		bool parseFrames(String const & token, unsigned int & out)
		{
			if (token.empty())
			{
				return false;
			}
			unsigned long long value = 0;
			for (char each : token)
			{
				if (each < '0' || each > '9')
				{
					return false;
				}
				value = value * 10 + static_cast<unsigned long long>(each - '0');
				if (value > InputInjection::MAX_FRAMES)
				{
					return false;
				}
			}
			if (value == 0)
			{
				return false;
			}
			out = static_cast<unsigned int>(value);
			return true;
		}

		//! strict index parse: a decimal integer, ZERO allowed (finger numbers
		//! start at 0, unlike frame counts)
		bool parseIndex(String const & token, unsigned int & out)
		{
			if (token.empty() || token.size() > 4)
			{
				return false;
			}
			unsigned int value = 0;
			for (char each : token)
			{
				if (each < '0' || each > '9')
				{
					return false;
				}
				value = value * 10 + static_cast<unsigned int>(each - '0');
			}
			out = value;
			return true;
		}

		//! the pointer button a token names (default Left on an empty token)
		bool parseButton(String const & token,
			InputInjection::PointerButton & out)
		{
			const String name = upperCase(token);
			if (name.empty() || name == "LEFT")
			{
				out = InputInjection::PointerButton::Left;
				return true;
			}
			if (name == "MIDDLE")
			{
				out = InputInjection::PointerButton::Middle;
				return true;
			}
			if (name == "RIGHT")
			{
				out = InputInjection::PointerButton::Right;
				return true;
			}
			return false;
		}

		//! "step 3: <reason>" - every refusal names the offending step
		String stepError(std::size_t index, String const & reason)
		{
			std::ostringstream text;
			text << "step " << (index + 1) << " (" << reason << ")";
			return text.str();
		}
	}

	//---------------------------------------------------------
	//--- KeyCodeNames ----------------------------------------
	//---------------------------------------------------------
	KeyEventData::KeyCode KeyCodeNames::fromName(String const & name)
	{
		String wanted = upperCase(name);
		if (wanted.rfind("KC_", 0) == 0)
		{
			wanted = wanted.substr(3);
		}
		if (wanted.empty())
		{
			return KeyEventData::KC_UNASSIGNED;
		}
		for (KeyNameRow const & row : KEY_NAME_TABLE)
		{
			if (wanted == row.name)
			{
				return row.key;
			}
		}
		for (KeyNameRow const & row : KEY_ALIAS_TABLE)
		{
			if (wanted == row.name)
			{
				return row.key;
			}
		}
		return KeyEventData::KC_UNASSIGNED;
	}
	//---------------------------------------------------------
	String KeyCodeNames::toName(KeyEventData::KeyCode key)
	{
		for (KeyNameRow const & row : KEY_NAME_TABLE)
		{
			if (row.key == key)
			{
				return row.name;
			}
		}
		return String();
	}
	//---------------------------------------------------------
	StringVector KeyCodeNames::allNames()
	{
		StringVector names;
		names.reserve(sizeof(KEY_NAME_TABLE) / sizeof(KEY_NAME_TABLE[0]));
		for (KeyNameRow const & row : KEY_NAME_TABLE)
		{
			names.push_back(row.name);
		}
		return names;
	}

	//---------------------------------------------------------
	//--- GamepadNames ----------------------------------------
	//---------------------------------------------------------
	Gamepad::Button GamepadNames::buttonFromName(String const & name)
	{
		const String wanted = upperCase(name);
		if (wanted.empty())
		{
			return Gamepad::GB_COUNT;
		}
		for (GamepadButtonRow const & row : GAMEPAD_BUTTON_TABLE)
		{
			if (wanted == row.name)
			{
				return row.button;
			}
		}
		for (GamepadButtonRow const & row : GAMEPAD_BUTTON_ALIAS_TABLE)
		{
			if (wanted == row.name)
			{
				return row.button;
			}
		}
		return Gamepad::GB_COUNT;
	}
	//---------------------------------------------------------
	String GamepadNames::buttonToName(Gamepad::Button button)
	{
		for (GamepadButtonRow const & row : GAMEPAD_BUTTON_TABLE)
		{
			if (row.button == button)
			{
				return row.name;
			}
		}
		return String();
	}
	//---------------------------------------------------------
	StringVector GamepadNames::allButtonNames()
	{
		StringVector names;
		names.reserve(sizeof(GAMEPAD_BUTTON_TABLE) /
			sizeof(GAMEPAD_BUTTON_TABLE[0]));
		for (GamepadButtonRow const & row : GAMEPAD_BUTTON_TABLE)
		{
			names.push_back(row.name);
		}
		return names;
	}
	//---------------------------------------------------------
	Gamepad::Axis GamepadNames::axisFromName(String const & name)
	{
		const String wanted = upperCase(name);
		if (wanted.empty())
		{
			return Gamepad::GA_COUNT;
		}
		for (GamepadAxisRow const & row : GAMEPAD_AXIS_TABLE)
		{
			if (wanted == row.name)
			{
				return row.axis;
			}
		}
		for (GamepadAxisRow const & row : GAMEPAD_AXIS_ALIAS_TABLE)
		{
			if (wanted == row.name)
			{
				return row.axis;
			}
		}
		return Gamepad::GA_COUNT;
	}
	//---------------------------------------------------------
	String GamepadNames::axisToName(Gamepad::Axis axis)
	{
		for (GamepadAxisRow const & row : GAMEPAD_AXIS_TABLE)
		{
			if (row.axis == axis)
			{
				return row.name;
			}
		}
		return String();
	}
	//---------------------------------------------------------
	StringVector GamepadNames::allAxisNames()
	{
		StringVector names;
		names.reserve(sizeof(GAMEPAD_AXIS_TABLE) /
			sizeof(GAMEPAD_AXIS_TABLE[0]));
		for (GamepadAxisRow const & row : GAMEPAD_AXIS_TABLE)
		{
			names.push_back(row.name);
		}
		return names;
	}

	//---------------------------------------------------------
	//--- InputInjection --------------------------------------
	//---------------------------------------------------------
	float InputInjection::tiltAngleFromVector(float x, float y)
	{
		// InputManager::tiltVectorFromAngle maps a -> (sin a, -cos a, 0), so
		// the inverse is atan2(x, -y); a zero vector has no direction and
		// reads as upright
		if (!std::isfinite(x) || !std::isfinite(y) ||
			(x == 0.0f && y == 0.0f))
		{
			return 0.0f;
		}
		return std::atan2(x, -y);
	}
	//---------------------------------------------------------
	bool InputInjection::compile(StringVector const & steps,
		Sequence & outSequence, String & outError)
	{
		// compiled into a LOCAL sequence and handed over only on success: a
		// refusal must never leave the caller a half-built gesture it could
		// mistake for a valid one
		Sequence working;
		outSequence = Sequence();
		outError.clear();
		if (steps.empty())
		{
			outError = "no input steps given";
			return false;
		}
		if (steps.size() > MAX_STEPS)
		{
			std::ostringstream text;
			text << "too many input steps (" << steps.size() << " > "
				<< MAX_STEPS << ")";
			outError = text.str();
			return false;
		}
		unsigned int cursor = 0;		//!< the frame the next step starts on
		unsigned int lastFrame = 0;		//!< highest frame an event landed on
		bool anyEvent = false;
		// advance the cursor, refusing a gesture past the frame bound
		auto advance = [&](unsigned int frames, std::size_t index) -> bool
		{
			if (cursor + frames > MAX_FRAMES)
			{
				outError = stepError(index,
					"the sequence would span more than " +
					std::to_string(MAX_FRAMES) + " frames");
				return false;
			}
			cursor += frames;
			return true;
		};
		auto emit = [&](Event event)
		{
			event.frame = cursor;
			lastFrame = std::max(lastFrame, event.frame);
			anyEvent = true;
			working.events.push_back(event);
		};
		// emit at an explicit frame (the release edge of a held press)
		auto emitAt = [&](Event event, unsigned int frame)
		{
			event.frame = frame;
			lastFrame = std::max(lastFrame, event.frame);
			anyEvent = true;
			working.events.push_back(event);
		};

		for (std::size_t index = 0; index < steps.size(); ++index)
		{
			const StringVector tokens = tokenize(steps[index]);
			if (tokens.empty())
			{
				outError = stepError(index, "empty step");
				return false;
			}
			const String verb = upperCase(tokens[0]);
			if (verb == "WAIT")
			{
				unsigned int frames = 0;
				if (tokens.size() != 2 || !parseFrames(tokens[1], frames))
				{
					outError = stepError(index,
						"'wait' needs a frame count of 1.." +
						std::to_string(MAX_FRAMES));
					return false;
				}
				if (!advance(frames, index))
				{
					return false;
				}
				continue;
			}
			if (verb == "KEY")
			{
				if (tokens.size() < 3)
				{
					outError = stepError(index,
						"'key' needs down|up|press and a key name");
					return false;
				}
				const String action = upperCase(tokens[1]);
				const KeyEventData::KeyCode key =
					KeyCodeNames::fromName(tokens[2]);
				if (key == KeyEventData::KC_UNASSIGNED)
				{
					outError = stepError(index,
						"unknown key name '" + tokens[2] + "'");
					return false;
				}
				Event event;
				event.key = key;
				if (action == "DOWN" || action == "UP")
				{
					if (tokens.size() != 3)
					{
						outError = stepError(index,
							"'key " + action + "' takes only a key name");
						return false;
					}
					event.kind = action == "DOWN" ? EventKind::KeyDown
						: EventKind::KeyUp;
					emit(event);
					continue;
				}
				if (action == "PRESS")
				{
					unsigned int frames = 1;
					if (tokens.size() == 4)
					{
						if (!parseFrames(tokens[3], frames))
						{
							outError = stepError(index,
								"'key press' hold frames must be 1.." +
								std::to_string(MAX_FRAMES));
							return false;
						}
					}
					else if (tokens.size() != 3)
					{
						outError = stepError(index,
							"'key press' takes a key name and optional hold "
							"frames");
						return false;
					}
					event.kind = EventKind::KeyDown;
					emit(event);
					const unsigned int downFrame = cursor;
					if (!advance(frames, index))
					{
						return false;
					}
					Event release;
					release.key = key;
					release.kind = EventKind::KeyUp;
					emitAt(release, downFrame + frames);
					continue;
				}
				outError = stepError(index,
					"'key' action must be down, up or press (got '" +
					tokens[1] + "')");
				return false;
			}
			if (verb == "POINTER")
			{
				if (tokens.size() < 4)
				{
					outError = stepError(index,
						"'pointer' needs move|down|up|click and x y");
					return false;
				}
				const String action = upperCase(tokens[1]);
				float x = 0.0f;
				float y = 0.0f;
				if (!parseFloat(tokens[2], x) || !parseFloat(tokens[3], y))
				{
					outError = stepError(index,
						"'pointer' x/y must be numbers (window pixels)");
					return false;
				}
				PointerButton button = PointerButton::Left;
				if (tokens.size() == 5)
				{
					if (!parseButton(tokens[4], button))
					{
						outError = stepError(index,
							"'pointer' button must be left, middle or right "
							"(got '" + tokens[4] + "')");
						return false;
					}
				}
				else if (tokens.size() != 4)
				{
					outError = stepError(index,
						"'pointer' takes an action, x, y and an optional "
						"button");
					return false;
				}
				Event event;
				event.x = x;
				event.y = y;
				event.button = button;
				if (action == "MOVE")
				{
					event.kind = EventKind::PointerMove;
					emit(event);
					continue;
				}
				if (action == "DOWN" || action == "UP")
				{
					event.kind = action == "DOWN" ? EventKind::PointerDown
						: EventKind::PointerUp;
					emit(event);
					continue;
				}
				if (action == "CLICK")
				{
					// move onto the target, press, hold one frame, release -
					// the shape a widget's hit test and a game's tap gesture
					// both expect
					Event move = event;
					move.kind = EventKind::PointerMove;
					emit(move);
					Event press = event;
					press.kind = EventKind::PointerDown;
					emit(press);
					const unsigned int downFrame = cursor;
					if (!advance(1, index))
					{
						return false;
					}
					Event release = event;
					release.kind = EventKind::PointerUp;
					emitAt(release, downFrame + 1);
					continue;
				}
				outError = stepError(index,
					"'pointer' action must be move, down, up or click (got '" +
					tokens[1] + "')");
				return false;
			}
			if (verb == "TOUCH")
			{
				// touch <id> down|move|up|tap <x> <y> - the finger number is
				// the CALLER's, so a two-finger gesture is two interleaved
				// step streams with stable ids
				if (tokens.size() != 5)
				{
					outError = stepError(index,
						"'touch' needs a finger id, down|move|up|tap and x y");
					return false;
				}
				unsigned int touchId = 0;
				if (!parseIndex(tokens[1], touchId) ||
					static_cast<int>(touchId) > MAX_TOUCH_ID)
				{
					outError = stepError(index,
						"'touch' finger id must be 0.." +
						std::to_string(MAX_TOUCH_ID));
					return false;
				}
				const String action = upperCase(tokens[2]);
				float x = 0.0f;
				float y = 0.0f;
				if (!parseFloat(tokens[3], x) || !parseFloat(tokens[4], y))
				{
					outError = stepError(index,
						"'touch' x/y must be numbers (window pixels)");
					return false;
				}
				Event event;
				event.touchId = static_cast<int>(touchId);
				event.x = x;
				event.y = y;
				if (action == "DOWN" || action == "MOVE" || action == "UP")
				{
					event.kind = action == "DOWN" ? EventKind::TouchDown
						: (action == "MOVE" ? EventKind::TouchMove
							: EventKind::TouchUp);
					emit(event);
					continue;
				}
				if (action == "TAP")
				{
					// down, held one frame, up - the shape a tap gesture and a
					// widget hit test both expect (the pointer `click` sibling)
					Event press = event;
					press.kind = EventKind::TouchDown;
					emit(press);
					const unsigned int downFrame = cursor;
					if (!advance(1, index))
					{
						return false;
					}
					Event release = event;
					release.kind = EventKind::TouchUp;
					emitAt(release, downFrame + 1);
					continue;
				}
				outError = stepError(index,
					"'touch' action must be down, move, up or tap (got '" +
					tokens[2] + "')");
				return false;
			}
			if (verb == "GAMEPAD")
			{
				if (tokens.size() < 4)
				{
					outError = stepError(index,
						"'gamepad' needs button <NAME> down|up|press or "
						"axis <NAME> <value>");
					return false;
				}
				const String what = upperCase(tokens[1]);
				if (what == "BUTTON")
				{
					const Gamepad::Button button =
						GamepadNames::buttonFromName(tokens[2]);
					if (button == Gamepad::GB_COUNT)
					{
						outError = stepError(index,
							"unknown gamepad button '" + tokens[2] + "'");
						return false;
					}
					const String action = upperCase(tokens[3]);
					Event event;
					event.gamepadButton = button;
					if (action == "DOWN" || action == "UP")
					{
						if (tokens.size() != 4)
						{
							outError = stepError(index,
								"'gamepad button " + action +
								"' takes only a button name");
							return false;
						}
						event.kind = action == "DOWN"
							? EventKind::GamepadButtonDown
							: EventKind::GamepadButtonUp;
						emit(event);
						continue;
					}
					if (action == "PRESS")
					{
						unsigned int frames = 1;
						if (tokens.size() == 5)
						{
							if (!parseFrames(tokens[4], frames))
							{
								outError = stepError(index,
									"'gamepad button press' hold frames must "
									"be 1.." + std::to_string(MAX_FRAMES));
								return false;
							}
						}
						else if (tokens.size() != 4)
						{
							outError = stepError(index,
								"'gamepad button press' takes a button name "
								"and optional hold frames");
							return false;
						}
						event.kind = EventKind::GamepadButtonDown;
						emit(event);
						const unsigned int downFrame = cursor;
						if (!advance(frames, index))
						{
							return false;
						}
						Event release;
						release.gamepadButton = button;
						release.kind = EventKind::GamepadButtonUp;
						emitAt(release, downFrame + frames);
						continue;
					}
					outError = stepError(index,
						"'gamepad button' action must be down, up or press "
						"(got '" + tokens[3] + "')");
					return false;
				}
				if (what == "AXIS")
				{
					const Gamepad::Axis axis =
						GamepadNames::axisFromName(tokens[2]);
					if (axis == Gamepad::GA_COUNT)
					{
						outError = stepError(index,
							"unknown gamepad axis '" + tokens[2] + "'");
						return false;
					}
					float value = 0.0f;
					if (tokens.size() != 4 || !parseFloat(tokens[3], value))
					{
						outError = stepError(index,
							"'gamepad axis' needs one number (sticks -1..1, "
							"triggers 0..1)");
						return false;
					}
					if (value < -1.0f || value > 1.0f)
					{
						outError = stepError(index,
							"'gamepad axis' value must be within -1..1");
						return false;
					}
					Event event;
					event.kind = EventKind::GamepadAxis;
					event.gamepadAxis = axis;
					event.axisValue = value;
					emit(event);
					continue;
				}
				outError = stepError(index,
					"'gamepad' must be button or axis (got '" + tokens[1] +
					"')");
				return false;
			}
			if (verb == "TILT")
			{
				if (tokens.size() < 3)
				{
					outError = stepError(index,
						"'tilt' needs angle <radians> or vector <x> <y>");
					return false;
				}
				const String action = upperCase(tokens[1]);
				Event event;
				event.kind = EventKind::TiltAngle;
				if (action == "ANGLE")
				{
					float radians = 0.0f;
					if (tokens.size() != 3 ||
						!parseFloat(tokens[2], radians))
					{
						outError = stepError(index,
							"'tilt angle' needs one number (radians)");
						return false;
					}
					event.angle = radians;
					emit(event);
					continue;
				}
				if (action == "VECTOR")
				{
					float x = 0.0f;
					float y = 0.0f;
					if (tokens.size() != 4 || !parseFloat(tokens[2], x) ||
						!parseFloat(tokens[3], y))
					{
						outError = stepError(index,
							"'tilt vector' needs two numbers (a gravity "
							"direction)");
						return false;
					}
					event.angle = tiltAngleFromVector(x, y);
					emit(event);
					continue;
				}
				outError = stepError(index,
					"'tilt' must be angle or vector (got '" + tokens[1] +
					"')");
				return false;
			}
			outError = stepError(index,
				"unknown step verb '" + tokens[0] +
				"' (expected key, pointer, touch, gamepad, tilt or wait)");
			return false;
		}
		if (!anyEvent)
		{
			outError = "the sequence carries no input events (only waits)";
			return false;
		}
		if (lastFrame + 1 > MAX_FRAMES)
		{
			std::ostringstream text;
			text << "the sequence would span more than " << MAX_FRAMES
				<< " frames";
			outError = text.str();
			return false;
		}
		// frame order: the runtime walks the timeline once, frame by frame
		std::stable_sort(working.events.begin(), working.events.end(),
			[](Event const & left, Event const & right)
			{
				return left.frame < right.frame;
			});
		working.frameSpan = lastFrame + 1;
		outSequence = working;
		return true;
	}
}
