/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	AssetDatabaseTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the asset database
	(core_project/AssetDatabase): sidecar minting/import, rescan
	stability, rename-with/without-sidecar behavior, id/path
	lookups, the active-database serialization helpers and the
	component-level scene round-trip (id wins over a stale path,
	legacy id-less scenes keep loading). The engine components'
	wiring (Sprite/Model/ScriptComponent) is covered end to end by
	the player_assetid_selfcheck integration test.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_game/SceneSerializer.h>
#include <core_serialization/XMLArchive.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <unistd.h> // getpid - unique temp fixture names (parallel ctest!)

namespace
{
	//! RAII temp project directory (PID-suffixed - ctest runs in parallel)
	//! with the standard assets/ + scripts/ subdirectories
	struct TempProject
	{
		std::filesystem::path root;

		explicit TempProject(std::string const & name)
			: root(std::filesystem::temp_directory_path() /
				(name + "_" + std::to_string(::getpid())))
		{
			std::filesystem::remove_all(this->root);
			std::filesystem::create_directories(this->root / "assets");
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempProject()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		//! create a (fake) asset file, parents included; returns its
		//! absolute path
		std::string writeAsset(std::string const & relativePath,
			std::string const & content = "asset bytes")
		{
			const std::filesystem::path path = this->root / relativePath;
			std::filesystem::create_directories(path.parent_path());
			std::ofstream file(path, std::ios::trunc);
			file << content;
			return path.string();
		}
		std::string metaPath(std::string const & relativePath) const
		{
			return (this->root / relativePath).string() +
				Orkige::AssetDatabase::META_FILE_EXTENSION;
		}
	};

	//! whatever happens in a test, the process-wide active database must
	//! not leak into the next one
	struct ActiveDatabaseGuard
	{
		~ActiveDatabaseGuard()
		{
			Orkige::AssetDatabase::setActive(optr<Orkige::AssetDatabase>());
		}
	};

	bool isHex32(Orkige::String const & id)
	{
		if (id.size() != 32)
		{
			return false;
		}
		for (const char character : id)
		{
			if (!std::isxdigit(static_cast<unsigned char>(character)))
			{
				return false;
			}
		}
		return true;
	}
}

TEST_CASE("AssetDatabase fresh import mints sidecars and answers lookups",
	"[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_import");
	project.writeAsset("assets/textures/ball.png");
	project.writeAsset("scripts/player.lua", "-- lua");

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);

	// the sidecars were minted next to the assets
	REQUIRE(std::filesystem::is_regular_file(
		project.metaPath("assets/textures/ball.png")));
	REQUIRE(std::filesystem::is_regular_file(
		project.metaPath("scripts/player.lua")));
	REQUIRE(database.getAssetCount() == 2);

	// id/path/file-name lookups are consistent
	const Orkige::String ballId =
		database.idForPath("assets/textures/ball.png");
	const Orkige::String scriptId = database.idForPath("scripts/player.lua");
	REQUIRE(isHex32(ballId));
	REQUIRE(isHex32(scriptId));
	REQUIRE(ballId != scriptId);
	REQUIRE(database.pathForId(ballId) == "assets/textures/ball.png");
	REQUIRE(database.idForFileName("ball.png") == ballId);
	REQUIRE(database.fileNameForId(ballId) == "ball.png");
	REQUIRE(database.pathForId("no-such-id").empty());
	REQUIRE(database.idForPath("assets/no_such.png").empty());
	REQUIRE(database.idForFileName("no_such.png").empty());
}

TEST_CASE("AssetDatabase rescan is stable - same ids every time", "[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_stable");
	project.writeAsset("assets/ball.png");

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);
	const Orkige::String firstId = database.idForPath("assets/ball.png");
	REQUIRE(isHex32(firstId));

	database.refresh(project.root.string(), true);
	REQUIRE(database.idForPath("assets/ball.png") == firstId);

	// a second database instance reads the same identity off disk
	Orkige::AssetDatabase second;
	second.refresh(project.root.string(), true);
	REQUIRE(second.idForPath("assets/ball.png") == firstId);
}

TEST_CASE("AssetDatabase read-only refresh never writes sidecars",
	"[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_readonly");
	project.writeAsset("assets/ball.png");

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), false);
	// no sidecar = no id, and none was minted behind the project's back
	REQUIRE(database.getAssetCount() == 0);
	REQUIRE_FALSE(std::filesystem::exists(project.metaPath("assets/ball.png")));

	// existing sidecars ARE read in read-only mode
	Orkige::AssetDatabase importer;
	importer.refresh(project.root.string(), true);
	const Orkige::String mintedId = importer.idForPath("assets/ball.png");
	database.refresh(project.root.string(), false);
	REQUIRE(database.idForPath("assets/ball.png") == mintedId);
}

TEST_CASE("AssetDatabase rename WITH the sidecar keeps the id", "[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_rename_meta");
	project.writeAsset("assets/ball.png");

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);
	const Orkige::String originalId = database.idForPath("assets/ball.png");
	REQUIRE(isHex32(originalId));

	// move asset AND sidecar together (also into a subdirectory)
	std::filesystem::create_directories(project.root / "assets" / "sprites");
	std::filesystem::rename(project.root / "assets" / "ball.png",
		project.root / "assets" / "sprites" / "hero.png");
	std::filesystem::rename(project.metaPath("assets/ball.png"),
		project.metaPath("assets/sprites/hero.png"));

	database.refresh(project.root.string(), true);
	REQUIRE(database.getAssetCount() == 1);
	REQUIRE(database.pathForId(originalId) == "assets/sprites/hero.png");
	REQUIRE(database.idForFileName("hero.png") == originalId);
	REQUIRE(database.idForPath("assets/ball.png").empty());
}

TEST_CASE("AssetDatabase rename WITHOUT the sidecar mints a fresh id and "
	"drops the orphan", "[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_rename_orphan");
	project.writeAsset("assets/ball.png");

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);
	const Orkige::String originalId = database.idForPath("assets/ball.png");

	// the asset moves alone - its sidecar stays behind as an orphan
	std::filesystem::rename(project.root / "assets" / "ball.png",
		project.root / "assets" / "hero.png");

	database.refresh(project.root.string(), true);
	// no silent re-linking: the moved asset is a NEW asset
	const Orkige::String freshId = database.idForPath("assets/hero.png");
	REQUIRE(isHex32(freshId));
	REQUIRE(freshId != originalId);
	REQUIRE(database.pathForId(originalId).empty());
	// the orphaned sidecar was deleted, the fresh one exists
	REQUIRE_FALSE(std::filesystem::exists(project.metaPath("assets/ball.png")));
	REQUIRE(std::filesystem::is_regular_file(
		project.metaPath("assets/hero.png")));
}

TEST_CASE("AssetDatabase duplicated sidecar ids get re-minted on import",
	"[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_duplicate");
	project.writeAsset("assets/ball.png");

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);
	const Orkige::String originalId = database.idForPath("assets/ball.png");

	// the user copies asset + sidecar (Finder duplicate)
	std::filesystem::copy_file(project.root / "assets" / "ball.png",
		project.root / "assets" / "ball_copy.png");
	std::filesystem::copy_file(project.metaPath("assets/ball.png"),
		project.metaPath("assets/ball_copy.png"));

	database.refresh(project.root.string(), true);
	REQUIRE(database.getAssetCount() == 2);
	const Orkige::String copyId = database.idForPath("assets/ball_copy.png");
	REQUIRE(isHex32(copyId));
	REQUIRE(copyId != originalId);
	// the first (sorted) asset keeps the original identity
	REQUIRE(database.idForPath("assets/ball.png") == originalId);
	// the ids survive the NEXT rescan (the copy's sidecar was rewritten)
	database.refresh(project.root.string(), true);
	REQUIRE(database.idForPath("assets/ball.png") == originalId);
	REQUIRE(database.idForPath("assets/ball_copy.png") == copyId);
}

TEST_CASE("AssetDatabase invalid sidecars are re-minted on import",
	"[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_invalid_meta");
	project.writeAsset("assets/ball.png");
	// garbage where the sidecar should be
	{
		std::ofstream file(project.metaPath("assets/ball.png"));
		file << "this is not xml";
	}
	Orkige::String ignored;
	REQUIRE_FALSE(Orkige::AssetDatabase::readMetaFile(
		project.metaPath("assets/ball.png"), ignored));

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);
	const Orkige::String id = database.idForPath("assets/ball.png");
	REQUIRE(isHex32(id));
	// the sidecar is valid now
	Orkige::String reread;
	REQUIRE(Orkige::AssetDatabase::readMetaFile(
		project.metaPath("assets/ball.png"), reread));
	REQUIRE(reread == id);
}

TEST_CASE("AssetDatabase::importAsset registers a just-created file",
	"[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_import_one");
	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true); // empty project

	const std::string assetPath = project.writeAsset("assets/imported.glb");
	const Orkige::String id = database.importAsset(assetPath);
	REQUIRE(isHex32(id));
	REQUIRE(database.pathForId(id) == "assets/imported.glb");
	REQUIRE(database.idForFileName("imported.glb") == id);
	REQUIRE(std::filesystem::is_regular_file(
		project.metaPath("assets/imported.glb")));
	// importing again reuses the sidecar id
	REQUIRE(database.importAsset(assetPath) == id);
	// project-relative input works too
	REQUIRE(database.importAsset("assets/imported.glb") == id);
	// a path outside the project is refused honestly
	REQUIRE(database.importAsset("/definitely/elsewhere.png").empty());
}

TEST_CASE("AssetDatabase sidecar v2: texture import block round-trips and an "
	"id-only sidecar still reads clean", "[assetdb][texture]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_texturev2");
	const std::string metaPath = project.metaPath("assets/hero.png");
	std::filesystem::create_directories(
		std::filesystem::path(metaPath).parent_path());

	SECTION("a v2 sidecar preserves the id AND round-trips the settings")
	{
		Orkige::TextureImport written;
		written.base.filter = "point";
		written.base.wrap = "wrap";
		written.base.maxSize = 1024;
		written.base.premultiply = true;
		written.base.generateMips = true;
		written.hasAndroid = true;
		written.android = written.base;
		written.android.maxSize = 512;		// mobile caps harder
		written.hasIos = true;
		written.ios = written.base;
		written.ios.filter = "bilinear";	// iOS overrides just the filter

		const Orkige::String id = "1234567890abcdef1234567890abcdef";
		REQUIRE(Orkige::AssetDatabase::writeMetaFile(metaPath, id, written));

		// the id reads back through the plain (v1) reader - back-compat: a
		// settings-carrying sidecar is still a valid id sidecar
		Orkige::String readId;
		REQUIRE(Orkige::AssetDatabase::readMetaFile(metaPath, readId));
		CHECK(readId == id);

		Orkige::TextureImport read;
		REQUIRE(Orkige::AssetDatabase::readImportSettings(metaPath, read));
		CHECK(read.base.filter == "point");
		CHECK(read.base.wrap == "wrap");
		CHECK(read.base.maxSize == 1024);
		CHECK(read.base.premultiply == true);
		CHECK(read.base.generateMips == true);
		REQUIRE(read.hasAndroid);
		CHECK(read.android.maxSize == 512);
		CHECK(read.android.filter == "point");		// inherited from base
		REQUIRE(read.hasIos);
		CHECK(read.ios.filter == "bilinear");		// overridden
		CHECK(read.ios.maxSize == 1024);			// inherited from base

		// resolvedFor() picks the platform block when present, else base
		CHECK(read.resolvedFor("").maxSize == 1024);
		CHECK(read.resolvedFor("android").maxSize == 512);
		CHECK(read.resolvedFor("ios").filter == "bilinear");
		CHECK(read.resolvedFor("windows").maxSize == 1024);	// unknown = base
	}
	SECTION("an id-only v1 sidecar reads clean and yields no import block")
	{
		const Orkige::String id = "fedcba0987654321fedcba0987654321";
		REQUIRE(Orkige::AssetDatabase::writeMetaFile(metaPath, id));
		Orkige::String readId;
		REQUIRE(Orkige::AssetDatabase::readMetaFile(metaPath, readId));
		CHECK(readId == id);
		// no <texture> block: readImportSettings reports false and leaves
		// defaults, so a live sprite keeps its default sampler
		Orkige::TextureImport read;
		CHECK_FALSE(Orkige::AssetDatabase::readImportSettings(metaPath, read));
		CHECK(read.base.filter == "bilinear");
		CHECK(read.base.maxSize == 0);
		CHECK_FALSE(read.hasAndroid);
	}
}

TEST_CASE("AssetDatabase::metaFilePathForId resolves a texture's sidecar",
	"[assetdb][texture]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_metapath");
	project.writeAsset("assets/hero.png");
	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);
	const Orkige::String id = database.idForFileName("hero.png");
	REQUIRE(isHex32(id));
	const Orkige::String metaPath = database.metaFilePathForId(id);
	REQUIRE_FALSE(metaPath.empty());
	CHECK(std::filesystem::is_regular_file(metaPath));
	CHECK(metaPath == project.metaPath("assets/hero.png"));
	CHECK(database.metaFilePathForId("no-such-id").empty());
}

TEST_CASE("AssetDatabase::generateId yields 128-bit hex ids", "[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	const Orkige::String first = Orkige::AssetDatabase::generateId();
	const Orkige::String second = Orkige::AssetDatabase::generateId();
	REQUIRE(isHex32(first));
	REQUIRE(isHex32(second));
	REQUIRE(first != second);
}

TEST_CASE("AssetDatabase serialization helpers resolve against the active "
	"database", "[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	ActiveDatabaseGuard guard;
	TempProject project("orkige_test_assetdb_resolve");
	project.writeAsset("assets/hero.png");
	project.writeAsset("scripts/player.lua", "-- lua");

	optr<Orkige::AssetDatabase> database =
		Orkige::onew(new Orkige::AssetDatabase());
	database->refresh(project.root.string(), true);
	const Orkige::String heroId = database->idForFileName("hero.png");
	const Orkige::String scriptId = database->idForPath("scripts/player.lua");

	SECTION("no active database: value and id carry exactly as read")
	{
		Orkige::String value = "stale.png";
		Orkige::String id = heroId;
		Orkige::AssetDatabase::resolveReference(value, id,
			Orkige::AssetDatabase::REF_FILE_NAME);
		CHECK(value == "stale.png");
		CHECK(id == heroId);
		CHECK(Orkige::AssetDatabase::referenceIdForValue("stale.png", heroId,
			Orkige::AssetDatabase::REF_FILE_NAME) == heroId);
	}
	SECTION("a resolving id wins over a stale value")
	{
		Orkige::AssetDatabase::setActive(database);
		Orkige::String value = "ball.png"; // the pre-rename name
		Orkige::String id = heroId;
		Orkige::AssetDatabase::resolveReference(value, id,
			Orkige::AssetDatabase::REF_FILE_NAME);
		CHECK(value == "hero.png");
		CHECK(id == heroId);

		Orkige::String path = "scripts/old_name.lua";
		Orkige::String pathId = scriptId;
		Orkige::AssetDatabase::resolveReference(path, pathId,
			Orkige::AssetDatabase::REF_PROJECT_PATH);
		CHECK(path == "scripts/player.lua");
	}
	SECTION("an unresolvable id falls back to the value and self-heals")
	{
		Orkige::AssetDatabase::setActive(database);
		Orkige::String value = "hero.png";
		Orkige::String id = "00000000000000000000000000000000";
		Orkige::AssetDatabase::resolveReference(value, id,
			Orkige::AssetDatabase::REF_FILE_NAME);
		CHECK(value == "hero.png");
		CHECK(id == heroId); // healed to the real asset's id
	}
	SECTION("an unknown value keeps the stored id (missing-asset case)")
	{
		Orkige::AssetDatabase::setActive(database);
		Orkige::String value = "engine_builtin.mesh";
		Orkige::String id = "00000000000000000000000000000000";
		Orkige::AssetDatabase::resolveReference(value, id,
			Orkige::AssetDatabase::REF_FILE_NAME);
		CHECK(value == "engine_builtin.mesh");
		CHECK(id == "00000000000000000000000000000000");
		CHECK(Orkige::AssetDatabase::referenceIdForValue(
			"engine_builtin.mesh", id,
			Orkige::AssetDatabase::REF_FILE_NAME) == id);
	}
	SECTION("save-side id refreshes from the active database")
	{
		Orkige::AssetDatabase::setActive(database);
		CHECK(Orkige::AssetDatabase::referenceIdForValue("hero.png", "",
			Orkige::AssetDatabase::REF_FILE_NAME) == heroId);
		CHECK(Orkige::AssetDatabase::referenceIdForValue("scripts/player.lua",
			"stale-id", Orkige::AssetDatabase::REF_PROJECT_PATH) == scriptId);
		CHECK(Orkige::AssetDatabase::referenceIdForValue("", "any-id",
			Orkige::AssetDatabase::REF_FILE_NAME).empty());
	}
}

TEST_CASE("Project::load activates a read-only asset database, close "
	"deactivates it", "[assetdb][project]")
{
	Orkige::CoreTestEnvironment::get();
	ActiveDatabaseGuard guard;
	TempProject dir("orkige_test_assetdb_project");
	std::filesystem::remove_all(dir.root); // Project::create builds the tree

	Orkige::Project created;
	Orkige::String error;
	REQUIRE(Orkige::Project::create(dir.root.string(), "AssetDb", created,
		&error));
	const std::filesystem::path asset =
		std::filesystem::path(created.getAssetsDirectory()) / "ball.png";
	{
		std::ofstream file(asset);
		file << "asset bytes";
	}

	Orkige::Project project;
	REQUIRE(project.load(dir.root.string(), &error));
	REQUIRE(project.getAssetDatabase());
	REQUIRE(Orkige::AssetDatabase::getActive() == project.getAssetDatabase());
	// the load-time refresh is READ-ONLY: no sidecar appeared
	REQUIRE_FALSE(std::filesystem::exists(asset.string() +
		Orkige::AssetDatabase::META_FILE_EXTENSION));
	REQUIRE(project.getAssetDatabase()->getAssetCount() == 0);

	// the editor's import pass mints the sidecars
	project.importAssets();
	REQUIRE(std::filesystem::is_regular_file(asset.string() +
		Orkige::AssetDatabase::META_FILE_EXTENSION));
	REQUIRE(project.getAssetDatabase()->getAssetCount() == 1);
	const Orkige::String id =
		project.getAssetDatabase()->idForPath("assets/ball.png");
	REQUIRE(isHex32(id));

	project.close();
	REQUIRE_FALSE(Orkige::AssetDatabase::getActive());
	REQUIRE_FALSE(project.getAssetDatabase());
}

TEST_CASE("Asset references round-trip through a scene: id wins over a "
	"stale path, legacy scenes keep loading", "[assetdb][scene]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::registerOrkigeTestComponents();
	ActiveDatabaseGuard guard;
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();

	TempProject project("orkige_test_assetdb_scene");
	project.writeAsset("scripts/player.lua", "-- lua");
	const std::string scenePath =
		(project.root / "scenes_main.oscene").string();

	optr<Orkige::AssetDatabase> database =
		Orkige::onew(new Orkige::AssetDatabase());
	database->refresh(project.root.string(), true);
	const Orkige::String assetId = database->idForPath("scripts/player.lua");
	REQUIRE(isHex32(assetId));

	SECTION("the id survives a rename and fixes the stale path up on load")
	{
		// save a scene that references the asset (id captured via the
		// active database)
		Orkige::AssetDatabase::setActive(database);
		{
			optr<Orkige::GameObject> hero =
				manager.createGameObject("Hero").lock();
			REQUIRE(hero);
			REQUIRE(hero->addComponent<Orkige::TestAssetRefComponent>());
			hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->setAssetReference("scripts/player.lua");
			CHECK(hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->getAssetId() == assetId);
		}
		REQUIRE(Orkige::SceneSerializer::saveScene(scenePath, manager));
		manager.clear();

		// the asset gets renamed WITH its sidecar - the saved scene's path
		// is stale now
		std::filesystem::rename(project.root / "scripts" / "player.lua",
			project.root / "scripts" / "hero.lua");
		std::filesystem::rename(project.metaPath("scripts/player.lua"),
			project.metaPath("scripts/hero.lua"));
		database->refresh(project.root.string(), true);
		REQUIRE(database->pathForId(assetId) == "scripts/hero.lua");

		REQUIRE(Orkige::SceneSerializer::loadScene(scenePath, manager));
		optr<Orkige::GameObject> hero = manager.getGameObject("Hero").lock();
		REQUIRE(hero);
		Orkige::TestAssetRefComponent* component =
			hero->getComponentPtr<Orkige::TestAssetRefComponent>();
		REQUIRE(component);
		// the id resolved - the stale path was fixed up in memory
		CHECK(component->getAssetPath() == "scripts/hero.lua");
		CHECK(component->getAssetId() == assetId);
	}
	SECTION("a legacy id-less scene loads via the path (and self-heals)")
	{
		// save WITHOUT any active database - the classic pre-id scene
		{
			optr<Orkige::GameObject> hero =
				manager.createGameObject("Hero").lock();
			REQUIRE(hero);
			REQUIRE(hero->addComponent<Orkige::TestAssetRefComponent>());
			hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->setAssetReference("scripts/player.lua");
			CHECK(hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->getAssetId().empty());
		}
		REQUIRE(Orkige::SceneSerializer::saveScene(scenePath, manager));
		manager.clear();

		SECTION("no database on load either (standalone scene)")
		{
			REQUIRE(Orkige::SceneSerializer::loadScene(scenePath, manager));
			optr<Orkige::GameObject> hero =
				manager.getGameObject("Hero").lock();
			REQUIRE(hero);
			CHECK(hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->getAssetPath() == "scripts/player.lua");
			CHECK(hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->getAssetId().empty());
		}
		SECTION("with a database the reference self-heals to the asset's id")
		{
			Orkige::AssetDatabase::setActive(database);
			REQUIRE(Orkige::SceneSerializer::loadScene(scenePath, manager));
			optr<Orkige::GameObject> hero =
				manager.getGameObject("Hero").lock();
			REQUIRE(hero);
			CHECK(hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->getAssetPath() == "scripts/player.lua");
			CHECK(hero->getComponentPtr<Orkige::TestAssetRefComponent>()
				->getAssetId() == assetId);
		}
	}
	manager.clear();
}

TEST_CASE("AssetDatabase path lookups: normalization-robust, case-sensitive "
	"(the cross-platform contract)", "[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_portability");
	project.writeAsset("assets/sub/tile.png");

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);

	const Orkige::String tileId = database.idForPath("assets/sub/tile.png");
	REQUIRE(!tileId.empty());
	// keys are the lexically-normal generic form - other spellings of the
	// same path must resolve identically on every platform
	CHECK(database.idForPath("./assets/sub/tile.png") == tileId);
	CHECK(database.idForPath("assets//sub/tile.png") == tileId);
	CHECK(database.idForPath("assets/sub/../sub/tile.png") == tileId);
	// deliberately CASE-SENSITIVE everywhere: a case-insensitive host
	// filesystem (macOS default) must not hide a spelling that would break
	// on Linux/Android - the database refuses it on macOS too
	CHECK(database.idForPath("assets/sub/Tile.png").empty());
	CHECK(database.idForFileName("Tile.png").empty());
	CHECK(database.idForFileName("tile.png") == tileId);
}

TEST_CASE("AssetDatabase::importAsset rejects paths outside the project root",
	"[assetdb]")
{
	Orkige::CoreTestEnvironment::get();
	TempProject project("orkige_test_assetdb_containment");
	project.writeAsset("assets/inside.png");
	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), true);

	// a file genuinely outside the root (the temp dir above it)
	const std::filesystem::path outside =
		project.root.parent_path() /
		("orkige_test_outside_" + std::to_string(::getpid()) + ".png");
	{
		std::ofstream file(outside, std::ios::trunc);
		file << "outside bytes";
	}
	CHECK(database.importAsset(outside.string()).empty());
	// relative traversal out of the root
	CHECK(database.importAsset("../outside.png").empty());
	// inside stays accepted (control)
	CHECK(!database.importAsset("assets/inside.png").empty());
	std::error_code ignored;
	std::filesystem::remove(outside, ignored);
}
