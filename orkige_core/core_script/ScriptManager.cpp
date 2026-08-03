/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	ScriptManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptManager.h"
#include "core_debugnet/Json.h"
#include "core_filesystem/DataResource.h"

#include <cstddef>
#include <utility>

namespace Orkige
{
	IMPL_OSINGLETON(ScriptManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ScriptManager::ScriptManager()
	{
		// Open ONLY the pure-computation standard libraries. The `package`
		// library (which installs `require`) is deliberately NOT opened, and
		// the io/debug libraries are never opened either. `os` is opened only
		// so applySandboxAllowlist() can keep its read-only clock subset and
		// drop the rest. A scene or script file is untrusted CONTENT: loading
		// it must not open a path to the filesystem, other processes or
		// arbitrary code loading - see applySandboxAllowlist().
		this->luaState.open_libraries(
			sol::lib::base,
			sol::lib::string,
			sol::lib::table,
			sol::lib::math,
			sol::lib::os);
		this->applySandboxAllowlist();
	}
	//---------------------------------------------------------
	void ScriptManager::applySandboxAllowlist()
	{
		sol::state & lua = this->luaState;

		// --- deny arbitrary file / process / code-loading globals -----------
		// Each of these is a direct escape from "content" to arbitrary machine
		// access, so none is reachable from a sandboxed script:
		//   load / loadstring - compile+run arbitrary source at runtime
		//   loadfile / dofile - read+run an arbitrary file from disk
		//   require / package - load Lua/C modules off the package path
		//   io / debug        - raw file handles / the reflection+hook library
		// io, debug and package are never opened above; niling them here is
		// defensive so the denial is explicit and survives a future
		// open_libraries edit. collectgarbage is deliberately NOT denied: it
		// controls only the GC (force a collection / read the count), carries
		// no file/process/code-loading capability, and both game scripts (a
		// loading-screen collect) and the engine's own weak-handle orphan
		// tests legitimately drive it.
		char const * const denied[] = {
			"load", "loadstring", "loadfile", "dofile",
			"require", "package",
			"io", "debug"
		};
		for (char const * name : denied)
		{
			lua[name] = sol::lua_nil;
		}

		// --- os: keep only the read-only clock/format subset ----------------
		// os.execute/remove/rename/exit/getenv/tmpname/setlocale are process +
		// filesystem + environment access and are dropped; os.time/os.clock/
		// os.date are pure read-only helpers a game legitimately wants (RNG
		// seeding, timing, timestamp formatting) and carry no capability, so a
		// pruned `os` table exposes exactly those three.
		if (lua["os"].is<sol::table>())
		{
			const sol::table full = lua["os"];
			sol::table safe = lua.create_table();
			safe["time"] = full["time"];
			safe["clock"] = full["clock"];
			safe["date"] = full["date"];
			lua["os"] = safe;
		}

		// The permitted computation surface stays: base (assert, error, pcall,
		// xpcall, ipairs, pairs, next, select, tonumber, tostring, type,
		// set/getmetatable, raw*, print, _G, _VERSION) plus the string, table
		// and math libraries. `print` writes only to the process log stream (no
		// file/process access) and stays as-is. The sanctioned engine API
		// tables (world/shared/self, music/save/screen/haptics/input, loc,
		// component handles, and - editor only - editor.*) are installed AFTER
		// this hardening and are unaffected.

		// The one capability handed BACK in place of the denied file globals:
		// `data`, a read-only view of the CONTENT this game ships. It grants
		// strictly less than `io` - no writes, no handles, no paths outside the
		// project, and only what is actually mounted - so authored data files
		// stop having to be baked into executable .lua source.
		this->installDataTable();
	}
	//---------------------------------------------------------
	namespace
	{
		//! @brief one parsed JSON value as its natural Lua value: objects become
		//! string-keyed tables, arrays 1-based array tables, scalars themselves
		//! and null a nil (so an absent-in-JSON and a null-in-JSON member read
		//! the same in Lua). Recursion is bounded because JsonValue::parse
		//! bounds nesting depth before this ever sees a value.
		sol::object jsonToLua(sol::state_view lua, JsonValue const & value)
		{
			switch (value.getType())
			{
			case JsonValue::Type::Bool:
				return sol::make_object(lua, value.asBool());
			case JsonValue::Type::Number:
				return sol::make_object(lua, value.asNumber());
			case JsonValue::Type::String:
				return sol::make_object(lua, value.asString());
			case JsonValue::Type::Array:
			{
				sol::table array = lua.create_table();
				for (std::size_t i = 0; i < value.size(); ++i)
				{
					array[i + 1] = jsonToLua(lua, value.at(i));
				}
				return array;
			}
			case JsonValue::Type::Object:
			{
				sol::table table = lua.create_table();
				for (auto const & member : value.members())
				{
					table[member.first] = jsonToLua(lua, member.second);
				}
				return table;
			}
			case JsonValue::Type::Null:
			default:
				return sol::object(lua, sol::in_place, sol::lua_nil);
			}
		}
		//! the honest two-value refusal every `data` function returns: nil plus
		//! a message, so a script reads `local text, err = data.read(name)`
		sol::variadic_results dataFailure(sol::state_view lua,
			String const & error)
		{
			sol::variadic_results results;
			results.push_back(sol::object(lua, sol::in_place, sol::lua_nil));
			results.push_back(sol::make_object(lua, error));
			return results;
		}
		//! the one-value success
		sol::variadic_results dataSuccess(sol::object value)
		{
			sol::variadic_results results;
			results.push_back(std::move(value));
			return results;
		}
	}
	//---------------------------------------------------------
	void ScriptManager::installDataTable()
	{
		sol::state & lua = this->luaState;
		sol::table data = lua.create_table();

		// data.read(name) -> text | nil, error
		// FORMAT-NEUTRAL by design: JSON, CSV, INI or a game's own text
		// grammar all come back as the bytes on record. Restricting formats
		// would buy nothing - only EXECUTING content was ever the risk, and
		// reading a `.lua` file as TEXT is not running it.
		data["read"] = [](sol::this_state ts, String const & name)
			-> sol::variadic_results
		{
			sol::state_view view(ts);
			String text;
			String error;
			if (!DataResource::read(name, text, error))
			{
				return dataFailure(view, error);
			}
			return dataSuccess(sol::make_object(view, text));
		};

		// data.json(text) -> table | nil, error
		// the decode helper over the engine's own JSON codec (the one the
		// editor's MCP endpoint parses with) - a convenience on top of
		// data.read, never the only road to a data file
		data["json"] = [](sol::this_state ts, String const & text)
			-> sol::variadic_results
		{
			sol::state_view view(ts);
			JsonValue value;
			if (!JsonValue::parse(text, value))
			{
				return dataFailure(view, "not valid JSON");
			}
			return dataSuccess(jsonToLua(view, value));
		};

		// data.readJson(name) -> table | nil, error (read + decode in one)
		data["readJson"] = [](sol::this_state ts, String const & name)
			-> sol::variadic_results
		{
			sol::state_view view(ts);
			String text;
			String error;
			if (!DataResource::read(name, text, error))
			{
				return dataFailure(view, error);
			}
			JsonValue value;
			if (!JsonValue::parse(text, value))
			{
				return dataFailure(view,
					"data resource '" + name + "' is not valid JSON");
			}
			return dataSuccess(jsonToLua(view, value));
		};

		lua["data"] = data;
	}
	//---------------------------------------------------------
	ScriptManager::~ScriptManager()
	{
	}
	//---------------------------------------------------------
	sol::state & ScriptManager::metaExportState()
	{
		if (ScriptManager::getSingletonPtr() != NULL)
		{
			return ScriptManager::getSingleton().state();
		}
		// no scripting booted: keep meta export working against a private
		// state that is simply never used for script execution. Heap-allocated
		// and intentionally never freed: usertypes registered into it at static
		// init would otherwise be torn down by lua_close at PROGRAM EXIT, where
		// the destruction order against sol's own global usertype registry is
		// unspecified (a use-after-free on macOS). Skipping the exit-time
		// lua_close avoids it; the static pointer keeps the state reachable so
		// LeakSanitizer stays quiet, and the OS reclaims it at exit anyway.
		static sol::state * fallbackState = new sol::state();
		return *fallbackState;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
