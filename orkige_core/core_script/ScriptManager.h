/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	ScriptManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptManager_h__7_7_2026__12_00_00__
#define __ScriptManager_h__7_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

#include <sol/sol.hpp>

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */
	//! owner of the global Lua scripting state (sol2 backed) - the Lua
	//! BACKEND behind the neutral core_script/ScriptRuntime.h seam; only the
	//! Meta_Lua.h macros, the seam implementation and backend-specific tests
	//! touch this class directly. A ScriptRuntime instance creates one before
	//! the ORKIGE_MODULE init functions run to expose the registered meta
	//! types to Lua; applications that never boot scripting still get the
	//! non-scripting parts of the meta export (see metaExportState()).
	class ORKIGE_CORE_DLL ScriptManager : public Singleton<ScriptManager>
	{
		DECL_OSINGLETON(ScriptManager)
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		sol::state luaState;		//!< the owned Lua state
		//--- Methods -----------------------------------------
	public:
		//! constructor - creates the Lua state and opens the base libraries
		ScriptManager();
		//! destructor
		virtual ~ScriptManager();

		//! get the sol2 Lua state
		inline sol::state & state()
		{
			return this->luaState;
		}

		//! state targeted by the OrkigeMetaExport macros: the singleton state
		//! if scripting was booted, otherwise a private throwaway fallback
		//! state. The fallback keeps module initialisation (TypeManager and
		//! component factory registration happen inside the same export
		//! functions) working in applications that never boot scripting.
		static sol::state & metaExportState();
	protected:
	private:
		//! @brief harden the freshly opened Lua state to the sandbox
		//! security allowlist: a scene or script file is CONTENT that may be
		//! authored by an agent or fetched from an untrusted source, so loading
		//! it must never grant arbitrary file read/write, process execution or
		//! code loading. Every script sandbox
		//! (ScriptRuntime::loadScriptInstance) is a fresh environment whose
		//! reads fall through to these globals, so removing a capability here
		//! removes it from EVERY game- and editor-script sandbox at once. Keeps
		//! the pure-computation stdlib (base/string/table/math + a read-only os
		//! subset) and the engine API tables; denies io/os process+file
		//! access/require/package/load/loadstring/loadfile/dofile/debug/
		//! collectgarbage. See Docs/lua-api.md (Sandbox / security).
		void applySandboxAllowlist();
	};
	/** @} */
}

#endif //__ScriptManager_h__7_7_2026__12_00_00__
