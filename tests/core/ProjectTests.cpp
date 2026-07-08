/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	ProjectTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the project system (core_project/Project):
	manifest load/save round-trips, skeleton creation, validation
	failures with honest error messages, path resolution and scene
	discovery. The editor/player integration (resource locations,
	window title, play mode plumbing) is covered by the
	editor_project_play / player_example_project ctest runs.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_project/Project.h>
#include <core_project/NativeModule.h>

#include <filesystem>
#include <fstream>

namespace
{
	//! RAII temp directory below std::filesystem::temp_directory_path()
	struct TempDir
	{
		Orkige::String path;
		explicit TempDir(std::string const & name)
			: path((std::filesystem::temp_directory_path() / name).string())
		{
			std::filesystem::remove_all(this->path);
		}
		~TempDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->path, ignored);
		}
	};

	//! write raw text to a file (creating parent directories)
	void writeFile(std::string const & path, std::string const & content)
	{
		std::filesystem::create_directories(
			std::filesystem::path(path).parent_path());
		std::ofstream file(path, std::ios::trunc);
		file << content;
	}
}

TEST_CASE("Project::create builds the skeleton and load round-trips it",
	"[project]")
{
	Orkige::CoreTestEnvironment::get();
	TempDir dir("orkige_test_project_create");

	Orkige::Project created;
	Orkige::String error;
	REQUIRE(Orkige::Project::create(dir.path, "My Game", created, &error));
	INFO(error);
	REQUIRE(created.isLoaded());
	REQUIRE(created.getName() == "My Game");
	REQUIRE(created.getMainScene() == "scenes/main.oscene");

	// the skeleton really exists on disk
	REQUIRE(std::filesystem::is_directory(created.getScenesDirectory()));
	REQUIRE(std::filesystem::is_directory(created.getAssetsDirectory()));
	REQUIRE(std::filesystem::is_directory(created.getScriptsDirectory()));
	REQUIRE(std::filesystem::is_regular_file(
		std::filesystem::path(created.getRootDirectory()) /
		Orkige::Project::MANIFEST_FILE_NAME));

	// loading the directory gives the same project back
	Orkige::Project loaded;
	REQUIRE(loaded.load(dir.path, &error));
	REQUIRE(loaded.getName() == "My Game");
	REQUIRE(loaded.getMainScene() == "scenes/main.oscene");
	REQUIRE(loaded.getRootDirectory() == created.getRootDirectory());

	// ... and so does loading the manifest file itself
	Orkige::Project loadedFromManifest;
	REQUIRE(loadedFromManifest.load(
		(std::filesystem::path(dir.path) /
			Orkige::Project::MANIFEST_FILE_NAME).string(), &error));
	REQUIRE(loadedFromManifest.getName() == "My Game");
	REQUIRE(loadedFromManifest.getRootDirectory() ==
		created.getRootDirectory());

	// a second create on the same directory is refused honestly
	Orkige::Project duplicate;
	REQUIRE_FALSE(Orkige::Project::create(dir.path, "Again", duplicate,
		&error));
	REQUIRE(error.find("already an Orkige project") != Orkige::String::npos);
}

TEST_CASE("Project::create without a name borrows the folder name",
	"[project]")
{
	Orkige::CoreTestEnvironment::get();
	TempDir dir("orkige_test_project_autoname");

	Orkige::Project created;
	Orkige::String error;
	REQUIRE(Orkige::Project::create(dir.path, "", created, &error));
	REQUIRE(created.getName() == "orkige_test_project_autoname");
}

TEST_CASE("Project settings and main scene survive a save/load round-trip",
	"[project]")
{
	Orkige::CoreTestEnvironment::get();
	TempDir dir("orkige_test_project_roundtrip");

	Orkige::Project project;
	Orkige::String error;
	REQUIRE(Orkige::Project::create(dir.path, "Round Trip", project, &error));
	project.setMainScene("scenes/levels/boss.oscene");
	project.setSetting("export.android.package", "com.orkitec.roundtrip");
	project.setSetting("empty.value", "");
	REQUIRE(project.save(&error));

	Orkige::Project loaded;
	REQUIRE(loaded.load(dir.path, &error));
	REQUIRE(loaded.getName() == "Round Trip");
	REQUIRE(loaded.getMainScene() == "scenes/levels/boss.oscene");
	REQUIRE(loaded.getSettings().size() == 2);
	REQUIRE(loaded.hasSetting("export.android.package"));
	REQUIRE(loaded.getSetting("export.android.package") ==
		"com.orkitec.roundtrip");
	REQUIRE(loaded.hasSetting("empty.value"));
	REQUIRE(loaded.getSetting("empty.value").empty());
	REQUIRE(loaded.getSetting("no.such.key", "fallback") == "fallback");
}

TEST_CASE("Project::load fails honestly on invalid input", "[project]")
{
	Orkige::CoreTestEnvironment::get();
	TempDir dir("orkige_test_project_invalid");
	std::filesystem::create_directories(dir.path);
	Orkige::Project project;
	Orkige::String error;

	SECTION("a path that is neither a directory nor a .orkproj file")
	{
		REQUIRE_FALSE(project.load(
			(std::filesystem::path(dir.path) / "scene.oscene").string(),
			&error));
		REQUIRE(error.find("not a project") != Orkige::String::npos);
		REQUIRE_FALSE(project.isLoaded());
	}
	SECTION("a directory without a manifest")
	{
		REQUIRE_FALSE(project.load(dir.path, &error));
		REQUIRE(error.find("no project manifest") != Orkige::String::npos);
	}
	SECTION("a malformed manifest")
	{
		writeFile((std::filesystem::path(dir.path) /
			Orkige::Project::MANIFEST_FILE_NAME).string(),
			"<OrkigeProject version=\"1\"><Name>Broken");
		REQUIRE_FALSE(project.load(dir.path, &error));
		REQUIRE(error.find("could not parse") != Orkige::String::npos);
	}
	SECTION("a wrong root element")
	{
		writeFile((std::filesystem::path(dir.path) /
			Orkige::Project::MANIFEST_FILE_NAME).string(),
			"<SomethingElse version=\"1\"><Name>X</Name></SomethingElse>");
		REQUIRE_FALSE(project.load(dir.path, &error));
		REQUIRE(error.find("root element") != Orkige::String::npos);
	}
	SECTION("a manifest version newer than this build")
	{
		writeFile((std::filesystem::path(dir.path) /
			Orkige::Project::MANIFEST_FILE_NAME).string(),
			"<OrkigeProject version=\"999\"><Name>X</Name></OrkigeProject>");
		REQUIRE_FALSE(project.load(dir.path, &error));
		REQUIRE(error.find("version 999") != Orkige::String::npos);
	}
	SECTION("a manifest without a name")
	{
		writeFile((std::filesystem::path(dir.path) /
			Orkige::Project::MANIFEST_FILE_NAME).string(),
			"<OrkigeProject version=\"1\"></OrkigeProject>");
		REQUIRE_FALSE(project.load(dir.path, &error));
		REQUIRE(error.find("needs a name") != Orkige::String::npos);
	}
	SECTION("an absolute main scene path")
	{
		writeFile((std::filesystem::path(dir.path) /
			Orkige::Project::MANIFEST_FILE_NAME).string(),
			"<OrkigeProject version=\"1\"><Name>X</Name>"
			"<MainScene>/tmp/evil.oscene</MainScene></OrkigeProject>");
		REQUIRE_FALSE(project.load(dir.path, &error));
		REQUIRE(error.find("project-relative") != Orkige::String::npos);
	}
	SECTION("a setting without a key")
	{
		writeFile((std::filesystem::path(dir.path) /
			Orkige::Project::MANIFEST_FILE_NAME).string(),
			"<OrkigeProject version=\"1\"><Name>X</Name>"
			"<Settings><Setting value=\"orphan\"/></Settings>"
			"</OrkigeProject>");
		REQUIRE_FALSE(project.load(dir.path, &error));
		REQUIRE(error.find("without a key") != Orkige::String::npos);
	}
	SECTION("a failed load leaves a previously loaded project untouched")
	{
		TempDir goodDir("orkige_test_project_invalid_good");
		Orkige::Project good;
		REQUIRE(Orkige::Project::create(goodDir.path, "Good", good, &error));
		REQUIRE_FALSE(good.load(dir.path, &error));
		REQUIRE(good.isLoaded());
		REQUIRE(good.getName() == "Good");
	}
}

TEST_CASE("Project path resolution", "[project]")
{
	Orkige::CoreTestEnvironment::get();
	TempDir dir("orkige_test_project_paths");

	Orkige::Project project;
	Orkige::String error;
	REQUIRE(Orkige::Project::create(dir.path, "Paths", project, &error));
	const std::filesystem::path root(project.getRootDirectory());

	// the standard directories hang off the root
	REQUIRE(project.getScenesDirectory() == (root / "scenes").string());
	REQUIRE(project.getAssetsDirectory() == (root / "assets").string());
	REQUIRE(project.getScriptsDirectory() == (root / "scripts").string());
	REQUIRE(project.getMainScenePath() ==
		(root / "scenes" / "main.oscene").string());

	// relative resolves against the root, absolute passes through
	REQUIRE(project.resolvePath("assets/mesh.glb") ==
		(root / "assets" / "mesh.glb").string());
	REQUIRE(project.resolvePath("/somewhere/else.oscene") ==
		"/somewhere/else.oscene");

	// project-relative form (and "" for paths outside the project)
	REQUIRE(project.makeProjectRelative(
		(root / "scenes" / "main.oscene").string()) == "scenes/main.oscene");
	REQUIRE(project.makeProjectRelative(root.string()) == ".");
	REQUIRE(project.makeProjectRelative("/definitely/elsewhere").empty());

	// an unloaded project resolves nothing
	Orkige::Project unloaded;
	REQUIRE(unloaded.resolvePath("scenes/main.oscene").empty());
	REQUIRE(unloaded.getMainScenePath().empty());
	REQUIRE_FALSE(unloaded.isLoaded());

	// close() returns to the unloaded state
	project.close();
	REQUIRE_FALSE(project.isLoaded());
	REQUIRE(project.getName().empty());
}

TEST_CASE("NativeModule::configFromProject reads the native.* settings",
	"[project][native]")
{
	Orkige::CoreTestEnvironment::get();
	TempDir dir("orkige_test_project_native");

	Orkige::Project project;
	Orkige::String error;
	REQUIRE(Orkige::Project::create(dir.path, "Native", project, &error));

	SECTION("no native.* keys = disabled, generic-player behavior")
	{
		const Orkige::NativeModule::Config config =
			Orkige::NativeModule::configFromProject(project);
		REQUIRE_FALSE(config.enabled);
		REQUIRE(config.target.empty());
	}
	SECTION("native.target alone opts in, dirs get their defaults")
	{
		project.setSetting("native.target", "my_game");
		const Orkige::NativeModule::Config config =
			Orkige::NativeModule::configFromProject(project);
		REQUIRE(config.enabled);
		REQUIRE(config.target == "my_game");
		REQUIRE(config.cmakeDir == "native");
		REQUIRE(config.buildDir == "native/build");
	}
	SECTION("explicit dirs override the defaults")
	{
		project.setSetting("native.target", "my_game");
		project.setSetting("native.cmakeDir", "code");
		project.setSetting("native.buildDir", "code/out");
		const Orkige::NativeModule::Config config =
			Orkige::NativeModule::configFromProject(project);
		REQUIRE(config.enabled);
		REQUIRE(config.cmakeDir == "code");
		REQUIRE(config.buildDir == "code/out");
	}
	SECTION("an empty native.target stays disabled")
	{
		project.setSetting("native.target", "");
		REQUIRE_FALSE(
			Orkige::NativeModule::configFromProject(project).enabled);
	}
	SECTION("the native keys survive a manifest round-trip")
	{
		project.setSetting("native.target", "my_game");
		REQUIRE(project.save(&error));
		Orkige::Project loaded;
		REQUIRE(loaded.load(dir.path, &error));
		const Orkige::NativeModule::Config config =
			Orkige::NativeModule::configFromProject(loaded);
		REQUIRE(config.enabled);
		REQUIRE(config.target == "my_game");
	}
}

TEST_CASE("NativeModule build-command assembly", "[project][native]")
{
	Orkige::CoreTestEnvironment::get();

	SECTION("configureCommand carries generator, dirs and the ORKIGE_* vars")
	{
		const Orkige::StringVector command =
			Orkige::NativeModule::configureCommand("/opt/cmake",
				"/proj/native", "/proj/native/build", "/engine",
				"/engine/build/macos-debug", "Debug",
				{ "-DCMAKE_MAKE_PROGRAM=/opt/ninja" });
		const Orkige::StringVector expected = {
			"/opt/cmake", "-G", "Ninja",
			"-S", "/proj/native", "-B", "/proj/native/build",
			"-DCMAKE_BUILD_TYPE=Debug",
			"-DORKIGE_ROOT=/engine",
			"-DORKIGE_ENGINE_BUILD_DIR=/engine/build/macos-debug",
			"-DCMAKE_MAKE_PROGRAM=/opt/ninja",
		};
		REQUIRE(command == expected);
	}
	SECTION("buildCommand is the incremental cmake --build")
	{
		REQUIRE(Orkige::NativeModule::buildCommand("/opt/cmake",
			"/proj/native/build") ==
			Orkige::StringVector{ "/opt/cmake", "--build",
				"/proj/native/build" });
	}
	SECTION("executablePath appends the target to the build dir")
	{
		const Orkige::String path = Orkige::NativeModule::executablePath(
			"/proj/native/build", "my_game");
#ifdef _WIN32
		REQUIRE(path == "/proj/native/build\\my_game.exe");
#else
		REQUIRE(path == "/proj/native/build/my_game");
#endif
	}
	SECTION("needsConfigure is keyed on CMakeCache.txt")
	{
		TempDir dir("orkige_test_native_needs_configure");
		std::filesystem::create_directories(dir.path);
		REQUIRE(Orkige::NativeModule::needsConfigure(dir.path));
		writeFile((std::filesystem::path(dir.path) /
			"CMakeCache.txt").string(), "# cache");
		REQUIRE_FALSE(Orkige::NativeModule::needsConfigure(dir.path));
	}
}

TEST_CASE("Project::listScenes discovers .oscene files under scenes/",
	"[project]")
{
	Orkige::CoreTestEnvironment::get();
	TempDir dir("orkige_test_project_scenes");

	Orkige::Project project;
	Orkige::String error;
	REQUIRE(Orkige::Project::create(dir.path, "Scenes", project, &error));
	REQUIRE(project.listScenes().empty()); // nothing saved yet

	const std::filesystem::path scenes(project.getScenesDirectory());
	writeFile((scenes / "main.oscene").string(), "<XMLArchive/>");
	writeFile((scenes / "arena.oscene").string(), "<XMLArchive/>");
	writeFile((scenes / "levels" / "boss.oscene").string(), "<XMLArchive/>");
	writeFile((scenes / "notes.txt").string(), "not a scene");

	const Orkige::StringVector found = project.listScenes();
	REQUIRE(found.size() == 3);
	REQUIRE(found[0] == "scenes/arena.oscene");
	REQUIRE(found[1] == "scenes/levels/boss.oscene");
	REQUIRE(found[2] == "scenes/main.oscene");
}
