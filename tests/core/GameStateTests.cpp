/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	GameStateTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless game-state unit tests: the state string round-trips through
	get()/set(), and every set() emits a game.stateChanged event carrying the
	previous and new values on the ONE engine event bus (captured through the
	bus's trace recorder). The rendered proof (Lua game.setState + an events
	subscriber receiving the change) is the player_gameplay_selfcheck run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_game/GameState.h>
#include <core_script/ScriptEventBus.h>

#include <string>

namespace
{
	//! find the first captured event of the given name (nullptr when absent)
	Orkige::ScriptEventBus::FrameEvent const * findEvent(
		std::vector<Orkige::ScriptEventBus::FrameEvent> const & events,
		char const * name)
	{
		for(Orkige::ScriptEventBus::FrameEvent const & event : events)
		{
			if(event.name == name)
			{
				return &event;
			}
		}
		return nullptr;
	}
	//! read a flattened scalar field of a captured event ("" when absent)
	Orkige::String fieldValue(
		Orkige::ScriptEventBus::FrameEvent const & event, char const * key)
	{
		for(std::pair<Orkige::String, Orkige::String> const & field : event.fields)
		{
			if(field.first == key)
			{
				return field.second;
			}
		}
		return "";
	}
}

TEST_CASE("GameState round-trips the state string", "[unit][gamestate]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::GameState state;

	REQUIRE(state.get() == "");	// starts unset
	state.set("menu");
	REQUIRE(state.get() == "menu");
	state.set("playing");
	REQUIRE(state.get() == "playing");
}

TEST_CASE("GameState.set emits game.stateChanged with old + new",
	"[unit][gamestate]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::GameState state;

	Orkige::ScriptEventBus & bus = Orkige::ScriptEventBus::getSingleton();
	bus.setTraceCapture(true);
	bus.takeFrameEvents();	// drain anything left by earlier tests

	state.set("playing");	// from "" -> "playing"
	state.set("gameover");	// from "playing" -> "gameover"

	std::vector<Orkige::ScriptEventBus::FrameEvent> events = bus.takeFrameEvents();
	bus.setTraceCapture(false);

	// two changes -> two game.stateChanged events, in order
	int changeCount = 0;
	for(Orkige::ScriptEventBus::FrameEvent const & event : events)
	{
		if(event.name == "game.stateChanged")
		{
			++changeCount;
		}
	}
	REQUIRE(changeCount == 2);

	Orkige::ScriptEventBus::FrameEvent const * first =
		findEvent(events, "game.stateChanged");
	REQUIRE(first != nullptr);
	// the FIRST change goes from the empty start state to "playing"
	REQUIRE(fieldValue(*first, "old") == "");
	REQUIRE(fieldValue(*first, "new") == "playing");
}
