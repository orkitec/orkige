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
#include "core_script/ScriptDebugCore.h"

#include <functional>
#include <map>
#include <vector>

// The ONLY place (besides Meta.h) that selects a scripting backend: call
// sites include this header unconditionally and never test ORKIGE_LUA.
#ifdef ORKIGE_LUA
#include "core_script/ScriptManager.h"
// MetaLuaDetail::makeHandle (the weak-handle factory) backs the handle seams
// below - setSelfHandle / callHookWithObject / registerHandle*Accessor - so
// callers hand engine objects to Lua as WEAK handles without ever naming a
// backend detail or testing ORKIGE_LUA themselves.
#include "core_base/Meta.h"
#include <sol/sol.hpp>
#include <utility>
#endif

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */
	class ScriptInstance;
	class GameObject;			//core_game/GameObject.h - the scriptable-component registry resolves + injects handles over it
	class TypeInfo;				//core_base/TypeInfo.h - the reflected component kind a script name maps to
	struct ScriptEventPayload;	//core_script/ScriptEventBus.h - passed by reference

	//! @brief header-visible RAII that scopes the ScriptEventBus's current
	//! subscription OWNER to `owner` for a script call, restoring it on exit.
	//! Declared here (defined in the seam .cpp) so the ScriptInstance call
	//! template can tag a subscribe()/emit() with its sandbox WITHOUT pulling the
	//! bus header into every call site - the seam stays the one place that knows
	//! the bus. No-op-shaped when the bus is unused.
	class ORKIGE_CORE_DLL ScriptCallScope
	{
	public:
		explicit ScriptCallScope(void const * owner);
		~ScriptCallScope();
	private:
		void const * mPrevious;
		ScriptCallScope(ScriptCallScope const &) = delete;
		ScriptCallScope & operator=(ScriptCallScope const &) = delete;
	};

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
		//! @brief call the script function with an event payload converted to a
		//! Lua table (the ScriptEventBus delivery). The bounded payload maps
		//! back to natural Lua values: scalars directly, a one-level nested
		//! table as a sub-table; string keys become string keys, integer keys
		//! array indices. @return false with *outError on a script error.
		bool invokePayload(ScriptEventPayload const & payload,
			String * outError) const;
	protected:
	private:
	};

	//! @brief a backend-neutral key/value bundle a host function exchanges with
	//! a script: flat string fields + string-list fields (the shape of a Lua
	//! table of scalars/arrays, without a backend type leaking to call sites).
	//! Editor script TOOLS use it to carry an editor-verb's arguments in and its
	//! reply out - the same flat field/list shape the DebugMessage verb replies
	//! already use, so the editor adapts one to the other with no new vocabulary.
	struct ScriptValueMap
	{
		std::map<String, String>		fields;	//!< scalar fields by key
		std::map<String, StringVector>	lists;	//!< string-list fields by key

		//! is a scalar field present
		bool has(String const & key) const
		{
			return this->fields.find(key) != this->fields.end();
		}
		//! read a scalar field ("" when absent)
		String const & get(String const & key) const
		{
			static const String empty;
			std::map<String, String>::const_iterator it = this->fields.find(key);
			return it != this->fields.end() ? it->second : empty;
		}
		//! set a scalar field
		void set(String const & key, String const & value)
		{
			this->fields[key] = value;
		}
	};

	//! @brief a host callback exposed to scripts as `<table>.<name>(argsTable)`:
	//! the single Lua table argument arrives converted into `args`, the callback
	//! fills `reply` (returned to the script as a table) and returns true;
	//! returning false raises a Lua error carrying `error` at the call site (the
	//! honest failure the calling script sees, with its file:line). The sol2
	//! marshalling lives in the ScriptRuntime implementation, so registering
	//! subsystems (the editor's script-tool host) stay free of backend types.
	typedef std::function<bool(ScriptValueMap const & args,
		ScriptValueMap & reply, String & error)> ScriptHostFunction;

	//! @brief one scriptable component KIND, declared at its OWN meta-export
	//! site through the OSCRIPT_HANDLE macro (@see core_base/Meta_Lua.h /
	//! Meta_None.h). ONE declaration replaces the former hand-wired pair of
	//! blocks a scriptable component needed: the `self.<name>` injection in
	//! ScriptComponent::populateSelfTable AND the `world.<accessor>` handle
	//! accessor - the registry drives BOTH surfaces plus the generic
	//! `self:getComponent` / `world.getComponent` floor. The thunks are built
	//! at the component's own translation unit (where the type is complete), so
	//! the type-erased weak-handle push is the SAME MetaLuaDetail::makeHandle
	//! path the hand-wired accessors used - a component is never silently
	//! script-unreachable again. Backend-neutral by construction: the
	//! self-injection thunk speaks only ScriptInstance::setSelfHandle; the Lua
	//! handle-maker is compiled only in the Lua backend (absent, and never
	//! referenced, in ORKIGE_SCRIPTING=OFF).
	struct ScriptComponentAccess
	{
		String			name;				//!< the script vocabulary name: self.<name> + getComponent("<name>")
		bool			injectSelf = true;	//!< populateSelfTable sets self.<name> when the owner carries the component
		String			worldAccessor;		//!< "" or the legacy convenience accessor name (e.g. "getTransform")
		TypeInfo const*	type = nullptr;		//!< the reflected component kind (links the script name to the MCP kind name)
		//! set self[key] to a WEAK handle when the owner carries the component
		//! (no-op otherwise) - neutral: speaks only ScriptInstance::setSelfHandle
		std::function<void(GameObject &, ScriptInstance &, char const *)> injectHandle;
#ifdef ORKIGE_LUA
		//! the owner's component as a weak-handle-or-nil Lua value (backs
		//! world.<accessor> / world.getComponent / self:getComponent) - built
		//! where the component type is complete, so makeHandle can instantiate.
		//! An absent component (has-component guarded) is nil, never a throw.
		std::function<sol::object(sol::state_view, GameObject &)> makeHandleFor;
#endif
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

		//! @brief emit a script event from the `events.emit(name, payload)`
		//! binding: bounded-convert the trailing payload argument (a plain Lua
		//! table of string/number/bool values, one nesting level deep) and hand
		//! it to the ScriptEventBus. An absent/nil payload is an empty event; a
		//! value the bus cannot carry (a function/userdata, or a table nested
		//! deeper than one level) raises a Lua error at the emit call site - the
		//! honest failure at emit. A no-op without a scripting backend.
		void emitEventFromScript(String const & name, ScriptArgs const & args);

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

		//! @brief the string-keyed GLOBAL names of the scripting state (the
		//! engine API tables, registered types, script-created globals) -
		//! the editor's completion source. Sorted; empty without a backend.
		StringVector globalNames();
		//! @brief the string-keyed member names of one global: a table's own
		//! keys, plus (for a registered usertype/global object) the keys of
		//! its metatable's __index table - so a type's methods enumerate too.
		//! Sorted; empty for an unknown name or without a backend.
		StringVector globalMemberNames(String const & name);

		//! read optional trailing argument #index (0-based) as a number
		//! (fallback when absent or not a number) - @see ScriptArgs
		static double numberArg(ScriptArgs const & args, int index,
			double fallback);
		//! read optional trailing argument #index (0-based) as a string
		//! (fallback when absent or not a string) - @see ScriptArgs
		static String stringArg(ScriptArgs const & args, int index,
			String const & fallback);
		//! read optional trailing argument #index (0-based) as a bool (fallback
		//! when absent or not a boolean) - @see ScriptArgs
		static bool boolArg(ScriptArgs const & args, int index, bool fallback);

		//! the natural Lua value type of a trailing argument, for functions that
		//! store a value UNDER its type (the `save` table dispatches on this)
		enum ArgType
		{
			AT_ABSENT = 0,	//!< no such argument (or nil)
			AT_BOOL,		//!< a boolean
			AT_NUMBER,		//!< a number
			AT_STRING,		//!< a string
			AT_OTHER		//!< some other Lua type (table/function/...)
		};
		//! @brief the value type of optional trailing argument #index - @see ArgType
		static ArgType argType(ScriptArgs const & args, int index);

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

		//! @brief register <tableName>.<functionName>(id) that hands Lua a WEAK
		//! handle over the woptr the resolver returns (nil when the woptr is
		//! empty). The handle locks per method call and raises an honest,
		//! pcall-catchable error naming the object once it is gone - never a raw
		//! pointer outliving its engine object. Backend-neutral: the resolver
		//! deals only in woptr, the weak-handle wrapping stays behind this seam.
		//! No-op without a scripting backend.
		template<typename Resolver>
		void registerHandleAccessor(char const * tableName,
			char const * functionName, Resolver resolver)
		{
#ifdef ORKIGE_LUA
			sol::state & lua = this->luaManager.state();
			if (!lua[tableName].is<sol::table>())
			{
				lua[tableName] = lua.create_table();
			}
			lua[tableName][functionName] = [resolver](String const & id)
			{
				return MetaLuaDetail::makeHandle(resolver(id));
			};
#else
			(void)tableName;
			(void)functionName;
			(void)resolver;
#endif
		}

		//! @brief the array sibling of registerHandleAccessor: the resolver
		//! returns a std::vector<woptr<T>> and Lua receives an array of WEAK
		//! handles (empty entries dropped). No-op without a scripting backend.
		template<typename Resolver>
		void registerHandleListAccessor(char const * tableName,
			char const * functionName, Resolver resolver)
		{
#ifdef ORKIGE_LUA
			sol::state & lua = this->luaManager.state();
			if (!lua[tableName].is<sol::table>())
			{
				lua[tableName] = lua.create_table();
			}
			lua[tableName][functionName] = [resolver](String const & id)
			{
				auto weaks = resolver(id);
				std::vector<typename decltype(
					MetaLuaDetail::makeHandle(weaks.front()))::value_type> handles;
				handles.reserve(weaks.size());
				for (auto const & weak : weaks)
				{
					auto handle = MetaLuaDetail::makeHandle(weak);
					if (handle)
					{
						handles.push_back(*handle);
					}
				}
				return handles;
			};
#else
			(void)tableName;
			(void)functionName;
			(void)resolver;
#endif
		}

		//! @brief register a host callback as <tableName>.<functionName>(args)
		//! (the table is created when missing): the script passes ONE table of
		//! arguments, which arrives as a ScriptValueMap; the callback fills a
		//! reply map returned to the script as a table, or returns false to raise
		//! a Lua error carrying `error` at the call site. The backend-neutral way
		//! for a C++ subsystem to expose a table-in/table-out function to scripts
		//! (the editor's script-tool `editor.*` verbs). No-op without a backend.
		//! @see ScriptHostFunction
		void registerHostFunction(char const * tableName,
			char const * functionName, ScriptHostFunction function);

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

		//! @brief register a scriptable-component access declaration (idempotent
		//! BY NAME - a re-registration replaces the entry). Called at module-init
		//! time by the OSCRIPT_HANDLE macro, so it must be safe BEFORE any
		//! ScriptRuntime instance exists (the registry is a process-wide list).
		static void registerComponentAccess(ScriptComponentAccess entry);
		//! @brief the registered scriptable-component declarations, in
		//! registration order - the self.<name> / world.<accessor> /
		//! getComponent("<name>") vocabulary. Populated in both backends (the
		//! None backend's OSCRIPT_HANDLE registers a handle-less entry, so
		//! self.<name> injection stays honest there and world accessors no-op).
		static std::vector<ScriptComponentAccess> const & componentAccessRegistry();
		//! @brief install the registry-driven world.<accessor>(id) family and
		//! the generic world.getComponent(id, name) onto the scripting state,
		//! resolving ids through `resolveById`. Idempotent (called from
		//! ScriptComponent::ensureScriptApi); a no-op without a scripting backend.
		void installComponentAccessors(
			std::function<GameObject * (String const &)> resolveById);
#ifdef ORKIGE_LUA
		//! @brief the owner-by-id + component-name resolution shared by
		//! world.getComponent, the world.<accessor> convenience accessors and
		//! self:getComponent: nil for an absent object, an unknown vocabulary
		//! name, or a missing component (the quiet-probe contract, never a
		//! throw); else the component's weak handle. A name matches either the
		//! script vocabulary name ("transform") or the reflected kind name
		//! ("TransformComponent", the MCP get_component name). Lua-backend only.
		sol::object componentHandleFor(sol::state_view lua, String const & id,
			String const & name);
#endif

		//--- script debugger -------------------------------------------------
		// The backend-neutral debug seam: breakpoints, stepping and paused-frame
		// introspection for the editor's script debugger. The line-hook
		// machinery, call-stack walking and local/upvalue readback are an
		// implementation detail of the Lua backend behind these methods; call
		// sites (the player debug link, the editor, tests) never see a backend
		// type. Everything here is MAIN-THREAD only: a break blocks inside
		// script execution and the pump handler runs on that same thread.
		//
		// Lifecycle: the line hook is installed ONLY while at least one
		// breakpoint is set or a step is pending - with neither, script
		// execution carries zero debug overhead. On a hit the runtime blocks
		// inside the hook and calls the registered pump handler in a loop until
		// debugResume() releases it; the handler services the debug transport
		// (and must eventually resume - a vanished client calls debugDetach()).
		// Without a registered handler a hit resumes immediately (never a
		// wedge). In ORKIGE_SCRIPTING=OFF builds every operation refuses with
		// the honest disabled error; on the browser player (which cannot block
		// its main thread) setDebugBreakpoints refuses honestly instead.

		//! @brief replace the WHOLE breakpoint set (the protocol's full-list
		//! replace; an empty list clears everything and uninstalls the hook).
		//! False with *outError set when scripting is disabled or the platform
		//! cannot block for a break (the browser player).
		bool setDebugBreakpoints(
			std::vector<ScriptBreakpoint> const & breakpoints,
			String * outError);
		//! @brief register the handler the runtime calls IN A LOOP while a
		//! break holds execution: each call should service the debug transport
		//! once (and pace itself - a short sleep); a resume/step command from
		//! the transport releases the loop. Pass an empty function to clear
		//! (a break then resumes immediately).
		void setDebugPumpHandler(std::function<void()> handler);
		//! is script execution currently paused at a break
		bool isDebugBroken() const;
		//! @brief increments on every break entry, so a transport can send its
		//! break notification exactly once per pause (0 until the first break)
		unsigned int debugBreakSequence() const;
		//! the paused location's normalized script path ("" while running)
		String debugBreakFile() const;
		//! the paused location's 1-based line (0 while running)
		int debugBreakLine() const;
		//! the call stack captured at the break (innermost first; empty while
		//! running)
		std::vector<ScriptStackFrame> debugStackFrames() const;
		//! @brief release a held break: continue freely (None) or arm a step
		//! (In/Over/Out) that pauses again at the next matching line. A no-op
		//! while not broken.
		void debugResume(ScriptStepMode stepMode = ScriptStepMode::None);
		//! @brief read variables at a paused frame. An empty expandPath lists
		//! the frame's locals + upvalues; a non-empty one names a root variable
		//! and a chain of table keys ("[3]" for a non-string key) and lists
		//! THAT table's fields. Bounded by maxEntries; results are shallow
		//! (type + display string, tables marked expandable). Empty with
		//! *outError set while not broken / on a bad frame or path.
		std::vector<ScriptDebugVariable> debugVariables(int frameIndex,
			StringVector const & expandPath, int maxEntries,
			String * outError);
		//! @brief the disconnect path: clear every breakpoint and, when broken,
		//! resume - a client that vanished mid-break must never leave the
		//! runtime wedged inside the hook. Safe to call at any time.
		void debugDetach();
	protected:
	private:
		//! the honest OFF-configuration error message
		static String disabledError();
		//! id -> live GameObject for the registry-driven accessors (set by
		//! installComponentAccessors; empty until then)
		std::function<GameObject * (String const &)> componentResolver;
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
		//! @brief set a `self` field to a WEAK handle over the given woptr (nil
		//! when it is empty): the script reads e.g. self.transform / self.gameObject
		//! as a handle that locks per method call and raises an honest,
		//! pcall-catchable error once the object is gone - never a raw pointer a
		//! cached field could dereference after teardown. Backend-neutral: the
		//! caller passes only a woptr, the weak-handle wrapping stays behind this
		//! seam. No-op without a scripting backend.
		template<typename ObjectType>
		void setSelfHandle(char const * key, woptr<ObjectType> const & weak)
		{
#ifdef ORKIGE_LUA
			this->selfTable[key] = MetaLuaDetail::makeHandle(weak);
#else
			(void)key;
			(void)weak;
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

		//! @brief install self:getComponent(name) on this instance's `self`
		//! table - the generic floor resolving any declared component KIND
		//! through the ScriptComponentAccess registry (nil for an absent or
		//! unknown kind, never a throw), by the SAME vocabulary and weak-handle
		//! currency as world.getComponent. Reads self.id at call time (so it
		//! needs no owner captured). Backend-neutral (no-op without a backend).
		void installComponentAccessor();

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
			// tag any events.subscribe/emit the hook runs with THIS sandbox, so
			// its subscriptions are cancelled when the instance is retired
			ScriptCallScope ownerScope(this);
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
		//! @brief call an OPTIONAL hook name(self, handle) passing a WEAK handle
		//! over the given woptr (nil when empty) - the weak-handle sibling of
		//! callFunction for delivering an engine object (the OTHER GameObject of a
		//! contact event) as a handle rather than a raw pointer. A hook that
		//! stashes the handle touches it safely later: a stale touch raises an
		//! honest script error instead of reading freed state. Backend-neutral:
		//! the caller passes only a woptr. No-op semantics match callFunction.
		template<typename ObjectType>
		bool callHookWithObject(char const * name, String * outError,
			woptr<ObjectType> const & weak)
		{
#ifdef ORKIGE_LUA
			return this->callFunction(name, outError,
				MetaLuaDetail::makeHandle(weak));
#else
			(void)weak;
			return this->callFunction(name, outError);
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
