/**************************************************************
	created:	2026/07/08 at 12:00
	filename: 	NativeModule.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __NativeModule_h__8_7_2026__12_00_00__
#define __NativeModule_h__8_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	class Project;

	//! @brief a project's OPTIONAL native (C++) game module - manifest keys
	//! and the pure build-command assembly the editor's compile-on-Play uses.
	//! @remarks A project opts in by carrying the setting "native.target" in
	//! its manifest (the executable CMake target name). "native.cmakeDir"
	//! (default "native") names the project-relative directory holding the
	//! module's CMakeLists.txt, "native.buildDir" (default "native/build")
	//! its build tree. A project WITHOUT these keys plays through the generic
	//! player exactly as before - Lua-only projects stay zero-compile.
	//!
	//! The module is a standalone CMake project built against the ENGINE via
	//! cmake/OrkigeGameModule.cmake (see that file for the contract); the
	//! resulting executable must implement the player CLI contract -
	//! "[scene.oscene] [--project <dir>] [--debug-port N]"
	//! (parse it with PlayerArguments in engine_runtime/PlayerRuntime.h) -
	//! so the editor can run it as the play process. Everything here is pure
	//! string/filesystem logic so the unit tests cover it headlessly; the
	//! caller supplies the machine-specific pieces (cmake path, extra cache
	//! arguments) from its own build-time constants.
	//!
	//! WHICH ENGINE a module builds against has two forms, and both are
	//! resolved here so compile-on-Play and the exporter never grow two
	//! answers (@see EngineSdk): an engine CHECKOUT plus its build tree (the
	//! developer case, unchanged) or an installed, relocatable SDK PACK
	//! (Docs/sdk-pack.md) - what a downloaded editor has, since it carries
	//! neither a repository nor a build tree.
	namespace NativeModule
	{
		//--- manifest setting keys (project.orkproj <Settings>) ----
		extern const String SETTING_TARGET;		//!< "native.target" (opts the project in)
		extern const String SETTING_CMAKE_DIR;	//!< "native.cmakeDir"
		extern const String SETTING_BUILD_DIR;	//!< "native.buildDir"
		extern const String DEFAULT_CMAKE_DIR;	//!< "native"
		extern const String DEFAULT_BUILD_DIR;	//!< "native/build"

		//! a project's native module configuration, defaults applied
		struct Config
		{
			bool	enabled = false;	//!< "native.target" is set and non-empty
			String	target;			//!< executable CMake target name
			String	cmakeDir;			//!< project-relative CMakeLists dir
			String	buildDir;			//!< project-relative build tree
		};

		//! @brief read the native module configuration from a project's
		//! settings; enabled is false (and the rest defaulted-but-unused)
		//! when the project carries no non-empty "native.target"
		Config configFromProject(Project const & project);

		//! @brief the per-flavor build tree for a module: the config buildDir
		//! (default "native/build") with a "-<flavor>" suffix ("next" or
		//! "classic"). A module tree is flavor-bound like the engine tree it
		//! links (@see cmake/OrkigeGameModule.cmake), so the two render flavors
		//! build into SEPARATE trees that never share a cache - the editor
		//! passes its own compile-time flavor, the exporter the engine tree's.
		String flavoredBuildDir(String const & buildDir, String const & flavor);

		//--- which engine a module builds against -------------------

		//! the form of the engine a native module was resolved to
		enum class EngineSdkKind
		{
			None,		//!< neither a build tree nor an installed pack
			BuildTree,	//!< an engine checkout plus the tree it was built in
			Pack		//!< an installed, relocatable SDK pack
		};

		//! @brief the engine a module configure links against, resolved once
		//! and consumed by BOTH the editor's compile-on-Play and the exporter.
		struct EngineSdk
		{
			EngineSdkKind	kind = EngineSdkKind::None;
			//! the engine SOURCE root (BuildTree) or the PACK root (Pack) -
			//! either way what -DORKIGE_ROOT names, which is why it is one
			//! field: a consumer says "where Orkige is" and the game-module
			//! helper works out which form it was handed
			String			root;
			//! the engine build tree to link (BuildTree only; a pack has none)
			String			buildDir;
			//! the configuration the module MUST be built in: the editor's own
			//! build type against a tree, the one the pack records against a
			//! pack (a pack carries exactly one, and mixing is refused by name)
			String			buildType;
			//! the render flavor the engine archives carry ("next"/"classic";
			//! "" when the package is too old to say)
			String			flavor;
			//! Pack only: the target platform the pack was built for
			String			platform;

			bool found() const { return this->kind != EngineSdkKind::None; }
			bool fromPack() const { return this->kind == EngineSdkKind::Pack; }
		};

		//! the file whose presence beside the game-module helper MARKS a pack
		extern const String PACK_MARKER_FILE;	//!< "cmake/OrkigeSdkPack.cmake"
		//! the pack's find_package config, which records its configuration
		extern const String PACK_CONFIG_FILE;	//!< "cmake/OrkigeConfig.cmake"

		//! @brief pure: the value of a `set(<name> "<value>")` assignment in
		//! cmake @p text, or "" when the file carries no such line.
		//! @remarks A pack describes itself in its own cmake surface, which is
		//! the ONE place those facts live. Reading the two declarations this
		//! layer needs (the pack's target platform and the configuration +
		//! flavor its archives carry) beats restating them in a second file
		//! that could drift from the one a consumer's configure actually reads.
		String cmakeSetValue(String const & text, String const & name);

		//! @brief pure: where an installed pack for @p flavor lives under a
		//! writable state directory - "<state>/sdk/<flavor>".
		//! @remarks Per flavor because a pack is flavor-bound like the engine
		//! it holds, so one machine may hold both without either overwriting
		//! the other.
		String installedPackDirectory(String const & stateDirectory,
			String const & flavor);

		//! @brief read a pack's own description of itself off disk.
		//! @return an EngineSdk of kind Pack, or kind None when @p packRoot
		//! carries no pack (no marker, or no find_package config beside it).
		EngineSdk describePack(String const & packRoot);

		//! @brief the engine to build against: the BUILD TREE when one is
		//! reachable (the developer case - a configured tree with the engine
		//! sources beside it), else an installed SDK pack, else None.
		//! @remarks The order is the point: a developer running out of a
		//! checkout keeps building against the very engine their editor runs
		//! on, whatever else is installed on the machine.
		EngineSdk resolveEngineSdk(String const & engineRootDirectory,
			String const & engineBuildDirectory, String const & engineBuildType,
			String const & packRootDirectory);

		//--- the toolchain (which we never ship) --------------------

		//! @brief the build programs a module configure needs. We ship the
		//! ENGINE, never a compiler: a machine without these is a different
		//! problem from a machine without the SDK, and says so separately.
		struct Toolchain
		{
			String	cmake;			//!< "" when none was found
			String	makeProgram;	//!< the Ninja generator program, "" if none

			bool complete() const
			{
				return !this->cmake.empty() && !this->makeProgram.empty();
			}
		};

		//! pure: split a PATH-style variable into its directories
		StringVector searchPathDirectories(String const & pathVariable);

		//! @brief the first existing `<directory>/<name>` (plus the platform's
		//! executable suffix), or "" when no directory carries it
		String findProgram(String const & name,
			StringVector const & directories);

		//! @brief resolve the build programs: @p preferredCmake /
		//! @p preferredMakeProgram when they exist (the baked developer paths),
		//! else a search over @p searchDirectories (a downloaded editor's PATH)
		Toolchain resolveToolchain(String const & preferredCmake,
			String const & preferredMakeProgram,
			StringVector const & searchDirectories);

		//! @brief pure: what still stands between this project and a built
		//! module - the ONE sentence to show - or "" when the build can start.
		//! @remarks TWO prerequisites, reported as two, because they have
		//! different fixes: a missing SDK is something to install through
		//! Orkige, a missing compiler/cmake/ninja is something to install on
		//! the machine. A pack whose render flavor is not this build's is the
		//! third honest refusal - its archives are the other backend's.
		String modulePrerequisiteProblem(EngineSdk const & engine,
			Toolchain const & tools, String const & expectedFlavor,
			String const & packDirectory, String const & projectName,
			String const & target);

		//! @brief the module build tree for @p engine: "<buildDir>-<flavor>"
		//! against a build tree, "<buildDir>-sdk-<flavor>" against a pack.
		//! @remarks A module tree is bound to the ENGINE it was configured
		//! against, not just to the flavor: the cache holds that engine's
		//! ORKIGE_ROOT and its build type, and a configured tree is only ever
		//! rebuilt incrementally (@see needsConfigure). Two directories keep a
		//! developer's tree build and a pack build from silently inheriting
		//! each other's cache.
		String moduleBuildDirectory(String const & buildDir,
			EngineSdk const & engine, String const & flavor);

		//! does the build tree still need a configure run? (no CMakeCache.txt
		//! yet - `cmake --build` handles re-configures of an existing tree)
		bool needsConfigure(String const & buildDirAbsolute);

		//! @brief assemble the configure command: Ninja generator, explicit
		//! source/build dirs, the configuration @p engine requires and the
		//! ORKIGE_* cache variables OrkigeGameModule.cmake reads;
		//! extraArguments (e.g. -DCMAKE_MAKE_PROGRAM, hermeticity settings)
		//! are appended verbatim.
		//! @remarks ORKIGE_ENGINE_BUILD_DIR travels only for a BUILD TREE - it
		//! is meaningless against a pack, which has no build tree, and the
		//! helper's pack mode never reads it.
		StringVector configureCommand(String const & cmakeExecutable,
			String const & sourceDirAbsolute, String const & buildDirAbsolute,
			EngineSdk const & engine,
			StringVector const & extraArguments = StringVector());

		//! assemble the (incremental) build command for a configured tree
		StringVector buildCommand(String const & cmakeExecutable,
			String const & buildDirAbsolute);

		extern const String ARTIFACT_MANIFEST;	//!< "orkige_module_artifact.txt"

		//! @brief pick the module's artifact out of the manifest the build
		//! WROTE, falling back to the desktop guess when there is none.
		//! @remarks Where a module lands is the build's answer, not a caller's:
		//! only a desktop module is "<buildDir>/<target>". The game-module
		//! helper (@see cmake/OrkigeGameModule.cmake) therefore emits
		//! ARTIFACT_MANIFEST beside the build with an "artifact=<path>" line
		//! the generator resolved exactly, and this parses it. Pure so the
		//! unit tests cover every shape headlessly; @p manifestText empty (no
		//! manifest, or one from a build too old to write it) yields the
		//! legacy "<buildDir>/<target>" - the desktop answer, unchanged.
		String artifactPathFromManifest(String const & manifestText,
			String const & buildDirAbsolute, String const & target);

		//! @brief where the built module landed: the artifact the build tree's
		//! manifest names, else the desktop "<buildDir>/<target>" fallback
		String executablePath(String const & buildDirAbsolute,
			String const & target);
	}
}

#endif //__NativeModule_h__8_7_2026__12_00_00__
