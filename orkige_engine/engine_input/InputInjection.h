/**************************************************************
	created:	2026/07/30 at 09:10
	filename: 	InputInjection.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __InputInjection_h__30_7_2026__09_10_00__
#define __InputInjection_h__30_7_2026__09_10_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_input/KeyEventData.h"

#include <vector>

namespace Orkige
{
	//! @brief the TEXT vocabulary of KeyEventData::KeyCode: the ONE name table
	//! shared by everything that has to spell a key out loud.
	//! @remarks The engine's own carriers store key bindings as the raw enum
	//! VALUE (`.oactions` writes ints), so a name table only becomes necessary
	//! where a human or an agent authors the binding - the injected-input step
	//! grammar below. Names are the enum spellings without the `KC_` prefix
	//! ("SPACE", "LEFT", "A"), matched case-insensitively and with an optional
	//! `KC_` prefix, plus a handful of friendly aliases (ENTER, ESC, ...).
	//! Pure: no singleton, no SDL, headlessly unit-testable.
	namespace KeyCodeNames
	{
		//! @brief the KeyCode a name spells, or KC_UNASSIGNED when the name is
		//! not in the table. Case-insensitive; a leading "KC_" is optional.
		ORKIGE_ENGINE_DLL KeyEventData::KeyCode fromName(String const & name);
		//! @brief the canonical name of a KeyCode ("SPACE"), or "" when the
		//! code is outside the table
		ORKIGE_ENGINE_DLL String toName(KeyEventData::KeyCode key);
		//! @brief every canonical name, in table order - the vocabulary an
		//! error message or a doc listing quotes
		ORKIGE_ENGINE_DLL StringVector allNames();
	}

	//! @brief the pure half of AGENT-DRIVEN INPUT: a compact text step list
	//! ("key press SPACE 3") compiled into a FRAME-STAMPED event timeline the
	//! runtime replays one frame at a time.
	//! @remarks An agent playtesting a game needs "press right for 10 frames"
	//! as ONE request - a round trip per key edge would make every gesture
	//! wall-clock dependent and every assertion flaky. So the whole gesture
	//! travels as a list of step strings (the debug protocol carries flat
	//! string lists natively), is compiled HERE into (frame, event) pairs, and
	//! the runtime applies each frame's events at its frame boundary. The
	//! grammar, its limits and every refusal are pure decisions, so they are
	//! unit-tested without a window, a socket or a game.
	//! @par The grammar (one step per string, tokens whitespace-separated,
	//! verbs and key names case-insensitive)
	//! @code
	//!   key down <NAME>            press and hold
	//!   key up <NAME>              release
	//!   key press <NAME> [frames]  hold for `frames` frames (default 1)
	//!   pointer move <x> <y>
	//!   pointer down <x> <y> [button]   button = left|middle|right (default left)
	//!   pointer up <x> <y> [button]
	//!   pointer click <x> <y> [button]  move+press, held one frame, release
	//!   tilt angle <radians>       the simulated tilt angle (0 = upright)
	//!   tilt vector <x> <y>        a gravity direction, converted to an angle
	//!   wait <frames>              advance the timeline without an event
	//! @endcode
	//! Coordinates are WINDOW PIXELS of the running game's drawable (the same
	//! space MSG_STATS reports as window_w/window_h and gui rects live in).
	namespace InputInjection
	{
		//! the widest gesture a single sequence may span, in frames (10s at
		//! 60fps): a bound so one request can never occupy a session for long
		static const unsigned int MAX_FRAMES = 600;
		//! the most steps one sequence may carry
		static const unsigned int MAX_STEPS = 256;

		//! what one compiled event does when its frame comes up
		enum class EventKind
		{
			KeyDown,
			KeyUp,
			PointerMove,
			PointerDown,
			PointerUp,
			TiltAngle
		};

		//! a mouse/touch button, kept in the vocabulary the grammar spells
		enum class PointerButton
		{
			Left,
			Middle,
			Right
		};

		//! ONE compiled event: what to do, and on which frame of the sequence
		struct Event
		{
			unsigned int			frame = 0;			//!< 0-based frame offset
			EventKind				kind = EventKind::KeyDown;
			KeyEventData::KeyCode	key = KeyEventData::KC_UNASSIGNED;
			float					x = 0.0f;			//!< pointer window px
			float					y = 0.0f;			//!< pointer window px
			PointerButton			button = PointerButton::Left;
			float					angle = 0.0f;		//!< tilt radians
		};

		//! a compiled step list: the events in frame order plus the number of
		//! frames the gesture occupies (frameSpan = last event frame + 1)
		struct Sequence
		{
			std::vector<Event>	events;
			unsigned int		frameSpan = 0;

			bool empty() const { return this->events.empty(); }
		};

		//! @brief compile a step list. Returns false and fills @p outError with
		//! ONE human-readable reason (naming the offending step by 1-based
		//! index) on a malformed verb, an unknown key name, a non-numeric
		//! coordinate, a zero/absurd frame count or a sequence past the
		//! MAX_STEPS / MAX_FRAMES bounds. An EMPTY list is an error too - a
		//! gesture that does nothing is a mistake, not a no-op.
		ORKIGE_ENGINE_DLL bool compile(StringVector const & steps,
			Sequence & outSequence, String & outError);

		//! @brief pure: the simulated tilt ANGLE a gravity direction implies -
		//! the inverse of InputManager::tiltVectorFromAngle, so
		//! `tilt vector 0 -1` is exactly "upright" and a vector step and an
		//! angle step drive the very same seam. A zero-length vector is 0.
		ORKIGE_ENGINE_DLL float tiltAngleFromVector(float x, float y);
	}
}

#endif //__InputInjection_h__30_7_2026__09_10_00__
