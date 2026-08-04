/********************************************************************
	created:	Monday 2026/08/03 at 16:00
	filename: 	EditorCliRun.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// The IMPURE half of the editor's command line: carry out a decision
// EditorCli.cpp already made (@see EditorCli.h). Runs entirely BEFORE the
// window, the render backend and the engine exist - the whole point is that a
// machine with no display can package a game.

#include "EditorApp.h"
#include "EditorBuildInfo.h"
#include "EditorCli.h"
#include "EditorPayloadFetcher.h"
#include "EditorProjectTests.h"	// the ONE "can this project be tested" decision
#include "EditorResourcePaths.h"

#include <ExportProject.h>	// the manifest facts an export packages from

#include <core_http/HttpClient.h>
#include <core_script/ScriptRuntime.h>	// does THIS build carry an interpreter
#include <core_script/ScriptTestTools.h>	// the ONE tests/ + suffix vocabulary

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include "engine_runtime/AppHost.h"

namespace
{
	using Orkige::String;

	//! every human line carries the program's name, like the exporter's, so a
	//! script greps one prefix whichever door it came through
	void say(String const & message)
	{
		std::printf("orkige_editor: %s\n", message.c_str());
		std::fflush(stdout);
	}
	//---------------------------------------------------------
	int fail(String const & message)
	{
		std::printf("orkige_editor: ERROR: %s\n", message.c_str());
		std::fflush(stdout);
		return 1;
	}
	//---------------------------------------------------------
	//! `export`: the SAME plan-then-run pair the Build menu and the MCP verb
	//! use. Nothing is spawned and nothing is re-implemented - what this door
	//! adds is that it needs no window to walk through.
	int runExportCommand(OrkigeEditor::EditorCliCommand const & command)
	{
		// the manifest facts, read straight off disk. A live `Orkige::Project`
		// is deliberately NOT loaded: it would seize the process-wide active
		// AssetDatabase (@see ExportProject.h), and an export needs four facts,
		// not a world.
		OrkigeExport::ExportProject project;
		String error;
		if(!OrkigeExport::ExportProject::readManifest(command.projectPath,
			project, &error))
		{
			return fail(error);
		}
		const OrkigeEditor::EditorExportPlan plan =
			planExport(project, command.platform);
		if(!plan.ok)
		{
			// the plan's refusal is already the complete, actionable sentence -
			// the same one the menu shows on hover and the endpoint returns
			return fail(plan.error);
		}
		say("packaging '" + project.name + "' for " + plan.platform +
			" (engine: " + plan.enginePayload + ")");
		PlannedExportOverrides overrides;
		overrides.outputDirectory = command.outputDirectory;
		overrides.credentials = command.credentials;
		String artifact;
		if(!runPlannedExport(plan, project,
			[](std::string const & line) { say(line); },
			artifact, error, &overrides))
		{
			return fail(error);
		}
		// the machine-readable contract, spelled exactly like the exporter's so
		// a caller keys on one grep whichever door produced the artifact
		std::printf("orkige_editor: OK %s\n", artifact.c_str());
		std::fflush(stdout);
		return 0;
	}
	//---------------------------------------------------------
	//! @brief a run artifact, identified by BOTH its path and when it was
	//! written - which is what lets a reused report directory be told apart
	//! from a fresh one (@see newestRunArtifact)
	struct RunArtifact
	{
		String							path;
		std::filesystem::file_time_type	written{};

		bool operator==(RunArtifact const & other) const
		{
			return this->path == other.path && this->written == other.written;
		}
	};
	//---------------------------------------------------------
	//! @brief the newest `tests-*.jsonl` in @p directory, empty when there is
	//! none.
	//!
	//! The file name carries a UTC stamp in `%Y%m%dT%H%M%SZ` form, which sorts
	//! chronologically as plain text - so "greatest name" IS "most recent",
	//! with no clock read and no directory-order assumption. Sampled once
	//! before the run and once after, so a directory a caller REUSES cannot
	//! hand back a previous run's file as this one's: a run that died before
	//! writing anything leaves the two samples identical, and this door then
	//! names no artifact rather than a stale one.
	RunArtifact newestRunArtifact(String const & directory)
	{
		std::error_code ignored;
		RunArtifact newest;
		String newestName;
		for(std::filesystem::directory_iterator it(directory, ignored), end;
			it != end; it.increment(ignored))
		{
			const String name = it->path().filename().string();
			if(name.rfind("tests-", 0) != 0 ||
				it->path().extension() != ".jsonl" || name <= newestName)
			{
				continue;
			}
			newestName = name;
			newest.path = it->path().lexically_normal().string();
			newest.written = std::filesystem::last_write_time(it->path(),
				ignored);
		}
		return newest;
	}
	//---------------------------------------------------------
	//! @brief `test`: run the project's Lua suite in this installation's
	//! player and hand back its exit code.
	//!
	//! @remarks THE ONE SUBCOMMAND THAT RUNS ANOTHER PROCESS, and it is not
	//! the thing the export rule forbids. Spawning a second EXPORTER would
	//! duplicate a decision; spawning the player duplicates nothing, because
	//! the editor holds no runner to duplicate. A test that declares a scene
	//! runs in a live world - physics stepping, scripts updating, frames
	//! advancing - and the player is the only part of an installation that
	//! has one. What this door contributes is the resolution only THIS
	//! installation can do: which player it has (the copy inside the app for a
	//! distributed editor, the build tree's binary for a developer one).
	//!
	//! Standard streams are INHERITED rather than piped, so the runner's
	//! output reaches the caller as it happens. A suite with play-mode tests
	//! takes real seconds per case, and a run that wedges must show which test
	//! it was on - not sit silent until it is killed.
	int runTestCommand(OrkigeEditor::EditorCliCommand const & command)
	{
		// the same manifest read `export` does, for the same reason: four
		// facts off disk, never a live Project (@see ExportProject.h)
		OrkigeExport::ExportProject project;
		String error;
		if(!OrkigeExport::ExportProject::readManifest(command.projectPath,
			project, &error))
		{
			return fail(error);
		}
		// a suite is a SHAPE the project either has or does not, and that is
		// answerable here without booting an engine to be told nothing ran.
		// Where the line falls matters: this asks only whether the directory
		// exists. What counts as a test inside it, and what an empty one is
		// worth, stay the runner's to decide.
		//
		// The VERDICT on those facts is not made here. This door and the
		// editor's Tests panel / MCP session run in different phases of one
		// process and cannot share a code path, so they share the DECISION
		// instead (@see EditorProjectTests.h) - a project that cannot be
		// tested is refused in the same words whichever door asked.
		const OrkigeEditor::EditorResourcePath player =
			OrkigeEditor::editorResources().player();
		std::error_code ignored;
		OrkigeEditor::ProjectTestPreflight facts;
		facts.projectName = project.name;
		facts.projectOpen = true;	// the manifest above read
		facts.testsDirectory = (std::filesystem::path(project.root) /
			Orkige::ScriptTestTools::testsDirectoryName())
			.lexically_normal().string();
		facts.testsDirectoryExists =
			std::filesystem::is_directory(facts.testsDirectory, ignored);
		// the COMPILE-TIME fact, which is the only one available this early:
		// `available()` also requires the runtime to be BOOTED, and nothing
		// here has booted one. The player asks the identical question before
		// its own boot, and both binaries come out of one build.
		facts.scriptingAvailable =
			std::strcmp(Orkige::ScriptRuntime::backendName(), "none") != 0;
		facts.playerFound = player.found();
		const String refusal = OrkigeEditor::projectTestRunRefusal(facts);
		if(!refusal.empty())
		{
			return fail(refusal);
		}
		std::vector<String> arguments = { player.path, "--project",
			project.root, "--run-tests" };
		if(!command.testFilter.empty())
		{
			arguments.push_back("--test-filter");
			arguments.push_back(command.testFilter);
		}
		std::vector<char const *> argv;
		argv.reserve(arguments.size() + 1);
		for(String const & argument : arguments)
		{
			argv.push_back(argument.c_str());
		}
		argv.push_back(nullptr);

		SDL_Environment* environment = SDL_CreateEnvironment(true);
		String reportDirectory;
		if(!command.reportDirectory.empty())
		{
			// --report-dir rides the runner's OWN artifact seam. There is one
			// report format and one writer; this only says where it lands.
			std::filesystem::create_directories(command.reportDirectory,
				ignored);
			reportDirectory = std::filesystem::absolute(
				command.reportDirectory, ignored).lexically_normal().string();
			SDL_SetEnvironmentVariable(environment, "ORKIGE_TEST_REPORT_DIR",
				reportDirectory.c_str(), true);
		}
		// the automation hooks aimed at an EDITOR run must not reach the
		// player: it honours the same variables and would exit after N frames
		// or overwrite the requested screenshot, cutting a suite short and
		// calling it a pass
		SDL_UnsetEnvironmentVariable(environment, "ORKIGE_DEMO_FRAMES");
		SDL_UnsetEnvironmentVariable(environment, "ORKIGE_DEMO_SCREENSHOT");

		const RunArtifact before = reportDirectory.empty()
			? RunArtifact() : newestRunArtifact(reportDirectory);

		say("running the tests of '" + project.name + "' (player: " +
			player.path + ")");
		SDL_PropertiesID properties = SDL_CreateProperties();
		SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
			const_cast<char**>(argv.data()));
		SDL_SetPointerProperty(properties,
			SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
		SDL_Process* process = SDL_CreateProcessWithProperties(properties);
		SDL_DestroyProperties(properties);
		SDL_DestroyEnvironment(environment);
		if(process == 0)
		{
			return fail("could not start the player '" + player.path + "': " +
				SDL_GetError());
		}
		int exitCode = 1;
		const bool exited = SDL_WaitProcess(process, true, &exitCode);
		SDL_DestroyProcess(process);
		if(!exited)
		{
			return fail("the test run could not be waited on: " +
				String(SDL_GetError()));
		}
		const RunArtifact after = reportDirectory.empty()
			? RunArtifact() : newestRunArtifact(reportDirectory);
		// only THIS run's report is worth naming: a run that died before
		// opening one leaves the directory exactly as it found it
		const String artifact = (after == before) ? String("") : after.path;
		if(exitCode == Orkige::AppHost::NO_DISPLAY_EXIT_CODE)
		{
			// the runner could not open a window at all: this login session
			// owns no screen (on macOS, fast user switching). No suite ran,
			// so there is no verdict to report - relay the SKIP rather than
			// call a run that never happened a failure.
			std::printf("orkige_editor: the test runner reports no display "
				"session, so no suite was run\n");
			return Orkige::AppHost::NO_DISPLAY_EXIT_CODE;
		}
		if(exitCode != 0)
		{
			// the SUITE's verdict, relayed - not re-derived. The runner has
			// already named every refusal on the way past.
			return fail("the test suite of '" + project.name + "' did not "
				"pass (the run exited " + std::to_string(exitCode) + ")" +
				(artifact.empty() ? String("") : " - see '" + artifact + "'"));
		}
		// the machine-readable last line every subcommand ends on. A run's
		// artifact is its JSONL report, so that is what OK names when one was
		// asked for; with no --report-dir the runner wrote it somewhere only
		// it knows, and saying so beats naming a path this process guessed.
		const String okPayload = artifact.empty()
			? ("the test suite of '" + project.name + "' passed") : artifact;
		std::printf("orkige_editor: OK %s\n", okPayload.c_str());
		std::fflush(stdout);
		return 0;
	}
	//---------------------------------------------------------
	//! `fetch-payload --list`: what this installation can install, and what it
	//! already has
	int listPayloads(OrkigeEditor::EditorPayloadFetcher const & fetcher)
	{
		const std::vector<OrkigeEditor::FetchablePayload> payloads =
			OrkigeEditor::fetchablePayloads();
		for(OrkigeEditor::FetchablePayload const & payload : payloads)
		{
			const String installed = fetcher.installedPath(payload.id);
			say(payload.id + " - " + payload.label + " - " +
				(installed.empty() ? String("not installed") : installed));
		}
		return 0;
	}
	//---------------------------------------------------------
	//! `fetch-payload <id>`: the download a released editor performs from its
	//! settings window, driven to completion here instead of over frames.
	//!
	//! @remarks The fetcher's `automatedRun` veto exists so a SCRIPTED EDITOR
	//! RUN is byte-identical to one on a machine with no network. This
	//! invocation is the opposite of incidental - a person or a build script
	//! named the download by id on the command line - so the fetcher is
	//! configured with the veto off. Nothing else about the run changes: no
	//! settings are read or written, and the install lands where a fetched
	//! payload always lands.
	int runFetchPayloadCommand(OrkigeEditor::EditorCliCommand const & command)
	{
		OrkigeEditor::EditorPayloadFetcher::Config config;
		config.version = Orkige::editorBuildVersion();
		config.flavor = ORKIGE_EDITOR_RENDER_BACKEND;
		config.rootDirectory =
			OrkigeEditor::editorWritableStateDirectory() + "payloads";
		OrkigeEditor::EditorPayloadFetcher fetcher(config);
		fetcher.setProcessRunner(
			[](std::vector<std::string> const & argv, std::string & output,
				int & exitCode)
			{
				return runProcessCaptured(argv, output, exitCode);
			});
		if(command.listPayloads)
		{
			return listPayloads(fetcher);
		}
		OrkigeEditor::FetchablePayload payload;
		if(!OrkigeEditor::findFetchablePayload(command.payloadId, payload))
		{
			return fail("'" + command.payloadId + "' is not a payload this "
				"build knows - run 'fetch-payload --list' to see them");
		}
		Orkige::HttpClient http;
		http.setUserAgent("orkige-editor/" +
			String(Orkige::editorBuildIdentity()));
		fetcher.setHttpClient(&http);
		String reason;
		if(!fetcher.canFetch(reason))
		{
			return fail(reason);
		}
		fetcher.beginFetch(command.payloadId);
		OrkigeEditor::PayloadFetchStage stage =
			OrkigeEditor::PayloadFetchStage::Idle;
		String lastMessage;
		// the same two pumps the frame loop runs, here in a plain loop: this
		// process has no frames to hang off
		for(;;)
		{
			http.update();
			fetcher.update();
			const OrkigeEditor::PayloadFetchStatus status = fetcher.status();
			if(status.stage != stage && !status.message.empty() &&
				status.message != lastMessage)
			{
				say(status.message);
				lastMessage = status.message;
			}
			stage = status.stage;
			if(stage == OrkigeEditor::PayloadFetchStage::Installed ||
				stage == OrkigeEditor::PayloadFetchStage::Failed)
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if(stage != OrkigeEditor::PayloadFetchStage::Installed)
		{
			return fail(fetcher.status().message);
		}
		std::printf("orkige_editor: OK %s\n",
			fetcher.installedPath(command.payloadId).c_str());
		std::fflush(stdout);
		return 0;
	}
}

//---------------------------------------------------------
namespace OrkigeEditor
{
	int runEditorCli(EditorCliCommand const & command)
	{
		if(command.usageError)
		{
			std::fprintf(stderr, "orkige_editor: %s\n", command.error.c_str());
			std::fprintf(stderr, "%s", editorCliUsage().c_str());
			std::fflush(stderr);
			return editorCliUsageExitCode();
		}
		switch(command.verb)
		{
		case EditorCliVerb::Help:
			std::printf("%s", editorCliUsage().c_str());
			std::fflush(stdout);
			return 0;
		case EditorCliVerb::Version:
			std::printf("%s\n", Orkige::editorVersionLine().c_str());
			std::fflush(stdout);
			return 0;
		case EditorCliVerb::Changelog:
		{
			const std::string & changelog = Orkige::editorBuildChangelog();
			std::printf("%s\n", changelog.empty()
				? Orkige::editorNoChangelogNote() : changelog.c_str());
			std::fflush(stdout);
			return 0;
		}
		case EditorCliVerb::Export:
			return runExportCommand(command);
		case EditorCliVerb::Test:
			return runTestCommand(command);
		case EditorCliVerb::FetchPayload:
			return runFetchPayloadCommand(command);
		case EditorCliVerb::None:
			break;
		}
		// unreachable: main() only calls this for a headless command
		return 0;
	}
}
