// MeshImport - import path handling (see header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "MeshImport.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Orkige
{
	namespace
	{
		//! extensions the importer accepts (lower-case, with dot)
		const char* const SUPPORTED_EXTENSIONS[] = {
			".glb", ".gltf", ".obj", ".fbx", ".dae", ".stl", ".ply", ".3ds",
			".mesh"
		};
	}
	//---------------------------------------------------------
	bool isSupportedMeshFile(String const& path)
	{
		String extension = std::filesystem::path(path).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (extension.empty())
		{
			return false;
		}
		for (const char* supported : SUPPORTED_EXTENSIONS)
		{
			if (extension == supported)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	String meshImportDestinationDir(String const& currentScenePath,
		String const& projectImportDir)
	{
		if (currentScenePath.empty())
		{
			return projectImportDir;
		}
		return (std::filesystem::path(currentScenePath).parent_path() /
			"media").string();
	}
	//---------------------------------------------------------
	String meshImportObjectBaseName(String const& path)
	{
		return std::filesystem::path(path).stem().string();
	}
	//---------------------------------------------------------
	String importMeshFileToDir(String const& sourcePath, String const& destDir,
		String* errorMessage)
	{
		namespace fs = std::filesystem;
		std::error_code error;
		if (!fs::is_regular_file(sourcePath, error))
		{
			if (errorMessage)
			{
				*errorMessage = "source file does not exist: " + sourcePath;
			}
			return String();
		}
		const fs::path destination =
			fs::path(destDir) / fs::path(sourcePath).filename();
		// importing a file that already lives in the destination directory is
		// a no-op copy, not an error
		if (fs::exists(destination, error) &&
			fs::equivalent(sourcePath, destination, error))
		{
			return destination.string();
		}
		fs::create_directories(destDir, error);
		if (error)
		{
			if (errorMessage)
			{
				*errorMessage = "cannot create media directory '" + destDir +
					"': " + error.message();
			}
			return String();
		}
		fs::copy_file(sourcePath, destination,
			fs::copy_options::overwrite_existing, error);
		if (error)
		{
			if (errorMessage)
			{
				*errorMessage = "copy to '" + destination.string() +
					"' failed: " + error.message();
			}
			return String();
		}
		return destination.string();
	}
}
