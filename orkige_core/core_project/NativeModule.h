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
	//! The module is a standalone CMake project built against the ENGINE'S
	//! build tree via cmake/OrkigeGameModule.cmake (see that file for the
	//! contract); the resulting executable must implement the player CLI
	//! contract - "[scene.oscene] [--project <dir>] [--debug-port N]"
	//! (parse it with PlayerArguments in engine_runtime/PlayerRuntime.h) -
	//! so the editor can run it as the play process. Everything here is pure
	//! string/filesystem logic so the unit tests cover it headlessly; the
	//! editor supplies the machine-specific pieces (cmake path, engine build
	//! dir, extra cache arguments) from its own build-time constants.
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

		//! does the build tree still need a configure run? (no CMakeCache.txt
		//! yet - `cmake --build` handles re-configures of an existing tree)
		bool needsConfigure(String const & buildDirAbsolute);

		//! @brief assemble the configure command: Ninja generator, explicit
		//! source/build dirs, CMAKE_BUILD_TYPE and the two ORKIGE_* cache
		//! variables OrkigeGameModule.cmake requires; extraArguments (e.g.
		//! -DCMAKE_MAKE_PROGRAM, hermeticity settings) are appended verbatim
		StringVector configureCommand(String const & cmakeExecutable,
			String const & sourceDirAbsolute, String const & buildDirAbsolute,
			String const & engineRootDirectory,
			String const & engineBuildDirectory, String const & buildType,
			StringVector const & extraArguments = StringVector());

		//! assemble the (incremental) build command for a configured tree
		StringVector buildCommand(String const & cmakeExecutable,
			String const & buildDirAbsolute);

		//! where the built executable lands (Ninja: <buildDir>/<target>)
		String executablePath(String const & buildDirAbsolute,
			String const & target);
	}
}

#endif //__NativeModule_h__8_7_2026__12_00_00__
