/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	EditorTestSession.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorTestSession_h__4_8_2026__12_00_00__
#define __EditorTestSession_h__4_8_2026__12_00_00__

#include "EditorProjectTests.h"

#include <core_util/String.h>

struct EditorConsole;

//! @file EditorTestSession.h
//! @brief the ONE seam that runs a project's Lua suite from inside the editor.
//!
//! @par One runner, three doors
//! There is a single place the editor starts a test run, and the Tests panel,
//! the MCP verbs (`run_project_tests` / `get_project_test_results`) and any
//! future caller all go through it. Two doors that each spawned a player would
//! be two things that could disagree about what "the run" is, and a person
//! watching the panel while an agent polls the endpoint must see one run, not
//! two.
//!
//! @par It drives the PLAYER, exactly as Play does
//! The editor never runs game scripts: it does not tick game objects, so
//! `ScriptComponent` is dormant in edit mode, and a test that declares a scene
//! needs a live world with frames advancing. The player is the part of an
//! installation that has one. So this spawns `orkige_player --project <p>
//! --run-tests [--test-filter <f>]` - the same runner the player's own CLI and
//! `orkige_editor test` reach - resolving WHICH player this installation has
//! through the same locator Play uses (@see EditorResourcePaths.h).
//!
//! @par Nothing blocks the UI
//! The run is a child process, polled non-blocking once per frame from the
//! editor's frame loop (@ref tickProjectTestSession), exactly like the
//! compile-on-Play build stream. Results appear as they are produced, because
//! the runner FLUSHES its artifact per record and this tails that file; the
//! player's own output is mirrored into the Console, so a run that dies has
//! left its reason where a person will look for it.
//!
//! @par What a run reports when the player never gets to a verdict
//! A crashed or killed runner writes no `summary` line. The report then still
//! carries every record that landed before the fall, and the session says so
//! plainly rather than presenting an incomplete artifact as a finished run -
//! `runFailure()` is non-empty exactly in that case.

namespace OrkigeEditor
{
	//! @brief what the panel and the endpoint read while a run is in flight.
	struct ProjectTestSessionState
	{
		ProjectTestRunState	state = ProjectTestRunState::Idle;
		Orkige::String		label;			//!< what this run covers
		std::size_t			leg = 0;		//!< 1-based leg in flight, 0 = none
		std::size_t			legCount = 0;
		Orkige::String		projectRoot;	//!< the project the run belongs to
		Orkige::String		filter;			//!< the leg's filter ("" = all)
		//! @brief non-empty when the run could not reach a verdict: the runner
		//! died, was killed, or never started. A finished run whose TESTS
		//! failed leaves this EMPTY - a failing suite is a result, not a
		//! malfunction, and conflating them would hide a crash behind a red
		//! test row.
		Orkige::String		runFailure;
		int					exitCode = 0;
	};

	//! @brief start a run. False (with @p outError set to one actionable
	//! sentence) when it cannot start: no project, no suite, no scripting
	//! backend in this build, no player beside this editor, or a run already
	//! in flight.
	//! @param projectRoot the open project's root directory
	//! @param projectName what to call it in messages
	bool startProjectTestRun(ProjectTestRunPlan const & plan,
		Orkige::String const & projectRoot, Orkige::String const & projectName,
		EditorConsole * console, Orkige::String & outError);

	//! @brief one frame of the run: drain the player's output into the
	//! Console, tail the artifact into the report, and start the next leg when
	//! the current one exits. Cheap and a no-op while nothing runs.
	void tickProjectTestSession(EditorConsole * console);

	//! @brief stop the run: the leg in flight is killed, the records already
	//! read stand, and the state becomes Cancelled.
	void cancelProjectTestRun(EditorConsole * console);

	//! @brief drop the run's project-scoped state (results included) because
	//! the open project changed. A run in flight is cancelled first - its
	//! results describe a project that is no longer open.
	void projectTestSessionOnProjectChanged(Orkige::String const & projectRoot);

	//! @brief the live state of the run
	ProjectTestSessionState projectTestSessionState();

	//! @brief the accumulated results. Live during a run, and the last run's
	//! results after one.
	ProjectTestReport const & projectTestSessionReport();

	//! @brief the tail of the runner's own output, kept for the case the
	//! artifact cannot explain: a crash leaves its reason here.
	Orkige::String projectTestSessionOutputTail();

	//! @brief release the session's process and temp directory at shutdown.
	void shutdownProjectTestSession();
}

#endif //__EditorTestSession_h__4_8_2026__12_00_00__
