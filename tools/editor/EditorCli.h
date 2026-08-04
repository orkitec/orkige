/********************************************************************
	created:	Monday 2026/08/03 at 16:00
	filename: 	EditorCli.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorCli_h__3_8_2026__16_00_00__
#define __EditorCli_h__3_8_2026__16_00_00__

#include "EditorBuildSettings.h"

#include <core_util/String.h>

#include <vector>

//! @file EditorCli.h
//! @brief the editor's command-line front door: the same capabilities the
//! windowed editor has, reachable from a shell script.
//!
//! @par Why the editor carries a CLI at all
//! An installed Orkige is the editor application and the players it fetches -
//! and NOTHING else. `orkige_export` is a development-tree tool, so on a
//! machine that has only a distributed Orkige the export capability exists
//! solely inside this process. Without a command line, packaging a game there
//! means driving a windowed application over its MCP endpoint, which no build
//! server can reasonably do. These subcommands are that missing door: the same
//! in-process exporter the Build menu runs, the same engine-source resolution,
//! the same refusals, addressed by argv instead of by a menu item.
//!
//! @par Two entry points, ONE implementation
//! Nothing here duplicates `orkige_export`, and nothing here spawns it. The
//! export subcommand plans through @ref planExport and runs through @ref
//! runPlannedExport - the identical pair behind the Build menu and the MCP
//! `export_project` verb. What it adds is the one thing a standalone exporter
//! cannot know: which engine source THIS installation has (its own build tree,
//! the payload it carries inside itself, a fetched device player, an installed
//! SDK pack) and the three-tier refusal sentence when it has none.
//!
//! @par Running a game's tests is a RUNTIME question, so it goes to the runtime
//! @ref EditorCliVerb::Test is the one subcommand that runs another process,
//! and the distinction from the rule above is what makes that consistent. A
//! second exporter would be a second COPY OF A DECISION - two places that
//! could disagree about what a package contains. The player is not a copy of
//! anything the editor holds: it is the engine's runtime, the only thing in
//! the installation that owns a game world, and a test that declares a scene
//! needs one. The editor could not run those tests in process at any price
//! short of becoming a runtime itself. So this door supplies what only it
//! knows - WHICH player this installation has (@see EditorResourcePaths.h) -
//! and the runner's verdict travels back untouched as the exit code.
//!
//! @par The parse is pure
//! @ref parseEditorCli is a total function from argv to a decision, so the
//! whole surface - including the hazard below - is asserted headlessly
//! (EditorCliTests). Everything that touches a disk, a network or a project
//! lives in EditorCliRun.cpp, behind @ref runEditorCli.
//!
//! @par THE HAZARD this exists to close
//! An argument the editor does not understand used to be silently ignored, and
//! the editor would open a window. On a build server a typo (`exprot`) would
//! therefore launch a GUI application and hang the job until its timeout, with
//! nothing in the log to say why. So a first argument that is a WORD rather
//! than a flag must resolve to a known subcommand or be REFUSED with exit 2.
//! Flags keep their historical behaviour, because the windowed editor's own
//! options are flags and unknown ones must stay harmless there.
//!
//! @par A subcommand run is an automated run
//! A headless invocation sets the same `automatedRun` boolean a scripted test
//! does, rather than inventing a third mode: no view settings, no recents, no
//! imgui ini, no IDE lock, no MCP endpoint, no credential vault. The ONE
//! deliberate exception is stated at @ref EditorCliVerb::Export.
//!
//! @par Windows: the editor is a CONSOLE-subsystem executable
//! `add_executable(orkige_editor ...)` carries no `WIN32`, which is what makes
//! stdout, stderr and the exit code reach a caller on Windows. Flipping the
//! editor to the GUI subsystem would silently kill this whole surface - the
//! process would detach from the console and every subcommand would appear to
//! print nothing.

namespace OrkigeEditor
{
	//! what a headless invocation was asked to do
	enum class EditorCliVerb
	{
		//! no subcommand: launch the editor exactly as before
		None,
		//! `export` - package a project, in process, through the ONE export
		//! seam.
		//!
		//! @remarks THE ONE STATED DEVIATION from "an automated run never
		//! reads user state": this READS the machine-local per-project build
		//! settings (@see EditorBuildSettings.h) for the signing identity
		//! NAMES. Skipping them would make `orkige_editor export` produce a
		//! differently-signed artifact than Build > Export on the same
		//! machine, which is exactly the drift a shared front door exists to
		//! remove. It is a READ and nothing else - no user state is written -
		//! and passwords stay out of it entirely: the credential vault is not
		//! installed in an automated run, so a password comes from the
		//! environment, which is where a build server keeps one anyway.
		Export,
		//! `test` - run a project's own Lua suite (`<project>/tests/*.test.lua`)
		//! through this installation's player and report its exit code.
		//!
		//! @remarks The runner is the player's (`--run-tests`), because a test
		//! that declares a scene runs in a LIVE WORLD - physics stepping,
		//! scripts updating, frames advancing. The editor has no world to lend
		//! it before its render backend and window exist, and a headless door
		//! that could only run the worldless half of a suite would report a
		//! green verdict on an untested game.
		Test,
		//! `fetch-payload` - download and install a platform's player
		FetchPayload,
		//! `version` - the build identity (the `--version` flag's twin)
		Version,
		//! `changelog` - what this build shipped with
		Changelog,
		//! `help` - the usage text
		Help
	};

	//! @brief the decision @ref parseEditorCli reached
	struct EditorCliCommand
	{
		EditorCliVerb	verb = EditorCliVerb::None;
		//! the first argument exactly as typed - what a refusal quotes
		Orkige::String	subcommand;
		//! true when the arguments do not form a runnable command; @ref error
		//! then holds the one sentence and the process exits 2
		bool			usageError = false;
		Orkige::String	error;

		//--- export ------------------------------------------
		Orkige::String	projectPath;	//!< `--project`
		Orkige::String	platform;		//!< `--platform`
		Orkige::String	outputDirectory;//!< `--output` ("" = the project's builds/)
		//! credentials named on the command line; an empty field falls through
		//! to the machine store and then to the environment
		BuildCredentials	credentials;

		//--- test --------------------------------------------
		//! `--test-filter`: matched against `<file>::<test name>`, exactly as
		//! the player's own flag is ("" = run everything)
		Orkige::String	testFilter;
		//! `--report-dir`: where the run's JSONL artifact lands. Empty leaves
		//! the runner's own default (beside the breadcrumb trail in the
		//! writable app directory) alone - this door adds a way to ASK for the
		//! artifact somewhere a build server can collect it, never a second
		//! report format.
		Orkige::String	reportDirectory;

		//--- fetch-payload -----------------------------------
		Orkige::String	payloadId;		//!< which player to install
		bool			listPayloads = false;	//!< `--list`

		//! @brief does this invocation stay OFF the window road? True for every
		//! subcommand AND for a refusal - a mistyped subcommand must exit,
		//! never open a window on a machine that has no one looking at it.
		bool headless() const
		{
			return this->verb != EditorCliVerb::None || this->usageError;
		}
	};

	//! @brief the exit code a parsed command reports before it even runs: 2
	//! for a usage error, 0 otherwise. Named so the contract "usage is 2" has
	//! one definition.
	int editorCliUsageExitCode();

	//! @brief the argument vector WITHOUT argv[0] to a decision. Total and
	//! pure: it reads no environment, opens no file and never throws.
	EditorCliCommand parseEditorCli(std::vector<Orkige::String> const & arguments);

	//! @brief the usage text, honest about what this door covers.
	//! @remarks It deliberately promises no scene or asset operations IN THIS
	//! PROCESS. A `TransformComponent` stores its transform inside the render
	//! node, so loading a scene pulls in a render world - and the editor boots
	//! exactly one render backend, the graphics one, straight into a window.
	//! The engine does carry a deviceless render system (@see
	//! RenderSystemSelection.h), which is how the PLAYER holds a live scene
	//! with no display; teaching the editor to boot that way is its own piece
	//! of work and nothing here may advertise it as done. Where a live world
	//! is what the caller actually wants, the honest answer is the runtime
	//! that already has one - which is what @ref EditorCliVerb::Test does.
	Orkige::String editorCliUsage();

	//! @brief run a parsed command to completion. 0 = it worked, 1 = it ran
	//! and failed, 2 = the arguments were not usable. Defined in
	//! EditorCliRun.cpp (the editor executable), because everything it reaches
	//! - the resource locator's baked fallbacks, the export seam, the payload
	//! fetcher - lives there.
	int runEditorCli(EditorCliCommand const & command);
}

#endif //__EditorCli_h__3_8_2026__16_00_00__
