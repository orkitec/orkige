/********************************************************************
	created:	Wednesday 2026/08/05 at 15:00
	filename: 	ExportLinux.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportLinux.h"

#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportSettings.h"

#include <string>

namespace OrkigeExport
{
	namespace
	{
		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		void emit(ExportLog const & log, Orkige::String const & message)
		{
			if(log)
			{
				log(message);
			}
		}
	}
	//---------------------------------------------------------
	Orkige::String linuxAppDirectoryName(ExportProject const & project)
	{
		return project.exeName();
	}
	//---------------------------------------------------------
	bool exportLinux(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, PayloadTestRun const & tests,
		Orkige::String & outArtifact, Orkige::String * error)
	{
		if(!project.nativeTarget().empty())
		{
			// compiled game code is built here, and the build the exporter
			// drives is written against an Apple toolchain today (it pins an
			// architecture and a sysroot that mean nothing on Linux). Saying
			// so beats configuring a module tree that would reconfigure itself
			// on every run and link the wrong thing if it ever succeeded.
			return report(error, "project '" + project.name + "' builds "
				"compiled C++ game code ('" + project.nativeTarget() + "'), "
				"which the Linux package does not build yet - export it for "
				"macOS, or ship the project's behaviour as Lua");
		}

		Orkige::String executable;
		Orkige::String sourceTree;
		Orkige::String flavor;
		if(source.fromBundle())
		{
			// packaging from a distributed app's own payload: no build tree
			// exists on this machine, so both the player and the media come
			// out of the app
			if(!ExportFiles::isDirectory(
				ExportFiles::join(source.bundleResources, "Media")))
			{
				return report(error, "no engine media at '" +
					ExportFiles::join(source.bundleResources, "Media") +
					"' - the Orkige this export runs from carries no "
					"packageable engine payload");
			}
			executable = bundledEngineTool(source, "orkige_player");
			if(executable.empty())
			{
				return report(error, "no player executable in '" +
					(source.bundleTools.empty() ? source.bundleResources
						: source.bundleTools) + "' - the Orkige this export "
					"runs from carries no packageable engine payload");
			}
			flavor = bundleFlavor(source);
			emit(environment.log, "packaging the engine payload in '" +
				source.bundleResources + "'");
		}
		else if(source.buildDirectory.empty())
		{
			return report(error, "a Linux package needs an engine build tree "
				"or a staged engine payload - an Orkige SDK pack alone is the "
				"engine a native module is BUILT against, not a game to "
				"assemble around");
		}
		else
		{
			// prefer the RELEASE player: a Debug one runs far slower - but
			// never for a TEST BUILD, which exists to judge the tree it was
			// pointed at (@see prefersSiblingReleasePlayer)
			executable = ExportFiles::join(
				ExportFiles::join(source.buildDirectory, "tools/player"),
				"orkige_player");
			sourceTree = source.buildDirectory;
			if(tests.enabled)
			{
				emit(environment.log, "test build: packaging the player from "
					"'" + source.buildDirectory + "' (the named tree), not a "
					"release sibling - a suite judges the runtime this tree "
					"produced");
			}
			if(prefersSiblingReleasePlayer(
				readCMakeCache(source.buildDirectory, "CMAKE_BUILD_TYPE"),
				tests.enabled))
			{
				const Orkige::String releaseTree =
					siblingReleaseTree(source.buildDirectory);
				const Orkige::String releasePlayer = releaseTree.empty()
					? Orkige::String()
					: ExportFiles::join(
						ExportFiles::join(releaseTree, "tools/player"),
						"orkige_player");
				if(!releasePlayer.empty() &&
					ExportFiles::isRegularFile(releasePlayer))
				{
					executable = releasePlayer;
					sourceTree = releaseTree;
					emit(environment.log, "using the release player '" +
						executable + "'");
				}
				else if(!releaseTree.empty())
				{
					emit(environment.log, "WARNING: no release player at '" +
						releasePlayer + "' - exporting the DEBUG player (build "
						"the release preset for shippable speed)");
				}
			}
			if(!ExportFiles::isRegularFile(executable))
			{
				return report(error, "no player binary at '" + executable +
					"' - build the preset first");
			}
			flavor = renderBackend(sourceTree);
		}

		Orkige::String backendMedia;
		if(!source.fromBundle())
		{
			backendMedia = (flavor == "next")
				? ogreNextMediaDirectory(sourceTree)
				: ogreMediaDirectory(sourceTree);
			if(backendMedia.empty())
			{
				return report(error, Orkige::String("no vcpkg ") +
					(flavor == "next" ? "Ogre-Next" : "OGRE") +
					" media under '" + sourceTree + "'");
			}
		}

		// the artifact is one directory holding everything, which is what a
		// Linux game is distributed as - copied or archived whole, run from
		// inside itself
		const Orkige::String appDirectory =
			ExportFiles::join(outputDirectory, linuxAppDirectoryName(project));
		if(!ExportFiles::removeTree(appDirectory, error) ||
			!ExportFiles::makeDirectories(appDirectory, error))
		{
			return false;
		}

		const Orkige::String packagedExecutable =
			ExportFiles::join(appDirectory, project.exeName());
		if(!ExportFiles::copyFile(executable, packagedExecutable, error) ||
			!ExportFiles::makeExecutable(packagedExecutable, error))
		{
			return false;
		}
		// no closure step: the Linux dependency closure is linked statically,
		// so the binary is whole as built and the machine supplies the rest
		// (its C/C++ runtimes and its own display, driver and audio libraries)
		emit(environment.log, "player " + project.exeName() +
			" (statically linked - no bundled libraries)");

		if(source.fromBundle())
		{
			// a staged payload's Media/ IS this layout already (it is what the
			// app renders from), so the whole tree copies across as one piece
			if(!ExportFiles::copyTree(
				ExportFiles::join(source.bundleResources, "Media"),
				ExportFiles::join(appDirectory, "Media"), error, 0))
			{
				return false;
			}
		}
		else if(!stageEngineMediaFromTree(appDirectory, backendMedia, flavor,
			engineSourceMedia(environment.repoRoot, flavor), error))
		{
			return false;
		}

		int staged = 0;
		if(!stageProjectPayload(project,
			ExportFiles::join(appDirectory, PAYLOAD_DIR_NAME),
			cookPlatformToken("linux"), flavor, environment.log, &staged,
			error))
		{
			return false;
		}
		if(tests.enabled)
		{
			int testFiles = 0;
			if(!stageTestSuite(project,
				ExportFiles::join(appDirectory, PAYLOAD_DIR_NAME),
				environment.log, &testFiles, error))
			{
				return false;
			}
			staged += testFiles;
		}
		// the marker sits beside the binary, which is what SDL_GetBasePath()
		// answers for a binary run out of its own directory - so the game
		// boots its project with no arguments
		if(!writeProjectMarker(appDirectory, tests, error))
		{
			return false;
		}
		// the third-party license notices ride beside the marker: the linked
		// libraries require their text to travel with the binary, and this
		// directory is the binary a player receives
		if(!stageThirdPartyNotices(appDirectory, source, environment, 0, error))
		{
			return false;
		}
		emit(environment.log,
			"project payload: " + std::to_string(staged) + " files");
		// said out loud rather than left to be noticed: the package is a
		// portable directory, and where its icon and menu entry come from is a
		// question the desktop environment's install answers, not this
		emit(environment.log, "portable directory: run './" +
			project.exeName() + "' inside it (no desktop entry or icon is "
			"written)");
		outArtifact = appDirectory;
		return true;
	}
}
