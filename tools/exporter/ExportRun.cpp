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
#include "ExportMacos.h"
#include "ExportWeb.h"

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
		return platform == "macos" || platform == "ios-simulator" ||
			platform == "ios" || platform == "ios-ipa" ||
			platform == "android" || platform == "android-aab" ||
			platform == "web";
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
			if(request.platform != "macos" && request.platform != "web" &&
				request.source.devicePayload.empty())
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
		else
		{
			if(request.source.buildDirectory.empty())
			{
				return refuse(error, "no engine source: pass an engine build "
					"tree or a staged engine payload");
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
		else
		{
			source.buildDirectory =
				ExportFiles::absolute(source.buildDirectory);
		}
		if(!source.devicePayload.empty())
		{
			source.devicePayload = ExportFiles::absolute(source.devicePayload);
		}

		bool packaged = false;
		if(request.platform == "macos")
		{
			packaged = exportMacos(project, source, outputDirectory,
				environment, artifact, error);
		}
		else if(request.platform == "ios-simulator" ||
			request.platform == "ios" || request.platform == "ios-ipa")
		{
			IosRequest iosRequest;
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
