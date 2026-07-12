/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	EditorScriptToolsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the EDITOR SCRIPT TOOLS support in the
	UI-independent editor layer:
	- discovery (EditorScriptTools): the *.editor.lua filename/label parsing
	  and the recursive scan (dedupe + sort + rescan), all pure filesystem
	  logic that also holds in ORKIGE_SCRIPTING=OFF builds;
	- the one-undo SCRIPT TRANSACTION on EditorCore (beginScriptTransaction /
	  endScriptTransaction): commit folds a run's commands into ONE undo step,
	  abort rolls them all back leaving no partial edits.
	The sandbox EXECUTION of a tool (the editor.* verb table) needs a booted
	editor and is covered by the editor_scripts integration selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <EditorCore.h>
#include <EditorScriptTools.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace
{
	//! the shared headless singleton world, cleared fresh per test (the
	//! transaction tests use log-only ProbeCommands, so the world stays empty)
	Orkige::GameObjectManager& freshWorld()
	{
		Orkige::CoreTestEnvironment& env = Orkige::CoreTestEnvironment::get();
		env.gameObjectManager.clear();
		return env.gameObjectManager;
	}

	//! a log-only undoable command (no engine needed): execute appends
	//! "<tag>:do", unexecute "<tag>:undo" - enough to prove the transaction
	//! folds/rolls back in the right order
	class ProbeCommand : public Orkige::EditorCommand
	{
	public:
		ProbeCommand(std::vector<std::string>& log, std::string tag)
			: mLog(log), mTag(std::move(tag)) {}
		bool execute(Orkige::EditorCore&) override
		{
			mLog.push_back(mTag + ":do");
			return true;
		}
		bool unexecute(Orkige::EditorCore&) override
		{
			mLog.push_back(mTag + ":undo");
			return true;
		}
		Orkige::String getDescription() const override { return mTag; }
	private:
		std::vector<std::string>& mLog;
		std::string mTag;
	};

	//! a unique temp directory for a scan fixture (parallel-ctest safe)
	std::filesystem::path makeTempDir(std::string const& tag)
	{
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() /
			("orkige_editortools_" + tag + "_" + std::to_string(getpid()) + "_" +
				std::to_string(std::chrono::steady_clock::now()
					.time_since_epoch().count()));
		std::filesystem::create_directories(dir);
		return dir;
	}

	void writeFile(std::filesystem::path const& path, std::string const& content)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream out(path);
		out << content;
	}
}

TEST_CASE("editorToolNameForFile derives the name only from *.editor.lua",
	"[editor][tools]")
{
	CHECK(Orkige::editorToolNameForFile("retag_tiles.editor.lua") ==
		"retag_tiles");
	CHECK(Orkige::editorToolNameForFile("scripts/a/border_walls.editor.lua") ==
		"border_walls");
	// a plain library, a component script, or another file is NOT a tool
	CHECK(Orkige::editorToolNameForFile("helpers.lua").empty());
	CHECK(Orkige::editorToolNameForFile("player.component.lua").empty());
	CHECK(Orkige::editorToolNameForFile("readme.txt").empty());
	CHECK(Orkige::editorToolNameForFile(".editor.lua").empty());
}

TEST_CASE("editorToolDefaultLabel title-cases the tool name", "[editor][tools]")
{
	CHECK(Orkige::editorToolDefaultLabel("retag_tiles") == "Retag Tiles");
	CHECK(Orkige::editorToolDefaultLabel("border-walls") == "Border Walls");
	CHECK(Orkige::editorToolDefaultLabel("cleanup") == "Cleanup");
}

TEST_CASE("editorToolLabelOverride reads the first-line -- tool: comment",
	"[editor][tools]")
{
	const std::filesystem::path dir = makeTempDir("label");
	const std::filesystem::path withLabel = dir / "a.editor.lua";
	writeFile(withLabel, "-- tool:   Frame The Level  \nlocal x = 1\n");
	CHECK(Orkige::editorToolLabelOverride(withLabel.string()) ==
		"Frame The Level");

	const std::filesystem::path noLabel = dir / "b.editor.lua";
	writeFile(noLabel, "-- an ordinary comment\neditor.log('hi')\n");
	CHECK(Orkige::editorToolLabelOverride(noLabel.string()).empty());

	const std::filesystem::path noComment = dir / "c.editor.lua";
	writeFile(noComment, "editor.log('hi')\n");
	CHECK(Orkige::editorToolLabelOverride(noComment.string()).empty());

	std::filesystem::remove_all(dir);
}

TEST_CASE("scanEditorTools discovers, labels, dedupes and sorts",
	"[editor][tools]")
{
	const std::filesystem::path dir = makeTempDir("scan");
	// a labelled tool, an unlabelled tool (name-derived label), a nested tool,
	// a component script + a plain library (both IGNORED), and a duplicate name
	// in a subfolder (first-found wins)
	writeFile(dir / "zeta.editor.lua", "-- tool: Alpha Tool\nreturn 0\n");
	writeFile(dir / "border_walls.editor.lua", "return 0\n");
	writeFile(dir / "sub" / "helper_tool.editor.lua", "-- tool: Helper\n");
	writeFile(dir / "player.component.lua", "return 0\n");
	writeFile(dir / "helpers.lua", "return 0\n");

	std::vector<Orkige::EditorScriptTool> tools =
		Orkige::scanEditorTools(dir.string());
	REQUIRE(tools.size() == 3);
	// sorted by label: "Alpha Tool" < "Border Walls" < "Helper"
	CHECK(tools[0].label == "Alpha Tool");
	CHECK(tools[0].name == "zeta");
	CHECK(tools[1].label == "Border Walls");
	CHECK(tools[1].name == "border_walls");
	CHECK(tools[2].label == "Helper");
	CHECK(tools[2].name == "helper_tool");
	// every path is absolute and points at a real file
	for (Orkige::EditorScriptTool const& tool : tools)
	{
		CHECK(std::filesystem::path(tool.path).is_absolute());
		CHECK(std::filesystem::exists(tool.path));
	}

	// RESCAN picks up a freshly written tool (the write-triggered rescan path)
	writeFile(dir / "cleanup.editor.lua", "return 0\n");
	tools = Orkige::scanEditorTools(dir.string());
	CHECK(tools.size() == 4);

	// a missing directory scans to nothing (honest, not an error)
	CHECK(Orkige::scanEditorTools((dir / "does_not_exist").string()).empty());

	std::filesystem::remove_all(dir);
}

TEST_CASE("EditorCore script transaction folds a run into ONE undo step",
	"[editor][tools]")
{
	Orkige::EditorCore core(freshWorld());
	std::vector<std::string> log;

	CHECK_FALSE(core.inScriptTransaction());
	core.beginScriptTransaction();
	CHECK(core.inScriptTransaction());
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "A"))));
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "B"))));
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "C"))));
	// three commands are on the stack DURING the run
	CHECK(core.getUndoStackSize() == 3);
	const std::size_t folded =
		core.endScriptTransaction(true, "Run Tool: Demo");
	CHECK_FALSE(core.inScriptTransaction());
	CHECK(folded == 3);
	// they collapse into ONE undo step carrying the tool description
	CHECK(core.getUndoStackSize() == 1);
	CHECK(core.getUndoDescription() == "Run Tool: Demo");

	// ONE undo reverts the WHOLE run, children unwound in reverse order
	log.clear();
	REQUIRE(core.undo());
	CHECK(log == std::vector<std::string>{ "C:undo", "B:undo", "A:undo" });
	CHECK_FALSE(core.canUndo());
	CHECK(core.canRedo());

	// ONE redo re-applies the whole run forward
	log.clear();
	REQUIRE(core.redo());
	CHECK(log == std::vector<std::string>{ "A:do", "B:do", "C:do" });
	CHECK(core.getUndoStackSize() == 1);
}

TEST_CASE("EditorCore script transaction abort rolls back with NO partial edits",
	"[editor][tools]")
{
	Orkige::EditorCore core(freshWorld());
	std::vector<std::string> log;

	// a prior, committed edit exists on the stack before the tool runs
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "prior"))));
	CHECK(core.getUndoStackSize() == 1);

	core.beginScriptTransaction();
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "A"))));
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "B"))));
	log.clear();
	// the run failed: abort rolls the run's commands back (latest first) and
	// drops them - leaving ONLY the prior edit, no partial tool edits
	const std::size_t rolled = core.endScriptTransaction(false, "Run Tool: Bad");
	CHECK(rolled == 2);
	CHECK(log == std::vector<std::string>{ "B:undo", "A:undo" });
	CHECK(core.getUndoStackSize() == 1);
	CHECK(core.getUndoDescription() == "prior");
}

TEST_CASE("EditorCore script transaction with no edits pushes nothing",
	"[editor][tools]")
{
	Orkige::EditorCore core(freshWorld());
	core.beginScriptTransaction();
	// a tool that reads only (or does nothing) executes no command
	const std::size_t folded = core.endScriptTransaction(true, "Run Tool: Noop");
	CHECK(folded == 0);
	CHECK(core.getUndoStackSize() == 0);
	CHECK_FALSE(core.canUndo());
}
