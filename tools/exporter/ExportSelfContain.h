/********************************************************************
	created:	Friday 2026/07/31 at 16:00
	filename: 	ExportSelfContain.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportSelfContain_h__31_7_2026__16_00_00__
#define __ExportSelfContain_h__31_7_2026__16_00_00__

#include "ExportProcess.h"

#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file ExportSelfContain.h
//! @brief make a macOS binary inside an app bundle self-contained.
//!
//! A binary linked against vcpkg dylibs carries `@rpath` dependencies plus
//! build-tree rpaths, so a COPY of the app on another machine dies in dyld
//! before `main`. This copies the non-system dylib closure into the bundle's
//! `Contents/Frameworks`, points the binary there
//! (`@executable_path/../Frameworks`), REMOVES every build-tree rpath - so a
//! missing dylib fails on the build machine rather than on a user's - and
//! ad-hoc re-signs, which `install_name_tool` makes necessary on arm64.
//!
//! ONE implementation, two callers: an exported game, and the editor bundle
//! the build stages. An app the build stages and an app the exporter writes
//! are therefore self-contained the same way.
//!
//! The parsing of `otool` output is separated from the running of it, so the
//! decisions - which dependency is a system library, which rpath belongs to
//! the build machine, which symlinks are a versioned dylib's loader aliases -
//! are all testable without a Mach-O binary to hand.

namespace OrkigeExport
{
	//! @brief one non-system dependency: the install name as written into the
	//! binary, and the file it resolved to
	struct DylibDependency
	{
		Orkige::String	dependency;	//!< "@rpath/libfoo.dylib" or an abs path
		Orkige::String	resolved;	//!< the file it was found at
	};

	//! @brief the non-system dependencies named in `otool -L` output.
	//! @remarks `/usr/lib/` and `/System/` are the OS's own and stay dynamic;
	//! everything else has to ride inside the bundle. PURE - it takes the tool
	//! output as text.
	std::vector<Orkige::String> parseOtoolDependencies(
		Orkige::String const & otoolOutput);

	//! @brief the `LC_RPATH` entries named in `otool -l` output (PURE)
	std::vector<Orkige::String> parseOtoolRpaths(
		Orkige::String const & otoolOutput);

	//! @brief does an rpath belong to the machine that built the binary?
	//! Those must be gone from a shipped app: with one left in, a missing
	//! dylib silently resolves on the build machine and fails on a user's.
	bool isBuildMachineRpath(Orkige::String const & rpath,
		std::vector<Orkige::String> const & bannedMarkers);

	//! @brief the symlink leaf names in @p directory that resolve to
	//! @p dylibName - the dlopen aliases of a versioned dylib (libfoo.dylib
	//! and libfoo.1.dylib -> libfoo.1.2.3.dylib). A leaf-name dlopen (the
	//! Vulkan loader probe in the render system) asks for the unversioned
	//! names, so a self-contained bundle must carry them beside the real file.
	std::vector<Orkige::String> dylibAliases(Orkige::String const & directory,
		Orkige::String const & dylibName);

	//! @brief resolve a dependency against @p searchDirectories: an
	//! `@rpath/name` is looked up by leaf name, an absolute path stands for
	//! itself. Empty when it resolves nowhere.
	Orkige::String resolveDylibDependency(Orkige::String const & dependency,
		std::vector<Orkige::String> const & searchDirectories);

	//! @brief the inputs of one self-contain pass
	struct SelfContainRequest
	{
		Orkige::String					executable;
		Orkige::String					frameworksDirectory;
		std::vector<Orkige::String>		searchDirectories;
		//! substrings that mark an rpath as belonging to the build machine
		std::vector<Orkige::String>		bannedRpathMarkers;
	};

	//! @brief copy the closure into the bundle, retarget the binary at it,
	//! delete every build-machine rpath and ad-hoc re-sign.
	//! @param runner the platform-tool seam (`install_name_tool`, `codesign`)
	//! @param log receives one line per bundled dylib and alias
	//! @return false with an @p error naming the dependency that resolved
	//!         nowhere, or the tool call that failed
	bool makeSelfContained(SelfContainRequest const & request,
		ProcessRunner const & runner,
		std::function<void(Orkige::String const &)> const & log,
		Orkige::String * error);
}

#endif //__ExportSelfContain_h__31_7_2026__16_00_00__
