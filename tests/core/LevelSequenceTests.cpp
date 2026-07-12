/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	LevelSequenceTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The levels.olevels asset round-trip (core_game/LevelSequence) and the
	progression save round-trip (core_game/LevelManager) - headless.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_game/LevelSequence.h>
#include <core_game/LevelManager.h>
#include <core_game/SaveStore.h>

#include <filesystem>
#include <fstream>

namespace
{
	Orkige::String tempPath(std::string const & name)
	{
		const Orkige::String path =
			(std::filesystem::temp_directory_path() / name).string();
		std::filesystem::remove(path);
		return path;
	}
}

TEST_CASE("LevelSequence round-trips a levels.olevels file", "[level]")
{
	const Orkige::String path = tempPath("orkige_levels_test.olevels");

	Orkige::LevelSequence written;
	written.addEntry(Orkige::LevelSequence::Entry("scenes/main.oscene",
		"First Slide", 1));
	written.addEntry(Orkige::LevelSequence::Entry("scenes/level2.oscene",
		"Straight Shot", 0));
	REQUIRE(written.save(path));

	Orkige::LevelSequence read;
	REQUIRE(read.load(path));
	REQUIRE(read.getCount() == 2);
	REQUIRE(read.getScenePath(0) == "scenes/main.oscene");
	REQUIRE(read.getName(0) == "First Slide");
	REQUIRE(read.getPar(0) == 1);
	REQUIRE(read.getScenePath(1) == "scenes/level2.oscene");
	REQUIRE(read.getName(1) == "Straight Shot");
	REQUIRE(read.getPar(1) == 0);
	// out-of-range access is honest
	REQUIRE(read.getScenePath(2).empty());
	REQUIRE(read.getPar(-1) == 0);

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("LevelSequence refuses a non-olevels file", "[level]")
{
	const Orkige::String path = tempPath("orkige_levels_bad.olevels");
	{
		std::ofstream bogus(path, std::ios::binary);
		bogus << "not an orkige archive";
	}
	Orkige::LevelSequence sequence;
	REQUIRE_FALSE(sequence.load(path));

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("LevelManager persists resume index and best moves", "[level]")
{
	// progression rides the shared SaveStore ("level.*" keys). SaveStore and
	// LevelManager are both Singletons (one alive at a time), so the writer and
	// the reader each live in their own scope, sharing only the on-disk file.
	const Orkige::String path = tempPath("orkige_progress_test.osave");
	{
		Orkige::SaveStore store;
		store.setSaveFile(path);
		Orkige::LevelManager manager;

		// an empty store is the honest fresh state
		REQUIRE(manager.resumeLevel() == 0);
		REQUIRE(manager.bestMoves(0) == -1);

		manager.setResumeLevel(2);
		manager.recordBestMoves(0, 5);
		manager.recordBestMoves(1, 3);
		// recordBestMoves keeps the minimum
		manager.recordBestMoves(0, 9);
		manager.recordBestMoves(0, 4);
		// saveProgress flushes the shared store atomically
		REQUIRE(manager.saveProgress());
	}

	{
		Orkige::SaveStore store;
		store.setSaveFile(path);
		REQUIRE(store.load());
		Orkige::LevelManager reloaded;
		REQUIRE(reloaded.resumeLevel() == 2);
		REQUIRE(reloaded.bestMoves(0) == 4);
		REQUIRE(reloaded.bestMoves(1) == 3);
		REQUIRE(reloaded.bestMoves(2) == -1);
	}

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("LevelManager progression is an honest no-op without a SaveStore",
	"[level]")
{
	// no SaveStore alive: the accessors answer with the fresh-state defaults and
	// the mutators/saveProgress do nothing (the editor / scriptless-run stance)
	Orkige::LevelManager manager;
	REQUIRE(manager.resumeLevel() == 0);
	REQUIRE(manager.bestMoves(0) == -1);
	manager.setResumeLevel(3);
	manager.recordBestMoves(0, 7);
	REQUIRE(manager.resumeLevel() == 0);
	REQUIRE(manager.bestMoves(0) == -1);
	REQUIRE_FALSE(manager.saveProgress());
}

TEST_CASE("LevelManager queues and consumes a deferred load request", "[level]")
{
	Orkige::LevelManager manager;
	manager.sequence().addEntry(Orkige::LevelSequence::Entry(
		"scenes/main.oscene", "First Slide", 1));
	manager.sequence().addEntry(Orkige::LevelSequence::Entry(
		"scenes/level2.oscene", "Straight Shot", 0));

	REQUIRE_FALSE(manager.hasPendingLoad());
	// an out-of-range level is ignored (no request queued)
	manager.loadLevel(5);
	REQUIRE_FALSE(manager.hasPendingLoad());

	manager.loadLevel(1);
	REQUIRE(manager.hasPendingLoad());
	// re-entrancy guard: a second request while one is pending is dropped
	manager.loadLevel(0);
	manager.loadScenePath("scenes/other.oscene");

	int index = -99;
	Orkige::String scene;
	REQUIRE(manager.consumePendingLoad(index, scene));
	REQUIRE(index == 1);
	REQUIRE(scene == "scenes/level2.oscene");
	REQUIRE_FALSE(manager.hasPendingLoad());
	// a second consume finds nothing
	REQUIRE_FALSE(manager.consumePendingLoad(index, scene));

	// a raw path request carries index -1
	manager.loadScenePath("scenes/other.oscene");
	REQUIRE(manager.consumePendingLoad(index, scene));
	REQUIRE(index == -1);
	REQUIRE(scene == "scenes/other.oscene");
}
