/**************************************************************
	created:	2026/07/08 at 10:00
	filename: 	ScriptRuntimeTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Tests of the backend-neutral core_script/ScriptRuntime.h seam. This
	file compiles in EVERY scripting configuration: with a Lua backend the
	facade must run scripts, without one every operation must fail with
	the honest "scripting disabled" error - both branches are asserted
	here, so the OFF-configuration test run exercises the disabled path.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CoreTestEnvironment.h"

#include <core_script/ScriptRuntime.h>

#include <filesystem>
#include <fstream>

using Orkige::optr;
using Orkige::woptr;

namespace
{
	//! a throwaway directory with a scripts/ subfolder (mirror of the
	//! ScriptComponentTests helper)
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

TEST_CASE("ScriptRuntime::runString runs code or reports the disabled error", "[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();

	const Orkige::ScriptRuntime::Result result =
		env.scriptRuntime.runString("return 6 * 7, 'hello'");
	if (!Orkige::ScriptRuntime::available())
	{
		// the OFF configuration: honest error instead of silence
		CHECK_FALSE(result.success);
		CHECK(result.error.find("scripting disabled") != Orkige::String::npos);
		return;
	}
	REQUIRE(result.success);
	CHECK(result.error.empty());
	REQUIRE(result.returnValues.size() == 2);
	CHECK(result.returnValues[0] == "42");
	CHECK(result.returnValues[1] == "hello");

	// a script error comes back as Result::error, never as an exception
	const Orkige::ScriptRuntime::Result broken =
		env.scriptRuntime.runString("this is not a script(");
	CHECK_FALSE(broken.success);
	CHECK_FALSE(broken.error.empty());

	// globals set by a run are readable through the typed getters
	REQUIRE(env.scriptRuntime.runString(
		"seamtest = { number = 4.5, flag = true, word = 'orkige' }").success);
	CHECK(env.scriptRuntime.getNumber({"seamtest", "number"}, -1.0) ==
		Catch::Approx(4.5));
	CHECK(env.scriptRuntime.getBool({"seamtest", "flag"}, false));
	CHECK(env.scriptRuntime.getString({"seamtest", "word"}, "") == "orkige");
	// missing paths read as the fallback
	CHECK(env.scriptRuntime.getNumber({"seamtest", "nope"}, -1.0) == -1.0);
	CHECK(env.scriptRuntime.getNumber({"no", "such", "table"}, -2.0) == -2.0);
}

TEST_CASE("ScriptRuntime instances run the init/update/shutdown contract", "[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	TempScriptDir dir("orkige_scriptruntime_instance_test");
	// each instance runs in its own sandbox: bare assignments stay inside the
	// instance, so the observable state goes into a real global table the
	// test creates up front (the `shared` pattern of ScriptComponent)
	dir.write("instance.lua", R"lua(
		seam_instance.inits = 0
		seam_instance.updates = 0
		seam_instance.shutdowns = 0
		function init(self)
			seam_instance.inits = seam_instance.inits + 1
			seam_instance.owner = self.owner
		end
		function update(self, dt)
			seam_instance.updates = seam_instance.updates + 1
			seam_instance.dt = dt
		end
		function shutdown(self)
			seam_instance.shutdowns = seam_instance.shutdowns + 1
		end
	)lua");
	env.scriptRuntime.ensureGlobalTable("seam_instance");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());

	Orkige::String error;
	optr<Orkige::ScriptInstance> instance =
		env.scriptRuntime.loadScriptInstance("scripts/instance.lua", &error);
	if (!Orkige::ScriptRuntime::available())
	{
		// the OFF configuration refuses the load with the honest error
		CHECK(instance == nullptr);
		CHECK(error.find("scripting disabled") != Orkige::String::npos);
		env.scriptRuntime.setScriptSearchRoot("");
		return;
	}
	REQUIRE(instance);
	instance->setSelfValue("owner", Orkige::String("SeamTest"));
	REQUIRE(instance->callInit(&error));
	REQUIRE(instance->callUpdate(0.125f, &error));
	REQUIRE(instance->callUpdate(0.125f, &error));
	REQUIRE(instance->callShutdown(&error));
	CHECK(env.scriptRuntime.getNumber({"seam_instance", "inits"}, -1.0) == 1.0);
	CHECK(env.scriptRuntime.getNumber({"seam_instance", "updates"}, -1.0) == 2.0);
	CHECK(env.scriptRuntime.getNumber({"seam_instance", "dt"}, -1.0) ==
		Catch::Approx(0.125));
	CHECK(env.scriptRuntime.getNumber({"seam_instance", "shutdowns"}, -1.0) == 1.0);
	CHECK(env.scriptRuntime.getString({"seam_instance", "owner"}, "") ==
		"SeamTest");

	// a missing file is an error string, not a crash
	optr<Orkige::ScriptInstance> missing =
		env.scriptRuntime.loadScriptInstance("scripts/no_such.lua", &error);
	CHECK(missing == nullptr);
	CHECK(error.find("not found") != Orkige::String::npos);

	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptRuntime global tables and function registration", "[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();

	if (!Orkige::ScriptRuntime::available())
	{
		// without a backend the registration API must be a safe no-op
		CHECK_FALSE(env.scriptRuntime.hasGlobalTable("seam_reg"));
		env.scriptRuntime.ensureGlobalTable("seam_reg");
		CHECK_FALSE(env.scriptRuntime.hasGlobalTable("seam_reg"));
		env.scriptRuntime.registerFunction("seam_reg", "double",
			[](double value) { return value * 2.0; });
		SUCCEED("scripting disabled - registration is inert as designed");
		return;
	}

	env.scriptRuntime.ensureGlobalTable("seam_shared_probe");
	CHECK(env.scriptRuntime.hasGlobalTable("seam_shared_probe"));

	// a registered C++ function is callable from script (the table is
	// created on demand)
	env.scriptRuntime.registerFunction("seam_reg", "double",
		[](double value) { return value * 2.0; });
	CHECK(env.scriptRuntime.hasGlobalTable("seam_reg"));
	const Orkige::ScriptRuntime::Result result =
		env.scriptRuntime.runString("return seam_reg.double(21)");
	REQUIRE(result.success);
	REQUIRE(result.returnValues.size() == 1);
	CHECK(result.returnValues[0] == "42.0");
}

TEST_CASE("ScriptRuntime sandbox denies unsafe globals", "[script][security]")
{
	// THREAT MODEL: a scene/script file is CONTENT (an agent or an untrusted
	// source may author it). Loading it must not grant file, process or
	// arbitrary-code-loading access. Every game- AND editor-script sandbox
	// shares the one hardened Lua state through loadScriptInstance, so this
	// GAME-sandbox proof also covers the editor sandbox's global surface (the
	// editor path is exercised end to end by the editor_scripts selfcheck).
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	TempScriptDir dir("orkige_scriptruntime_security_test");
	// the top-level chunk asserts every denied global is nil and every
	// permitted one is present; an assert failure makes the load INVALID, so a
	// clean load is the whole proof
	dir.write("denied.lua", R"lua(
		assert(io == nil, "io reachable")
		assert(require == nil, "require reachable")
		assert(package == nil, "package reachable")
		assert(load == nil, "load reachable")
		assert(loadstring == nil, "loadstring reachable")
		assert(loadfile == nil, "loadfile reachable")
		assert(dofile == nil, "dofile reachable")
		assert(debug == nil, "debug reachable")
		-- collectgarbage is PERMITTED: a GC control, no file/process/code
		-- capability (a game or the weak-handle tests legitimately drive it)
		assert(type(collectgarbage) == "function", "collectgarbage denied")
		-- os is a pruned read-only table: the dangerous members are gone
		assert(type(os) == "table", "os subset missing")
		assert(os.execute == nil, "os.execute reachable")
		assert(os.remove == nil, "os.remove reachable")
		assert(os.rename == nil, "os.rename reachable")
		assert(os.getenv == nil, "os.getenv reachable")
		assert(os.exit == nil, "os.exit reachable")
		assert(os.time and os.clock and os.date, "safe os subset missing")
		-- the permitted computation stdlib still works
		assert(math.floor(1.9) == 1, "math missing")
		assert(string.upper("ab") == "AB", "string missing")
		assert(table.concat({"a", "b"}) == "ab", "table missing")
		assert(type(pcall) == "function", "pcall missing")
		assert(type(setmetatable) == "function", "setmetatable missing")
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());

	Orkige::String error;
	optr<Orkige::ScriptInstance> instance =
		env.scriptRuntime.loadScriptInstance("scripts/denied.lua", &error);
	if (!Orkige::ScriptRuntime::available())
	{
		// OFF configuration: the load is refused with the honest error, so the
		// sandbox denies everything by construction (nothing runs at all)
		CHECK(instance == nullptr);
		CHECK(error.find("scripting disabled") != Orkige::String::npos);
		env.scriptRuntime.setScriptSearchRoot("");
		return;
	}
	INFO("sandbox assertion failure: " << error);
	REQUIRE(instance);	// every denial + permission assertion held

	// the shared global state is hardened too: a direct call to a denied
	// global from the console path (global env) errors instead of loading code
	const Orkige::ScriptRuntime::Result loadCall =
		env.scriptRuntime.runString("return load('return 1')");
	CHECK_FALSE(loadCall.success);
	const Orkige::ScriptRuntime::Result requireCall =
		env.scriptRuntime.runString("return require('os')");
	CHECK_FALSE(requireCall.success);

	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptRuntime::readExportedProperties parses the exports table (P5b)",
	"[script][export]")
{
	using namespace Orkige;
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	TempScriptDir dir("orkige_scriptruntime_export_test");
	dir.write("mover.lua", R"lua(
		properties = {
			moveSpeed = { type = "number", default = 4.5, min = 0, max = 20 },
			canDouble = { type = "bool",   default = true },
			team      = { type = "string", default = "red" },
			tint      = { type = "color",  default = {0.25, 0.5, 0.75, 1.0} },
			muzzle    = { type = "vec3",   default = {0, 0, 1} },
			icon      = { type = "asset",  kind = "texture" },
		}
		function update(self, dt) end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());

	std::vector<ScriptExportProperty> exports =
		env.scriptRuntime.readExportedProperties("scripts/mover.lua");

	if (!ScriptRuntime::available())
	{
		// OFF configuration: scripts have no exports (honest, not a crash)
		CHECK(exports.empty());
		env.scriptRuntime.setScriptSearchRoot("");
		return;
	}

	// six exports, DETERMINISTICALLY ordered by name (Lua tables are unordered)
	REQUIRE(exports.size() == 6);
	CHECK(exports[0].name == "canDouble");
	CHECK(exports[1].name == "icon");
	CHECK(exports[2].name == "moveSpeed");
	CHECK(exports[3].name == "muzzle");
	CHECK(exports[4].name == "team");
	CHECK(exports[5].name == "tint");

	// a name index so the per-kind assertions read cleanly
	auto byName = [&exports](std::string const & name) -> ScriptExportProperty const *
	{
		for (ScriptExportProperty const & e : exports)
		{
			if (e.name == name) { return &e; }
		}
		return nullptr;
	};

	ScriptExportProperty const * speed = byName("moveSpeed");
	REQUIRE(speed);
	CHECK(speed->kind == PropertyKind::Float);
	CHECK(speed->defaultValue.asFloat() == Catch::Approx(4.5));
	CHECK(speed->hasRange);
	CHECK(speed->minValue == Catch::Approx(0.0f));
	CHECK(speed->maxValue == Catch::Approx(20.0f));

	ScriptExportProperty const * canDouble = byName("canDouble");
	REQUIRE(canDouble);
	CHECK(canDouble->kind == PropertyKind::Bool);
	CHECK(canDouble->defaultValue.asBool());

	ScriptExportProperty const * team = byName("team");
	REQUIRE(team);
	CHECK(team->kind == PropertyKind::String);
	CHECK(team->defaultValue.asString() == "red");

	ScriptExportProperty const * tint = byName("tint");
	REQUIRE(tint);
	CHECK(tint->kind == PropertyKind::Color);
	CHECK(tint->defaultValue.asColor().b == Catch::Approx(0.75f));
	CHECK(tint->defaultValue.asColor().a == Catch::Approx(1.0f));

	ScriptExportProperty const * muzzle = byName("muzzle");
	REQUIRE(muzzle);
	CHECK(muzzle->kind == PropertyKind::Vec3);
	CHECK(muzzle->defaultValue.asVec3().z == Catch::Approx(1.0f));

	ScriptExportProperty const * icon = byName("icon");
	REQUIRE(icon);
	CHECK(icon->kind == PropertyKind::AssetRef);
	CHECK(icon->referenceHint == "texture");

	// a script with NO `properties` table yields no exports (not an error)
	dir.write("plain.lua", "function update(self, dt) end\n");
	CHECK(env.scriptRuntime.readExportedProperties("scripts/plain.lua").empty());
	// a missing file: empty, honest
	CHECK(env.scriptRuntime.readExportedProperties("scripts/nope.lua").empty());

	env.scriptRuntime.setScriptSearchRoot("");
}
