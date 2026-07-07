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

#include <sol/sol.hpp>

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */
	//! owner of the global Lua scripting state (sol2 backed).
	//! Create one instance (like the other engine singletons) before the
	//! ORKIGE_MODULE init functions run to expose the registered meta types
	//! to Lua; applications that never create a ScriptManager still get the
	//! non-scripting parts of the meta export (see metaExportState()).
	class ORKIGE_CORE_DLL ScriptManager : public Singleton<ScriptManager>
	{
		DECL_OSINGLETON(ScriptManager)
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		sol::state luaState;	//!< the owned Lua state
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
	};
	/** @} */
}

#endif //__ScriptManager_h__7_7_2026__12_00_00__
