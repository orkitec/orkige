/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	EditorTestSession.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorTestSession.cpp - the impure half of running a project's Lua suite
// from the editor (@see EditorTestSession.h): the child process, the artifact
// tail and the frame-driven pump. Every DECISION it makes came from
// EditorProjectTests.h, which is pure and unit-tested headlessly.

#include "EditorTestSession.h"

#include "EditorApp.h"
#include "EditorResourcePaths.h"

#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptTestTools.h>

#include <SDL3/SDL_process.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_thread.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		using Orkige::String;

		//! how much of the runner's own output is worth keeping: enough to
		//! carry a stack tail or an abort message, bounded so a chatty suite
		//! cannot grow it without limit
		const std::size_t kMaxOutputTail = 16384;

		//! @brief the one live run. A session, not a queue: a second run would
		//! contend for the same report directory and leave two verdicts with
		//! no way to say which is current.
		struct TestSession
		{
			ProjectTestRunProgress	progress;
			ProjectTestReport		report;
			String					projectRoot;
			String					projectName;
			//! the per-run temp directory; each leg gets its own subdirectory
			//! so the artifact of the leg in flight is unambiguous
			String					reportRoot;
			String					legDirectory;
			String					artifactPath;	//!< resolved once it appears
			std::uintmax_t			artifactOffset = 0;
			String					artifactBuffer;	//!< the partial trailing line
			SDL_Process*			process = 0;
			String					outputBuffer;	//!< partial stdout line
			String					outputTail;
			String					runFailure;
		};

		TestSession & session()
		{
			static TestSession sSession;
			return sSession;
		}
		//---------------------------------------------------------
		void say(EditorConsole * console, ConsoleLevel level,
			String const & text)
		{
			if(console != 0)
			{
				console->addLine(level, "[tests] " + text);
			}
		}
		//---------------------------------------------------------
		//! @brief kill and reap the leg in flight. Safe to call with none.
		void endLegProcess(TestSession & live)
		{
			if(live.process == 0)
			{
				return;
			}
			int exitCode = 0;
			if(!SDL_WaitProcess(live.process, false, &exitCode))
			{
				SDL_KillProcess(live.process, true);
				SDL_WaitProcess(live.process, true, &exitCode);
			}
			SDL_DestroyProcess(live.process);
			live.process = 0;
		}
		//---------------------------------------------------------
		void removeReportRoot(TestSession & live)
		{
			if(live.reportRoot.empty())
			{
				return;
			}
			std::error_code ignored;
			std::filesystem::remove_all(live.reportRoot, ignored);
			live.reportRoot.clear();
		}
		//---------------------------------------------------------
		//! @brief spawn the runner for the leg @ref ProjectTestRunProgress has
		//! in flight. False (with @p outError set) when it could not start.
		bool startLeg(TestSession & live, String const & playerPath,
			String & outError)
		{
			const String filter = live.progress.currentFilter();
			live.legDirectory = (std::filesystem::path(live.reportRoot) /
				("leg-" + std::to_string(live.progress.leg())))
				.lexically_normal().string();
			std::error_code ignored;
			std::filesystem::create_directories(live.legDirectory, ignored);
			live.artifactPath.clear();
			live.artifactOffset = 0;
			live.artifactBuffer.clear();

			std::vector<String> arguments = { playerPath, "--project",
				live.projectRoot, "--run-tests" };
			if(!filter.empty())
			{
				arguments.push_back("--test-filter");
				arguments.push_back(filter);
			}
			std::vector<char const *> argv;
			argv.reserve(arguments.size() + 1);
			for(String const & argument : arguments)
			{
				argv.push_back(argument.c_str());
			}
			argv.push_back(0);

			SDL_Environment* environment = SDL_CreateEnvironment(true);
			SDL_SetEnvironmentVariable(environment, "ORKIGE_TEST_REPORT_DIR",
				live.legDirectory.c_str(), true);
			// the automation hooks aimed at an EDITOR run must not reach the
			// player: it honours the same variables and would exit after N
			// frames or overwrite the requested screenshot, cutting a suite
			// short and calling it a pass
			SDL_UnsetEnvironmentVariable(environment, "ORKIGE_DEMO_FRAMES");
			SDL_UnsetEnvironmentVariable(environment, "ORKIGE_DEMO_SCREENSHOT");

			SDL_PropertiesID properties = SDL_CreateProperties();
			SDL_SetPointerProperty(properties,
				SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
				const_cast<char**>(argv.data()));
			SDL_SetPointerProperty(properties,
				SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
			// piped, not inherited: the editor has no console to write to on
			// every platform, and the Console panel is where a person looks
			SDL_SetNumberProperty(properties,
				SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
			SDL_SetBooleanProperty(properties,
				SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
			live.process = SDL_CreateProcessWithProperties(properties);
			SDL_DestroyProperties(properties);
			SDL_DestroyEnvironment(environment);
			if(live.process == 0)
			{
				outError = "could not start the player '" + playerPath + "': " +
					String(SDL_GetError());
				return false;
			}
			return true;
		}
		//---------------------------------------------------------
		//! @brief the artifact of the leg in flight, once the runner has
		//! opened one. The leg directory holds exactly one run, so the first
		//! `tests-*.jsonl` in it IS this leg's report - no timestamp
		//! comparison and no stale-file question.
		String findLegArtifact(String const & legDirectory)
		{
			std::error_code ignored;
			for(std::filesystem::directory_iterator it(legDirectory, ignored),
				end; it != end; it.increment(ignored))
			{
				const String name = it->path().filename().string();
				if(name.rfind("tests-", 0) == 0 &&
					it->path().extension() == ".jsonl")
				{
					return it->path().lexically_normal().string();
				}
			}
			return String();
		}
		//---------------------------------------------------------
		//! @brief read whatever the runner appended since the last look and
		//! fold the COMPLETE lines into the report. The writer flushes per
		//! record, so this is what makes a run readable while it runs.
		void tailArtifact(TestSession & live)
		{
			if(live.artifactPath.empty())
			{
				if(live.legDirectory.empty())
				{
					return;
				}
				live.artifactPath = findLegArtifact(live.legDirectory);
				if(live.artifactPath.empty())
				{
					return;		// the runner has not opened one yet
				}
			}
			std::error_code ignored;
			const std::uintmax_t size =
				std::filesystem::file_size(live.artifactPath, ignored);
			if(ignored || size <= live.artifactOffset)
			{
				return;
			}
			std::ifstream stream(live.artifactPath.c_str(),
				std::ios::in | std::ios::binary);
			if(!stream.is_open())
			{
				return;
			}
			stream.seekg(static_cast<std::streamoff>(live.artifactOffset));
			std::string chunk;
			chunk.resize(static_cast<std::size_t>(size - live.artifactOffset));
			stream.read(&chunk[0], static_cast<std::streamsize>(chunk.size()));
			const std::streamsize got = stream.gcount();
			chunk.resize(static_cast<std::size_t>(got < 0 ? 0 : got));
			live.artifactOffset += static_cast<std::uintmax_t>(chunk.size());
			live.artifactBuffer += chunk;
			consumeProjectTestLines(live.artifactBuffer, live.report);
		}
		//---------------------------------------------------------
		//! @brief drain the runner's stdout into the Console, one line each,
		//! and keep the tail for a run that dies without a verdict
		void pumpOutput(TestSession & live, EditorConsole * console,
			bool flushPartial)
		{
			SDL_IOStream* output = live.process != 0
				? SDL_GetProcessOutput(live.process) : 0;
			if(output != 0)
			{
				char chunk[4096];
				std::size_t bytesRead = 0;
				while((bytesRead = SDL_ReadIO(output, chunk, sizeof(chunk))) > 0)
				{
					live.outputBuffer.append(chunk, bytesRead);
				}
			}
			std::size_t start = 0;
			std::size_t newline = live.outputBuffer.find('\n');
			while(newline != String::npos)
			{
				String line = live.outputBuffer.substr(start, newline - start);
				if(!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}
				if(!line.empty())
				{
					const bool bad = line.find("FAIL") != String::npos ||
						line.find("ERROR") != String::npos;
					say(console, bad ? ConsoleLevel::Error : ConsoleLevel::Info,
						line);
					live.outputTail += line;
					live.outputTail += '\n';
					if(live.outputTail.size() > kMaxOutputTail)
					{
						live.outputTail.erase(0,
							live.outputTail.size() - kMaxOutputTail);
					}
				}
				start = newline + 1;
				newline = live.outputBuffer.find('\n', start);
			}
			live.outputBuffer.erase(0, start);
			if(flushPartial && !live.outputBuffer.empty())
			{
				say(console, ConsoleLevel::Info, live.outputBuffer);
				live.outputTail += live.outputBuffer;
				live.outputBuffer.clear();
			}
		}
	}
	//---------------------------------------------------------
	bool startProjectTestRun(ProjectTestRunPlan const & plan,
		String const & projectRoot, String const & projectName,
		EditorConsole * console, String & outError)
	{
		TestSession & live = session();
		const EditorResourcePath player = editorResources().player();
		// gather the facts, ask the ONE decision (@see EditorProjectTests.h):
		// this door and the headless `test` subcommand run in different phases
		// of the process, so they cannot share a code path - but they must not
		// hold two opinions about whether a project is testable
		ProjectTestPreflight facts;
		facts.projectName = projectName;
		facts.projectOpen = !projectRoot.empty();
		// the COMPILE-TIME fact, which is the only one available here:
		// `available()` also requires a BOOTED runtime, and the editor boots
		// none for game scripts
		facts.scriptingAvailable =
			std::strcmp(Orkige::ScriptRuntime::backendName(), "none") != 0;
		std::error_code ignored;
		if(facts.projectOpen)
		{
			facts.testsDirectory = (std::filesystem::path(projectRoot) /
				Orkige::ScriptTestTools::testsDirectoryName())
				.lexically_normal().string();
			facts.testsDirectoryExists =
				std::filesystem::is_directory(facts.testsDirectory, ignored);
		}
		facts.playerFound = player.found();
		facts.runInFlight = live.progress.running();
		facts.planEmpty = plan.empty();
		outError = projectTestRunRefusal(facts);
		if(!outError.empty())
		{
			return false;
		}
		endLegProcess(live);
		removeReportRoot(live);
		// A run that covers the WHOLE suite replaces what came before; a
		// filtered one updates its own rows and leaves the rest standing.
		// Wiping the passes to re-run one failure would leave a person
		// looking at a suite that appears to consist of its failures alone.
		// A verdict is keyed by test identity, so the merge is exact.
		if(projectTestPlanCoversAll(plan) || live.projectRoot != projectRoot)
		{
			live.report = ProjectTestReport();
		}
		live.outputBuffer.clear();
		live.outputTail.clear();
		live.runFailure.clear();
		live.projectRoot = projectRoot;
		live.projectName = projectName;
		// the artifact directory is per-EDITOR, not per-run: the main thread's
		// id is stable for this process and different in another, so two
		// editors open at once cannot read each other's reports, and this
		// editor's own leftovers are cleared rather than accumulated. It lives
		// in the temp directory because a run artifact is diagnostics - never
		// something to write into the user's project.
		live.reportRoot = (std::filesystem::temp_directory_path(ignored) /
			("orkige_editor_tests_" +
				std::to_string(static_cast<unsigned long long>(
					SDL_GetCurrentThreadID()))))
			.lexically_normal().string();
		std::filesystem::remove_all(live.reportRoot, ignored);
		std::filesystem::create_directories(live.reportRoot, ignored);

		live.progress.begin(plan);
		String error;
		if(!startLeg(live, player.path, error))
		{
			live.progress.cancel();
			live.runFailure = error;
			outError = error;
			say(console, ConsoleLevel::Error, error);
			return false;
		}
		say(console, ConsoleLevel::Info, "running " + plan.label + " of '" +
			projectName + "' (" + std::to_string(plan.filters.size()) +
			" run(s))");
		return true;
	}
	//---------------------------------------------------------
	void tickProjectTestSession(EditorConsole * console)
	{
		TestSession & live = session();
		if(live.process == 0)
		{
			return;
		}
		pumpOutput(live, console, false);
		tailArtifact(live);
		int exitCode = 0;
		if(!SDL_WaitProcess(live.process, false, &exitCode))
		{
			return;		// still running
		}
		// the leg has exited: take everything it wrote before folding it in,
		// or the last records of a fast suite would be lost to the race
		pumpOutput(live, console, true);
		tailArtifact(live);
		SDL_DestroyProcess(live.process);
		live.process = 0;

		// A leg that wrote no `summary` line did not FINISH - it died. That is
		// a different thing from a suite whose tests failed, and the two must
		// never be reported as one: a crash that read as "0 passed" would be
		// the worst possible lie a test surface can tell.
		const bool died = !live.report.hasSummary;
		if(died)
		{
			live.runFailure = "the test runner exited (" +
				std::to_string(exitCode) + ") without finishing the run";
			if(!live.report.records.empty())
			{
				live.runFailure += " - the last test it reached was '" +
					live.report.records.back().name + "'";
			}
			say(console, ConsoleLevel::Error, live.runFailure);
			say(console, ConsoleLevel::Error, "the runner's own output is "
				"above; the results below are what it managed to report");
		}
		// each leg writes its OWN summary line, so only the LAST leg's may
		// stand as the run's; what actually spans a multi-leg run is the
		// tally over the records
		const bool moreLegs = live.progress.leg() < live.progress.legCount();
		if(moreLegs)
		{
			live.report.hasSummary = false;
		}
		live.progress.legFinished(exitCode);
		if(died)
		{
			// no further legs after a fall: a runner that cannot finish one
			// run will not finish the next, and a queue of crashes buries the
			// first and only useful message
			live.progress.cancel();
		}
		if(live.progress.running())
		{
			String error;
			const EditorResourcePath player = editorResources().player();
			if(!player.found() || !startLeg(live, player.path, error))
			{
				live.runFailure = error.empty()
					? String("the player disappeared mid-run") : error;
				live.progress.cancel();
				say(console, ConsoleLevel::Error, live.runFailure);
			}
			return;
		}
		if(live.progress.state() == ProjectTestRunState::Finished)
		{
			const Orkige::ScriptTestSummary tally = live.report.tally();
			say(console, tally.exitCode() == 0 ? ConsoleLevel::Info
				: ConsoleLevel::Error,
				Orkige::ScriptTestReport::summaryText(tally));
		}
	}
	//---------------------------------------------------------
	void cancelProjectTestRun(EditorConsole * console)
	{
		TestSession & live = session();
		if(!live.progress.running() && live.process == 0)
		{
			return;
		}
		endLegProcess(live);
		live.progress.cancel();
		say(console, ConsoleLevel::Warning, "test run stopped");
	}
	//---------------------------------------------------------
	void projectTestSessionOnProjectChanged(String const & projectRoot)
	{
		TestSession & live = session();
		if(live.projectRoot == projectRoot)
		{
			return;
		}
		endLegProcess(live);
		live.progress.cancel();
		removeReportRoot(live);
		live.progress = ProjectTestRunProgress();
		live.report = ProjectTestReport();
		live.outputBuffer.clear();
		live.outputTail.clear();
		live.runFailure.clear();
		live.artifactPath.clear();
		live.artifactBuffer.clear();
		live.legDirectory.clear();
		live.projectRoot = projectRoot;
		live.projectName.clear();
	}
	//---------------------------------------------------------
	ProjectTestSessionState projectTestSessionState()
	{
		TestSession const & live = session();
		ProjectTestSessionState state;
		state.state = live.progress.state();
		state.label = live.progress.label();
		state.leg = live.progress.leg();
		state.legCount = live.progress.legCount();
		state.projectRoot = live.projectRoot;
		state.filter = live.progress.currentFilter();
		state.runFailure = live.runFailure;
		state.exitCode = live.progress.exitCode();
		return state;
	}
	//---------------------------------------------------------
	ProjectTestReport const & projectTestSessionReport()
	{
		return session().report;
	}
	//---------------------------------------------------------
	String projectTestSessionOutputTail()
	{
		return session().outputTail;
	}
	//---------------------------------------------------------
	void shutdownProjectTestSession()
	{
		TestSession & live = session();
		endLegProcess(live);
		removeReportRoot(live);
	}
	//---------------------------------------------------------
}
