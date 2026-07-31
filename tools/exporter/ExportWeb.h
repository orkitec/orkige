/********************************************************************
	created:	Saturday 2026/08/01 at 14:00
	filename: 	ExportWeb.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportWeb_h__1_8_2026__14_00_00__
#define __ExportWeb_h__1_8_2026__14_00_00__

#include "ExportPayload.h"
#include "ExportProject.h"

#include <core_util/String.h>

//! @file ExportWeb.h
//! @brief package a project for the browser: a static directory any web server
//! hosts as-is.
//!
//! A web export COMPILES NOTHING. The wasm player is a build artifact like
//! every other platform's player, and everything else is bytes this exporter
//! arranges - so packaging for the browser needs no Emscripten toolchain on the
//! machine that runs it, which is what lets a downloaded editor produce one.
//!
//! @par What ships
//!   index.html            the shell page, the project's title/background/icon
//!                         substituted into tools/player/web/index.html.in
//!   game.js               the data loader (tools/player/web/pak_loader.js,
//!                         verbatim) - fetches the pak and hands it to the
//!                         module filesystem through the documented
//!                         FS_createDataFile / run-dependency surface
//!   game.pak              the WHOLE payload in ONE engine pak (@see
//!                         ExportZip): engine media, the project, the
//!                         orkige_project.txt marker
//!   orkige_player.js      the wasm player pair, copied from the web-release
//!   orkige_player.wasm    tree (or the staged payload a distributed editor
//!                         carries)
//!   icon.png              the per-project favicon
//!
//! @par Why a pak
//! The payload is the same archive `RenderSystem::mountPak` already reads, so
//! the browser player boots the way the Android player boots an uncompressed
//! APK: the small tree read through fopen (marker, manifest, scenes, scripts,
//! config, shader/font media) is written into the module filesystem at boot and
//! the bulk game media is MOUNTED straight out of the archive. One mechanism,
//! two packages - and the page's loader touches only exported runtime methods
//! rather than the module's internals.

namespace OrkigeExport
{
	//! the archive the whole payload rides in
	extern const char * const WEB_PAK_FILE_NAME;
	//! the data loader the shell page pulls in (the copied pak_loader.js)
	extern const char * const WEB_LOADER_FILE_NAME;
	//! the page a browser opens
	extern const char * const WEB_SHELL_FILE_NAME;
	//! the wasm player pair, under `tools/player` in a web-release tree and
	//! under `<resources>/web` in a staged engine payload
	extern const char * const WEB_PLAYER_SCRIPT_FILE_NAME;
	extern const char * const WEB_PLAYER_WASM_FILE_NAME;
	//! the staged engine payload's browser subdirectory (@see exportWeb)
	extern const char * const WEB_PAYLOAD_DIR_NAME;

	//! @brief substitute the shell page's placeholders (PURE). An unknown
	//! placeholder is left alone - the page then fails visibly rather than
	//! silently shipping a half-filled template.
	Orkige::String webShellPage(Orkige::String const & templateText,
		Orkige::String const & title, Orkige::String const & background,
		Orkige::String const & dataLoader,
		Orkige::String const & playerScript);

	//! @brief an archive-internal name for the project-relative @p path:
	//! forward slashes on every host, so a package written on Windows reads
	//! identically to one written on macOS (PURE).
	Orkige::String webArchiveName(Orkige::String const & path);

	//! @brief package @p project as a browser build (@see ExportWeb.h).
	//! @param source a web-release build tree, or a staged engine payload
	//!        carrying its `web/` subdirectory
	//! @param outArtifact receives the output directory (the whole export IS
	//!        the artifact - it is served, not launched)
	bool exportWeb(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, Orkige::String & outArtifact,
		Orkige::String * error);
}

#endif //__ExportWeb_h__1_8_2026__14_00_00__
