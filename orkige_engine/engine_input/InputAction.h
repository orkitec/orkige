/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	InputAction.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __InputAction_h__9_7_2026__10_00_00__
#define __InputAction_h__9_7_2026__10_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_input/KeyEventData.h"

#include <vector>

namespace Orkige
{
	//! @brief the KIND of an action - what query is meant to answer it.
	//! @remarks Kinds are advisory: down/pressed/released and value/value2 all
	//! work on every action (down derives from a magnitude threshold, value2
	//! packs the two components), but the kind documents a game author's intent
	//! and drives what the editor UI (future) offers.
	enum class InputActionKind
	{
		Digital,	//!< a button - answered by down()/pressed()/released()
		Analog1D,	//!< a single axis - answered by value()
		Analog2D,	//!< a pair of axes - answered by value2()
	};

	//! @brief one input source feeding an action.
	//! @remarks A binding contributes a signed value to ONE component
	//! (0 = x, 1 = y) of its action; several bindings on the same component
	//! combine by MAX MAGNITUDE (steer = tilt OR arrows, whichever pushes
	//! harder). The three source shapes cover every real consumer found in the
	//! reference games (see the jumper/roller default set):
	//!   Key      any of \c keys held -> +1 on the component (a digital button)
	//!   KeyAxis  the promoted jumper axis() helper: any negativeKeys held
	//!            -> -1, any positiveKeys held -> +1 (both -> 0)
	//!   TiltAxis reads component \c tiltComponent of InputManager::getTilt()
	//!            (0 = x, 1 = y). Tilt is (0,-1,0) at rest, so this reads the
	//!            COMPONENT - 0 at rest, not the vector's -1 y.
	struct InputActionBinding
	{
		enum Type
		{
			Key,		//!< keys[] -> digital +1
			KeyAxis,	//!< negativeKeys[]/positiveKeys[] -> -1/0/+1
			TiltAxis,	//!< tilt component -> [-1..1]
		};
		Type type = Key;
		std::vector<KeyEventData::KeyCode> keys;			//!< Key
		std::vector<KeyEventData::KeyCode> negativeKeys;	//!< KeyAxis
		std::vector<KeyEventData::KeyCode> positiveKeys;	//!< KeyAxis
		int tiltComponent = 0;		//!< TiltAxis: 0 = getTilt().x, 1 = getTilt().y
		int outputComponent = 0;	//!< which action-value component (0 = x, 1 = y)
	};

	//! @brief a named action: a kind, its bindings and the once-per-frame
	//! edge snapshot. The snapshot fields are written ONLY by
	//! InputActionMap::update() and read back by every query, so two queries in
	//! the same frame always agree and pressed/released stay true for exactly
	//! one frame.
	struct InputAction
	{
		String name;
		InputActionKind kind = InputActionKind::Digital;
		std::vector<InputActionBinding> bindings;
		//--- per-frame snapshot (do not write outside update()) ---
		float value[2] = { 0.0f, 0.0f };	//!< combined per-component value
		bool down = false;					//!< held this frame
		bool pressed = false;				//!< down && !down-last-frame
		bool released = false;				//!< !down && down-last-frame
	};
	//---------------------------------------------------------
}

#endif //__InputAction_h__9_7_2026__10_00_00__
