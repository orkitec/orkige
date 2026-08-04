/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptTestTierTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Tests of the LUA TEST TIER - the vocabulary a project's own `*.test.lua`
	files are written in, the discovery rule that finds them and the JSONL run
	artifact that reports them. The pure halves (discovery, filtering, record
	formatting) run in every configuration; the vocabulary itself is driven
	through the real runtime and skips honestly under ORKIGE_SCRIPTING=OFF.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_filesystem/ResourceReader.h>
#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptTestReport.h>
#include <core_script/ScriptTestTools.h>

#include <map>
#include <string>
#include <vector>

namespace
{
	//! a fake in-test reader: test files and libraries by resource name, with
	//! nothing on disk behind them
	class TierReader : public Orkige::ResourceReader
	{
	public:
		std::map<Orkige::String, Orkige::String> files;

		bool readText(Orkige::String const & name,
			Orkige::String & out) const override
		{
			std::map<Orkige::String, Orkige::String>::const_iterator it =
				this->files.find(name);
			if(it == this->files.end())
			{
				return false;
			}
			out = it->second;
			return true;
		}
	};

	struct InstalledReader
	{
		explicit InstalledReader(Orkige::ResourceReader * reader)
		{
			Orkige::ResourceAccess::setReader(reader);
		}
		~InstalledReader()
		{
			Orkige::ResourceAccess::setReader(nullptr);
		}
	};

	//! find one record by test name (Catch-friendly: returns a default record
	//! with an empty status when absent)
	Orkige::ScriptTestRecord recordNamed(
		std::vector<Orkige::ScriptTestRecord> const & records,
		Orkige::String const & name)
	{
		for(Orkige::ScriptTestRecord const & record : records)
		{
			if(record.name == name)
			{
				return record;
			}
		}
		return Orkige::ScriptTestRecord();
	}
}

TEST_CASE("ScriptTestTools recognises a test file by its suffix", "[script]")
{
	CHECK(Orkige::ScriptTestTools::testNameForFile("tests/movement.test.lua") ==
		"movement");
	CHECK(Orkige::ScriptTestTools::testNameForFile(
		"tests/deep/nested/loot.test.lua") == "loot");
	// a Windows-spelled path derives the same name
	CHECK(Orkige::ScriptTestTools::testNameForFile(
		"tests\\deep\\loot.test.lua") == "loot");

	// everything else in the suffix family is NOT a test file
	CHECK(Orkige::ScriptTestTools::testNameForFile(
		"scripts/player.component.lua").empty());
	CHECK(Orkige::ScriptTestTools::testNameForFile(
		"scripts/retag.editor.lua").empty());
	CHECK(Orkige::ScriptTestTools::testNameForFile(
		"scripts/mathutil.lua").empty());
	CHECK(Orkige::ScriptTestTools::testNameForFile("tests/notes.txt").empty());
	// the bare suffix is a file with no name
	CHECK(Orkige::ScriptTestTools::testNameForFile(".test.lua").empty());
}

TEST_CASE("ScriptTestTools::collectTestFiles orders and dedupes", "[script]")
{
	Orkige::StringVector paths;
	paths.push_back("tests/zebra.test.lua");
	paths.push_back("tests/readme.md");
	paths.push_back("tests/alpha.test.lua");
	paths.push_back("tests/helpers.lua");			// a library, not a test
	paths.push_back("tests/sub/alpha.test.lua");	// a duplicate stable name
	paths.push_back("scripts/player.component.lua");

	Orkige::StringVector duplicates;
	const std::vector<Orkige::ScriptTestFile> files =
		Orkige::ScriptTestTools::collectTestFiles(paths, &duplicates);

	REQUIRE(files.size() == 2);
	// sorted by stable name, so a run is reproducible on every machine
	CHECK(files[0].name == "alpha");
	CHECK(files[0].resourceName == "tests/alpha.test.lua");
	CHECK(files[1].name == "zebra");

	// the first of a duplicate name wins and the clash is reported, never
	// silently dropped and never fatal
	REQUIRE(duplicates.size() == 1);
	CHECK(duplicates[0].find("tests/sub/alpha.test.lua") !=
		Orkige::String::npos);
	CHECK(duplicates[0].find("tests/alpha.test.lua") != Orkige::String::npos);
}

TEST_CASE("ScriptTestTools::filterMatches selects by file or by name",
	"[script]")
{
	const Orkige::String file = "tests/movement.test.lua";
	// an empty filter selects everything
	CHECK(Orkige::ScriptTestTools::filterMatches("", file, "clamp"));
	// a file substring selects the whole file
	CHECK(Orkige::ScriptTestTools::filterMatches("movement", file, "clamp"));
	// a name substring selects one case
	CHECK(Orkige::ScriptTestTools::filterMatches("clamp", file, "clamp is "
		"symmetric"));
	CHECK_FALSE(Orkige::ScriptTestTools::filterMatches("physics", file,
		"clamp"));
}

TEST_CASE("the JSONL run artifact carries file, name, status and message",
	"[script]")
{
	CHECK(Orkige::ScriptTestReport::metaLine("roller", "2026-08-03T10:00:00Z",
		"", 2) ==
		"{\"record\":\"meta\",\"project\":\"roller\","
		"\"utc\":\"2026-08-03T10:00:00Z\",\"filter\":\"\",\"files\":2}");

	Orkige::ScriptTestRecord record;
	record.file = "tests/movement.test.lua";
	record.name = "clamp is symmetric";
	record.status = "fail";
	// a message carrying quotes must survive as JSON
	record.message = "tests/movement.test.lua:4: expected -1, got \"x\"";
	record.ms = 1.25;
	const Orkige::String line = Orkige::ScriptTestReport::testLine(record);
	CHECK(line.find("\"file\":\"tests/movement.test.lua\"") !=
		Orkige::String::npos);
	CHECK(line.find("\"status\":\"fail\"") != Orkige::String::npos);
	CHECK(line.find("\\\"x\\\"") != Orkige::String::npos);

	Orkige::ScriptTestSummary summary;
	summary.files = 2;
	summary.total = 3;
	summary.passed = 2;
	summary.failed = 1;
	CHECK(summary.exitCode() == 1);
	CHECK(Orkige::ScriptTestReport::summaryLine(summary).find(
		"\"exitCode\":1") != Orkige::String::npos);

	// a clean run is the ctest contract's zero
	Orkige::ScriptTestSummary clean;
	clean.files = 1;
	clean.total = 4;
	clean.passed = 4;
	CHECK(clean.exitCode() == 0);
	CHECK(Orkige::ScriptTestReport::summaryText(clean).find("4 passed") !=
		Orkige::String::npos);
}

TEST_CASE("the artifact reader is the writer's exact inverse", "[script]")
{
	// The reader lives beside the writer so the format has ONE definition -
	// and a round trip is what proves that, rather than two hand-kept
	// spellings that agree until one of them is edited.
	Orkige::ScriptTestReport::MetaRecord meta;
	Orkige::ScriptTestRecord record;
	Orkige::ScriptTestSummary summary;

	Orkige::ScriptTestRecord written;
	written.file = "tests/movement.test.lua";
	written.name = "clamp is symmetric";
	written.status = "fail";
	written.message = "tests/movement.test.lua:4: expected -1, got \"x\"";
	written.ms = 1.25;
	REQUIRE(Orkige::ScriptTestReport::parseLine(
		Orkige::ScriptTestReport::testLine(written), meta, record, summary) ==
		Orkige::ScriptTestReport::LineKind::Test);
	CHECK(record.file == written.file);
	CHECK(record.name == written.name);
	CHECK(record.status == written.status);
	CHECK(record.message == written.message);	// quotes and all
	CHECK(record.ms == written.ms);

	REQUIRE(Orkige::ScriptTestReport::parseLine(
		Orkige::ScriptTestReport::metaLine("roller", "2026-08-03T10:00:00Z",
			"movement", 2), meta, record, summary) ==
		Orkige::ScriptTestReport::LineKind::Meta);
	CHECK(meta.project == "roller");
	CHECK(meta.utc == "2026-08-03T10:00:00Z");
	CHECK(meta.filter == "movement");
	CHECK(meta.files == 2);

	Orkige::ScriptTestSummary tally;
	tally.files = 2;
	tally.total = 3;
	tally.passed = 2;
	tally.failed = 1;
	tally.filtered = 4;
	tally.ms = 12.5;
	REQUIRE(Orkige::ScriptTestReport::parseLine(
		Orkige::ScriptTestReport::summaryLine(tally), meta, record, summary) ==
		Orkige::ScriptTestReport::LineKind::Summary);
	CHECK(summary.files == 2);
	CHECK(summary.total == 3);
	CHECK(summary.passed == 2);
	CHECK(summary.failed == 1);
	CHECK(summary.filtered == 4);
	CHECK(summary.ms == 12.5);
	// the exit code is DERIVED by the reader's own rule, never read back
	CHECK(summary.exitCode() == 1);
}

TEST_CASE("the artifact reader refuses what it cannot honestly read",
	"[script]")
{
	Orkige::ScriptTestReport::MetaRecord meta;
	Orkige::ScriptTestRecord record;
	Orkige::ScriptTestSummary summary;
	char const * const junk[] = {
		"",
		"not json",
		"{\"record\":\"test\"",					// truncated mid-write
		"[1,2,3]",									// json, but not an object
		"{\"record\":\"something-new\"}",		// a kind this build lacks
		// a status word outside the closed vocabulary: a reader that guessed
		// would turn an unknown verdict into a pass
		"{\"record\":\"test\",\"file\":\"a\",\"name\":\"b\","
			"\"status\":\"ok\"}",
	};
	for(char const * line : junk)
	{
		INFO("line: " << line);
		CHECK(Orkige::ScriptTestReport::parseLine(line, meta, record,
			summary) == Orkige::ScriptTestReport::LineKind::None);
	}
}

TEST_CASE("the test vocabulary passes, fails and names the failing line",
	"[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the test tier has nothing to run");
		return;
	}
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;

	TierReader reader;
	reader.files["tests/vocab.test.lua"] =
		"test('deep equal tables', function(t)\n"					// 1
		"  t.eq({ 1, 2, { a = 3 } }, { 1, 2, { a = 3 } })\n"		// 2
		"end)\n"													// 3
		"test('eq reports both sides', function(t)\n"				// 4
		"  t.eq(4, 3)\n"											// 5
		"end)\n"													// 6
		"test('near tolerates float drift', function(t)\n"			// 7
		"  t.near(0.1 + 0.2, 0.3)\n"								// 8
		"end)\n"													// 9
		"test('truthy falsy isnil', function(t)\n"					// 10
		"  t.truthy(1) t.falsy(false) t.isnil(nil)\n"				// 11
		"end)\n"													// 12
		"test('errors catches a raise', function(t)\n"				// 13
		"  local msg = t.errors(function() error('boom') end)\n"	// 14
		"  t.truthy(string.find(msg, 'boom', 1, true) ~= nil)\n"	// 15
		"end)\n"													// 16
		"test('an unexpected raise is an error', function(t)\n"		// 17
		"  local x = nil\n"											// 18
		"  return x.y\n"											// 19
		"end)\n"													// 20
		"test('fail says why', function(t)\n"						// 21
		"  t.fail('not implemented')\n"								// 22
		"end)\n";													// 23
	InstalledReader installed(&reader);

	std::vector<Orkige::ScriptTestRecord> records;
	int declared = 0;
	Orkige::String error;
	REQUIRE(runtime.runTestFile("tests/vocab.test.lua", "", records, &declared,
		&error));
	CHECK(error.empty());
	CHECK(declared == 7);
	REQUIRE(records.size() == 7);

	CHECK(recordNamed(records, "deep equal tables").status == "pass");
	CHECK(recordNamed(records, "near tolerates float drift").status == "pass");
	CHECK(recordNamed(records, "truthy falsy isnil").status == "pass");
	CHECK(recordNamed(records, "errors catches a raise").status == "pass");

	// an ASSERTION refusal is a "fail" and carries the expected/got discipline
	const Orkige::ScriptTestRecord eqFail =
		recordNamed(records, "eq reports both sides");
	CHECK(eqFail.status == "fail");
	CHECK(eqFail.message.find("expected 3, got 4") != Orkige::String::npos);
	// FILE:LINE COMES FREE: the chunk name is the project-relative path and
	// the line is the one inside the test body
	CHECK(eqFail.message.find("tests/vocab.test.lua:5:") !=
		Orkige::String::npos);

	// anything else raised is an "error", which is a different fact
	CHECK(recordNamed(records, "an unexpected raise is an error").status ==
		"error");

	const Orkige::ScriptTestRecord failed = recordNamed(records, "fail says why");
	CHECK(failed.status == "fail");
	CHECK(failed.message.find("not implemented") != Orkige::String::npos);
	CHECK(failed.message.find("tests/vocab.test.lua:22:") !=
		Orkige::String::npos);

	// the filter runs a subset without touching the rest
	records.clear();
	REQUIRE(runtime.runTestFile("tests/vocab.test.lua", "deep equal", records,
		&declared, &error));
	CHECK(declared == 7);
	REQUIRE(records.size() == 1);
	CHECK(records[0].name == "deep equal tables");
}

TEST_CASE("a frameless caller refuses a play-mode test honestly", "[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the test tier has nothing to run");
		return;
	}
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;

	TierReader reader;
	reader.files["tests/play.test.lua"] =
		"test('the ball falls', { scene = 'scenes/level.oscene' }, "
		"function(t)\n"
		"  t.fail('this body must never run')\n"
		"end)\n";
	InstalledReader installed(&reader);

	std::vector<Orkige::ScriptTestRecord> records;
	Orkige::String error;
	// runTestFile is the FRAMELESS road: it has no world to load a scene into
	// and advances no frames, so it cannot run this test
	REQUIRE(runtime.runTestFile("tests/play.test.lua", "", records, 0, &error));
	REQUIRE(records.size() == 1);
	// NOT a silent pass: the run fails and says exactly what is missing (the
	// frame-driven runner is orkige_player --run-tests)
	CHECK(records[0].status == "error");
	CHECK(records[0].message.find("frame-driven runner") !=
		Orkige::String::npos);
	CHECK(records[0].name == "the ball falls");
}

TEST_CASE("a test file that cannot load is a run failure, not a skip",
	"[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the test tier has nothing to run");
		return;
	}
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;

	TierReader reader;
	reader.files["tests/broken.test.lua"] = "test('x', function(t) end\n";
	InstalledReader installed(&reader);

	std::vector<Orkige::ScriptTestRecord> records;
	Orkige::String error;
	CHECK_FALSE(runtime.runTestFile("tests/broken.test.lua", "", records, 0,
		&error));
	CHECK(error.find("tests/broken.test.lua") != Orkige::String::npos);

	// an absent file, likewise
	records.clear();
	error.clear();
	CHECK_FALSE(runtime.runTestFile("tests/absent.test.lua", "", records, 0,
		&error));
	CHECK(error.find("not found") != Orkige::String::npos);
}

TEST_CASE("a test file reaches its helpers through script.require", "[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the test tier has nothing to run");
		return;
	}
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;

	// THE POINT OF THE WHOLE TIER: a test exercises the SAME library the game
	// loads, by the SAME mechanism, out of the same mounted content
	TierReader reader;
	reader.files["scripts/mathutil.lua"] =
		"local M = {}\n"
		"function M.clamp(v, lo, hi)\n"
		"  if v < lo then return lo end\n"
		"  if v > hi then return hi end\n"
		"  return v\n"
		"end\n"
		"return M\n";
	reader.files["tests/movement.test.lua"] =
		"local m = script.require('scripts/mathutil.lua')\n"
		"test('clamp is symmetric', function(t)\n"
		"  t.eq(m.clamp(-5, -1, 1), -1)\n"
		"  t.eq(m.clamp(5, -1, 1), 1)\n"
		"  t.eq(m.clamp(0, -1, 1), 0)\n"
		"end)\n";
	InstalledReader installed(&reader);

	std::vector<Orkige::ScriptTestRecord> records;
	Orkige::String error;
	REQUIRE(runtime.runTestFile("tests/movement.test.lua", "", records, 0,
		&error));
	REQUIRE(records.size() == 1);
	CHECK(records[0].status == "pass");
	CHECK(records[0].message.empty());
}
