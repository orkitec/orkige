/********************************************************************
	created:	Thursday 2026/07/30 at 12:00
	filename: 	EditorBuildInfo.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorBuildInfo - the build IDENTITY of this editor binary: which source
// commit it was compiled from and when.
//
// The version number (ORKIGE_EDITOR_VERSION) says which engine generation this
// is; the identity says which BUILD. A distributed binary needs both - a bug
// report against "2.0.0" names thousands of trees, a report against
// "2.0.0 (a16c0227a, 2026-07-30)" names exactly one.
//
// The two values are compile definitions on THIS translation unit only
// (tools/editor/CMakeLists.txt): a stamp that changes every build must not
// invalidate the whole editor's object cache. An ordinary developer build
// leaves them unset and reports an honest "local build" instead of inventing a
// commit - the values come from the packaging pipeline, which knows the sha it
// checked out (Docs/nightly-builds.md).
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <string>

namespace Orkige
{
	//! @brief the source commit this binary was built from - the short sha the
	//! build was stamped with, or "" for an unstamped (local) build.
	char const* editorBuildCommit();

	//! @brief the date this binary was built (ISO "YYYY-MM-DD"), or "" for an
	//! unstamped (local) build.
	char const* editorBuildDate();

	//! @brief the one-line build identity every surface shows: the version plus
	//! the commit/date when stamped ("2.0.0 (a16c0227a, 2026-07-30)"), else
	//! "2.0.0 (local build)". Never fabricates a commit it was not given.
	std::string editorBuildIdentity();

	//! @brief the exact line `orkige_editor --version` prints (no trailing
	//! newline): "orkige_editor <identity> [<flavor>, <build type>]". Machine
	//! -readable enough for the packaging smoke test to grep the commit out of
	//! it, human-readable enough to paste into a bug report.
	std::string editorVersionLine();
}
