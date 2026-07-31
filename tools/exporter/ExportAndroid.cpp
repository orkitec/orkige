/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportAndroid.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportAndroid.h"

#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportIcons.h"
#include "ExportImage.h"
#include "ExportProcess.h"

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
		//! the arguments both packaging scripts share, in the order they
		//! expect them
		void appendCommonArguments(std::vector<Orkige::String> & command,
			Orkige::String const & payloadDirectory,
			Orkige::String const & package, Orkige::String const & label,
			Orkige::String const & resDirectory,
			Orkige::String const & launchColour,
			Orkige::String const & assetsMode,
			Orkige::String const & outputPath)
		{
			command.push_back("--project-payload");
			command.push_back(payloadDirectory);
			command.push_back("--package");
			command.push_back(package);
			command.push_back("--label");
			command.push_back(label);
			command.push_back("--res-dir");
			command.push_back(resDirectory);
			command.push_back("--launch-color");
			command.push_back(launchColour);
			command.push_back("--assets");
			command.push_back(assetsMode);
			command.push_back("--output");
			command.push_back(outputPath);
		}
		//---------------------------------------------------------
		//! stage the launcher-icon res/ tree the packagers compile with aapt2
		bool stageAndroidRes(ExportProject const & project,
			Orkige::String const & outputDirectory,
			ExportEnvironment const & environment, Orkige::String & outResDir,
			Orkige::String * error)
		{
			const Orkige::String resDirectory =
				ExportFiles::join(outputDirectory, "res-staging");
			if(!ExportFiles::removeTree(resDirectory, error))
			{
				return false;
			}
			const Orkige::String iconSource = resolveIconSource(project,
				environment.defaultIconPath, environment.log);
			ExportImage icon;
			if(!loadSquareIconSource(iconSource, icon, error) ||
				!makeAndroidMipmaps(icon, resDirectory, error))
			{
				return false;
			}
			outResDir = resDirectory;
			return true;
		}
	}
	//---------------------------------------------------------
	bool androidPackageName(ExportProject const & project,
		Orkige::String & out, Orkige::String * error)
	{
		const Orkige::String package = project.setting("export.android.package",
			"com.orkitec." + project.idSlug());
		if(!isValidAndroidPackage(package))
		{
			return report(error, "'" + package + "' is not a valid Android "
				"package name (export.android.package)");
		}
		out = package;
		return true;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> androidApkArguments(
		Orkige::String const & script, Orkige::String const & payloadDirectory,
		Orkige::String const & package, Orkige::String const & label,
		Orkige::String const & resDirectory,
		Orkige::String const & launchColour, Orkige::String const & assetsMode,
		Orkige::String const & orientation, Orkige::String const & outputPath,
		Orkige::String const & engineBuild)
	{
		std::vector<Orkige::String> command = { "bash", script };
		appendCommonArguments(command, payloadDirectory, package, label,
			resDirectory, launchColour, assetsMode, outputPath);
		if(orientation != "auto")
		{
			command.push_back("--orientation");
			command.push_back(androidScreenOrientation(orientation));
		}
		command.push_back(engineBuild);
		return command;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> androidBundleArguments(
		Orkige::String const & script, Orkige::String const & payloadDirectory,
		Orkige::String const & package, Orkige::String const & label,
		Orkige::String const & resDirectory,
		Orkige::String const & launchColour, Orkige::String const & assetsMode,
		Orkige::String const & orientation, Orkige::String const & outputPath,
		Orkige::String const & engineBuild,
		AndroidBundleOptions const & options)
	{
		std::vector<Orkige::String> command = { "bash", script };
		appendCommonArguments(command, payloadDirectory, package, label,
			resDirectory, launchColour, assetsMode, outputPath);
		command.push_back("--version-code");
		command.push_back(std::to_string(options.versionCode));
		command.push_back("--version-name");
		command.push_back(options.versionName);
		if(orientation != "auto")
		{
			command.push_back("--orientation");
			command.push_back(androidScreenOrientation(orientation));
		}
		command.push_back(engineBuild);
		if(options.moduleOnly)
		{
			command.push_back("--module-only");
		}
		else
		{
			command.push_back("--keystore");
			command.push_back(options.keystore.keystore);
			command.push_back("--key-alias");
			command.push_back(options.keystore.alias);
			command.push_back("--bundletool");
			command.push_back(options.bundletool);
		}
		return command;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> androidSigningGaps(
		AndroidKeystore const & keystore, Orkige::String const & bundletool)
	{
		std::vector<Orkige::String> missing;
		if(bundletool.empty())
		{
			missing.push_back(Orkige::String("a bundletool jar (--bundletool "
				"/ ") + BUNDLETOOL_ENV + " / a `bundletool` on PATH)");
		}
		if(keystore.keystore.empty())
		{
			missing.push_back(Orkige::String("a release keystore "
				"(--android-keystore / ") + ANDROID_KEYSTORE_ENV + ")");
		}
		else
		{
			if(keystore.alias.empty())
			{
				missing.push_back(Orkige::String("a key alias "
					"(--android-key-alias / ") + ANDROID_KEY_ALIAS_ENV + ")");
			}
			if(!keystore.hasStorePassword)
			{
				missing.push_back(Orkige::String("the keystore password (") +
					ANDROID_KEYSTORE_PASS_ENV + ")");
			}
		}
		return missing;
	}
	//---------------------------------------------------------
	bool exportAndroid(ExportProject const & project,
		EngineSource const & source, Orkige::String const & outputDirectory,
		AndroidRequest const & request,
		ExportEnvironment const & environment, Orkige::String & outArtifact,
		Orkige::String * error)
	{
		if(!project.nativeTarget().empty())
		{
			return report(error, "project '" + project.name + "' has a native "
				"module ('" + project.nativeTarget() + "') - native modules "
				"are desktop-only for now, mobile native builds are future "
				"work (the Lua/scene parts of a project export fine without "
				"one)");
		}
		if(source.fromBundle())
		{
			return report(error, "a staged engine payload packages the desktop "
				"app only; an Android package needs the Android player, which "
				"comes from its own preset build tree");
		}
		if(environment.repoRoot.empty())
		{
			return report(error, "an Android package is assembled by the SDK "
				"scripts beside the player (tools/player/android) - this "
				"export has no engine source tree to run them from");
		}
		const Orkige::String nativeLib = ExportFiles::join(
			ExportFiles::join(source.buildDirectory, "tools/player"),
			"libmain.so");
		if(!ExportFiles::isRegularFile(nativeLib))
		{
			return report(error, "no libmain.so at '" + nativeLib +
				"' - build the android-" +
				(request.bundle ? "release (or android-debug)" : "debug") +
				" preset first");
		}
		if(request.bundle &&
			readCMakeCache(source.buildDirectory, "CMAKE_BUILD_TYPE") !=
				"Release")
		{
			const Orkige::String buildType =
				readCMakeCache(source.buildDirectory, "CMAKE_BUILD_TYPE");
			emit(environment.log, "WARNING: engine tree '" +
				source.buildDirectory + "' is a " +
				(buildType.empty() ? "?" : buildType) + " build - the release "
				"bundle will carry a non-optimized libmain.so; build the "
				"android-release preset for a shippable bundle");
		}

		Orkige::String package;
		if(!androidPackageName(project, package, error))
		{
			return false;
		}
		Orkige::String assetsMode;
		if(!androidAssetsMode(project.settings, assetsMode, error))
		{
			return false;
		}
		if(request.bundle)
		{
			emit(environment.log, "release bundle: versionCode " +
				std::to_string(request.options.versionCode) + ", versionName " +
				request.options.versionName);
			if(!request.options.moduleOnly)
			{
				// the honest gate: refuse and produce nothing rather than a
				// half-signed artifact (the same shape as the iOS gate)
				const std::vector<Orkige::String> missing = androidSigningGaps(
					request.options.keystore, request.options.bundletool);
				if(!missing.empty())
				{
					Orkige::String joined;
					for(std::size_t index = 0; index < missing.size(); ++index)
					{
						joined += (index == 0 ? "" : "; ") + missing[index];
					}
					return report(error, "a signed Android App Bundle needs " +
						joined + ". See Docs/store-release.md for the one-time "
						"setup, or ask for the unsigned bundle module for "
						"inspection.");
				}
			}
		}

		const Orkige::String flavor = renderBackend(source.buildDirectory);
		const Orkige::String payloadDirectory =
			ExportFiles::join(outputDirectory, "payload-staging");
		if(!ExportFiles::removeTree(payloadDirectory, error))
		{
			return false;
		}
		int staged = 0;
		if(!stageProjectPayload(project, payloadDirectory,
			cookPlatformToken("android"), flavor, environment.log, &staged,
			error))
		{
			return false;
		}
		emit(environment.log,
			"project payload: " + std::to_string(staged) + " files");
		Orkige::String resDirectory;
		if(!stageAndroidRes(project, outputDirectory, environment, resDirectory,
			error))
		{
			return false;
		}
		const Orkige::String launchColour = launchBackground(project.settings);
		const Orkige::String orientation =
			orientationSetting(project.settings);

		const Orkige::String scriptDirectory = ExportFiles::join(
			environment.repoRoot, "tools/player/android");
		Orkige::String artifact;
		std::vector<Orkige::String> command;
		if(request.bundle)
		{
			artifact = ExportFiles::join(outputDirectory, project.exeName() +
				(request.options.moduleOnly ? ".aab.module.zip" : ".aab"));
			command = androidBundleArguments(
				ExportFiles::join(scriptDirectory, "build_aab.sh"),
				payloadDirectory, package, project.name, resDirectory,
				launchColour, assetsMode, orientation, artifact,
				source.buildDirectory, request.options);
		}
		else
		{
			artifact =
				ExportFiles::join(outputDirectory, project.exeName() + ".apk");
			command = androidApkArguments(
				ExportFiles::join(scriptDirectory, "package_apk.sh"),
				payloadDirectory, package, project.name, resDirectory,
				launchColour, assetsMode, orientation, artifact,
				source.buildDirectory);
		}
		emit(environment.log, "$ " + commandLine(command));
		const ProcessResult result = environment.runner(command);
		ExportFiles::removeTree(payloadDirectory, 0);
		ExportFiles::removeTree(resDirectory, 0);
		if(!result.launched)
		{
			return report(error, "could not run '" + command[0] + "'");
		}
		if(result.exitCode != 0)
		{
			return report(error, "command failed (exit " +
				std::to_string(result.exitCode) + "): " + command[1] +
				(result.output.empty() ? "" : " - " + result.output));
		}
		if(!ExportFiles::isRegularFile(artifact))
		{
			return report(error, ExportFiles::fileName(command[1]) +
				" produced no '" + artifact + "'");
		}
		if(request.bundle && request.options.moduleOnly)
		{
			emit(environment.log, "unsigned bundle module (NOT submittable) - "
				"see Docs/store-release.md");
		}
		else if(request.bundle)
		{
			emit(environment.log, "upload: submit '" + artifact +
				"' to Google Play (see Docs/store-release.md)");
		}
		else
		{
			emit(environment.log, "install: adb install -r '" + artifact + "'");
		}
		outArtifact = artifact;
		return true;
	}
}
