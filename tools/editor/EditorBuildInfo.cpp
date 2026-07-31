/********************************************************************
	created:	Thursday 2026/07/30 at 12:00
	filename: 	EditorBuildInfo.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// @see EditorBuildInfo.h - the build-identity strings. This is the ONE
// translation unit carrying the ORKIGE_BUILD_COMMIT / ORKIGE_BUILD_DATE
// compile definitions, so a re-stamp recompiles one small file instead of the
// whole editor.
#include "EditorBuildInfo.h"

#include "EditorResourcePaths.h"	// the ONE bundle-first resource locator

#include <core_util/VersionOrder.h>

#include <fstream>
#include <sstream>
#include <string>

// unset in an ordinary developer build: the identity then reads "local build"
// rather than claiming a commit nobody passed in
#ifndef ORKIGE_BUILD_COMMIT
#define ORKIGE_BUILD_COMMIT ""
#endif
#ifndef ORKIGE_BUILD_DATE
#define ORKIGE_BUILD_DATE ""
#endif

namespace Orkige
{
	char const* editorBuildCommit()
	{
		return ORKIGE_BUILD_COMMIT;
	}

	char const* editorBuildDate()
	{
		return ORKIGE_BUILD_DATE;
	}

	std::string editorBuildVersion()
	{
		// ONE composition rule, in ONE place: the packaging tooling derives the
		// archive name, the VERSION file and the published manifest from the
		// same grammar and from the same two stamped values, and the nightly's
		// smoke test matches this binary's line against the packaged version -
		// so the two implementations of the grammar cannot drift unnoticed.
		return VersionOrder::compose(ORKIGE_EDITOR_VERSION, editorBuildDate(),
			editorBuildCommit());
	}

	std::string editorBuildIdentity()
	{
		std::string identity = ORKIGE_EDITOR_VERSION;
		const std::string commit = editorBuildCommit();
		const std::string date = editorBuildDate();
		if (commit.empty() && date.empty())
		{
			return identity + " (local build)";
		}
		const std::string ordered = editorBuildVersion();
		if (!ordered.empty())
		{
			// the ordered version already spells out the date and the commit
			return ordered;
		}
		// a partial stamp composes no ordered version (a commit with no date):
		// report exactly what was given rather than inventing the rest
		identity += " (";
		identity += commit.empty() ? "unknown commit" : commit;
		if (!date.empty())
		{
			identity += ", ";
			identity += date;
		}
		identity += ")";
		return identity;
	}

	std::string editorVersionLine()
	{
		return "orkige_editor " + editorBuildIdentity() +
			" [" ORKIGE_EDITOR_RENDER_BACKEND ", " ORKIGE_EDITOR_BUILD_TYPE "]";
	}

	char const* editorNoChangelogNote()
	{
		return "This build carries no changelog - it was built from a source "
			"tree rather than packaged from a release.";
	}

	std::string const & editorBuildChangelog()
	{
		// read ONCE, on the first ask: the About box draws every frame while it
		// is open, and a file read per frame would be a disk hit per frame.
		// A build with no packaged changelog caches the empty answer just as
		// firmly, so the miss is not re-probed either.
		static const std::string changelog = []()
		{
			const OrkigeEditor::EditorResourcePath path =
				OrkigeEditor::editorResources().changelog();
			if (!path.found())
			{
				return std::string();
			}
			std::ifstream file(path.path.c_str(), std::ios::binary);
			if (!file)
			{
				return std::string();
			}
			std::ostringstream text;
			text << file.rdbuf();
			return text.str();
		}();
		return changelog;
	}
}
