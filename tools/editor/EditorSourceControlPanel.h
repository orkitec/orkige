/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorSourceControlPanel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorSourceControlPanel_h__27_7_2026__12_00_00__
#define __EditorSourceControlPanel_h__27_7_2026__12_00_00__

//! @file EditorSourceControlPanel.h
//! @brief the "Source Control" dockable panel + its shared git-status service.
//! The service owns ONE cached status snapshot (the EditorGit seam) that both the
//! panel AND the Asset browser read - never two git invocations for the same
//! data - refreshed on the documented cadence (panel focus / a refresh button /
//! after every panel-issued operation). Operations run on a worker thread so a
//! commit or push never freezes the frame; results marshal back on sourceControlTick().
//!
//! Deliberately NO MCP verbs for stage/commit/push: agents are forbidden from
//! committing, and an MCP tool would launder that prohibition. @see Docs/editor.md.

#include "EditorGit.h"

#include <string>

namespace OrkigeEditor
{
	//! @brief advance the async git service ONE frame: marshal a finished worker
	//! (apply its result, refresh the shared snapshot, reload a discarded open
	//! document) and start a pending job. Called from the editor loop so badges +
	//! ops progress even while the panel tab is not the visible one. A no-op
	//! during automated runs (no live git subprocess) unless a selfcheck seam
	//! explicitly drove the service.
	void sourceControlTick();

	//! @brief the open project changed (open / close / switch): drop the cached
	//! repo + snapshot and, for a real project, request a first status refresh so
	//! the Asset browser shows badges without the panel ever being opened.
	//! `projectRoot` empty = no project (the service goes idle).
	void sourceControlOnProjectChanged(std::string const& projectRoot);

	//! @brief point the service at `projectRoot` ONLY if it is not already
	//! tracking it (idempotent - safe to call every frame from the Asset browser /
	//! the panel so badges follow a project switch even with the panel closed).
	void sourceControlEnsureProject(std::string const& projectRoot);

	//! @brief the ONE badge snapshot the Asset browser reads (repo-relative file
	//! badges + aggregated dirty folders + the project's in-repo prefix). Inactive
	//! (no badges) when the project is not in a git repo, git is absent, a refresh
	//! has not completed yet, or the run is automated.
	GitBadgeSnapshot const& sourceControlBadgeSnapshot();

	//! @brief the editor's merged-stream git runner (stdout+stderr captured so a
	//! commit-msg hook rejection / push auth error surfaces). Exposed for the
	//! selfcheck to drive a GitRepo against its throwaway temp repo.
	GitRunner sourceControlGitRunner();
}

#endif //__EditorSourceControlPanel_h__27_7_2026__12_00_00__
