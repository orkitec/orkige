/********************************************************************
	created:	Wednesday 2026/08/05 at 15:00
	filename: 	ExportLinux.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportLinux_h__5_8_2026__15_00_00__
#define __ExportLinux_h__5_8_2026__15_00_00__

#include "ExportPayload.h"
#include "ExportProcess.h"
#include "ExportProject.h"

#include <core_util/String.h>

//! @file ExportLinux.h
//! @brief packaging a project as a PORTABLE DIRECTORY - the shape a Linux game
//! is distributed in.
//!
//! @verbatim
//!   <Exe>/<Exe>                  the player binary, renamed to the game
//!   <Exe>/Media/                 the flavor's engine media
//!   <Exe>/project/               manifest, scenes/, assets/, scripts/, data/
//!   <Exe>/orkige_project.txt     the default-project marker
//!   <Exe>/THIRD-PARTY-NOTICES.md the bundled libraries' license texts
//! @endverbatim
//!
//! Linux has no bundle format, so the directory IS the artifact: it is copied
//! or archived whole and the binary is run from inside it. That works for the
//! same reason a macOS `.app` does - the runtime resolves its project marker
//! and its engine media against `SDL_GetBasePath()`, which on Linux is the
//! directory the executable lives in, so the packaged layout needs no path
//! baked into anything and no argument on launch (@see
//! Orkige::PlayerBundle in engine_runtime/PlayerRuntime.h).
//!
//! There is no library-closure step, and that is a property of the build
//! rather than an omission: the Linux dependency closure is linked STATICALLY
//! (`VCPKG_LIBRARY_LINKAGE static` in triplets/x64-linux.cmake), so the binary
//! this copies is already whole. What stays dynamic is the machine's own - the
//! C and C++ runtimes, and the display, driver and audio libraries SDL and the
//! render backends resolve through the platform.
//!
//! A desktop package ships the HOST's player binary, so this packages on Linux
//! and refuses by name anywhere else (@ref OrkigeExport::desktopHostRefusal) -
//! there is no Linux executable in a macOS or Windows build tree to package.

namespace OrkigeExport
{
	//! @brief the artifact's directory name for @p project: its executable
	//! name, which is also the binary's name inside it (PURE).
	//! @remarks One name, not the display name outside and the executable name
	//! inside: the directory is what a person types on a command line to reach
	//! the game, and a display name may carry spaces.
	Orkige::String linuxAppDirectoryName(ExportProject const & project);

	//! @brief package @p project as `<outputDirectory>/<Exe>/`.
	//! @param outArtifact receives the directory path on success
	//! @param tests a TEST BUILD: carry the project's suite and run it
	//!        (default off, so a shipping directory is untouched)
	//! @return false with an honest @p error naming the missing piece
	bool exportLinux(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, PayloadTestRun const & tests,
		Orkige::String & outArtifact, Orkige::String * error);
}

#endif //__ExportLinux_h__5_8_2026__15_00_00__
