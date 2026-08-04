/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	PlayerTestRun.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "PlayerTestRun.h"

#include <core_project/Project.h>
#include <core_project/ProjectPaths.h>
#include <core_script/ScriptRuntime.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <core_script/ScriptTaskCore.h>
#include <core_script/ScriptTaskManager.h>
#include <core_script/ScriptTestReport.h>
#include <core_script/ScriptTestTools.h>
#include <engine_input/InputTestDrive.h>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	//! @brief the JSONL sink: one line appended and FLUSHED per record, so a
	//! run that crashes still names the test that was live (the file's last
	//! line). A run with no writable directory simply logs and reports nothing
	//! extra - an artifact is diagnostics, never a precondition.
	class TestReportFile
	{
	public:
		void open(Orkige::String const & path)
		{
			if(path.empty())
			{
				return;
			}
			std::error_code error;
			std::filesystem::create_directories(
				std::filesystem::path(path).parent_path(), error);
			this->mStream.open(path.c_str(),
				std::ios::out | std::ios::trunc);
			if(this->mStream.is_open())
			{
				this->mPath = path;
			}
		}
		void write(Orkige::String const & line)
		{
			if(!this->mStream.is_open())
			{
				return;
			}
			this->mStream << line << '\n';
			this->mStream.flush();
		}
		Orkige::String const & path() const { return this->mPath; }
	private:
		std::ofstream	mStream;
		Orkige::String	mPath;
	};

	//! ISO 8601 UTC now, plus the filesystem-safe stamp the file name carries
	void utcStamps(Orkige::String & outIso, Orkige::String & outFileStamp)
	{
		const std::time_t nowTime = std::time(nullptr);
		std::tm utcTm{};
#if defined(_WIN32)
		gmtime_s(&utcTm, &nowTime);
#else
		gmtime_r(&nowTime, &utcTm);
#endif
		char isoBuf[32] = { 0 };
		char stampBuf[32] = { 0 };
		std::strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
		std::strftime(stampBuf, sizeof(stampBuf), "%Y%m%dT%H%M%SZ", &utcTm);
		outIso = isoBuf;
		outFileStamp = stampBuf;
	}

	//! @brief the loose-file WALK behind discovery: every regular file under
	//! `<projectRoot>/tests/`, as project-relative forward-slash names. The
	//! DECISION about which of them are tests is the pure rule in
	//! ScriptTestTools - this only enumerates.
	Orkige::StringVector listTestDirectoryFiles(
		Orkige::String const & projectRoot)
	{
		Orkige::StringVector paths;
		const std::filesystem::path root(projectRoot);
		const std::filesystem::path testsDir =
			root / Orkige::ScriptTestTools::testsDirectoryName();
		std::error_code error;
		if(!std::filesystem::is_directory(testsDir, error))
		{
			return paths;
		}
		for(std::filesystem::recursive_directory_iterator
			it(testsDir, error), end; !error && it != end;
			it.increment(error))
		{
			// the ONE reserved-output policy, so a stray build tree under
			// tests/ is never walked (@see ProjectPaths)
			if(it->is_directory(error) &&
				Orkige::ProjectPaths::isReservedOutputDir(it->path()))
			{
				it.disable_recursion_pending();
				continue;
			}
			if(!it->is_regular_file(error))
			{
				continue;
			}
			paths.push_back(
				it->path().lexically_relative(root).generic_string());
		}
		return paths;
	}
}

namespace
{
	//! @brief run ONE play-mode test: a fresh world at its scene, the body
	//! started as a script task, then frames until it lands. The task's own
	//! tick budget is what makes a wedged wait a NAMED failure - this loop
	//! carries the same bound as a second belt, so a host whose pumpFrame
	//! stops advancing the world cannot hang the run either.
	void runPlayCase(Orkige::ScriptRuntime & runtime,
		Orkige::ScriptRuntime::ScriptTestSessionId session,
		Orkige::ScriptTestCase const & testCase,
		PlayerTestHooks const & hooks, Orkige::ScriptTestRecord & record)
	{
		record.name = testCase.name;
		if(!hooks.loadScene || !hooks.pumpFrame)
		{
			record.status = "error";
			record.message = "a play-mode test needs a frame-driven runner "
				"(this host advances no frames)";
			return;
		}
		// PER-TEST ISOLATION: the world is torn down whole and reloaded, for
		// every test, even two in a row on the same scene. A test starts from
		// the scene as authored or not at all.
		if(!hooks.loadScene(testCase.scene))
		{
			record.status = "error";
			record.message = "the scene '" + testCase.scene +
				"' could not be loaded";
			return;
		}
		const int tickLimit = Orkige::ScriptTaskCore::defaultTestTickLimit();
		Orkige::String error;
		const Orkige::ScriptTaskManager::TaskId task =
			runtime.startTestCase(session, testCase.index, tickLimit, &error);
		if(task == 0)
		{
			record.status = "error";
			record.message = error;
			return;
		}
		bool hostAlive = true;
		for(int frame = 0; frame <= tickLimit + 1; ++frame)
		{
			if(Orkige::ScriptTaskManager::getSingletonPtr() == 0 ||
				!Orkige::ScriptTaskManager::getSingleton().isActive(task))
			{
				break;
			}
			if(!hooks.pumpFrame())
			{
				hostAlive = false;
				break;
			}
		}
		Orkige::ScriptTaskStatus status = Orkige::ScriptTaskStatus::Unknown;
		Orkige::String taskError;
		if(Orkige::ScriptTaskManager::getSingletonPtr() != 0)
		{
			status = Orkige::ScriptTaskManager::getSingleton().statusOf(task);
			taskError = Orkige::ScriptTaskManager::getSingleton().errorOf(task);
			Orkige::ScriptTaskManager::getSingleton().cancel(task);
		}
		// the body records its own verdict when it RAN to the end; anything
		// else (a time-out, a cancel, a host that stopped) is the runner's to
		// report - and it is a failure, never a skip
		if(runtime.testCaseRecord(session, testCase.index, record) &&
			!record.status.empty())
		{
			record.name = testCase.name;
			return;
		}
		record.status = "error";
		if(!taskError.empty())
		{
			record.message = taskError;
		}
		else if(!hostAlive)
		{
			record.message = "the run ended before the test finished";
		}
		else if(status == Orkige::ScriptTaskStatus::Cancelled)
		{
			record.message = "the test's task was cancelled";
		}
		else
		{
			record.message = "the test finished without recording a verdict";
		}
	}
}
//---------------------------------------------------------
int runProjectLuaTests(Orkige::Project const & project,
	Orkige::String const & filter, Orkige::String const & fallbackReportDir,
	PlayerTestHooks const & hooks)
{
	if(!Orkige::ScriptRuntime::available())
	{
		// the honest refusal, not a green run: an ORKIGE_SCRIPTING=OFF build
		// has no interpreter, so it cannot answer the question that was asked
		SDL_Log("orkige_player: --run-tests needs a scripting backend - this "
			"build has none (ORKIGE_SCRIPTING=OFF)");
		return 1;
	}
	Orkige::ScriptRuntime & runtime = Orkige::ScriptRuntime::getSingleton();

	// THE ENGINE'S SCRIPT SURFACE, installed for the run. It is otherwise
	// installed lazily by the first ScriptComponent that loads, which would
	// make `world`, `save`, `http` and `store` reachable from a test only when
	// the project's scene happens to carry a script component - a test asking
	// about the store would silently see a nil table instead of a verdict. The
	// call is idempotent, so a scene that does load one changes nothing.
	Orkige::ScriptComponent::ensureScriptApi();

	// THE INPUT CAPABILITY, opened for exactly this run. The driver resolves a
	// target name and injects through InputManager - the one synthesis path
	// agent-driven input already uses - so a driven key is the same key the
	// platform would have delivered. It is installed here, not in the engine's
	// script surface, because only a test run should be able to press
	// anything; the sandbox binding in beginTestFile keeps it out of every
	// game script's reach even during this run.
	Orkige::InputTestDriver inputDriver;
	runtime.setTestInputHandler(
		[&inputDriver](Orkige::String const & verb,
			Orkige::String const & target, Orkige::String & outError)
	{
		if(verb == "press")
		{
			return inputDriver.press(target, outError);
		}
		if(verb == "release")
		{
			return inputDriver.release(target, outError);
		}
		outError = "unknown input verb '" + verb + "'";
		return false;
	});

	Orkige::StringVector duplicates;
	const std::vector<Orkige::ScriptTestFile> files =
		Orkige::ScriptTestTools::collectTestFiles(
			listTestDirectoryFiles(project.getRootDirectory()), &duplicates);
	for(Orkige::String const & duplicate : duplicates)
	{
		SDL_Log("orkige_player: two files derive the test name %s",
			duplicate.c_str());
	}

	Orkige::String iso;
	Orkige::String fileStamp;
	utcStamps(iso, fileStamp);

	Orkige::String reportDir = fallbackReportDir;
	if(char const * dirEnv = std::getenv("ORKIGE_TEST_REPORT_DIR"))
	{
		reportDir = dirEnv;
	}
	if(!reportDir.empty() && reportDir.back() != '/' &&
		reportDir.back() != '\\')
	{
		reportDir += '/';
	}
	TestReportFile report;
	if(!reportDir.empty())
	{
		report.open(reportDir + "tests-" + fileStamp + ".jsonl");
	}
	report.write(Orkige::ScriptTestReport::metaLine(project.getName(), iso,
		filter, static_cast<int>(files.size())));

	Orkige::ScriptTestSummary summary;
	summary.files = static_cast<int>(files.size());
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();

	// the record sink: one JSONL line, one log line on a refusal, one tally
	const auto recordOutcome =
		[&report, &summary](Orkige::ScriptTestRecord const & record)
	{
		report.write(Orkige::ScriptTestReport::testLine(record));
		++summary.total;
		if(record.status == "pass")
		{
			++summary.passed;
		}
		else if(record.status == "fail")
		{
			++summary.failed;
			SDL_Log("orkige_player:   FAIL %s :: %s\n    %s",
				record.file.c_str(), record.name.c_str(),
				record.message.c_str());
		}
		else
		{
			++summary.errors;
			SDL_Log("orkige_player:   ERROR %s :: %s\n    %s",
				record.file.c_str(), record.name.c_str(),
				record.message.c_str());
		}
	};

	// PASS 1: open every file (its chunk running IS its declaration pass) and
	// run, right there, every test that needs no scene. They are fast, they
	// cannot be disturbed by a world, and their verdicts all land before any
	// scene is loaded - so a suite whose logic is broken says so without ever
	// booting a level.
	struct OpenFile
	{
		Orkige::String								resourceName;
		Orkige::ScriptRuntime::ScriptTestSessionId	session;
	};
	std::vector<OpenFile> openFiles;
	std::vector<std::pair<std::size_t, Orkige::ScriptTestCase> > playCases;
	for(Orkige::ScriptTestFile const & file : files)
	{
		int declared = 0;
		Orkige::String error;
		Orkige::ScriptRuntime::ScriptTestSessionId session = 0;
		std::vector<Orkige::ScriptTestCase> cases;
		if(!runtime.beginTestFile(file.resourceName, filter, session, cases,
			&declared, &error))
		{
			// a whole file that cannot load is one honest ERROR record, never
			// a silent skip - the artifact must never imply a file passed
			Orkige::ScriptTestRecord record;
			record.file = file.resourceName;
			record.status = "error";
			record.message = error;
			recordOutcome(record);
			continue;
		}
		summary.filtered += declared - static_cast<int>(cases.size());
		OpenFile open;
		open.resourceName = file.resourceName;
		open.session = session;
		openFiles.push_back(open);
		const std::size_t fileIndex = openFiles.size() - 1;
		for(Orkige::ScriptTestCase const & testCase : cases)
		{
			if(!testCase.scene.empty())
			{
				playCases.push_back(std::make_pair(fileIndex, testCase));
				continue;
			}
			Orkige::ScriptTestRecord record;
			record.file = file.resourceName;
			record.name = testCase.name;
			Orkige::String caseError;
			if(!runtime.runTestCase(session, testCase.index, record,
				&caseError))
			{
				record.status = "error";
				record.message = caseError;
			}
			record.file = file.resourceName;
			record.name = testCase.name;
			// a finished test holds nothing: whatever it pressed and never
			// released comes up HERE, so one test can never press a key into
			// the next one (the same boundary the fresh world draws)
			inputDriver.releaseAll();
			recordOutcome(record);
		}
	}

	// PASS 2: the play-mode tests, GROUPED BY SCENE (stable, so a group keeps
	// its declaration order). Each still gets its own fresh world - the
	// grouping is about reading a run, not about sharing one.
	std::stable_sort(playCases.begin(), playCases.end(),
		[](std::pair<std::size_t, Orkige::ScriptTestCase> const & a,
			std::pair<std::size_t, Orkige::ScriptTestCase> const & b)
	{
		return a.second.scene < b.second.scene;
	});
	for(std::pair<std::size_t, Orkige::ScriptTestCase> const & entry : playCases)
	{
		OpenFile const & open = openFiles[entry.first];
		Orkige::ScriptTestRecord record;
		record.file = open.resourceName;
		runPlayCase(runtime, open.session, entry.second, hooks, record);
		record.file = open.resourceName;
		inputDriver.releaseAll();
		recordOutcome(record);
	}
	for(OpenFile const & open : openFiles)
	{
		runtime.endTestFile(open.session);
	}
	// the capability closes with the run: nothing that outlives this function
	// can press anything
	runtime.setTestInputHandler(Orkige::ScriptRuntime::TestInputHandler());

	summary.ms = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	report.write(Orkige::ScriptTestReport::summaryLine(summary));

	SDL_Log("orkige_player: tests - %s",
		Orkige::ScriptTestReport::summaryText(summary).c_str());
	if(!report.path().empty())
	{
		SDL_Log("orkige_player: test report '%s'", report.path().c_str());
	}
	if(files.empty())
	{
		SDL_Log("orkige_player: no '%s' files under '%s/%s'",
			Orkige::ScriptTestTools::testFileSuffix(),
			project.getRootDirectory().c_str(),
			Orkige::ScriptTestTools::testsDirectoryName());
	}
	return summary.exitCode();
}
