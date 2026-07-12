/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	GameState.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __GameState_h__12_7_2026__16_00_00__
#define __GameState_h__12_7_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

namespace Orkige
{
	//! @brief a THIN game-state convenience: a single named string the game
	//! keeps ("menu" / "playing" / "gameover" / ...) whose every change fires a
	//! `game.stateChanged` event ({ old, new }) on the ONE engine event bus
	//! (core_script/ScriptEventBus). Deliberately NOT a state machine - no
	//! transitions table, no enter/exit hooks, no allowed-edge validation; it
	//! composes with the events system scripts already have (subscribe to
	//! `game.stateChanged` to react). The heritage GameStateManager stays gone.
	//! @remarks Held here (like TimeControl) rather than on a subsystem because
	//! it is a cross-cutting game-wide knob. ONLY a runtime that creates one has
	//! it: the player does (tools/player/main.cpp), the editor never does, so the
	//! Lua `game` table is an honest no-op in edit mode (a `set` there stores
	//! nothing and emits nothing). Never persisted/serialized - a fresh
	//! scene/level starts at the empty state until the game sets one.
	class ORKIGE_CORE_DLL GameState : public Singleton<GameState>
	{
		DECL_OSINGLETON(GameState)
		//--- Variables ---------------------------------------
	private:
		String	mState;		//!< the current state name ("" = unset)
		//--- Methods -----------------------------------------
	public:
		//! constructor
		GameState();
		//! destructor
		virtual ~GameState();

		//! @brief the current state name ("" when never set)
		String const & get() const { return this->mState; }
		//! @brief set the state name and emit `game.stateChanged` carrying the
		//! previous value (`old`) and the new one (`new`) through the event bus.
		//! The event fires on every call (even when the value is unchanged), so a
		//! subscriber sees exactly one signal per set.
		void set(String const & name);
	};
}

#endif //__GameState_h__12_7_2026__16_00_00__
