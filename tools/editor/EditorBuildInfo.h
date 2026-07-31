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

	//! @brief the ORDERED identity of this build - the one string that answers
	//! "is that download newer than what I run":
	//! "2.0.0-nightly.20260730+dea551f9e" (@see core_util/VersionOrder.h,
	//! which composes it and compares two of them). "" for an unstamped build
	//! or a partial stamp that cannot compose one - a build with no ordered
	//! identity is honestly incomparable rather than ranked by guesswork.
	std::string editorBuildVersion();

	//! @brief the one-line build identity every surface shows: the ordered
	//! version when stamped ("2.0.0-nightly.20260730+dea551f9e", which carries
	//! the date and the commit inside it), the version plus whatever partial
	//! stamp exists otherwise, and "2.0.0 (local build)" for a developer
	//! build. Never fabricates a commit it was not given.
	std::string editorBuildIdentity();

	//! @brief the exact line `orkige_editor --version` prints (no trailing
	//! newline): "orkige_editor <identity> [<flavor>, <build type>]". Machine
	//! -readable enough for the packaging smoke test to grep the commit out of
	//! it, human-readable enough to paste into a bug report.
	std::string editorVersionLine();

	//! @brief the changelog THIS build shipped with - the CHANGELOG.md the
	//! packaging pipeline wrote beside the app, verbatim. "" for a build that
	//! carries none (every developer build), which the About box reports in
	//! one line rather than showing an empty box.
	//! @remarks Resolved through the ONE resource locator
	//! (EditorResourcePaths.h) and read ONCE: the reference stays valid for
	//! the process, so a UI that draws every frame never touches the disk.
	std::string const & editorBuildChangelog();

	//! @brief the one line a build with no packaged changelog says instead -
	//! the same sentence the About box shows and `--changelog` prints, so the
	//! two surfaces cannot drift.
	char const* editorNoChangelogNote();
}
