/**************************************************************
	created:	2026/07/08 at 10:00
	filename: 	ScriptRuntime.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptRuntime_h__8_7_2026__10_00_00__
#define __ScriptRuntime_h__8_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"
#include "core_util/optr.h"

// The ONLY place (besides Meta.h) that selects a scripting backend: call
// sites include this header unconditionally and never test ORKIGE_LUA.
#ifdef ORKIGE_LUA
#include "core_script/ScriptManager.h"
#include <sol/sol.hpp>
#include <utility>
#endif

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */
	class ScriptInstance;

	//! @brief the backend-neutral scripting seam: everything outside the
	//! core_base/Meta_*.h backends and core_script itself talks to scripting
	//! exclusively through this facade - call sites carry no ORKIGE_LUA
	//! guards. The facade compiles in EVERY configuration; built with
	//! ORKIGE_SCRIPTING=OFF each operation fails with an honest "scripting
	//! disabled" error instead of failing to compile. A future second
	//! backend (or a resurrected Python one) implements this same interface.
	//! @remarks create one instance (like the other engine singletons)
	//! BEFORE the ORKIGE_MODULE init functions run: in Lua builds it boots
	//! the ScriptManager the OrkigeMetaExport macros target.
	class ORKIGE_CORE_DLL ScriptRuntime : public Singleton<ScriptRuntime>
	{
		DECL_OSINGLETON(ScriptRuntime)
		//--- Types -------------------------------------------
	public:
		//! outcome of ScriptRuntime::runString
		struct Result
		{
			bool			success = false;	//!< the chunk parsed and ran
			String			error;				//!< the error when !success
			StringVector	returnValues;		//!< stringified script return values
		};
		//--- Variables ---------------------------------------
	private:
		String scriptSearchRoot;	//!< directory relative script paths resolve against ("" = none)
#ifdef ORKIGE_LUA
		ScriptManager luaManager;	//!< the booted Lua backend (owns the sol2 state)
#endif
		//--- Methods -----------------------------------------
	public:
		//! constructor - boots the compiled-in scripting backend (if any)
		ScriptRuntime();
		//! destructor
		virtual ~ScriptRuntime();

		//! is a scripting backend compiled in AND booted
		static bool available();
		//! name of the compiled-in backend: "Lua", or "none" when scripting is off
		static char const * backendName();

		//! @brief set the directory project-relative script paths resolve
		//! against - the runtimes point this at the open project's root
		//! directory, so a ScriptComponent's "scripts/player.lua" finds
		//! "<project>/scripts/player.lua" ("" = no project open)
		inline void setScriptSearchRoot(String const & rootDirectory)
		{
			this->scriptSearchRoot = rootDirectory;
		}
		//! @see ScriptRuntime::setScriptSearchRoot
		inline String const & getScriptSearchRoot() const
		{
			return this->scriptSearchRoot;
		}
		//! @brief resolve a script path to an existing file: absolute paths
		//! pass through, relative ones are tried against the search root
		//! first, then against the current working directory
		//! @return the resolved path, or "" when no such file exists
		String resolveScriptPath(String const & scriptPath) const;

		//! @brief run a chunk of script source (the console / smoke-test
		//! path); never throws - errors come back in Result::error
		Result runString(String const & code);

		//! @brief load a script file into a fresh sandboxed instance (its
		//! globals never leak into other instances, even of the same file)
		//! @return the instance, or NULL with *outError set (file not found,
		//! script error, scripting disabled)
		optr<ScriptInstance> loadScriptInstance(String const & scriptFile,
			String * outError);

		//! read a global (path like {"shared","jumper","wins"}) as a number
		double getNumber(StringVector const & path, double fallback);
		//! read a global (path walk like getNumber) as a bool
		bool getBool(StringVector const & path, bool fallback);
		//! read a global (path walk like getNumber) as a string
		String getString(StringVector const & path, String const & fallback);

		//! does a global table of that name exist (registration idempotence)
		bool hasGlobalTable(String const & name);
		//! create the global table if it does not exist yet
		void ensureGlobalTable(String const & name);

		//! @brief register a C++ callable as <tableName>.<functionName>
		//! (the table is created when missing) - no-op without a backend
		template<typename Callable>
		void registerFunction(char const * tableName,
			char const * functionName, Callable && function)
		{
#ifdef ORKIGE_LUA
			sol::state & lua = this->luaManager.state();
			if (!lua[tableName].is<sol::table>())
			{
				lua[tableName] = lua.create_table();
			}
			lua[tableName][functionName] = std::forward<Callable>(function);
#else
			(void)tableName;
			(void)functionName;
			(void)function;
#endif
		}
	protected:
	private:
		//! the honest OFF-configuration error message
		static String disabledError();
	};

	//! @brief one loaded script file in its own sandboxed environment,
	//! obtained from ScriptRuntime::loadScriptInstance. Script contract
	//! (every function optional): init(self), update(self, dt),
	//! shutdown(self); `self` is a per-instance table populated through
	//! setSelfValue before callInit runs.
	class ORKIGE_CORE_DLL ScriptInstance
	{
		friend class ScriptRuntime;
		//--- Variables ---------------------------------------
	private:
#ifdef ORKIGE_LUA
		sol::environment		environment;	//!< per-instance sandbox (reads fall through to _G)
		sol::table				selfTable;		//!< the `self` table passed to the script functions
		sol::protected_function	updateFunction;	//!< cached env["update"] (may be invalid)
#endif
		//--- Methods -----------------------------------------
	public:
		//! destructor - releases the instance's script references
		~ScriptInstance();
		//! set a field on the `self` table handed to the script functions
		//! (component owners push typed pointers here) - no-op without a backend
		template<typename ValueType>
		void setSelfValue(char const * key, ValueType value)
		{
#ifdef ORKIGE_LUA
			this->selfTable[key] = value;
#else
			(void)key;
			(void)value;
#endif
		}
		//! @brief run init(self) if the script defines one, then cache the
		//! script's update function
		//! @return false with *outError set on a script error
		bool callInit(String * outError);
		//! run update(self, dt) if the script defines one
		//! @return false with *outError set on a script error
		bool callUpdate(float deltaTime, String * outError);
		//! run shutdown(self) if the script defines one
		//! @return false with *outError set on a script error
		bool callShutdown(String * outError);
	protected:
	private:
		//! only ScriptRuntime::loadScriptInstance creates instances
		ScriptInstance();
	};
	/** @} */
}

#endif //__ScriptRuntime_h__8_7_2026__10_00_00__
