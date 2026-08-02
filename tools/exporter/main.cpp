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
//!                            android-aab|web
//!                 (--engine-build <preset build dir> | --engine-bundle <dir>
//!                  [--engine-tools <dir>] [--device-payload <dir>])
//!                 [--output <dir>]
//!   orkige_export self-contain --frameworks <dir> [--search <dir>]...
//!                 [--banned <substring>]... <binary>...
//!   orkige_export cook-textures <payload dir> --flavor next|classic
//!                 [--platform ios|android|web]
//! @endverbatim
//!
//! The engine pieces an export packages (the player binary, the engine media)
//! come from ONE of two sources. `--engine-build` is a preset build tree: the
//! developer case. `--engine-bundle` is a STAGED payload - the Media/ tree and
//! the executables a distributed Orkige carries inside itself - which packages
//! the desktop app - and, when the browser player rides along inside it, the
//! web build - on a machine with no repository and no build tree. Everything
//! after the sourcing is the same code, so both produce the same bundle.
//!
//! A MOBILE package ships that platform's player, which is neither of those:
//! a phone runs another architecture's binary, so the player is a separate
//! prebuilt payload - `--device-payload`, the directory a released editor
//! fetches and installs (@see Docs/device-payloads.md), or that platform's own
//! preset build tree.
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
//! way (@see ExportSelfContain.h); `cook-textures` is the same block-
//! compression pass over a payload, reachable so a test can cook a project in
//! place and boot a runtime on it (@see ExportTextureCook.h). Both exist for
//! the same reason: one implementation, two entry points, never a second copy.

#include "ExportFiles.h"
#include "ExportProcess.h"
#include "ExportProject.h"
#include "ExportRun.h"
#include "ExportSelfContain.h"
#include "ExportTextureCook.h"

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
	//! export.icon ("" lets the run derive it from the repository root)
	String defaultIconPath()
	{
#ifdef ORKIGE_EXPORT_DEFAULT_ICON
		const String baked = ORKIGE_EXPORT_DEFAULT_ICON;
		if(OrkigeExport::ExportFiles::isRegularFile(baked))
		{
			return baked;
		}
#endif
		return String();
	}

	//---------------------------------------------------------
	//! the texture cook on its own: the same operation an export performs over
	//! its staged payload, reachable so a test can cook a project tree in place
	//! and boot a runtime on it without packaging a whole app
	int cookTexturesMain(std::vector<String> const & arguments)
	{
		String directory;
		String platform;
		String flavor;
		for(std::size_t index = 0; index < arguments.size(); ++index)
		{
			String const & argument = arguments[index];
			const bool hasValue = index + 1 < arguments.size();
			if(argument == "--platform" && hasValue)
			{
				platform = arguments[++index];
			}
			else if(argument == "--flavor" && hasValue)
			{
				flavor = arguments[++index];
			}
			else if(argument.rfind("--", 0) == 0)
			{
				return fail("unknown cook-textures argument '" + argument + "'");
			}
			else if(directory.empty())
			{
				directory = argument;
			}
			else
			{
				return fail("cook-textures takes ONE payload directory");
			}
		}
		if(directory.empty() || flavor.empty())
		{
			return fail("cook-textures needs a payload directory and --flavor "
				"next|classic (--platform \"\"|ios|android|web)");
		}
		OrkigeExport::TextureCookResult result;
		String error;
		if(!OrkigeExport::cookTexturePayload(directory, platform, flavor,
			result, logLine, &error))
		{
			return fail(error);
		}
		// the machine-readable contract the callers key on
		std::printf("orkige_export: COOKED %d\n", result.cooked);
		std::fflush(stdout);
		return 0;
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
	if(!arguments.empty() && arguments[0] == "cook-textures")
	{
		return cookTexturesMain(
			std::vector<String>(arguments.begin() + 1, arguments.end()));
	}

	String projectPath;
	String platform;
	String engineBuild;
	String engineBundle;
	String engineTools;
	String devicePayload;
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
		else if(argument == "--device-payload") { devicePayload = value; }
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
			"android|android-aab|web\n"
			"                     (--engine-build <preset build dir> |\n"
			"                      --engine-bundle <dir> [--engine-tools "
			"<dir>]\n"
			"                      [--device-payload <dir>])\n"
			"                     [--output <dir>]\n"
			"       orkige_export self-contain --frameworks <dir> "
			"[--search <dir>]... <binary>...\n");
		return 2;
	}
	if(!OrkigeExport::isPackagedPlatform(platform))
	{
		return fail("'" + platform + "' is not a platform this exporter "
			"packages yet");
	}
	if(!engineBundle.empty() && !engineBuild.empty())
	{
		return fail("--engine-bundle and --engine-build name two different "
			"engine sources - pass one");
	}
	if(engineBundle.empty() && engineBuild.empty())
	{
		return fail("no engine source: pass --engine-build <preset build "
			"tree> or --engine-bundle <staged engine payload>");
	}

	OrkigeExport::ExportProject project;
	String error;
	if(!OrkigeExport::ExportProject::readManifest(projectPath, project,
		&error))
	{
		return fail(error);
	}

	OrkigeExport::ExportRequest request;
	request.platform = platform;
	request.outputDirectory = output;
	request.defaultIconPath = defaultIconPath();
	request.cmake = cmakeProgram;
	request.ninja = ninjaProgram;
	request.signingIdentity = signingIdentity;
	request.provisioningProfile = provisioningProfile;
	request.distributionIdentity = distributionIdentity;
	request.distributionProfile = distributionProfile;
	request.androidKeystore = androidKeystore;
	request.androidKeyAlias = androidKeyAlias;
	request.bundletool = bundletool;
	request.unsignedBundleModule = unsignedBundleModule;
	request.environment = OrkigeExport::currentEnvironment();
	if(!engineBundle.empty())
	{
		request.source.bundleResources = engineBundle;
		request.source.bundleTools = engineTools;
		// the prebuilt player for the DEVICE platform being packaged, which a
		// staged payload does not carry (@see ExportPayload.h)
		request.source.devicePayload = devicePayload;
		// a staged payload IS the engine source; the baked repository root
		// stays out of it (@see ExportRun.h - the beside-itself invariant)
	}
	else
	{
		request.source.buildDirectory = engineBuild;
		request.repoRoot = repoRoot;
	}

	String artifact;
	if(!OrkigeExport::runExport(project, request, logLine, artifact, &error))
	{
		return fail(error);
	}
	// the machine-readable contract every caller keys on
	std::printf("orkige_export: OK %s\n", artifact.c_str());
	std::fflush(stdout);
	return 0;
}
