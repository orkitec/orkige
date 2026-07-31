/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportProjectTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The manifest facts an export packages from: the derived names (which become
	an executable's file name and a bundle id) and the honest refusals when the
	manifest is missing, malformed or nameless.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportProject.h"

#include <filesystem>

using namespace OrkigeExport;

namespace
{
	//! a scratch directory that removes itself
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_export_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	ExportProject named(Orkige::String const & name)
	{
		ExportProject project;
		project.name = name;
		return project;
	}
}

TEST_CASE("the executable name keeps only alphanumerics", "[unit][export]")
{
	CHECK(named("My Game").exeName() == "MyGame");
	CHECK(named("Roller 2!").exeName() == "Roller2");
	CHECK(named("jumper").exeName() == "jumper");
	// nothing survives -> a name that still produces a runnable artifact
	CHECK(named("-- ??? --").exeName() == "OrkigeGame");
}

TEST_CASE("the id slug is reverse-DNS safe", "[unit][export]")
{
	CHECK(named("My Game").idSlug() == "mygame");
	CHECK(named("Roller 2").idSlug() == "roller2");
	// a reverse-DNS label may not start with a digit
	CHECK(named("2048").idSlug() == "p2048");
	CHECK(named("!!!").idSlug() == "orkigegame");
}

TEST_CASE("the manifest reads name, scene and settings", "[unit][export]")
{
	ScratchDir scratch("manifest");
	const Orkige::String manifest =
		ExportFiles::join(scratch.path, "project.orkproj");
	REQUIRE(ExportFiles::writeTextFile(manifest,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<OrkigeProject version=\"1\">\n"
		"  <Name>My Game</Name>\n"
		"  <MainScene>scenes/main.oscene</MainScene>\n"
		"  <Settings>\n"
		"    <Setting key=\"export.macos.bundleId\" value=\"com.x.y\"/>\n"
		"    <Setting key=\"native.target\" value=\" JumperNative \"/>\n"
		"  </Settings>\n"
		"</OrkigeProject>\n", 0));

	ExportProject project;
	Orkige::String error;
	REQUIRE(ExportProject::readManifest(scratch.path, project, &error));
	CHECK(project.name == "My Game");
	CHECK(project.mainScene == "scenes/main.oscene");
	CHECK(project.setting("export.macos.bundleId") == "com.x.y");
	CHECK(project.setting("nothing", "fallback") == "fallback");
	// the native target is trimmed - it becomes a file name
	CHECK(project.nativeTarget() == "JumperNative");
	CHECK(project.exeName() == "MyGame");

	// the manifest FILE resolves to the same project as its directory
	ExportProject viaFile;
	REQUIRE(ExportProject::readManifest(manifest, viaFile, &error));
	CHECK(viaFile.name == project.name);
	CHECK(viaFile.root == project.root);
}

TEST_CASE("a project without a native module has no target", "[unit][export]")
{
	ScratchDir scratch("plain");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(scratch.path, "project.orkproj"),
		"<OrkigeProject version=\"1\"><Name>Plain</Name></OrkigeProject>\n",
		0));
	ExportProject project;
	REQUIRE(ExportProject::readManifest(scratch.path, project, 0));
	CHECK(project.nativeTarget().empty());
	CHECK(project.mainScene.empty());
	CHECK(project.settings.empty());
}

TEST_CASE("a broken manifest refuses with a reason", "[unit][export]")
{
	ScratchDir scratch("broken");
	ExportProject project;
	Orkige::String error;

	// absent
	CHECK_FALSE(ExportProject::readManifest(scratch.path, project, &error));
	CHECK_FALSE(error.empty());

	// not an OrkigeProject
	const Orkige::String manifest =
		ExportFiles::join(scratch.path, "project.orkproj");
	REQUIRE(ExportFiles::writeTextFile(manifest, "<Something/>\n", 0));
	error.clear();
	CHECK_FALSE(ExportProject::readManifest(scratch.path, project, &error));
	CHECK_FALSE(error.empty());

	// no Name: every derived identity would be a guess, so it refuses here
	// rather than shipping an artifact called OrkigeGame
	REQUIRE(ExportFiles::writeTextFile(manifest,
		"<OrkigeProject version=\"1\"><Name>  </Name></OrkigeProject>\n", 0));
	error.clear();
	CHECK_FALSE(ExportProject::readManifest(scratch.path, project, &error));
	CHECK_FALSE(error.empty());

	// unparseable XML
	REQUIRE(ExportFiles::writeTextFile(manifest, "<OrkigeProject", 0));
	error.clear();
	CHECK_FALSE(ExportProject::readManifest(scratch.path, project, &error));
	CHECK_FALSE(error.empty());
}
