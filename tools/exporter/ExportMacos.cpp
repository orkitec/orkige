/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportMacos.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportMacos.h"

#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportIcons.h"
#include "ExportImage.h"
#include "ExportPlist.h"
#include "ExportSelfContain.h"
#include "ExportSettings.h"

#include "core_project/NativeModule.h"

#include <filesystem>
#include <string>
#include <vector>

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
		//! run a platform tool, echoing the command line the way a shell would
		//! show it; a nonzero exit fails the export with the tool's own output
		bool runTool(ExportEnvironment const & environment,
			std::vector<Orkige::String> const & arguments,
			Orkige::String * error)
		{
			emit(environment.log, "$ " + commandLine(arguments));
			const ProcessResult result = environment.runner(arguments);
			if(!result.launched)
			{
				return report(error, "could not run '" + arguments[0] + "'");
			}
			if(result.exitCode != 0)
			{
				return report(error, "command failed (exit " +
					std::to_string(result.exitCode) + "): " + arguments[0] +
					(result.output.empty() ? "" : " - " + result.output));
			}
			return true;
		}
		//---------------------------------------------------------
		//! the staged payload's executable @p name, or "" when it lacks it
		Orkige::String bundledTool(EngineSource const & source,
			Orkige::String const & name)
		{
			const Orkige::String tools = source.bundleTools.empty()
				? source.bundleResources : source.bundleTools;
			const Orkige::String path = ExportFiles::join(tools, name);
			return ExportFiles::isRegularFile(path) ? path : Orkige::String();
		}
		//---------------------------------------------------------
		//! the flavor a STAGED payload names through its own shader tree, so a
		//! caller cannot tell the exporter a flavor the media does not have
		Orkige::String bundleFlavor(EngineSource const & source)
		{
			const Orkige::String media =
				ExportFiles::join(source.bundleResources, "Media");
			return ExportFiles::isDirectory(ExportFiles::join(media, "Hlms"))
				? "next" : "classic";
		}
	}
	//---------------------------------------------------------
	Orkige::JsonValue macosInfoPlist(ExportProject const & project,
		Orkige::String const & bundleId)
	{
		Orkige::JsonValue info = Orkige::JsonValue::object();
		info.set("CFBundleDevelopmentRegion", Orkige::JsonValue("en"));
		info.set("CFBundleExecutable", Orkige::JsonValue(project.exeName()));
		info.set("CFBundleIdentifier", Orkige::JsonValue(bundleId));
		info.set("CFBundleInfoDictionaryVersion", Orkige::JsonValue("6.0"));
		info.set("CFBundleName", Orkige::JsonValue(project.name));
		info.set("CFBundleDisplayName", Orkige::JsonValue(project.name));
		info.set("CFBundlePackageType", Orkige::JsonValue("APPL"));
		info.set("CFBundleShortVersionString", Orkige::JsonValue("1.0"));
		info.set("CFBundleVersion", Orkige::JsonValue("1"));
		info.set("LSMinimumSystemVersion", Orkige::JsonValue("11.0"));
		info.set("NSHighResolutionCapable", Orkige::JsonValue(true));
		// CFBundleIconFile is what macOS reads; CFBundleIconName is the modern
		// spelling (harmless, and future-proofs an asset-catalog move)
		info.set("CFBundleIconFile", Orkige::JsonValue("AppIcon"));
		info.set("CFBundleIconName", Orkige::JsonValue("AppIcon"));
		return info;
	}
	//---------------------------------------------------------
	bool buildNativeModule(ExportProject const & project,
		Orkige::String const & target, Orkige::String const & buildDirectory,
		ExportEnvironment const & environment, Orkige::String & outExecutable,
		Orkige::String & outEngineTree, Orkige::String * error)
	{
		Orkige::String engineTree = buildDirectory;
		const Orkige::String releaseTree = siblingReleaseTree(buildDirectory);
		if(readCMakeCache(buildDirectory, "CMAKE_BUILD_TYPE") != "Release")
		{
			if(ExportFiles::isRegularFile(ExportFiles::join(
				ExportFiles::join(releaseTree, "orkige_engine"),
				"liborkige_engine.a")))
			{
				engineTree = releaseTree;
				emit(environment.log,
					"native module: building Release against '" + engineTree +
					"'");
			}
			else
			{
				const Orkige::String buildType =
					readCMakeCache(buildDirectory, "CMAKE_BUILD_TYPE");
				emit(environment.log, "WARNING: no release engine tree at '" +
					releaseTree + "' - exporting a " +
					(buildType.empty() ? "?" : buildType) +
					" build of the native module");
			}
		}
		const Orkige::String cachedBuildType =
			readCMakeCache(engineTree, "CMAKE_BUILD_TYPE");
		const Orkige::String buildType =
			cachedBuildType.empty() ? "Debug" : cachedBuildType;
		const Orkige::String flavor = renderBackend(engineTree);
		const Orkige::String cmakeDirSetting =
			project.setting("native.cmakeDir", "native");
		const Orkige::String sourceDirectory =
			ExportFiles::join(project.root, cmakeDirSetting);
		if(!ExportFiles::isRegularFile(
			ExportFiles::join(sourceDirectory, "CMakeLists.txt")))
		{
			return report(error, "native module source '" + sourceDirectory +
				"' has no CMakeLists.txt");
		}
		const Orkige::String moduleBuildDirectory = ExportFiles::join(
			project.root,
			project.setting("native.buildDir", "native/build") +
				"-export-" + flavor);
		const Orkige::String architecture = engineTreeArchitecture(engineTree);
		if(architecture.empty())
		{
			return report(error, "cannot derive the target architecture from '"
				+ engineTree + "' (no vcpkg triplet dir)");
		}
		const Orkige::String cachePath =
			ExportFiles::join(moduleBuildDirectory, "CMakeCache.txt");
		if(ExportFiles::isRegularFile(cachePath))
		{
			const Orkige::String cachedEngineRaw =
				readCMakeCache(moduleBuildDirectory, "ORKIGE_ENGINE_BUILD_DIR");
			const Orkige::String cachedEngine = cachedEngineRaw.empty()
				? Orkige::String() : ExportFiles::absolute(cachedEngineRaw);
			if(readCMakeCache(moduleBuildDirectory, "CMAKE_OSX_ARCHITECTURES")
				!= architecture)
			{
				// a cache without the arch pin (or with the wrong one)
				// produces objects that cannot link against the engine
				// libraries - heal it
				emit(environment.log, "WARNING: export build tree '" +
					moduleBuildDirectory + "' is not pinned to " +
					architecture + " - reconfiguring");
				ExportFiles::removeTree(moduleBuildDirectory, 0);
			}
			else if(cachedEngine != ExportFiles::absolute(engineTree))
			{
				// the module was built against a DIFFERENT engine tree before
				// (e.g. the other render flavor): its objects link the wrong
				// backend closure and the bundled media would not match
				emit(environment.log, "WARNING: export build tree '" +
					moduleBuildDirectory + "' targeted a different engine tree "
					"('" + cachedEngine + "' != '" +
					ExportFiles::absolute(engineTree) + "') - reconfiguring");
				ExportFiles::removeTree(moduleBuildDirectory, 0);
			}
		}
		if(!ExportFiles::isRegularFile(cachePath))
		{
			const Orkige::String sysroot =
				readCMakeCache(engineTree, "CMAKE_OSX_SYSROOT");
			std::vector<Orkige::String> configure = {
				environment.cmake, "-G", "Ninja",
				"-S", sourceDirectory, "-B", moduleBuildDirectory,
				"-DCMAKE_BUILD_TYPE=" + buildType,
				"-DORKIGE_ROOT=" + environment.repoRoot,
				"-DORKIGE_ENGINE_BUILD_DIR=" + engineTree,
				// hermeticity, the same as the presets
				"-DCMAKE_IGNORE_PREFIX_PATH=/usr/local",
				"-DCMAKE_OSX_ARCHITECTURES=" + architecture,
				"-DCMAKE_OSX_SYSROOT=" +
					(sysroot.empty() ? Orkige::String("macosx") : sysroot),
			};
			const Orkige::String scripting =
				readCMakeCache(engineTree, "ORKIGE_SCRIPTING");
			if(!scripting.empty())
			{
				configure.push_back("-DORKIGE_SCRIPTING=" + scripting);
			}
			if(!environment.ninja.empty())
			{
				configure.push_back(
					"-DCMAKE_MAKE_PROGRAM=" + environment.ninja);
			}
			if(!runTool(environment, configure, error))
			{
				return false;
			}
		}
		if(!runTool(environment,
			{ environment.cmake, "--build", moduleBuildDirectory }, error))
		{
			return false;
		}
		// WHERE the module landed is the build's answer, read from the manifest
		// it wrote (cmake/OrkigeGameModule.cmake) - "<buildDir>/<target>" is
		// only the desktop shape, and the exporter must not be the place that
		// assumption is frozen
		const Orkige::String executable =
			Orkige::NativeModule::executablePath(moduleBuildDirectory, target);
		if(!ExportFiles::isRegularFile(executable))
		{
			return report(error, "native module build produced no '" +
				executable + "'");
		}
		outExecutable = executable;
		outEngineTree = engineTree;
		return true;
	}
	//---------------------------------------------------------
	bool exportMacos(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, Orkige::String & outArtifact,
		Orkige::String * error)
	{
		const Orkige::String nativeTarget = project.nativeTarget();
		Orkige::String executable;
		Orkige::String sourceTree;
		Orkige::String flavor;

		if(source.fromBundle())
		{
			// packaging from a distributed app's own payload: no build tree
			// exists on this machine, so the player and the media both come
			// out of the app
			if(!nativeTarget.empty())
			{
				return report(error, "this project builds compiled C++ game "
					"code (its native.target setting), which needs the engine "
					"source tree and a C++ toolchain - the Orkige this export "
					"runs from carries neither");
			}
			if(!ExportFiles::isDirectory(
				ExportFiles::join(source.bundleResources, "Media")))
			{
				return report(error, "no engine media at '" +
					ExportFiles::join(source.bundleResources, "Media") +
					"' - the Orkige this export runs from carries no "
					"packageable engine payload");
			}
			executable = bundledTool(source, "orkige_player");
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
		else if(!nativeTarget.empty())
		{
			if(!buildNativeModule(project, nativeTarget, source.buildDirectory,
				environment, executable, sourceTree, error))
			{
				return false;
			}
			flavor = renderBackend(sourceTree);
		}
		else
		{
			// prefer the RELEASE player: a Debug one runs far slower
			executable = ExportFiles::join(
				ExportFiles::join(source.buildDirectory, "tools/player"),
				"orkige_player");
			sourceTree = source.buildDirectory;
			if(readCMakeCache(source.buildDirectory, "CMAKE_BUILD_TYPE") !=
				"Release")
			{
				const Orkige::String releaseTree =
					siblingReleaseTree(source.buildDirectory);
				const Orkige::String releasePlayer = ExportFiles::join(
					ExportFiles::join(releaseTree, "tools/player"),
					"orkige_player");
				if(ExportFiles::isRegularFile(releasePlayer))
				{
					executable = releasePlayer;
					sourceTree = releaseTree;
					emit(environment.log, "using the release player '" +
						executable + "'");
				}
				else
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

		const Orkige::String appDirectory =
			ExportFiles::join(outputDirectory, project.name + ".app");
		if(!ExportFiles::removeTree(appDirectory, error))
		{
			return false;
		}
		const Orkige::String contents =
			ExportFiles::join(appDirectory, "Contents");
		const Orkige::String macosDirectory =
			ExportFiles::join(contents, "MacOS");
		const Orkige::String resources =
			ExportFiles::join(contents, "Resources");
		if(!ExportFiles::makeDirectories(macosDirectory, error) ||
			!ExportFiles::makeDirectories(resources, error))
		{
			return false;
		}

		const Orkige::String bundledExecutable =
			ExportFiles::join(macosDirectory, project.exeName());
		if(!ExportFiles::copyFile(executable, bundledExecutable, error) ||
			!ExportFiles::makeExecutable(bundledExecutable, error))
		{
			return false;
		}

		// the dylib closure: rpath dependencies resolve against the source
		// tree's vcpkg - or, packaging from a staged payload, against the
		// Frameworks directory that app already carries them in (its own
		// player loads them from there, so it is where the copy's dependencies
		// live)
		SelfContainRequest selfContain;
		selfContain.executable = bundledExecutable;
		selfContain.frameworksDirectory =
			ExportFiles::join(contents, "Frameworks");
		if(source.fromBundle())
		{
			const Orkige::String tools = source.bundleTools.empty()
				? source.bundleResources : source.bundleTools;
			// step up EXPLICITLY rather than through parent_path(): the roots
			// arrive terminated with a separator (that is what makes them
			// concatenable), and a trailing separator makes parent_path()
			// answer the directory itself - the search would land on
			// Contents/MacOS/Frameworks and resolve nothing
			const Orkige::String siblings = std::filesystem::path(
				ExportFiles::join(tools, "..")).lexically_normal().string();
			selfContain.searchDirectories.push_back(
				ExportFiles::join(siblings, "Frameworks"));
		}
		else
		{
			const Orkige::String triplet = vcpkgTripletDirectory(sourceTree);
			if(!triplet.empty())
			{
				selfContain.searchDirectories.push_back(
					ExportFiles::join(triplet, "debug/lib"));
				selfContain.searchDirectories.push_back(
					ExportFiles::join(triplet, "lib"));
			}
		}
		selfContain.bannedRpathMarkers.push_back("vcpkg_installed");
		if(!environment.repoRoot.empty())
		{
			selfContain.bannedRpathMarkers.push_back(environment.repoRoot);
		}
		if(!makeSelfContained(selfContain, environment.runner, environment.log,
			error))
		{
			return false;
		}

		if(source.fromBundle())
		{
			// a staged payload's Media/ IS this layout already (it is what the
			// app renders from), so the whole tree copies across as one piece
			if(!ExportFiles::copyTree(
				ExportFiles::join(source.bundleResources, "Media"),
				ExportFiles::join(resources, "Media"), error, 0))
			{
				return false;
			}
		}
		else if(!stageEngineMediaFromTree(resources, backendMedia, flavor,
			engineSourceMedia(environment.repoRoot, flavor), error))
		{
			return false;
		}

		int staged = 0;
		if(!stageProjectPayload(project,
			ExportFiles::join(resources, PAYLOAD_DIR_NAME),
			cookPlatformToken("macos"), flavor, environment.log, &staged,
			error))
		{
			return false;
		}
		if(!writeProjectMarker(resources, error))
		{
			return false;
		}
		emit(environment.log,
			"project payload: " + std::to_string(staged) + " files");

		// app icon: Contents/Resources/AppIcon.icns from export.icon (or the
		// engine default). macOS has no launch-image concept, so this is
		// icon-only.
		const Orkige::String iconSource = resolveIconSource(project,
			environment.defaultIconPath, environment.log);
		ExportImage icon;
		if(!loadSquareIconSource(iconSource, icon, error))
		{
			return false;
		}
		const Orkige::String iconset =
			ExportFiles::join(outputDirectory, project.exeName() + ".iconset");
		if(!ExportFiles::removeTree(iconset, error) ||
			!makeMacosIconset(icon, iconset, error))
		{
			return false;
		}
		if(!runTool(environment, { "iconutil", "-c", "icns", iconset, "-o",
			ExportFiles::join(resources, "AppIcon.icns") }, error))
		{
			return false;
		}
		ExportFiles::removeTree(iconset, 0);

		const Orkige::String bundleId = project.setting("export.macos.bundleId",
			"com.orkitec." + project.idSlug());
		if(!ExportPlist::write(macosInfoPlist(project, bundleId),
			ExportFiles::join(contents, "Info.plist"), error))
		{
			return false;
		}
		emit(environment.log, "bundle id " + bundleId);
		outArtifact = appDirectory;
		return true;
	}
}
