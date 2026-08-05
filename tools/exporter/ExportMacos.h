/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportMacos.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportMacos_h__31_7_2026__18_00_00__
#define __ExportMacos_h__31_7_2026__18_00_00__

#include "ExportMacosSign.h"
#include "ExportPayload.h"
#include "ExportProcess.h"
#include "ExportProject.h"

#include <core_debugnet/Json.h>
#include <core_util/String.h>

//! @file ExportMacos.h
//! @brief packaging a project as a double-clickable macOS `.app`.
//!
//! @verbatim
//!   Contents/MacOS/<Exe>     the player binary - the RELEASE one when the
//!                            given tree's Release sibling carries it, else
//!                            the given tree's (with a warning). A project
//!                            with a native module ships the MODULE
//!                            executable, built here against the engine tree.
//!   Contents/Frameworks/     the executable's non-system dylib closure plus
//!                            each dylib's dlopen symlink aliases, rpaths
//!                            rewritten to @executable_path/../Frameworks
//!                            (build-tree rpaths removed) and the binary
//!                            ad-hoc re-signed - the bundle must not depend on
//!                            this machine's build trees.
//!   Contents/Resources/      Media/ (the flavor's engine media), project/
//!                            (manifest, scenes/, assets/, scripts/), the
//!                            AppIcon.icns and the orkige_project.txt marker.
//! @endverbatim
//!
//! The marker is the no-args default-project mechanism: the runtime reads it
//! from `SDL_GetBasePath()` - Contents/Resources in a mac bundle - so the app
//! launches its project with no arguments.

namespace OrkigeExport
{
	//! @brief package @p project as `<outputDirectory>/<Name>.app`.
	//! @param outArtifact receives the bundle path on success
	//! @return false with an honest @p error naming the missing piece
	//! @param tests a TEST BUILD: carry the project's suite and run it
	//!        (default off, so a shipping bundle is untouched)
	//! @param signing what the finished bundle is sealed with. An unresolved
	//!        one (@ref MacosSigning::real is false) is the DEFAULT: the app
	//!        keeps the ad-hoc signature the closure step gives it, which is
	//!        what it has always shipped with (@see ExportMacosSign.h).
	bool exportMacos(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, PayloadTestRun const & tests,
		MacosSigning const & signing, Orkige::String & outArtifact,
		Orkige::String * error);

	//! @brief the Info.plist an exported macOS app carries
	Orkige::JsonValue macosInfoPlist(ExportProject const & project,
		Orkige::String const & bundleId);

	//! @brief build the project's native module for export against EITHER
	//! render flavor's engine tree: Release against the given tree's Release
	//! sibling when its libraries exist, else the given tree's build type with
	//! a warning.
	//! @remarks a SEPARATE, per-flavor build tree
	//! (`<native.buildDir>-export-<flavor>`) keeps the editor's
	//! compile-on-Play cache untouched AND keeps the two render flavors'
	//! module builds from poisoning each other (a module tree is
	//! flavor-bound).
	//! @param outExecutable receives the built module binary
	//! @param outEngineTree receives the tree it was built against - the same
	//!        tree whose media the bundle must then carry
	bool buildNativeModule(ExportProject const & project,
		Orkige::String const & target, Orkige::String const & buildDirectory,
		ExportEnvironment const & environment, Orkige::String & outExecutable,
		Orkige::String & outEngineTree, Orkige::String * error);

	//! @brief build the project's native module against an installed SDK pack
	//! - what a DISTRIBUTED editor has instead of a build tree
	//! (Docs/sdk-pack.md).
	//! @remarks The pack records its own configuration, flavor, scripting
	//! backend, compile contract and OS floor, so nothing about the engine is
	//! restated here; the module tree is its own
	//! (`<native.buildDir>-export-sdk-<flavor>`) for the same reason the tree
	//! build's is.
	//! @param outExecutable receives the built module binary
	//! @param outBundle receives the app BUNDLE the build produced where the
	//!        pack's target takes that shape (an Apple mobile module), "" where
	//!        it does not - the bundle is what gets installed and packaged, and
	//!        the build writes down which it made rather than leaving the
	//!        packager to derive one from the other
	bool buildNativeModuleFromPack(ExportProject const & project,
		Orkige::String const & target, Orkige::String const & packRoot,
		ExportEnvironment const & environment, Orkige::String & outExecutable,
		Orkige::String * error, Orkige::String * outBundle = 0);
}

#endif //__ExportMacos_h__31_7_2026__18_00_00__
