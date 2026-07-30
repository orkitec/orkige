/**************************************************************
	created:	2026/07/30 at 09:00
	filename: 	EditorResourcePathsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorResourcePaths.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using OrkigeEditor::EditorResourceFallbacks;
using OrkigeEditor::EditorResourceLocator;
using OrkigeEditor::EditorResourcePath;
using OrkigeEditor::EditorResourceRoot;

namespace
{
	//! the two roots the locator derives from a base path, per platform layout
#ifdef __APPLE__
	const char* const RESOURCE_ROOT = "/Apps/Orkige.app/Contents/Resources/";
	const char* const TOOL_ROOT = "/Apps/Orkige.app/Contents/MacOS/";
	const char* const BASE = "/Apps/Orkige.app/Contents/Resources";
#else
	const char* const RESOURCE_ROOT = "/Apps/orkige/share/orkige/";
	const char* const TOOL_ROOT = "/Apps/orkige/";
	const char* const BASE = "/Apps/orkige";
#endif

	//! a filesystem that exists only as a set of paths - the whole point of the
	//! injectable probe: the decision table is exercised with no files on disk
	struct FakeTree
	{
		std::set<Orkige::String> paths;

		EditorResourceLocator::ExistsFn probe()
		{
			return [this](Orkige::String const & path)
			{
				return this->paths.count(path) != 0;
			};
		}
	};

	//! a fully populated developer tree (every fallback path present)
	EditorResourceFallbacks treeFallbacks()
	{
		EditorResourceFallbacks fallbacks;
		fallbacks.engineMedia = "/tree/vcpkg/share/ogre-next/Media";
		fallbacks.engineMediaMarker = "Hlms";
		fallbacks.flavor = "next";
		fallbacks.fonts = "/tree/engine/media/fonts";
		fallbacks.water = "/tree/engine/media/water";
		fallbacks.decals = "/tree/engine/media/decals";
		fallbacks.bloom = "/tree/engine/media/bloom/next";
		fallbacks.grade = "/tree/engine/media/grade/next";
		fallbacks.uiFonts = "/tree/editor/media";
		fallbacks.player = "/tree/build/tools/player/orkige_player";
		fallbacks.texcook = "/tree/build/tools/texcook/texcook";
		return fallbacks;
	}
}

TEST_CASE("editor resources: a staged bundle answers every query itself",
	"[editor][resources]")
{
	// the bundle carries the layout an export writes; the developer tree is
	// ALSO present, so this proves the bundle takes PRECEDENCE (a distributed
	// app must never reach for the machine that built it)
	FakeTree tree;
	const Orkige::String root = RESOURCE_ROOT;
	tree.paths = {
		root + "Media/Hlms", root + "Media/fonts", root + "Media/water",
		root + "Media/decals", root + "Media/bloom/next",
		root + "Media/grade/next", root + "fa-solid-900.ttf",
		Orkige::String(TOOL_ROOT) + "orkige_player",
		Orkige::String(TOOL_ROOT) + "texcook",
		"/tree/vcpkg/share/ogre-next/Media/Hlms", "/tree/engine/media/fonts",
		"/tree/engine/media/water", "/tree/engine/media/decals",
		"/tree/engine/media/bloom/next", "/tree/engine/media/grade/next",
		"/tree/editor/media/fa-solid-900.ttf",
		"/tree/build/tools/player/orkige_player",
		"/tree/build/tools/texcook/texcook" };

	const EditorResourceLocator locator(BASE, treeFallbacks(), tree.probe());
	CHECK(locator.bundleResourceRoot() == root);
	CHECK(locator.bundleToolRoot() == Orkige::String(TOOL_ROOT));

	// the media root reports the DIRECTORY (not the marker inside it), so the
	// engine registers Media/ exactly as an exported runtime does
	const EditorResourcePath media = locator.engineMedia();
	REQUIRE(media.fromBundle());
	CHECK(media.path == root + "Media");
	CHECK(locator.engineFonts().path == root + "Media/fonts");
	CHECK(locator.engineWater().fromBundle());
	CHECK(locator.engineDecals().fromBundle());
	CHECK(locator.engineBloom().path == root + "Media/bloom/next");
	CHECK(locator.engineGrade().path == root + "Media/grade/next");
	CHECK(locator.uiFont("fa-solid-900.ttf").path == root + "fa-solid-900.ttf");
	// the sibling executables come from the bundle's tool root
	CHECK(locator.player().path == Orkige::String(TOOL_ROOT) + "orkige_player");
	CHECK(locator.texcook().fromBundle());
	// the one boot line names the bundle
	CHECK(locator.describe().find("bundled app") != Orkige::String::npos);
}

TEST_CASE("editor resources: a build-tree run falls back to the tree",
	"[editor][resources]")
{
	// nothing staged beside the executable: every query answers from the tree,
	// which is what a plain `cmake --build` run must keep doing
	FakeTree tree;
	tree.paths = {
		"/tree/vcpkg/share/ogre-next/Media/Hlms", "/tree/engine/media/fonts",
		"/tree/engine/media/water", "/tree/engine/media/decals",
		"/tree/engine/media/bloom/next", "/tree/engine/media/grade/next",
		"/tree/editor/media/DejaVuSans.ttf",
		"/tree/build/tools/player/orkige_player",
		"/tree/build/tools/texcook/texcook" };

	const EditorResourceLocator locator(BASE, treeFallbacks(), tree.probe());
	const EditorResourcePath media = locator.engineMedia();
	REQUIRE(media.found());
	CHECK(media.root == EditorResourceRoot::Tree);
	CHECK(media.path == "/tree/vcpkg/share/ogre-next/Media");
	CHECK(locator.engineFonts().root == EditorResourceRoot::Tree);
	CHECK(locator.uiFont("DejaVuSans.ttf").path ==
		"/tree/editor/media/DejaVuSans.ttf");
	CHECK(locator.player().path == "/tree/build/tools/player/orkige_player");
	CHECK(locator.describe().find("developer tree") != Orkige::String::npos);
}

TEST_CASE("editor resources: a Media without the flavor marker never wins",
	"[editor][resources]")
{
	// a half-staged bundle (the directory exists, the shader tree does not)
	// must not shadow a working developer tree - rendering depends on the
	// marker subdirectory being there
	FakeTree tree;
	tree.paths = { Orkige::String(RESOURCE_ROOT) + "Media",
		"/tree/vcpkg/share/ogre-next/Media/Hlms" };
	const EditorResourceLocator locator(BASE, treeFallbacks(), tree.probe());
	const EditorResourcePath media = locator.engineMedia();
	REQUIRE(media.found());
	CHECK(media.root == EditorResourceRoot::Tree);
}

TEST_CASE("editor resources: the marker is the flavor's own shader tree",
	"[editor][resources]")
{
	// a classic build looks for Main/, so a bundle carrying only the
	// Ogre-Next templates is NOT a classic media root
	EditorResourceFallbacks classic = treeFallbacks();
	classic.engineMediaMarker = "Main";
	classic.flavor = "classic";
	classic.engineMedia = "/tree/vcpkg/share/ogre/Media";
	classic.bloom = "/tree/engine/media/bloom/classic";

	FakeTree tree;
	tree.paths = { Orkige::String(RESOURCE_ROOT) + "Media/Hlms" };
	CHECK_FALSE(EditorResourceLocator(BASE, classic, tree.probe())
		.engineMedia().found());

	tree.paths.insert(Orkige::String(RESOURCE_ROOT) + "Media/Main");
	const EditorResourceLocator staged(BASE, classic, tree.probe());
	CHECK(staged.engineMedia().fromBundle());
	// the compositor media is staged per flavor, so the classic build asks for
	// the classic subdirectory
	tree.paths.insert(Orkige::String(RESOURCE_ROOT) + "Media/bloom/classic");
	CHECK(EditorResourceLocator(BASE, classic, tree.probe()).engineBloom().path
		== Orkige::String(RESOURCE_ROOT) + "Media/bloom/classic");
}

TEST_CASE("editor resources: nothing anywhere is an honest Missing",
	"[editor][resources]")
{
	// the clean-room failure mode: no bundled payload and no reachable tree.
	// Every query answers Missing with an empty path (consumers skip or refuse),
	// and the boot line SAYS rendering will not work instead of implying it will
	FakeTree tree;
	const EditorResourceLocator locator(BASE, treeFallbacks(), tree.probe());
	CHECK_FALSE(locator.engineMedia().found());
	CHECK(locator.engineMedia().path.empty());
	CHECK_FALSE(locator.engineFonts().found());
	CHECK_FALSE(locator.player().found());
	CHECK_FALSE(locator.texcook().found());
	CHECK(locator.describe().find("NO engine media") != Orkige::String::npos);
}

TEST_CASE("editor resources: no base path leaves only the tree",
	"[editor][resources]")
{
	// SDL_GetBasePath can fail (or be empty on platforms without one): the
	// locator then has no bundle roots at all and must not synthesise a
	// relative path out of nothing
	FakeTree tree;
	tree.paths = { "/tree/vcpkg/share/ogre-next/Media/Hlms",
		"/tree/build/tools/player/orkige_player" };
	const EditorResourceLocator locator("", treeFallbacks(), tree.probe());
	CHECK(locator.bundleResourceRoot().empty());
	CHECK(locator.bundleToolRoot().empty());
	CHECK(locator.engineMedia().root == EditorResourceRoot::Tree);
	CHECK(locator.player().root == EditorResourceRoot::Tree);
}

TEST_CASE("editor resources: blanked fallbacks make a bundle prove itself",
	"[editor][resources]")
{
	// the shape ORKIGE_EDITOR_BUNDLE_ONLY produces (EditorResourceBinding.cpp):
	// with every tree path blank, only a genuinely complete bundle resolves -
	// which is what the bundle selfcheck asserts about a staged copy
	EditorResourceFallbacks bundleOnly;
	bundleOnly.engineMediaMarker = "Hlms";
	bundleOnly.flavor = "next";

	FakeTree tree;
	tree.paths = { "/tree/vcpkg/share/ogre-next/Media/Hlms",
		"/tree/build/tools/player/orkige_player" };
	CHECK_FALSE(EditorResourceLocator(BASE, bundleOnly, tree.probe())
		.engineMedia().found());

	tree.paths.insert(Orkige::String(RESOURCE_ROOT) + "Media/Hlms");
	tree.paths.insert(Orkige::String(TOOL_ROOT) + "orkige_player");
	const EditorResourceLocator locator(BASE, bundleOnly, tree.probe());
	CHECK(locator.engineMedia().fromBundle());
	CHECK(locator.player().fromBundle());
}

TEST_CASE("editor resources: a trailing separator on the base is optional",
	"[editor][resources]")
{
	// SDL_GetBasePath is documented separator-terminated; a caller that trims it
	// (or a hand-built path) must resolve to the same roots
	FakeTree tree;
	tree.paths = { Orkige::String(RESOURCE_ROOT) + "Media/Hlms" };
	const EditorResourceLocator bare(BASE, treeFallbacks(), tree.probe());
	const EditorResourceLocator slashed(Orkige::String(BASE) + "/",
		treeFallbacks(), tree.probe());
	CHECK(bare.bundleResourceRoot() == slashed.bundleResourceRoot());
	CHECK(bare.engineMedia().path == slashed.engineMedia().path);
}
