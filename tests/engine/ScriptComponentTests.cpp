/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	ScriptComponentTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless ScriptComponent unit tests: script resolution through the
	ScriptRuntime search root, the init/update/shutdown lifecycle,
	per-instance environment isolation (with deliberate sharing through
	the `shared` table), error containment (log once, disable, never
	crash), reloadScript and the scene serialization round-trip. The
	rendered end-to-end proof is the player_jumper_lua_selfcheck
	integration run (projects/jumper-lua).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/ScriptComponent.h>
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>

#include <filesystem>
#include <fstream>

namespace
{
	//! a throwaway project-like directory with a scripts/ subfolder
	struct TempScriptDir
	{
		std::filesystem::path root;
		explicit TempScriptDir(std::string const & name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempScriptDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		//! (over)write scripts/<name> with the given source
		std::string write(std::string const & name, std::string const & source)
		{
			const std::filesystem::path path = this->root / "scripts" / name;
			std::ofstream file(path);
			file << source;
			return path.string();
		}
	};
}

namespace
{
	//! read shared.<table>.<key> as double (-1 when missing)
	double sharedNumber(std::string const & table, std::string const & key)
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", table, key}, -1.0);
	}
	//! skip helper: the script-BEHAVIOR tests need a live backend; in
	//! ORKIGE_SCRIPTING=OFF builds they pass trivially (the disabled-error
	//! path has its own test below)
	bool scriptingAvailable()
	{
		if (Orkige::ScriptRuntime::available())
		{
			return true;
		}
		SUCCEED("scripting disabled - script behavior test skipped");
		return false;
	}
}

TEST_CASE("ScriptRuntime resolves script paths against the search root", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	TempScriptDir dir("orkige_script_resolve_test");
	const std::string absolute = dir.write("thing.lua", "-- empty\n");

	Orkige::ScriptRuntime & scripts = env.scriptRuntime;
	scripts.setScriptSearchRoot(dir.root.string());

	// project-relative resolves against the root
	CHECK(scripts.resolveScriptPath("scripts/thing.lua") ==
		(dir.root / "scripts/thing.lua").string());
	// absolute paths pass through
	CHECK(scripts.resolveScriptPath(absolute) == absolute);
	// missing files resolve to ""
	CHECK(scripts.resolveScriptPath("scripts/no_such.lua").empty());
	CHECK(scripts.resolveScriptPath("").empty());

	scripts.setScriptSearchRoot("");
}

TEST_CASE("ScriptComponent runs the init/update/shutdown lifecycle", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempScriptDir dir("orkige_script_lifecycle_test");
	dir.write("behavior.lua", R"lua(
		function init(self)
			shared.lifecycle = { inits = 1, updates = 0, shutdowns = 0,
				dt = -1 }
			shared.lifecycle.id = self.id
		end
		function update(self, dt)
			shared.lifecycle.updates = shared.lifecycle.updates + 1
			shared.lifecycle.dt = dt
		end
		function shutdown(self)
			shared.lifecycle.shutdowns = shared.lifecycle.shutdowns + 1
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> hero =
		env.gameObjectManager.createGameObject("Hero").lock();
	REQUIRE(hero);
	REQUIRE(hero->addComponent<Orkige::ScriptComponent>());
	Orkige::ScriptComponent* script =
		hero->getComponentPtr<Orkige::ScriptComponent>();
	script->setScriptFile("scripts/behavior.lua");

	// nothing runs before the first update (lazy load - the editor's edit
	// mode never updates, so scripts stay dormant there by construction)
	CHECK_FALSE(script->isScriptStarted());

	env.gameObjectManager.update(0.25f);
	CHECK(script->isScriptStarted());
	CHECK_FALSE(script->hasScriptError());
	CHECK(sharedNumber("lifecycle", "inits") == 1.0);
	CHECK(sharedNumber("lifecycle", "updates") == 1.0);
	CHECK(sharedNumber("lifecycle", "dt") == Catch::Approx(0.25));
	// self.id carries the owning GameObject's id
	CHECK(env.scriptRuntime.getString(
		{"shared", "lifecycle", "id"}, "") == "Hero");

	env.gameObjectManager.update(0.25f);
	env.gameObjectManager.update(0.25f);
	CHECK(sharedNumber("lifecycle", "updates") == 3.0);
	CHECK(sharedNumber("lifecycle", "inits") == 1.0);

	// a disabled script keeps its state but stops updating
	script->setScriptEnabled(false);
	env.gameObjectManager.update(0.25f);
	CHECK(sharedNumber("lifecycle", "updates") == 3.0);
	script->setScriptEnabled(true);

	// removing the component calls shutdown(self) exactly once
	REQUIRE(hero->removeComponent<Orkige::ScriptComponent>());
	CHECK(sharedNumber("lifecycle", "shutdowns") == 1.0);

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptComponent instances are isolated but can share deliberately", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempScriptDir dir("orkige_script_isolation_test");
	// `counter` is an environment global: per instance. `shared.iso` is the
	// documented cross-instance table.
	dir.write("counted.lua", R"lua(
		counter = 1	-- would be >1 if another instance's environment leaked in
		shared.iso = shared.iso or { total = 0 }
		function update(self, dt)
			counter = counter + 1
			shared.iso[self.id] = counter
			shared.iso.total = shared.iso.total + 1
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	// defensive reset: catch_discover_tests runs each case in its own
	// process, but a manual all-in-one run shares the script state
	env.scriptRuntime.runString("if shared then shared.iso = nil end");

	for (const char* id : { "IsoA", "IsoB" })
	{
		optr<Orkige::GameObject> gameObject =
			env.gameObjectManager.createGameObject(id).lock();
		REQUIRE(gameObject);
		REQUIRE(gameObject->addComponent<Orkige::ScriptComponent>());
		gameObject->getComponentPtr<Orkige::ScriptComponent>()
			->setScriptFile("scripts/counted.lua");
	}

	env.gameObjectManager.update(0.016f);
	// each instance counted ITS OWN environment global from 1 to 2...
	CHECK(sharedNumber("iso", "IsoA") == 2.0);
	CHECK(sharedNumber("iso", "IsoB") == 2.0);
	// ...while the shared table saw both instances
	CHECK(sharedNumber("iso", "total") == 2.0);

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("A script error disables the instance and never spams or crashes", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempScriptDir dir("orkige_script_error_test");
	dir.write("broken.lua", R"lua(
		shared.broken = { updates = 0 }
		function update(self, dt)
			shared.broken.updates = shared.broken.updates + 1
			error("boom")
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> gameObject =
		env.gameObjectManager.createGameObject("Broken").lock();
	REQUIRE(gameObject);
	REQUIRE(gameObject->addComponent<Orkige::ScriptComponent>());
	Orkige::ScriptComponent* script =
		gameObject->getComponentPtr<Orkige::ScriptComponent>();
	script->setScriptFile("scripts/broken.lua");

	env.gameObjectManager.update(0.016f);
	REQUIRE(script->hasScriptError());
	// the error carries the Lua file:line context and the message
	CHECK(script->getScriptError().find("boom") != Orkige::String::npos);
	CHECK(script->getScriptError().find("broken.lua") != Orkige::String::npos);

	// the instance disabled itself: no further update calls, no crash
	env.gameObjectManager.update(0.016f);
	env.gameObjectManager.update(0.016f);
	CHECK(sharedNumber("broken", "updates") == 1.0);

	SECTION("reloadScript clears the error and reloads from disk")
	{
		dir.write("broken.lua", R"lua(
			shared.broken = shared.broken or { updates = 0 }
			function update(self, dt)
				shared.broken.updates = shared.broken.updates + 100
			end
		)lua");
		script->reloadScript();
		CHECK_FALSE(script->hasScriptError());
		env.gameObjectManager.update(0.016f);
		CHECK_FALSE(script->hasScriptError());
		CHECK(sharedNumber("broken", "updates") == 101.0);
	}

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("A missing script file is an error, not a crash", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> gameObject =
		env.gameObjectManager.createGameObject("Ghost").lock();
	REQUIRE(gameObject);
	REQUIRE(gameObject->addComponent<Orkige::ScriptComponent>());
	Orkige::ScriptComponent* script =
		gameObject->getComponentPtr<Orkige::ScriptComponent>();
	script->setScriptFile("scripts/does_not_exist.lua");

	env.gameObjectManager.update(0.016f);
	REQUIRE(script->hasScriptError());
	if (Orkige::ScriptRuntime::available())
	{
		CHECK(script->getScriptError().find("not found") !=
			Orkige::String::npos);
	}
	else
	{
		// the honest ORKIGE_SCRIPTING=OFF error of the ScriptRuntime seam
		CHECK(script->getScriptError().find("scripting disabled") !=
			Orkige::String::npos);
	}

	env.gameObjectManager.clear();
}

TEST_CASE("ScriptComponent round-trips through the scene serializer", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	env.gameObjectManager.clear();
	const std::string scenePath = (std::filesystem::temp_directory_path() /
		"orkige_script_roundtrip.oscene").string();

	{
		optr<Orkige::GameObject> gameObject =
			env.gameObjectManager.createGameObject("Scripted").lock();
		REQUIRE(gameObject);
		REQUIRE(gameObject->addComponent<Orkige::ScriptComponent>());
		Orkige::ScriptComponent* script =
			gameObject->getComponentPtr<Orkige::ScriptComponent>();
		script->setScriptFile("scripts/player.lua");
		script->setScriptEnabled(false);
	}
	REQUIRE(Orkige::SceneSerializer::saveScene(scenePath,
		env.gameObjectManager));
	env.gameObjectManager.clear();

	REQUIRE(Orkige::SceneSerializer::loadScene(scenePath,
		env.gameObjectManager));
	optr<Orkige::GameObject> loaded =
		env.gameObjectManager.getGameObject("Scripted").lock();
	REQUIRE(loaded);
	REQUIRE(loaded->hasComponent<Orkige::ScriptComponent>());
	Orkige::ScriptComponent* script =
		loaded->getComponentPtr<Orkige::ScriptComponent>();
	CHECK(script->getScriptFile() == "scripts/player.lua");
	CHECK_FALSE(script->isScriptEnabled());
	CHECK_FALSE(script->hasScriptError());
	CHECK_FALSE(script->isScriptStarted());

	env.gameObjectManager.clear();
	std::error_code ignored;
	std::filesystem::remove(scenePath, ignored);
}
