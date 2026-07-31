/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! @brief orkige_export - package an Orkige project as a distributable app.
//!
//! @verbatim
//!   orkige_export --project <dir> --platform macos
//!                 (--engine-build <preset build dir> | --engine-bundle <dir>
//!                  [--engine-tools <dir>])
//!                 [--output <dir>]
//!   orkige_export self-contain --frameworks <dir> [--search <dir>]...
//!                 [--banned <substring>]... <binary>...
//! @endverbatim
//!
//! The engine pieces an export packages (the player binary, the engine media)
//! come from ONE of two sources. `--engine-build` is a preset build tree: the
//! developer case. `--engine-bundle` is a STAGED payload - the Media/ tree and
//! the executables a distributed Orkige carries inside itself - which packages
//! the desktop app on a machine with no repository and no build tree.
//! Everything after the sourcing is the same code, so both produce the same
//! bundle.
//!
//! Output lands in `<project>/builds/<platform>/` (or `--output`). The last
//! line on success is `orkige_export: OK <artifact>` - the editor's Build menu
//! parses it for the reveal-in-Finder nicety, tests for the artifact path.
//!
//! `self-contain` is the same dylib-closure operation an export performs,
//! reachable on its own so the editor BUILD can stage its app the identical
//! way (@see ExportSelfContain.h).

#include "ExportFiles.h"
#include "ExportMacos.h"
#include "ExportProcess.h"
#include "ExportProject.h"
#include "ExportSelfContain.h"
#include "ExportSettings.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	using Orkige::String;

	void logLine(String const & message)
	{
		std::printf("orkige_export: %s\n", message.c_str());
		std::fflush(stdout);
	}

	int fail(String const & message)
	{
		std::printf("orkige_export: ERROR: %s\n", message.c_str());
		std::fflush(stdout);
		return 1;
	}

	//! the engine source tree this binary was built in - the fallback for the
	//! media an export bundles and the module builds it drives, overridable
	//! with --repo (a distributed copy passes --engine-bundle instead and
	//! needs neither)
	String defaultRepoRoot()
	{
#ifdef ORKIGE_EXPORT_REPO_ROOT
		return ORKIGE_EXPORT_REPO_ROOT;
#else
		return "";
#endif
	}

	//! the neutral engine icon an export falls back to when a project sets no
	//! export.icon
	String defaultIconPath(String const & repoRoot)
	{
#ifdef ORKIGE_EXPORT_DEFAULT_ICON
		const String baked = ORKIGE_EXPORT_DEFAULT_ICON;
		if(OrkigeExport::ExportFiles::isRegularFile(baked))
		{
			return baked;
		}
#endif
		return repoRoot.empty() ? String()
			: OrkigeExport::ExportFiles::join(repoRoot,
				"Util/media/orkige_default_icon.png");
	}

	//---------------------------------------------------------
	//! the editor build's staging call: the same operation an export performs
	int selfContainMain(std::vector<String> const & arguments)
	{
		OrkigeExport::SelfContainRequest request;
		std::vector<String> binaries;
		std::vector<String> banned;
		for(std::size_t index = 0; index < arguments.size(); ++index)
		{
			String const & argument = arguments[index];
			const bool hasValue = index + 1 < arguments.size();
			if(argument == "--frameworks" && hasValue)
			{
				request.frameworksDirectory = arguments[++index];
			}
			else if(argument == "--search" && hasValue)
			{
				request.searchDirectories.push_back(arguments[++index]);
			}
			else if(argument == "--banned" && hasValue)
			{
				banned.push_back(arguments[++index]);
			}
			else if(argument.rfind("--", 0) == 0)
			{
				return fail("unknown self-contain argument '" + argument + "'");
			}
			else
			{
				binaries.push_back(argument);
			}
		}
		if(request.frameworksDirectory.empty() || binaries.empty())
		{
			return fail("self-contain needs --frameworks <dir> and at least "
				"one binary");
		}
		request.bannedRpathMarkers = banned.empty()
			? std::vector<String>{ "vcpkg_installed" } : banned;
		for(String const & binary : binaries)
		{
			if(!OrkigeExport::ExportFiles::isRegularFile(binary))
			{
				// a flavor that never builds a given tool simply has no binary
				// to retarget; that is not a failure of the staging step
				logLine("skipping absent binary " + binary);
				continue;
			}
			request.executable = binary;
			String error;
			if(!OrkigeExport::makeSelfContained(request,
				OrkigeExport::defaultProcessRunner(), logLine, &error))
			{
				return fail(error);
			}
		}
		return 0;
	}
}

//---------------------------------------------------------
int main(int argc, char ** argv)
{
	std::vector<String> arguments(argv + 1, argv + argc);
	if(!arguments.empty() && arguments[0] == "self-contain")
	{
		return selfContainMain(
			std::vector<String>(arguments.begin() + 1, arguments.end()));
	}

	String projectPath;
	String platform;
	String engineBuild;
	String engineBundle;
	String engineTools;
	String output;
	String repoRoot = defaultRepoRoot();
	String cmakeProgram;
	String ninjaProgram;
	for(std::size_t index = 0; index < arguments.size(); ++index)
	{
		String const & argument = arguments[index];
		if(index + 1 >= arguments.size())
		{
			return fail("missing value for '" + argument + "'");
		}
		String const & value = arguments[++index];
		if(argument == "--project") { projectPath = value; }
		else if(argument == "--platform") { platform = value; }
		else if(argument == "--engine-build") { engineBuild = value; }
		else if(argument == "--engine-bundle") { engineBundle = value; }
		else if(argument == "--engine-tools") { engineTools = value; }
		else if(argument == "--output") { output = value; }
		else if(argument == "--repo") { repoRoot = value; }
		else if(argument == "--cmake") { cmakeProgram = value; }
		else if(argument == "--ninja") { ninjaProgram = value; }
		else { return fail("unknown argument '" + argument + "'"); }
	}
	if(projectPath.empty() || platform.empty())
	{
		std::fprintf(stderr,
			"usage: orkige_export --project <dir> --platform macos\n"
			"                     (--engine-build <preset build dir> |\n"
			"                      --engine-bundle <dir> [--engine-tools "
			"<dir>])\n"
			"                     [--output <dir>]\n"
			"       orkige_export self-contain --frameworks <dir> "
			"[--search <dir>]... <binary>...\n");
		return 2;
	}
	if(platform != "macos")
	{
		return fail("'" + platform + "' is not a platform this exporter "
			"packages yet");
	}

	OrkigeExport::ExportProject project;
	String error;
	if(!OrkigeExport::ExportProject::readManifest(projectPath, project,
		&error))
	{
		return fail(error);
	}

	OrkigeExport::EngineSource source;
	if(!engineBundle.empty())
	{
		if(!engineBuild.empty())
		{
			return fail("--engine-bundle and --engine-build name two "
				"different engine sources - pass one");
		}
		source.bundleResources = OrkigeExport::ExportFiles::absolute(
			engineBundle);
		source.bundleTools = engineTools.empty() ? source.bundleResources
			: OrkigeExport::ExportFiles::absolute(engineTools);
		if(!OrkigeExport::ExportFiles::isDirectory(source.bundleResources))
		{
			return fail("engine payload '" + source.bundleResources +
				"' does not exist");
		}
	}
	else
	{
		if(engineBuild.empty())
		{
			return fail("no engine source: pass --engine-build <preset build "
				"tree> or --engine-bundle <staged engine payload>");
		}
		source.buildDirectory =
			OrkigeExport::ExportFiles::absolute(engineBuild);
		if(!OrkigeExport::ExportFiles::isDirectory(source.buildDirectory))
		{
			return fail("engine build tree '" + source.buildDirectory +
				"' does not exist");
		}
	}

	const String outputDirectory = OrkigeExport::ExportFiles::absolute(
		output.empty()
			? OrkigeExport::ExportFiles::join(
				OrkigeExport::ExportFiles::join(project.root, "builds"),
				platform)
			: output);
	if(!OrkigeExport::ExportFiles::makeDirectories(outputDirectory, &error))
	{
		return fail(error);
	}
	logLine("project '" + project.name + "' -> " + outputDirectory + " (" +
		platform + ")");

	OrkigeExport::ExportEnvironment environment;
	environment.repoRoot = repoRoot;
	environment.defaultIconPath = defaultIconPath(repoRoot);
	environment.log = logLine;
	environment.runner = OrkigeExport::defaultProcessRunner();
	environment.cmake = cmakeProgram.empty()
		? (OrkigeExport::findOnPath("cmake").empty()
			? String("cmake") : OrkigeExport::findOnPath("cmake"))
		: cmakeProgram;
	environment.ninja = ninjaProgram.empty()
		? OrkigeExport::findOnPath("ninja") : ninjaProgram;

	String artifact;
	if(!OrkigeExport::exportMacos(project, source, outputDirectory,
		environment, artifact, &error))
	{
		return fail(error);
	}
	logLine("artifact size " + OrkigeExport::humanSize(
		OrkigeExport::ExportFiles::treeSize(artifact)));
	// the machine-readable contract every caller keys on
	std::printf("orkige_export: OK %s\n", artifact.c_str());
	std::fflush(stdout);
	return 0;
}
