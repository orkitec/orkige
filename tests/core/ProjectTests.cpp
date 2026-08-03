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
		Orkige::NativeModule::EngineSdk engine;
		engine.kind = Orkige::NativeModule::EngineSdkKind::BuildTree;
		engine.root = "/engine";
		engine.buildDir = "/engine/build/macos-debug";
		engine.buildType = "Debug";
		const Orkige::StringVector command =
			Orkige::NativeModule::configureCommand("/opt/cmake",
				"/proj/native", "/proj/native/build", engine,
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
	SECTION("against a pack ORKIGE_ROOT is the pack and no build tree travels")
	{
		// a pack HAS no build tree, and its own build type is the one the
		// module must be configured in (the helper refuses a mismatch by name)
		Orkige::NativeModule::EngineSdk pack;
		pack.kind = Orkige::NativeModule::EngineSdkKind::Pack;
		pack.root = "/sdk/next";
		pack.buildType = "Release";
		const Orkige::StringVector command =
			Orkige::NativeModule::configureCommand("/opt/cmake",
				"/proj/native", "/proj/native/build-sdk-next", pack);
		REQUIRE(command == Orkige::StringVector{
			"/opt/cmake", "-G", "Ninja",
			"-S", "/proj/native", "-B", "/proj/native/build-sdk-next",
			"-DCMAKE_BUILD_TYPE=Release",
			"-DORKIGE_ROOT=/sdk/next",
		});
	}
	SECTION("a CROSS pack's toolchain file travels on the configure line")
	{
		// We ship the engine, never a toolchain - but a pack built FOR another
		// platform has to say what the machine's compiler must be told, and
		// CMake reads that only from a toolchain file, before it probes a
		// compiler at all. Without it the machine builds for itself and every
		// object misses the archives it is meant to link.
		Orkige::NativeModule::EngineSdk pack;
		pack.kind = Orkige::NativeModule::EngineSdkKind::Pack;
		pack.root = "/sdk/ios";
		pack.buildType = "Debug";
		pack.toolchainFile = "/sdk/ios/cmake/OrkigeSdkToolchain.cmake";
		const Orkige::StringVector command =
			Orkige::NativeModule::configureCommand("/opt/cmake",
				"/proj/native", "/proj/native/build-sdk-next", pack);
		REQUIRE(command == Orkige::StringVector{
			"/opt/cmake", "-G", "Ninja",
			"-S", "/proj/native", "-B", "/proj/native/build-sdk-next",
			"-DCMAKE_BUILD_TYPE=Debug",
			"-DORKIGE_ROOT=/sdk/ios",
			"-DCMAKE_TOOLCHAIN_FILE=/sdk/ios/cmake/OrkigeSdkToolchain.cmake",
		});
	}
	SECTION("the bundle is read as its own line, never derived")
	{
		// an app bundle is a DIRECTORY, and it is the thing that gets
		// installed, signed and packaged; the executable inside it is only a
		// file. The build writes both down.
		REQUIRE(Orkige::NativeModule::artifactBundleFromManifest(
			"shape=appbundle\n"
			"artifact=/b/my_game.app/my_game\n"
			"bundle=/b/my_game.app\n") == "/b/my_game.app");
		// a shape that builds no bundle writes no such line, and its absence
		// IS the answer
		REQUIRE(Orkige::NativeModule::artifactBundleFromManifest(
			"shape=executable\nartifact=/b/my_game\n").empty());
		REQUIRE(Orkige::NativeModule::artifactBundleFromManifest("").empty());
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
	SECTION("the artifact is the build's answer, not a guess")
	{
		// where a module lands is only "<buildDir>/<target>" on desktop: an
		// Android module is libmain.so, an Apple mobile module sits inside a
		// bundle. The build writes down what it produced and this reads it.
		REQUIRE(Orkige::NativeModule::artifactPathFromManifest(
			"target=my_game\nshape=sharedlib\n"
			"artifact=/proj/native/build/libmain.so\n",
			"/proj/native/build", "my_game") ==
			"/proj/native/build/libmain.so");
		// a bundle's inner executable, with the bundle recorded beside it
		REQUIRE(Orkige::NativeModule::artifactPathFromManifest(
			"shape=appbundle\n"
			"artifact=/b/my_game.app/Contents/MacOS/my_game\n"
			"bundle=/b/my_game.app\n",
			"/b", "my_game") == "/b/my_game.app/Contents/MacOS/my_game");
		// trailing carriage return (a manifest written on Windows) is not
		// part of the path
		REQUIRE(Orkige::NativeModule::artifactPathFromManifest(
			"artifact=C:/b/my_game.exe\r\n", "C:/b", "my_game") ==
			"C:/b/my_game.exe");
	}
	SECTION("no manifest leaves the desktop answer exactly as it was")
	{
		const Orkige::String fallback =
			Orkige::NativeModule::artifactPathFromManifest("",
				"/proj/native/build", "my_game");
		REQUIRE(fallback == Orkige::NativeModule::executablePath(
			"/proj/native/build", "my_game"));
		// a manifest without the key, and one with an empty value, fall back
		// too rather than answering with nothing
		REQUIRE(Orkige::NativeModule::artifactPathFromManifest(
			"target=my_game\n", "/proj/native/build", "my_game") == fallback);
		REQUIRE(Orkige::NativeModule::artifactPathFromManifest(
			"artifact=\n", "/proj/native/build", "my_game") == fallback);
	}
	SECTION("executablePath reads the manifest the build wrote")
	{
		TempDir dir("orkige_test_native_artifact");
		std::filesystem::create_directories(dir.path);
		writeFile((std::filesystem::path(dir.path) /
			Orkige::NativeModule::ARTIFACT_MANIFEST).string(),
			"target=my_game\nshape=sharedlib\n"
			"artifact=" + dir.path + "/libmain.so\n");
		REQUIRE(Orkige::NativeModule::executablePath(dir.path, "my_game") ==
			dir.path + "/libmain.so");
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
	SECTION("flavoredBuildDir suffixes the flavor so the two flavors never share a tree")
	{
		REQUIRE(Orkige::NativeModule::flavoredBuildDir("native/build", "next")
			== "native/build-next");
		REQUIRE(Orkige::NativeModule::flavoredBuildDir("native/build", "classic")
			== "native/build-classic");
		REQUIRE(Orkige::NativeModule::flavoredBuildDir("code/out", "next")
			!= Orkige::NativeModule::flavoredBuildDir("code/out", "classic"));
	}
	SECTION("a pack build gets its own module tree, never the tree build's")
	{
		Orkige::NativeModule::EngineSdk tree;
		tree.kind = Orkige::NativeModule::EngineSdkKind::BuildTree;
		Orkige::NativeModule::EngineSdk pack;
		pack.kind = Orkige::NativeModule::EngineSdkKind::Pack;
		REQUIRE(Orkige::NativeModule::moduleBuildDirectory("native/build", tree,
			"next") == "native/build-next");
		REQUIRE(Orkige::NativeModule::moduleBuildDirectory("native/build", pack,
			"next") == "native/build-sdk-next");
		REQUIRE(Orkige::NativeModule::moduleBuildDirectory("native/build", pack,
			"next") != Orkige::NativeModule::moduleBuildDirectory("native/build",
				tree, "next"));
	}
}

//! the SDK pack as the engine a native module builds against: what a
//! DOWNLOADED editor has, since it carries neither a repository nor a build
//! tree (@see Docs/sdk-pack.md)
TEST_CASE("NativeModule resolves the engine: build tree first, pack second",
	"[project][native][sdk]")
{
	Orkige::CoreTestEnvironment::get();

	//! write the two files that make a directory a pack, as the install writes
	//! them (cmake/OrkigeSdk.cmake realizes both through configure_file)
	auto stagePack = [](Orkige::String const & root,
		Orkige::String const & buildType, Orkige::String const & flavor)
	{
		std::filesystem::create_directories(
			std::filesystem::path(root) / "cmake");
		writeFile((std::filesystem::path(root) /
			Orkige::NativeModule::PACK_MARKER_FILE).string(),
			"include_guard(GLOBAL)\n"
			"set(ORKIGE_SDK_TARGET_PLATFORM \"macos\")\n"
			"set(ORKIGE_SDK_MODULE_SHAPE \"executable\")\n");
		writeFile((std::filesystem::path(root) /
			Orkige::NativeModule::PACK_CONFIG_FILE).string(),
			"set(ORKIGE_PACKAGE_KIND \"sdk\")\n"
			"set(ORKIGE_PACKAGE_RENDER_BACKEND \"" + flavor + "\")\n"
			"set(ORKIGE_PACKAGE_BUILD_TYPE \"" + buildType + "\")\n");
	};

	SECTION("cmakeSetValue reads one declaration out of a generated config")
	{
		const Orkige::String text =
			"# a comment mentioning set(ORKIGE_PACKAGE_BUILD_TYPE \"Debug\")\n"
			"set(ORKIGE_PACKAGE_KIND \"sdk\")\n"
			"set(ORKIGE_PACKAGE_BUILD_TYPE \"Release\")\n"
			"set(ORKIGE_PACKAGE_SANITIZERS OFF)\n"
			"set(ORKIGE_PACKAGE_SOURCE_ROOT \"\")\n";
		REQUIRE(Orkige::NativeModule::cmakeSetValue(text,
			"ORKIGE_PACKAGE_BUILD_TYPE") == "Release");
		REQUIRE(Orkige::NativeModule::cmakeSetValue(text,
			"ORKIGE_PACKAGE_KIND") == "sdk");
		// an unquoted value, an empty one, and a name that is only a prefix
		REQUIRE(Orkige::NativeModule::cmakeSetValue(text,
			"ORKIGE_PACKAGE_SANITIZERS") == "OFF");
		REQUIRE(Orkige::NativeModule::cmakeSetValue(text,
			"ORKIGE_PACKAGE_SOURCE_ROOT").empty());
		REQUIRE(Orkige::NativeModule::cmakeSetValue(text,
			"ORKIGE_PACKAGE").empty());
		REQUIRE(Orkige::NativeModule::cmakeSetValue(text, "NOT_THERE").empty());
	}
	SECTION("installedPackDirectory is per flavor under the writable state")
	{
		REQUIRE(Orkige::NativeModule::installedPackDirectory("/state", "next")
			== (std::filesystem::path("/state") / "sdk" / "next").string());
		REQUIRE(Orkige::NativeModule::installedPackDirectory("/state", "next")
			!= Orkige::NativeModule::installedPackDirectory("/state",
				"classic"));
		REQUIRE(Orkige::NativeModule::installedPackDirectory("", "next").empty());
	}
	SECTION("describePack reads the pack's own description of itself")
	{
		TempDir dir("orkige_test_native_pack");
		stagePack(dir.path, "Release", "next");
		const Orkige::NativeModule::EngineSdk pack =
			Orkige::NativeModule::describePack(dir.path);
		REQUIRE(pack.found());
		REQUIRE(pack.fromPack());
		REQUIRE(pack.root == dir.path);
		REQUIRE(pack.buildType == "Release");
		REQUIRE(pack.flavor == "next");
		REQUIRE(pack.platform == "macos");
		REQUIRE(pack.buildDir.empty());
	}
	SECTION("a directory without the marker is not a pack")
	{
		TempDir dir("orkige_test_native_notpack");
		std::filesystem::create_directories(
			std::filesystem::path(dir.path) / "cmake");
		// only the package config: a half-unpacked (or plain wrong) directory
		writeFile((std::filesystem::path(dir.path) /
			Orkige::NativeModule::PACK_CONFIG_FILE).string(),
			"set(ORKIGE_PACKAGE_BUILD_TYPE \"Release\")\n");
		REQUIRE_FALSE(Orkige::NativeModule::describePack(dir.path).found());
		REQUIRE_FALSE(Orkige::NativeModule::describePack("").found());
	}
	SECTION("a reachable build tree wins over an installed pack")
	{
		TempDir dir("orkige_test_native_resolve");
		const Orkige::String engineRoot =
			(std::filesystem::path(dir.path) / "engine").string();
		const Orkige::String engineBuild =
			(std::filesystem::path(engineRoot) / "build").string();
		const Orkige::String packRoot =
			(std::filesystem::path(dir.path) / "sdk").string();
		stagePack(packRoot, "Release", "next");

		// nothing reachable but the pack: the downloaded-editor case
		Orkige::NativeModule::EngineSdk sdk =
			Orkige::NativeModule::resolveEngineSdk(engineRoot, engineBuild,
				"Debug", packRoot);
		REQUIRE(sdk.fromPack());
		REQUIRE(sdk.buildType == "Release");

		// the developer case: a configured tree with the engine beside it
		std::filesystem::create_directories(
			std::filesystem::path(engineRoot) / "cmake");
		writeFile((std::filesystem::path(engineRoot) / "cmake" /
			"OrkigeGameModule.cmake").string(), "# helper");
		std::filesystem::create_directories(engineBuild);
		writeFile((std::filesystem::path(engineBuild) /
			"CMakeCache.txt").string(), "# cache");
		sdk = Orkige::NativeModule::resolveEngineSdk(engineRoot, engineBuild,
			"Debug", packRoot);
		REQUIRE(sdk.kind == Orkige::NativeModule::EngineSdkKind::BuildTree);
		REQUIRE(sdk.root == engineRoot);
		REQUIRE(sdk.buildDir == engineBuild);
		REQUIRE(sdk.buildType == "Debug");

		// neither: nothing to build against - the downloaded editor whose SDK
		// was never installed, which is a refusal rather than a guess
		REQUIRE_FALSE(Orkige::NativeModule::resolveEngineSdk("", "", "Debug",
			(std::filesystem::path(dir.path) / "absent").string()).found());
		REQUIRE_FALSE(Orkige::NativeModule::resolveEngineSdk("", "", "Debug",
			"").found());
		// an engine root whose build tree was never configured is not one
		// either (a checkout with no build in it)
		REQUIRE(Orkige::NativeModule::resolveEngineSdk(engineRoot,
			(std::filesystem::path(dir.path) / "nobuild").string(), "Debug",
			packRoot).fromPack());
	}
	SECTION("the toolchain prefers the baked paths, else searches the PATH")
	{
		TempDir dir("orkige_test_native_toolchain");
		const Orkige::String binDir =
			(std::filesystem::path(dir.path) / "bin").string();
		std::filesystem::create_directories(binDir);
#ifdef _WIN32
		const Orkige::String suffix = ".exe";
#else
		const Orkige::String suffix = "";
#endif
		writeFile((std::filesystem::path(binDir) / ("cmake" + suffix)).string(),
			"#!/bin/sh\n");
		writeFile((std::filesystem::path(binDir) / ("ninja" + suffix)).string(),
			"#!/bin/sh\n");
		const Orkige::StringVector path = { "/definitely/not/here", binDir };
		Orkige::NativeModule::Toolchain tools =
			Orkige::NativeModule::resolveToolchain("", "", path);
		REQUIRE(tools.complete());
		REQUIRE(tools.cmake ==
			(std::filesystem::path(binDir) / ("cmake" + suffix)).string());

		// a baked path that still exists wins (the developer case is untouched)
		const Orkige::String baked =
			(std::filesystem::path(binDir) / ("ninja" + suffix)).string();
		tools = Orkige::NativeModule::resolveToolchain(baked, baked, path);
		REQUIRE(tools.cmake == baked);

		// nothing anywhere
		tools = Orkige::NativeModule::resolveToolchain("/gone/cmake",
			"/gone/ninja", { "/definitely/not/here" });
		REQUIRE_FALSE(tools.complete());
		REQUIRE(tools.cmake.empty());
	}
	SECTION("searchPathDirectories splits a PATH and drops empty entries")
	{
#ifdef _WIN32
		const Orkige::StringVector split =
			Orkige::NativeModule::searchPathDirectories("C:\\a;;C:\\b");
#else
		const Orkige::StringVector split =
			Orkige::NativeModule::searchPathDirectories("/a::/b");
#endif
		REQUIRE(split.size() == 2);
		REQUIRE(Orkige::NativeModule::searchPathDirectories("").empty());
	}
	SECTION("a missing SDK and a missing toolchain are reported as two")
	{
		Orkige::NativeModule::EngineSdk none;
		Orkige::NativeModule::EngineSdk pack;
		pack.kind = Orkige::NativeModule::EngineSdkKind::Pack;
		pack.root = "/state/sdk/next";
		pack.flavor = "next";
		Orkige::NativeModule::Toolchain complete;
		complete.cmake = "/usr/bin/cmake";
		complete.makeProgram = "/usr/bin/ninja";

		// no SDK: an install problem, and the message says where it belongs
		const Orkige::String noSdk =
			Orkige::NativeModule::modulePrerequisiteProblem(none, complete,
				"next", "/state/sdk/next", "Jumper Native", "jumper_native");
		REQUIRE_FALSE(noSdk.empty());
		REQUIRE(noSdk.find("SDK") != Orkige::String::npos);
		REQUIRE(noSdk.find("/state/sdk/next") != Orkige::String::npos);
		REQUIRE(noSdk.find("cmake") == Orkige::String::npos);

		// no toolchain: a machine problem, named by what to install
		const Orkige::String noTools =
			Orkige::NativeModule::modulePrerequisiteProblem(pack,
				Orkige::NativeModule::Toolchain(), "next", "/state/sdk/next",
				"Jumper Native", "jumper_native");
		REQUIRE_FALSE(noTools.empty());
		REQUIRE(noTools.find("cmake") != Orkige::String::npos);
		REQUIRE(noTools.find("ninja") != Orkige::String::npos);
		REQUIRE(noTools != noSdk);

		// one missing program is named alone
		Orkige::NativeModule::Toolchain halfway;
		halfway.cmake = "/usr/bin/cmake";
		const Orkige::String noNinja =
			Orkige::NativeModule::modulePrerequisiteProblem(pack, halfway,
				"next", "/state/sdk/next", "Jumper Native", "jumper_native");
		REQUIRE(noNinja.find("ninja is not on the PATH") !=
			Orkige::String::npos);

		// the other flavor's pack is refused for what it is
		Orkige::NativeModule::EngineSdk otherFlavor = pack;
		otherFlavor.flavor = "classic";
		const Orkige::String wrongFlavor =
			Orkige::NativeModule::modulePrerequisiteProblem(otherFlavor,
				complete, "next", "/state/sdk/next", "Jumper Native",
				"jumper_native");
		REQUIRE(wrongFlavor.find("classic") != Orkige::String::npos);

		// everything in place: no sentence at all
		REQUIRE(Orkige::NativeModule::modulePrerequisiteProblem(pack, complete,
			"next", "/state/sdk/next", "Jumper Native", "jumper_native")
			.empty());
		// ...and a build tree needs no flavor check (it IS this build)
		Orkige::NativeModule::EngineSdk tree;
		tree.kind = Orkige::NativeModule::EngineSdkKind::BuildTree;
		REQUIRE(Orkige::NativeModule::modulePrerequisiteProblem(tree, complete,
			"next", "/state/sdk/next", "Jumper Native", "jumper_native")
			.empty());
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
