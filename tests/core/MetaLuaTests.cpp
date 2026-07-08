/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	MetaLuaTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
	This file tests the sol2/Lua META BACKEND itself (Meta_Lua.h and
	ScriptManager), so unlike the rest of the suite it talks to sol2
	directly; it is only compiled into the test binary when
	ORKIGE_SCRIPTING=LUA (see CMakeLists.txt). Backend-neutral scripting
	behavior belongs in ScriptRuntimeTests.cpp instead.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_script/ScriptManager.h>
#include <core_event/GlobalEventManager.h>
#include <core_base/Object.h>
#include <core_base/TypeManager.h>

namespace Orkige
{
	//! test type exported to Lua through the standard OOBJECT macro dance
	class LuaMathThing : public Object
	{
		OOBJECT(LuaMathThing, Object)
		//--- Variables ---------------------------------------
	public:
		int value;	//!< exposed to Lua via OVAR
		//--- Methods -----------------------------------------
	public:
		LuaMathThing() : Object("LuaMathThing"), value(0) {}
		virtual ~LuaMathThing() {}
		int addTo(int amount)
		{
			this->value += amount;
			return this->value;
		}
	};
	//---------------------------------------------------------
	OOBJECT_IMPL(LuaMathThing)
		OCONSTRUCTOR0()
		OFUNC(addTo)
		OVAR(value)
	OOBJECT_END
	//---------------------------------------------------------
	static void exportLuaMathThing()
	{
		static bool exported = false;
		if(!exported)
		{
			exported = true;
			LuaMathThing::OrkigeMetaExport("orkige_core_tests");
		}
	}
}

namespace
{
	//! C++-side receiver for the Lua-triggered event (headless mirror of the
	//! hello_orkige Lua smoke test)
	struct LuaEventProbe
	{
		bool received = false;
		bool onLuaEvent(Orkige::Event const & event)
		{
			this->received = event.getData() &&
				event.getData()->getObjectID() == "lua_payload";
			return false;
		}
	};
}

TEST_CASE("ScriptManager boots a working Lua state", "[lua]")
{
	Orkige::CoreTestEnvironment::get();
	// the ScriptRuntime member booted the Lua backend singleton
	REQUIRE(Orkige::ScriptManager::getSingletonPtr() != nullptr);
	// the meta export state IS the booted singleton state (not the fallback)
	CHECK(&Orkige::ScriptManager::metaExportState() ==
		&Orkige::ScriptManager::getSingleton().state());

	sol::state & lua = Orkige::ScriptManager::getSingleton().state();
	const sol::protected_function_result result =
		lua.safe_script("return 6 * 7", sol::script_pass_on_error);
	REQUIRE(result.valid());
	CHECK(result.get<int>() == 42);
}

TEST_CASE("Module-exported core types are constructible and callable from Lua", "[lua]")
{
	Orkige::CoreTestEnvironment::get();
	sol::state & lua = Orkige::ScriptManager::getSingleton().state();

	const sol::protected_function_result result = lua.safe_script(R"lua(
		local o = Object.new1('from_lua')
		return o:getObjectID()
	)lua", sol::script_pass_on_error);
	REQUIRE(result.valid());
	CHECK(result.get<std::string>() == "from_lua");
}

TEST_CASE("A registered test type's methods and members work from Lua", "[lua]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::exportLuaMathThing();

	// registering also put the type into the TypeManager
	CHECK(Orkige::TypeManager::getSingleton().isRegistered("LuaMathThing"));

	sol::state & lua = Orkige::ScriptManager::getSingleton().state();
	const sol::protected_function_result result = lua.safe_script(R"lua(
		local thing = LuaMathThing.new0()
		thing:addTo(40)
		local total = thing:addTo(2)		-- method call
		local before = thing.value			-- OVAR read
		thing.value = 100					-- OVAR write
		return total, before, thing:addTo(0)
	)lua", sol::script_pass_on_error);
	REQUIRE(result.valid());
	CHECK(result.get<int>(0) == 42);
	CHECK(result.get<int>(1) == 42);
	CHECK(result.get<int>(2) == 100);
}

TEST_CASE("Lua-triggered events reach C++ listeners with their payload", "[lua]")
{
	Orkige::CoreTestEnvironment::get();

	LuaEventProbe probe;
	const Orkige::EventType type("lua_unit_event");
	optr<Orkige::EventListener> listener =
		Orkige::GlobalEventManager::getSingleton().bind(
			type, &LuaEventProbe::onLuaEvent, &probe);

	sol::state & lua = Orkige::ScriptManager::getSingleton().state();
	const sol::protected_function_result result = lua.safe_script(R"lua(
		local payload = Object.new1('lua_payload')
		local ev = Event('lua_unit_event')
		ev:setData(payload)
		assert(ev:getData():getObjectID() == 'lua_payload')
		GlobalEventManager.getSingleton():trigger(ev)
	)lua", sol::script_pass_on_error);
	REQUIRE(result.valid());
	CHECK(probe.received);

	REQUIRE(Orkige::GlobalEventManager::getSingleton().delListener(
		listener, type));
}
