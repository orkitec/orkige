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
#include "core_script/ScriptLibrary.h"
#include "core_script/ScriptRuntime.h"

#include <cstddef>
#include <stdexcept>
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

		// The other capability handed back, and the one `require` was standing
		// in the way of: `script`, the LIBRARY LOADER. It is not a hole in the
		// allowlist above, and the reasoning matters - a scene can ALREADY
		// attach a path-bound ScriptComponent naming any project-relative .lua
		// file, so a hostile .oscene can already cause any project script to
		// run. A loader jailed to project-relative .lua names reaches EXACTLY
		// the same files: the same capability with better ergonomics, not a new
		// one. What stock `require` additionally granted - the package path, C
		// modules, an arbitrary search - and what `load` grants - code
		// synthesised from a STRING - stay denied.
		this->installScriptTable();
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
	namespace
	{
		//! the registry slot the per-sandbox library caches hang off. The Lua
		//! REGISTRY is not reachable from any script, so the cache is invisible
		//! - a script cannot inspect it, poison it or clear it.
		char const * const kLibraryCacheRegistryKey = "orkige.scriptLibraries";

		//! @brief the libraries `script.require` is CURRENTLY loading,
		//! outermost first. Library loads NEST SYNCHRONOUSLY on the one script
		//! thread, so this stack is exactly the dependency chain being resolved
		//! - a name already on it closes a cycle. Refusing there is what keeps
		//! a mutual require from recursing until the C stack dies.
		StringVector & libraryLoadChain()
		{
			static StringVector chain;
			return chain;
		}

		//! @brief the caller's "<chunk>:<line>: " prefix - exactly what Lua's
		//! own `error(message, 2)` prepends. luaL_where is the C AUXILIARY api,
		//! not the `debug` LIBRARY the sandbox denies, and script chunks already
		//! load under their project-relative names, so a refusal reads
		//! "scripts/player.component.lua:12: ..." with no reflection surface
		//! granted to anyone.
		String callerWhere(lua_State * state)
		{
			luaL_where(state, 1);
			char const * where = lua_tostring(state, -1);
			const String prefix = (where != NULL) ? where : "";
			lua_pop(state, 1);
			return prefix;
		}
	}
	//---------------------------------------------------------
	void ScriptManager::installScriptTable()
	{
		sol::state & lua = this->luaState;

		// CACHING DECISION: per SANDBOX, not per process. The engine's sandbox
		// doctrine is that one script instance's state never leaks into
		// another's and that deliberate sharing goes through the `shared`
		// table; a process-wide module registry (what stock Lua `require`
		// keeps) would be a SECOND, undeclared sharing channel - two components
		// mutating "their" copy of a library would silently be mutating one
		// table. So each requiring environment gets its own copy of a library,
		// loaded at most once for that environment: repeated requires inside
		// one sandbox are cheap and return the IDENTICAL value (identity holds
		// wherever it is observable), and a sandbox's libraries die with it,
		// which is also what makes hot-reload correct - a rebuilt sandbox
		// re-reads its libraries with no cache to invalidate.
		sol::table caches = lua.create_table();
		sol::table cacheMeta = lua.create_table();
		cacheMeta["__mode"] = "k";	// weak KEYS: a bucket dies with its sandbox
		caches[sol::metatable_key] = cacheMeta;
		lua.registry()[kLibraryCacheRegistryKey] = caches;

		sol::table script = lua.create_table();

		// script.require(name) -> the library's return value
		// It RAISES rather than returning nil+error the way `data` does, and
		// the difference is honest: absent DATA is a situation a game handles,
		// while a missing LIBRARY is a broken dependency - stock `require`
		// raises for the same reason. The message carries the requiring line's
		// own "<file>:<line>: " prefix (@see callerWhere).
		script["require"] = [](sol::this_state ts, sol::this_environment te,
			String const & name) -> sol::object
		{
			sol::state_view view(ts);
			const auto refuse = [&](String const & message) -> sol::object
			{
				throw std::runtime_error(callerWhere(ts.lua_state()) + message);
			};
			String error;
			if(!ScriptLibrary::checkName(name, error))
			{
				return refuse(error);
			}
			if(ScriptRuntime::getSingletonPtr() == NULL)
			{
				return refuse("script.require('" + name + "'): the script "
					"runtime is not up");
			}
			// the requiring sandbox owns the cache bucket; a require from the
			// bare globals (the console) keys off the globals table
			sol::table owner = te
				? sol::table(static_cast<sol::environment &>(te))
				: sol::table(view.globals());
			sol::table caches = view.registry()[kLibraryCacheRegistryKey];
			const sol::object bucketObject = caches.get<sol::object>(owner);
			sol::table bucket;
			if(bucketObject.is<sol::table>())
			{
				bucket = bucketObject.as<sol::table>();
			}
			else
			{
				bucket = view.create_table();
				caches.set(owner, bucket);
			}
			const sol::object cached = bucket.get<sol::object>(name);
			if(cached.valid() && cached != sol::lua_nil)
			{
				return cached;
			}
			StringVector & chain = libraryLoadChain();
			for(String const & loading : chain)
			{
				if(loading == name)
				{
					return refuse(ScriptLibrary::cycleError(chain, name));
				}
			}
			// THE READ GOES THROUGH THE RESOURCE READER, never fopen: the ONE
			// routing loadScriptInstance uses, so a library resolves out of a
			// mounted pak or an APK entry exactly as it does loose. A file read
			// here would work on a desktop and find nothing on a phone.
			String source;
			if(!ScriptRuntime::getSingleton().readScriptSource(name, source,
				&error))
			{
				return refuse("script.require('" + name + "'): " + error);
			}
			// a library gets its OWN fresh environment (reads fall through to
			// the globals), never the requiring sandbox's: it must not see -
			// or write into - the component's `self` and locals, and each
			// sandbox's copy stays independent
			sol::environment libraryEnv(view.lua_state(), sol::create,
				view.globals());
			chain.push_back(name);
			const sol::protected_function_result loadResult = view.safe_script(
				source, libraryEnv, sol::script_pass_on_error, "@" + name);
			chain.pop_back();
			if(!loadResult.valid())
			{
				const sol::error scriptError = loadResult;
				return refuse("script.require('" + name + "'): " +
					String(scriptError.what()));
			}
			sol::object value = (loadResult.return_count() > 0)
				? loadResult.get<sol::object>(0)
				: sol::object(view, sol::in_place, sol::lua_nil);
			if(!value.valid() || value == sol::lua_nil)
			{
				// a chunk that returns nothing still counts as loaded (stock
				// require's convention), so the cache never re-runs it
				value = sol::make_object(view, true);
			}
			bucket.set(name, value);
			return value;
		};

		lua["script"] = script;
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
