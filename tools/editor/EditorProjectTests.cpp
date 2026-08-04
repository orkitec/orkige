/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	EditorProjectTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorProjectTests.cpp - the pure decisions behind the Tests panel and the
// project-test MCP verbs (@see EditorProjectTests.h). No process, no clock, no
// UI; the one filesystem call is the directory WALK behind discovery, which is
// the same shape EditorScriptTools uses for *.editor.lua.

#include "EditorProjectTests.h"

#include <core_project/ProjectPaths.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace OrkigeEditor
{
	using Orkige::ScriptTestRecord;
	namespace ScriptTestReport = Orkige::ScriptTestReport;
	using Orkige::ScriptTestSummary;
	using Orkige::String;

	//---------------------------------------------------------
	String projectTestKey(ScriptTestRecord const & record)
	{
		return record.file + "::" + record.name;
	}
	//---------------------------------------------------------
	bool projectTestPassed(ScriptTestRecord const & record)
	{
		return record.status == "pass";
	}
	//---------------------------------------------------------
	ProjectTestLocation projectTestFailureLocation(String const & message)
	{
		ProjectTestLocation location;
		// the shape is "<chunk>:<line>: <text>": scan for the FIRST colon that
		// is followed by digits and then another colon. Taking the first (not
		// the last) match matters - a message may quote a second file:line in
		// its text, and the position that raised is the one at the front.
		for(std::size_t at = message.find(':'); at != String::npos;
			at = message.find(':', at + 1))
		{
			std::size_t digits = at + 1;
			while(digits < message.size() &&
				std::isdigit(static_cast<unsigned char>(message[digits])) != 0)
			{
				++digits;
			}
			if(digits == at + 1 || digits >= message.size() ||
				message[digits] != ':')
			{
				continue;	// not "<something>:<number>:"
			}
			const String file = message.substr(0, at);
			if(file.empty())
			{
				continue;
			}
			// accumulate the digits with a ceiling rather than converting: an
			// absurd number in a message must not overflow into a negative
			// line, and no source file has a billion lines
			int number = 0;
			for(std::size_t digit = at + 1; digit < digits && number < 1000000000;
				++digit)
			{
				number = number * 10 + (message[digit] - '0');
			}
			// a line number of 0 is not a position; leave the location empty
			// rather than send a reader to a line that cannot exist
			if(number <= 0)
			{
				continue;
			}
			location.file = file;
			location.line = number;
			return location;
		}
		return location;
	}
	//---------------------------------------------------------
	std::vector<ScriptTestRecord> ProjectTestReport::failures() const
	{
		std::vector<ScriptTestRecord> failed;
		for(ScriptTestRecord const & record : this->records)
		{
			if(!projectTestPassed(record))
			{
				failed.push_back(record);
			}
		}
		return failed;
	}
	//---------------------------------------------------------
	ScriptTestSummary ProjectTestReport::tally() const
	{
		ScriptTestSummary summary;
		std::vector<String> files;
		for(ScriptTestRecord const & record : this->records)
		{
			++summary.total;
			if(record.status == "pass")
			{
				++summary.passed;
			}
			else if(record.status == "fail")
			{
				++summary.failed;
			}
			else
			{
				++summary.errors;
			}
			summary.ms += record.ms;
			if(std::find(files.begin(), files.end(), record.file) ==
				files.end())
			{
				files.push_back(record.file);
			}
		}
		summary.files = static_cast<int>(files.size());
		return summary;
	}
	//---------------------------------------------------------
	ScriptTestReport::LineKind feedProjectTestLine(ProjectTestReport & report,
		String const & line)
	{
		ProjectTestMeta meta;
		ScriptTestRecord record;
		ScriptTestSummary summary;
		const ScriptTestReport::LineKind kind =
			ScriptTestReport::parseLine(line, meta, record, summary);
		switch(kind)
		{
		case ScriptTestReport::LineKind::Meta:
			// the FIRST meta line names the run; a later leg's meta line
			// carries that leg's own filter and would otherwise overwrite the
			// identity of the run as a whole
			if(!report.hasMeta)
			{
				report.meta = meta;
				report.hasMeta = true;
			}
			break;
		case ScriptTestReport::LineKind::Test:
		{
			const String key = projectTestKey(record);
			bool replaced = false;
			for(ScriptTestRecord & existing : report.records)
			{
				if(projectTestKey(existing) == key)
				{
					existing = record;
					replaced = true;
					break;
				}
			}
			if(!replaced)
			{
				report.records.push_back(record);
			}
			break;
		}
		case ScriptTestReport::LineKind::Summary:
			report.summary = summary;
			report.hasSummary = true;
			break;
		case ScriptTestReport::LineKind::None:
			break;
		}
		return kind;
	}
	//---------------------------------------------------------
	std::size_t consumeProjectTestLines(String & buffer,
		ProjectTestReport & report)
	{
		std::size_t consumed = 0;
		std::size_t start = 0;
		std::size_t newline = buffer.find('\n');
		while(newline != String::npos)
		{
			String line = buffer.substr(start, newline - start);
			// tolerate a CRLF artifact: the writer emits '\n', but a report
			// that travelled through a text-mode copy would carry '\r'
			if(!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			feedProjectTestLine(report, line);
			++consumed;
			start = newline + 1;
			newline = buffer.find('\n', start);
		}
		buffer.erase(0, start);
		return consumed;
	}
	//---------------------------------------------------------
	std::vector<String> reduceProjectTestFilters(
		std::vector<String> const & filters)
	{
		std::vector<String> reduced;
		for(String const & candidate : filters)
		{
			bool covered = false;
			for(String const & kept : reduced)
			{
				// a kept filter that is a SUBSTRING of the candidate already
				// selects everything the candidate would
				if(candidate.find(kept) != String::npos)
				{
					covered = true;
					break;
				}
			}
			if(covered)
			{
				continue;
			}
			// drop what the candidate now covers, so order of arrival cannot
			// leave a redundant leg behind
			reduced.erase(std::remove_if(reduced.begin(), reduced.end(),
				[&candidate](String const & kept)
				{
					return kept.find(candidate) != String::npos;
				}), reduced.end());
			reduced.push_back(candidate);
		}
		return reduced;
	}
	//---------------------------------------------------------
	bool projectTestPlanCoversAll(ProjectTestRunPlan const & plan)
	{
		for(String const & filter : plan.filters)
		{
			if(filter.empty())
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	ProjectTestRunPlan planAllProjectTests()
	{
		ProjectTestRunPlan plan;
		plan.filters.push_back("");
		plan.label = "all tests";
		return plan;
	}
	//---------------------------------------------------------
	ProjectTestRunPlan planProjectTestFile(String const & file)
	{
		ProjectTestRunPlan plan;
		if(file.empty())
		{
			return plan;
		}
		plan.filters.push_back(file);
		plan.label = file;
		return plan;
	}
	//---------------------------------------------------------
	ProjectTestRunPlan planProjectTest(String const & file, String const & name)
	{
		ProjectTestRunPlan plan;
		if(file.empty() || name.empty())
		{
			return plan;
		}
		plan.filters.push_back(file + "::" + name);
		plan.label = name;
		return plan;
	}
	//---------------------------------------------------------
	ProjectTestRunPlan planRerunFailedProjectTests(
		ProjectTestReport const & previous)
	{
		std::vector<String> filters;
		for(ScriptTestRecord const & record : previous.records)
		{
			if(projectTestPassed(record))
			{
				continue;
			}
			// a whole-FILE failure (a file that could not even load) has no
			// test name; re-running it means re-running the file
			filters.push_back(record.name.empty()
				? record.file : projectTestKey(record));
		}
		ProjectTestRunPlan plan;
		plan.filters = reduceProjectTestFilters(filters);
		plan.label = plan.filters.empty() ? String("nothing failed")
			: (std::to_string(plan.filters.size()) + " failed");
		return plan;
	}
	//---------------------------------------------------------
	void ProjectTestRunProgress::begin(ProjectTestRunPlan const & plan)
	{
		this->mPlan = plan;
		this->mLeg = 0;
		this->mExitCode = 0;
		this->mState = plan.filters.empty() ? ProjectTestRunState::Finished
			: ProjectTestRunState::Running;
	}
	//---------------------------------------------------------
	String const & ProjectTestRunProgress::currentFilter() const
	{
		static const String kNone;
		if(this->mState != ProjectTestRunState::Running ||
			this->mLeg >= this->mPlan.filters.size())
		{
			return kNone;
		}
		return this->mPlan.filters[this->mLeg];
	}
	//---------------------------------------------------------
	void ProjectTestRunProgress::legFinished(int exitCode)
	{
		if(this->mState != ProjectTestRunState::Running)
		{
			return;
		}
		// the worst verdict wins: one failing leg makes the whole run fail,
		// exactly as one failing test makes a suite fail
		this->mExitCode = std::max(this->mExitCode, exitCode);
		++this->mLeg;
		if(this->mLeg >= this->mPlan.filters.size())
		{
			this->mState = ProjectTestRunState::Finished;
		}
	}
	//---------------------------------------------------------
	void ProjectTestRunProgress::cancel()
	{
		if(this->mState == ProjectTestRunState::Running)
		{
			this->mState = ProjectTestRunState::Cancelled;
		}
	}
	//---------------------------------------------------------
	std::size_t ProjectTestRunProgress::leg() const
	{
		return (this->mState == ProjectTestRunState::Running)
			? this->mLeg + 1 : 0;
	}
	//---------------------------------------------------------
	String projectTestRunRefusal(ProjectTestPreflight const & facts)
	{
		// ORDER IS THE MESSAGE: the first thing a caller must fix comes first,
		// so a person who has neither a project nor a suite is not told about
		// the suite
		if(facts.runInFlight)
		{
			return "a test run is already in flight - stop it first";
		}
		if(!facts.projectOpen)
		{
			return "no project is open, and a test suite belongs to a project";
		}
		if(!facts.scriptingAvailable)
		{
			// the honest refusal, never a green run: a build with no
			// interpreter cannot answer the question that was asked
			return "this build has no scripting backend (ORKIGE_SCRIPTING=OFF)"
				", so it cannot run a Lua suite - use a build with scripting "
				"enabled";
		}
		if(!facts.testsDirectoryExists)
		{
			return "'" + facts.projectName + "' has no test suite - a project "
				"tests itself with '" +
				String(Orkige::ScriptTestTools::testFileSuffix()) +
				"' files under '" + facts.testsDirectory + "'";
		}
		if(!facts.playerFound)
		{
			// the runner is the PLAYER's; an installation that carries none
			// has nothing to run a suite in
			return "no player executable found - this build ships none beside "
				"the editor and no build tree is reachable, so there is "
				"nothing to run the suite in";
		}
		if(facts.planEmpty)
		{
			return "there is nothing to run";
		}
		return String();
	}
	//---------------------------------------------------------
	std::vector<Orkige::ScriptTestFile> scanProjectTests(
		String const & projectRoot)
	{
		if(projectRoot.empty())
		{
			return std::vector<Orkige::ScriptTestFile>();
		}
		const std::filesystem::path root(projectRoot);
		const std::filesystem::path testsDirectory =
			root / Orkige::ScriptTestTools::testsDirectoryName();
		std::error_code error;
		if(!std::filesystem::is_directory(testsDirectory, error))
		{
			return std::vector<Orkige::ScriptTestFile>();
		}
		Orkige::StringVector paths;
		for(std::filesystem::recursive_directory_iterator
			it(testsDirectory, error), end; !error && it != end;
			it.increment(error))
		{
			// the ONE reserved-output policy, so a stray build tree under
			// tests/ is never walked (@see ProjectPaths) - the same guard the
			// runner's own walk carries
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
			paths.push_back(it->path().lexically_relative(root)
				.generic_string());
		}
		return Orkige::ScriptTestTools::collectTestFiles(paths);
	}
	//---------------------------------------------------------
}
