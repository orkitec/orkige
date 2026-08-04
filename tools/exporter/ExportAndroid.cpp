/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportAndroid.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportAndroid.h"

#include "ExportAndroidAssemble.h"
#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportIcons.h"
#include "ExportImage.h"
#include "ExportProcess.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace OrkigeExport
{
	const char * const ANDROID_HOME_ENV = "ANDROID_HOME";
	const char * const ANDROID_SDK_ROOT_ENV = "ANDROID_SDK_ROOT";
	const char * const JAVA_HOME_ENV = "JAVA_HOME";

	namespace
	{
		//! the API level the player's own dex is compiled for; an SDK whose
		//! newest platform is older cannot link a package for it
		const int ANDROID_MINIMUM_API = 28;

		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		//! @p name in @p environment, or "" (whitespace-only reads as absent,
		//! the same rule the signing resolvers use)
		Orkige::String lookup(EnvironmentMap const & environment,
			char const * name)
		{
			EnvironmentMap::const_iterator found = environment.find(name);
			if(found == environment.end())
			{
				return Orkige::String();
			}
			Orkige::String const & value = found->second;
			for(std::size_t index = 0; index < value.size(); ++index)
			{
				if(std::isspace(static_cast<unsigned char>(value[index])) == 0)
				{
					return value;
				}
			}
			return Orkige::String();
		}
		//---------------------------------------------------------
		//! the dot-separated numbers in a version-shaped directory name, or an
		//! empty list when it carries none ("35.0.0" -> {35,0,0})
		std::vector<int> versionParts(Orkige::String const & name)
		{
			std::vector<int> parts;
			std::size_t index = 0;
			while(index < name.size())
			{
				if(std::isdigit(static_cast<unsigned char>(name[index])) == 0)
				{
					// a suffix like "-rc1" ends the comparable prefix rather
					// than disqualifying the whole name
					break;
				}
				int value = 0;
				while(index < name.size() &&
					std::isdigit(static_cast<unsigned char>(name[index])) != 0)
				{
					value = value * 10 +
						static_cast<int>(name[index] - '0');
					++index;
				}
				parts.push_back(value);
				if(index < name.size() && name[index] == '.')
				{
					++index;
				}
				else
				{
					break;
				}
			}
			return parts;
		}
		//---------------------------------------------------------
		//! is @p left an older version than @p right (component-wise; a
		//! shorter prefix that ties sorts older, so "35.0" < "35.0.1")
		bool olderVersion(std::vector<int> const & left,
			std::vector<int> const & right)
		{
			const std::size_t count = std::max(left.size(), right.size());
			for(std::size_t index = 0; index < count; ++index)
			{
				const int a = index < left.size() ? left[index] : 0;
				const int b = index < right.size() ? right[index] : 0;
				if(a != b)
				{
					return a < b;
				}
			}
			return false;
		}
		//---------------------------------------------------------
		//! the immediate subdirectory names of @p path, sorted (empty when it
		//! is not a directory - an absent SDK piece is not an error here, it
		//! is the answer)
		std::vector<Orkige::String> subdirectoryNames(
			Orkige::String const & path)
		{
			std::vector<Orkige::String> names;
			std::error_code ignored;
			const std::filesystem::directory_iterator end;
			std::filesystem::directory_iterator entry(
				std::filesystem::path(path), ignored);
			if(ignored)
			{
				return names;
			}
			for(; entry != end; entry.increment(ignored))
			{
				if(ignored)
				{
					break;
				}
				if(entry->is_directory(ignored))
				{
					names.push_back(entry->path().filename().string());
				}
			}
			std::sort(names.begin(), names.end());
			return names;
		}
		//---------------------------------------------------------
		//! is @p home a real JDK? All three programs a package needs - javac
		//! compiles the Java, java runs d8 and apksigner, keytool makes the
		//! debug key - AND the `release` descriptor every JDK since modules
		//! carries at its root.
		//! @remarks the descriptor is what keeps this HONEST on macOS, which
		//! ships /usr/bin/javac, java and keytool as STUBS that only forward to
		//! a JDK if one is installed. Reporting "found a JDK" off those and
		//! then failing inside javac would be exactly the lumped, unactionable
		//! answer this whole resolution exists to avoid.
		bool isJdk(Orkige::String const & home)
		{
			if(home.empty())
			{
				return false;
			}
			const Orkige::String bin = ExportFiles::join(home, "bin");
			char const * const programs[] = { "javac", "java", "keytool" };
			for(char const * program : programs)
			{
				const Orkige::String path =
					ExportFiles::join(bin, program);
				if(!ExportFiles::isRegularFile(path) &&
					!ExportFiles::isRegularFile(path + ".exe"))
				{
					return false;
				}
			}
			return ExportFiles::isRegularFile(
				ExportFiles::join(home, "release")) ||
				ExportFiles::isRegularFile(
					ExportFiles::join(home, "lib/modules"));
		}
		//---------------------------------------------------------
		//! the JDK home a `javac` on the PATH belongs to (`<home>/bin/javac`),
		//! following the symlink first - the usual shape of a package
		//! manager's shim
		Orkige::String jdkHomeFromPath()
		{
			const Orkige::String javac = findOnPath("javac");
			if(javac.empty())
			{
				return Orkige::String();
			}
			std::error_code ignored;
			std::filesystem::path real =
				std::filesystem::canonical(std::filesystem::path(javac),
					ignored);
			if(ignored)
			{
				real = std::filesystem::path(javac);
			}
			const std::filesystem::path home =
				real.parent_path().parent_path();
			return home.string();
		}
		//---------------------------------------------------------
		//! every JDK home worth probing, in order: what the environment names,
		//! what the PATH implies, then where each platform's own packaging
		//! puts one - the same "found rather than configured" rule the SDK
		//! lookup follows
		std::vector<Orkige::String> jdkCandidates(
			EnvironmentMap const & environment)
		{
			std::vector<Orkige::String> candidates;
			const Orkige::String named = lookup(environment, JAVA_HOME_ENV);
			if(!named.empty())
			{
				candidates.push_back(named);
			}
			const Orkige::String onPath = jdkHomeFromPath();
			if(!onPath.empty())
			{
				candidates.push_back(onPath);
			}
#if defined(__APPLE__)
			candidates.push_back("/opt/homebrew/opt/openjdk/libexec/"
				"openjdk.jdk/Contents/Home");
			candidates.push_back("/usr/local/opt/openjdk/libexec/"
				"openjdk.jdk/Contents/Home");
			// every installed runtime, newest name last so it is preferred
			char const * const jvms = "/Library/Java/JavaVirtualMachines";
			for(Orkige::String const & name : subdirectoryNames(jvms))
			{
				candidates.push_back(ExportFiles::join(
					ExportFiles::join(jvms, name), "Contents/Home"));
			}
#endif
			return candidates;
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
		//! stage the launcher-icon res/ tree the assembly compiles with aapt2
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
	int androidMinimumApi()
	{
		return ANDROID_MINIMUM_API;
	}
	//---------------------------------------------------------
	bool AndroidToolchain::complete() const
	{
		return this->aapt2 && this->zipalign && this->apksigner && this->d8 &&
			this->jdk && !this->platformJar.empty();
	}
	//---------------------------------------------------------
	Orkige::String newestAndroidBuildTools(
		std::vector<Orkige::String> const & versions)
	{
		Orkige::String best;
		std::vector<int> bestParts;
		for(Orkige::String const & name : versions)
		{
			const std::vector<int> parts = versionParts(name);
			if(parts.empty())
			{
				continue;
			}
			if(best.empty() || olderVersion(bestParts, parts))
			{
				best = name;
				bestParts = parts;
			}
		}
		return best;
	}
	//---------------------------------------------------------
	Orkige::String newestAndroidPlatform(
		std::vector<Orkige::String> const & names, int minimumApi)
	{
		const Orkige::String prefix = "android-";
		Orkige::String best;
		int bestApi = 0;
		for(Orkige::String const & name : names)
		{
			if(name.compare(0, prefix.size(), prefix) != 0)
			{
				continue;
			}
			const Orkige::String tail = name.substr(prefix.size());
			// a preview platform is named for its letter ("android-VanillaIce")
			// and carries no stable API number - skip rather than guess
			const std::vector<int> parts = versionParts(tail);
			if(parts.empty() || tail.size() != std::to_string(parts[0]).size())
			{
				continue;
			}
			if(parts[0] >= minimumApi && parts[0] > bestApi)
			{
				best = name;
				bestApi = parts[0];
			}
		}
		return best;
	}
	//---------------------------------------------------------
	Orkige::String androidVcpkgRoot(EnvironmentMap const & environment)
	{
		const Orkige::String named = lookup(environment, "VCPKG_ROOT");
		if(!named.empty())
		{
			return named;
		}
		const Orkige::String home = lookup(environment, "HOME");
		return home.empty() ? Orkige::String()
			: ExportFiles::join(home, "Development/vcpkg");
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> androidSdkCandidates(
		EnvironmentMap const & environment)
	{
		std::vector<Orkige::String> candidates;
		const Orkige::String named[] = {
			lookup(environment, ANDROID_HOME_ENV),
			lookup(environment, ANDROID_SDK_ROOT_ENV)
		};
		for(Orkige::String const & value : named)
		{
			if(!value.empty())
			{
				candidates.push_back(value);
			}
		}
		// ...then where each platform's own installer puts one, so a person
		// who installed Android Studio and configured nothing is still found
		const Orkige::String home = lookup(environment, "HOME");
		if(!home.empty())
		{
#if defined(__APPLE__)
			candidates.push_back(
				ExportFiles::join(home, "Library/Android/sdk"));
#else
			candidates.push_back(ExportFiles::join(home, "Android/Sdk"));
			candidates.push_back(ExportFiles::join(home, "Android/sdk"));
#endif
		}
		const Orkige::String localAppData = lookup(environment, "LOCALAPPDATA");
		if(!localAppData.empty())
		{
			candidates.push_back(
				ExportFiles::join(localAppData, "Android/Sdk"));
		}
		return candidates;
	}
	//---------------------------------------------------------
	AndroidToolchain resolveAndroidToolchain(
		EnvironmentMap const & environment)
	{
		AndroidToolchain tools;
		for(Orkige::String const & candidate :
			androidSdkCandidates(environment))
		{
			if(ExportFiles::isDirectory(
				ExportFiles::join(candidate, "build-tools")) ||
				ExportFiles::isDirectory(
					ExportFiles::join(candidate, "platforms")))
			{
				tools.sdkRoot = candidate;
				break;
			}
		}
		if(!tools.sdkRoot.empty())
		{
			// the NEWEST installed of each, rather than a pinned version: a
			// person's SDK is whatever their SDK manager gave them, and every
			// build-tools release since the minimum API assembles this package
			const Orkige::String buildToolsRoot =
				ExportFiles::join(tools.sdkRoot, "build-tools");
			const Orkige::String version = newestAndroidBuildTools(
				subdirectoryNames(buildToolsRoot));
			if(!version.empty())
			{
				tools.buildTools = ExportFiles::join(buildToolsRoot, version);
			}
			const Orkige::String platformsRoot =
				ExportFiles::join(tools.sdkRoot, "platforms");
			const Orkige::String platform = newestAndroidPlatform(
				subdirectoryNames(platformsRoot), androidMinimumApi());
			if(!platform.empty())
			{
				const Orkige::String jar = ExportFiles::join(
					ExportFiles::join(platformsRoot, platform), "android.jar");
				if(ExportFiles::isRegularFile(jar))
				{
					tools.platformJar = jar;
				}
			}
		}
		if(!tools.buildTools.empty())
		{
			tools.aapt2 = ExportFiles::isRegularFile(
				ExportFiles::join(tools.buildTools, "aapt2")) ||
				ExportFiles::isRegularFile(
					ExportFiles::join(tools.buildTools, "aapt2.exe"));
			tools.zipalign = ExportFiles::isRegularFile(
				ExportFiles::join(tools.buildTools, "zipalign")) ||
				ExportFiles::isRegularFile(
					ExportFiles::join(tools.buildTools, "zipalign.exe"));
			// the JARs rather than the launcher scripts: those are what the
			// packaging script runs, through the JDK's own `java`
			tools.apksigner = ExportFiles::isRegularFile(
				ExportFiles::join(tools.buildTools, "lib/apksigner.jar"));
			tools.d8 = ExportFiles::isRegularFile(
				ExportFiles::join(tools.buildTools, "lib/d8.jar"));
		}
		for(Orkige::String const & candidate : jdkCandidates(environment))
		{
			if(isJdk(candidate))
			{
				tools.javaHome = candidate;
				break;
			}
		}
		tools.jdk = !tools.javaHome.empty();
		return tools;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> androidToolchainGaps(
		AndroidToolchain const & tools)
	{
		std::vector<Orkige::String> missing;
		if(tools.sdkRoot.empty())
		{
			missing.push_back(Orkige::String("the Android SDK - install it "
				"with Android Studio or the command-line tools, then point ") +
				ANDROID_HOME_ENV + " at it");
		}
		else
		{
			// one sentence per PROGRAM: somebody who has the SDK and only
			// lacks the platform jar should read about the platform jar
			if(!tools.aapt2)
			{
				missing.push_back("aapt2 - install the Android SDK build tools "
					"(`sdkmanager \"build-tools;35.0.0\"`, or SDK Manager > SDK "
					"Tools > Android SDK Build-Tools)");
			}
			if(!tools.zipalign)
			{
				missing.push_back("zipalign - it ships with the Android SDK "
					"build tools (`sdkmanager \"build-tools;35.0.0\"`)");
			}
			if(!tools.apksigner)
			{
				missing.push_back("apksigner - it ships with the Android SDK "
					"build tools (`sdkmanager \"build-tools;35.0.0\"`)");
			}
			if(!tools.d8)
			{
				missing.push_back("d8 - it ships with the Android SDK build "
					"tools (`sdkmanager \"build-tools;35.0.0\"`)");
			}
			if(tools.platformJar.empty())
			{
				missing.push_back("an Android platform of API " +
					std::to_string(androidMinimumApi()) + " or newer "
					"(`sdkmanager \"platforms;android-35\"`)");
			}
		}
		if(!tools.jdk)
		{
			missing.push_back(Orkige::String("a JDK - javac compiles the "
				"package's Java, java runs the SDK's own tools and keytool "
				"makes the debug key (install one, e.g. `brew install "
				"openjdk` or your distribution's OpenJDK package, and set ") +
				JAVA_HOME_ENV + ")");
		}
		return missing;
	}
	//---------------------------------------------------------
	Orkige::String androidToolchainRefusal(AndroidToolchain const & tools)
	{
		const std::vector<Orkige::String> missing =
			androidToolchainGaps(tools);
		if(missing.empty())
		{
			return Orkige::String();
		}
		// the engine/toolchain split, said out loud: Orkige ships the player,
		// Android's own tools assemble the package around it
		Orkige::String message = "an Android package is assembled by the "
			"Android SDK's own tools, which this machine is missing:";
		for(std::size_t index = 0; index < missing.size(); ++index)
		{
			message += "\n  - " + missing[index];
		}
		if(!tools.sdkRoot.empty())
		{
			message += "\n(looked under '" + tools.sdkRoot + "')";
		}
		// the reading, named as a DOC rather than a URL: this library is also
		// a standalone CLI and carries no editor constants, and a doc name is
		// what doc_link_lint can check against the corpus on disk
		message += "\nSee Docs/device-payloads.md for the whole set of "
			"Android prerequisites.";
		return message;
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
		// TWO engine sources, like every other platform: a preset build tree,
		// or a FETCHED device payload - which carries the stripped player, the
		// engine media, the packaging script and the Java it compiles, so a
		// machine with no repository has every ENGINE piece. What is left is
		// the machine's own Android toolchain, gated below by name.
		const bool fromPayload = !source.devicePayload.empty();
		if(source.fromBundle() && !fromPayload)
		{
			return report(error, "a staged engine payload packages the desktop "
				"app only; an Android package needs the Android player - "
				"install it (Settings > Build Targets), or package from the "
				"android-debug preset build tree");
		}
		if(!fromPayload && environment.repoRoot.empty())
		{
			return report(error, "an Android package is assembled around the "
				"manifest template and the Java glue beside the player "
				"(tools/player/android) - this export has no engine source "
				"tree to take them from");
		}
		if(fromPayload && request.bundle)
		{
			// a store App Bundle is built off an android-RELEASE tree and needs
			// bundletool plus a release keystore; the published player is the
			// debug one, so shipping a bundle out of it would be a lie about
			// what was optimized. Store artifacts stay a source-tree job.
			return report(error, "a release App Bundle is built from the "
				"android-release preset tree (the installed player is the debug "
				"build) - see Docs/store-release.md; the installed player "
				"packages APKs");
		}
		const Orkige::String nativeLib = fromPayload
			? ExportFiles::join(source.devicePayload, "libmain.so")
			: ExportFiles::join(
				ExportFiles::join(source.buildDirectory, "tools/player"),
				"libmain.so");
		if(!ExportFiles::isRegularFile(nativeLib))
		{
			return report(error, fromPayload
				? ("the installed Android player is incomplete (no libmain.so "
					"at '" + nativeLib + "') - fetch it again under Settings > "
					"Build Targets")
				: ("no libmain.so at '" + nativeLib + "' - build the android-" +
					(request.bundle ? "release (or android-debug)" : "debug") +
					" preset first"));
		}
		if(!fromPayload && request.bundle &&
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
		// the machine's half of the prerequisites, reported program by program.
		// This is a TOOLCHAIN gate and nothing else: a project with no compiled
		// C++ has no engine SDK question to answer, so none is asked here.
		const AndroidToolchain tools =
			resolveAndroidToolchain(request.environment);
		const Orkige::String toolchainRefusal = androidToolchainRefusal(tools);
		if(!toolchainRefusal.empty())
		{
			return report(error, toolchainRefusal);
		}
		emit(environment.log, "Android tools: " + tools.buildTools +
			", JDK " + tools.javaHome);

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
		// the Android library archives the project depends on: project-relative
		// paths to files it already carries - nothing is downloaded and no
		// dependency graph is resolved (Docs/android-libraries.md)
		std::vector<Orkige::String> libraryNames;
		if(!androidLibrarySettings(project.settings, libraryNames, error))
		{
			return false;
		}
		std::vector<Orkige::String> libraryArchives;
		for(Orkige::String const & relative : libraryNames)
		{
			const Orkige::String path =
				ExportFiles::join(project.root, relative);
			if(!ExportFiles::isRegularFile(path))
			{
				return report(error, "export.android.libraries names '" +
					relative + "', which is not a file in this project (looked "
					"at '" + path + "')");
			}
			libraryArchives.push_back(path);
		}
		if(!libraryArchives.empty())
		{
			emit(environment.log, "Android libraries: " +
				std::to_string(libraryArchives.size()) + " archive(s)");
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

		const Orkige::String flavor = fromPayload
			? payloadFlavor(source.devicePayload)
			: renderBackend(source.buildDirectory);
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

		// the assembly runs IN PROCESS over the Android SDK's own programs
		AndroidPackageRequest packaging;
		packaging.buildDirectory = fromPayload ? Orkige::String()
			: source.buildDirectory;
		packaging.devicePayload = source.devicePayload;
		packaging.repoRoot = environment.repoRoot;
		packaging.vcpkgRoot = androidVcpkgRoot(request.environment);
		packaging.projectPayload = payloadDirectory;
		packaging.launcherResources = resDirectory;
		packaging.libraryArchives = libraryArchives;
		packaging.package = package;
		packaging.label = project.name;
		packaging.launchColour = launchColour;
		packaging.assetsMode = assetsMode;
		// only a NON-auto lock reaches the manifest, so an unconstrained
		// project leaves the template byte-identical
		packaging.screenOrientation = orientation == "auto"
			? Orkige::String() : androidScreenOrientation(orientation);
		packaging.bundle = request.bundle;
		packaging.moduleOnly = request.options.moduleOnly;
		packaging.versionCode = request.options.versionCode;
		packaging.versionName = request.options.versionName;
		packaging.bundletool = request.options.bundletool;
		packaging.keystore = request.options.keystore;
		packaging.environment = request.environment;
		packaging.log = environment.log;
		packaging.runner = environment.runner;
		packaging.outputPath = request.bundle
			? ExportFiles::join(outputDirectory, project.exeName() +
				(request.options.moduleOnly ? ".aab.module.zip" : ".aab"))
			: ExportFiles::join(outputDirectory, project.exeName() + ".apk");

		Orkige::String artifact;
		const bool packaged = assembleAndroidPackage(packaging, tools, artifact,
			error);
		ExportFiles::removeTree(payloadDirectory, 0);
		ExportFiles::removeTree(resDirectory, 0);
		if(!packaged)
		{
			return false;
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
