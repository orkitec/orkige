/********************************************************************
	created:	Wednesday 2026/08/05 at 22:30
	filename: 	ExportWindows.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportWindows.h"

#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportSettings.h"

#include <algorithm>
#include <cctype>
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
		//---------------------------------------------------------
		//! upper-case an ASCII name for a case-insensitive comparison. The
		//! names being compared are alphanumeric by construction, so no locale
		//! enters into it.
		Orkige::String upperAscii(Orkige::String const & text)
		{
			Orkige::String out;
			out.reserve(text.size());
			for(char character : text)
			{
				out += static_cast<char>(std::toupper(
					static_cast<unsigned char>(character)));
			}
			return out;
		}
		//---------------------------------------------------------
		//! does @p name end in @p extension, compared case-insensitively (a
		//! linker may write .DLL as readily as .dll)?
		bool hasExtension(Orkige::String const & name,
			Orkige::String const & extension)
		{
			if(name.size() <= extension.size())
			{
				return false;
			}
			return upperAscii(name.substr(name.size() - extension.size())) ==
				upperAscii(extension);
		}
	}
	//---------------------------------------------------------
	bool isWindowsReservedName(Orkige::String const & stem)
	{
		const Orkige::String upper = upperAscii(stem);
		if(upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
		{
			return true;
		}
		// COM1-COM9 and LPT1-LPT9. COM0/LPT0 are not reserved, and neither is a
		// two-digit form - the reservation is exactly these eighteen names.
		if(upper.size() == 4 && (upper.compare(0, 3, "COM") == 0 ||
			upper.compare(0, 3, "LPT") == 0))
		{
			return upper[3] >= '1' && upper[3] <= '9';
		}
		return false;
	}
	//---------------------------------------------------------
	Orkige::String windowsAppDirectoryName(ExportProject const & project)
	{
		const Orkige::String name = project.exeName();
		if(!isWindowsReservedName(name))
		{
			return name;
		}
		// the artifact could not be WRITTEN under this name, so it gets one
		// that stays alphanumeric and still reads as the game it came from
		return name + "Game";
	}
	//---------------------------------------------------------
	Orkige::String windowsExecutableName(ExportProject const & project)
	{
		return windowsAppDirectoryName(project) + ".exe";
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> windowsCompanionLibraries(
		std::vector<Orkige::String> const & siblingFiles)
	{
		std::vector<Orkige::String> libraries;
		for(Orkige::String const & name : siblingFiles)
		{
			// only DLLs, and only the ones sitting directly beside the binary.
			// Everything else a build directory holds - import libraries,
			// debug databases, incremental-link state, other targets - belongs
			// to the machine that built the game, not to the game.
			if(hasExtension(name, ".dll"))
			{
				libraries.push_back(name);
			}
		}
		return libraries;
	}
	//---------------------------------------------------------
	bool exportWindows(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, PayloadTestRun const & tests,
		Orkige::String & outArtifact, Orkige::String * error)
	{
		if(!project.nativeTarget().empty())
		{
			// compiled game code is built here, and the build the exporter
			// drives is written against an Apple toolchain today (it pins an
			// architecture and a sysroot that mean nothing on Windows). Saying
			// so beats configuring a module tree that would reconfigure itself
			// on every run and link the wrong thing if it ever succeeded.
			return report(error, "project '" + project.name + "' builds "
				"compiled C++ game code ('" + project.nativeTarget() + "'), "
				"which the Windows package does not build yet - export it for "
				"macOS, or ship the project's behaviour as Lua");
		}

		Orkige::String executable;
		Orkige::String playerDirectory;
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
			return report(error, "a Windows package needs an engine build tree "
				"or a staged engine payload - an Orkige SDK pack alone is the "
				"engine a native module is BUILT against, not a game to "
				"assemble around");
		}
		else
		{
			// prefer the RELEASE player: a Debug one runs far slower - but
			// never for a TEST BUILD, which exists to judge the tree it was
			// pointed at (@see prefersSiblingReleasePlayer)
			playerDirectory =
				ExportFiles::join(source.buildDirectory, "tools/player");
			executable =
				ExportFiles::join(playerDirectory, "orkige_player.exe");
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
				const Orkige::String releaseDirectory = releaseTree.empty()
					? Orkige::String()
					: ExportFiles::join(releaseTree, "tools/player");
				const Orkige::String releasePlayer = releaseDirectory.empty()
					? Orkige::String()
					: ExportFiles::join(releaseDirectory,
						"orkige_player.exe");
				if(!releasePlayer.empty() &&
					ExportFiles::isRegularFile(releasePlayer))
				{
					executable = releasePlayer;
					playerDirectory = releaseDirectory;
					sourceTree = releaseTree;
					emit(environment.log, "using the release player '" +
						executable + "'");
				}
				else if(!releaseTree.empty())
				{
					emit(environment.log, "WARNING: no release player at '" +
						releasePlayer + "' - exporting the DEBUG player. A "
						"debug build links the DEBUG C runtime, which is not "
						"redistributable and is absent from a machine with no "
						"Visual Studio - build the release preset for a "
						"package other people can run");
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
		// Windows game is distributed as - copied or archived whole, run from
		// inside itself
		const Orkige::String appDirectory =
			ExportFiles::join(outputDirectory,
				windowsAppDirectoryName(project));
		if(!ExportFiles::removeTree(appDirectory, error) ||
			!ExportFiles::makeDirectories(appDirectory, error))
		{
			return false;
		}

		const Orkige::String packagedExecutable =
			ExportFiles::join(appDirectory, windowsExecutableName(project));
		if(!ExportFiles::copyFile(executable, packagedExecutable, error))
		{
			return false;
		}
		// no executable bit is set, and that is not an omission: Windows takes
		// executability from the .exe extension, so a POSIX permission call
		// here would be a no-op dressed up as a guarantee.

		// what rides beside the binary is ENUMERATED rather than assumed. The
		// closure is linked statically (VCPKG_LIBRARY_LINKAGE static in the
		// x64-windows-static-md triplet), so this is expected to find nothing;
		// it exists so a dependency that ever does build as a DLL travels with
		// the game instead of leaving a package that cannot start.
		std::vector<Orkige::String> companions;
		if(!playerDirectory.empty())
		{
			std::vector<Orkige::String> siblings;
			for(Orkige::String const & relative :
				ExportFiles::listFilesRecursive(playerDirectory))
			{
				// the files BESIDE the binary only - a build directory's
				// subtrees are its own bookkeeping
				if(relative.find('/') == Orkige::String::npos &&
					relative.find('\\') == Orkige::String::npos)
				{
					siblings.push_back(relative);
				}
			}
			companions = windowsCompanionLibraries(siblings);
		}
		for(Orkige::String const & library : companions)
		{
			if(!ExportFiles::copyFile(
				ExportFiles::join(playerDirectory, library),
				ExportFiles::join(appDirectory, library), error))
			{
				return false;
			}
		}
		if(companions.empty())
		{
			emit(environment.log, "player " + windowsExecutableName(project) +
				" (statically linked - no bundled libraries)");
		}
		else
		{
			emit(environment.log, "player " + windowsExecutableName(project) +
				" (" + std::to_string(companions.size()) +
				" companion librar" + (companions.size() == 1 ? "y" : "ies") +
				" beside it)");
		}

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
			cookPlatformToken("windows"), flavor, environment.log, &staged,
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
		// portable directory, and where its Start-menu entry and its icon come
		// from is a question an installer answers, not this
		emit(environment.log, "portable directory: run '" +
			windowsExecutableName(project) + "' inside it (no shortcut is "
			"written, and the executable carries no icon resource)");
		outArtifact = appDirectory;
		return true;
	}
}
