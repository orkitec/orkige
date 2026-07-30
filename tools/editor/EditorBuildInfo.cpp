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

	std::string editorBuildIdentity()
	{
		std::string identity = ORKIGE_EDITOR_VERSION;
		const std::string commit = editorBuildCommit();
		const std::string date = editorBuildDate();
		if (commit.empty() && date.empty())
		{
			return identity + " (local build)";
		}
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
}
