/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	PlayerBundleTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Unit tests for PlayerBundle (engine_runtime/PlayerRuntime.h) - the
// exported-app default-project mechanism: an exported bundle carries an
// orkige_project.txt marker next to its resources naming the bundled
// project, plus (optionally) the engine Media/ directory. Pure filesystem
// logic, exercised against a temp directory fixture; the SDL_GetBasePath
// default only kicks in for an empty baseDir and is not needed here.
#include <catch2/catch_test_macros.hpp>
#include <engine_runtime/PlayerRuntime.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#include <process.h>	// _getpid - unique temp fixture names (parallel ctest)
#define getpid _getpid
#else
#include <unistd.h> // getpid - unique temp fixture name
#endif

namespace
{
	//! a throwaway bundle-base directory, wiped on destruction
	struct BundleFixture
	{
		std::filesystem::path base;

		BundleFixture()
		{
			base = std::filesystem::temp_directory_path() /
				("orkige_playerbundle_test_" +
					std::to_string(::getpid()));
			std::filesystem::remove_all(base);
			std::filesystem::create_directories(base);
		}
		~BundleFixture()
		{
			std::error_code ignored;
			std::filesystem::remove_all(base, ignored);
		}
		void writeMarker(std::string const& content)
		{
			std::ofstream marker(base /
				Orkige::PlayerBundle::PROJECT_MARKER_FILE_NAME);
			marker << content;
		}
	};
}

TEST_CASE("no marker means no bundled project (the dev-run case)",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	CHECK(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string()).empty());
}

TEST_CASE("marker resolves the bundled project against the base directory",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	std::filesystem::create_directories(fixture.base / "project");
	fixture.writeMarker("project\n");
	// with and without the trailing separator on the base dir. Compared as
	// PATHS, not strings: the resolver joins with '/', std::filesystem with
	// the platform's preferred separator - the same path either way.
	CHECK(std::filesystem::path(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string())) == fixture.base / "project");
	CHECK(std::filesystem::path(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string() + "/")) == fixture.base / "project");
}

TEST_CASE("marker tolerates trailing whitespace and CRLF",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	std::filesystem::create_directories(fixture.base / "project");
	fixture.writeMarker("project\r\n");
	CHECK(std::filesystem::path(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string())) == fixture.base / "project");
}

TEST_CASE("marker naming a missing path is an honest miss",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	fixture.writeMarker("no_such_project\n");
	CHECK(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string()).empty());
}

TEST_CASE("empty marker yields no bundled project", "[engine][playerbundle]")
{
	BundleFixture fixture;
	fixture.writeMarker("\n");
	CHECK(Orkige::PlayerBundle::findBundledProject(
		fixture.base.string()).empty());
}

TEST_CASE("bundled media overrides the fallback only when Media/Main exists",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	// no Media at all -> the build-tree fallback wins
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback",
		fixture.base.string()) == "/dev/fallback");
	// a Media dir without Main/ is not the engine media - still the fallback
	std::filesystem::create_directories(fixture.base / "Media");
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback",
		fixture.base.string()) == "/dev/fallback");
	// the real classic bundled layout wins (Media/Main = RTSS library)
	std::filesystem::create_directories(fixture.base / "Media" / "Main");
	CHECK(std::filesystem::path(Orkige::PlayerBundle::resolveMediaDirectory(
		"/dev/fallback", fixture.base.string())) == fixture.base / "Media");
}

TEST_CASE("bundled next-flavor media (Media/Hlms) also overrides the fallback",
	"[engine][playerbundle]")
{
	BundleFixture fixture;
	// the Ogre-Next flavor bundles its Hlms shader templates instead of the
	// classic Main/RTShaderLib set - Media/Hlms alone marks a bundled Media
	std::filesystem::create_directories(fixture.base / "Media" / "Hlms");
	CHECK(std::filesystem::path(Orkige::PlayerBundle::resolveMediaDirectory(
		"/dev/fallback", fixture.base.string())) == fixture.base / "Media");
}

TEST_CASE("empty base directory falls through safely",
	"[engine][playerbundle]")
{
	// an explicit "" asks for SDL_GetBasePath(); whatever it returns, a
	// test runner's directory carries no marker - the call must not throw
	// and the media resolution must keep the fallback
	CHECK(Orkige::PlayerBundle::resolveMediaDirectory("/dev/fallback")
		== "/dev/fallback");
}

TEST_CASE("PlayerArguments parses the shared player CLI contract",
	"[engine][playerbundle]")
{
	// scene + --project + --debug-port + --orientation (the manifest's
	// export.orientation delivered explicitly when the manifest itself does
	// not travel to the device - the editor's Android play sessions)
	const char* argv[] = { "player", "scene.oscene", "--project", "/proj",
		"--debug-port", "4242", "--orientation", "auto" };
	const Orkige::PlayerArguments arguments = Orkige::PlayerArguments::parse(
		static_cast<int>(std::size(argv)), const_cast<char**>(argv));
	CHECK(arguments.valid);
	CHECK(arguments.scenePath == "scene.oscene");
	CHECK(arguments.projectPath == "/proj");
	CHECK(arguments.debugRequested);
	CHECK(arguments.debugPort == 4242);
	CHECK(arguments.orientation == "auto");
}

TEST_CASE("PlayerArguments defaults leave the orientation to the manifest",
	"[engine][playerbundle]")
{
	const char* argv[] = { "player", "scene.oscene" };
	const Orkige::PlayerArguments arguments = Orkige::PlayerArguments::parse(
		static_cast<int>(std::size(argv)), const_cast<char**>(argv));
	CHECK(arguments.valid);
	CHECK(arguments.orientation.empty());
	// an unknown argument still reports honestly
	const char* badArgv[] = { "player", "--rotate" };
	const Orkige::PlayerArguments bad = Orkige::PlayerArguments::parse(
		static_cast<int>(std::size(badArgv)), const_cast<char**>(badArgv));
	CHECK_FALSE(bad.valid);
	CHECK(bad.unknownArgument == "--rotate");
}

//--- the mount-versus-extract rule ------------------------------------------
// A packaged file may be MOUNTED (read in place from the archive) only when
// EVERY runtime reader of it goes through the resource system. It must be
// EXTRACTED when a reader opens it BY PATH (fopen/tinyxml2) or when it is
// DISCOVERED by walking a directory - a mounted entry is neither a file handle
// nor a directory entry. The rule is ONE function so the Android `stored` APK
// and the browser game pak cannot drift apart, and these cases name every
// extension it decides on, with the reason.

TEST_CASE("bulk media inside the packaged sub-trees mounts in place",
	"[engine][playerbundle][mount]")
{
	using Orkige::PlayerBundle::isMountedMediaPath;
	// the three sub-trees a package carries bulk media in: an exported
	// project's own assets, and the sample media beside the dev player
	CHECK(isMountedMediaPath("project/assets/ball.png"));
	CHECK(isMountedMediaPath("assets/crate.png"));
	CHECK(isMountedMediaPath("jumper_media/player.png"));
	// nested paths are inside the sub-tree too
	CHECK(isMountedMediaPath("project/assets/textures/tiles/grass.png"));

	// every asset kind the engine reads THROUGH the resource system
	// (readResourceText / openResource / a resource-name texture-mesh-sound
	// load) - content, not documents, so mounting them is the whole point
	for (const char * name : { "project/assets/ground.omat",
		"project/assets/blob.oshape", "project/assets/tower.omesh",
		"project/assets/hero.oanim", "project/assets/hud.oatlas",
		"project/assets/coin.osfx", "project/assets/thud.sfs",
		"project/assets/hud.oui", "project/assets/gui_default.ogui",
		"project/assets/helper.lua", "project/assets/level.glb",
		"project/assets/blip.ogg", "project/assets/step.wav",
		"project/assets/sky.dds", "project/assets/tile.oitd" })
	{
		INFO(name);
		CHECK(isMountedMediaPath(name));
	}
}

TEST_CASE("path-opened and directory-discovered kinds are always extracted",
	"[engine][playerbundle][mount]")
{
	using Orkige::PlayerBundle::isMountedMediaPath;
	// .oprefab: PrefabSerializer opens it through XMLArchive (tinyxml2, by
	// path). Mounted, every prefab instance in a scene loads CHILDLESS.
	CHECK_FALSE(isMountedMediaPath("project/assets/tile.oprefab"));
	CHECK_FALSE(isMountedMediaPath("assets/wall_block.oprefab"));
	CHECK_FALSE(isMountedMediaPath("jumper_media/crate.oprefab"));
}

TEST_CASE("asset id sidecars are not a packaged kind at all",
	"[engine][playerbundle][mount]")
{
	using Orkige::PlayerBundle::isMountedMediaPath;
	// A package carries NO .orkmeta: sidecars are editor bookkeeping, and the
	// export bakes the one runtime-relevant answer (a texture's sampler) into
	// the payload manifest. So the rule says nothing about them - the question
	// "mount or extract?" never arises, and a stale exclusion pretending it
	// does would only hide a payload that should not contain one.
	CHECK(isMountedMediaPath("project/assets/ball.png.orkmeta"));
	CHECK(isMountedMediaPath("assets/crate.png.orkmeta"));
	// outside the media sub-trees nothing is mounted anyway
	CHECK_FALSE(isMountedMediaPath("project/scenes/main.oscene.orkmeta"));
}

TEST_CASE("the exclusion list is keyed on extension, not on convention",
	"[engine][playerbundle][mount]")
{
	using Orkige::PlayerBundle::isMountedMediaPath;
	// Scenes, the manifest, the config assets and the localisation tables
	// normally live OUTSIDE a media sub-tree, so convention alone would keep
	// them out of the archive. But a manifest points at its config assets by
	// an arbitrary path string, so an author can put one under assets/ - and
	// each of these degrades SILENTLY when it cannot be read, which is exactly
	// the failure mode worth making impossible. Hence: extension-keyed.
	for (const char * name : { "project/assets/level1.oscene",
		"project/assets/game.orkproj", "project/assets/levels.olevels",
		"project/assets/input.oactions", "project/assets/physics.olayers",
		"project/assets/en.xlf", "assets/de.xlf",
		"jumper_media/physics.olayers" })
	{
		INFO(name);
		CHECK_FALSE(isMountedMediaPath(name));
	}
}

TEST_CASE("everything outside the media sub-trees is extracted wholesale",
	"[engine][playerbundle][mount]")
{
	using Orkige::PlayerBundle::isMountedMediaPath;
	// the fopen tree: the manifest, scenes (XMLArchive), scripts, the config
	// assets and the localisation tables (StringTable opens .xlf by path)
	for (const char * name : { "project/game.orkproj",
		"project/scenes/level1.oscene", "project/scripts/player.lua",
		"project/input.oactions", "project/physics.olayers",
		"project/levels.olevels", "project/loc/en.xlf",
		"orkige_project.txt", "orkige_mount.txt", "orkige_assets.txt",
		"example.oscene" })
	{
		INFO(name);
		CHECK_FALSE(isMountedMediaPath(name));
	}
	// the engine shader/font/effect media: a directory tree the shader
	// loaders want as real files
	for (const char * name : { "Media/Main/RTShaderLib.material",
		"Media/RTShaderLib/GLSL/SGXLib_Common.glsl",
		"Media/fonts/Nunito-Regular.ttf", "Media/water/water_plane.glb",
		"Media/Hlms/Pbs/Any/Main.piece_ps.any" })
	{
		INFO(name);
		CHECK_FALSE(isMountedMediaPath(name));
	}
	// a sub-tree name must MATCH at the head, not merely resemble one
	CHECK_FALSE(isMountedMediaPath("assets_backup/ball.png"));
	CHECK_FALSE(isMountedMediaPath("other/project/assets/ball.png"));
	CHECK_FALSE(isMountedMediaPath(""));
}
