/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	CVarTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Unit coverage for the console-variable registry (core_debug/CVarManager)
	and the scripting-free command grammar (core_debug/CVarCommand). Both are
	pure core - these run UNCHANGED in the ORKIGE_SCRIPTING=OFF core too (no
	renderer, no scripting, no CoreTestEnvironment boot needed: the manager is
	an IMPL_OSINGLETON_GETCREATE singleton, auto-created on first touch). Each
	case uses uniquely-named cvars because the singleton persists across cases.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_debug/CVarManager.h>
#include <core_debug/CVarCommand.h>

using Orkige::CVar;
using Orkige::CVarManager;
using Orkige::CVarType;
using Orkige::String;
namespace CVarCommand = Orkige::CVarCommand;

TEST_CASE("CVarManager registers and reads typed cvars", "[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();

	manager.registerCVar("t_reg.int", CVarType::Int, "60");
	manager.registerCVar("t_reg.float", CVarType::Float, "1.5");
	manager.registerCVar("t_reg.bool", CVarType::Bool, "true");
	manager.registerCVar("t_reg.str", CVarType::String, "hello world");

	CHECK(manager.exists("t_reg.int"));
	CHECK_FALSE(manager.exists("t_reg.nope"));

	CHECK(manager.getInt("t_reg.int") == 60);
	CHECK(manager.getFloat("t_reg.float") == 1.5f);
	CHECK(manager.getBool("t_reg.bool") == true);
	CHECK(manager.getString("t_reg.str") == "hello world");

	// unknown-name fallbacks
	CHECK(manager.getInt("t_reg.nope", 7) == 7);
	CHECK(manager.getBool("t_reg.nope", true) == true);
}

TEST_CASE("CVarManager setString coerces and rejects per type", "[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();
	manager.registerCVar("t_set.int", CVarType::Int, "0");
	manager.registerCVar("t_set.float", CVarType::Float, "0");
	manager.registerCVar("t_set.bool", CVarType::Bool, "0");

	String error;
	// good values
	CHECK(manager.setString("t_set.int", "42", &error));
	CHECK(manager.getInt("t_set.int") == 42);
	CHECK(manager.setString("t_set.float", "3.25", &error));
	CHECK(manager.getFloat("t_set.float") == 3.25f);
	// bool accepts several spellings, canonicalises to 1/0
	CHECK(manager.setString("t_set.bool", "on", &error));
	CHECK(manager.getBool("t_set.bool") == true);
	CHECK(manager.getString("t_set.bool") == "1");
	CHECK(manager.setString("t_set.bool", "false", &error));
	CHECK(manager.getBool("t_set.bool") == false);

	// bad values are rejected WITH an error and leave the value untouched
	error.clear();
	CHECK_FALSE(manager.setString("t_set.int", "notanumber", &error));
	CHECK_FALSE(error.empty());
	CHECK(manager.getInt("t_set.int") == 42); // unchanged

	CHECK_FALSE(manager.setString("t_set.float", "1.2.3", &error));
	CHECK(manager.getFloat("t_set.float") == 3.25f);

	CHECK_FALSE(manager.setString("t_set.bool", "maybe", &error));
	CHECK(manager.getBool("t_set.bool") == false);

	// unknown name is an honest error, never a crash
	error.clear();
	CHECK_FALSE(manager.setString("t_set.ghost", "1", &error));
	CHECK(error.find("unknown") != String::npos);
}

TEST_CASE("CVarManager fires onChange on accepted changes only", "[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();
	int changes = 0;
	float lastSeen = 0.0f;
	manager.registerCVar("t_cb.g", CVarType::Float, "18",
		Orkige::CVAR_NONE, "gravity",
		[&](CVar const & cvar) { ++changes; lastSeen = cvar.asFloat(); });

	// onChange fires once for the initial default apply at registration
	CHECK(changes == 1);
	CHECK(lastSeen == 18.0f);

	REQUIRE(manager.setString("t_cb.g", "30"));
	CHECK(changes == 2);
	CHECK(lastSeen == 30.0f);

	// a REJECTED set must NOT fire onChange
	CHECK_FALSE(manager.setString("t_cb.g", "junk"));
	CHECK(changes == 2);

	// reset restores the default and fires onChange
	REQUIRE(manager.reset("t_cb.g"));
	CHECK(changes == 3);
	CHECK(lastSeen == 18.0f);
}

TEST_CASE("CVarManager honours READONLY and records flags", "[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();
	manager.registerCVar("t_flag.ro", CVarType::Int, "5", Orkige::CVAR_READONLY);
	manager.registerCVar("t_flag.cheat", CVarType::Int, "0",
		Orkige::CVAR_CHEAT | Orkige::CVAR_PERSIST);

	String error;
	CHECK_FALSE(manager.setString("t_flag.ro", "9", &error));
	CHECK(error.find("read-only") != String::npos);
	CHECK(manager.getInt("t_flag.ro") == 5);
	CHECK_FALSE(manager.reset("t_flag.ro"));

	CVar const * cheat = manager.find("t_flag.cheat");
	REQUIRE(cheat != nullptr);
	CHECK(cheat->hasFlag(Orkige::CVAR_CHEAT));
	CHECK(cheat->hasFlag(Orkige::CVAR_PERSIST));
	CHECK_FALSE(cheat->hasFlag(Orkige::CVAR_READONLY));
}

TEST_CASE("CVarManager findByPrefix lists sorted matches", "[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();
	manager.registerCVar("t_find.zulu", CVarType::Int, "1");
	manager.registerCVar("t_find.alpha", CVarType::Int, "1");
	manager.registerCVar("t_find.bravo", CVarType::Int, "1");

	Orkige::StringVector matches = manager.findByPrefix("t_find.");
	REQUIRE(matches.size() >= 3);
	// the map is sorted, so alpha < bravo < zulu among our keys
	auto indexOf = [&](String const & name) -> std::ptrdiff_t
	{
		for (std::size_t i = 0; i < matches.size(); ++i)
		{
			if (matches[i] == name) { return static_cast<std::ptrdiff_t>(i); }
		}
		return -1;
	};
	CHECK(indexOf("t_find.alpha") < indexOf("t_find.bravo"));
	CHECK(indexOf("t_find.bravo") < indexOf("t_find.zulu"));
	CHECK(manager.findByPrefix("t_find.no_such_prefix").empty());
}

TEST_CASE("CVarManager persistence: manifest apply + PERSIST filter",
	"[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();
	manager.registerCVar("t_persist.keep", CVarType::Float, "18",
		Orkige::CVAR_PERSIST);
	manager.registerCVar("t_persist.transient", CVarType::Float, "1");

	// applySettings applies "cvar."-prefixed keys through setString
	std::map<String, String> settings;
	settings["cvar.t_persist.keep"] = "40";
	settings["cvar.t_persist.transient"] = "9";
	settings["unrelated.key"] = "x";
	manager.applySettings(settings);
	CHECK(manager.getFloat("t_persist.keep") == 40.0f);
	CHECK(manager.getFloat("t_persist.transient") == 9.0f);

	// only PERSIST cvars whose value differs from the default write back
	std::map<String, String> out;
	manager.collectPersisted(out);
	CHECK(out.count("cvar.t_persist.keep") == 1);
	CHECK(out["cvar.t_persist.keep"] == "40");
	CHECK(out.count("cvar.t_persist.transient") == 0); // not PERSIST

	// back at the default -> the persisted key is dropped from the map
	REQUIRE(manager.reset("t_persist.keep"));
	out["cvar.t_persist.keep"] = "stale"; // pretend it was in the manifest
	manager.collectPersisted(out);
	CHECK(out.count("cvar.t_persist.keep") == 0);
}

TEST_CASE("CVarManager applies a manifest override registered LATER",
	"[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();
	// the order-independence path: the manifest names a cvar the owning system
	// has not registered yet - the override is held and applied on registration
	std::map<String, String> settings;
	settings["cvar.t_late.g"] = "55";
	manager.applySettings(settings);
	CHECK_FALSE(manager.exists("t_late.g"));

	manager.registerCVar("t_late.g", CVarType::Float, "18");
	CHECK(manager.getFloat("t_late.g") == 55.0f); // override won over the default
}

TEST_CASE("CVarCommand grammar drives the registry", "[cvar][unit]")
{
	CVarManager & manager = CVarManager::getSingleton();
	manager.registerCVar("t_cmd.speed", CVarType::Float, "10");
	manager.registerCVar("t_cmd.name", CVarType::String, "orko");

	CHECK(CVarCommand::isCommand("set x 1"));
	CHECK(CVarCommand::isCommand("  get x"));
	CHECK(CVarCommand::isCommand("find pre"));
	CHECK(CVarCommand::isCommand("reset x"));
	CHECK_FALSE(CVarCommand::isCommand("print('hi')"));
	CHECK_FALSE(CVarCommand::isCommand(""));

	// set / get
	CHECK(CVarCommand::run("set t_cmd.speed 25") == "t_cmd.speed = 25");
	CHECK(CVarCommand::run("get t_cmd.speed") == "t_cmd.speed = 25");
	// a String cvar keeps spaces (everything after the name is the value)
	CHECK(CVarCommand::run("set t_cmd.name big ork") == "t_cmd.name = big ork");

	// reset
	CHECK(CVarCommand::run("reset t_cmd.speed") == "t_cmd.speed = 10");

	// find lists matches (a "\n"-joined block); at least our two keys
	const String listing = CVarCommand::run("find t_cmd.");
	CHECK(listing.find("t_cmd.speed") != String::npos);
	CHECK(listing.find("t_cmd.name") != String::npos);

	// malformed / error paths come back as "error: ..." strings, never throw
	CHECK(CVarCommand::run("get").rfind("error:", 0) == 0);
	CHECK(CVarCommand::run("set t_cmd.speed").rfind("error:", 0) == 0);
	CHECK(CVarCommand::run("set t_cmd.speed nan").rfind("error:", 0) == 0);
	CHECK(CVarCommand::run("get t_cmd.ghost").rfind("error:", 0) == 0);
	CHECK(CVarCommand::run("wibble x").rfind("error:", 0) == 0);
}

TEST_CASE("CVarCommand::parseSet extracts name and value", "[cvar][unit]")
{
	String name;
	String value;
	CHECK(CVarCommand::parseSet("set roller_gravity 30", name, value));
	CHECK(name == "roller_gravity");
	CHECK(value == "30");

	CHECK(CVarCommand::parseSet("set title hello world", name, value));
	CHECK(name == "title");
	CHECK(value == "hello world");

	CHECK_FALSE(CVarCommand::parseSet("get x", name, value));
	CHECK_FALSE(CVarCommand::parseSet("set x", name, value)); // no value
	CHECK_FALSE(CVarCommand::parseSet("set", name, value));
}
