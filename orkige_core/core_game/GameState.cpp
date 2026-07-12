/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	GameState.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/GameState.h"
#include "core_script/ScriptEventBus.h"
#include "core_script/ScriptEventPayload.h"

namespace Orkige
{
	IMPL_OSINGLETON(GameState)
	//---------------------------------------------------------
	GameState::GameState()
	{
	}
	//---------------------------------------------------------
	GameState::~GameState()
	{
	}
	//---------------------------------------------------------
	void GameState::set(String const & name)
	{
		const String previous = this->mState;
		this->mState = name;
		// announce the change on the SAME bus scripts already use: a plain
		// C++ producer builds the bounded payload directly (no scripting
		// backend touched), so the event reaches Lua subscribers AND any C++
		// listener bound to "game.stateChanged". The bus is process-wide and
		// always present, so this works even in ORKIGE_SCRIPTING=OFF (there are
		// simply no Lua subscribers then).
		ScriptEventPayload payload;
		payload.setString("old", previous);
		payload.setString("new", name);
		ScriptEventBus::getSingleton().emit("game.stateChanged", payload);
	}
}
