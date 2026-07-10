/**************************************************************
	created:	2026/07/11 at 11:00
	filename: 	BreadcrumbsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit coverage for the crash-survivable breadcrumb trail: the
	bounded in-memory ring (last-N, oldest dropped in order), the hard byte
	cap, the JSON-line format (parsed back through the engine's own JSON
	reader), rotate() moving live -> prev and loadFile round-tripping the
	survived file. No window, no player.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_debug/Breadcrumbs.h"
#include "core_debugnet/Json.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace Orkige;

namespace
{
	//! split a .jsonl blob into its non-empty lines
	std::vector<String> splitLines(String const & text)
	{
		std::vector<String> lines;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			if (!line.empty())
			{
				lines.push_back(line);
			}
		}
		return lines;
	}
	//! a unique temp file path for a test (removed by the caller)
	std::string tempPath(char const * name)
	{
		return (std::filesystem::temp_directory_path() /
			(std::string("orkige_breadcrumbs_") + name + ".jsonl")).string();
	}
}

TEST_CASE("Breadcrumbs ring keeps the last N entries in order", "[unit][breadcrumbs]")
{
	Breadcrumbs crumbs(4);
	for (int i = 0; i < 6; ++i)
	{
		crumbs.record("scene", "load " + std::to_string(i));
	}
	CHECK(crumbs.count() == 4u);
	const std::vector<String> lines = splitLines(crumbs.contents());
	REQUIRE(lines.size() == 4u);
	// the two oldest ("load 0","load 1") were dropped; the rest stay in order
	for (size_t i = 0; i < lines.size(); ++i)
	{
		JsonValue value;
		REQUIRE(JsonValue::parse(lines[i], value));
		CHECK(value.get("kind").asString() == "scene");
		CHECK(value.get("msg").asString() ==
			("load " + std::to_string(static_cast<int>(i) + 2)));
	}
}

TEST_CASE("Breadcrumbs lines are parseable JSON with optional fields",
	"[unit][breadcrumbs]")
{
	Breadcrumbs crumbs(8);
	crumbs.record("script_error", "boom \"quoted\"\nnewline",
		{ { "object", "Player" } });
	const std::vector<String> lines = splitLines(crumbs.contents());
	REQUIRE(lines.size() == 1u);
	JsonValue value;
	REQUIRE(JsonValue::parse(lines[0], value));
	CHECK(value.get("kind").asString() == "script_error");
	CHECK(value.get("msg").asString() == "boom \"quoted\"\nnewline");
	CHECK(value.get("object").asString() == "Player");
	CHECK(value.has("t"));
}

TEST_CASE("Breadcrumbs enforces the hard byte cap", "[unit][breadcrumbs]")
{
	const std::string path = tempPath("bytecap");
	std::filesystem::remove(path);
	{
		// tiny byte cap; each line is ~40+ bytes, so most entries get dropped
		Breadcrumbs crumbs(1000, /*maxFileBytes*/ 200);
		crumbs.setFile(path);
		crumbs.rotate();
		for (int i = 0; i < 100; ++i)
		{
			crumbs.record("log", "a warning line number " + std::to_string(i));
		}
	}
	String disk;
	REQUIRE(Breadcrumbs::loadFile(path, disk));
	CHECK(disk.size() <= 200u);
	// whatever survived is still whole, parseable lines - and the NEWEST ones
	const std::vector<String> lines = splitLines(disk);
	REQUIRE(!lines.empty());
	JsonValue last;
	REQUIRE(JsonValue::parse(lines.back(), last));
	CHECK(last.get("msg").asString() == "a warning line number 99");
	std::filesystem::remove(path);
}

TEST_CASE("Breadcrumbs rotate moves live -> prev and loadFile round-trips",
	"[unit][breadcrumbs]")
{
	const std::string path = tempPath("rotate");
	const std::string prev = tempPath("rotate.prev");
	// the derived prev path replaces ".jsonl" with ".prev.jsonl"
	const std::string derivedPrev =
		path.substr(0, path.size() - 6) + ".prev.jsonl";
	std::filesystem::remove(path);
	std::filesystem::remove(derivedPrev);

	{
		Breadcrumbs crumbs(64);
		crumbs.setFile(path);
		crumbs.rotate();							// first boot: no prior file
		crumbs.record("boot", "session one");
		crumbs.record("scene", "level1.oscene");
	}
	// a new session rotates the previous run's file aside and starts fresh
	{
		Breadcrumbs crumbs(64);
		crumbs.setFile(path);
		crumbs.rotate();							// moves session one -> prev
		crumbs.record("boot", "session two");
	}

	String live;
	String previous;
	REQUIRE(Breadcrumbs::loadFile(path, live));
	REQUIRE(Breadcrumbs::loadFile(derivedPrev, previous));
	// the survived (previous) file holds session one; the live file session two
	CHECK(previous.find("session one") != String::npos);
	CHECK(previous.find("level1.oscene") != String::npos);
	CHECK(live.find("session two") != String::npos);
	CHECK(live.find("session one") == String::npos);

	std::filesystem::remove(path);
	std::filesystem::remove(derivedPrev);
	(void)prev;
}

TEST_CASE("Breadcrumbs loadFile fails honestly on a missing file",
	"[unit][breadcrumbs]")
{
	String out = "untouched";
	CHECK_FALSE(Breadcrumbs::loadFile(
		"/nonexistent/orkige/breadcrumbs.jsonl", out));
	CHECK(out == "untouched");
}
