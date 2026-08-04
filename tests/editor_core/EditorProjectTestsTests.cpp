/**************************************************************
	created:	2026/08/04 at 12:00
	filename: 	EditorProjectTestsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the Tests panel's pure half
	(tools/editor/EditorProjectTests.{h,cpp}): the runner's JSONL run
	artifact accumulated into a result model, where a failure points,
	which runs a button implies, and the lifecycle of a multi-leg run.

	The cases that matter most are the ones where a wrong answer would
	be a LIE about a test suite: a report with no summary line is a run
	that died and must not read as a finished one; an unknown status
	word must never be folded into "passed"; and re-running a set of
	failures must not silently skip one because the filter grammar is a
	substring match.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <EditorProjectTests.h>

#include <core_script/ScriptTestReport.h>

#include <string>
#include <vector>

using OrkigeEditor::consumeProjectTestLines;
using OrkigeEditor::feedProjectTestLine;
using OrkigeEditor::planAllProjectTests;
using OrkigeEditor::planProjectTest;
using OrkigeEditor::planProjectTestFile;
using OrkigeEditor::planRerunFailedProjectTests;
using OrkigeEditor::ProjectTestLocation;
using OrkigeEditor::ProjectTestReport;
using OrkigeEditor::ProjectTestRunPlan;
using OrkigeEditor::ProjectTestRunProgress;
using OrkigeEditor::ProjectTestRunState;
using OrkigeEditor::projectTestFailureLocation;
using OrkigeEditor::projectTestKey;
using OrkigeEditor::projectTestPlanCoversAll;
using OrkigeEditor::reduceProjectTestFilters;

namespace
{
	//! one `test` line, written by the RUNNER's own writer - so these cases
	//! read the real format rather than a hand-spelled imitation of it
	std::string testLine(std::string const & file, std::string const & name,
		std::string const & status, std::string const & message = "",
		double ms = 0.0)
	{
		Orkige::ScriptTestRecord record;
		record.file = file;
		record.name = name;
		record.status = status;
		record.message = message;
		record.ms = ms;
		return Orkige::ScriptTestReport::testLine(record);
	}
}

TEST_CASE("a report accumulates the runner's own artifact", "[projecttests]")
{
	Orkige::ScriptTestSummary written;
	written.files = 1;
	written.total = 2;
	written.passed = 1;
	written.failed = 1;

	ProjectTestReport report;
	feedProjectTestLine(report, Orkige::ScriptTestReport::metaLine(
		"Jumper Lua", "2026-08-04T10:00:00Z", "", 1));
	feedProjectTestLine(report, testLine("tests/movement.test.lua",
		"clamp holds", "pass", "", 0.5));
	feedProjectTestLine(report, testLine("tests/movement.test.lua",
		"gravity pulls", "fail", "tests/movement.test.lua:12: expected 3"));
	feedProjectTestLine(report,
		Orkige::ScriptTestReport::summaryLine(written));

	REQUIRE(report.hasMeta);
	CHECK(report.meta.project == "Jumper Lua");
	CHECK(report.meta.files == 1);
	REQUIRE(report.records.size() == 2);
	CHECK(report.records[0].name == "clamp holds");
	CHECK(report.records[1].status == "fail");
	CHECK(report.hasSummary);

	const Orkige::ScriptTestSummary tally = report.tally();
	CHECK(tally.total == 2);
	CHECK(tally.passed == 1);
	CHECK(tally.failed == 1);
	CHECK(tally.errors == 0);
	CHECK(tally.files == 1);
	CHECK(tally.exitCode() == 1);
	REQUIRE(report.failures().size() == 1);
	CHECK(report.failures()[0].name == "gravity pulls");
}

TEST_CASE("a run that died leaves no summary line", "[projecttests]")
{
	// THE case this model exists for: the artifact's last line is where the
	// runner was when it fell. Nothing may present that as a finished run.
	ProjectTestReport report;
	feedProjectTestLine(report, Orkige::ScriptTestReport::metaLine("P", "", "",
		2));
	feedProjectTestLine(report, testLine("tests/a.test.lua", "one", "pass"));
	CHECK_FALSE(report.hasSummary);
	REQUIRE(report.records.size() == 1);
	CHECK(report.records.back().name == "one");
	// and the tally still describes exactly what DID run
	CHECK(report.tally().total == 1);
	CHECK(report.tally().exitCode() == 0);
}

TEST_CASE("a malformed or unknown line changes nothing", "[projecttests]")
{
	ProjectTestReport report;
	const char* junk[] = {
		"",
		"   ",
		"not json at all",
		"{\"record\":\"test\"",					// truncated mid-write
		"[1,2,3]",								// json, but not an object
		"{\"record\":\"something-new\"}",		// a kind this build lacks
		// a status word outside the closed vocabulary: treating it as a pass
		// would be the worst possible mistake a test reader can make
		"{\"record\":\"test\",\"file\":\"a\",\"name\":\"b\",\"status\":\"ok\"}",
	};
	for (const char* line : junk)
	{
		INFO("line: " << line);
		CHECK(feedProjectTestLine(report, line) ==
			Orkige::ScriptTestReport::LineKind::None);
	}
	CHECK(report.records.empty());
	CHECK_FALSE(report.hasMeta);
	CHECK_FALSE(report.hasSummary);
}

TEST_CASE("a re-run replaces a test's row instead of appending", "[projecttests]")
{
	ProjectTestReport report;
	feedProjectTestLine(report, testLine("tests/a.test.lua", "one", "fail",
		"tests/a.test.lua:3: no"));
	feedProjectTestLine(report, testLine("tests/a.test.lua", "two", "pass"));
	feedProjectTestLine(report, testLine("tests/a.test.lua", "one", "pass"));
	REQUIRE(report.records.size() == 2);
	CHECK(report.records[0].name == "one");
	CHECK(report.records[0].status == "pass");
	CHECK(report.records[0].message.empty());
	CHECK(report.failures().empty());
}

TEST_CASE("tailing consumes complete lines and keeps the partial one",
	"[projecttests]")
{
	// the writer flushes per record, but a reader can still catch a line
	// mid-write; a half-parsed record would be a fabricated verdict
	ProjectTestReport report;
	Orkige::String buffer =
		testLine("tests/a.test.lua", "one", "pass") + "\n" +
		testLine("tests/a.test.lua", "two", "pass") + "\n" +
		"{\"record\":\"test\",\"file\":\"tests/a.te";
	CHECK(consumeProjectTestLines(buffer, report) == 2);
	CHECK(report.records.size() == 2);
	CHECK(buffer == "{\"record\":\"test\",\"file\":\"tests/a.te");

	// the rest of that line arrives on the next look
	buffer += "st.lua\",\"name\":\"three\",\"status\":\"pass\","
		"\"message\":\"\",\"ms\":0}\n";
	CHECK(consumeProjectTestLines(buffer, report) == 1);
	CHECK(report.records.size() == 3);
	CHECK(buffer.empty());
}

TEST_CASE("a CRLF-carrying artifact still reads", "[projecttests]")
{
	ProjectTestReport report;
	Orkige::String buffer = testLine("tests/a.test.lua", "one", "pass") +
		"\r\n";
	CHECK(consumeProjectTestLines(buffer, report) == 1);
	REQUIRE(report.records.size() == 1);
	CHECK(report.records[0].name == "one");
}

TEST_CASE("a failure message carries its file:line", "[projecttests]")
{
	const ProjectTestLocation at = projectTestFailureLocation(
		"tests/movement.test.lua:42: expected 3, got 4");
	REQUIRE(at.found());
	CHECK(at.file == "tests/movement.test.lua");
	CHECK(at.line == 42);

	// the FIRST position wins: the raise site is at the front, and a message
	// may quote another one in its text
	const ProjectTestLocation first = projectTestFailureLocation(
		"tests/a.test.lua:7: see scripts/player.lua:99 for why");
	REQUIRE(first.found());
	CHECK(first.file == "tests/a.test.lua");
	CHECK(first.line == 7);
}

TEST_CASE("a message with no position yields no location", "[projecttests]")
{
	// a refusal the RUNNER authored has no source position, and inventing one
	// would send a reader to the wrong place
	const char* messageless[] = {
		"the scene 'levels/one.oscene' could not be loaded",
		"",
		"tests/a.test.lua: no line number here",
		"tests/a.test.lua:0: a line zero is not a position",
		":12: nothing before the colon",
	};
	for (const char* message : messageless)
	{
		INFO("message: " << message);
		CHECK_FALSE(projectTestFailureLocation(message).found());
	}
}

TEST_CASE("only a whole-suite run may replace the previous results",
	"[projecttests]")
{
	// re-running one failure must not erase the passes that are still true;
	// this is the rule the session reads before it clears the report
	CHECK(OrkigeEditor::projectTestPlanCoversAll(planAllProjectTests()));
	CHECK_FALSE(OrkigeEditor::projectTestPlanCoversAll(
		planProjectTestFile("tests/a.test.lua")));
	CHECK_FALSE(OrkigeEditor::projectTestPlanCoversAll(
		planProjectTest("tests/a.test.lua", "one")));
	CHECK_FALSE(OrkigeEditor::projectTestPlanCoversAll(ProjectTestRunPlan()));
}

TEST_CASE("a plan is one leg per thing to run", "[projecttests]")
{
	const ProjectTestRunPlan all = planAllProjectTests();
	REQUIRE(all.filters.size() == 1);
	CHECK(all.filters[0].empty());			// no filter = everything

	const ProjectTestRunPlan file = planProjectTestFile("tests/a.test.lua");
	REQUIRE(file.filters.size() == 1);
	CHECK(file.filters[0] == "tests/a.test.lua");

	const ProjectTestRunPlan one = planProjectTest("tests/a.test.lua",
		"clamp holds");
	REQUIRE(one.filters.size() == 1);
	CHECK(one.filters[0] == "tests/a.test.lua::clamp holds");

	// a plan with nothing to name is EMPTY, not a plan that runs everything
	CHECK(planProjectTestFile("").empty());
	CHECK(planProjectTest("tests/a.test.lua", "").empty());
	CHECK(planProjectTest("", "one").empty());
}

TEST_CASE("filters that cover each other collapse", "[projecttests]")
{
	// the rule follows from the grammar: the runner matches a SUBSTRING, so a
	// shorter filter already selects everything a longer one containing it
	// would
	const std::vector<Orkige::String> reduced = reduceProjectTestFilters({
		"tests/a.test.lua::clamp",
		"tests/a.test.lua::clamp holds more",	// covered by the one above
		"tests/b.test.lua::other",
		"tests/a.test.lua::clamp",				// a duplicate
	});
	REQUIRE(reduced.size() == 2);
	CHECK(reduced[0] == "tests/a.test.lua::clamp");
	CHECK(reduced[1] == "tests/b.test.lua::other");

	// a broader filter arriving LATE still wins - order of arrival must not
	// leave a redundant leg behind
	const std::vector<Orkige::String> late = reduceProjectTestFilters({
		"tests/a.test.lua::one", "tests/a.test.lua" });
	REQUIRE(late.size() == 1);
	CHECK(late[0] == "tests/a.test.lua");

	// an empty filter is a substring of everything, so it collapses the lot
	const std::vector<Orkige::String> everything = reduceProjectTestFilters({
		"tests/a.test.lua::one", "", "tests/b.test.lua" });
	REQUIRE(everything.size() == 1);
	CHECK(everything[0].empty());
}

TEST_CASE("re-run failed selects exactly the failures", "[projecttests]")
{
	ProjectTestReport report;
	feedProjectTestLine(report, testLine("tests/a.test.lua", "one", "pass"));
	feedProjectTestLine(report, testLine("tests/a.test.lua", "two", "fail",
		"tests/a.test.lua:4: no"));
	feedProjectTestLine(report, testLine("tests/b.test.lua", "three", "error",
		"the scene could not be loaded"));

	const ProjectTestRunPlan plan = planRerunFailedProjectTests(report);
	REQUIRE(plan.filters.size() == 2);
	CHECK(plan.filters[0] == "tests/a.test.lua::two");
	CHECK(plan.filters[1] == "tests/b.test.lua::three");

	// nothing failed => nothing to run, and the button that offers it is dead
	ProjectTestReport green;
	feedProjectTestLine(green, testLine("tests/a.test.lua", "one", "pass"));
	CHECK(planRerunFailedProjectTests(green).empty());
	CHECK(planRerunFailedProjectTests(ProjectTestReport()).empty());
}

TEST_CASE("a whole-file failure re-runs the file", "[projecttests]")
{
	// a file that could not even load records with NO test name; the only
	// thing that can be re-run is the file
	ProjectTestReport report;
	feedProjectTestLine(report, testLine("tests/broken.test.lua", "", "error",
		"tests/broken.test.lua:1: unexpected symbol"));
	const ProjectTestRunPlan plan = planRerunFailedProjectTests(report);
	REQUIRE(plan.filters.size() == 1);
	CHECK(plan.filters[0] == "tests/broken.test.lua");
}

TEST_CASE("a record's key is also a filter that selects it", "[projecttests]")
{
	Orkige::ScriptTestRecord record;
	record.file = "tests/a.test.lua";
	record.name = "clamp holds";
	CHECK(projectTestKey(record) == "tests/a.test.lua::clamp holds");
	// the identity and the filter subject are ONE string - that is what lets
	// a row be re-run without a second grammar
	CHECK(Orkige::ScriptTestTools::filterMatches(projectTestKey(record),
		record.file, record.name));
}

TEST_CASE("a run walks its legs in order and ends Finished", "[projecttests]")
{
	ProjectTestRunPlan plan;
	plan.filters = { "a", "b", "c" };
	ProjectTestRunProgress progress;
	CHECK(progress.state() == ProjectTestRunState::Idle);

	progress.begin(plan);
	CHECK(progress.state() == ProjectTestRunState::Running);
	CHECK(progress.legCount() == 3);
	CHECK(progress.leg() == 1);
	CHECK(progress.currentFilter() == "a");

	progress.legFinished(0);
	CHECK(progress.leg() == 2);
	CHECK(progress.currentFilter() == "b");
	progress.legFinished(0);
	CHECK(progress.currentFilter() == "c");
	progress.legFinished(0);
	CHECK(progress.state() == ProjectTestRunState::Finished);
	CHECK(progress.leg() == 0);
	CHECK(progress.currentFilter().empty());
	CHECK(progress.exitCode() == 0);
}

TEST_CASE("one failing leg fails the whole run", "[projecttests]")
{
	ProjectTestRunPlan plan;
	plan.filters = { "a", "b" };
	ProjectTestRunProgress progress;
	progress.begin(plan);
	progress.legFinished(1);
	progress.legFinished(0);		// a later green leg does not redeem it
	CHECK(progress.state() == ProjectTestRunState::Finished);
	CHECK(progress.exitCode() == 1);
}

TEST_CASE("cancelling stops the remaining legs and keeps the verdict",
	"[projecttests]")
{
	ProjectTestRunPlan plan;
	plan.filters = { "a", "b", "c" };
	ProjectTestRunProgress progress;
	progress.begin(plan);
	progress.legFinished(1);
	progress.cancel();
	CHECK(progress.state() == ProjectTestRunState::Cancelled);
	CHECK(progress.currentFilter().empty());	// nothing is in flight
	CHECK(progress.exitCode() == 1);			// what was learned still stands
	// a cancelled run does not resume by being reported at
	progress.legFinished(0);
	CHECK(progress.state() == ProjectTestRunState::Cancelled);
	CHECK(progress.exitCode() == 1);
}

TEST_CASE("an empty plan finishes instead of waiting for a leg",
	"[projecttests]")
{
	// Running with nothing to run would leave a caller polling forever
	ProjectTestRunProgress progress;
	progress.begin(ProjectTestRunPlan());
	CHECK(progress.state() == ProjectTestRunState::Finished);
	CHECK(progress.legCount() == 0);
	CHECK(progress.leg() == 0);
	CHECK(progress.currentFilter().empty());
}

TEST_CASE("one decision refuses a run, in one order", "[projecttests]")
{
	// two doors reach the runner from inside the editor binary - the headless
	// `test` subcommand and the panel/MCP session - in different phases of the
	// process. They cannot share a code path; they must not hold two OPINIONS.
	using OrkigeEditor::ProjectTestPreflight;
	using OrkigeEditor::projectTestRunRefusal;

	ProjectTestPreflight ready;
	ready.projectName = "Jumper Lua";
	ready.testsDirectory = "/p/tests";
	ready.projectOpen = true;
	ready.scriptingAvailable = true;
	ready.testsDirectoryExists = true;
	ready.playerFound = true;
	CHECK(projectTestRunRefusal(ready).empty());

	ProjectTestPreflight noProject = ready;
	noProject.projectOpen = false;
	CHECK(projectTestRunRefusal(noProject).find("no project is open") !=
		Orkige::String::npos);

	ProjectTestPreflight noScripting = ready;
	noScripting.scriptingAvailable = false;
	CHECK(projectTestRunRefusal(noScripting).find("ORKIGE_SCRIPTING=OFF") !=
		Orkige::String::npos);

	ProjectTestPreflight noSuite = ready;
	noSuite.testsDirectoryExists = false;
	CHECK(projectTestRunRefusal(noSuite).find("has no test suite") !=
		Orkige::String::npos);
	// the sentence names the project and where the files would go, so a
	// reader knows what to create
	CHECK(projectTestRunRefusal(noSuite).find("Jumper Lua") !=
		Orkige::String::npos);
	CHECK(projectTestRunRefusal(noSuite).find("/p/tests") !=
		Orkige::String::npos);

	ProjectTestPreflight noPlayer = ready;
	noPlayer.playerFound = false;
	CHECK(projectTestRunRefusal(noPlayer).find("no player executable") !=
		Orkige::String::npos);

	ProjectTestPreflight nothingSelected = ready;
	nothingSelected.planEmpty = true;
	CHECK(projectTestRunRefusal(nothingSelected).find("nothing to run") !=
		Orkige::String::npos);

	// ORDER IS THE MESSAGE: a run already in flight is the first thing to
	// say, and someone with neither a project nor a suite is not told about
	// the suite
	ProjectTestPreflight busy = ready;
	busy.runInFlight = true;
	busy.testsDirectoryExists = false;
	CHECK(projectTestRunRefusal(busy).find("already in flight") !=
		Orkige::String::npos);
	ProjectTestPreflight bare;
	bare.scriptingAvailable = true;
	CHECK(projectTestRunRefusal(bare).find("no project is open") !=
		Orkige::String::npos);
}

TEST_CASE("scanning a project with no tests directory yields nothing",
	"[projecttests]")
{
	CHECK(OrkigeEditor::scanProjectTests("").empty());
	CHECK(OrkigeEditor::scanProjectTests(
		"/definitely/not/a/project/on/this/machine").empty());
}
