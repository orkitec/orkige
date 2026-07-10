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
#include "core_base/PropertyValue.h"

#include <vector>

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

	//! @brief one EXPORTED property declared by a script's top-level
	//! `properties` table (script-declared properties auto-exposed in the
	//! inspector). A backend-neutral, sol2-free description: the
	//! ScriptComponent turns a vector of these into a DYNAMIC per-instance
	//! PropertySchema so a script's tunables surface in the inspector / debug
	//! protocol / MCP through the SAME registry path as a C++ component's
	//! static properties. Read via ScriptRuntime::readExportedProperties; the
	//! None backend never produces any (scripts simply have no exports there).
	struct ScriptExportProperty
	{
		String			name;			//!< the property name (its schema key + the `self` field)
		PropertyKind	kind;			//!< the reflected value shape
		PropertyValue	defaultValue;	//!< the declared default (the value a fresh instance starts at)
		String			referenceHint;	//!< AssetRef/ObjectRef: the asset-kind / object-type hint ("" otherwise)
		bool			hasRange = false;	//!< true when min/max were declared (a slider hint)
		float			minValue = 0.0f;	//!< the declared slider lower bound
		float			maxValue = 0.0f;	//!< the declared slider upper bound

		ScriptExportProperty() : kind(PropertyKind::Float) {}
	};

	//! @brief the OPTIONAL-TRAILING-ARGUMENTS parameter for functions
	//! registered through ScriptRuntime::registerFunction: declare it as the
	//! LAST parameter of the registered callable and script calls may pass
	//! any number of extra values (callbacks, ease names, delays, ...). Read
	//! them through the ScriptCallback::fromArgs / ScriptRuntime::numberArg /
	//! stringArg helpers - call sites stay free of backend types and guards.
#ifdef ORKIGE_LUA
	typedef sol::variadic_args ScriptArgs;
#else
	//! placeholder in no-scripting builds (the registered callables must
	//! keep compiling; they are never invoked)
	struct ScriptArgs {};
#endif

	//! @brief a script function value received by a registered C++ function
	//! (e.g. a tween's onUpdate/onComplete closure), invokable from C++.
	//! Backend-neutral at every call site; in no-scripting builds callbacks
	//! are never valid(). Copyable - safe to stash inside std::function
	//! wrappers handed to core systems.
	class ORKIGE_CORE_DLL ScriptCallback
	{
		//--- Variables ---------------------------------------
	private:
#ifdef ORKIGE_LUA
		sol::protected_function mFunction;	//!< the wrapped Lua function (may be invalid)
#endif
		//--- Methods -----------------------------------------
	public:
		//! an invalid (never-callable) callback
		ScriptCallback();

		//! @brief read optional trailing argument #index (0-based within the
		//! ScriptArgs tail) as a callback; invalid when absent, nil or not a
		//! function
		static ScriptCallback fromArgs(ScriptArgs const & args, int index);

		//! is there a script function to call
		bool valid() const;
		//! @brief call the script function with no arguments
		//! @return false with *outError set on a script error
		bool invoke(String * outError) const;
		//! @brief call the script function with count number arguments
		//! @return false with *outError set on a script error. A script
		//! function explicitly returning false sets *outRequestedStop (the
		//! documented "return false from onUpdate to cancel" channel).
		bool invokeNumbers(float const * values, int count,
			bool * outRequestedStop, String * outError) const;
	protected:
	private:
	};

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

		//! @brief read a script file's top-level `properties` table (the
		//! exported-property declaration) into backend-neutral
		//! descriptors. The Lua backend loads the file into a THROWAWAY
		//! sandboxed environment (init/update are NOT run - this only reads the
		//! declaration) and translates each entry; a parse error or a missing
		//! `properties` table yields an empty vector. In ORKIGE_SCRIPTING=OFF
		//! builds this ALWAYS returns {} - scripts have no exports there, which
		//! keeps the "scripting disabled is honest, not a crash" contract (a
		//! ScriptComponent then reports an empty dynamic schema and keeps
		//! whatever export values were serialized, inert). Declaration:
		//! @code
		//! properties = {
		//!   moveSpeed = { type = "number", default = 4.5, min = 0, max = 20 },
		//!   canDouble = { type = "bool",   default = true },
		//!   tint      = { type = "color",  default = {1,1,1,1} },
		//!   icon      = { type = "asset",  kind = "texture" },
		//! }
		//! @endcode
		//! Types map 1:1 to PropertyKind: number->Float, bool->Bool,
		//! string->String, vec3->Vec3, color->Color, asset->AssetRef,
		//! object->ObjectRef.
		std::vector<ScriptExportProperty> readExportedProperties(
			String const & scriptFile);

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

		//! read optional trailing argument #index (0-based) as a number
		//! (fallback when absent or not a number) - @see ScriptArgs
		static double numberArg(ScriptArgs const & args, int index,
			double fallback);
		//! read optional trailing argument #index (0-based) as a string
		//! (fallback when absent or not a string) - @see ScriptArgs
		static String stringArg(ScriptArgs const & args, int index,
			String const & fallback);

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

		//! @brief register a C++ callable as a top-level global function (not
		//! under a table) - the loc() localisation accessor uses it so scripts
		//! call loc("key") directly. No-op without a backend.
		template<typename Callable>
		void registerGlobalFunction(char const * functionName,
			Callable && function)
		{
#ifdef ORKIGE_LUA
			this->luaManager.state()[functionName] =
				std::forward<Callable>(function);
#else
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
		//! @brief inject a reflected PropertyValue onto the `self` table as its
		//! natural Lua type (a script's EXPORTED properties are
		//! pushed here before init so the script reads them as tunables -
		//! `self.moveSpeed` etc). Number/Bool/String/reference map to the
		//! scalar Lua types; Vec3 -> {x,y,z}, Color -> {r,g,b,a} array tables.
		//! Backend-neutral (no-op without a scripting backend) so ScriptComponent
		//! stays free of sol2 - the mapping lives in the ScriptRuntime impl.
		void setSelfProperty(char const * key, PropertyValue const & value);

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
		//! @brief call an OPTIONAL script-defined function name(self, args...)
		//! if the environment defines it - the generalization of the lifecycle
		//! calls above (callInit/callUpdate/callShutdown are the fixed cases).
		//! The per-instance `self` table is ALWAYS the first argument; the
		//! forwarded args follow (engine pointers registered as usertypes come
		//! through as such, e.g. the OTHER GameObject of a contact event). A
		//! function the script does not define is a silent no-op (returns true) -
		//! event hooks like onContactBegin are opt-in.
		//! @return false with *outError set on a script error
		template<typename... Args>
		bool callFunction(char const * name, String * outError, Args &&... args)
		{
			oAssert(outError);
#ifdef ORKIGE_LUA
			if (!this->environment.valid())
			{
				return true;
			}
			const sol::object function = this->environment[name];
			if (!function.is<sol::protected_function>())
			{
				return true;	// optional hook not defined - nothing to call
			}
			const sol::protected_function_result result =
				function.as<sol::protected_function>()(this->selfTable,
					std::forward<Args>(args)...);
			if (!result.valid())
			{
				const sol::error error = result;
				*outError = error.what();
				return false;
			}
			return true;
#else
			(void)name;
			((void)args, ...);	// fold: silence the unused forwarded args
			*outError = "scripting disabled (built with ORKIGE_SCRIPTING=OFF)";
			return false;
#endif
		}
	protected:
	private:
		//! only ScriptRuntime::loadScriptInstance creates instances
		ScriptInstance();
	};
	/** @} */
}

#endif //__ScriptRuntime_h__8_7_2026__10_00_00__
