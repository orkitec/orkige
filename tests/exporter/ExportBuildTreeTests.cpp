/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportBuildTreeTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	What an export reads out of the build tree it packages.

	Two of these answers decide whether the produced app works at all: the
	FLAVOR picks which engine media gets bundled (bundle the wrong one and the
	app boots to a blank window), and the ARCHITECTURE pins a native module's
	build (get it wrong and the module's objects will not link against the
	engine libraries at all). Both are derived, never asked for, so both are
	asserted here against real cache text.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportBuildTree.h"
#include "ExportFiles.h"

#include <filesystem>

using namespace OrkigeExport;

namespace
{
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_tree_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	void writeCache(Orkige::String const & tree, Orkige::String const & body)
	{
		REQUIRE(ExportFiles::writeTextFile(
			ExportFiles::join(tree, "CMakeCache.txt"), body, 0));
	}
}

TEST_CASE("a cache line yields its value", "[unit][export]")
{
	CHECK(parseCMakeCacheLine("ORKIGE_RENDER_BACKEND:STRING=next",
		"ORKIGE_RENDER_BACKEND") == "next");
	CHECK(parseCMakeCacheLine("CMAKE_BUILD_TYPE:STRING=Release ",
		"CMAKE_BUILD_TYPE") == "Release");
	// a different entry must not answer, and neither must a longer name that
	// merely starts the same way
	CHECK(parseCMakeCacheLine("CMAKE_BUILD_TYPE:STRING=Release",
		"CMAKE_BUILD").empty());
	CHECK(parseCMakeCacheLine("ORKIGE_RENDER_BACKEND_CONFIGURED:BOOL=ON",
		"ORKIGE_RENDER_BACKEND").empty());
	CHECK(parseCMakeCacheLine("// a comment", "CMAKE_BUILD_TYPE").empty());
	CHECK(parseCMakeCacheLine("", "CMAKE_BUILD_TYPE").empty());
}

TEST_CASE("the cache is read out of a tree", "[unit][export]")
{
	ScratchDir scratch("cache");
	writeCache(scratch.path,
		"// comment\n"
		"CMAKE_BUILD_TYPE:STRING=Debug\n"
		"ORKIGE_RENDER_BACKEND:STRING=next\n");
	CHECK(readCMakeCache(scratch.path, "CMAKE_BUILD_TYPE") == "Debug");
	CHECK(readCMakeCache(scratch.path, "ORKIGE_RENDER_BACKEND") == "next");
	CHECK(readCMakeCache(scratch.path, "NOT_THERE").empty());
	// a tree that is not one answers nothing rather than throwing
	CHECK(readCMakeCache(ExportFiles::join(scratch.path, "nope"),
		"CMAKE_BUILD_TYPE").empty());
}

TEST_CASE("the render flavor defaults to classic", "[unit][export]")
{
	ScratchDir scratch("flavor");
	writeCache(scratch.path, "ORKIGE_RENDER_BACKEND:STRING=next\n");
	CHECK(renderBackend(scratch.path) == "next");

	// a cache that names no backend is the historical default; so is a tree
	// that has no cache at all
	writeCache(scratch.path, "CMAKE_BUILD_TYPE:STRING=Debug\n");
	CHECK(renderBackend(scratch.path) == "classic");
	CHECK(renderBackend(ExportFiles::join(scratch.path, "absent")) ==
		"classic");
}

TEST_CASE("each tree names its own release sibling", "[unit][export]")
{
	// the sibling is derived from the tree's NAME, so everything the name
	// encodes is carried over: the host it targets and the flavor it is bound
	// to. Build trees are flavor-bound, so a next export reaching for the
	// classic release tree would link the other backend entirely - and a Linux
	// export reaching for a macOS one would name a directory that is not there.
	CHECK(releaseTreeName("macos-debug") == "macos-release");
	CHECK(releaseTreeName("macos-debug-classic") == "macos-release-classic");
	CHECK(releaseTreeName("linux-debug-next") == "linux-release-next");
	CHECK(releaseTreeName("linux-debug-classic") == "linux-release-classic");
	CHECK(releaseTreeName("windows-debug") == "windows-release");
	// the build type is a whole hyphen-separated component, never a substring
	// somebody's directory happens to contain
	CHECK(releaseTreeName("my-debugger-tree").empty());
	// ...and a tree whose name describes no build type has no sibling to name,
	// rather than one belonging to a different build
	CHECK(releaseTreeName("scratch").empty());
	CHECK(releaseTreeName("").empty());

	ScratchDir scratch("sibling");
	const Orkige::String tree = ExportFiles::join(scratch.path, "macos-debug");
	REQUIRE(ExportFiles::makeDirectories(tree, 0));
	writeCache(tree, "ORKIGE_RENDER_BACKEND:STRING=next\n");
	CHECK(siblingReleaseTree(tree) ==
		ExportFiles::join(scratch.path, "macos-release"));

	const Orkige::String classicTree =
		ExportFiles::join(scratch.path, "macos-debug-classic");
	REQUIRE(ExportFiles::makeDirectories(classicTree, 0));
	writeCache(classicTree, "ORKIGE_RENDER_BACKEND:STRING=classic\n");
	CHECK(siblingReleaseTree(classicTree) ==
		ExportFiles::join(scratch.path, "macos-release-classic"));

	const Orkige::String linuxTree =
		ExportFiles::join(scratch.path, "linux-debug-next");
	REQUIRE(ExportFiles::makeDirectories(linuxTree, 0));
	writeCache(linuxTree, "ORKIGE_RENDER_BACKEND:STRING=next\n");
	CHECK(siblingReleaseTree(linuxTree) ==
		ExportFiles::join(scratch.path, "linux-release-next"));

	// a separator-terminated root still names the sibling BESIDE the tree, not
	// beside its parent
	CHECK(siblingReleaseTree(tree + "/") ==
		ExportFiles::join(scratch.path, "macos-release"));

	// no sibling to name is answered with nothing, so the caller keeps the
	// tree it was given instead of warning about a directory nobody meant
	CHECK(siblingReleaseTree(
		ExportFiles::join(scratch.path, "scratch")).empty());
}

TEST_CASE("a test build packages the tree it was pointed at", "[unit][export]")
{
	// SHIPPING SPEED is the only reason the substitution exists, so it holds
	// for every ordinary export from a non-Release tree...
	CHECK(prefersSiblingReleasePlayer("Debug", false));
	CHECK(prefersSiblingReleasePlayer("RelWithDebInfo", false));
	CHECK(prefersSiblingReleasePlayer("", false));
	// ...and never when the tree already IS the release one
	CHECK_FALSE(prefersSiblingReleasePlayer("Release", false));

	// A TEST BUILD asks whether the runtime THIS tree produced still passes
	// the project's suite once packaged, so it is answered by that tree and no
	// other - whatever build type either one carries. A sibling is a different
	// build of a different commit, and one older than the packaged test build
	// does not act on the marker's run-tests directive at all: it plays the
	// game, forever, and the suite never reaches a verdict.
	CHECK_FALSE(prefersSiblingReleasePlayer("Debug", true));
	CHECK_FALSE(prefersSiblingReleasePlayer("Release", true));
	CHECK_FALSE(prefersSiblingReleasePlayer("", true));
}

TEST_CASE("the architecture comes off the triplet", "[unit][export]")
{
	CHECK(tripletArchitecture("arm64-osx") == "arm64");
	CHECK(tripletArchitecture("arm64-ios-simulator") == "arm64");
	CHECK(tripletArchitecture("x64-windows-static-md") == "x86_64");
	CHECK(tripletArchitecture("x86_64-linux") == "x86_64");
	// an unknown triplet reports nothing, which the module build turns into an
	// honest refusal rather than an unpinned (and silently wrong) compile
	CHECK(tripletArchitecture("wasm32-emscripten").empty());
	CHECK(tripletArchitecture("").empty());
}

TEST_CASE("the vcpkg triplet directory is the one with headers",
	"[unit][export]")
{
	ScratchDir scratch("triplet");
	const Orkige::String installed =
		ExportFiles::join(scratch.path, "vcpkg_installed");
	// vcpkg keeps bookkeeping directories beside the real triplet; only the
	// one carrying include/ is the installed tree
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(installed, "vcpkg/info"), 0));
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(installed, "arm64-osx/include"), 0));
	CHECK(vcpkgTripletDirectory(scratch.path) ==
		ExportFiles::join(installed, "arm64-osx"));
	CHECK(engineTreeArchitecture(scratch.path) == "arm64");

	// a tree without one answers nothing
	ScratchDir empty("triplet_empty");
	CHECK(vcpkgTripletDirectory(empty.path).empty());
	CHECK(engineTreeArchitecture(empty.path).empty());
}

TEST_CASE("the backend media roots resolve per flavor", "[unit][export]")
{
	ScratchDir scratch("media");
	const Orkige::String triplet =
		ExportFiles::join(scratch.path, "vcpkg_installed/arm64-osx");
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(triplet, "include"), 0));
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(triplet, "share/ogre/Media/Main"), 0));
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(triplet, "share/ogre-next/Media/Hlms"), 0));

	CHECK(ogreMediaDirectory(scratch.path) ==
		ExportFiles::join(triplet, "share/ogre/Media"));
	CHECK(ogreNextMediaDirectory(scratch.path) ==
		ExportFiles::join(triplet, "share/ogre-next/Media"));
	// an absent one is "" - the caller turns that into an honest refusal, not
	// an app missing its shaders
	ScratchDir bare("media_bare");
	CHECK(ogreMediaDirectory(bare.path).empty());
	CHECK(ogreNextMediaDirectory(bare.path).empty());
}

TEST_CASE("Hlms is mandatory, Atmosphere rides along when present",
	"[unit][export]")
{
	ScratchDir scratch("subdirs");
	const Orkige::String withSky = ExportFiles::join(scratch.path, "with");
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(withSky, "Hlms"), 0));
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(withSky, "Atmosphere"), 0));
	CHECK(ogreNextMediaSubdirs(withSky) ==
		std::vector<Orkige::String>{ "Hlms", "Atmosphere" });

	// an older port pin ships no Atmosphere; the runtime degrades that
	// honestly (no sky, flat fog colour), so bundling stays optional here too
	const Orkige::String withoutSky =
		ExportFiles::join(scratch.path, "without");
	REQUIRE(ExportFiles::makeDirectories(
		ExportFiles::join(withoutSky, "Hlms"), 0));
	CHECK(ogreNextMediaSubdirs(withoutSky) ==
		std::vector<Orkige::String>{ "Hlms" });
}

TEST_CASE("the engine source media is per flavor where it matters",
	"[unit][export]")
{
	ScratchDir scratch("source");
	const Orkige::String media =
		ExportFiles::join(scratch.path, "orkige_engine/media");
	for(const char * name : { "fonts", "water", "decals", "rtss",
		"bloom/next", "grade/next", "bloom/classic" })
	{
		REQUIRE(ExportFiles::makeDirectories(
			ExportFiles::join(media, name), 0));
	}

	const EngineSourceMedia next = engineSourceMedia(scratch.path, "next");
	CHECK(next.fonts == ExportFiles::join(media, "fonts"));
	CHECK(next.water == ExportFiles::join(media, "water"));
	CHECK(next.decals == ExportFiles::join(media, "decals"));
	CHECK(next.rtss == ExportFiles::join(media, "rtss"));
	// the compositor media is flavor-specific: the two backends' shaders are
	// not interchangeable
	CHECK(next.bloom == ExportFiles::join(media, "bloom/next"));
	CHECK(next.grade == ExportFiles::join(media, "grade/next"));

	const EngineSourceMedia classic =
		engineSourceMedia(scratch.path, "classic");
	CHECK(classic.bloom == ExportFiles::join(media, "bloom/classic"));
	// absent stays empty rather than pointing at a directory that is not there
	CHECK(classic.grade.empty());

	// no source tree at all (a distributed app): nothing to offer
	const EngineSourceMedia none = engineSourceMedia("", "next");
	CHECK(none.fonts.empty());
	CHECK(none.bloom.empty());
}
