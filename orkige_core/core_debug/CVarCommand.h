/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	CVarCommand.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __CVarCommand_h__9_7_2026__14_00_00__
#define __CVarCommand_h__9_7_2026__14_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	//! @brief the tiny, scripting-FREE console command grammar over
	//! CVarManager: the stable non-Lua surface both the editor Console line and
	//! the (Lua-agnostic) tooling speak. Four verbs, whitespace-tokenised:
	//!   set <name> <value>   change a cvar (value may contain spaces for a
	//!                        String cvar - everything after the name is the value)
	//!   get <name>           print a cvar's current value
	//!   find <prefix>        list registered cvars whose name starts with prefix
	//!                        (an empty/absent prefix lists all)
	//!   reset <name>         restore a cvar to its registered default
	//! @remarks lives in core_debug with ZERO Lua dependency by design - the
	//! Lua `cvar` table and the MSG_SET_CVAR protocol branch are separate, thin
	//! layers on the same registry. run() never throws: a malformed line or a
	//! rejected value comes back as an "error: ..." string.
	namespace CVarCommand
	{
		//! @brief is the FIRST whitespace-delimited token one of the four cvar
		//! verbs (set/get/find/reset)? The editor Console uses this to decide
		//! whether a line routes here or to the Lua REPL.
		bool ORKIGE_CORE_DLL isCommand(String const & line);

		//! @brief parse and run a command line against the CVarManager
		//! singleton; returns a human-readable, single- or multi-line result
		//! (a value, a listing, or "error: ...").
		String ORKIGE_CORE_DLL run(String const & line);

		//! @brief extract the name and value of a `set <name> <value>` line
		//! WITHOUT running it (the editor sends these as MSG_SET_CVAR to a
		//! running player instead of applying locally). false when the line is
		//! not a well-formed set command.
		bool ORKIGE_CORE_DLL parseSet(String const & line, String & outName,
			String & outValue);
	}
}

#endif //__CVarCommand_h__9_7_2026__14_00_00__
