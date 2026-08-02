/**************************************************************
	created:	2026/08/02 at 12:00
	filename: 	TextureSamplerTableTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the texture sampler table
	(core_project/TextureSamplerTable): key normalisation, the
	default answer for an unknown texture, the two fill sources
	(a project's sidecars and a manifest's baked block) and the
	round trip an export bakes and a packaged runtime reads back.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_project/TextureSamplerTable.h>

#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <process.h>	// _getpid - unique temp fixture names (parallel ctest)
#define getpid _getpid
#else
#include <unistd.h>	// getpid - unique temp fixture names (parallel ctest!)
#endif

using Orkige::optr;
using Orkige::TextureSampler;
using Orkige::TextureSamplerTable;

namespace
{
	//! RAII temp project directory (PID-suffixed - ctest runs in parallel)
	struct TempSamplerProject
	{
		std::filesystem::path root;

		explicit TempSamplerProject(std::string const & name)
			: root(std::filesystem::temp_directory_path() /
				(name + "_" + std::to_string(::getpid())))
		{
			std::filesystem::remove_all(this->root);
			std::filesystem::create_directories(this->root / "assets");
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempSamplerProject()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		void writeFile(std::string const & relativePath,
			std::string const & content) const
		{
			const std::filesystem::path path = this->root / relativePath;
			std::filesystem::create_directories(path.parent_path());
			std::ofstream file(path, std::ios::trunc);
			file << content;
		}
		//! an asset plus a sidecar carrying the given <texture> attributes
		void writeTexture(std::string const & relativePath,
			Orkige::String const & assetId,
			Orkige::TextureImport const & import) const
		{
			this->writeFile(relativePath, "png bytes");
			REQUIRE(Orkige::AssetDatabase::writeMetaFile(
				(this->root / relativePath).string() +
					Orkige::AssetDatabase::META_FILE_EXTENSION,
				assetId, import));
		}
		Orkige::String manifestPath() const
		{
			return (this->root / "project.orkproj").string();
		}
		void writeManifest() const
		{
			this->writeFile("project.orkproj",
				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<OrkigeProject version=\"1\">\n"
				"    <Name>SamplerFixture</Name>\n"
				"</OrkigeProject>\n");
		}
	};

	//! the process-wide active table (and database) must not leak between tests
	struct ActiveTableGuard
	{
		~ActiveTableGuard()
		{
			TextureSamplerTable::setActive(optr<TextureSamplerTable>());
			Orkige::AssetDatabase::setActive(optr<Orkige::AssetDatabase>());
		}
	};

	Orkige::TextureImport pointWrap()
	{
		Orkige::TextureImport import;
		import.base.filter = "point";
		import.base.wrap = "wrap";
		return import;
	}
}

TEST_CASE("TextureSamplerTable keys on the bare stem", "[samplertable]")
{
	// the cook renames as it compresses (ball.png -> ball.dds) while the scene
	// keeps naming the source, so the STEM is the one spelling both share
	CHECK(TextureSamplerTable::keyFor("assets/ball.png") == "ball");
	CHECK(TextureSamplerTable::keyFor("ball.png") == "ball");
	CHECK(TextureSamplerTable::keyFor("ball.dds") == "ball");
	CHECK(TextureSamplerTable::keyFor("ball.oitd") == "ball");
	CHECK(TextureSamplerTable::keyFor("ball") == "ball");
	CHECK(TextureSamplerTable::keyFor("assets/tiles/grass.png") == "grass");
	CHECK(TextureSamplerTable::keyFor("").empty());
	// case-sensitive on every platform, like the database's path lookups
	CHECK(TextureSamplerTable::keyFor("Ball.png") == "Ball");
}

TEST_CASE("TextureSamplerTable answers the defaults for anything it does not "
	"know", "[samplertable]")
{
	TextureSamplerTable table;
	CHECK(table.empty());
	const TextureSampler unknown = table.lookup("assets/never_authored.png");
	CHECK(unknown.filter == "bilinear");
	CHECK(unknown.wrap == "clamp");
	CHECK(unknown.isDefault());

	// a DEFAULT sampler is not stored: an absent key already means it
	TextureSampler defaults;
	table.set("assets/plain.png", defaults);
	CHECK(table.empty());

	TextureSampler crisp;
	crisp.filter = "point";
	table.set("assets/pixel.png", crisp);
	CHECK(table.size() == 1);
	CHECK(table.lookup("pixel.dds").filter == "point");
	CHECK(table.lookup("pixel.dds").wrap == "clamp");
	// setting it back to the defaults removes the entry again
	table.set("assets/pixel.png", defaults);
	CHECK(table.empty());
}

TEST_CASE("TextureSamplerTable fills from a project's sidecars",
	"[samplertable]")
{
	Orkige::CoreTestEnvironment::get();
	TempSamplerProject project("orkige_test_sampler_fill");

	Orkige::TextureImport crisp = pointWrap();
	// a per-platform override: mobile wants the crisp look, desktop does not
	crisp.base.filter = "bilinear";
	crisp.base.wrap = "clamp";
	crisp.hasIos = true;
	crisp.ios.filter = "point";
	crisp.ios.wrap = "wrap";
	project.writeTexture("assets/tile.png",
		"a55e710000000000000000000000000a", crisp);
	project.writeTexture("assets/hero.png",
		"a55e710000000000000000000000000b", pointWrap());
	// an id-only sidecar authors no sampler at all
	project.writeFile("assets/plain.png", "png bytes");
	REQUIRE(Orkige::AssetDatabase::writeMetaFile(
		(project.root / "assets/plain.png").string() +
			Orkige::AssetDatabase::META_FILE_EXTENSION,
		"a55e710000000000000000000000000c"));

	Orkige::AssetDatabase database;
	database.refresh(project.root.string(), false);

	TextureSamplerTable desktop;
	desktop.fillFromAssets(database, "");
	CHECK(desktop.size() == 1);
	CHECK(desktop.lookup("hero.png").filter == "point");
	CHECK(desktop.lookup("hero.png").wrap == "wrap");
	CHECK(desktop.lookup("tile.png").isDefault());
	CHECK(desktop.lookup("plain.png").isDefault());

	TextureSamplerTable ios;
	ios.fillFromAssets(database, "ios");
	CHECK(ios.size() == 2);
	CHECK(ios.lookup("tile.png").filter == "point");
	CHECK(ios.lookup("tile.png").wrap == "wrap");
}

TEST_CASE("a manifest's baked sampler block round-trips and wins over the "
	"sidecars", "[samplertable]")
{
	Orkige::CoreTestEnvironment::get();
	ActiveTableGuard guard;
	TempSamplerProject project("orkige_test_sampler_bake");
	project.writeManifest();
	project.writeTexture("assets/hero.png",
		"a55e710000000000000000000000000b", pointWrap());

	// an AUTHORING project derives its answers from the sidecars on load
	{
		Orkige::Project authoring;
		Orkige::String error;
		REQUIRE(authoring.load(project.root.string(), &error));
		CHECK_FALSE(authoring.hasBakedTextureSamplers());
		REQUIRE(authoring.getTextureSamplers());
		CHECK(authoring.getTextureSamplers()->lookup("hero.png").filter ==
			"point");
		CHECK(TextureSamplerTable::resolve("hero.png").filter == "point");
		authoring.close();
		// with no project open every texture samples with the defaults
		CHECK(TextureSamplerTable::resolve("hero.png").isDefault());
	}

	// what an export does: resolve once, write the answers into the payload
	// manifest, and drop the sidecars
	TextureSamplerTable baked;
	TextureSampler crisp;
	crisp.filter = "point";
	crisp.wrap = "wrap";
	baked.set("assets/hero.png", crisp);
	Orkige::String bakeError;
	REQUIRE(Orkige::Project::writeBakedTextureSamplers(project.manifestPath(),
		baked, &bakeError));
	std::filesystem::remove((project.root / "assets/hero.png").string() +
		Orkige::AssetDatabase::META_FILE_EXTENSION);

	Orkige::Project packaged;
	Orkige::String error;
	REQUIRE(packaged.load(project.root.string(), &error));
	CHECK(packaged.hasBakedTextureSamplers());
	REQUIRE(packaged.getTextureSamplers());
	// no sidecar left, and the answer still arrives
	CHECK(packaged.getAssetDatabase()->getAssetCount() == 0);
	CHECK(TextureSamplerTable::resolve("hero.png").filter == "point");
	CHECK(TextureSamplerTable::resolve("hero.png").wrap == "wrap");
	// the cooked spelling of the same texture resolves to the same answer
	CHECK(TextureSamplerTable::resolve("hero.dds").filter == "point");

	// a rewrite of a packaged manifest keeps the baked block instead of
	// silently dropping build output
	REQUIRE(packaged.save(&error));
	Orkige::Project reloaded;
	REQUIRE(reloaded.load(project.root.string(), &error));
	CHECK(reloaded.hasBakedTextureSamplers());
	CHECK(reloaded.getTextureSamplers()->lookup("hero.png").filter == "point");
}

TEST_CASE("an empty bake leaves no block and keeps the manifest loadable",
	"[samplertable]")
{
	Orkige::CoreTestEnvironment::get();
	ActiveTableGuard guard;
	TempSamplerProject project("orkige_test_sampler_empty_bake");
	project.writeManifest();

	const TextureSamplerTable nothing;
	Orkige::String error;
	REQUIRE(Orkige::Project::writeBakedTextureSamplers(project.manifestPath(),
		nothing, &error));

	Orkige::Project packaged;
	REQUIRE(packaged.load(project.root.string(), &error));
	// no block: the project reads as an ordinary one and every texture
	// samples with the defaults
	CHECK_FALSE(packaged.hasBakedTextureSamplers());
	CHECK(TextureSamplerTable::resolve("anything.png").isDefault());
}
