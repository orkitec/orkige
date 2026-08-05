/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportSettingsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The manifest-Setting decisions an export makes, asserted without a
	filesystem, a certificate or a device:
	- the look settings (launch background, orientation) and their degrade
	  targets, plus the exact platform vocabularies they map onto;
	- the Android release gates (version code, assets mode, package name);
	- the machine-local credential resolvers, whose arg-over-env precedence is
	  what stands between a signed artifact and a half-signed one.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportSettings.h"

using namespace OrkigeExport;

TEST_CASE("launch background validates and degrades", "[unit][export]")
{
	SettingMap settings;
	settings["export.launch.background"] = "#a1b2c3";
	CHECK(launchBackground(settings) == "#a1b2c3");

	// a malformed colour degrades to the engine default AND says so - it is a
	// typo in a manifest, not a reason to refuse the export
	settings["export.launch.background"] = "blue";
	Orkige::String warning;
	CHECK(launchBackground(settings, &warning) == DEFAULT_LAUNCH_BACKGROUND);
	CHECK_FALSE(warning.empty());

	// absent degrades silently: nothing was asked for
	warning.clear();
	CHECK(launchBackground(SettingMap(), &warning) ==
		DEFAULT_LAUNCH_BACKGROUND);
	CHECK(warning.empty());
}

TEST_CASE("orientation defaults to portrait", "[unit][export]")
{
	// portrait is the default AND the degrade target: a mobile game is
	// portrait unless it says otherwise, which also keeps the boot
	// orientation deterministic
	CHECK(orientationSetting(SettingMap()) == "portrait");

	SettingMap settings;
	settings["export.orientation"] = "auto";
	CHECK(orientationSetting(settings) == "auto");
	settings["export.orientation"] = "Portrait";
	CHECK(orientationSetting(settings) == "portrait");	// case-insensitive
	settings["export.orientation"] = " landscape ";
	CHECK(orientationSetting(settings) == "landscape");	// trimmed

	settings["export.orientation"] = "sideways";
	Orkige::String warning;
	CHECK(orientationSetting(settings, &warning) == "portrait");
	CHECK_FALSE(warning.empty());
}

TEST_CASE("orientation maps to the platform vocabularies", "[unit][export]")
{
	CHECK(iosOrientations("portrait") ==
		std::vector<Orkige::String>{ "UIInterfaceOrientationPortrait" });
	CHECK(iosOrientations("landscape") == std::vector<Orkige::String>{
		"UIInterfaceOrientationLandscapeLeft",
		"UIInterfaceOrientationLandscapeRight" });
	CHECK(iosOrientations("auto").size() == 3);

	CHECK(androidScreenOrientation("portrait") == "sensorPortrait");
	CHECK(androidScreenOrientation("landscape") == "sensorLandscape");
	CHECK(androidScreenOrientation("auto") == "unspecified");
}

TEST_CASE("android version code must be a positive integer", "[unit][export]")
{
	int code = 0;
	Orkige::String name;
	SettingMap settings;
	settings["export.android.versionCode"] = "7";
	settings["export.android.versionName"] = "1.2.3";
	REQUIRE(androidVersion(settings, code, name, 0));
	CHECK(code == 7);
	CHECK(name == "1.2.3");

	// absent, and whitespace-only, default to 1 / "1.0"
	REQUIRE(androidVersion(SettingMap(), code, name, 0));
	CHECK(code == 1);
	CHECK(name == "1.0");
	SettingMap blank;
	blank["export.android.versionCode"] = "  ";
	REQUIRE(androidVersion(blank, code, name, 0));
	CHECK(code == 1);

	// anything that is not a positive integer refuses: Google Play needs a
	// strictly increasing integer, so a "1.0" here would break every upload
	for(const char * bad : { "0", "-1", "1.0", "v3", "abc" })
	{
		SettingMap broken;
		broken["export.android.versionCode"] = bad;
		Orkige::String error;
		CHECK_FALSE(androidVersion(broken, code, name, &error));
		CHECK_FALSE(error.empty());
	}
}

TEST_CASE("android assets mode is stored by default", "[unit][export]")
{
	Orkige::String mode;
	REQUIRE(androidAssetsMode(SettingMap(), mode, 0));
	CHECK(mode == "stored");

	SettingMap blank;
	blank["export.android.assets"] = "  ";
	REQUIRE(androidAssetsMode(blank, mode, 0));
	CHECK(mode == "stored");

	SettingMap compressed;
	compressed["export.android.assets"] = "compressed";
	REQUIRE(androidAssetsMode(compressed, mode, 0));
	CHECK(mode == "compressed");

	for(const char * bad : { "zip", "none", "deflate", "STORED" })
	{
		SettingMap broken;
		broken["export.android.assets"] = bad;
		Orkige::String error;
		CHECK_FALSE(androidAssetsMode(broken, mode, &error));
		CHECK_FALSE(error.empty());
	}
}

TEST_CASE("android package names are validated", "[unit][export]")
{
	CHECK(isValidAndroidPackage("com.orkitec.game"));
	CHECK(isValidAndroidPackage("com.orkitec"));
	CHECK(isValidAndroidPackage("_a.b9_c"));
	CHECK_FALSE(isValidAndroidPackage("game"));			// needs two parts
	CHECK_FALSE(isValidAndroidPackage("com..game"));		// empty part
	CHECK_FALSE(isValidAndroidPackage("com.9game"));		// leading digit
	CHECK_FALSE(isValidAndroidPackage("com.my-game"));	// dash
	CHECK_FALSE(isValidAndroidPackage(""));
	CHECK_FALSE(isValidAndroidPackage("com.game."));		// trailing dot
}

TEST_CASE("ios signing resolves arg over env", "[unit][export]")
{
	// the CLI argument wins
	SigningPair pair = resolveIosSigning("cli-id", "cli.mobileprovision",
		EnvironmentMap());
	CHECK(pair.identity == "cli-id");
	CHECK(pair.profile == "cli.mobileprovision");

	EnvironmentMap environment;
	environment[IOS_SIGNING_IDENTITY_ENV] = "env-id";
	environment[IOS_PROVISIONING_PROFILE_ENV] = "env.mobileprovision";
	pair = resolveIosSigning("", "", environment);
	CHECK(pair.identity == "env-id");
	CHECK(pair.profile == "env.mobileprovision");

	// a mixed pair resolves each side independently
	pair = resolveIosSigning("cli-id", "", environment);
	CHECK(pair.identity == "cli-id");
	CHECK(pair.profile == "env.mobileprovision");

	// unresolved stays blank, which is what the export gate refuses on
	pair = resolveIosSigning("", "", EnvironmentMap());
	CHECK(pair.identity.empty());
	CHECK(pair.profile.empty());

	// whitespace-only reads as absent on BOTH sides: a blank identity must
	// refuse, never reach a codesign command line
	pair = resolveIosSigning("   ", "\t", EnvironmentMap());
	CHECK(pair.identity.empty());
	CHECK(pair.profile.empty());
	EnvironmentMap blanks;
	blanks[IOS_SIGNING_IDENTITY_ENV] = "  ";
	blanks[IOS_PROVISIONING_PROFILE_ENV] = "  ";
	pair = resolveIosSigning("", "", blanks);
	CHECK(pair.identity.empty());
	CHECK(pair.profile.empty());
}

TEST_CASE("distribution signing has its own environment", "[unit][export]")
{
	EnvironmentMap environment;
	environment[IOS_DISTRIBUTION_IDENTITY_ENV] = "dist-id";
	environment[IOS_DISTRIBUTION_PROFILE_ENV] = "AppStore.mobileprovision";
	SigningPair pair =
		resolveIosDistributionSigning("", "", environment);
	CHECK(pair.identity == "dist-id");
	CHECK(pair.profile == "AppStore.mobileprovision");

	CHECK(resolveIosDistributionSigning("cli", "", EnvironmentMap()).identity
		== "cli");

	// a DEVELOPMENT identity must never satisfy a distribution build - the
	// two certificates are different, and the App Store rejects the wrong one
	EnvironmentMap development;
	development[IOS_SIGNING_IDENTITY_ENV] = "dev-id";
	pair = resolveIosDistributionSigning("", "", development);
	CHECK(pair.identity.empty());
	CHECK(pair.profile.empty());
}

TEST_CASE("android keystore resolves, passwords stay in the env",
	"[unit][export]")
{
	EnvironmentMap environment;
	environment[ANDROID_KEYSTORE_ENV] = "env.jks";
	environment[ANDROID_KEY_ALIAS_ENV] = "envAlias";
	environment[ANDROID_KEYSTORE_PASS_ENV] = "secret";

	AndroidKeystore resolved =
		resolveAndroidKeystore("cli.jks", "cliAlias", environment);
	CHECK(resolved.keystore == "cli.jks");
	CHECK(resolved.alias == "cliAlias");
	// the password is only ever REPORTED as present: it reaches jarsigner
	// through the environment, so no secret lands on a command line
	CHECK(resolved.hasStorePassword);

	EnvironmentMap noPassword;
	noPassword[ANDROID_KEYSTORE_ENV] = "env.jks";
	noPassword[ANDROID_KEY_ALIAS_ENV] = "envAlias";
	resolved = resolveAndroidKeystore("", "", noPassword);
	CHECK(resolved.keystore == "env.jks");
	CHECK(resolved.alias == "envAlias");
	CHECK_FALSE(resolved.hasStorePassword);

	resolved = resolveAndroidKeystore("", "", EnvironmentMap());
	CHECK(resolved.keystore.empty());
	CHECK(resolved.alias.empty());
	CHECK_FALSE(resolved.hasStorePassword);
}

namespace
{
	Orkige::String noSuchTool(Orkige::String const &) { return ""; }
	Orkige::String toolOnPath(Orkige::String const &)
	{
		return "/opt/bin/bundletool";
	}
}

TEST_CASE("bundletool resolves arg over env over PATH", "[unit][export]")
{
	CHECK(resolveBundletool("cli.jar", EnvironmentMap(), noSuchTool) ==
		"cli.jar");

	EnvironmentMap environment;
	environment[BUNDLETOOL_ENV] = "env.jar";
	CHECK(resolveBundletool("", environment, noSuchTool) == "env.jar");

	CHECK(resolveBundletool("", EnvironmentMap(), toolOnPath) ==
		"/opt/bin/bundletool");
	CHECK(resolveBundletool("", EnvironmentMap(), noSuchTool).empty());
}

TEST_CASE("the cook platform token per export platform", "[unit][export]")
{
	// every desktop reads the sidecar's default block - there is no
	// desktop-specific override slot, because a desktop GPU wants none
	CHECK(cookPlatformToken("macos") == "");
	CHECK(cookPlatformToken("linux") == "");
	CHECK(cookPlatformToken("windows") == "");
	CHECK(cookPlatformToken("ios-simulator") == "ios");
	CHECK(cookPlatformToken("ios") == "ios");
	CHECK(cookPlatformToken("ios-ipa") == "ios");
	CHECK(cookPlatformToken("android") == "android");
	CHECK(cookPlatformToken("android-aab") == "android");
	CHECK(cookPlatformToken("web") == "web");
}

TEST_CASE("human size reports the artifact weight", "[unit][export]")
{
	CHECK(humanSize(0) == "0 B");
	CHECK(humanSize(512) == "512 B");
	CHECK(humanSize(1024) == "1.0 KiB");
	CHECK(humanSize(1536) == "1.5 KiB");
	CHECK(humanSize(1024ull * 1024ull) == "1.0 MiB");
	CHECK(humanSize(3ull * 1024ull * 1024ull * 1024ull) == "3.0 GiB");
	// beyond GiB the unit stops climbing rather than inventing one
	CHECK(humanSize(4096ull * 1024ull * 1024ull * 1024ull) == "4096.0 GiB");
}
