// ScriptBreakpointStoreTests - the editor's per-project breakpoint store:
// set/clear/toggle semantics, the wire list, and persistence under a temp
// project root (.orkige/breakpoints, reloaded on attach). Headless.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include <catch2/catch_test_macros.hpp>

#include "ScriptBreakpoints.h"

#include <filesystem>
#include <fstream>

using namespace Orkige;

namespace
{
	struct TempRoot
	{
		std::filesystem::path root;
		explicit TempRoot(std::string const& name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root);
		}
		~TempRoot()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
	};
}

TEST_CASE("breakpoint store set/clear/toggle and the wire list",
	"[editor][debug]")
{
	ScriptBreakpointStore store;	// detached: works, just never persists
	CHECK(store.list().empty());
	CHECK(store.set("scripts/player.lua", 12));
	CHECK_FALSE(store.set("scripts/player.lua", 12));	// idempotent
	CHECK(store.set("scripts\\player.lua", 30));	// normalizes separators
	CHECK(store.set("scripts/enemy.lua", 5));
	CHECK(store.has("scripts/player.lua", 30));
	// stable (file, line) order in list + wire form
	REQUIRE(store.list().size() == 3);
	const std::vector<String> wire = store.wireList();
	REQUIRE(wire.size() == 3);
	CHECK(wire[0] == "scripts/enemy.lua:5");
	CHECK(wire[1] == "scripts/player.lua:12");
	CHECK(wire[2] == "scripts/player.lua:30");
	// the per-file gutter query
	const std::vector<int> lines = store.linesFor("scripts/player.lua");
	REQUIRE(lines.size() == 2);
	// toggle: off then on again, reporting the NEW state
	CHECK_FALSE(store.toggle("scripts/player.lua", 12));
	CHECK_FALSE(store.has("scripts/player.lua", 12));
	CHECK(store.toggle("scripts/player.lua", 12));
	// garbage refuses
	CHECK_FALSE(store.set("", 3));
	CHECK_FALSE(store.set("scripts/a.lua", 0));
	// clearAll + revision moves on every change
	const unsigned int revision = store.revision();
	CHECK(store.clearAll());
	CHECK(store.revision() > revision);
	CHECK_FALSE(store.clearAll());
	CHECK(store.list().empty());
}

TEST_CASE("breakpoint store persists per project root", "[editor][debug]")
{
	TempRoot project("orkige_breakpoint_store_test");
	{
		ScriptBreakpointStore store;
		store.attachProject(project.root.string());
		store.set("scripts/player.lua", 12);
		store.set("scripts/enemy.lua", 5);
		store.clear("scripts/enemy.lua", 5);
	}
	// the store file exists under .orkige/ and a fresh attach reloads it
	CHECK(std::filesystem::exists(project.root / ".orkige" / "breakpoints"));
	{
		ScriptBreakpointStore store;
		store.attachProject(project.root.string());
		REQUIRE(store.list().size() == 1);
		CHECK(store.has("scripts/player.lua", 12));
		CHECK_FALSE(store.has("scripts/enemy.lua", 5));
		// detaching clears and stops persisting
		store.attachProject("");
		CHECK(store.list().empty());
		store.set("scripts/loose.lua", 2);	// in-memory only
	}
	{
		// the detached mutation never leaked into the project's store
		ScriptBreakpointStore store;
		store.attachProject(project.root.string());
		REQUIRE(store.list().size() == 1);
	}
	// a hand-edited file with junk lines loads the valid entries only
	{
		std::ofstream file(project.root / ".orkige" / "breakpoints",
			std::ios::trunc);
		file << "scripts/a.lua:3\n" << "garbage line\n" << "\n"
			<< "scripts/b.lua:7\r\n";
	}
	{
		ScriptBreakpointStore store;
		store.attachProject(project.root.string());
		REQUIRE(store.list().size() == 2);
		CHECK(store.has("scripts/a.lua", 3));
		CHECK(store.has("scripts/b.lua", 7));
	}
}
