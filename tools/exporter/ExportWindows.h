/********************************************************************
	created:	Wednesday 2026/08/05 at 22:30
	filename: 	ExportWindows.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportWindows_h__5_8_2026__22_30_00__
#define __ExportWindows_h__5_8_2026__22_30_00__

#include "ExportPayload.h"
#include "ExportProcess.h"
#include "ExportProject.h"
#include "ExportWindowsSign.h"

#include <core_util/String.h>

#include <vector>

//! @file ExportWindows.h
//! @brief packaging a project as a PORTABLE DIRECTORY - the shape a Windows
//! game is distributed in.
//!
//! @verbatim
//!   <Exe>/<Exe>.exe              the player binary, renamed to the game
//!   <Exe>/Media/                 the flavor's engine media
//!   <Exe>/project/               manifest, scenes/, assets/, scripts/, data/
//!   <Exe>/orkige_project.txt     the default-project marker
//!   <Exe>/THIRD-PARTY-NOTICES.md the bundled libraries' license texts
//! @endverbatim
//!
//! Windows has no bundle format either, so the directory IS the artifact, the
//! same way the Linux package is: it is copied or archived whole and the
//! executable is run from inside it. That works because the runtime resolves
//! its project marker and its engine media against `SDL_GetBasePath()`, which
//! on Windows is the directory the executable lives in, so nothing is baked
//! into the binary and the game boots with no argument (@see
//! Orkige::PlayerBundle in engine_runtime/PlayerRuntime.h).
//!
//! What a Windows package brings with it is decided by the triplet, not by
//! this file: `x64-windows-static-md` links the whole dependency closure
//! STATICALLY and leaves only the C RUNTIME dynamic, so the binary this copies
//! is already whole and the machine supplies the rest. That leaves exactly one
//! question a Linux package does not have to ask - whether anything was built
//! as a DLL after all - and it is answered by enumerating what sits beside the
//! player binary rather than by trusting the triplet
//! (@ref windowsCompanionLibraries).
//!
//! A desktop package ships the HOST's player binary, so this packages on
//! Windows and refuses by name anywhere else (@ref
//! OrkigeExport::desktopHostRefusal) - there is no Windows executable in a
//! macOS or Linux build tree to package.

namespace OrkigeExport
{
	//! @brief is @p stem one of the names Windows reserves for a DEVICE (PURE)?
	//! @remarks `CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9` and `LPT1`-`LPT9`
	//! name character devices at every directory, so neither a file nor a
	//! directory can carry one - and the extension does not save it: `CON.exe`
	//! is still the console. The match is case-insensitive, because the
	//! reservation is.
	//! @remarks Only these matter here: an executable name is reduced to its
	//! alphanumerics before it arrives, so the punctuated device names
	//! (`CONIN$`, `CONOUT$`) can never be produced.
	bool isWindowsReservedName(Orkige::String const & stem);

	//! @brief the artifact's directory name for @p project, which is also the
	//! executable's stem inside it (PURE).
	//! @remarks One name, not the display name outside and the executable name
	//! inside: the directory is what a person types on a command line to reach
	//! the game, and a display name may carry spaces.
	//! @remarks A project whose name reduces to a reserved device name gets a
	//! suffix, because otherwise the artifact could not be WRITTEN - the
	//! reduction stays alphanumeric so the result is still typeable.
	Orkige::String windowsAppDirectoryName(ExportProject const & project);

	//! @brief the packaged executable's file name for @p project (PURE):
	//! @ref windowsAppDirectoryName plus `.exe`.
	//! @remarks The extension is not decoration - it is what makes the file
	//! executable on Windows, where there is no permission bit to set.
	Orkige::String windowsExecutableName(ExportProject const & project);

	//! @brief which of the files sitting beside the player binary ride into the
	//! package (PURE).
	//! @param siblingFiles the file names found in the player's own build
	//!        directory (names only, no paths)
	//! @return the subset to copy, in the order given
	//! @remarks A build directory holds far more than the program: import
	//! libraries, debug databases, incremental-link state and other targets'
	//! executables all sit beside it. Copying `*.dll` and nothing else is a
	//! DECISION - the linked closure is static, so this is expected to be
	//! empty, and it exists so that a dependency which ever does become dynamic
	//! rides along instead of leaving a package that cannot start.
	//! @remarks A debug database is deliberately not carried: it is a
	//! developer's file, it is large, and shipping one hands out the symbol
	//! names of everything in the binary.
	std::vector<Orkige::String> windowsCompanionLibraries(
		std::vector<Orkige::String> const & siblingFiles);

	//! @brief package @p project as `<outputDirectory>/<Exe>/`.
	//! @param outArtifact receives the directory path on success
	//! @param tests a TEST BUILD: carry the project's suite and run it
	//!        (default off, so a shipping directory is untouched)
	//! @param signing what the packaged executable is sealed with. An
	//!        unresolved one (@ref WindowsSigning::real is false) is the
	//!        DEFAULT: the package is unsigned and byte-identical to one
	//!        produced before signing existed (@see ExportWindowsSign.h).
	//! @return false with an honest @p error naming the missing piece
	bool exportWindows(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, PayloadTestRun const & tests,
		WindowsSigning const & signing, Orkige::String & outArtifact,
		Orkige::String * error);
}

#endif //__ExportWindows_h__5_8_2026__22_30_00__
