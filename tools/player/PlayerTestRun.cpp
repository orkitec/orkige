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
#include <core_script/ScriptTestReport.h>
#include <core_script/ScriptTestTools.h>

#include <SDL3/SDL_log.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
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

//---------------------------------------------------------
int runProjectLuaTests(Orkige::Project const & project,
	Orkige::String const & filter, Orkige::String const & fallbackReportDir)
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

	for(Orkige::ScriptTestFile const & file : files)
	{
		std::vector<Orkige::ScriptTestRecord> records;
		int declared = 0;
		Orkige::String error;
		if(!runtime.runTestFile(file.resourceName, filter, records, &declared,
			&error))
		{
			// a whole file that cannot load is one honest ERROR record, never
			// a silent skip - the artifact must never imply a file passed
			Orkige::ScriptTestRecord record;
			record.file = file.resourceName;
			record.status = "error";
			record.message = error;
			report.write(Orkige::ScriptTestReport::testLine(record));
			SDL_Log("orkige_player:   ERROR %s: %s",
				file.resourceName.c_str(), error.c_str());
			++summary.errors;
			++summary.total;
			continue;
		}
		summary.filtered += declared - static_cast<int>(records.size());
		for(Orkige::ScriptTestRecord const & record : records)
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
		}
	}
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
