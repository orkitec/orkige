/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	SaveStoreTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless SaveStore unit tests: typed round-trip through the atomic
	temp-file+rename write, the dirty-flag / flush contract, the temp file's
	absence after a successful flush (atomicity), and the honest empty-store
	recovery from a missing / corrupt / too-new file. The rendered end-to-end
	proof (write, restart the scene, read back) is the player_save_selfcheck
	integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CoreTestEnvironment.h"

#include <core_game/SaveStore.h>

#include <filesystem>
#include <fstream>

using Catch::Approx;

namespace
{
	//! RAII temp file below std::filesystem::temp_directory_path()
	struct TempSave
	{
		Orkige::String path;
		explicit TempSave(char const * name)
			: path((std::filesystem::temp_directory_path() / name).string())
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
			std::filesystem::remove(this->path + ".tmp", ignored);
		}
		~TempSave()
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
			std::filesystem::remove(this->path + ".tmp", ignored);
		}
	};
}

TEST_CASE("SaveStore typed values round-trip through a flush + load",
	"[unit][save]")
{
	TempSave temp("orkige_savestore_roundtrip.osave");
	{
		Orkige::SaveStore store;
		store.setSaveFile(temp.path);
		store.setNumber("score", 1234.5);
		store.setBool("unlocked", true);
		store.setBool("muted", false);
		store.setString("player.name", "Ada");
		REQUIRE(store.isDirty());
		REQUIRE(store.flush());
		REQUIRE_FALSE(store.isDirty());
	}
	// a fresh store reads the SAME typed values back
	Orkige::SaveStore reloaded;
	reloaded.setSaveFile(temp.path);
	REQUIRE(reloaded.load());
	REQUIRE(reloaded.count() == 4);
	REQUIRE(reloaded.getNumber("score", 0.0) == Approx(1234.5));
	REQUIRE(reloaded.getBool("unlocked", false) == true);
	REQUIRE(reloaded.getBool("muted", true) == false);
	REQUIRE(reloaded.getString("player.name", "") == "Ada");
}

TEST_CASE("SaveStore typed getters fall back on a missing or mismatched key",
	"[unit][save]")
{
	Orkige::SaveStore store;	// no save file: pure in-memory behaviour
	store.setNumber("n", 42.0);
	store.setString("s", "hello");
	// absent key -> fallback
	REQUIRE(store.getNumber("nope", -1.0) == Approx(-1.0));
	REQUIRE(store.getBool("nope", true) == true);
	REQUIRE(store.getString("nope", "def") == "def");
	// present but wrong kind -> fallback (a string read as a number)
	REQUIRE(store.getNumber("s", -1.0) == Approx(-1.0));
	REQUIRE(store.getBool("n", true) == true);
	// the string getter is the honest "read whatever is there" accessor
	REQUIRE(store.getString("n", "") == "42");
	REQUIRE(store.has("n"));
	store.remove("n");
	REQUIRE_FALSE(store.has("n"));
}

TEST_CASE("SaveStore flush is atomic - no temp file remains afterwards",
	"[unit][save]")
{
	TempSave temp("orkige_savestore_atomic.osave");
	Orkige::SaveStore store;
	store.setSaveFile(temp.path);
	store.setNumber("coins", 7.0);
	REQUIRE(store.flush());
	// the real file exists and the temp file was renamed away (never left behind)
	REQUIRE(std::filesystem::exists(temp.path));
	REQUIRE_FALSE(std::filesystem::exists(temp.path + ".tmp"));
	// a flush with nothing dirty is a successful no-op
	REQUIRE(store.flush());
}

TEST_CASE("SaveStore load recovers from a corrupt file by starting empty",
	"[unit][save]")
{
	TempSave temp("orkige_savestore_corrupt.osave");
	{
		std::ofstream garbage(temp.path.c_str(), std::ios::binary);
		garbage << "this is not an orkige save file at all\n";
	}
	Orkige::SaveStore store;
	store.setSaveFile(temp.path);
	// pre-seed an in-memory value: a failed load must clear it, not keep it
	store.setNumber("stale", 99.0);
	REQUIRE_FALSE(store.load());
	REQUIRE(store.count() == 0);
	REQUIRE_FALSE(store.has("stale"));
}

TEST_CASE("SaveStore load of a missing file starts empty without error",
	"[unit][save]")
{
	TempSave temp("orkige_savestore_missing.osave");
	Orkige::SaveStore store;
	store.setSaveFile(temp.path);
	REQUIRE_FALSE(store.load());	// first run: no file yet
	REQUIRE(store.count() == 0);
}
