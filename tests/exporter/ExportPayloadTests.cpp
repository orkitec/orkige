/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportPayloadTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	What rides inside a packaged app.

	The payload rules are the ones a person notices only after shipping: a
	config asset a manifest Setting names does NOT live under assets/, so a
	wholesale assets/ copy misses it and the game boots without its input map;
	native/ and builds/ must NOT ship, or the download carries the whole build
	tree. The marker is the mechanism by which the app finds its project at
	all.

	The engine-media layout is asserted per flavor because the two backends'
	shader trees are not interchangeable, and because the classic path MERGES
	the engine's own shader library into the one location the runtime
	registers - a merge that silently doing nothing would leave every
	generated material unlit.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportImage.h"
#include "ExportMacos.h"
#include "ExportPayload.h"
#include "ExportSettings.h"

#include <core_project/AssetDatabase.h>

#include <filesystem>
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
				("orkige_payload_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	void writeFile(Orkige::String const & path, Orkige::String const & body)
	{
		REQUIRE(ExportFiles::writeTextFile(path, body, 0));
	}

	//! a project on disk with the whole shippable shape plus the parts that
	//! must stay home
	ExportProject makeProject(Orkige::String const & root)
	{
		writeFile(ExportFiles::join(root, "project.orkproj"),
			"<OrkigeProject version=\"1\"><Name>Probe</Name></OrkigeProject>\n");
		writeFile(ExportFiles::join(root, "scenes/main.oscene"),
			"<XMLArchive Version=\"0\"/>\n");
		writeFile(ExportFiles::join(root, "assets/coin.osfx"),
			"version 1\npreset coin\n");
		writeFile(ExportFiles::join(root, "scripts/player.lua"), "-- lua\n");
		// DEVELOPMENT artefacts, out for two different reasons: an editor TOOL
		// rides inside scripts/ (a payload subdirectory) and needs an explicit
		// strip; a test file lives in tests/, which is not a payload
		// subdirectory at all, so it is out by construction
		writeFile(ExportFiles::join(root, "scripts/retag.editor.lua"),
			"-- tool: Retag\n");
		writeFile(ExportFiles::join(root, "tests/movement.test.lua"),
			"test('x', function(t) end)\n");
		// authored DATA files: a script reads these by project-relative name
		// at runtime, so they must reach the payload like any other content
		writeFile(ExportFiles::join(root, "data/tuning.json"),
			"{\"moveSpeed\":4.5}\n");
		writeFile(ExportFiles::join(root, "data/levels/world1.csv"),
			"id,name\n1,start\n");
		// what must NOT ship: compiled code and previous packages
		writeFile(ExportFiles::join(root, "native/main.cpp"), "int main(){}\n");
		writeFile(ExportFiles::join(root, "builds/macos/old.app/x"), "stale\n");

		ExportProject project;
		project.root = root;
		project.name = "Probe";
		return project;
	}
}

TEST_CASE("the payload ships the manifest and the content subdirectories",
	"[unit][export]")
{
	ScratchDir scratch("payload");
	const Orkige::String root = ExportFiles::join(scratch.path, "project");
	const ExportProject project = makeProject(root);
	const Orkige::String destination =
		ExportFiles::join(scratch.path, "payload");

	int staged = 0;
	Orkige::String error;
	REQUIRE(stageProjectPayload(project, destination, "", "next", nullptr,
		&staged, &error));
	CHECK(staged == 6);	// manifest + scene + asset + script + two data files

	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "project.orkproj")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "scenes/main.oscene")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "scripts/player.lua")));
	// data/ ships whole, nesting included: a game that reads its levels from
	// a data file in the editor reads the SAME name from the package
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "data/tuning.json")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "data/levels/world1.csv")));
	// every asset kind rides along verbatim, including the sound PARAMETER
	// files the engine synthesizes at load
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "assets/coin.osfx")));

	// compiled code ships as the packaged binary, and a previous package is
	// not part of this one - both stay home
	CHECK_FALSE(ExportFiles::exists(ExportFiles::join(destination, "native")));
	CHECK_FALSE(ExportFiles::exists(ExportFiles::join(destination, "builds")));

	// a project's own Lua TEST SUITE is a development artefact: it is run
	// against a project on a dev machine, never by a shipped game. `tests` is
	// deliberately not a payloadSubdirs() entry, so it is out by construction -
	// this is what makes adding it there fail loudly
	CHECK_FALSE(ExportFiles::exists(ExportFiles::join(destination, "tests")));
	// an editor TOOL is dev tooling too, but it rides inside scripts/, which
	// IS a payload subdirectory - so it is copied and then stripped. Its
	// ordinary sibling in the same folder must survive that strip.
	CHECK_FALSE(ExportFiles::exists(
		ExportFiles::join(destination, "scripts/retag.editor.lua")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "scripts/player.lua")));
}

TEST_CASE("config assets ride along at their project-relative paths",
	"[unit][export]")
{
	ScratchDir scratch("config");
	const Orkige::String root = ExportFiles::join(scratch.path, "project");
	ExportProject project = makeProject(root);

	// a FILE setting, a DIRECTORY setting (localisation names a tree of one
	// .xlf per language), and a stale one pointing at nothing
	writeFile(ExportFiles::join(root, "input.oactions"), "actions\n");
	for(const char * name : { "en.xlf", "de.xlf", "en-XA.xlf" })
	{
		writeFile(ExportFiles::join(ExportFiles::join(root, "loc"), name),
			"<xliff/>");
	}
	project.settings["input.actions"] = "input.oactions";
	project.settings["localisation"] = "loc";
	project.settings["levels"] = "does_not_exist.olevels";

	const Orkige::String destination =
		ExportFiles::join(scratch.path, "staged");
	REQUIRE(ExportFiles::makeDirectories(destination, 0));

	std::vector<Orkige::String> messages;
	auto log = [&messages](Orkige::String const & message)
	{
		messages.push_back(message);
	};
	int staged = 0;
	Orkige::String error;
	REQUIRE(stageConfigSettings(project, destination, log, &staged, &error));
	CHECK(staged == 4);	// 3 .xlf + 1 .oactions

	// these live OUTSIDE assets/, so only this pass ships them - without it
	// the game boots with no input map and no translations
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "input.oactions")));
	for(const char * name : { "en.xlf", "de.xlf", "en-XA.xlf" })
	{
		CHECK(ExportFiles::isRegularFile(ExportFiles::join(
			ExportFiles::join(destination, "loc"), name)));
	}
	// a stale key WARNS and is skipped: not a reason to refuse an export
	CHECK_FALSE(ExportFiles::exists(
		ExportFiles::join(destination, "does_not_exist.olevels")));
	REQUIRE(messages.size() == 1);
	CHECK(messages[0].find("WARNING") != Orkige::String::npos);
	CHECK(messages[0].find("levels") != Orkige::String::npos);
}

TEST_CASE("the marker names the payload directory", "[unit][export]")
{
	ScratchDir scratch("marker");
	Orkige::String error;
	REQUIRE(writeProjectMarker(scratch.path, &error));

	const Orkige::String path =
		ExportFiles::join(scratch.path, PROJECT_MARKER_FILE_NAME);
	CHECK(ExportFiles::fileName(path) == "orkige_project.txt");
	Orkige::String content;
	REQUIRE(ExportFiles::readTextFile(path, content, &error));
	// the runtime reads this from SDL_GetBasePath() and boots what it names;
	// the trailing LF is part of the written form
	CHECK(content == "project\n");
}

TEST_CASE("the notices are looked for in engine-source precedence",
	"[unit][export]")
{
	// a distributed editor answers from inside itself before it consults
	// anything else, and an engine source tree is the last resort - the same
	// order every other packaged piece follows. Empty roots drop out entirely,
	// so a source that has only one of the four yields exactly one candidate.
	EngineSource source;
	source.bundleResources = "/apps/Orkige.app/Contents/Resources";
	source.devicePayload = "/state/payloads/android";
	source.sdkPack = "/state/sdk";
	const std::vector<Orkige::String> full =
		thirdPartyNoticesCandidates(source, "/src/orkige");
	REQUIRE(full.size() == 4);
	CHECK(full[0] == ExportFiles::join(source.bundleResources,
		THIRD_PARTY_NOTICES_FILE_NAME));
	CHECK(full[1] == ExportFiles::join(source.devicePayload,
		THIRD_PARTY_NOTICES_FILE_NAME));
	CHECK(full[2] == ExportFiles::join(source.sdkPack,
		THIRD_PARTY_NOTICES_FILE_NAME));
	CHECK(full[3] == ExportFiles::join("/src/orkige",
		THIRD_PARTY_NOTICES_FILE_NAME));

	EngineSource tree;
	const std::vector<Orkige::String> only =
		thirdPartyNoticesCandidates(tree, "/src/orkige");
	REQUIRE(only.size() == 1);
	CHECK(only[0] == full[3]);
	CHECK(thirdPartyNoticesCandidates(tree, "").empty());
}

TEST_CASE("the notices are staged from the first source that has them",
	"[unit][export]")
{
	ScratchDir scratch("notices");
	const Orkige::String preferred =
		ExportFiles::join(scratch.path, "bundle");
	const Orkige::String fallback = ExportFiles::join(scratch.path, "repo");
	const Orkige::String destination = ExportFiles::join(scratch.path, "app");
	writeFile(ExportFiles::join(fallback, THIRD_PARTY_NOTICES_FILE_NAME),
		"from the source tree\n");

	Orkige::String error;
	int staged = -1;
	const std::vector<Orkige::String> candidates =
		thirdPartyNoticesCandidates({ preferred, fallback });
	REQUIRE(stageThirdPartyNoticesFrom(destination, candidates, ExportLog(),
		&staged, &error));
	CHECK(staged == 1);
	Orkige::String content;
	REQUIRE(ExportFiles::readTextFile(
		ExportFiles::join(destination, THIRD_PARTY_NOTICES_FILE_NAME), content,
		&error));
	CHECK(content == "from the source tree\n");

	// with the preferred root carrying its own copy, that one wins
	writeFile(ExportFiles::join(preferred, THIRD_PARTY_NOTICES_FILE_NAME),
		"from the bundle\n");
	REQUIRE(stageThirdPartyNoticesFrom(destination, candidates, ExportLog(),
		&staged, &error));
	CHECK(staged == 1);
	REQUIRE(ExportFiles::readTextFile(
		ExportFiles::join(destination, THIRD_PARTY_NOTICES_FILE_NAME), content,
		&error));
	CHECK(content == "from the bundle\n");
}

TEST_CASE("a source with no notices warns instead of failing the export",
	"[unit][export]")
{
	// an export that cannot find a text file must still produce a runnable
	// app; the export suite is what asserts the file is actually there, so a
	// packaging path cannot lose it silently
	ScratchDir scratch("notices_absent");
	Orkige::String error;
	Orkige::String warned;
	int staged = -1;
	REQUIRE(stageThirdPartyNoticesFrom(scratch.path,
		thirdPartyNoticesCandidates({ ExportFiles::join(scratch.path, "none") }),
		[&warned](Orkige::String const & line) { warned = line; }, &staged,
		&error));
	CHECK(staged == 0);
	CHECK(warned.find(THIRD_PARTY_NOTICES_FILE_NAME) != Orkige::String::npos);
	CHECK_FALSE(ExportFiles::isRegularFile(
		ExportFiles::join(scratch.path, THIRD_PARTY_NOTICES_FILE_NAME)));
}

TEST_CASE("the next flavor bundles the Hlms templates", "[unit][export]")
{
	ScratchDir scratch("media_next");
	const Orkige::String backend =
		ExportFiles::join(scratch.path, "vcpkg-media");
	writeFile(ExportFiles::join(backend, "Hlms/Pbs/pbs.material"), "hlms\n");
	writeFile(ExportFiles::join(backend, "Atmosphere/sky.material"), "sky\n");

	EngineSourceMedia sourceMedia;
	sourceMedia.fonts = ExportFiles::join(scratch.path, "src/fonts");
	sourceMedia.water = ExportFiles::join(scratch.path, "src/water");
	sourceMedia.bloom = ExportFiles::join(scratch.path, "src/bloom-next");
	writeFile(ExportFiles::join(sourceMedia.fonts, "Nunito.ttf"), "font\n");
	writeFile(ExportFiles::join(sourceMedia.water, "water_plane.glb"), "mesh\n");
	writeFile(ExportFiles::join(sourceMedia.bloom, "bloom.material"), "bloom\n");

	const Orkige::String resources =
		ExportFiles::join(scratch.path, "Resources");
	Orkige::String error;
	REQUIRE(stageEngineMediaFromTree(resources, backend, "next", sourceMedia,
		&error));

	const Orkige::String media = ExportFiles::join(resources, "Media");
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "Hlms/Pbs/pbs.material")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "Atmosphere/sky.material")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "fonts/Nunito.ttf")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "water/water_plane.glb")));
	// the compositor media keeps its flavor directory, the way the runtime
	// looks it up
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "bloom/next/bloom.material")));
	// an absent source media directory is simply not there, not a failure
	CHECK_FALSE(ExportFiles::exists(ExportFiles::join(media, "decals")));
}

TEST_CASE("the classic flavor merges the engine shader library",
	"[unit][export]")
{
	ScratchDir scratch("media_classic");
	const Orkige::String backend =
		ExportFiles::join(scratch.path, "vcpkg-media");
	writeFile(ExportFiles::join(backend, "Main/base.program"), "main\n");
	writeFile(ExportFiles::join(backend, "RTShaderLib/lib.glsl"), "upstream\n");

	EngineSourceMedia sourceMedia;
	sourceMedia.rtss = ExportFiles::join(scratch.path, "src/rtss");
	writeFile(ExportFiles::join(sourceMedia.rtss, "metalrough.glsl"),
		"engine\n");

	const Orkige::String resources =
		ExportFiles::join(scratch.path, "Resources");
	Orkige::String error;
	REQUIRE(stageEngineMediaFromTree(resources, backend, "classic",
		sourceMedia, &error));

	const Orkige::String media = ExportFiles::join(resources, "Media");
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "Main/base.program")));
	// the engine's own metal-rough library is merged INTO RTShaderLib beside
	// the upstream files - the runtime registers that ONE location, so a
	// separate directory would simply never be found
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "RTShaderLib/lib.glsl")));
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(media, "RTShaderLib/metalrough.glsl")));
	// the next flavor's shader templates have no business in a classic bundle
	CHECK_FALSE(ExportFiles::exists(ExportFiles::join(media, "Hlms")));
}

TEST_CASE("the payload cook runs for the target platform", "[unit][export]")
{
	// the payload staging and the texture cook are one step: a texture cooked
	// for the wrong platform ships a container the device cannot decode
	ScratchDir scratch("payload_cook");
	const Orkige::String root = ExportFiles::join(scratch.path, "project");
	const ExportProject project = makeProject(root);

	ExportImage pixel(8, 8);
	for(std::size_t index = 3; index < pixel.pixels.size(); index += 4)
	{
		pixel.pixels[index] = 255;
	}
	const Orkige::String texture = ExportFiles::join(root, "assets/tile.png");
	REQUIRE(encodePngFile(pixel, texture, 0));
	REQUIRE(Orkige::AssetDatabase::writeMetaFile(
		texture + Orkige::AssetDatabase::META_FILE_EXTENSION, "tileid00"));

	// web ships PNG (no compressed format is guaranteed in a browser)
	{
		const Orkige::String destination =
			ExportFiles::join(scratch.path, "web");
		Orkige::String error;
		REQUIRE(stageProjectPayload(project, destination, "web", "classic",
			nullptr, 0, &error));
		CHECK(ExportFiles::isRegularFile(
			ExportFiles::join(destination, "assets/tile.png")));
	}
	// desktop next compresses it, replacing the source
	{
		const Orkige::String destination =
			ExportFiles::join(scratch.path, "desktop");
		Orkige::String error;
		REQUIRE(stageProjectPayload(project, destination, "", "next", nullptr,
			0, &error));
		CHECK(ExportFiles::isRegularFile(
			ExportFiles::join(destination, "assets/tile.dds")));
		CHECK_FALSE(ExportFiles::exists(
			ExportFiles::join(destination, "assets/tile.png")));
	}
}

TEST_CASE("a payload ships no sidecars and carries the baked samplers instead",
	"[unit][export]")
{
	// .orkmeta files are editor bookkeeping. The one answer a runtime reads
	// out of them - how a texture is sampled - is resolved ONCE here, for the
	// platform being packaged, into the payload manifest; nothing in a frozen
	// payload can be renamed, so the asset ids have nothing left to do.
	ScratchDir scratch("payload_samplers");
	const Orkige::String root = ExportFiles::join(scratch.path, "project");
	const ExportProject project = makeProject(root);

	ExportImage pixel(8, 8);
	for(std::size_t index = 3; index < pixel.pixels.size(); index += 4)
	{
		pixel.pixels[index] = 255;
	}
	// a crisp pixel-art texture (an AUTHORED sampler, kept as PNG so the
	// payload name matches the source), a plain one, and a per-platform
	// override that only mobile sees
	Orkige::TextureImport crisp;
	crisp.base.filter = "point";
	crisp.base.wrap = "wrap";
	crisp.base.format = "none";
	Orkige::TextureImport mobileOnly;
	mobileOnly.base.format = "none";
	mobileOnly.hasIos = true;
	mobileOnly.ios = mobileOnly.base;
	mobileOnly.ios.filter = "point";
	Orkige::TextureImport plain;
	plain.base.format = "none";
	struct { const char * name; Orkige::TextureImport const & import; } const
		textures[] =
	{
		{ "pixel", crisp }, { "phone", mobileOnly }, { "plain", plain }
	};
	for(auto const & entry : textures)
	{
		const Orkige::String path = ExportFiles::join(root,
			Orkige::String("assets/") + entry.name + ".png");
		REQUIRE(encodePngFile(pixel, path, 0));
		REQUIRE(Orkige::AssetDatabase::writeMetaFile(
			path + Orkige::AssetDatabase::META_FILE_EXTENSION,
			Orkige::String("id") + entry.name, entry.import));
	}

	const Orkige::String destination = ExportFiles::join(scratch.path, "out");
	Orkige::String error;
	REQUIRE(stageProjectPayload(project, destination, "", "next", nullptr, 0,
		&error));

	// not one sidecar anywhere in the payload
	for(Orkige::String const & relative :
		ExportFiles::listFilesRecursive(destination))
	{
		INFO(relative);
		CHECK(relative.find(".orkmeta") == Orkige::String::npos);
	}
	// the textures themselves did ship
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(destination, "assets/pixel.png")));

	// the desktop bake carries the authored sampler and NOT the iOS override
	Orkige::String manifest;
	REQUIRE(ExportFiles::readTextFile(
		ExportFiles::join(destination, "project.orkproj"), manifest, 0));
	CHECK(manifest.find("<TextureSamplers>") != Orkige::String::npos);
	CHECK(manifest.find("texture=\"pixel\"") != Orkige::String::npos);
	CHECK(manifest.find("filter=\"point\" wrap=\"wrap\"") !=
		Orkige::String::npos);
	CHECK(manifest.find("texture=\"phone\"") == Orkige::String::npos);
	// a texture nobody authored a sampler for stays out of the block
	CHECK(manifest.find("texture=\"plain\"") == Orkige::String::npos);

	// the iOS bake resolves the SAME sidecars against the ios slot
	const Orkige::String mobile = ExportFiles::join(scratch.path, "ios");
	REQUIRE(stageProjectPayload(project, mobile, "ios", "next", nullptr, 0,
		&error));
	Orkige::String mobileManifest;
	REQUIRE(ExportFiles::readTextFile(
		ExportFiles::join(mobile, "project.orkproj"), mobileManifest, 0));
	CHECK(mobileManifest.find("texture=\"phone\"") != Orkige::String::npos);
	CHECK(mobileManifest.find("texture=\"pixel\"") != Orkige::String::npos);
}

TEST_CASE("a project with no authored sampler bakes no block", "[unit][export]")
{
	ScratchDir scratch("payload_no_samplers");
	const Orkige::String root = ExportFiles::join(scratch.path, "project");
	const ExportProject project = makeProject(root);
	const Orkige::String destination = ExportFiles::join(scratch.path, "out");

	Orkige::String error;
	REQUIRE(stageProjectPayload(project, destination, "", "next", nullptr, 0,
		&error));
	Orkige::String manifest;
	REQUIRE(ExportFiles::readTextFile(
		ExportFiles::join(destination, "project.orkproj"), manifest, 0));
	CHECK(manifest.find("TextureSamplers") == Orkige::String::npos);
	CHECK(manifest.find("<Name>Probe</Name>") != Orkige::String::npos);
}

TEST_CASE("the macOS Info.plist names the executable and the icon",
	"[unit][export]")
{
	ExportProject project;
	project.name = "My Game";
	const Orkige::JsonValue info = macosInfoPlist(project, "com.example.game");

	// CFBundleExecutable must be the alnum-reduced name, because that is the
	// file name the binary is copied under - a mismatch and the app will not
	// launch at all
	CHECK(info.get("CFBundleExecutable").asString() == "MyGame");
	CHECK(info.get("CFBundleIdentifier").asString() == "com.example.game");
	CHECK(info.get("CFBundleName").asString() == "My Game");
	CHECK(info.get("CFBundleDisplayName").asString() == "My Game");
	CHECK(info.get("CFBundlePackageType").asString() == "APPL");
	// CFBundleIconFile is the key macOS reads for a loose .icns
	CHECK(info.get("CFBundleIconFile").asString() == "AppIcon");
	CHECK(info.get("CFBundleIconName").asString() == "AppIcon");
	CHECK(info.get("NSHighResolutionCapable").asBool());
	CHECK(info.get("LSMinimumSystemVersion").asString() == "11.0");
}

TEST_CASE("a device payload says which flavor it is", "[unit][export]")
{
	// The manifest is how an export from a FETCHED player learns what it is
	// packaging - and cooking a project's textures for the wrong backend ships
	// a game with unreadable images. The file NAME is spelled in the editor too
	// (OrkigeEditor::payloadRequiredPaths), because the two libraries do not
	// link each other; this half pins the literal.
	CHECK(Orkige::String(DEVICE_PAYLOAD_MANIFEST_FILE_NAME) ==
		"orkige_payload.txt");

	ScratchDir scratch("device_payload_manifest");
	const Orkige::String payload = ExportFiles::join(scratch.path, "payload");
	Orkige::String error;
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(payload, DEVICE_PAYLOAD_MANIFEST_FILE_NAME),
		"platform: ios-simulator\nflavor: next\n"
		"version: 2.0.0-nightly.20260802+abc\n", &error));
	CHECK(devicePayloadSetting(payload, "platform") == "ios-simulator");
	CHECK(payloadFlavor(payload) == "next");
	CHECK(devicePayloadSetting(payload, "version") ==
		"2.0.0-nightly.20260802+abc");
	// a key the manifest does not carry answers empty rather than guessing
	CHECK(devicePayloadSetting(payload, "nothing").empty());

	// a directory that is not a payload reports the same default a build tree
	// with no cache entry does, rather than throwing
	CHECK(payloadFlavor(ExportFiles::join(scratch.path, "absent")) ==
		"classic");
}
