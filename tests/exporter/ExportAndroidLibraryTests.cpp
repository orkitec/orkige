/********************************************************************
	created:	Tuesday 2026/08/04 at 10:00
	filename: 	ExportAndroidLibraryTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! @file ExportAndroidLibraryTests.cpp
//! @brief consuming an Android library archive, asserted without an SDK.
//!
//! Two properties carry this whole tier, and both are here because both fail
//! SILENTLY at build time and loudly on a player's phone:
//!
//!  - what a library DECLARES reaches the packaged manifest. A permission that
//!    is quietly dropped produces an app that installs, runs, and then throws
//!    a SecurityException the first time the library does its job.
//!  - what this merge does NOT implement is REFUSED BY NAME. The alternative
//!    is dropping it, which is the same failure with no message attached.
//!
//! Everything asserted here is pure text in and text out, so it is a hard gate
//! on every host - including one with no Android SDK, which is most of them.

#include "ExportAndroidLibrary.h"
#include "ExportSettings.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace OrkigeExport;

namespace
{
	//! the packaged app manifest a fragment merges into - the shape the
	//! checked-in template has once the export has substituted its identity
	Orkige::String appManifest()
	{
		return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
			"    package=\"com.studio.game\">\n"
			"  <uses-sdk android:minSdkVersion=\"28\" "
			"android:targetSdkVersion=\"35\" />\n"
			"  <uses-permission android:name=\"android.permission.INTERNET\" />\n"
			"  <application android:label=\"Studio Game\">\n"
			"    <activity android:name=\"com.orkitec.orkigeplayer.OrkigeActivity\" "
			"android:exported=\"true\" />\n"
			"  </application>\n"
			"</manifest>\n";
	}
	//---------------------------------------------------------
	//! one library manifest, named the way a refusal names it
	AndroidManifestFragment fragmentOf(Orkige::String const & text,
		Orkige::String const & source = "vendor-sdk.aar")
	{
		AndroidManifestFragment fragment;
		fragment.source = source;
		fragment.text = text;
		return fragment;
	}
	//---------------------------------------------------------
	//! merge @p fragments, requiring success and handing back the result
	Orkige::String merged(std::vector<AndroidManifestFragment> const & fragments,
		Orkige::String const & applicationId = "com.studio.game")
	{
		Orkige::String out;
		Orkige::String error;
		const bool ok = androidMergeManifest(appManifest(), fragments,
			applicationId, 28, out, 0, &error);
		INFO(error);
		REQUIRE(ok);
		return out;
	}
	//---------------------------------------------------------
	//! merge @p fragments, requiring a REFUSAL and handing back its message
	Orkige::String refusal(std::vector<AndroidManifestFragment> const & fragments)
	{
		Orkige::String out;
		Orkige::String error;
		REQUIRE_FALSE(androidMergeManifest(appManifest(), fragments,
			"com.studio.game", 28, out, 0, &error));
		REQUIRE_FALSE(error.empty());
		return error;
	}
	//---------------------------------------------------------
	bool holds(Orkige::String const & text, Orkige::String const & needle)
	{
		return text.find(needle) != Orkige::String::npos;
	}
}

//--- THE gate: a declaration reaches the package -----------------

TEST_CASE("a permission an Android library declares reaches the manifest",
	"[exporter][android][library]")
{
	// the one that matters most. A library asks for a permission because it
	// cannot work without it; an app that ships without the permission
	// installs, launches, and throws the first time the library runs. There is
	// no build-time symptom, so this is the assertion that has to exist.
	const Orkige::String out = merged({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
		"    package=\"com.vendor.sdk\">\n"
		"  <uses-permission android:name=\"android.permission.ACCESS_NETWORK_STATE\" />\n"
		"  <uses-permission android:name=\"com.google.android.gms.permission.AD_ID\" />\n"
		"</manifest>\n") });

	CHECK(holds(out, "android.permission.ACCESS_NETWORK_STATE"));
	CHECK(holds(out, "com.google.android.gms.permission.AD_ID"));
	// ...and the app's own declarations survive the merge unharmed
	CHECK(holds(out, "android.permission.INTERNET"));
	CHECK(holds(out, "package=\"com.studio.game\""));
	CHECK(holds(out, "com.orkitec.orkigeplayer.OrkigeActivity"));
}

TEST_CASE("an application component an Android library declares is packaged",
	"[exporter][android][library]")
{
	const Orkige::String out = merged({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
		"    package=\"com.vendor.sdk\">\n"
		"  <application>\n"
		"    <activity android:name=\"com.vendor.sdk.OverlayActivity\"\n"
		"        android:exported=\"false\" />\n"
		"    <service android:name=\"com.vendor.sdk.SyncService\" />\n"
		"    <receiver android:name=\"com.vendor.sdk.BootReceiver\" />\n"
		"    <provider android:name=\"com.vendor.sdk.Initializer\"\n"
		"        android:authorities=\"com.studio.game.vendorinit\" />\n"
		"    <meta-data android:name=\"com.vendor.sdk.VERSION\"\n"
		"        android:value=\"4\" />\n"
		"  </application>\n"
		"</manifest>\n") });

	CHECK(holds(out, "com.vendor.sdk.OverlayActivity"));
	CHECK(holds(out, "com.vendor.sdk.SyncService"));
	CHECK(holds(out, "com.vendor.sdk.BootReceiver"));
	CHECK(holds(out, "com.vendor.sdk.Initializer"));
	CHECK(holds(out, "com.vendor.sdk.VERSION"));
	// they land INSIDE <application>, not beside it - a component declared at
	// the manifest root is one the platform never sees
	const std::size_t application = out.find("<application");
	const std::size_t close = out.find("</application>");
	const std::size_t overlay = out.find("com.vendor.sdk.OverlayActivity");
	REQUIRE(application != Orkige::String::npos);
	REQUIRE(close != Orkige::String::npos);
	CHECK(overlay > application);
	CHECK(overlay < close);
}

TEST_CASE("package visibility queries survive the merge",
	"[exporter][android][library]")
{
	// on newer platforms an app sees only the packages it declares an interest
	// in, so a dropped <queries> entry is an intent that silently resolves to
	// nothing - a store or billing library that cannot reach its own host app
	const Orkige::String out = merged({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
		"    package=\"com.vendor.sdk\">\n"
		"  <queries>\n"
		"    <package android:name=\"com.vendor.host\" />\n"
		"    <intent>\n"
		"      <action android:name=\"com.vendor.BIND\" />\n"
		"    </intent>\n"
		"  </queries>\n"
		"</manifest>\n") });

	CHECK(holds(out, "<queries>"));
	CHECK(holds(out, "com.vendor.host"));
	CHECK(holds(out, "com.vendor.BIND"));
}

TEST_CASE("the application id placeholder resolves to the project's package",
	"[exporter][android][library]")
{
	// a content provider authority has to be unique per app, so a library
	// writes it against the app's own id. Leaving ${applicationId} in the
	// packaged manifest produces an authority no component can resolve.
	const Orkige::String out = merged({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
		"    package=\"com.vendor.sdk\">\n"
		"  <application>\n"
		"    <provider android:name=\"com.vendor.sdk.FileProvider\"\n"
		"        android:authorities=\"${applicationId}.vendorfiles\" />\n"
		"  </application>\n"
		"</manifest>\n") });

	CHECK(holds(out, "com.studio.game.vendorfiles"));
	CHECK_FALSE(holds(out, "${applicationId}"));
}

TEST_CASE("a feature two manifests disagree about ends up required",
	"[exporter][android][library]")
{
	// the platform's own rule. The other direction would let a device without
	// the feature install an app half of which cannot run.
	std::vector<AndroidManifestFragment> fragments;
	fragments.push_back(fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <uses-feature android:name=\"android.hardware.camera\"\n"
		"      android:required=\"false\" />\n"
		"</manifest>\n", "optional.aar"));
	fragments.push_back(fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <uses-feature android:name=\"android.hardware.camera\"\n"
		"      android:required=\"true\" />\n"
		"</manifest>\n", "required.aar"));
	const Orkige::String out = merged(fragments);

	CHECK(holds(out, "android.hardware.camera"));
	CHECK(holds(out, "android:required=\"true\""));
	// ...and only ONCE: the second declaration merged into the first
	CHECK(out.find("android.hardware.camera") ==
		out.rfind("android.hardware.camera"));
}

TEST_CASE("two libraries declaring the same thing declare it once",
	"[exporter][android][library]")
{
	// vendors ship overlapping archives; identical declarations are the same
	// declaration, and duplicating one is a manifest the platform rejects
	const Orkige::String declaration =
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <uses-permission android:name=\"android.permission.WAKE_LOCK\" />\n"
		"  <application>\n"
		"    <activity android:name=\"com.vendor.common.HostActivity\" />\n"
		"  </application>\n"
		"</manifest>\n";
	const Orkige::String out = merged({ fragmentOf(declaration, "a.aar"),
		fragmentOf(declaration, "b.aar") });

	CHECK(out.find("android.permission.WAKE_LOCK") ==
		out.rfind("android.permission.WAKE_LOCK"));
	CHECK(out.find("com.vendor.common.HostActivity") ==
		out.rfind("com.vendor.common.HostActivity"));
}

TEST_CASE("a library declaring nothing new leaves a permission the app has",
	"[exporter][android][library]")
{
	// the app already asks for INTERNET; a library asking again must not
	// produce a second declaration
	const Orkige::String out = merged({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <uses-permission android:name=\"android.permission.INTERNET\" />\n"
		"</manifest>\n") });
	CHECK(out.find("android.permission.INTERNET") ==
		out.rfind("android.permission.INTERNET"));
}

TEST_CASE("no library archive leaves the manifest byte-identical",
	"[exporter][android][library]")
{
	// the property that keeps this whole addition invisible where it is
	// unused: a project depending on no library packages exactly the manifest
	// it always did, with no XML round trip in the middle to reformat it
	Orkige::String out;
	Orkige::String error;
	REQUIRE(androidMergeManifest(appManifest(),
		std::vector<AndroidManifestFragment>(), "com.studio.game", 28, out, 0,
		&error));
	CHECK(out == appManifest());
}

//--- the refusals, each naming the archive and the element -------

TEST_CASE("an element this merge does not implement is refused by name",
	"[exporter][android][library]")
{
	// instrumentation runs a test harness against the app: dropping it makes
	// the app quietly different from what its author declared, so the export
	// stops and says which archive and which element
	const Orkige::String message = refusal({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <instrumentation android:name=\"com.vendor.sdk.Runner\" />\n"
		"</manifest>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
	CHECK(holds(message, "instrumentation"));
}

TEST_CASE("an application child this merge does not implement is refused",
	"[exporter][android][library]")
{
	const Orkige::String message = refusal({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <application>\n"
		"    <profileable android:shell=\"true\" />\n"
		"  </application>\n"
		"</manifest>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
	CHECK(holds(message, "profileable"));
}

TEST_CASE("a library that sets application attributes is refused by name",
	"[exporter][android][library]")
{
	// android:name on <application> replaces the app's Application class, and
	// android:theme its look - decisions the project owns. Applying one
	// silently is worse than refusing.
	const Orkige::String message = refusal({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <application android:name=\"com.vendor.sdk.VendorApplication\" />\n"
		"</manifest>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
	CHECK(holds(message, "android:name"));
	CHECK(holds(message, "com.vendor.sdk.VendorApplication"));
}

TEST_CASE("a merge directive this export does not implement is refused",
	"[exporter][android][library]")
{
	// the tools: vocabulary tells a merger to replace, remove or override.
	// Applying it wrongly and ignoring it are both worse than saying so.
	const Orkige::String message = refusal({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
		"    xmlns:tools=\"http://schemas.android.com/tools\">\n"
		"  <application>\n"
		"    <activity android:name=\"com.vendor.sdk.A\" tools:node=\"remove\" />\n"
		"  </application>\n"
		"</manifest>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
	CHECK(holds(message, "tools:node"));
}

TEST_CASE("a placeholder this export cannot resolve is refused by name",
	"[exporter][android][library]")
{
	const Orkige::String message = refusal({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <application>\n"
		"    <meta-data android:name=\"com.vendor.API_KEY\"\n"
		"        android:value=\"${vendorApiKey}\" />\n"
		"  </application>\n"
		"</manifest>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
	CHECK(holds(message, "${vendorApiKey}"));
}

TEST_CASE("a library needing a newer platform floor is refused by name",
	"[exporter][android][library]")
{
	// the app would install on devices the library cannot run on, and the
	// symptom is a crash in somebody else's code on somebody else's phone
	const Orkige::String message = refusal({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <uses-sdk android:minSdkVersion=\"33\" />\n"
		"</manifest>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
	CHECK(holds(message, "33"));
	CHECK(holds(message, "28"));
}

TEST_CASE("a library at or below the platform floor merges quietly",
	"[exporter][android][library]")
{
	const Orkige::String out = merged({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <uses-sdk android:minSdkVersion=\"21\" android:targetSdkVersion=\"33\" />\n"
		"  <uses-permission android:name=\"android.permission.VIBRATE\" />\n"
		"</manifest>\n") });
	CHECK(holds(out, "android.permission.VIBRATE"));
	// the app's own floor is untouched - a library never lowers it, and its
	// target is not the app's
	CHECK(holds(out, "android:minSdkVersion=\"28\""));
	CHECK(holds(out, "android:targetSdkVersion=\"35\""));
	CHECK_FALSE(holds(out, "android:minSdkVersion=\"21\""));
}

TEST_CASE("two libraries contradicting each other stop the export",
	"[exporter][android][library]")
{
	// same name, different value: the real merger resolves this with an
	// explicit directive somebody wrote. Nobody wrote one here, so picking a
	// winner would be inventing an answer.
	std::vector<AndroidManifestFragment> fragments;
	fragments.push_back(fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <application>\n"
		"    <meta-data android:name=\"com.vendor.MODE\" android:value=\"live\" />\n"
		"  </application>\n"
		"</manifest>\n", "one.aar"));
	fragments.push_back(fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <application>\n"
		"    <meta-data android:name=\"com.vendor.MODE\" android:value=\"test\" />\n"
		"  </application>\n"
		"</manifest>\n", "two.aar"));
	const Orkige::String message = refusal(fragments);
	CHECK(holds(message, "two.aar"));
	CHECK(holds(message, "com.vendor.MODE"));
}

TEST_CASE("a library contradicting the app's own manifest stops the export",
	"[exporter][android][library]")
{
	const Orkige::String message = refusal({ fragmentOf(
		"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"  <application>\n"
		"    <activity android:name=\"com.orkitec.orkigeplayer.OrkigeActivity\"\n"
		"        android:exported=\"false\" />\n"
		"  </application>\n"
		"</manifest>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
	CHECK(holds(message, "OrkigeActivity"));
}

TEST_CASE("an unreadable library manifest is refused, not half-merged",
	"[exporter][android][library]")
{
	const Orkige::String message =
		refusal({ fragmentOf("<manifest><application>\n") });
	CHECK(holds(message, "vendor-sdk.aar"));
}

//--- routing one archive entry -----------------------------------

TEST_CASE("every part of a library archive is routed where it belongs",
	"[exporter][android][library]")
{
	CHECK(androidLibraryEntry("AndroidManifest.xml").kind ==
		AndroidLibraryEntry::MANIFEST);

	// the compiled Java: the archive's own, plus the jars it bundles. Each
	// keeps its own PATH - two bundled jars may share a file name, and
	// flattening them would drop one without a word.
	CHECK(androidLibraryEntry("classes.jar").kind == AndroidLibraryEntry::JAR);
	CHECK(androidLibraryEntry("libs/support.jar").kind ==
		AndroidLibraryEntry::JAR);
	CHECK(androidLibraryEntry("libs/support.jar").relative ==
		"libs/support.jar");
	CHECK(androidLibraryEntry("libs/a/support.jar").relative !=
		androidLibraryEntry("libs/b/support.jar").relative);

	const AndroidLibraryEntry resource =
		androidLibraryEntry("res/values/values.xml");
	CHECK(resource.kind == AndroidLibraryEntry::RESOURCE);
	CHECK(resource.relative == "values/values.xml");

	const AndroidLibraryEntry asset = androidLibraryEntry("assets/cfg/a.json");
	CHECK(asset.kind == AndroidLibraryEntry::ASSET);
	CHECK(asset.relative == "cfg/a.json");

	// the ABI comes OUT of the name: which one an entry is for decides whether
	// it is packaged at all
	const AndroidLibraryEntry native =
		androidLibraryEntry("jni/arm64-v8a/libvendor.so");
	CHECK(native.kind == AndroidLibraryEntry::NATIVE);
	CHECK(native.abi == "arm64-v8a");
	CHECK(native.relative == "libvendor.so");

	CHECK(androidLibraryEntry("R.txt").kind == AndroidLibraryEntry::SYMBOLS);
}

TEST_CASE("what an archive carries and this assembly cannot use is ignored",
	"[exporter][android][library]")
{
	// these are not DROPPED from something that would have used them - there
	// is no shrinker, no lint run and no native build here to hand them to
	char const * const unused[] = { "proguard.txt", "lint.jar", "api.jar",
		"public.txt", "annotations.zip", "META-INF/MANIFEST.MF",
		"aidl/com/vendor/IThing.aidl", "prefab/modules/vendor/module.json",
		"res/", "jni/", "" };
	for(char const * const name : unused)
	{
		INFO(name);
		CHECK(androidLibraryEntry(name).kind == AndroidLibraryEntry::IGNORED);
	}
}

TEST_CASE("a classpath is joined the way this host's JDK reads one",
	"[exporter][android][library]")
{
	const Orkige::String joined =
		androidClasspath({ "/work/a.jar", "/work/b.jar" });
#if defined(_WIN32)
	CHECK(joined == "/work/a.jar;/work/b.jar");
#else
	CHECK(joined == "/work/a.jar:/work/b.jar");
#endif
	CHECK(androidClasspath({ "/work/a.jar" }) == "/work/a.jar");
	CHECK(androidClasspath({}).empty());
}

TEST_CASE("a manifest's own package is read off it",
	"[exporter][android][library]")
{
	CHECK(androidManifestPackage(
		"<manifest package=\"com.vendor.sdk\" />") == "com.vendor.sdk");
	CHECK(androidManifestPackage("<manifest />").empty());
	CHECK(androidManifestPackage("not xml at all").empty());
}

//--- the setting that lists them ---------------------------------

TEST_CASE("the library setting is a list of project-relative archives",
	"[exporter][android][library]")
{
	SettingMap settings;
	settings["export.android.libraries"] =
		" libs/vendor.aar ; libs/other.aar ;; libs/vendor.aar ";
	std::vector<Orkige::String> libraries;
	Orkige::String error;
	REQUIRE(androidLibrarySettings(settings, libraries, &error));
	// the order written, whitespace trimmed, blanks skipped and the same
	// archive listed twice consumed once
	CHECK(libraries ==
		std::vector<Orkige::String>{ "libs/vendor.aar", "libs/other.aar" });

	CHECK(androidLibrarySettings(SettingMap(), libraries, &error));
	CHECK(libraries.empty());
}

TEST_CASE("a library path that leaves the project is refused by name",
	"[exporter][android][library]")
{
	// an export follows a manifest's paths, and a manifest is a file somebody
	// else may have written - so it never reaches outside the project
	char const * const refused[] = { "/opt/sdk/vendor.aar",
		"C:/sdk/vendor.aar", "../../elsewhere/vendor.aar",
		"libs/vendor.jar", "libs/vendor" };
	for(char const * const value : refused)
	{
		INFO(value);
		SettingMap settings;
		settings["export.android.libraries"] = value;
		std::vector<Orkige::String> libraries;
		Orkige::String error;
		CHECK_FALSE(androidLibrarySettings(settings, libraries, &error));
		CHECK(error.find(value) != Orkige::String::npos);
	}
}
