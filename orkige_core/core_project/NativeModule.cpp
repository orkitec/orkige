/**************************************************************
	created:	2026/07/08 at 12:00
	filename: 	NativeModule.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_project/NativeModule.h"
#include "core_project/Project.h"

#include <filesystem>
#include <system_error>

namespace Orkige
{
	namespace NativeModule
	{
		const String SETTING_TARGET = "native.target";
		const String SETTING_CMAKE_DIR = "native.cmakeDir";
		const String SETTING_BUILD_DIR = "native.buildDir";
		const String DEFAULT_CMAKE_DIR = "native";
		const String DEFAULT_BUILD_DIR = "native/build";
		//---------------------------------------------------------
		Config configFromProject(Project const & project)
		{
			Config config;
			config.target = project.getSetting(SETTING_TARGET);
			config.cmakeDir = project.getSetting(SETTING_CMAKE_DIR,
				DEFAULT_CMAKE_DIR);
			config.buildDir = project.getSetting(SETTING_BUILD_DIR,
				DEFAULT_BUILD_DIR);
			config.enabled = !config.target.empty();
			return config;
		}
		//---------------------------------------------------------
		String flavoredBuildDir(String const & buildDir, String const & flavor)
		{
			return buildDir + "-" + flavor;
		}
		//---------------------------------------------------------
		bool needsConfigure(String const & buildDirAbsolute)
		{
			std::error_code ignored;
			return !std::filesystem::exists(
				std::filesystem::path(buildDirAbsolute) / "CMakeCache.txt",
				ignored);
		}
		//---------------------------------------------------------
		StringVector configureCommand(String const & cmakeExecutable,
			String const & sourceDirAbsolute, String const & buildDirAbsolute,
			String const & engineRootDirectory,
			String const & engineBuildDirectory, String const & buildType,
			StringVector const & extraArguments)
		{
			StringVector command = {
				cmakeExecutable,
				"-G", "Ninja",
				"-S", sourceDirAbsolute,
				"-B", buildDirAbsolute,
				"-DCMAKE_BUILD_TYPE=" + buildType,
				"-DORKIGE_ROOT=" + engineRootDirectory,
				"-DORKIGE_ENGINE_BUILD_DIR=" + engineBuildDirectory,
			};
			command.insert(command.end(), extraArguments.begin(),
				extraArguments.end());
			return command;
		}
		//---------------------------------------------------------
		StringVector buildCommand(String const & cmakeExecutable,
			String const & buildDirAbsolute)
		{
			return { cmakeExecutable, "--build", buildDirAbsolute };
		}
		//---------------------------------------------------------
		String executablePath(String const & buildDirAbsolute,
			String const & target)
		{
#ifdef _WIN32
			const String executableName = target + ".exe";
#else
			const String executableName = target;
#endif
			return (std::filesystem::path(buildDirAbsolute) / executableName)
				.string();
		}
		//---------------------------------------------------------
	}
}
