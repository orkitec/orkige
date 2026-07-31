/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportPayload.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportPayload_h__31_7_2026__18_00_00__
#define __ExportPayload_h__31_7_2026__18_00_00__

#include "ExportBuildTree.h"
#include "ExportProject.h"

#include <core_util/String.h>

#include <functional>

//! @file ExportPayload.h
//! @brief what rides inside every packaged app: the shippable slice of the
//! project, and the engine media the runtime registers at boot.
//!
//! The PROJECT payload is the manifest plus scenes/assets/scripts - native/
//! and builds/ stay home, because compiled code ships as the packaged binary.
//! Project-CONFIG assets ride along too: files a manifest Setting names rather
//! than assets/ residents (the config-asset convention), copied verbatim at
//! their project-relative paths.
//!
//! The ENGINE media is laid out as `<resources>/Media` in the exact shape a
//! runtime resolves at boot (`PlayerBundle::resolveMediaDirectory`), so the
//! packaged app carries no vcpkg or source-tree path. A distributed editor
//! stages exactly this layout inside itself, which is why packaging from one
//! is a single copy of its `Media/`.
//!
//! The MARKER is the no-args default-project mechanism: the runtimes read
//! `orkige_project.txt` from `SDL_GetBasePath()` and boot the project it
//! names, with no command line at all.

namespace OrkigeExport
{
	//! @brief the log sink an export writes its progress to
	typedef std::function<void(Orkige::String const &)> ExportLog;

	//! @brief copy the manifest-referenced project-config assets (the
	//! `configSettingKeys()` values) into @p destination, preserving each
	//! project-relative path.
	//! @remarks a setting may name a single FILE (input.oactions and its
	//! siblings) or a DIRECTORY (localisation names a tree of one .xlf per
	//! language) - both bundle. A setting pointing at nothing WARNS and is
	//! skipped: a stale key in a manifest is not a reason to refuse an export.
	//! @param outStaged receives the number of files staged
	bool stageConfigSettings(ExportProject const & project,
		Orkige::String const & destination, ExportLog const & log,
		int * outStaged, Orkige::String * error);

	//! @brief copy the shippable project subset into @p destination and run
	//! the export-time texture cook over it for @p platform and @p flavor.
	//! @param texturePlatform the cook's platform token (@see
	//!        cookPlatformToken)
	//! @param outStaged receives the number of files staged
	bool stageProjectPayload(ExportProject const & project,
		Orkige::String const & destination,
		Orkige::String const & texturePlatform, Orkige::String const & flavor,
		ExportLog const & log, int * outStaged, Orkige::String * error);

	//! @brief lay the engine media a runtime registers at boot into
	//! `<resources>/Media`, sourced from the build's vcpkg + the source tree.
	//! @remarks per flavor: the classic RTSS shader library (Main +
	//! RTShaderLib, with the engine's own metal-rough library merged INTO
	//! RTShaderLib - the ONE location the runtime registers) or the Ogre-Next
	//! Hlms templates + the Atmosphere sky media. Then the content media each
	//! runtime resolves by name: fonts, water, decals and the per-flavor
	//! bloom/grade compositor media.
	bool stageEngineMediaFromTree(Orkige::String const & resources,
		Orkige::String const & backendMediaDirectory,
		Orkige::String const & flavor, EngineSourceMedia const & sourceMedia,
		Orkige::String * error);

	//! @brief write the default-project marker naming the payload directory
	bool writeProjectMarker(Orkige::String const & directory,
		Orkige::String * error);
}

#endif //__ExportPayload_h__31_7_2026__18_00_00__
