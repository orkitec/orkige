/**************************************************************
	created:	2026/08/03 at 12:00
	filename: 	DataResource.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __DataResource_h__3_8_2026__12_00_00__
#define __DataResource_h__3_8_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <cstddef>

namespace Orkige
{
	/** \addtogroup Filesystem
	*  @{ */

	//! @brief reading AUTHORED DATA by project-relative resource name - the
	//! read a sandboxed script performs when it wants a level table, an item
	//! list, a dialogue tree or a tuning table as CONTENT instead of code.
	//!
	//! The read goes through the process-wide ResourceReader (ResourceAccess)
	//! and NEVER through `fopen`. That is the load-bearing decision: a direct
	//! file read works on a desktop and fails on a phone, because the same data
	//! lives inside the APK there and inside the payload in a browser. One call,
	//! three packagings - a loose file, a mounted pak and an APK entry all
	//! resolve by the SAME project-relative name.
	//!
	//! Deliberately format-NEUTRAL: whatever the file holds comes back as text.
	//! Only EXECUTING content was ever the risk (which is why the script sandbox
	//! denies io/require/load/loadfile/dofile), and reading is not executing - so
	//! no extension, kind or directory is privileged here.
	namespace DataResource
	{
		//! @brief the largest data resource a caller may obtain, in bytes.
		//! A bound on what one read hands back, so a mistaken name cannot turn
		//! into an unbounded string in a script. It bounds the RESULT, not the
		//! archive read itself (the reader materialises the entry before this
		//! sees it) - the honest limit of a cap at this layer.
		const std::size_t kMaxBytes = 8u * 1024u * 1024u;

		//! @brief the PURE name guard: is @p name a legal project-relative data
		//! resource name? Rejects an empty name, an ABSOLUTE path, a drive/UNC
		//! root and ANY ".." traversal segment - the containment decision is
		//! PathJail::isSafeRelativeEntry, the engine's one path-containment
		//! primitive, so a data read is jailed by exactly the rule a pak entry
		//! is. No filesystem access and no reader needed: headless-testable on
		//! its own.
		//! @param outError filled with an honest reason when the name is refused
		//! (untouched on success).
		bool checkName(String const & name, String & outError);

		//! @brief read the named data resource's text.
		//! @return false with @p outError set (and @p outText untouched) when
		//! the name is refused by checkName, when NO reader is installed, when
		//! the name resolves nowhere, or when the content exceeds kMaxBytes.
		//! @remarks there is deliberately NO `fopen` fallback here. The
		//! ResourceAccess contract lets a CORE LOADER fall back to its own file
		//! read when no reader is installed - correct for a loader in a headless
		//! test, WRONG for a script: content that is not mounted must stay
		//! unreadable rather than become a raw path into the machine. An
		//! uninstalled reader is an honest error, never a file read.
		bool read(String const & name, String & outText, String & outError);
	}

	/** @} */
}

#endif //__DataResource_h__3_8_2026__12_00_00__
