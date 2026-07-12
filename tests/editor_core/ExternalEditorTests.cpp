/**************************************************************
	created:	2026/07/13 at 12:00
	filename: 	ExternalEditorTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the open-at-line service
	(tools/editor/ExternalEditor.{h,cpp}): the console file:line
	parser, the {file}/{line} command-template expansion, the
	resolution order (configured setting -> autodetected CLI ->
	platform opener, with an injected PATH probe so no real editor is
	launched) and the quick-peek line window reader.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <ExternalEditor.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Orkige;

TEST_CASE("parseFileLineRefs finds a bare project-relative reference",
	"[externaleditor]")
{
	const std::vector<FileLineRef> refs =
		parseFileLineRefs("scripts/player.lua:12");
	REQUIRE(refs.size() == 1);
	CHECK(refs[0].path == "scripts/player.lua");
	CHECK(refs[0].line == 12);
	CHECK(refs[0].column == 0);
}

TEST_CASE("parseFileLineRefs finds a reference embedded in a Lua error line",
	"[externaleditor]")
{
	const std::string text =
		"error: scripts/player.lua:34: attempt to index a nil value";
	const std::vector<FileLineRef> refs = parseFileLineRefs(text);
	REQUIRE(refs.size() == 1);
	CHECK(refs[0].path == "scripts/player.lua");
	CHECK(refs[0].line == 34);
	// the span points at the reference inside the surrounding message
	CHECK(text.substr(refs[0].begin, refs[0].end - refs[0].begin) ==
		"scripts/player.lua:34");
}

TEST_CASE("parseFileLineRefs handles an absolute path with a column",
	"[externaleditor]")
{
	const std::vector<FileLineRef> refs =
		parseFileLineRefs("[build] /Users/me/game/foo.cpp:45:9: error: bad");
	REQUIRE(refs.size() == 1);
	CHECK(refs[0].path == "/Users/me/game/foo.cpp");
	CHECK(refs[0].line == 45);
	CHECK(refs[0].column == 9);
}

TEST_CASE("parseFileLineRefs ignores non-path numeric colons", "[externaleditor]")
{
	// a "12:30:45" timestamp carries no '.' or '/', so it is not a reference
	CHECK(parseFileLineRefs("at 12:30:45 something happened").empty());
	CHECK(parseFileLineRefs("no references on this line at all").empty());
}

TEST_CASE("parseFileLineRefs finds multiple references on one line",
	"[externaleditor]")
{
	const std::vector<FileLineRef> refs =
		parseFileLineRefs("a/one.lua:1 and b/two.lua:2");
	REQUIRE(refs.size() == 2);
	CHECK(refs[0].path == "a/one.lua");
	CHECK(refs[0].line == 1);
	CHECK(refs[1].path == "b/two.lua");
	CHECK(refs[1].line == 2);
}

TEST_CASE("expandEditorCommand splits the template and keeps a spaced path whole",
	"[externaleditor]")
{
	const std::vector<std::string> argv =
		expandEditorCommand("code -g {file}:{line}", "/a b/x.lua", 12);
	REQUIRE(argv.size() == 3);
	CHECK(argv[0] == "code");
	CHECK(argv[1] == "-g");
	// the path (with a space) is ONE argv element, not split by the whitespace
	CHECK(argv[2] == "/a b/x.lua:12");
}

TEST_CASE("expandEditorCommand strips the line suffix when there is no line",
	"[externaleditor]")
{
	const std::vector<std::string> argv =
		expandEditorCommand("code -g {file}:{line}", "/a b/x.lua", 0);
	REQUIRE(argv.size() == 3);
	// no dangling trailing colon when the line is unknown
	CHECK(argv[2] == "/a b/x.lua");
}

TEST_CASE("expandEditorCommand substitutes a lone file placeholder",
	"[externaleditor]")
{
	const std::vector<std::string> argv =
		expandEditorCommand("open {file}", "/tmp/y.lua", 5);
	REQUIRE(argv.size() == 2);
	CHECK(argv[0] == "open");
	CHECK(argv[1] == "/tmp/y.lua");
}

TEST_CASE("resolveEditorCommand prefers the configured template",
	"[externaleditor]")
{
	// the probe must not even be consulted when a setting is present
	bool probed = false;
	auto probe = [&probed](std::string const&) { probed = true; return true; };
	const EditorCommandResolution res = resolveEditorCommand(
		"myeditor --goto {file}:{line}", "/p/x.lua", 7, probe, true);
	CHECK_FALSE(probed);
	CHECK(res.source == "setting");
	CHECK(res.opensAtLine);
	REQUIRE(res.argv.size() == 3);
	CHECK(res.argv[0] == "myeditor");
	CHECK(res.argv[2] == "/p/x.lua:7");
}

TEST_CASE("resolveEditorCommand autodetects the first CLI editor on PATH",
	"[externaleditor]")
{
	// only the second candidate ("subl") resolves; the first is absent
	auto probe = [](std::string const& exe) { return exe == "subl"; };
	const EditorCommandResolution res =
		resolveEditorCommand("", "/p/x.lua", 9, probe, false);
	CHECK(res.source == "detect:subl");
	CHECK(res.opensAtLine);
	REQUIRE_FALSE(res.argv.empty());
	CHECK(res.argv[0] == "subl");
	CHECK(res.argv.back() == "/p/x.lua:9");
}

TEST_CASE("resolveEditorCommand falls back to the platform opener",
	"[externaleditor]")
{
	auto none = [](std::string const&) { return false; };

	const EditorCommandResolution mac =
		resolveEditorCommand("", "/p/x.lua", 9, none, true);
	CHECK(mac.source == "opener");
	CHECK_FALSE(mac.opensAtLine);
	REQUIRE(mac.argv.size() == 2);
	CHECK(mac.argv[0] == "open");
	CHECK(mac.argv[1] == "/p/x.lua");

	const EditorCommandResolution linux =
		resolveEditorCommand("", "/p/x.lua", 9, none, false);
	CHECK(linux.argv[0] == "xdg-open");
}

TEST_CASE("readFileLinesAround returns a centred window", "[externaleditor]")
{
	const std::filesystem::path file =
		std::filesystem::temp_directory_path() / "orkige_peek_test.txt";
	{
		std::ofstream out(file);
		for (int i = 1; i <= 30; ++i)
		{
			out << "line " << i << "\n";
		}
	}

	int firstLine = -1;
	const std::vector<std::string> window =
		readFileLinesAround(file.string(), 15, 5, firstLine);
	CHECK(firstLine == 10);
	REQUIRE(window.size() == 11);	// lines 10..20 inclusive
	CHECK(window.front() == "line 10");
	CHECK(window[5] == "line 15");	// the target line
	CHECK(window.back() == "line 20");

	// near the start the window clamps to line 1
	int firstNearStart = -1;
	const std::vector<std::string> head =
		readFileLinesAround(file.string(), 2, 5, firstNearStart);
	CHECK(firstNearStart == 1);
	CHECK(head.front() == "line 1");

	std::filesystem::remove(file);
}

TEST_CASE("readFileLinesAround reports an unreadable file as empty",
	"[externaleditor]")
{
	int firstLine = -1;
	const std::vector<std::string> window = readFileLinesAround(
		"/no/such/orkige/file/anywhere.txt", 3, 5, firstLine);
	CHECK(window.empty());
	CHECK(firstLine == 0);
}
