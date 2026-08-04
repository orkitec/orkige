/********************************************************************
	created:	Sunday 2026/08/03 at 10:00
	filename: 	ExportAndroidAssembleTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! @file ExportAndroidAssembleTests.cpp
//! @brief the Android package assembly, asserted without an Android SDK.
//!
//! Packaging for Android is a fixed choreography over the SDK's own programs,
//! and the whole command set one run spawns is decided up front by a PURE
//! planner - which is what lets every argv, and the properties that hold
//! ACROSS them, be a hard gate on every host, including one that holds no SDK,
//! no JDK and no keystore.
//!
//! The property this file exists for is the first one: **nothing is handed to
//! a command interpreter**. A host that ships no shell (and one whose shell is
//! a different operating system's, reached through a compatibility layer that
//! would receive native paths) must package a game exactly like every other,
//! so `argv[0]` is always a program the toolchain resolution named. That is
//! easy to regress and invisible when it regresses, because the machines a
//! change is written on all happen to have a shell.

#include "ExportAndroid.h"
#include "ExportAndroidAssemble.h"
#include "ExportFiles.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace OrkigeExport;

namespace
{
	//! a fully populated layout: every step present, so a sweep over
	//! `plan.all()` sees the whole command set a run can produce
	AndroidAssemblyLayout makeLayout()
	{
		AndroidAssemblyLayout layout;
		layout.buildTools = "/srv/sdk/build-tools/35.0.0";
		layout.platformJar = "/srv/sdk/platforms/android-35/android.jar";
		layout.javaHome = "/opt/jdk";
		layout.strip = "/srv/ndk/bin/llvm-strip";
		layout.minimumApi = 28;
		layout.nativeLibrary = "/build/tools/player/libmain.so";
		layout.stagedLibrary = "/work/stage/lib/arm64-v8a/libmain.so";
		layout.javaSources = { "/glue/SDLActivity.java",
			"/repo/OrkigeActivity.java" };
		layout.classesDirectory = "/work/classes";
		layout.classListFile = "/work/classlist.txt";
		layout.dexDirectory = "/work/dex";
		layout.resDirectory = "/work/res";
		layout.compiledResources = "/work/res.zip";
		layout.manifestPath = "/work/AndroidManifest.xml";
		layout.linkedDirectory = "/work/linked";
		layout.unalignedPackage = "/work/unaligned.apk";
		layout.outputPath = "/out/JumperLua.apk";
		layout.debugKeystore = "/home/dev/.android/debug.keystore";
		return layout;
	}
	//---------------------------------------------------------
	//! the signed App Bundle shape - the only one that reaches bundletool and
	//! jarsigner
	AndroidAssemblyLayout makeBundleLayout()
	{
		AndroidAssemblyLayout layout = makeLayout();
		layout.bundle = true;
		layout.outputPath = "/out/JumperLua.aab";
		layout.moduleZip = "/work/base.zip";
		layout.bundlePath = "/work/app.aab";
		layout.bundleConfig = "/work/BundleConfig.json";
		layout.bundletool = "/tools/bundletool.jar";
		layout.releaseKeystore = "/secrets/release.jks";
		layout.releaseKeyAlias = "upload";
		layout.storePasswordEnv = "ORKIGE_ANDROID_KEYSTORE_PASS";
		layout.keyPasswordEnv = "ORKIGE_ANDROID_KEY_PASS";
		return layout;
	}
	//---------------------------------------------------------
	bool contains(std::vector<Orkige::String> const & arguments,
		Orkige::String const & value)
	{
		return std::find(arguments.begin(), arguments.end(), value) !=
			arguments.end();
	}
	//---------------------------------------------------------
	//! the host's own spelling of a path the PLANNER composes. Every path the
	//! plan builds itself (a program under a resolved toolchain root, a jar
	//! under build-tools) goes through ExportFiles::join, whose contract is
	//! ONE separator - the host's preferred one - so on Windows the POSIX
	//! fixture paths come back backslashed. Restated here independently (a
	//! plain character swap) rather than routed through the helper under
	//! test, so the expectation stays an expectation. Paths the layout hands
	//! over VERBATIM (an output path, a keystore, a module zip) are passed
	//! through untouched by the planner and stay POSIX in the fixtures.
	Orkige::String hostPath(Orkige::String posix)
	{
#if defined(_WIN32)
		std::replace(posix.begin(), posix.end(), '/', '\\');
#endif
		return posix;
	}
	//---------------------------------------------------------
	//! the host's own name for a toolchain program: `.exe` on Windows, the
	//! bare name elsewhere (the androidProgramPath contract, restated)
	Orkige::String hostProgram(Orkige::String const & posix)
	{
#if defined(_WIN32)
		return hostPath(posix + ".exe");
#else
		return posix;
#endif
	}
	//---------------------------------------------------------
	//! a step's program is a PATH (however the host spells its separator),
	//! never a bare name PATH-lookup would have to resolve
	bool namesAPath(Orkige::String const & program)
	{
		return program.find('/') != Orkige::String::npos ||
			program.find('\\') != Orkige::String::npos;
	}
}

//--- THE no-shell gate -------------------------------------------

TEST_CASE("no assembly step is ever handed to a command interpreter",
	"[exporter][android][assemble]")
{
	// A shell in the middle is what this whole assembly exists to remove: it
	// is a machine prerequisite the engine cannot ship, it is absent on
	// Windows, and where it resolves through a compatibility layer it is
	// another operating system's shell being handed this one's paths. So no
	// planned step may name one - not as the program, and not smuggled in as
	// `<something> -c`.
	std::vector<AndroidCommand> steps = androidAssemblyPlan(makeLayout()).all();
	const std::vector<AndroidCommand> bundleSteps =
		androidAssemblyPlan(makeBundleLayout()).all();
	steps.insert(steps.end(), bundleSteps.begin(), bundleSteps.end());
	steps.push_back(androidExtractJavaGlueCommand("/dl/sdl.tar.gz", "/work"));
	// both shapes together must cover every step the planner can emit
	REQUIRE(steps.size() >= 12);

	char const * const interpreters[] = { "bash", "sh", "zsh", "dash", "csh",
		"ksh", "fish", "cmd", "cmd.exe", "powershell", "powershell.exe",
		"pwsh", "python", "python3", "perl", "ruby", "env" };
	for (AndroidCommand const& step : steps)
	{
		INFO(step.label);
		REQUIRE_FALSE(step.arguments.empty());
		const Orkige::String program =
			ExportFiles::fileName(step.arguments[0]);
		for (char const* interpreter : interpreters)
		{
			INFO(program);
			CHECK(program != Orkige::String(interpreter));
		}
		// ...and no step smuggles a script in behind a shell's -c
		CHECK_FALSE(contains(step.arguments, "-c"));
		// nothing is ever passed as an empty argument either: an empty argv
		// entry is how a forgotten value silently becomes a positional one
		for (Orkige::String const& argument : step.arguments)
		{
			CHECK_FALSE(argument.empty());
		}
	}
}

TEST_CASE("every assembly step names an ABSOLUTE program",
	"[exporter][android][assemble]")
{
	// a bare name would be resolved through PATH, which is a second answer to
	// the question the toolchain probe already answered - and a different one
	// on a machine with two SDKs. The one exception is the archive unpacker
	// for a build tree whose vcpkg sources were cleaned; the assembler
	// resolves that against PATH itself and refuses by name when it is absent.
	for (AndroidCommand const& step : androidAssemblyPlan(makeLayout()).all())
	{
		INFO(step.label);
		CHECK(namesAPath(step.arguments[0]));
	}
	for (AndroidCommand const& step :
		androidAssemblyPlan(makeBundleLayout()).all())
	{
		INFO(step.label);
		CHECK(namesAPath(step.arguments[0]));
	}
}

//--- the individual argv -----------------------------------------

TEST_CASE("the APK plan is the SDK choreography, in order",
	"[exporter][android][assemble]")
{
	const AndroidAssemblyPlan plan = androidAssemblyPlan(makeLayout());

	CHECK(plan.strip.arguments == std::vector<Orkige::String>{
		"/srv/ndk/bin/llvm-strip", "--strip-unneeded", "-o",
		"/work/stage/lib/arm64-v8a/libmain.so",
		"/build/tools/player/libmain.so" });

	// -source/-target 8 + -bootclasspath is the only combination javac still
	// accepts a custom bootclasspath for, which is what keeps java.* resolving
	// against android.jar instead of the host JDK
	CHECK(plan.compileJava.arguments == std::vector<Orkige::String>{
		hostProgram("/opt/jdk/bin/javac"), "-source", "8", "-target", "8",
		"-encoding", "UTF-8",
		"-bootclasspath", "/srv/sdk/platforms/android-35/android.jar",
		"-d", "/work/classes", "-nowarn",
		"/glue/SDLActivity.java", "/repo/OrkigeActivity.java" });

	CHECK(plan.dex.arguments == std::vector<Orkige::String>{
		hostProgram("/opt/jdk/bin/java"), "-cp",
		hostPath("/srv/sdk/build-tools/35.0.0/lib/d8.jar"),
		"com.android.tools.r8.D8", "--release", "--min-api", "28",
		"--lib", "/srv/sdk/platforms/android-35/android.jar",
		"--output", "/work/dex", "@/work/classlist.txt" });

	CHECK(plan.compileResources.arguments == std::vector<Orkige::String>{
		hostProgram("/srv/sdk/build-tools/35.0.0/aapt2"), "compile",
		"--dir", "/work/res", "-o", "/work/res.zip" });

	// the linked output goes to a DIRECTORY: the package is assembled here, so
	// every entry's compression is ours to decide (a mounted APK reads its
	// assets in place, which a deflated entry cannot offer)
	CHECK(plan.linkResources.arguments == std::vector<Orkige::String>{
		hostProgram("/srv/sdk/build-tools/35.0.0/aapt2"), "link",
		"--manifest", "/work/AndroidManifest.xml",
		"-I", "/srv/sdk/platforms/android-35/android.jar",
		"-o", "/work/linked", "--output-to-dir", "/work/res.zip" });
	CHECK_FALSE(contains(plan.linkResources.arguments, "--proto-format"));

	CHECK(plan.align.arguments == std::vector<Orkige::String>{
		hostProgram("/srv/sdk/build-tools/35.0.0/zipalign"), "-f", "4",
		"/work/unaligned.apk", "/out/JumperLua.apk" });

	CHECK(plan.sign.arguments == std::vector<Orkige::String>{
		hostProgram("/opt/jdk/bin/java"), "-jar",
		hostPath("/srv/sdk/build-tools/35.0.0/lib/apksigner.jar"), "sign",
		"--ks", "/home/dev/.android/debug.keystore",
		"--ks-pass", "pass:android", "--key-pass", "pass:android",
		"/out/JumperLua.apk" });

	// the shared Android debug key, created on demand: Android installs no
	// unsigned package, so signing was never the optional part
	CHECK(plan.createDebugKey.arguments[0] ==
		hostProgram("/opt/jdk/bin/keytool"));
	CHECK(contains(plan.createDebugKey.arguments,
		"/home/dev/.android/debug.keystore"));
	CHECK(contains(plan.createDebugKey.arguments, "androiddebugkey"));

	// an APK never reaches the bundle tier
	CHECK(plan.buildBundle.empty());
	CHECK(plan.signBundle.empty());
	CHECK(plan.verifyBundle.empty());
}

TEST_CASE("a payload's library is already stripped, so no strip runs",
	"[exporter][android][assemble]")
{
	// a payload's library was stripped when the payload was composed, on the
	// machine that had the NDK: a client has no strip tool for that target,
	// and downloading hundreds of MB of DWARF is not a thing to do
	AndroidAssemblyLayout layout = makeLayout();
	layout.strip = "";
	const AndroidAssemblyPlan plan = androidAssemblyPlan(layout);
	CHECK(plan.strip.empty());
	for (AndroidCommand const& step : plan.all())
	{
		CHECK(step.arguments[0].find("strip") == Orkige::String::npos);
	}
}

TEST_CASE("the App Bundle links in protobuf and signs with the release key",
	"[exporter][android][assemble]")
{
	const AndroidAssemblyPlan plan = androidAssemblyPlan(makeBundleLayout());

	// bundletool consumes protobuf-encoded resources, not binary AXML
	CHECK(contains(plan.linkResources.arguments, "--proto-format"));
	// ...and an App Bundle is never zipaligned or debug-signed: those belong
	// to an APK, and Play generates the device APKs itself
	CHECK(plan.align.empty());
	CHECK(plan.sign.empty());
	CHECK(plan.createDebugKey.empty());

	CHECK(plan.buildBundle.arguments == std::vector<Orkige::String>{
		hostProgram("/opt/jdk/bin/java"), "-jar", "/tools/bundletool.jar",
		"build-bundle",
		"--modules=/work/base.zip", "--output=/work/app.aab",
		"--config=/work/BundleConfig.json" });

	// the passwords travel as environment variable NAMES: jarsigner reads them
	// itself, so no secret ever reaches a command line (which every process
	// listing on the machine can read)
	CHECK(plan.signBundle.arguments == std::vector<Orkige::String>{
		hostProgram("/opt/jdk/bin/jarsigner"), "-sigalg", "SHA256withRSA",
		"-digestalg", "SHA-256", "-keystore", "/secrets/release.jks",
		"-storepass:env", "ORKIGE_ANDROID_KEYSTORE_PASS",
		"-keypass:env", "ORKIGE_ANDROID_KEY_PASS",
		"/work/app.aab", "upload" });
	for (Orkige::String const& argument : plan.signBundle.arguments)
	{
		CHECK(argument.find("hunter2") == Orkige::String::npos);
	}
	CHECK(plan.verifyBundle.arguments == std::vector<Orkige::String>{
		hostProgram("/opt/jdk/bin/jarsigner"), "-verify", "/work/app.aab" });
}

TEST_CASE("the unsigned bundle module reaches neither bundletool nor a key",
	"[exporter][android][assemble]")
{
	// the structural slice a test can always build: it stops at the module
	// zip, which needs no download and no secret
	AndroidAssemblyLayout layout = makeBundleLayout();
	layout.moduleOnly = true;
	const AndroidAssemblyPlan plan = androidAssemblyPlan(layout);
	CHECK(plan.buildBundle.empty());
	CHECK(plan.signBundle.empty());
	CHECK(plan.verifyBundle.empty());
	for (AndroidCommand const& step : plan.all())
	{
		CHECK_FALSE(contains(step.arguments, "/secrets/release.jks"));
		CHECK_FALSE(contains(step.arguments, "/tools/bundletool.jar"));
	}
}

TEST_CASE("a bundletool run without a config passes none",
	"[exporter][android][assemble]")
{
	// `compressed` mode leaves bundletool its own defaults, so the flag is
	// absent rather than empty - an empty --config= is a file it cannot open
	AndroidAssemblyLayout layout = makeBundleLayout();
	layout.bundleConfig = "";
	const AndroidAssemblyPlan plan = androidAssemblyPlan(layout);
	CHECK(plan.buildBundle.arguments.back() == "--output=/work/app.aab");
}

TEST_CASE("the SDL glue unpacker is a program, not a shell pipeline",
	"[exporter][android][assemble]")
{
	const AndroidCommand command =
		androidExtractJavaGlueCommand("/dl/sdl.tar.gz", "/work/src");
	CHECK(command.arguments == std::vector<Orkige::String>{
		"tar", "-xzf", "/dl/sdl.tar.gz", "-C", "/work/src",
		"--strip-components=1",
		"*/android-project/app/src/main/java/org/libsdl/app" });
	// the glob is tar's OWN pattern argument - it is never expanded by
	// anything before tar sees it, which is exactly why no shell is involved
	CHECK(command.arguments.back().find('*') != Orkige::String::npos);
}

//--- the generated text ------------------------------------------

TEST_CASE("the packaged manifest substitutes only what the export decided",
	"[exporter][android][assemble]")
{
	const Orkige::String templateText =
		"<manifest package=\"com.orkitec.orkigeplayer\"\n"
		"    android:versionCode=\"1\" android:versionName=\"2.0.0-dev\">\n"
		"  <uses-sdk android:minSdkVersion=\"28\" "
		"android:targetSdkVersion=\"35\" />\n"
		"  <application android:label=\"Orkige Player\"\n"
		"      android:debuggable=\"true\"\n"
		"      android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\">\n"
		"    <activity android:configChanges=\"orientation\" />\n"
		"  </application>\n"
		"</manifest>\n";

	// an EMPTY edit leaves the template byte-identical: a bare player run
	// packages the checked-in manifest and nothing else
	CHECK(androidManifestText(templateText, AndroidManifestEdits()) ==
		templateText);

	AndroidManifestEdits edits;
	edits.package = "com.studio.game";
	edits.label = "Studio Game";
	const Orkige::String renamed = androidManifestText(templateText, edits);
	CHECK(renamed.find("package=\"com.studio.game\"") !=
		Orkige::String::npos);
	CHECK(renamed.find("android:label=\"Studio Game\"") !=
		Orkige::String::npos);
	// the activity is named FULLY QUALIFIED in the template on purpose, so a
	// renamed package never has to touch the component reference
	CHECK(renamed.find("com.orkitec.orkigeplayer") == Orkige::String::npos);
	// nothing else moved: still debuggable, still the framework theme
	CHECK(renamed.find("android:debuggable=\"true\"") != Orkige::String::npos);
	CHECK(renamed.find("Theme.NoTitleBar.Fullscreen") != Orkige::String::npos);
}

TEST_CASE("a locked orientation is injected, an unconstrained one is not",
	"[exporter][android][assemble]")
{
	const Orkige::String templateText =
		"<activity android:configChanges=\"orientation\" />\n";
	AndroidManifestEdits edits;
	CHECK(androidManifestText(templateText, edits) == templateText);

	edits.screenOrientation = "portrait";
	const Orkige::String locked = androidManifestText(templateText, edits);
	CHECK(locked.find("android:screenOrientation=\"portrait\"") !=
		Orkige::String::npos);
	CHECK(locked.find("android:configChanges=\"orientation\"") !=
		Orkige::String::npos);
}

TEST_CASE("a launcher res tree swaps the framework theme for the launch one",
	"[exporter][android][assemble]")
{
	const Orkige::String templateText =
		"  <application\n"
		"      android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\">\n";
	AndroidManifestEdits edits;
	edits.launcherResources = true;
	const Orkige::String themed = androidManifestText(templateText, edits);
	CHECK(themed.find("android:icon=\"@mipmap/ic_launcher\"") !=
		Orkige::String::npos);
	CHECK(themed.find("android:theme=\"@style/OrkigeLaunch\"") !=
		Orkige::String::npos);
	// the icon and the theme end up as two separate attributes, not one run-on
	CHECK(themed.find("\"\n        android:theme") != Orkige::String::npos);
	CHECK(themed.find("Theme.NoTitleBar.Fullscreen") == Orkige::String::npos);
}

TEST_CASE("a release bundle stamps the version and is not debuggable",
	"[exporter][android][assemble]")
{
	const Orkige::String templateText =
		"<manifest android:versionCode=\"1\" "
		"android:versionName=\"2.0.0-dev\">\n"
		"  <application android:debuggable=\"true\" />\n"
		"</manifest>\n";
	AndroidManifestEdits edits;
	edits.release = true;
	edits.versionCode = 7;
	edits.versionName = "1.2.3";
	const Orkige::String release = androidManifestText(templateText, edits);
	CHECK(release.find("android:debuggable=\"false\"") !=
		Orkige::String::npos);
	CHECK(release.find("android:versionCode=\"7\"") != Orkige::String::npos);
	CHECK(release.find("android:versionName=\"1.2.3\"") !=
		Orkige::String::npos);
	CHECK(release.find("2.0.0-dev") == Orkige::String::npos);
}

TEST_CASE("Play's target-SDK floor is read off the manifest that ships",
	"[exporter][android][assemble]")
{
	CHECK(androidManifestTargetSdk(
		"<uses-sdk android:minSdkVersion=\"28\" "
		"android:targetSdkVersion=\"35\" />") == 35);
	CHECK(androidManifestTargetSdk("<uses-sdk />") == 0);
	// a build below the floor still packages - Play is the one that rejects
	// it, so the warning is loud and the artifact is real
	CHECK(androidPlayTargetSdkFloor() == 35);
}

TEST_CASE("the launch colour is validated before it reaches a resource",
	"[exporter][android][assemble]")
{
	CHECK(isAndroidLaunchColour("#12161f"));
	CHECK(isAndroidLaunchColour("#ABCDEF"));
	CHECK_FALSE(isAndroidLaunchColour("12161f"));
	CHECK_FALSE(isAndroidLaunchColour("#12161"));
	CHECK_FALSE(isAndroidLaunchColour("#12161g"));
	CHECK_FALSE(isAndroidLaunchColour(""));

	const Orkige::String colours = androidLaunchColoursXml("#12161f");
	CHECK(colours.find("<color name=\"launch_bg\">#12161f</color>") !=
		Orkige::String::npos);
	CHECK(androidLaunchStylesXml().find("name=\"OrkigeLaunch\"") !=
		Orkige::String::npos);
	CHECK(androidLaunchStylesXml().find("android:windowBackground") !=
		Orkige::String::npos);
}

TEST_CASE("the stored-mode bundle config keeps the assets uncompressed",
	"[exporter][android][assemble]")
{
	// bundletool otherwise re-compresses per its own defaults, and a deflated
	// zip entry is not randomly seekable - which is what streaming a track out
	// of the installed package needs
	const Orkige::String config = androidBundleConfigJson();
	CHECK(config.find("uncompressedGlob") != Orkige::String::npos);
	CHECK(config.find("assets/**") != Orkige::String::npos);
}

TEST_CASE("the extraction manifest is one bundled file per line",
	"[exporter][android][assemble]")
{
	CHECK(androidAssetManifest({}) == "");
	CHECK(androidAssetManifest({ "orkige_project.txt",
		"project/project.orkproj" }) ==
		"orkige_project.txt\nproject/project.orkproj\n");
}

//--- the toolchain a plan is laid out over -----------------------

TEST_CASE("a program is named the way this host names one",
	"[exporter][android][assemble]")
{
	const Orkige::String aapt2 = androidProgramPath("/srv/bt", "aapt2");
	CHECK(aapt2.find("aapt2") != Orkige::String::npos);
#if defined(_WIN32)
	CHECK(aapt2.find(".exe") != Orkige::String::npos);
#else
	CHECK(aapt2 == "/srv/bt/aapt2");
#endif
}

TEST_CASE("the vcpkg root a build tree's Java glue comes from",
	"[exporter][android][assemble]")
{
	// a fetched device payload carries the glue outright and never consults
	// this - a distributed editor has no vcpkg at all
	EnvironmentMap named;
	named["VCPKG_ROOT"] = "/opt/vcpkg";
	named["HOME"] = "/home/dev";
	CHECK(androidVcpkgRoot(named) == "/opt/vcpkg");

	EnvironmentMap homeOnly;
	homeOnly["HOME"] = "/home/dev";
	CHECK(androidVcpkgRoot(homeOnly) ==
		ExportFiles::join("/home/dev", "Development/vcpkg"));

	CHECK(androidVcpkgRoot(EnvironmentMap()).empty());
}
