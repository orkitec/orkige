/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	CVarProtocolBehaviourTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The cvar loop end to end, headless: the editor sends MSG_SET_CVAR over the
	real debug link, the PlayerDebugLink drives CVarManager::setString and the
	cvar's onChange fires and mutates an observable (standing in for
	ball.lua's physics:setGravity - the roller_gravity live path). Proves the
	MSG_SET_CVAR -> setString -> onChange -> behaviour chain the roller relies
	on, deterministically (loopback socket, no window/GPU), so it runs in the
	unit preset of BOTH render flavors.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_runtime/PlayerRuntime.h>
#include <core_debug/CVarManager.h>
#include <core_debugnet/DebugClient.h>
#include <core_debugnet/DebugProtocol.h>
#include <core_game/GameObjectManager.h>

#include <chrono>
#include <thread>

using Orkige::CVar;
using Orkige::CVarManager;
using Orkige::CVarType;
using Orkige::DebugClient;
using Orkige::DebugMessage;
using Orkige::PlayerDebugLink;
namespace Protocol = Orkige::DebugProtocol;

namespace
{
	//! pump the player link and the editor client until predicate() or the
	//! (generous) deadline; single-threaded and deterministic like the
	//! DebugProtocolTests helper
	template <typename Predicate>
	bool pumpUntil(PlayerDebugLink & link, DebugClient & client,
		Orkige::GameObjectManager & gameObjectManager, Predicate predicate,
		int timeoutMilliseconds = 5000)
	{
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMilliseconds);
		for (;;)
		{
			link.update(gameObjectManager, "test.oscene");
			client.update();
			if (predicate())
			{
				return true;
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

TEST_CASE("MSG_SET_CVAR fires onChange live over the debug link",
	"[cvar][debugnet]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();

	// the cvar's onChange stands in for ball.lua's physics:setGravity - the
	// live re-apply seam a set_cvar must reach
	float appliedGravity = 0.0f;
	int changes = 0;
	CVarManager & cvars = CVarManager::getSingleton();
	cvars.registerCVar("test_link_gravity", CVarType::Float, "18",
		Orkige::CVAR_NONE, "roller gravity stand-in",
		[&](CVar const & cvar)
		{
			appliedGravity = cvar.asFloat();
			++changes;
		});
	// the registry singleton persists across Catch2 SECTIONs, so force a known
	// baseline (and reset the counters) at every section entry - this also
	// proves onChange fires the live re-apply on a plain set
	changes = 0;
	appliedGravity = 0.0f;
	REQUIRE(cvars.setString("test_link_gravity", "18"));
	REQUIRE(appliedGravity == 18.0f);	// onChange fired the baseline apply
	REQUIRE(changes == 1);

	PlayerDebugLink link;
	REQUIRE(link.start(0));				// ephemeral port
	DebugClient client;
	REQUIRE(client.connect("127.0.0.1", link.getPort()));
	REQUIRE(pumpUntil(link, client, env.gameObjectManager,
		[&] { return client.isConnected(); }));

	SECTION("a valid set changes the value and fires onChange")
	{
		DebugMessage message(Protocol::MSG_SET_CVAR);
		message.set(Protocol::FIELD_CVAR_NAME, "test_link_gravity");
		message.set(Protocol::FIELD_VALUE, "42");
		REQUIRE(client.send(message));

		REQUIRE(pumpUntil(link, client, env.gameObjectManager,
			[&] { return changes >= 2; }));
		CHECK(appliedGravity == 42.0f);
		CHECK(cvars.getFloat("test_link_gravity") == 42.0f);
	}

	SECTION("a bad value is rejected with an error and no live change")
	{
		DebugMessage message(Protocol::MSG_SET_CVAR);
		message.set(Protocol::FIELD_CVAR_NAME, "test_link_gravity");
		message.set(Protocol::FIELD_VALUE, "not-a-number");
		REQUIRE(client.send(message));

		DebugMessage error;
		REQUIRE(pumpUntil(link, client, env.gameObjectManager, [&]
		{
			DebugMessage incoming;
			while (client.receive(incoming))
			{
				if (incoming.type == Protocol::MSG_ERROR)
				{
					error = incoming;
					return true;
				}
			}
			return false;
		}));
		CHECK(error.get(Protocol::FIELD_MESSAGE).find("set_cvar") !=
			Orkige::String::npos);
		// the value stayed at the last accepted default - onChange did NOT fire
		CHECK(changes == 1);
		CHECK(appliedGravity == 18.0f);
	}

	SECTION("an unknown cvar answers with an error, never crashes")
	{
		DebugMessage message(Protocol::MSG_SET_CVAR);
		message.set(Protocol::FIELD_CVAR_NAME, "no_such_cvar");
		message.set(Protocol::FIELD_VALUE, "1");
		REQUIRE(client.send(message));

		bool gotError = false;
		REQUIRE(pumpUntil(link, client, env.gameObjectManager, [&]
		{
			DebugMessage incoming;
			while (client.receive(incoming))
			{
				if (incoming.type == Protocol::MSG_ERROR &&
					incoming.get(Protocol::FIELD_MESSAGE).find("unknown") !=
						Orkige::String::npos)
				{
					gotError = true;
					return true;
				}
			}
			return false;
		}));
		CHECK(gotError);
	}

	link.shutdown();
	client.disconnect();
}
