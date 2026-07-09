/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	CVarManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __CVarManager_h__9_7_2026__14_00_00__
#define __CVarManager_h__9_7_2026__14_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

#include <cstdio>
#include <functional>
#include <map>

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */

	//! @brief a console variable's value type. The type is fixed at
	//! registration and drives parse+validation in CVarManager::setString.
	enum class CVarType
	{
		Int,		//!< a signed integer
		Float,		//!< a single-precision float
		Bool,		//!< a boolean ("1"/"0", "true"/"false", "on"/"off")
		String		//!< free-form text (never rejected)
	};

	//! @brief console-variable behaviour flags (a bitmask). v1 stores them and
	//! acts on PERSIST/READONLY; CHEAT is recorded but not yet enforced (a v2
	//! concern - see the console blueprint's v1 CUT list).
	enum CVarFlags
	{
		CVAR_NONE		= 0,
		CVAR_PERSIST	= 1 << 0,	//!< written back to the project on explicit save (only these persist)
		CVAR_CHEAT		= 1 << 1,	//!< marked as a cheat (enforcement deferred to v2)
		CVAR_READONLY	= 1 << 2	//!< set*/reset are refused after registration
	};

	//! @brief one registered console variable: a typed, flagged value with a
	//! canonical string form (the single source of truth - typed reads parse
	//! it) plus an optional onChange hook fired after every accepted change.
	//! @remarks the onChange callback is the LIVE RE-APPLY seam: a cvar bound
	//! to e.g. physics gravity or vsync sets its C++ system from onChange, so a
	//! `set` (console line, Lua cvar.set or the MSG_SET_CVAR protocol message)
	//! takes effect immediately without any per-frame polling.
	class ORKIGE_CORE_DLL CVar
	{
		//--- Types -------------------------------------------
	public:
		//! fired AFTER an accepted change (initial default apply included when
		//! requested); receives the cvar so the handler can read the new value
		typedef std::function<void(CVar const &)> ChangeCallback;
		//--- Variables ---------------------------------------
	public:
		String			name;			//!< the cvar name (its registry key)
		CVarType		type;			//!< the value type (fixed at registration)
		String			value;			//!< current value, canonical string form
		String			defaultValue;	//!< the registered default (reset target)
		String			description;	//!< human-readable help text ("" = none)
		int				flags;			//!< CVarFlags bitmask
		ChangeCallback	onChange;		//!< live re-apply hook (may be empty)
		//--- Methods -----------------------------------------
	public:
		CVar();
		//! current value parsed as an int (0 on a non-Int cvar / garbage)
		int asInt() const;
		//! current value parsed as a float (0 on a non-Float cvar / garbage)
		float asFloat() const;
		//! current value parsed as a bool
		bool asBool() const;
		//! current value as a string (the canonical form)
		String const & asString() const { return value; }
		//! is a flag set
		bool hasFlag(CVarFlags flag) const { return (flags & flag) != 0; }
	};

	//! @brief the console-variable registry: a single-threaded, renderer- and
	//! scripting-FREE registry of typed cvars living entirely in orkige_core.
	//! @remarks LogManager-style singleton (IMPL_OSINGLETON_GETCREATE, created
	//! on first getSingleton) so it is always available in EVERY process (the
	//! player, the editor, the unit tests) with zero boot wiring and works
	//! unchanged in ORKIGE_SCRIPTING=OFF builds - the Lua `cvar` table and the
	//! MSG_SET_CVAR protocol branch are thin OPTIONAL layers ON TOP of it, not
	//! dependencies of it. Registration is two-way: the OCVAR_* static-init
	//! macros for compile-time cvars and registerCVar() for dynamic/script
	//! ones. setString is the single mutation path: it parses+validates per
	//! type, rejects a bad value with an error (never crashes) and fires the
	//! cvar's onChange on success.
	class ORKIGE_CORE_DLL CVarManager : public Singleton<CVarManager>
	{
		DECL_OSINGLETON(CVarManager)
		//--- Variables ---------------------------------------
	public:
		//! the project-manifest Settings prefix a persisted cvar rides under
		//! ("cvar."), so "cvar.roller_gravity" <-> the "roller_gravity" cvar
		static const String SETTING_PREFIX;
	private:
		std::map<String, CVar>	mCVars;			//!< the registry (sorted by name)
		//! manifest overrides for cvars NOT YET registered (order-independence:
		//! a "cvar.x" Setting applied before the system owning "x" registers is
		//! held here and re-applied the moment registerCVar mints "x")
		std::map<String, String>	mPendingOverrides;
		bool					mInOnChange;	//!< re-entrancy guard for onChange
		//--- Methods -----------------------------------------
	public:
		CVarManager();
		virtual ~CVarManager();

		//! @brief register a cvar (idempotent: a repeat registration of the
		//! same name keeps the EXISTING value, only (re)applying an onChange /
		//! description when given - so a manifest override loaded before the
		//! owning system registers survives). The default is parsed+normalised
		//! to the type's canonical form; onChange, when given, fires once with
		//! the initial value.
		//! @return the (new or existing) cvar, never NULL
		CVar * registerCVar(String const & name, CVarType type,
			String const & defaultValue, int flags = CVAR_NONE,
			String const & description = String(),
			CVar::ChangeCallback onChange = CVar::ChangeCallback());

		//! is a cvar of that name registered
		bool exists(String const & name) const;
		//! the cvar by name, or NULL when unregistered
		CVar * find(String const & name);
		//! @overload
		CVar const * find(String const & name) const;

		//--- typed reads (fallback on unknown name) ----------
		int getInt(String const & name, int fallback = 0) const;
		float getFloat(String const & name, float fallback = 0.0f) const;
		bool getBool(String const & name, bool fallback = false) const;
		String getString(String const & name,
			String const & fallback = String()) const;

		//! @brief the single mutation path: set a cvar from a string, parsing
		//! and validating per its type. Refused (returns false, *outError set
		//! when non-NULL, cvar unchanged) on an unknown name, a READONLY cvar
		//! or a value that does not parse for the type. On success the
		//! canonical value is stored and onChange fires. Never throws/crashes.
		bool setString(String const & name, String const & value,
			String * outError = 0);

		//! reset a cvar to its registered default (fires onChange); false with
		//! *outError set on an unknown name or a READONLY cvar
		bool reset(String const & name, String * outError = 0);

		//! @brief the registered cvar names starting with prefix, sorted (the
		//! console's `find` data source). An empty prefix returns every name.
		StringVector findByPrefix(String const & prefix) const;

		//! @brief apply the "cvar."-prefixed keys of a project-manifest Settings
		//! map: an ALREADY-registered cvar is set through setString at once (so
		//! onChange fires and a bad value is rejected honestly); an override for
		//! a not-yet-registered cvar is HELD and re-applied the moment its owner
		//! calls registerCVar - so the manifest override survives regardless of
		//! whether the cvar is a compile-time OCVAR (registered first) or a Lua
		//! cvar.registerNumber (registered when the scene's scripts run). Safe to
		//! call on project load, before or after the owning systems come up.
		void applySettings(std::map<String, String> const & settings);

		//! @brief fold the CVAR_PERSIST cvars whose value differs from their
		//! default into the given map as "cvar.<name>" -> value (transient-by-
		//! default: only PERSIST cvars, and only when actually changed, ride
		//! into a saved manifest). Keys of previously-persisted cvars that are
		//! back at their default are ERASED so save() drops them.
		void collectPersisted(std::map<String, String> & settings) const;

	protected:
	private:
		//! store value into cvar and fire onChange (guarded against re-entrancy)
		void applyAndNotify(CVar & cvar, String const & canonicalValue);
		//! parse+validate value for type into *outCanonical; false + *outError
		//! on a value the type rejects
		static bool coerce(CVarType type, String const & value,
			String * outCanonical, String * outError);
	};

	//! @brief the record an OCVAR_* macro instantiates at static-init time: its
	//! constructor registers the cvar with the (auto-created) CVarManager. Safe
	//! during static initialisation - getSingleton lazily constructs the manager
	//! on first touch (IMPL_OSINGLETON_GETCREATE), so registration order among
	//! translation units does not matter.
	struct ORKIGE_CORE_DLL CVarStaticInit
	{
		CVarStaticInit(String const & name, CVarType type,
			String const & defaultValue, int flags, String const & description);
	};

	//--- OCVAR_* helpers: turn a typed literal into the canonical string ---
	inline String cvarToString(int value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%d", value);
		return buffer;
	}
	inline String cvarToString(float value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.9g", value);
		return buffer;
	}
	inline String cvarToString(bool value)
	{
		return value ? "1" : "0";
	}
	inline String cvarToString(char const * value)
	{
		return String(value);
	}

	/** @} End of "addtogroup Debug"*/
}

//! @brief compile-time cvar registration. Declare ONE per translation unit at
//! file scope; the unique C++ identifier is the first argument, the cvar name
//! (the registry key, a string) the second, then the typed default literal,
//! the CVarFlags bitmask and a help string. The value is registered when the
//! program's static initialisers run, before main. Give an onChange later via
//!   CVarManager::getSingleton().find("name")->onChange = ...;
//! (the macros deliberately stay declarative - the live hook belongs to the
//! owning system's init).
#define OCVAR_INT(identifier, name, defaultValue, flags, description)		\
	static ::Orkige::CVarStaticInit identifier(name,						\
		::Orkige::CVarType::Int, ::Orkige::cvarToString(int(defaultValue)),	\
		(flags), description)
#define OCVAR_FLOAT(identifier, name, defaultValue, flags, description)		\
	static ::Orkige::CVarStaticInit identifier(name,						\
		::Orkige::CVarType::Float,											\
		::Orkige::cvarToString(float(defaultValue)), (flags), description)
#define OCVAR_BOOL(identifier, name, defaultValue, flags, description)		\
	static ::Orkige::CVarStaticInit identifier(name,						\
		::Orkige::CVarType::Bool, ::Orkige::cvarToString(bool(defaultValue)),\
		(flags), description)
#define OCVAR_STRING(identifier, name, defaultValue, flags, description)		\
	static ::Orkige::CVarStaticInit identifier(name,						\
		::Orkige::CVarType::String, ::Orkige::String(defaultValue),			\
		(flags), description)

#endif //__CVarManager_h__9_7_2026__14_00_00__
