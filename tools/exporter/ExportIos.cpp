/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportIos.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportIos.h"

#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportIcons.h"
#include "ExportImage.h"
#include "ExportPlist.h"
#include "ExportProcess.h"
#include "ExportZip.h"

#include <algorithm>
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
		//! an icon file name without its extension - what CFBundleIconFiles
		//! carries (iOS resolves the @2x/@3x variants itself)
		Orkige::String iconBaseName(Orkige::String const & fileName)
		{
			const std::size_t dot = fileName.rfind('.');
			return (dot == Orkige::String::npos) ? fileName
				: fileName.substr(0, dot);
		}
	}
	//---------------------------------------------------------
	Orkige::JsonValue iosInfoPlistKeys(ExportProject const & project,
		Orkige::String const & bundleId,
		std::vector<Orkige::String> const & iconNames)
	{
		Orkige::JsonValue keys = Orkige::JsonValue::object();
		keys.set("CFBundleIdentifier", Orkige::JsonValue(bundleId));
		keys.set("CFBundleName", Orkige::JsonValue(project.name));
		keys.set("CFBundleDisplayName", Orkige::JsonValue(project.name));

		Orkige::JsonValue files = Orkige::JsonValue::array();
		for(Orkige::String const & name : iconNames)
		{
			files.push(Orkige::JsonValue(iconBaseName(name)));
		}
		Orkige::JsonValue primary = Orkige::JsonValue::object();
		primary.set("CFBundleIconFiles", files);
		Orkige::JsonValue icons = Orkige::JsonValue::object();
		icons.set("CFBundlePrimaryIcon", primary);
		keys.set("CFBundleIcons", icons);

		Orkige::JsonValue orientations = Orkige::JsonValue::array();
		for(Orkige::String const & orientation :
			iosOrientations(orientationSetting(project.settings)))
		{
			orientations.push(Orkige::JsonValue(orientation));
		}
		keys.set("UISupportedInterfaceOrientations", orientations);
		keys.set("NSAppTransportSecurity", appTransportSecurity());
		return keys;
	}
	//---------------------------------------------------------
	Orkige::String ipaArchiveName(Orkige::String const & appDirectory,
		Orkige::String const & filePath)
	{
		const std::filesystem::path bundle(appDirectory);
		const std::filesystem::path relative =
			std::filesystem::path(filePath).lexically_relative(bundle);
		// zip entry paths are forward-slashed on every host
		return "Payload/" + bundle.filename().string() + "/" +
			relative.generic_string();
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> codesignNestedArguments(
		Orkige::String const & identity, Orkige::String const & path)
	{
		return { "codesign", "--force", "--sign", identity, path };
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> codesignBundleArguments(
		Orkige::String const & identity,
		Orkige::String const & entitlementsPath,
		Orkige::String const & bundleDirectory)
	{
		return { "codesign", "--force", "--sign", identity,
			"--entitlements", entitlementsPath, "--generate-entitlement-der",
			bundleDirectory };
	}
	//---------------------------------------------------------
	bool exportIos(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory, IosRequest const & request,
		ExportEnvironment const & environment, Orkige::String & outArtifact,
		Orkige::String * error)
	{
		const bool signing = !request.signing.identity.empty();
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
				"app only; an iOS package needs the iOS player, which comes "
				"from its own preset build tree");
		}
		const Orkige::String sourceApp = ExportFiles::join(
			ExportFiles::join(source.buildDirectory, "tools/player"),
			"OrkigePlayer.app");
		if(!ExportFiles::isDirectory(sourceApp))
		{
			return report(error, signing
				? ("no device OrkigePlayer.app at '" + sourceApp + "' - a "
					"signed device/store build needs an arm64-ios (device, not "
					"simulator) player build; configure + build the "
					"ios-device-debug (or -release) preset first (see "
					"Docs/ios-signing.md)")
				: ("no OrkigePlayer.app at '" + sourceApp + "' - build the "
					"ios-simulator-debug preset first"));
		}
		if(signing && !ExportFiles::isRegularFile(request.signing.profile))
		{
			return report(error, "provisioning profile '" +
				request.signing.profile + "' does not exist");
		}

		const Orkige::String flavor = renderBackend(source.buildDirectory);
		const Orkige::String appDirectory =
			ExportFiles::join(outputDirectory, project.name + ".app");
		if(!ExportFiles::removeTree(appDirectory, error) ||
			!ExportFiles::copyTree(sourceApp, appDirectory, error, 0))
		{
			return false;
		}
		// the player bundle already carries the backend's shader media; the
		// engine's own content directories are committed to the source tree
		// and are added here
		if(!stageEngineContentMedia(appDirectory, flavor,
			engineSourceMedia(environment.repoRoot, flavor), error))
		{
			return false;
		}

		int staged = 0;
		if(!stageProjectPayload(project,
			ExportFiles::join(appDirectory, PAYLOAD_DIR_NAME),
			cookPlatformToken("ios"), flavor, environment.log, &staged, error))
		{
			return false;
		}
		if(!writeProjectMarker(appDirectory, error))
		{
			return false;
		}
		if(!ExportPlist::write(privacyManifest(),
			ExportFiles::join(appDirectory, PRIVACY_MANIFEST_FILE_NAME), error))
		{
			return false;
		}
		emit(environment.log,
			"project payload: " + std::to_string(staged) + " files");

		// per-project icons + identity: the prebuilt bundle ships the generic
		// player identity, so it is rewritten IN the plist's own DOM (every
		// key the template carries survives), and the loose CFBundleIconFiles
		// PNGs an iOS bundle honours at its root are written beside it
		const Orkige::String iconSource = resolveIconSource(project,
			environment.defaultIconPath, environment.log);
		ExportImage icon;
		if(!loadSquareIconSource(iconSource, icon, error))
		{
			return false;
		}
		std::vector<Orkige::String> iconNames;
		if(!makeIosIcons(icon, appDirectory, &iconNames, error))
		{
			return false;
		}
		const Orkige::String bundleId = project.setting("export.ios.bundleId",
			"com.orkitec." + project.idSlug());
		const Orkige::String plistPath =
			ExportFiles::join(appDirectory, "Info.plist");
		Orkige::JsonValue keys = iosInfoPlistKeys(project, bundleId, iconNames);
		Orkige::JsonValue existing;
		if(!ExportPlist::read(plistPath, existing, error))
		{
			return false;
		}
		if(!existing.has("UILaunchScreen"))
		{
			// an empty launch-screen dict opts the app into native full
			// resolution; a bundle whose template already declares one keeps
			// what it declares
			keys.set("UILaunchScreen", Orkige::JsonValue::object());
		}
		if(!ExportPlist::setKeys(plistPath, keys, error))
		{
			return false;
		}
		emit(environment.log, "bundle id " + bundleId);

		if(!signing)
		{
			emit(environment.log,
				"install: xcrun simctl install <udid> '" + appDirectory + "'");
			outArtifact = appDirectory;
			return true;
		}

		// embed the provisioning profile + write the entitlements the codesign
		// call binds into the signature
		const Orkige::String teamId = project.setting("export.ios.teamId");
		if(!ExportFiles::copyFile(request.signing.profile,
			ExportFiles::join(appDirectory, "embedded.mobileprovision"), error))
		{
			return false;
		}
		const Orkige::String entitlements =
			ExportFiles::join(outputDirectory, "entitlements.plist");
		if(!ExportPlist::write(
			iosEntitlements(teamId, bundleId, request.distribution),
			entitlements, error))
		{
			return false;
		}
		// sign INSIDE-OUT: nested binaries first, then the bundle around them
		// (a bundle signature seals what it contains, so a later nested sign
		// would invalidate it)
		const Orkige::String frameworks =
			ExportFiles::join(appDirectory, "Frameworks");
		if(ExportFiles::isDirectory(frameworks))
		{
			std::vector<Orkige::String> nested;
			std::error_code ignored;
			for(std::filesystem::directory_entry const & entry :
				std::filesystem::directory_iterator(
					std::filesystem::path(frameworks), ignored))
			{
				nested.push_back(entry.path().string());
			}
			std::sort(nested.begin(), nested.end());
			for(Orkige::String const & path : nested)
			{
				if(!runTool(environment,
					codesignNestedArguments(request.signing.identity, path),
					error))
				{
					return false;
				}
			}
		}
		if(!runTool(environment, codesignBundleArguments(
			request.signing.identity, entitlements, appDirectory), error))
		{
			return false;
		}
		ExportFiles::removeTree(entitlements, 0);
		emit(environment.log, "signed with identity '" +
			request.signing.identity + "' (team " +
			(teamId.empty() ? Orkige::String("?") : teamId) +
			(request.distribution ? ", distribution" : ", development") + ")");

		if(!request.packageIpa)
		{
			emit(environment.log, "install: xcrun devicectl device install app "
				"--device <udid> '" + appDirectory + "'");
			outArtifact = appDirectory;
			return true;
		}

		// the upload container: a zip whose ONE top-level directory is
		// Payload/. Regular files only - an iOS device bundle is flat, with no
		// macOS-style version symlinks to preserve.
		const Orkige::String ipaPath =
			ExportFiles::join(outputDirectory, project.exeName() + ".ipa");
		ExportZip ipa;
		for(Orkige::String const & relative :
			ExportFiles::listFilesRecursive(appDirectory))
		{
			const Orkige::String full =
				ExportFiles::join(appDirectory, relative);
			if(!ipa.addFile(ipaArchiveName(appDirectory, full), full,
				ExportZip::METHOD_DEFLATE, error))
			{
				return false;
			}
		}
		if(!ipa.write(ipaPath, error))
		{
			return false;
		}
		emit(environment.log, "artifact " + ipaPath);
		emit(environment.log, "upload: xcrun altool --upload-package '" +
			ipaPath + "' --type ios --apple-id <app-id> --bundle-id "
			"<bundle-id> --apiKey <key> --apiIssuer <issuer> (see "
			"Docs/store-release.md)");
		outArtifact = ipaPath;
		return true;
	}
}
