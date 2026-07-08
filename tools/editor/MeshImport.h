// MeshImport - path handling for "File > Import Mesh..." / drag & drop.
//
// The import rule: an imported mesh file is COPIED into the scene's media
// folder so scenes stay relocatable -
// - scene saved:   "<directory of the .oscene>/media/"
// - scene unsaved: the project-level import directory the editor passes in
//   (samples/scenes/media by default, ORKIGE_EDITOR_IMPORT_DIR overrides it -
//   the tests point that at their build directory)
// The copied file keeps its name; an existing file of the same name is
// overwritten (re-import = update). The editor then registers the destination
// directory as an Ogre resource location and creates the GameObject - that
// part lives in main.cpp, everything here is pure path/filesystem logic so
// the unit tests (tests/editor_core) can run it headlessly.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_util/String.h>

namespace Orkige
{
	//! @brief is this a mesh file the importer accepts? Decided purely by
	//! extension (case-insensitive): the formats Codec_Assimp/OGRE actually
	//! load (.glb .gltf .obj .fbx .dae .stl .ply .3ds and native .mesh).
	bool isSupportedMeshFile(String const& path);

	//! @brief the directory an import copies into (see the rule above).
	//! @param currentScenePath the open scene file ("" = unsaved scene)
	//! @param projectImportDir fallback for unsaved scenes
	String meshImportDestinationDir(String const& currentScenePath,
		String const& projectImportDir);

	//! @brief the GameObject base name for an imported file: the filename
	//! without directories or extension ("" if the path has no stem)
	String meshImportObjectBaseName(String const& path);

	//! @brief copy sourcePath into destDir (created if missing, existing file
	//! overwritten; a source already AT the destination is accepted as-is).
	//! @param errorMessage optional - receives a printable reason on failure
	//! @return the full destination path, or "" on failure
	String importMeshFileToDir(String const& sourcePath, String const& destDir,
		String* errorMessage = nullptr);
}
