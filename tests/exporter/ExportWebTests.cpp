/********************************************************************
	created:	Saturday 2026/08/01 at 14:00
	filename: 	ExportWebTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	Packaging a project for the browser.

	A web export compiles nothing, so the whole operation is asserted here
	against a staged engine payload standing in for a distributed editor: no
	Emscripten toolchain, no build tree, no browser. What matters is the SHAPE
	of the result, because the page and the player agree on it and nothing else
	checks that agreement until a browser is open:

	- the artifact set a static host serves (shell page, data loader, player
	  pair, pak, icon), with the project's identity substituted into the page
	  and no placeholder left behind;
	- the pak read back through the engine's OWN zip reader - the same MiniZip
	  the player mounts it with - carrying the marker, the project and the
	  engine media at the paths the runtime resolves them by;
	- the payload SPLIT the player performs at boot: the small tree it writes
	  out (marker, manifest, scenes, scripts, shader/font media) and the bulk
	  media that stays in the archive to be mounted in place, which is what the
	  entry paths have to make separable;
	- the refusals: compiled game code, and a copy that carries no browser
	  player, both named rather than half-packaged.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportWeb.h"

#include <engine_filesystem/MiniZip.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace OrkigeExport;

namespace
{
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_web_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	void writeFile(Orkige::String const & path, Orkige::String const & body)
	{
		REQUIRE(ExportFiles::writeTextFile(path, body, 0));
	}

	//! the browser half of a distributed editor's payload: the player pair,
	//! the page templates and the classic engine media, all under web/
	void stageWebPayload(Orkige::String const & resources)
	{
		const Orkige::String web = ExportFiles::join(resources, "web");
		writeFile(ExportFiles::join(web, "orkige_player.js"), "// module\n");
		writeFile(ExportFiles::join(web, "orkige_player.wasm"), "\0asm\n");
		writeFile(ExportFiles::join(web, "index.html.in"),
			"<title>@TITLE@</title><body style=\"background:@BACKGROUND@\">"
			"<script src=\"@DATA_LOADER@\"></script>"
			"<script src=\"@PLAYER_JS@\"></script></body>\n");
		writeFile(ExportFiles::join(web, "pak_loader.js"),
			"// fetch the pak\n");
		writeFile(ExportFiles::join(web, "Media/Main/quad.program"),
			"// rtss\n");
		writeFile(ExportFiles::join(web, "Media/fonts/Nunito-Regular.ttf"),
			"ttf\n");
	}

	//! a project with both halves of the split: the small tree read through
	//! fopen and the bulk media resolved by resource name
	ExportProject makeProject(Orkige::String const & root)
	{
		writeFile(ExportFiles::join(root, "project.orkproj"),
			"<OrkigeProject version=\"1\"><Name>Web Probe</Name>"
			"</OrkigeProject>\n");
		writeFile(ExportFiles::join(root, "scenes/main.oscene"),
			"<XMLArchive Version=\"0\"/>\n");
		writeFile(ExportFiles::join(root, "scripts/player.lua"), "-- lua\n");
		writeFile(ExportFiles::join(root, "assets/coin.osfx"),
			"version 1\npreset coin\n");
		ExportProject project;
		project.root = root;
		project.name = "Web Probe";
		return project;
	}

	//! a bundle-sourced export environment (no repository, no build tree - the
	//! distributed-editor case a web export has to work in)
	ExportEnvironment quietEnvironment(Orkige::String const & iconPath)
	{
		ExportEnvironment environment;
		environment.defaultIconPath = iconPath;
		return environment;
	}

	//! a 64x64 PNG-less stand-in is not enough (the icon decoder wants a real
	//! image), so the engine's own default icon is used when it is reachable
	Orkige::String repoDefaultIcon()
	{
#ifdef ORKIGE_EXPORT_TEST_DEFAULT_ICON
		return ORKIGE_EXPORT_TEST_DEFAULT_ICON;
#else
		return Orkige::String();
#endif
	}
}

TEST_CASE("web export: the shell page carries the project's identity",
	"[export][web]")
{
	const Orkige::String page = webShellPage(
		"<title>@TITLE@</title>|@BACKGROUND@|@DATA_LOADER@|@PLAYER_JS@|"
		"@BACKGROUND@",
		"My Game", "#101820", "game.js", "orkige_player.js");
	CHECK(page == "<title>My Game</title>|#101820|game.js|orkige_player.js|"
		"#101820");
	// every placeholder is substituted - a page shipped with one left in it
	// would render the literal text, so this is the assertion that matters
	CHECK(page.find('@') == Orkige::String::npos);
}

TEST_CASE("web export: archive names are forward-slashed on every host",
	"[export][web]")
{
	// a package written on Windows must read identically to one written on
	// macOS: the zip format's separator is '/', full stop
	CHECK(webArchiveName("project/assets/coin.png") ==
		"project/assets/coin.png");
	CHECK(webArchiveName("project\\assets\\coin.png") ==
		"project/assets/coin.png");
	CHECK(webArchiveName("") == "");
}

TEST_CASE("web export: a copy with the browser player packages a project",
	"[export][web]")
{
	if(repoDefaultIcon().empty())
	{
		SUCCEED("no default icon path baked in - the icon leg is covered by "
			"the icon tests");
		return;
	}
	ScratchDir scratch("bundle");
	const Orkige::String resources =
		ExportFiles::join(scratch.path, "Orkige.app/Contents/Resources");
	const Orkige::String projectRoot =
		ExportFiles::join(scratch.path, "project");
	const Orkige::String output = ExportFiles::join(scratch.path, "out");
	stageWebPayload(resources);
	const ExportProject project = makeProject(projectRoot);

	EngineSource source;
	source.bundleResources = resources;
	source.bundleTools = resources;
	Orkige::String artifact;
	Orkige::String error;
	REQUIRE(exportWeb(project, source, output,
		quietEnvironment(repoDefaultIcon()), artifact, &error));
	CHECK(error.empty());
	CHECK(artifact == output);

	// the artifact set a static host serves
	for(const char * name : { "index.html", "game.js", "game.pak",
		"orkige_player.js", "orkige_player.wasm", "icon.png" })
	{
		INFO(name);
		CHECK(ExportFiles::isRegularFile(ExportFiles::join(output, name)));
	}
	// the staging tree is sealed into the pak and gone - a leftover copy would
	// double the export's size on disk and get served as loose files
	CHECK_FALSE(ExportFiles::exists(ExportFiles::join(output, "pak-staging")));

	Orkige::String page;
	REQUIRE(ExportFiles::readTextFile(ExportFiles::join(output, "index.html"),
		page, 0));
	CHECK(page.find("<title>Web Probe</title>") != Orkige::String::npos);
	CHECK(page.find("game.js") != Orkige::String::npos);
	CHECK(page.find('@') == Orkige::String::npos);

	// the pak, read back through the reader the PLAYER mounts it with
	Orkige::MiniZip pak;
	REQUIRE(pak.open(ExportFiles::join(output, "game.pak")));
	// the small tree the player writes out at boot: the marker that makes the
	// module boot its bundled project with no arguments, the manifest, the
	// scenes and scripts, and the shader/font media the loaders want as files
	CHECK(pak.contains("orkige_project.txt"));
	CHECK(pak.contains("project/project.orkproj"));
	CHECK(pak.contains("project/scenes/main.oscene"));
	CHECK(pak.contains("project/scripts/player.lua"));
	CHECK(pak.contains("Media/Main/quad.program"));
	CHECK(pak.contains("Media/fonts/Nunito-Regular.ttf"));
	// ...and the bulk game media, which stays IN the archive to be mounted in
	// place: its entries live under the payload's assets/ - the prefix the
	// player splits on
	CHECK(pak.contains("project/assets/coin.osfx"));
	std::vector<unsigned char> marker;
	REQUIRE(pak.read("orkige_project.txt", marker));
	CHECK(Orkige::String(marker.begin(), marker.end()) == "project\n");

	// packaging twice over the same output yields the same bytes (entries in
	// one walk order, one fixed timestamp - @see ExportZip.h)
	std::vector<unsigned char> first;
	REQUIRE(ExportFiles::readBytes(ExportFiles::join(output, "game.pak"),
		first, 0));
	REQUIRE(exportWeb(project, source, output,
		quietEnvironment(repoDefaultIcon()), artifact, &error));
	std::vector<unsigned char> second;
	REQUIRE(ExportFiles::readBytes(ExportFiles::join(output, "game.pak"),
		second, 0));
	CHECK(first == second);
}

TEST_CASE("web export: what it refuses, it names", "[export][web]")
{
	ScratchDir scratch("refusals");
	const Orkige::String resources = ExportFiles::join(scratch.path, "res");
	const Orkige::String projectRoot =
		ExportFiles::join(scratch.path, "project");
	const Orkige::String output = ExportFiles::join(scratch.path, "out");
	ExportProject project = makeProject(projectRoot);
	EngineSource source;
	source.bundleResources = resources;
	source.bundleTools = resources;

	// compiled game code needs a C++ toolchain and a desktop target; the
	// browser player runs Lua/scene projects
	project.settings["native.target"] = "JumperNative";
	Orkige::String artifact;
	Orkige::String error;
	CHECK_FALSE(exportWeb(project, source, output,
		quietEnvironment(""), artifact, &error));
	CHECK(error.find("native module") != Orkige::String::npos);

	// a copy that never staged the browser player says so instead of writing a
	// page that would load a module which is not there
	project.settings.erase("native.target");
	error.clear();
	CHECK_FALSE(exportWeb(project, source, output,
		quietEnvironment(""), artifact, &error));
	CHECK(error.find("browser player") != Orkige::String::npos);

	// ...and one with the player but no engine media is just as broken: the
	// browser player is the classic flavor and cannot render without its
	// shader library
	writeFile(ExportFiles::join(resources, "web/orkige_player.js"), "//\n");
	writeFile(ExportFiles::join(resources, "web/orkige_player.wasm"), "x\n");
	error.clear();
	CHECK_FALSE(exportWeb(project, source, output,
		quietEnvironment(""), artifact, &error));
	CHECK(error.find("engine media") != Orkige::String::npos);
}
