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
//!   orkige_export --project <dir>
//!                 --platform macos|ios-simulator|ios|ios-ipa|android|
//!                            android-aab
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
//! bundle. A mobile package always needs a build tree: it ships THAT
//! platform's player, which only its own preset produces.
//!
//! The signing material for the two signed platforms is machine-local and
//! never committed - a CLI argument, else the environment (@see
//! ExportSettings.h). Without it those platforms refuse rather than emit a
//! half-signed artifact.
//!
//! Output lands in `<project>/builds/<platform>/` (or `--output`). The last
//! line on success is `orkige_export: OK <artifact>` - the editor's Build menu
//! parses it for the reveal-in-Finder nicety, tests for the artifact path.
//!
//! `self-contain` is the same dylib-closure operation an export performs,
//! reachable on its own so the editor BUILD can stage its app the identical
//! way (@see ExportSelfContain.h).

#include "ExportAndroid.h"
#include "ExportFiles.h"
#include "ExportIos.h"
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

	//! the machine-local signing material, read once so every resolver below
	//! sees the same lookup a test can stand in for
	OrkigeExport::EnvironmentMap currentEnvironment()
	{
		OrkigeExport::EnvironmentMap environment;
		const char * const names[] = {
			OrkigeExport::IOS_SIGNING_IDENTITY_ENV,
			OrkigeExport::IOS_PROVISIONING_PROFILE_ENV,
			OrkigeExport::IOS_DISTRIBUTION_IDENTITY_ENV,
			OrkigeExport::IOS_DISTRIBUTION_PROFILE_ENV,
			OrkigeExport::ANDROID_KEYSTORE_ENV,
			OrkigeExport::ANDROID_KEY_ALIAS_ENV,
			OrkigeExport::ANDROID_KEYSTORE_PASS_ENV,
			OrkigeExport::ANDROID_KEY_PASS_ENV,
			OrkigeExport::BUNDLETOOL_ENV,
		};
		for(const char * name : names)
		{
			const char * value = std::getenv(name);
			if(value != 0)
			{
				environment[name] = value;
			}
		}
		return environment;
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
	String signingIdentity;
	String provisioningProfile;
	String distributionIdentity;
	String distributionProfile;
	String androidKeystore;
	String androidKeyAlias;
	String bundletool;
	bool unsignedBundleModule = false;
	for(std::size_t index = 0; index < arguments.size(); ++index)
	{
		String const & argument = arguments[index];
		// the one valueless option: build just the unsigned bundle module
		if(argument == "--aab-unsigned-module")
		{
			unsignedBundleModule = true;
			continue;
		}
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
		else if(argument == "--signing-identity") { signingIdentity = value; }
		else if(argument == "--provisioning-profile")
		{
			provisioningProfile = value;
		}
		else if(argument == "--distribution-identity")
		{
			distributionIdentity = value;
		}
		else if(argument == "--distribution-profile")
		{
			distributionProfile = value;
		}
		else if(argument == "--android-keystore") { androidKeystore = value; }
		else if(argument == "--android-key-alias") { androidKeyAlias = value; }
		else if(argument == "--bundletool") { bundletool = value; }
		else { return fail("unknown argument '" + argument + "'"); }
	}
	if(projectPath.empty() || platform.empty())
	{
		std::fprintf(stderr,
			"usage: orkige_export --project <dir>\n"
			"                     --platform macos|ios-simulator|ios|ios-ipa|"
			"android|android-aab\n"
			"                     (--engine-build <preset build dir> |\n"
			"                      --engine-bundle <dir> [--engine-tools "
			"<dir>])\n"
			"                     [--output <dir>]\n"
			"       orkige_export self-contain --frameworks <dir> "
			"[--search <dir>]... <binary>...\n");
		return 2;
	}
	const bool knownPlatform = platform == "macos" ||
		platform == "ios-simulator" || platform == "ios" ||
		platform == "ios-ipa" || platform == "android" ||
		platform == "android-aab";
	if(!knownPlatform)
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
		if(platform != "macos")
		{
			return fail("a staged engine payload packages the desktop app "
				"only; '" + platform + "' needs that platform's player, which "
				"comes from its own preset build tree (--engine-build)");
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

	const OrkigeExport::EnvironmentMap machineEnvironment =
		currentEnvironment();
	String artifact;
	bool packaged = false;
	if(platform == "macos")
	{
		packaged = OrkigeExport::exportMacos(project, source, outputDirectory,
			environment, artifact, &error);
	}
	else if(platform == "ios-simulator" || platform == "ios" ||
		platform == "ios-ipa")
	{
		OrkigeExport::IosRequest request;
		if(platform == "ios")
		{
			request.signing = OrkigeExport::resolveIosSigning(signingIdentity,
				provisioningProfile, machineEnvironment);
			if(request.signing.identity.empty() ||
				request.signing.profile.empty())
			{
				return fail(String("physical-device iOS export needs a "
					"codesigning identity AND a provisioning profile (unsigned "
					"apps cannot install on hardware - the same gate as the "
					"editor's Play on an iPhone). Set --signing-identity/") +
					OrkigeExport::IOS_SIGNING_IDENTITY_ENV +
					" and --provisioning-profile/" +
					OrkigeExport::IOS_PROVISIONING_PROFILE_ENV +
					", or use --platform ios-simulator. See "
					"Docs/ios-signing.md");
			}
		}
		else if(platform == "ios-ipa")
		{
			request.signing = OrkigeExport::resolveIosDistributionSigning(
				distributionIdentity, distributionProfile, machineEnvironment);
			request.distribution = true;
			request.packageIpa = true;
			if(request.signing.identity.empty() ||
				request.signing.profile.empty())
			{
				return fail(String("App Store .ipa export needs a DISTRIBUTION "
					"codesigning identity AND an App Store provisioning "
					"profile. Set --distribution-identity/") +
					OrkigeExport::IOS_DISTRIBUTION_IDENTITY_ENV +
					" and --distribution-profile/" +
					OrkigeExport::IOS_DISTRIBUTION_PROFILE_ENV +
					". See Docs/store-release.md");
			}
		}
		packaged = OrkigeExport::exportIos(project, source, outputDirectory,
			request, environment, artifact, &error);
	}
	else
	{
		OrkigeExport::AndroidRequest request;
		request.bundle = (platform == "android-aab");
		if(request.bundle)
		{
			if(!OrkigeExport::androidVersion(project.settings,
				request.options.versionCode, request.options.versionName,
				&error))
			{
				return fail(error);
			}
			request.options.moduleOnly = unsignedBundleModule;
			request.options.keystore = OrkigeExport::resolveAndroidKeystore(
				androidKeystore, androidKeyAlias, machineEnvironment);
			request.options.bundletool = OrkigeExport::resolveBundletool(
				bundletool, machineEnvironment, OrkigeExport::findOnPath);
		}
		packaged = OrkigeExport::exportAndroid(project, source, outputDirectory,
			request, environment, artifact, &error);
	}
	if(!packaged)
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
