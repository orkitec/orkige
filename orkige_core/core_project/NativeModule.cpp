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
#include <fstream>
#include <sstream>
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
		const String PACK_MARKER_FILE = "cmake/OrkigeSdkPack.cmake";
		const String PACK_CONFIG_FILE = "cmake/OrkigeConfig.cmake";
		//---------------------------------------------------------
		namespace
		{
			//! read a whole text file, "" when it cannot be read
			String readTextFile(std::filesystem::path const & path)
			{
				std::ifstream file(path.string());
				if(!file)
				{
					return String();
				}
				std::ostringstream contents;
				contents << file.rdbuf();
				return contents.str();
			}
			//---------------------------------------------------------
			bool pathExists(String const & path)
			{
				if(path.empty())
				{
					return false;
				}
				std::error_code ignored;
				return std::filesystem::exists(path, ignored);
			}
		}
		//---------------------------------------------------------
		String cmakeSetValue(String const & text, String const & name)
		{
			// `set(<name> "<value>")`, as configure_file writes it: find the
			// declaration at the start of a line, then the quoted argument
			// after the name. Deliberately narrow - this reads two facts out
			// of a generated file, it is not a cmake parser.
			const String opening = "set(" + name;
			for(size_t start = 0; start < text.size(); )
			{
				const size_t end = text.find('\n', start);
				const String line = text.substr(start,
					end == String::npos ? String::npos : end - start);
				start = (end == String::npos) ? text.size() : end + 1;
				if(line.compare(0, opening.size(), opening) != 0)
				{
					continue;
				}
				// the character after the name must end it, so ORKIGE_A never
				// answers for ORKIGE_ABC
				const char after = line.size() > opening.size()
					? line[opening.size()] : ')';
				if(after != ' ' && after != '\t' && after != ')')
				{
					continue;
				}
				const size_t quote = line.find('"', opening.size());
				if(quote == String::npos)
				{
					// an unquoted value (a bare ON/OFF): take it up to the ')'
					const size_t close = line.rfind(')');
					if(close == String::npos || close <= opening.size())
					{
						return String();
					}
					String value = line.substr(opening.size(),
						close - opening.size());
					while(!value.empty() &&
						(value.front() == ' ' || value.front() == '\t'))
					{
						value.erase(value.begin());
					}
					while(!value.empty() &&
						(value.back() == ' ' || value.back() == '\t'))
					{
						value.pop_back();
					}
					return value;
				}
				const size_t closing = line.find('"', quote + 1);
				if(closing == String::npos)
				{
					return String();
				}
				return line.substr(quote + 1, closing - quote - 1);
			}
			return String();
		}
		//---------------------------------------------------------
		String installedPackDirectory(String const & stateDirectory,
			String const & flavor)
		{
			if(stateDirectory.empty())
			{
				return String();
			}
			return (std::filesystem::path(stateDirectory) / "sdk" / flavor)
				.string();
		}
		//---------------------------------------------------------
		EngineSdk describePack(String const & packRoot)
		{
			EngineSdk sdk;
			if(packRoot.empty())
			{
				return sdk;
			}
			const std::filesystem::path root(packRoot);
			const String marker = readTextFile(root / PACK_MARKER_FILE);
			const String config = readTextFile(root / PACK_CONFIG_FILE);
			if(marker.empty() || config.empty())
			{
				// no marker (or a half-unpacked pack that lost its package
				// config): not a pack, and saying so is what makes the
				// "SDK not installed" answer honest
				return sdk;
			}
			sdk.kind = EngineSdkKind::Pack;
			sdk.root = packRoot;
			sdk.platform = cmakeSetValue(marker, "ORKIGE_SDK_TARGET_PLATFORM");
			sdk.buildType = cmakeSetValue(config, "ORKIGE_PACKAGE_BUILD_TYPE");
			sdk.flavor = cmakeSetValue(config, "ORKIGE_PACKAGE_RENDER_BACKEND");
			return sdk;
		}
		//---------------------------------------------------------
		EngineSdk resolveEngineSdk(String const & engineRootDirectory,
			String const & engineBuildDirectory, String const & engineBuildType,
			String const & packRootDirectory)
		{
			// the developer case first and unchanged: a configured build tree
			// with the engine sources beside it is the engine this editor
			// itself runs on, so it wins over anything installed
			if(!engineBuildDirectory.empty() && !engineRootDirectory.empty() &&
				pathExists((std::filesystem::path(engineBuildDirectory) /
					"CMakeCache.txt").string()) &&
				pathExists((std::filesystem::path(engineRootDirectory) /
					"cmake" / "OrkigeGameModule.cmake").string()))
			{
				EngineSdk sdk;
				sdk.kind = EngineSdkKind::BuildTree;
				sdk.root = engineRootDirectory;
				sdk.buildDir = engineBuildDirectory;
				sdk.buildType = engineBuildType;
				return sdk;
			}
			return describePack(packRootDirectory);
		}
		//---------------------------------------------------------
		StringVector searchPathDirectories(String const & pathVariable)
		{
			StringVector directories;
#ifdef _WIN32
			const char separator = ';';
#else
			const char separator = ':';
#endif
			size_t start = 0;
			while(start <= pathVariable.size())
			{
				const size_t end = pathVariable.find(separator, start);
				const String entry = pathVariable.substr(start,
					end == String::npos ? String::npos : end - start);
				if(!entry.empty())
				{
					directories.push_back(entry);
				}
				if(end == String::npos)
				{
					break;
				}
				start = end + 1;
			}
			return directories;
		}
		//---------------------------------------------------------
		String findProgram(String const & name,
			StringVector const & directories)
		{
			for(String const & directory : directories)
			{
				const std::filesystem::path candidate =
					std::filesystem::path(directory) / name;
#ifdef _WIN32
				const String executable = candidate.string() + ".exe";
				if(pathExists(executable))
				{
					return executable;
				}
#endif
				if(pathExists(candidate.string()))
				{
					return candidate.string();
				}
			}
			return String();
		}
		//---------------------------------------------------------
		Toolchain resolveToolchain(String const & preferredCmake,
			String const & preferredMakeProgram,
			StringVector const & searchDirectories)
		{
			Toolchain tools;
			tools.cmake = pathExists(preferredCmake)
				? preferredCmake : findProgram("cmake", searchDirectories);
			tools.makeProgram = pathExists(preferredMakeProgram)
				? preferredMakeProgram : findProgram("ninja", searchDirectories);
			return tools;
		}
		//---------------------------------------------------------
		String modulePrerequisiteProblem(EngineSdk const & engine,
			Toolchain const & tools, String const & expectedFlavor,
			String const & packDirectory, String const & projectName,
			String const & target)
		{
			const String subject = "project '" + projectName +
				"' builds compiled C++ game code (the module '" + target + "')";
			if(!engine.found())
			{
				return subject + ", which needs the Orkige SDK for this build "
					"- and it is not installed. Install the SDK, then press "
					"Play again (it belongs in '" + packDirectory + "'). "
					"Projects whose behaviour is Lua scripts need none of it.";
			}
			if(engine.fromPack() && !expectedFlavor.empty() &&
				!engine.flavor.empty() && engine.flavor != expectedFlavor)
			{
				return subject + ", and the installed SDK holds the '" +
					engine.flavor + "' engine while this Orkige is '" +
					expectedFlavor + "' - its archives are the other render "
					"backend's. Install the '" + expectedFlavor + "' SDK.";
			}
			if(!tools.complete())
			{
				const bool both =
					tools.cmake.empty() && tools.makeProgram.empty();
				String missing;
				if(tools.cmake.empty())
				{
					missing = "cmake";
				}
				if(tools.makeProgram.empty())
				{
					missing += missing.empty() ? "ninja" : " and ninja";
				}
				return subject + ", which needs a C++ build toolchain on this "
					"machine - " + missing + (both ? " are" : " is") +
					" not on the PATH. Orkige ships the engine, never a "
					"compiler: install " + missing + " (and a C++ compiler - on "
					"macOS the Xcode Command Line Tools, `xcode-select "
					"--install`).";
			}
			return String();
		}
		//---------------------------------------------------------
		String moduleBuildDirectory(String const & buildDir,
			EngineSdk const & engine, String const & flavor)
		{
			// a pack build gets its OWN tree: the cache records the engine it
			// was configured against (and that engine's build type), and a
			// configured tree is only ever rebuilt incrementally
			return flavoredBuildDir(buildDir,
				engine.fromPack() ? ("sdk-" + flavor) : flavor);
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
			EngineSdk const & engine, StringVector const & extraArguments)
		{
			StringVector command = {
				cmakeExecutable,
				"-G", "Ninja",
				"-S", sourceDirAbsolute,
				"-B", buildDirAbsolute,
				"-DCMAKE_BUILD_TYPE=" + engine.buildType,
				"-DORKIGE_ROOT=" + engine.root,
			};
			if(!engine.fromPack())
			{
				command.push_back(
					"-DORKIGE_ENGINE_BUILD_DIR=" + engine.buildDir);
			}
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
		const String ARTIFACT_MANIFEST = "orkige_module_artifact.txt";
		//---------------------------------------------------------
		String artifactPathFromManifest(String const & manifestText,
			String const & buildDirAbsolute, String const & target)
		{
			// "artifact=<path>", one key=value per line
			const String key = "artifact=";
			for (size_t start = 0; start < manifestText.size(); )
			{
				const size_t end = manifestText.find('\n', start);
				const String line = manifestText.substr(start,
					end == String::npos ? String::npos : end - start);
				if (line.compare(0, key.size(), key) == 0)
				{
					String value = line.substr(key.size());
					while (!value.empty() &&
						(value.back() == '\r' || value.back() == ' '))
					{
						value.pop_back();
					}
					if (!value.empty())
					{
						return value;
					}
				}
				if (end == String::npos)
				{
					break;
				}
				start = end + 1;
			}
			// no manifest: the desktop shape, which is what a build that does
			// not write one produces
#ifdef _WIN32
			const String executableName = target + ".exe";
#else
			const String executableName = target;
#endif
			return (std::filesystem::path(buildDirAbsolute) / executableName)
				.string();
		}
		//---------------------------------------------------------
		String executablePath(String const & buildDirAbsolute,
			String const & target)
		{
			String manifestText;
			std::ifstream manifest(
				(std::filesystem::path(buildDirAbsolute) / ARTIFACT_MANIFEST)
					.string());
			if (manifest)
			{
				std::ostringstream contents;
				contents << manifest.rdbuf();
				manifestText = contents.str();
			}
			return artifactPathFromManifest(manifestText, buildDirAbsolute,
				target);
		}
		//---------------------------------------------------------
	}
}
