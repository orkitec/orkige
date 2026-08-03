/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportMobileTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! @file ExportMobileTests.cpp
//! @brief the decisions a mobile package is made of.
//!
//! Everything here is composition: which keys land on an iOS Info.plist, what
//! an `.ipa` calls a bundle file, the exact `codesign` invocation, the two
//! Android packaging command lines and the credential gate a signed bundle
//! sits behind. None of it needs a certificate, a keystore, an SDK or a
//! device - which is why the argument shape can be a hard gate on every host
//! while the cryptography itself is exercised only where credentials exist.

#include "ExportAndroid.h"
#include "ExportFiles.h"
#include "ExportImage.h"
#include "ExportIos.h"
#include "ExportPlist.h"

#include <engine_filesystem/MiniZip.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace OrkigeExport;

namespace
{
	ExportProject makeProject(
		std::map<Orkige::String, Orkige::String> const & settings = {})
	{
		ExportProject project;
		project.root = "/projects/jumper-lua";
		project.name = "Jumper Lua";
		project.settings = settings;
		return project;
	}
	//---------------------------------------------------------
	//! the value of a plist key as a flat string, for the array cases
	std::vector<Orkige::String> stringsOf(Orkige::JsonValue const & array)
	{
		std::vector<Orkige::String> out;
		for(std::size_t index = 0; index < array.size(); ++index)
		{
			out.push_back(array.at(index).asString());
		}
		return out;
	}
}

//--- iOS ---------------------------------------------------------

TEST_CASE("iosInfoPlistKeys rewrites the generic player identity",
	"[exporter][ios]")
{
	const Orkige::JsonValue keys = iosInfoPlistKeys(makeProject(),
		"com.orkitec.jumperlua", { "AppIcon120.png", "AppIcon180.png" });
	REQUIRE(keys.get("CFBundleIdentifier").asString() ==
		"com.orkitec.jumperlua");
	REQUIRE(keys.get("CFBundleName").asString() == "Jumper Lua");
	REQUIRE(keys.get("CFBundleDisplayName").asString() == "Jumper Lua");
	// the icon list drops the extension: iOS resolves the @2x/@3x variants
	// itself off the base name
	REQUIRE(stringsOf(keys.get("CFBundleIcons").get("CFBundlePrimaryIcon")
		.get("CFBundleIconFiles")) ==
		std::vector<Orkige::String>{ "AppIcon120", "AppIcon180" });
}

TEST_CASE("iosInfoPlistKeys carries the orientation lock and the cleartext "
	"policy", "[exporter][ios]")
{
	const Orkige::JsonValue portrait = iosInfoPlistKeys(makeProject(),
		"com.example.game", {});
	REQUIRE(stringsOf(portrait.get("UISupportedInterfaceOrientations")) ==
		iosOrientations("portrait"));

	const Orkige::JsonValue landscape = iosInfoPlistKeys(
		makeProject({ { "export.orientation", "landscape" } }),
		"com.example.game", {});
	REQUIRE(stringsOf(landscape.get("UISupportedInterfaceOrientations")) ==
		iosOrientations("landscape"));

	// cleartext reaches the local network only - never arbitrary loads
	const Orkige::JsonValue security = portrait.get("NSAppTransportSecurity");
	REQUIRE(security.get("NSAllowsLocalNetworking").asBool());
	REQUIRE_FALSE(security.has("NSAllowsArbitraryLoads"));
}

TEST_CASE("ipaArchiveName puts every bundle file under Payload/<App>.app",
	"[exporter][ios]")
{
	const Orkige::String app = "/out/Jumper Lua.app";
	REQUIRE(ipaArchiveName(app, app + "/OrkigePlayer") ==
		"Payload/Jumper Lua.app/OrkigePlayer");
	REQUIRE(ipaArchiveName(app, app + "/project/project.orkproj") ==
		"Payload/Jumper Lua.app/project/project.orkproj");
	// forward slashes on every host: a zip entry path is not a native path
	REQUIRE(ipaArchiveName(app, app + "/Media/Hlms/Pbs/Any.glsl")
		.find('\\') == Orkige::String::npos);
}

TEST_CASE("the codesign calls carry what a device install requires",
	"[exporter][ios]")
{
	const std::vector<Orkige::String> nested =
		codesignNestedArguments("Apple Development: Someone", "/app/lib.dylib");
	REQUIRE(nested == std::vector<Orkige::String>{ "codesign", "--force",
		"--sign", "Apple Development: Someone", "/app/lib.dylib" });

	const std::vector<Orkige::String> bundle = codesignBundleArguments(
		"Apple Development: Someone", "/out/entitlements.plist", "/out/G.app");
	REQUIRE(bundle == std::vector<Orkige::String>{ "codesign", "--force",
		"--sign", "Apple Development: Someone",
		"--entitlements", "/out/entitlements.plist",
		"--generate-entitlement-der", "/out/G.app" });
}

//--- Android -----------------------------------------------------

TEST_CASE("androidPackageName defaults off the project slug and validates",
	"[exporter][android]")
{
	Orkige::String package;
	Orkige::String error;
	REQUIRE(androidPackageName(makeProject(), package, &error));
	REQUIRE(package == "com.orkitec.jumperlua");

	REQUIRE(androidPackageName(
		makeProject({ { "export.android.package", "com.studio.game" } }),
		package, &error));
	REQUIRE(package == "com.studio.game");

	REQUIRE_FALSE(androidPackageName(
		makeProject({ { "export.android.package", "not a package" } }),
		package, &error));
	REQUIRE(error.find("not a valid Android package name") !=
		Orkige::String::npos);
}

TEST_CASE("the APK command line reaches package_apk.sh whole",
	"[exporter][android]")
{
	const std::vector<Orkige::String> command = androidApkArguments(
		"/repo/tools/player/android/package_apk.sh", "/out/payload-staging",
		"com.orkitec.jumperlua", "Jumper Lua", "/out/res-staging", "#101014",
		"stored", "portrait", "/out/JumperLua.apk", "/build/android-debug");
	REQUIRE(command == std::vector<Orkige::String>{
		"bash", "/repo/tools/player/android/package_apk.sh",
		"--project-payload", "/out/payload-staging",
		"--package", "com.orkitec.jumperlua",
		"--label", "Jumper Lua",
		"--res-dir", "/out/res-staging",
		"--launch-color", "#101014",
		"--assets", "stored",
		"--output", "/out/JumperLua.apk",
		"--orientation", androidScreenOrientation("portrait"),
		"/build/android-debug" });
}

TEST_CASE("only a locked orientation reaches the Android manifest",
	"[exporter][android]")
{
	// `auto` leaves the template's own (unspecified) value alone, so an
	// unconstrained project's manifest stays byte-identical
	const std::vector<Orkige::String> command = androidApkArguments(
		"/s.sh", "/p", "com.a.b", "L", "/r", "#000000", "stored", "auto",
		"/o.apk", "/build");
	REQUIRE(std::find(command.begin(), command.end(), "--orientation") ==
		command.end());
	REQUIRE(command.back() == "/build");
}

TEST_CASE("the App Bundle command line carries the version and the signing "
	"material", "[exporter][android]")
{
	AndroidBundleOptions options;
	options.versionCode = 7;
	options.versionName = "1.2.3";
	options.keystore.keystore = "/secrets/release.jks";
	options.keystore.alias = "upload";
	options.keystore.hasStorePassword = true;
	options.bundletool = "/tools/bundletool.jar";
	const std::vector<Orkige::String> command = androidBundleArguments(
		"/repo/tools/player/android/build_aab.sh", "/out/payload-staging",
		"com.orkitec.jumperlua", "Jumper Lua", "/out/res-staging", "#101014",
		"stored", "auto", "/out/JumperLua.aab", "/build/android-release",
		options);
	REQUIRE(command == std::vector<Orkige::String>{
		"bash", "/repo/tools/player/android/build_aab.sh",
		"--project-payload", "/out/payload-staging",
		"--package", "com.orkitec.jumperlua",
		"--label", "Jumper Lua",
		"--res-dir", "/out/res-staging",
		"--launch-color", "#101014",
		"--assets", "stored",
		"--output", "/out/JumperLua.aab",
		"--version-code", "7",
		"--version-name", "1.2.3",
		"/build/android-release",
		"--keystore", "/secrets/release.jks",
		"--key-alias", "upload",
		"--bundletool", "/tools/bundletool.jar" });
}

TEST_CASE("the unsigned bundle module asks for no credentials",
	"[exporter][android]")
{
	AndroidBundleOptions options;
	options.moduleOnly = true;
	const std::vector<Orkige::String> command = androidBundleArguments(
		"/s.sh", "/p", "com.a.b", "L", "/r", "#000000", "stored", "auto",
		"/o.aab.module.zip", "/build", options);
	REQUIRE(command.back() == "--module-only");
	REQUIRE(std::find(command.begin(), command.end(), "--keystore") ==
		command.end());
	REQUIRE(std::find(command.begin(), command.end(), "--bundletool") ==
		command.end());
}

//--- the signed iOS assembly, over an injected runner ---------------
// A real signature needs a certificate this machine does not hold, so the
// CRYPTOGRAPHY is left to a machine that has one. Everything around it - the
// bundle assembly, the plist rewrite, the profile embed, the inside-out
// signing ORDER and the .ipa layout - is exercised here with `codesign`
// standing in as a recording runner, because that is where a signed build
// actually breaks.

namespace
{
	//! @brief a scratch directory that removes itself.
	//! @remarks the name is the CALLER's, never a random number: every test
	//! case runs in its own process (catch_discover_tests), those processes
	//! run in parallel, and two of them seeded alike would draw the same
	//! "random" name and delete each other's tree mid-assembly. A name unique
	//! per test case cannot collide, because no two processes run the same
	//! case.
	class Scratch
	{
	public:
		explicit Scratch(Orkige::String const & name)
		{
			this->mPath = (std::filesystem::temp_directory_path() /
				("orkige_ios_test_" + name)).string();
			ExportFiles::removeTree(this->mPath, 0);
			ExportFiles::makeDirectories(this->mPath, 0);
		}
		~Scratch() { ExportFiles::removeTree(this->mPath, 0); }
		Orkige::String at(Orkige::String const & name) const
		{
			return ExportFiles::join(this->mPath, name);
		}
	private:
		Orkige::String mPath;
	};

	//! a minimal stand-in for what an ios-device preset build produces
	Orkige::String makePlayerBundle(Scratch const & scratch)
	{
		const Orkige::String app =
			scratch.at("tree/tools/player/OrkigePlayer.app");
		Orkige::String error;
		REQUIRE(ExportFiles::writeTextFile(
			ExportFiles::join(app, "OrkigePlayer"), "binary\n", &error));
		REQUIRE(ExportFiles::makeExecutable(
			ExportFiles::join(app, "OrkigePlayer"), &error));
		REQUIRE(ExportFiles::writeTextFile(
			ExportFiles::join(app, "Media/Hlms/keep.txt"), "shader\n", &error));
		// two nested dylibs, so the inside-out order has something to order
		REQUIRE(ExportFiles::writeTextFile(
			ExportFiles::join(app, "Frameworks/libb.dylib"), "b\n", &error));
		REQUIRE(ExportFiles::writeTextFile(
			ExportFiles::join(app, "Frameworks/liba.dylib"), "a\n", &error));
		Orkige::JsonValue info = Orkige::JsonValue::object();
		info.set("CFBundleIdentifier",
			Orkige::JsonValue("com.orkitec.orkige-player"));
		info.set("CFBundleExecutable", Orkige::JsonValue("OrkigePlayer"));
		info.set("UILaunchScreen", Orkige::JsonValue::object());
		REQUIRE(ExportPlist::write(info, ExportFiles::join(app, "Info.plist"),
			&error));
		return app;
	}
}

TEST_CASE("a signed iOS export assembles, signs inside-out and packages an "
	"ipa", "[exporter][ios]")
{
	Scratch scratch("signed");
	Orkige::String error;
	const Orkige::String sourceApp = makePlayerBundle(scratch);
	// a project whose payload is one manifest and one scene
	const Orkige::String projectRoot = scratch.at("project");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(projectRoot, "project.orkproj"),
		"<OrkigeProject><Name>Jumper Lua</Name></OrkigeProject>\n", &error));
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(projectRoot, "scenes/main.oscene"), "scene\n",
		&error));
	const Orkige::String profile = scratch.at("dev.mobileprovision");
	REQUIRE(ExportFiles::writeTextFile(profile, "profile\n", &error));
	const Orkige::String icon = scratch.at("icon.png");
	REQUIRE(encodePngFile(ExportImage(256, 256), icon, &error));

	ExportProject project = makeProject({ { "export.ios.teamId", "ABCDE12345" },
		{ "export.ios.bundleId", "com.studio.jumper" } });
	project.root = projectRoot;

	std::vector<std::vector<Orkige::String> > calls;
	ExportEnvironment environment;
	environment.defaultIconPath = icon;
	environment.runner =
		[&calls](std::vector<Orkige::String> const & arguments)
		{
			calls.push_back(arguments);
			ProcessResult result;
			result.launched = true;
			result.exitCode = 0;
			return result;
		};

	EngineSource source;
	source.buildDirectory = scratch.at("tree");
	IosRequest request;
	request.signing.identity = "Apple Distribution: Studio";
	request.signing.profile = profile;
	request.distribution = true;
	request.packageIpa = true;

	Orkige::String artifact;
	const bool exported = exportIos(project, source, scratch.at("out"),
		request, environment, artifact, &error);
	// an export reports its own refusal - assert THROUGH it, so a failure
	// names the missing piece instead of just saying "false"
	INFO("export refused: " << error);
	REQUIRE(exported);
	REQUIRE(error.empty());

	const Orkige::String app = ExportFiles::join(scratch.at("out"),
		"Jumper Lua.app");
	// the bundle is FLAT: the marker and the payload sit where
	// SDL_GetBasePath() points on iOS
	REQUIRE(ExportFiles::isRegularFile(
		ExportFiles::join(app, "orkige_project.txt")));
	REQUIRE(ExportFiles::isRegularFile(
		ExportFiles::join(app, "project/project.orkproj")));
	REQUIRE(ExportFiles::isRegularFile(
		ExportFiles::join(app, "project/scenes/main.oscene")));
	// the player bundle's own media survives the copy
	REQUIRE(ExportFiles::isRegularFile(
		ExportFiles::join(app, "Media/Hlms/keep.txt")));
	REQUIRE(ExportFiles::isRegularFile(
		ExportFiles::join(app, "PrivacyInfo.xcprivacy")));
	REQUIRE(ExportFiles::isRegularFile(
		ExportFiles::join(app, "embedded.mobileprovision")));

	// the generic player identity is gone; every template key survives
	Orkige::JsonValue info;
	REQUIRE(ExportPlist::read(ExportFiles::join(app, "Info.plist"), info,
		&error));
	REQUIRE(info.get("CFBundleIdentifier").asString() == "com.studio.jumper");
	REQUIRE(info.get("CFBundleExecutable").asString() == "OrkigePlayer");
	REQUIRE(info.has("UILaunchScreen"));
	REQUIRE(info.get("CFBundleIcons").get("CFBundlePrimaryIcon")
		.get("CFBundleIconFiles").size() > 0);

	// inside-out: both nested dylibs, THEN the bundle. A bundle signature
	// seals what it contains, so a later nested sign would invalidate it.
	REQUIRE(calls.size() == 3);
	REQUIRE(calls[0].back() == ExportFiles::join(app, "Frameworks/liba.dylib"));
	REQUIRE(calls[1].back() == ExportFiles::join(app, "Frameworks/libb.dylib"));
	REQUIRE(calls[2] == codesignBundleArguments("Apple Distribution: Studio",
		ExportFiles::join(scratch.at("out"), "entitlements.plist"), app));
	// the entitlements file is transient - it exists only for the signature
	REQUIRE_FALSE(ExportFiles::exists(
		ExportFiles::join(scratch.at("out"), "entitlements.plist")));

	// the artifact is the .ipa, and every bundle file sits under Payload/
	REQUIRE(artifact == ExportFiles::join(scratch.at("out"), "JumperLua.ipa"));
	Orkige::MiniZip ipa;
	REQUIRE(ipa.open(artifact));
	REQUIRE(ipa.contains("Payload/Jumper Lua.app/OrkigePlayer"));
	REQUIRE(ipa.contains("Payload/Jumper Lua.app/project/project.orkproj"));
	for(std::string const & name : ipa.names())
	{
		REQUIRE(name.rfind("Payload/", 0) == 0);
	}
}

TEST_CASE("an unsigned iOS export runs no codesign and keeps the .app",
	"[exporter][ios]")
{
	Scratch scratch("unsigned");
	Orkige::String error;
	makePlayerBundle(scratch);
	const Orkige::String projectRoot = scratch.at("project");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(projectRoot, "project.orkproj"),
		"<OrkigeProject><Name>Jumper Lua</Name></OrkigeProject>\n", &error));
	const Orkige::String icon = scratch.at("icon.png");
	REQUIRE(encodePngFile(ExportImage(256, 256), icon, &error));

	ExportProject project = makeProject();
	project.root = projectRoot;
	std::vector<std::vector<Orkige::String> > calls;
	ExportEnvironment environment;
	environment.defaultIconPath = icon;
	environment.runner =
		[&calls](std::vector<Orkige::String> const & arguments)
		{
			calls.push_back(arguments);
			ProcessResult result;
			result.launched = true;
			return result;
		};
	EngineSource source;
	source.buildDirectory = scratch.at("tree");

	Orkige::String artifact;
	const bool exported = exportIos(project, source, scratch.at("out"),
		IosRequest(), environment, artifact, &error);
	INFO("export refused: " << error);
	REQUIRE(exported);
	// a simulator runs unsigned code - there is nothing to sign and nothing to
	// wrap
	REQUIRE(calls.empty());
	REQUIRE(artifact == ExportFiles::join(scratch.at("out"),
		"Jumper Lua.app"));
	REQUIRE_FALSE(ExportFiles::exists(
		ExportFiles::join(artifact, "embedded.mobileprovision")));
	// the bundle id falls back to the project slug
	Orkige::JsonValue info;
	REQUIRE(ExportPlist::read(ExportFiles::join(artifact, "Info.plist"), info,
		&error));
	REQUIRE(info.get("CFBundleIdentifier").asString() ==
		"com.orkitec.jumperlua");
}

TEST_CASE("an iOS export of compiled game code asks for an SDK pack, and only "
	"then", "[exporter][ios]")
{
	// THE TWO PREREQUISITES ARE SEPARATE and neither ever stands in for the
	// other. A project whose game code is compiled ships its own module, so it
	// needs an engine to build against - a pack - and a prebuilt player is no
	// substitute. A project whose behaviour is Lua ships the PLAYER and has
	// nothing to compile, so a pack must never be asked of it, named to it, or
	// stand between it and a release build.
	Scratch scratch("tiers");
	Orkige::String artifact;
	Orkige::String error;
	{
		ExportProject project =
			makeProject({ { "native.target", "JumperNative" } });
		EngineSource source;
		source.buildDirectory = scratch.at("tree");
		REQUIRE_FALSE(exportIos(project, source, scratch.at("out"),
			IosRequest(), ExportEnvironment(), artifact, &error));
		REQUIRE(error.find("SDK") != Orkige::String::npos);
	}
	{
		// ...and the Lua project in the same spot is told about the PLAYER it
		// is missing, with no mention of an SDK anywhere in the sentence
		ExportProject project = makeProject();
		EngineSource source;
		source.bundleResources = scratch.at("staged");
		REQUIRE_FALSE(exportIos(project, source, scratch.at("out"),
			IosRequest(), ExportEnvironment(), artifact, &error));
		REQUIRE(error.find("iOS player") != Orkige::String::npos);
		REQUIRE(error.find("SDK") == Orkige::String::npos);
	}
}

TEST_CASE("the signed-bundle gate names every missing credential",
	"[exporter][android]")
{
	AndroidKeystore none;
	const std::vector<Orkige::String> empty = androidSigningGaps(none, "");
	REQUIRE(empty.size() == 2);				// bundletool AND a keystore
	REQUIRE(empty[0].find("bundletool") != Orkige::String::npos);
	REQUIRE(empty[1].find("keystore") != Orkige::String::npos);

	// a keystore with neither alias nor password names both gaps, so one run
	// tells the owner everything that is still missing
	AndroidKeystore partial;
	partial.keystore = "/secrets/release.jks";
	const std::vector<Orkige::String> gaps =
		androidSigningGaps(partial, "/tools/bundletool.jar");
	REQUIRE(gaps.size() == 2);
	REQUIRE(gaps[0].find("key alias") != Orkige::String::npos);
	REQUIRE(gaps[1].find("password") != Orkige::String::npos);

	AndroidKeystore complete;
	complete.keystore = "/secrets/release.jks";
	complete.alias = "upload";
	complete.hasStorePassword = true;
	REQUIRE(androidSigningGaps(complete, "/tools/bundletool.jar").empty());
}

TEST_CASE("the newest installed SDK pieces are the ones taken",
	"[exporter][android]")
{
	// a person's SDK holds whatever their SDK manager gave them, so the
	// packaging takes the newest of each rather than a pinned pair - compared
	// by NUMERIC component, which a filename sort gets backwards
	const std::vector<Orkige::String> versions = { "9.0.0", "35.0.0", "34.0.1",
		"30.0.3" };
	REQUIRE(newestAndroidBuildTools(versions) == "35.0.0");
	REQUIRE(newestAndroidBuildTools({ "35.0.0", "35.0.1" }) == "35.0.1");
	// a preview build-tools name still carries a comparable numeric prefix
	REQUIRE(newestAndroidBuildTools({ "34.0.0", "35.0.0-rc1" }) ==
		"35.0.0-rc1");
	REQUIRE(newestAndroidBuildTools({ "source.properties" }).empty());
	REQUIRE(newestAndroidBuildTools({}).empty());

	// a platform must clear the API the player's own dex is built for, and a
	// LETTER-named preview carries no stable number to compare - skipped
	// rather than guessed at
	const std::vector<Orkige::String> platforms = { "android-28", "android-35",
		"android-31", "android-VanillaIceCream" };
	REQUIRE(newestAndroidPlatform(platforms, androidMinimumApi()) ==
		"android-35");
	REQUIRE(newestAndroidPlatform({ "android-21", "android-26" },
		androidMinimumApi()).empty());
	REQUIRE(newestAndroidPlatform({ "sources" }, androidMinimumApi()).empty());
	REQUIRE(androidMinimumApi() == 28);
}

TEST_CASE("an Android SDK is FOUND rather than configured",
	"[exporter][android]")
{
	// The candidates a home directory produces are built with ExportFiles::join
	// and therefore carry the HOST's separator. What this case is about is
	// WHICH directories are offered and in what ORDER, so the comparisons run
	// on a separator-normalized form rather than on a POSIX-shaped literal -
	// otherwise the assertion is about the runner, not about the lookup.
	auto normalized = [](Orkige::String const & path)
	{
		Orkige::String result = path;
		std::replace(result.begin(), result.end(), '\\', '/');
		return result;
	};

	// the two variables people set, in order, then the place each platform's
	// own installer puts one - so somebody who installed the tools and
	// configured nothing is still found
	EnvironmentMap environment;
	environment["HOME"] = "/home/dev";
	environment["ANDROID_SDK_ROOT"] = "/opt/android";
	environment["ANDROID_HOME"] = "/srv/sdk";
	const std::vector<Orkige::String> candidates =
		androidSdkCandidates(environment);
	REQUIRE(candidates.size() >= 3);
	// a variable is taken VERBATIM - it names a directory, and the person who
	// exported it wrote the separators they meant
	REQUIRE(candidates[0] == "/srv/sdk");
	REQUIRE(candidates[1] == "/opt/android");
	// ...and after them, at least one place under the home directory itself
	size_t homeDerived = 0;
	for(size_t index = 2; index < candidates.size(); ++index)
	{
		if(normalized(candidates[index]).rfind("/home/dev/", 0) == 0)
		{
			++homeDerived;
		}
	}
	REQUIRE(homeDerived >= 1);

	// the Windows installer's own location, which is where a machine with no
	// variable set and no HOME keeps one
	EnvironmentMap windowsLike;
	windowsLike["LOCALAPPDATA"] = "C:/Users/dev/AppData/Local";
	const std::vector<Orkige::String> local =
		androidSdkCandidates(windowsLike);
	REQUIRE(local.size() == 1);
	REQUIRE(normalized(local[0]) == "C:/Users/dev/AppData/Local/Android/Sdk");

	// whitespace reads as absent, the same rule the signing resolvers use
	EnvironmentMap blank;
	blank["ANDROID_HOME"] = "   ";
	const std::vector<Orkige::String> none = androidSdkCandidates(blank);
	for (Orkige::String const& candidate : none)
	{
		REQUIRE(candidate != "   ");
	}
}

TEST_CASE("a missing Android tool is named, one program at a time",
	"[exporter][android]")
{
	// THE POINT of this gate: a person who has the SDK and no JDK reads about
	// the JDK. One lumped "install the Android SDK" would be unactionable for
	// four of the five things that can be missing here.
	AndroidToolchain nothing;
	const std::vector<Orkige::String> bare = androidToolchainGaps(nothing);
	REQUIRE(bare.size() == 2);		// the SDK itself, and the JDK
	REQUIRE(bare[0].find("Android SDK") != Orkige::String::npos);
	REQUIRE(bare[0].find(ANDROID_HOME_ENV) != Orkige::String::npos);
	REQUIRE(bare[1].find("JDK") != Orkige::String::npos);

	// an SDK with a JDK beside it, but no build tools unpacked: FOUR named
	// programs plus the platform, each with the command that installs it
	AndroidToolchain sdkOnly;
	sdkOnly.sdkRoot = "/srv/sdk";
	sdkOnly.javaHome = "/opt/jdk";
	sdkOnly.jdk = true;
	const std::vector<Orkige::String> gaps = androidToolchainGaps(sdkOnly);
	REQUIRE(gaps.size() == 5);
	for (Orkige::String const& gap : gaps)
	{
		INFO(gap);
		REQUIRE(gap.find("sdkmanager") != Orkige::String::npos);
	}
	REQUIRE(gaps[0].find("aapt2") == 0);
	REQUIRE(gaps[1].find("zipalign") == 0);
	REQUIRE(gaps[2].find("apksigner") == 0);
	REQUIRE(gaps[3].find("d8") == 0);
	REQUIRE(gaps[4].find("platform") != Orkige::String::npos);

	// ONE missing piece is ONE sentence - the case that would be lost by
	// reporting the SDK as a single yes/no
	AndroidToolchain jdkMissing;
	jdkMissing.sdkRoot = "/srv/sdk";
	jdkMissing.buildTools = "/srv/sdk/build-tools/35.0.0";
	jdkMissing.platformJar = "/srv/sdk/platforms/android-35/android.jar";
	jdkMissing.aapt2 = jdkMissing.zipalign = true;
	jdkMissing.apksigner = jdkMissing.d8 = true;
	const std::vector<Orkige::String> onlyJdk =
		androidToolchainGaps(jdkMissing);
	REQUIRE(onlyJdk.size() == 1);
	REQUIRE(onlyJdk[0].find("JDK") != Orkige::String::npos);
	REQUIRE_FALSE(jdkMissing.complete());

	AndroidToolchain ready = jdkMissing;
	ready.jdk = true;
	ready.javaHome = "/opt/jdk";
	REQUIRE(ready.complete());
	REQUIRE(androidToolchainGaps(ready).empty());
	REQUIRE(androidToolchainRefusal(ready).empty());

	// the refusal a person actually reads: every gap on its own line, and the
	// engine/toolchain split said out loud. It must NEVER offer a download
	// (nothing here is fetchable) and must NEVER mention an engine SDK pack -
	// that tier belongs to compiled C++ game code alone, and a Lua game must
	// not be sent looking for one.
	const Orkige::String refusal = androidToolchainRefusal(jdkMissing);
	REQUIRE(refusal.find("Android SDK's own tools") != Orkige::String::npos);
	REQUIRE(refusal.find("\n  - ") != Orkige::String::npos);
	REQUIRE(refusal.find("/srv/sdk") != Orkige::String::npos);
	REQUIRE(refusal.find("Build Targets") == Orkige::String::npos);
	REQUIRE(refusal.find("SDK pack") == Orkige::String::npos);
}

TEST_CASE("the APK command line names its engine source and its tools",
	"[exporter][android]")
{
	// the fetched-player shape: `--payload` instead of the positional build
	// tree, so no build tree is named at all - there is none on the machine
	AndroidToolchain tools;
	tools.buildTools = "/srv/sdk/build-tools/35.0.0";
	tools.javaHome = "/opt/jdk";
	const std::vector<Orkige::String> command = androidApkArguments(
		"/payloads/player-android/android/package_apk.sh", "/out/payload",
		"com.orkitec.jumperlua", "Jumper Lua", "/out/res", "#101014", "stored",
		"auto", "/out/JumperLua.apk", "", "/payloads/player-android", tools);
	const std::vector<Orkige::String>::const_iterator payload =
		std::find(command.begin(), command.end(), "--payload");
	REQUIRE(payload != command.end());
	REQUIRE(*(payload + 1) == "/payloads/player-android");
	// the resolved toolchain is handed DOWN, so the programs the export
	// checked for are exactly the ones that run
	REQUIRE(std::find(command.begin(), command.end(), "--build-tools") !=
		command.end());
	REQUIRE(std::find(command.begin(), command.end(), tools.javaHome) !=
		command.end());
	// and nothing empty is ever passed as a positional build directory
	for (Orkige::String const& argument : command)
	{
		REQUIRE_FALSE(argument.empty());
	}
}
