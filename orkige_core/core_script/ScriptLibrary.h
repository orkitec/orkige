/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptLibrary.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptLibrary_h__3_8_2026__16_00_00__
#define __ScriptLibrary_h__3_8_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */

	//! @brief the PURE rules behind `script.require` - the loader that lets one
	//! project script use another as a LIBRARY (shared helpers, common math, a
	//! tuning table's accessors) instead of every `.component.lua` being an
	//! island.
	//!
	//! WHY THIS IS NOT A SANDBOX ESCALATION. A scene can already attach a
	//! path-bound ScriptComponent naming any project-relative `.lua` file, so a
	//! hostile `.oscene` can already cause any project script to RUN. A loader
	//! restricted to project-relative `.lua` names reaches EXACTLY THE SAME
	//! FILES - it is the same capability with better ergonomics, not a new one.
	//! What WOULD be an escalation is `load()` over an arbitrary STRING (code
	//! synthesised from data) or `loadfile()` over an arbitrary PATH, and both
	//! stay denied: the name is jailed to the project (PathJail) and must name a
	//! `.lua` file, so the reachable set is bounded by what the project ships.
	//!
	//! The containment decision is PathJail::isSafeRelativeEntry - the engine's
	//! ONE path-containment primitive, the same rule a pak entry and a `data`
	//! read are jailed by. Nothing here touches the filesystem, so it is
	//! headless-testable on its own; the READ itself goes through the injected
	//! ResourceReader (@see ScriptRuntime::loadScriptLibrary), never `fopen`, so
	//! a library resolves identically as a loose file, a mounted pak entry and
	//! an APK entry.
	namespace ScriptLibrary
	{
		//! the file extension a library name must carry (".lua")
		char const * extension();

		//! @brief the PURE name guard: is @p name a legal project-relative
		//! library name? Rejects an empty name, an ABSOLUTE path, a drive/UNC
		//! root, ANY ".." traversal segment (all four via PathJail) and any name
		//! that does not end in ".lua". No filesystem access needed.
		//! @param outError filled with an honest reason when the name is refused
		//! (untouched on success).
		bool checkName(String const & name, String & outError);

		//! @brief the honest cycle refusal: the message for a library that is
		//! ALREADY being loaded further up the chain, rendered as the chain that
		//! closed the loop ("a.lua -> b.lua -> a.lua"). Blowing the C stack on a
		//! mutual `script.require` is never acceptable, so the loader keeps the
		//! in-progress chain and refuses with this instead.
		//! @param loadingChain the names currently being loaded, outermost first
		//! @param name the name being requested again
		String cycleError(StringVector const & loadingChain,
			String const & name);
	}

	/** @} */
}

#endif //__ScriptLibrary_h__3_8_2026__16_00_00__
