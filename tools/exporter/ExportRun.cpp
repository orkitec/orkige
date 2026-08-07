/********************************************************************
	created:	Friday 2026/08/01 at 09:00
	filename: 	ExportRun.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportRun.h"

#include "ExportAndroid.h"
#include "ExportFiles.h"
#include "ExportIos.h"
#include "ExportLinux.h"
#include "ExportMacos.h"
#include "ExportWeb.h"
#include "ExportWindows.h"

#include <cstdlib>

namespace OrkigeExport
{
	namespace
	{
		using Orkige::String;

		//! set @p error (when the caller wants one) and refuse
		bool refuse(String * error, String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
	}
	//---------------------------------------------------------
	bool isPackagedPlatform(String const & platform)
	{
		return platform == "macos" || platform == "linux" ||
			platform == "windows" ||
			platform == "ios-simulator" ||
			platform == "ios" || platform == "ios-ipa" ||
			platform == "android" || platform == "android-aab" ||
			platform == "web";
	}
	//---------------------------------------------------------
	bool isDesktopPlatform(String const & platform)
	{
		return platform == "macos" || platform == "linux" ||
			platform == "windows";
	}
	//---------------------------------------------------------
	String desktopPlatformLabel(String const & platform)
	{
		if(platform == "macos")
		{
			return "macOS";
		}
		if(platform == "linux")
		{
			return "Linux";
		}
		if(platform == "windows")
		{
			return "Windows";
		}
		return String();
	}
	//---------------------------------------------------------
	String hostDesktopPlatform()
	{
#if defined(__APPLE__)
		return "macos";
#elif defined(__linux__)
		return "linux";
#elif defined(_WIN32)
		return "windows";
#else
		// the honest answer where the exporter has no desktop target for its
		// own host yet, rather than a guess that would package the wrong
		// operating system's binary
		return String();
#endif
	}
	//---------------------------------------------------------
	String desktopHostRefusal(String const & platform, String const & hostDesktop)
	{
		if(!isDesktopPlatform(platform) || platform == hostDesktop)
		{
			return String();
		}
		const String label = desktopPlatformLabel(platform);
		if(hostDesktop.empty())
		{
			return "a " + label + " package is assembled around the " + label +
				" player, and this exporter runs on an operating system it "
				"packages no desktop app for - export from a machine running " +
				label;
		}
		const String hostLabel = desktopPlatformLabel(hostDesktop);
		return "a " + label + " package is assembled around the " + label +
			" player, and this exporter runs on " + hostLabel + " - nothing "
			"here cross-compiles a player for another operating system. Export "
			"for " + hostLabel + ", or run the export on a " + label +
			" machine";
	}
	//---------------------------------------------------------
	String testRunPlatformRefusal(String const & platform)
	{
		if(isDesktopPlatform(platform) ||
			platform == "ios-simulator" ||
			platform == "ios" || platform == "ios-ipa")
		{
			return String();
		}
		if(!isPackagedPlatform(platform))
		{
			return "'" + platform + "' is not a platform this packages for";
		}
		return "a test build is not available for '" + platform + "': its "
			"payload rides inside an archive the runtime mounts in place, and "
			"the test runner discovers a suite by walking a directory - so the "
			"run would find no tests and report a pass over nothing. Export "
			"--with-tests for a desktop or an iOS target";
	}
	//---------------------------------------------------------
	EnvironmentMap currentEnvironment()
	{
		EnvironmentMap environment;
		const char * const names[] = {
			IOS_SIGNING_IDENTITY_ENV,
			IOS_PROVISIONING_PROFILE_ENV,
			IOS_DISTRIBUTION_IDENTITY_ENV,
			IOS_DISTRIBUTION_PROFILE_ENV,
			ANDROID_KEYSTORE_ENV,
			ANDROID_KEY_ALIAS_ENV,
			ANDROID_KEYSTORE_PASS_ENV,
			ANDROID_KEY_PASS_ENV,
			BUNDLETOOL_ENV,
			// the macOS Developer ID material: the certificate's name (public,
			// and the only one of these that may also arrive on a command
			// line) and the two notarization credential shapes Apple takes
			MACOS_SIGNING_IDENTITY_ENV,
			MACOS_KEYCHAIN_ENV,
			NOTARY_KEY_ENV,
			NOTARY_KEY_ID_ENV,
			NOTARY_ISSUER_ENV,
			NOTARY_APPLE_ID_ENV,
			NOTARY_APP_PASSWORD_ENV,
			NOTARY_TEAM_ID_ENV,
			NOTARY_TIMEOUT_ENV,
			// the Windows Authenticode material: a machine-store thumbprint
			// (public) or a certificate file, its password, and the timestamp
			// authority that countersigns
			WINDOWS_CERTIFICATE_ENV,
			WINDOWS_CERTIFICATE_PASSWORD_ENV,
			WINDOWS_THUMBPRINT_ENV,
			WINDOWS_TIMESTAMP_URL_ENV,
			SIGNTOOL_ENV,
			// where the Android SDK and the JDK that drives it are found; HOME
			// (and LOCALAPPDATA on Windows) so the default install location is
			// reachable when nobody configured anything
			ANDROID_HOME_ENV,
			ANDROID_SDK_ROOT_ENV,
			JAVA_HOME_ENV,
			"HOME",
			"LOCALAPPDATA",
			// how signtool.exe is found: the Windows SDK's own variable, the
			// two program directories the kits install under, and PATH as the
			// last resort (@see locateSigntool)
			"WindowsSdkDir",
			"ProgramFiles(x86)",
			"ProgramFiles",
			"PATH",
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
	bool runExport(ExportProject const & project, ExportRequest const & request,
		ExportLog const & log, String & artifact, String * error)
	{
		if(!isPackagedPlatform(request.platform))
		{
			return refuse(error, "'" + request.platform + "' is not a platform "
				"this exporter packages yet");
		}
		// a desktop package ships the HOST's own player binary; refuse before
		// anything is written rather than produce a directory that cannot run
		{
			const String hostRefusal = desktopHostRefusal(request.platform,
				hostDesktopPlatform());
			if(!hostRefusal.empty())
			{
				return refuse(error, hostRefusal);
			}
		}
		if(request.withTests)
		{
			const String refusal = testRunPlatformRefusal(request.platform);
			if(!refusal.empty())
			{
				return refuse(error, refusal);
			}
		}
		// the credential gate runs BEFORE anything is copied: a half-signed
		// artifact is worse than an honestly ad-hoc one, so a signed export
		// with a missing credential names it and packages nothing
		MacosSigning macosSigning;
		if(request.macosSigning.requested())
		{
			const String platformRefusal =
				macosSigningPlatformRefusal(request.platform);
			if(!platformRefusal.empty())
			{
				return refuse(error, platformRefusal);
			}
			if(!resolveMacosSigning(request.macosSigning, request.environment,
				macosSigning, error))
			{
				return false;
			}
		}
		WindowsSigning windowsSigning;
		if(request.windowsSigning.requested())
		{
			const String platformRefusal =
				windowsSigningPlatformRefusal(request.platform);
			if(!platformRefusal.empty())
			{
				return refuse(error, platformRefusal);
			}
			if(!resolveWindowsSigning(request.windowsSigning,
				request.environment, windowsSigning, error))
			{
				return false;
			}
			// ...and the TOOL is found here too, before a single file is
			// copied: a machine with no Windows SDK must refuse rather than
			// assemble a package it then cannot sign
			if(windowsSigning.real())
			{
				String refusal;
				windowsSigning.signtool = locateSigntool(
					windowsSigning.signtool, request.environment,
					defaultFileProbe(), defaultDirectoryLister(), &refusal);
				if(windowsSigning.signtool.empty())
				{
					return refuse(error, refusal);
				}
			}
		}
		if(request.source.fromBundle())
		{
			if(!request.source.buildDirectory.empty())
			{
				return refuse(error, "a staged engine payload and an engine "
					"build tree name two different engine sources - pass one");
			}
			if(!ExportFiles::isDirectory(request.source.bundleResources))
			{
				return refuse(error, "engine payload '" +
					request.source.bundleResources + "' does not exist");
			}
			// a staged payload packages the desktop app and - when the browser
			// player rides along beside it - the web build. A DEVICE build
			// needs that platform's own player, which is not carried but
			// FETCHED: hand the payload directory in and the platform becomes
			// packageable here too.
			//
			// UNLESS the app is not a player at all: a project whose game code
			// is compiled ships its own module, so what it needs is the engine
			// to build that module against, not somebody's prebuilt runtime.
			// The two are separate prerequisites and neither ever stands in
			// for the other - a project with no C++ never needs a pack, and a
			// project whose app IS its module never needs a player.
			const bool moduleIsTheApp = !project.nativeTarget().empty() &&
				!request.source.sdkPack.empty();
			if(!isDesktopPlatform(request.platform) &&
				request.platform != "web" &&
				request.source.devicePayload.empty() && !moduleIsTheApp)
			{
				return refuse(error, "a staged engine payload packages the "
					"desktop app and the browser build; '" + request.platform +
					"' needs that platform's player - install it (Settings > "
					"Build Targets), or package from that platform's preset "
					"build tree");
			}
			// the beside-itself invariant: a staged payload IS the engine
			// source, so nothing may also point at a repository (@see
			// ExportRun.h). One field, one rule, checked once.
			if(!request.repoRoot.empty())
			{
				return refuse(error, "a staged engine payload cannot also "
					"package from an engine source tree - the two would supply "
					"the same files from different places");
			}
		}
		else if(request.source.buildDirectory.empty() &&
			!request.source.sdkPack.empty())
		{
			// AN SDK PACK ALONE IS AN ENGINE SOURCE for a project whose game
			// code is compiled: the module IS the app, so no prebuilt player is
			// wanted, and the pack carries everything the module needs to be
			// built and to run - headers, archives, the dependency closure and
			// the engine media (Docs/sdk-pack.md). A project whose behaviour is
			// Lua has nothing to compile and does need a player, so it says so.
			if(project.nativeTarget().empty())
			{
				return refuse(error, "an Orkige SDK pack is the engine a "
					"project's COMPILED game code is built against; project '" +
					project.name + "' has none, so it ships the player instead "
					"- pass an engine build tree or a staged engine payload");
			}
		}
		else
		{
			if(request.source.buildDirectory.empty())
			{
				return refuse(error, "no engine source: pass an engine build "
					"tree, a staged engine payload, or - for a project with "
					"compiled game code - an Orkige SDK pack");
			}
			if(!ExportFiles::isDirectory(request.source.buildDirectory))
			{
				return refuse(error, "engine build tree '" +
					request.source.buildDirectory + "' does not exist");
			}
			// the same one-engine rule for the engine a MODULE builds against:
			// a build tree already IS one, and two would be two engines
			if(!request.source.sdkPack.empty())
			{
				return refuse(error, "an engine build tree cannot also build a "
					"native module against an SDK pack - the two are two "
					"engines");
			}
		}
		if(!request.source.sdkPack.empty() &&
			!ExportFiles::isDirectory(request.source.sdkPack))
		{
			return refuse(error, "the Orkige SDK pack '" +
				request.source.sdkPack + "' does not exist");
		}
		if(!request.source.devicePayload.empty())
		{
			// the same one-engine rule as the SDK pack: a build tree already
			// produces the platform's player, so a fetched one beside it
			// would be a second engine
			if(!request.source.fromBundle())
			{
				return refuse(error, "an engine build tree cannot also package "
					"from a fetched device player - the two are two engines");
			}
			if(!ExportFiles::isDirectory(request.source.devicePayload))
			{
				return refuse(error, "the device player payload '" +
					request.source.devicePayload + "' does not exist");
			}
		}

		const String outputDirectory = ExportFiles::absolute(
			request.outputDirectory.empty()
				? ExportFiles::join(ExportFiles::join(project.root, "builds"),
					request.platform)
				: request.outputDirectory);
		if(!ExportFiles::makeDirectories(outputDirectory, error))
		{
			return false;
		}
		if(log)
		{
			log("project '" + project.name + "' -> " + outputDirectory + " (" +
				request.platform + ")");
		}

		ExportEnvironment environment;
		environment.repoRoot = request.repoRoot;
		environment.defaultIconPath = request.defaultIconPath.empty()
			? (request.repoRoot.empty() ? String()
				: ExportFiles::join(request.repoRoot,
					"Util/media/orkige_default_icon.png"))
			: request.defaultIconPath;
		environment.log = log;
		environment.runner = request.runner ? request.runner
			: defaultProcessRunner();
		environment.cmake = request.cmake.empty()
			? (findOnPath("cmake").empty() ? String("cmake")
				: findOnPath("cmake"))
			: request.cmake;
		environment.ninja = request.ninja.empty() ? findOnPath("ninja")
			: request.ninja;

		EngineSource source = request.source;
		if(!source.sdkPack.empty())
		{
			source.sdkPack = ExportFiles::absolute(source.sdkPack);
		}
		if(source.fromBundle())
		{
			source.bundleResources =
				ExportFiles::absolute(source.bundleResources);
			source.bundleTools = source.bundleTools.empty()
				? source.bundleResources
				: ExportFiles::absolute(source.bundleTools);
		}
		else if(!source.buildDirectory.empty())
		{
			source.buildDirectory =
				ExportFiles::absolute(source.buildDirectory);
		}
		if(!source.devicePayload.empty())
		{
			source.devicePayload = ExportFiles::absolute(source.devicePayload);
		}

		PayloadTestRun tests;
		tests.enabled = request.withTests;
		tests.filter = request.testFilter;

		bool packaged = false;
		if(request.platform == "macos")
		{
			packaged = exportMacos(project, source, outputDirectory,
				environment, tests, macosSigning, artifact, error);
		}
		else if(request.platform == "linux")
		{
			packaged = exportLinux(project, source, outputDirectory,
				environment, tests, artifact, error);
		}
		else if(request.platform == "windows")
		{
			packaged = exportWindows(project, source, outputDirectory,
				environment, tests, windowsSigning, artifact, error);
		}
		else if(request.platform == "ios-simulator" ||
			request.platform == "ios" || request.platform == "ios-ipa")
		{
			IosRequest iosRequest;
			iosRequest.tests = tests;
			if(request.platform == "ios")
			{
				iosRequest.signing = resolveIosSigning(request.signingIdentity,
					request.provisioningProfile, request.environment);
				if(iosRequest.signing.identity.empty() ||
					iosRequest.signing.profile.empty())
				{
					return refuse(error, String("physical-device iOS export "
						"needs a codesigning identity AND a provisioning "
						"profile (unsigned apps cannot install on hardware - "
						"the same gate as the editor's Play on an iPhone). "
						"Set ") + IOS_SIGNING_IDENTITY_ENV + " and " +
						IOS_PROVISIONING_PROFILE_ENV + ", or export for the "
						"iOS simulator. See Docs/ios-signing.md");
				}
			}
			else if(request.platform == "ios-ipa")
			{
				iosRequest.signing = resolveIosDistributionSigning(
					request.distributionIdentity, request.distributionProfile,
					request.environment);
				iosRequest.distribution = true;
				iosRequest.packageIpa = true;
				if(iosRequest.signing.identity.empty() ||
					iosRequest.signing.profile.empty())
				{
					return refuse(error, String("App Store .ipa export needs a "
						"DISTRIBUTION codesigning identity AND an App Store "
						"provisioning profile. Set ") +
						IOS_DISTRIBUTION_IDENTITY_ENV + " and " +
						IOS_DISTRIBUTION_PROFILE_ENV +
						". See Docs/store-release.md");
				}
			}
			packaged = exportIos(project, source, outputDirectory, iosRequest,
				environment, artifact, error);
		}
		else if(request.platform == "web")
		{
			packaged = exportWeb(project, source, outputDirectory, environment,
				artifact, error);
		}
		else
		{
			AndroidRequest androidRequest;
			androidRequest.bundle = (request.platform == "android-aab");
			// the machine's SDK + JDK are resolved from the same environment
			// the signing material is (@see resolveAndroidToolchain)
			androidRequest.environment = request.environment;
			if(androidRequest.bundle)
			{
				if(!androidVersion(project.settings,
					androidRequest.options.versionCode,
					androidRequest.options.versionName, error))
				{
					return false;
				}
				androidRequest.options.moduleOnly = request.unsignedBundleModule;
				androidRequest.options.keystore = resolveAndroidKeystore(
					request.androidKeystore, request.androidKeyAlias,
					request.environment);
				androidRequest.options.bundletool = resolveBundletool(
					request.bundletool, request.environment, findOnPath);
			}
			packaged = exportAndroid(project, source, outputDirectory,
				androidRequest, environment, artifact, error);
		}
		if(!packaged)
		{
			return false;
		}
		if(log)
		{
			log("artifact size " +
				humanSize(ExportFiles::treeSize(artifact)));
		}
		return true;
	}
}
