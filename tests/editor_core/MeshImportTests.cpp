/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	MeshImportTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the mesh-import path handling
	(tools/editor/MeshImport.{h,cpp}): extension validation, the
	destination-directory rule and the copy-into-media-dir step. The
	full import (resource registration + object creation + undo) runs
	in the editor_edittest integration test.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <MeshImport.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	//! fresh scratch directory under the system temp dir, removed on exit
	struct ScratchDir
	{
		fs::path path;
		ScratchDir()
		{
			path = fs::temp_directory_path() /
				("orkige_meshimport_test_" + std::to_string(
					std::chrono::steady_clock::now()
						.time_since_epoch().count()));
			fs::remove_all(path);
			fs::create_directories(path);
		}
		~ScratchDir()
		{
			std::error_code ignored;
			fs::remove_all(path, ignored);
		}
	};
}

TEST_CASE("mesh import accepts mesh extensions case-insensitively",
	"[editor][import]")
{
	REQUIRE(Orkige::isSupportedMeshFile("model.glb"));
	REQUIRE(Orkige::isSupportedMeshFile("/abs/path/Model.GLB"));
	REQUIRE(Orkige::isSupportedMeshFile("scene.gltf"));
	REQUIRE(Orkige::isSupportedMeshFile("thing.obj"));
	REQUIRE(Orkige::isSupportedMeshFile("Rig.FBX"));
	REQUIRE(Orkige::isSupportedMeshFile("old.dae"));
	REQUIRE(Orkige::isSupportedMeshFile("native.mesh"));

	REQUIRE_FALSE(Orkige::isSupportedMeshFile("scene.oscene"));
	REQUIRE_FALSE(Orkige::isSupportedMeshFile("readme.txt"));
	REQUIRE_FALSE(Orkige::isSupportedMeshFile("noextension"));
	REQUIRE_FALSE(Orkige::isSupportedMeshFile(""));
	REQUIRE_FALSE(Orkige::isSupportedMeshFile("model.glb.bak"));
}

TEST_CASE("mesh import destination follows the media-dir rule",
	"[editor][import]")
{
	// saved scene: "<scene dir>/media"
	const std::string sceneDir =
		(fs::path("/tmp") / "proj" / "level1.oscene").string();
	REQUIRE(Orkige::meshImportDestinationDir(sceneDir, "/fallback") ==
		(fs::path("/tmp") / "proj" / "media").string());
	// unsaved scene: the project-level import directory
	REQUIRE(Orkige::meshImportDestinationDir("", "/fallback") == "/fallback");
}

TEST_CASE("mesh import object base name is the file stem",
	"[editor][import]")
{
	REQUIRE(Orkige::meshImportObjectBaseName("/a/b/chair.glb") == "chair");
	REQUIRE(Orkige::meshImportObjectBaseName("crate.old.obj") == "crate.old");
	REQUIRE(Orkige::meshImportObjectBaseName("") == "");
}

TEST_CASE("mesh import copies the file into the destination dir",
	"[editor][import]")
{
	ScratchDir scratch;
	const fs::path source = scratch.path / "widget.glb";
	{
		std::ofstream file(source, std::ios::binary);
		file << "glTF-not-really";
	}
	const fs::path destDir = scratch.path / "media"; // does not exist yet

	std::string error;
	const std::string destination = Orkige::importMeshFileToDir(
		source.string(), destDir.string(), &error);
	REQUIRE(destination == (destDir / "widget.glb").string());
	REQUIRE(fs::exists(destination));

	// re-import of the exact same file (already at the destination) is a
	// no-op success, an updated source overwrites
	REQUIRE(Orkige::importMeshFileToDir(destination, destDir.string()) ==
		destination);
	{
		std::ofstream file(source, std::ios::binary);
		file << "glTF-not-really-v2";
	}
	REQUIRE(Orkige::importMeshFileToDir(source.string(), destDir.string()) ==
		destination);
	REQUIRE(fs::file_size(destination) == fs::file_size(source));

	// a missing source fails with a printable reason, not a crash
	error.clear();
	REQUIRE(Orkige::importMeshFileToDir(
		(scratch.path / "missing.glb").string(), destDir.string(), &error)
		== "");
	REQUIRE_FALSE(error.empty());
}
