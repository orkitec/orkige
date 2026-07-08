/**************************************************************
	created:	2026/07/08 at 10:00
	filename: 	ScriptRuntime.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptRuntime.h"

#include <filesystem>

namespace Orkige
{
	IMPL_OSINGLETON(ScriptRuntime)
#ifdef ORKIGE_LUA
	namespace
	{
		//! walk a global path like {"shared","jumper","wins"} to its value
		//! (nil when any step is missing or not a table)
		sol::object resolveGlobalPath(sol::state & lua, StringVector const & path)
		{
			if (path.empty())
			{
				return sol::object(sol::lua_nil);
			}
			sol::object current = lua[path[0]];
			for (std::size_t i = 1; i < path.size(); ++i)
			{
				if (!current.is<sol::table>())
				{
					return sol::object(sol::lua_nil);
				}
				current = current.as<sol::table>()[path[i]];
			}
			return current;
		}
	}
#endif //ORKIGE_LUA
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ScriptRuntime::ScriptRuntime()
	{
	}
	//---------------------------------------------------------
	ScriptRuntime::~ScriptRuntime()
	{
	}
	//---------------------------------------------------------
	bool ScriptRuntime::available()
	{
#ifdef ORKIGE_LUA
		return ScriptRuntime::getSingletonPtr() != NULL;
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	char const * ScriptRuntime::backendName()
	{
#ifdef ORKIGE_LUA
		return "Lua";
#else
		return "none";
#endif
	}
	//---------------------------------------------------------
	String ScriptRuntime::resolveScriptPath(String const & scriptPath) const
	{
		if (scriptPath.empty())
		{
			return "";
		}
		std::error_code ignored;
		const std::filesystem::path path(scriptPath);
		if (path.is_absolute())
		{
			return std::filesystem::is_regular_file(path, ignored) ? scriptPath : "";
		}
		if (!this->scriptSearchRoot.empty())
		{
			const std::filesystem::path rooted =
				std::filesystem::path(this->scriptSearchRoot) / path;
			if (std::filesystem::is_regular_file(rooted, ignored))
			{
				return rooted.string();
			}
		}
		// working-directory fallback: keeps bare-scene runs (no project) and
		// tests working with cwd-relative script paths
		if (std::filesystem::is_regular_file(path, ignored))
		{
			return scriptPath;
		}
		return "";
	}
	//---------------------------------------------------------
	ScriptRuntime::Result ScriptRuntime::runString(String const & code)
	{
		Result result;
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		const sol::protected_function_result scriptResult =
			lua.safe_script(code, sol::script_pass_on_error);
		if (!scriptResult.valid())
		{
			const sol::error error = scriptResult;
			result.error = error.what();
			return result;
		}
		result.success = true;
		for (int i = 0; i < scriptResult.return_count(); ++i)
		{
			const String value =
				lua["tostring"](scriptResult.get<sol::object>(i));
			result.returnValues.push_back(value);
		}
#else
		(void)code;
		result.error = ScriptRuntime::disabledError();
#endif
		return result;
	}
	//---------------------------------------------------------
	optr<ScriptInstance> ScriptRuntime::loadScriptInstance(
		String const & scriptFile, String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		const String resolvedPath = this->resolveScriptPath(scriptFile);
		if (resolvedPath.empty())
		{
			*outError = "script file not found (searched the project root '" +
				this->scriptSearchRoot + "' and the working directory)";
			return optr<ScriptInstance>();
		}
		sol::state & lua = this->luaManager.state();
		optr<ScriptInstance> instance(new ScriptInstance());
		// per-instance sandbox: a fresh table whose reads fall through to the
		// real globals - writes stay in the instance (isolation between
		// instances; deliberate sharing goes through the `shared` table)
		instance->environment = sol::environment(lua, sol::create, lua.globals());
		const sol::protected_function_result loadResult = lua.safe_script_file(
			resolvedPath, instance->environment, sol::script_pass_on_error);
		if (!loadResult.valid())
		{
			const sol::error error = loadResult;
			*outError = error.what();
			return optr<ScriptInstance>();
		}
		instance->selfTable = lua.create_table();
		return instance;
#else
		(void)scriptFile;
		*outError = ScriptRuntime::disabledError();
		return optr<ScriptInstance>();
#endif
	}
	//---------------------------------------------------------
	double ScriptRuntime::getNumber(StringVector const & path, double fallback)
	{
#ifdef ORKIGE_LUA
		const sol::object value =
			resolveGlobalPath(this->luaManager.state(), path);
		return value.is<double>() ? value.as<double>() : fallback;
#else
		(void)path;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::getBool(StringVector const & path, bool fallback)
	{
#ifdef ORKIGE_LUA
		const sol::object value =
			resolveGlobalPath(this->luaManager.state(), path);
		return value.is<bool>() ? value.as<bool>() : fallback;
#else
		(void)path;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	String ScriptRuntime::getString(StringVector const & path,
		String const & fallback)
	{
#ifdef ORKIGE_LUA
		const sol::object value =
			resolveGlobalPath(this->luaManager.state(), path);
		return value.is<String>() ? value.as<String>() : fallback;
#else
		(void)path;
		return fallback;
#endif
	}
	//---------------------------------------------------------
	bool ScriptRuntime::hasGlobalTable(String const & name)
	{
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		return lua[name].is<sol::table>();
#else
		(void)name;
		return false;
#endif
	}
	//---------------------------------------------------------
	void ScriptRuntime::ensureGlobalTable(String const & name)
	{
#ifdef ORKIGE_LUA
		sol::state & lua = this->luaManager.state();
		if (!lua[name].is<sol::table>())
		{
			lua[name] = lua.create_table();
		}
#else
		(void)name;
#endif
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	String ScriptRuntime::disabledError()
	{
		return "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
	}
	//---------------------------------------------------------
	//--- ScriptInstance --------------------------------------
	//---------------------------------------------------------
	ScriptInstance::ScriptInstance()
	{
	}
	//---------------------------------------------------------
	ScriptInstance::~ScriptInstance()
	{
	}
	//---------------------------------------------------------
	bool ScriptInstance::callInit(String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		const sol::object initObject = this->environment["init"];
		if (initObject.is<sol::protected_function>())
		{
			const sol::protected_function_result initResult =
				initObject.as<sol::protected_function>()(this->selfTable);
			if (!initResult.valid())
			{
				const sol::error error = initResult;
				*outError = error.what();
				return false;
			}
		}
		const sol::object updateObject = this->environment["update"];
		if (updateObject.is<sol::protected_function>())
		{
			this->updateFunction = updateObject.as<sol::protected_function>();
		}
		return true;
#else
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptInstance::callUpdate(float deltaTime, String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		if (!this->updateFunction.valid())
		{
			return true;	// no update function - nothing to do
		}
		const sol::protected_function_result result =
			this->updateFunction(this->selfTable, deltaTime);
		if (!result.valid())
		{
			const sol::error error = result;
			*outError = error.what();
			return false;
		}
		return true;
#else
		(void)deltaTime;
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
	//---------------------------------------------------------
	bool ScriptInstance::callShutdown(String * outError)
	{
		oAssert(outError);
#ifdef ORKIGE_LUA
		if (!this->environment.valid())
		{
			return true;
		}
		const sol::object shutdownObject = this->environment["shutdown"];
		if (shutdownObject.is<sol::protected_function>())
		{
			const sol::protected_function_result result =
				shutdownObject.as<sol::protected_function>()(this->selfTable);
			if (!result.valid())
			{
				const sol::error error = result;
				*outError = error.what();
				return false;
			}
		}
		return true;
#else
		*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
		return false;
#endif
	}
}
