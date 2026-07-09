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
#include <core_game/GameObjectComponent.h>
#include <core_game/SceneSerializer.h>
#include <core_base/PropertySchema.h>
#include <core_base/PropertyValue.h>

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

TEST_CASE("ScriptComponent::hotReload swaps compile-before-swap (WP #77)", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempScriptDir dir("orkige_script_hotreload_test");
	// variant A: publishes value = 1 and counts its ticks; leaves the shared
	// state as the engine-side value that must SURVIVE a reload
	dir.write("probe.lua", R"lua(
		shared.hr = shared.hr or {}
		function init(self)
			shared.hr.value = 1
			shared.hr.inits = (shared.hr.inits or 0) + 1
		end
		function update(self, dt)
			shared.hr.ticks = (shared.hr.ticks or 0) + 1
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	env.scriptRuntime.runString("if shared then shared.hr = nil end");

	optr<Orkige::GameObject> probe =
		env.gameObjectManager.createGameObject("Probe").lock();
	REQUIRE(probe);
	REQUIRE(probe->addComponent<Orkige::ScriptComponent>());
	Orkige::ScriptComponent* script =
		probe->getComponentPtr<Orkige::ScriptComponent>();
	script->setScriptFile("scripts/probe.lua");

	env.gameObjectManager.update(0.016f);
	REQUIRE(script->isScriptStarted());
	REQUIRE_FALSE(script->hasScriptError());
	CHECK(sharedNumber("hr", "value") == 1.0);
	CHECK(sharedNumber("hr", "inits") == 1.0);

	SECTION("a valid edit hot-swaps and keeps the shared/engine state")
	{
		dir.write("probe.lua", R"lua(
			shared.hr = shared.hr or {}
			function init(self)
				shared.hr.value = 2
				shared.hr.inits = (shared.hr.inits or 0) + 1
			end
			function update(self, dt)
				shared.hr.ticks = (shared.hr.ticks or 0) + 1
			end
		)lua");
		script->hotReload();
		CHECK_FALSE(script->hasReloadError());
		CHECK_FALSE(script->hasScriptError());
		// behavior changed (value 1 -> 2) and re-init ran (inits 1 -> 2)
		CHECK(sharedNumber("hr", "value") == 2.0);
		CHECK(sharedNumber("hr", "inits") == 2.0);
		// the new instance keeps updating
		const double ticksBefore = sharedNumber("hr", "ticks");
		env.gameObjectManager.update(0.016f);
		CHECK(sharedNumber("hr", "ticks") > ticksBefore);
	}

	SECTION("a broken edit is contained: the OLD instance keeps running")
	{
		// re-init to value 2 first so we can prove the broken reload does NOT
		// clobber it
		dir.write("probe.lua", R"lua(
			function init(self) shared.hr.value = 2 end
			function update(self, dt)
				shared.hr.ticks = (shared.hr.ticks or 0) + 1
			end
		)lua");
		script->hotReload();
		REQUIRE_FALSE(script->hasReloadError());
		REQUIRE(sharedNumber("hr", "value") == 2.0);

		// now a syntax error: compile-before-swap must keep the old instance
		dir.write("probe.lua", "this is not valid lua ((\n");
		const double ticksBefore = sharedNumber("hr", "ticks");
		script->hotReload();
		// the reload error is reported WITHOUT the fatal flag - object alive
		CHECK(script->hasReloadError());
		CHECK_FALSE(script->hasScriptError());
		CHECK(script->getLastReloadError().find("probe.lua") !=
			Orkige::String::npos);
		// the running (value = 2) instance was untouched and still ticks
		CHECK(sharedNumber("hr", "value") == 2.0);
		env.gameObjectManager.update(0.016f);
		CHECK(sharedNumber("hr", "ticks") > ticksBefore);

		// a subsequent GOOD edit heals the reload error
		dir.write("probe.lua", R"lua(
			function init(self) shared.hr.value = 3 end
		)lua");
		script->hotReload();
		CHECK_FALSE(script->hasReloadError());
		CHECK(script->getLastReloadError().empty());
		CHECK(sharedNumber("hr", "value") == 3.0);
	}

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("Two loadScriptInstance calls yield independent envs; shared survives teardown", "[script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempScriptDir dir("orkige_script_instance_indep_test");
	// each instance writes an env-local `mark` and bumps a shared counter -
	// the env-local must NOT leak between the two instances of the same file
	dir.write("indep.lua", R"lua(
		mark = (mark or 0) + 1	-- env-local: fresh per instance -> always 1
		shared.indep = shared.indep or { count = 0, lastMark = -1 }
		shared.indep.count = shared.indep.count + 1
		shared.indep.lastMark = mark
	)lua");
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;
	runtime.setScriptSearchRoot(dir.root.string());
	// loadScriptInstance is a pure factory (it does NOT create the `shared`
	// table the way ScriptComponent::ensureScriptApi does), so make sure the
	// documented cross-instance table exists before the scripts touch it
	runtime.ensureGlobalTable("shared");
	runtime.runString("if shared then shared.indep = nil end");

	Orkige::String error;
	optr<Orkige::ScriptInstance> a =
		runtime.loadScriptInstance("scripts/indep.lua", &error);
	REQUIRE(a);
	optr<Orkige::ScriptInstance> b =
		runtime.loadScriptInstance("scripts/indep.lua", &error);
	REQUIRE(b);
	// both saw a fresh env-local `mark` (== 1), never each other's
	CHECK(sharedNumber("indep", "lastMark") == 1.0);
	// the shared table accumulated BOTH loads
	CHECK(sharedNumber("indep", "count") == 2.0);

	// dropping the instances (their dtors GC the environments) must leave the
	// global `shared` table - and its contents - standing
	a.reset();
	b.reset();
	CHECK(sharedNumber("indep", "count") == 2.0);
	CHECK(runtime.hasGlobalTable("shared"));

	runtime.setScriptSearchRoot("");
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

TEST_CASE("ScriptComponent surfaces script exports through the union schema (P5b)",
	"[script][export]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	TempScriptDir dir("orkige_script_export_schema_test");
	dir.write("mover.lua", R"lua(
		properties = {
			moveSpeed = { type = "number", default = 4.5, min = 0, max = 20 },
			team      = { type = "string", default = "red" },
		}
		function update(self, dt) end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<GameObject> hero =
		env.gameObjectManager.createGameObject("Hero").lock();
	REQUIRE(hero);
	REQUIRE(hero->addComponent<ScriptComponent>());
	ScriptComponent* script = hero->getComponentPtr<ScriptComponent>();
	script->setScriptFile("scripts/mover.lua");

	const PropertySchema unionSchema = getComponentSchema(*script);
	// the STATIC half is always present (script/enabled + telemetry)
	CHECK(unionSchema.find("script") != nullptr);
	CHECK(unionSchema.find("enabled") != nullptr);

	if (!scriptingAvailable())
	{
		// OFF configuration: no dynamic exports, union == static, no crash
		CHECK(script->getInstancePropertySchema().empty());
		CHECK(unionSchema.find("moveSpeed") == nullptr);
		env.gameObjectManager.clear();
		env.scriptRuntime.setScriptSearchRoot("");
		return;
	}

	// the DYNAMIC exported properties appear in the union, indistinguishable
	// from static ones (same PropertyDesc/PropertyValue currency)
	PropertyDesc const * speed = unionSchema.find("moveSpeed");
	REQUIRE(speed != nullptr);
	CHECK(speed->kind == PropertyKind::Float);
	CHECK(speed->meta.hasRange);
	CHECK(speed->meta.maxValue == Catch::Approx(20.0f));
	// its value reads through the type-erased getter, at the declared default
	void const * instance = static_cast<void const *>(script);
	CHECK(speed->get(instance).asFloat() == Catch::Approx(4.5));
	PropertyDesc const * team = unionSchema.find("team");
	REQUIRE(team != nullptr);
	CHECK(team->kind == PropertyKind::String);
	CHECK(team->get(instance).asString() == "red");

	// a set through the reflected setter lands in the per-instance store
	speed->set(static_cast<void *>(script), PropertyValue::makeFloat(9.0));
	CHECK(script->getExportValue("moveSpeed").asFloat() == Catch::Approx(9.0));

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptComponent export values round-trip per-instance through the scene (P5b)",
	"[script][export]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempScriptDir dir("orkige_script_export_serialize_test");
	dir.write("mover.lua", R"lua(
		properties = {
			moveSpeed = { type = "number", default = 4.5 },
			team      = { type = "string", default = "red" },
		}
		function update(self, dt) end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	const std::string scenePath = (std::filesystem::temp_directory_path() /
		"orkige_script_export_roundtrip.oscene").string();

	// two instances of the SAME script with DIFFERENT export values - proves
	// the values are per-INSTANCE, not per-type
	{
		optr<GameObject> a = env.gameObjectManager.createGameObject("A").lock();
		optr<GameObject> b = env.gameObjectManager.createGameObject("B").lock();
		REQUIRE(a->addComponent<ScriptComponent>());
		REQUIRE(b->addComponent<ScriptComponent>());
		ScriptComponent* sa = a->getComponentPtr<ScriptComponent>();
		ScriptComponent* sb = b->getComponentPtr<ScriptComponent>();
		sa->setScriptFile("scripts/mover.lua");
		sb->setScriptFile("scripts/mover.lua");
		sa->setExportValue("moveSpeed", PropertyValue::makeFloat(11.0));
		sa->setExportValue("team", PropertyValue::makeString("blue"));
		// B keeps the default moveSpeed (4.5), overrides only team
		sb->setExportValue("team", PropertyValue::makeString("green"));
	}
	REQUIRE(SceneSerializer::saveScene(scenePath, env.gameObjectManager));
	env.gameObjectManager.clear();

	REQUIRE(SceneSerializer::loadScene(scenePath, env.gameObjectManager));
	ScriptComponent* la = env.gameObjectManager.getGameObject("A").lock()
		->getComponentPtr<ScriptComponent>();
	ScriptComponent* lb = env.gameObjectManager.getGameObject("B").lock()
		->getComponentPtr<ScriptComponent>();
	// the schema re-discovered on load (script path restored first), values
	// restored per-instance from the named records
	CHECK(la->getExportValue("moveSpeed").asFloat() == Catch::Approx(11.0));
	CHECK(la->getExportValue("team").asString() == "blue");
	CHECK(lb->getExportValue("moveSpeed").asFloat() == Catch::Approx(4.5));
	CHECK(lb->getExportValue("team").asString() == "green");

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
	std::error_code ignored;
	std::filesystem::remove(scenePath, ignored);
}

TEST_CASE("ScriptComponent export discovery reconciles BY NAME (P5b)",
	"[script][export]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempScriptDir dir("orkige_script_export_reconcile_test");
	// export set v1: { kept, dropped }
	dir.write("probe.lua", R"lua(
		properties = {
			kept    = { type = "number", default = 1.0 },
			dropped = { type = "number", default = 2.0 },
		}
		function update(self, dt) end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<GameObject> probe =
		env.gameObjectManager.createGameObject("Probe").lock();
	REQUIRE(probe->addComponent<ScriptComponent>());
	ScriptComponent* script = probe->getComponentPtr<ScriptComponent>();
	script->setScriptFile("scripts/probe.lua");

	// the designer sets a non-default value on the property that will SURVIVE
	script->setExportValue("kept", PropertyValue::makeFloat(42.0));
	CHECK(script->getExportValue("kept").asFloat() == Catch::Approx(42.0));

	// load once so hotReload has a running instance to swap
	env.gameObjectManager.update(0.016f);
	REQUIRE(script->isScriptStarted());

	// export set v2: { kept (survives), added (new) } - `dropped` removed
	dir.write("probe.lua", R"lua(
		properties = {
			kept  = { type = "number", default = 1.0 },
			added = { type = "number", default = 7.0 },
		}
		function update(self, dt) end
	)lua");
	script->hotReload();
	REQUIRE_FALSE(script->hasReloadError());

	const PropertySchema schema = script->getInstancePropertySchema();
	// kept: KEPT its designer-set value across the export-set change
	REQUIRE(schema.find("kept") != nullptr);
	CHECK(script->getExportValue("kept").asFloat() == Catch::Approx(42.0));
	// added: new export present at its declared default
	REQUIRE(schema.find("added") != nullptr);
	CHECK(script->getExportValue("added").asFloat() == Catch::Approx(7.0));
	// dropped: removed export is gone from the schema
	CHECK(schema.find("dropped") == nullptr);

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}
