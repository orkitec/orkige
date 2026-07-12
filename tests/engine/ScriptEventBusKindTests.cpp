/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	ScriptEventBusKindTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	THE multi-consumer proof at the unit level: TWO script component kinds on
	ONE GameObject both subscribe to the same event and both receive a single
	emit (the gap the single-consumer gui poll convention left). Plus the
	sandbox-scoping guarantees through the real ScriptComponent lifecycle -
	removing a component cancels its subscriptions, and a hot-reload re-runs
	init and re-subscribes WITHOUT duplicating. The rendered end-to-end proof
	is the player selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/ScriptComponent.h>
#include <engine_gocomponent/ScriptComponentRegistry.h>
#include <core_game/GameObject.h>
#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptEventBus.h>
#include <core_script/ScriptEventPayload.h>
#include <core_event/GlobalEventManager.h>

#include <filesystem>
#include <fstream>

using Orkige::optr;

namespace
{
	struct TempProjectDir
	{
		std::filesystem::path root;
		explicit TempProjectDir(std::string const & name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempProjectDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		void write(std::string const & relative, std::string const & source)
		{
			const std::filesystem::path path = this->root / "scripts" / relative;
			std::ofstream file(path);
			file << source;
		}
		std::string scriptsDir() const { return (this->root / "scripts").string(); }
	};

	bool scriptingAvailable()
	{
		if (Orkige::ScriptRuntime::available())
		{
			return true;
		}
		SUCCEED("scripting disabled - bus kind test skipped");
		return false;
	}
	double sharedNumber(std::string const & key)
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{ "shared", key }, -1.0);
	}
}

TEST_CASE("Two script kinds on one object both receive one bus emit",
	"[script][kind][events]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	if (!scriptingAvailable()) { return; }

	TempProjectDir dir("orkige_eventbus_kind_test");
	// two DIFFERENT kinds, each subscribing to the SAME event in init()
	dir.write("listenerA.component.lua", R"lua(
		function init(self)
			events.subscribe("boom", function(e)
				shared.hits = (shared.hits or 0) + 1
			end)
		end
	)lua");
	dir.write("listenerB.component.lua", R"lua(
		function init(self)
			events.subscribe("boom", function(e)
				shared.hits = (shared.hits or 0) + 1
			end)
		end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	ScriptEventBus::getSingleton().clear();
	env.scriptRuntime.runString(
		"if shared then for k in pairs(shared) do shared[k] = nil end end");

	optr<GameObject> hero = env.gameObjectManager.createGameObject("Hero").lock();
	REQUIRE(hero);
	REQUIRE(hero->addComponent(TypeInfo("listenerA")));
	REQUIRE(hero->addComponent(TypeInfo("listenerB")));

	// tick once: the lazy first load runs each kind's init(), which subscribes
	env.gameObjectManager.update(0.016f);
	REQUIRE(ScriptEventBus::getSingleton().subscriberCount("boom") == 2u);

	// ONE emit reaches BOTH kinds - the multi-consumer proof (a single-consumer
	// poll would have let only the first script see it)
	ScriptEventBus::getSingleton().emit("boom", ScriptEventPayload());
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("hits") == 2.0);

	SECTION("hot-reload re-subscribes without duplicating")
	{
		std::vector<ScriptComponent*> scripts = ScriptComponent::collectFrom(*hero);
		REQUIRE(scripts.size() == 2);
		// reload one kind: its OLD instance retires (its subscription cancels),
		// the fresh instance re-runs init and re-subscribes - net still 2
		scripts.front()->hotReload();
		CHECK(ScriptEventBus::getSingleton().subscriberCount("boom") == 2u);

		env.scriptRuntime.runString("shared.hits = 0");
		ScriptEventBus::getSingleton().emit("boom", ScriptEventPayload());
		GlobalEventManager::getSingleton().tick();
		CHECK(sharedNumber("hits") == 2.0);	// not 3 - no stale duplicate
	}

	SECTION("tearing the scene down cancels both subscriptions")
	{
		// scene teardown destroys the components, so each sandbox retires and
		// cancels its subscription. Drop the local strong ref too - a held
		// shared_ptr would otherwise keep the object (and its instances) alive.
		env.gameObjectManager.clear();
		hero.reset();
		CHECK(ScriptEventBus::getSingleton().subscriberCount("boom") == 0u);
	}

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
	registry.clear();
}
