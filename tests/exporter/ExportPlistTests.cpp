/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportPlistTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The Apple property lists an export ships.

	The three fixed declarations are asserted through a WRITE/READ round trip,
	because what matters is not the dictionary in memory but the file Apple's
	tooling reads. Two of them are security statements and are asserted as
	EXACT sets: an over-declaring privacy manifest is a review problem, and an
	extra transport-security exception silently opens cleartext to the whole
	internet - both must fail a test rather than ship.

	The in-place key edit is the other half: the iOS bundle an export inherits
	carries keys this code has never heard of, and they must survive verbatim.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportPlist.h"
#include "ExportSettings.h"

#include <filesystem>

using namespace OrkigeExport;

namespace
{
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_plist_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	//! write then read back, so every assertion is about the FILE
	Orkige::JsonValue roundTrip(Orkige::JsonValue const & value,
		Orkige::String const & path)
	{
		Orkige::String error;
		REQUIRE(ExportPlist::write(value, path, &error));
		Orkige::JsonValue reloaded;
		REQUIRE(ExportPlist::read(path, reloaded, &error));
		return reloaded;
	}
}

TEST_CASE("a plist round-trips every value shape", "[unit][export]")
{
	ScratchDir scratch("shapes");
	const Orkige::String path = ExportFiles::join(scratch.path, "a.plist");

	Orkige::JsonValue nested = Orkige::JsonValue::object();
	nested.set("inner", Orkige::JsonValue("value"));
	Orkige::JsonValue list = Orkige::JsonValue::array();
	list.push(Orkige::JsonValue("one"));
	list.push(Orkige::JsonValue("two"));

	Orkige::JsonValue root = Orkige::JsonValue::object();
	root.set("text", Orkige::JsonValue("hello"));
	root.set("yes", Orkige::JsonValue(true));
	root.set("no", Orkige::JsonValue(false));
	root.set("count", Orkige::JsonValue(42));
	root.set("ratio", Orkige::JsonValue(1.5));
	root.set("list", list);
	root.set("nested", nested);
	root.set("emptyDict", Orkige::JsonValue::object());
	root.set("emptyList", Orkige::JsonValue::array());
	// a string needing XML escaping must survive as its own characters
	root.set("escaped", Orkige::JsonValue("a & b < c > \"d\""));

	const Orkige::JsonValue reloaded = roundTrip(root, path);
	CHECK(reloaded.get("text").asString() == "hello");
	CHECK(reloaded.get("yes").asBool());
	CHECK_FALSE(reloaded.get("no").asBool());
	CHECK(reloaded.get("count").asInt() == 42);
	CHECK(reloaded.get("ratio").asNumber() == 1.5);
	CHECK(reloaded.get("list").size() == 2);
	CHECK(reloaded.get("list").at(1).asString() == "two");
	CHECK(reloaded.get("nested").get("inner").asString() == "value");
	CHECK(reloaded.get("emptyDict").isObject());
	CHECK(reloaded.get("emptyList").isArray());
	CHECK(reloaded.get("escaped").asString() == "a & b < c > \"d\"");

	// an integral number is an <integer>, a fractional one a <real> - the
	// plist types a version and a scale actually are
	Orkige::String text;
	REQUIRE(ExportPlist::serialize(root, text, 0));
	CHECK(text.find("<integer>42</integer>") != Orkige::String::npos);
	CHECK(text.find("<real>1.5") != Orkige::String::npos);
	CHECK(text.find("<!DOCTYPE plist") != Orkige::String::npos);
}

TEST_CASE("a non-dictionary plist root refuses", "[unit][export]")
{
	Orkige::String error;
	Orkige::String text;
	CHECK_FALSE(ExportPlist::serialize(Orkige::JsonValue("scalar"), text,
		&error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("app transport security allows the local network only",
	"[unit][export]")
{
	ScratchDir scratch("ats");
	Orkige::JsonValue root = Orkige::JsonValue::object();
	root.set("NSAppTransportSecurity", appTransportSecurity());
	root.set("UILaunchScreen", Orkige::JsonValue::object());

	const Orkige::JsonValue reloaded = roundTrip(root,
		ExportFiles::join(scratch.path, "Info.plist"));
	const Orkige::JsonValue & security =
		reloaded.get("NSAppTransportSecurity");
	CHECK(security.get("NSAllowsLocalNetworking").asBool());
	// cleartext to the whole internet stays blocked, and NOTHING else is
	// declared - an escalation here has to be a test failure
	CHECK_FALSE(security.has("NSAllowsArbitraryLoads"));
	CHECK(security.members().size() == 1);
	CHECK(reloaded.get("UILaunchScreen").isObject());
}

TEST_CASE("the privacy manifest declares exactly the audited categories",
	"[unit][export]")
{
	ScratchDir scratch("privacy");
	const Orkige::JsonValue reloaded = roundTrip(privacyManifest(),
		ExportFiles::join(scratch.path, PRIVACY_MANIFEST_FILE_NAME));

	CHECK_FALSE(reloaded.get("NSPrivacyTracking").asBool());
	CHECK(reloaded.get("NSPrivacyTrackingDomains").size() == 0);
	CHECK(reloaded.get("NSPrivacyCollectedDataTypes").size() == 0);

	// exactly two required-reason categories, each with its approved code:
	// an under-declaring manifest is rejected at review, an over-declaring
	// one invites questions the engine cannot answer
	const Orkige::JsonValue & accessed =
		reloaded.get("NSPrivacyAccessedAPITypes");
	REQUIRE(accessed.size() == 2);
	CHECK(accessed.at(0).get("NSPrivacyAccessedAPIType").asString() ==
		"NSPrivacyAccessedAPICategoryFileTimestamp");
	CHECK(accessed.at(0).get("NSPrivacyAccessedAPITypeReasons").at(0)
		.asString() == "C617.1");
	CHECK(accessed.at(1).get("NSPrivacyAccessedAPIType").asString() ==
		"NSPrivacyAccessedAPICategorySystemBootTime");
	CHECK(accessed.at(1).get("NSPrivacyAccessedAPITypeReasons").at(0)
		.asString() == "35F9.1");
}

TEST_CASE("entitlements compose the application identifier", "[unit][export]")
{
	Orkige::JsonValue development =
		iosEntitlements("ABCDE12345", "com.example.game", false);
	CHECK(development.get("application-identifier").asString() ==
		"ABCDE12345.com.example.game");
	CHECK(development.get("com.apple.developer.team-identifier").asString() ==
		"ABCDE12345");
	// development attaches a debugger
	CHECK(development.get("get-task-allow").asBool());

	// no team id: the bundle id stands alone and the dictionary is still valid
	CHECK(iosEntitlements("", "com.example.game", false)
		.get("application-identifier").asString() == "com.example.game");

	// a DISTRIBUTION build must clear get-task-allow - the App Store rejects
	// a submission that carries it
	Orkige::JsonValue distribution =
		iosEntitlements("ABCDE12345", "com.example.game", true);
	CHECK_FALSE(distribution.get("get-task-allow").asBool());
	CHECK(distribution.get("application-identifier").asString() ==
		"ABCDE12345.com.example.game");
}

TEST_CASE("setKeys rewrites identity and keeps everything else",
	"[unit][export]")
{
	ScratchDir scratch("edit");
	const Orkige::String path = ExportFiles::join(scratch.path, "Info.plist");
	// the shape a prebuilt player bundle carries, including a key this code
	// has no model for at all
	REQUIRE(ExportFiles::writeTextFile(path,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<plist version=\"1.0\">\n"
		"<dict>\n"
		"	<key>CFBundleIdentifier</key>\n"
		"	<string>com.orkitec.player</string>\n"
		"	<key>CFBundleExecutable</key>\n"
		"	<string>OrkigePlayer</string>\n"
		"	<key>UILaunchScreen</key>\n"
		"	<dict/>\n"
		"	<key>SomeUnknownKey</key>\n"
		"	<array><string>keep me</string></array>\n"
		"</dict>\n"
		"</plist>\n", 0));

	Orkige::JsonValue icons = Orkige::JsonValue::object();
	Orkige::JsonValue names = Orkige::JsonValue::array();
	names.push(Orkige::JsonValue("AppIcon60x60"));
	Orkige::JsonValue primary = Orkige::JsonValue::object();
	primary.set("CFBundleIconFiles", names);
	icons.set("CFBundlePrimaryIcon", primary);

	Orkige::JsonValue keys = Orkige::JsonValue::object();
	keys.set("CFBundleIdentifier", Orkige::JsonValue("com.example.mygame"));
	keys.set("CFBundleDisplayName", Orkige::JsonValue("My Game"));
	keys.set("CFBundleIcons", icons);

	Orkige::String error;
	REQUIRE(ExportPlist::setKeys(path, keys, &error));

	Orkige::JsonValue reloaded;
	REQUIRE(ExportPlist::read(path, reloaded, &error));
	// replaced in place
	CHECK(reloaded.get("CFBundleIdentifier").asString() ==
		"com.example.mygame");
	// appended
	CHECK(reloaded.get("CFBundleDisplayName").asString() == "My Game");
	CHECK(reloaded.get("CFBundleIcons").get("CFBundlePrimaryIcon")
		.get("CFBundleIconFiles").at(0).asString() == "AppIcon60x60");
	// untouched, including the key nothing here models
	CHECK(reloaded.get("CFBundleExecutable").asString() == "OrkigePlayer");
	CHECK(reloaded.get("UILaunchScreen").isObject());
	CHECK(reloaded.get("SomeUnknownKey").at(0).asString() == "keep me");
}

TEST_CASE("editing a plist that is not one refuses", "[unit][export]")
{
	ScratchDir scratch("notaplist");
	const Orkige::String path = ExportFiles::join(scratch.path, "x.plist");
	REQUIRE(ExportFiles::writeTextFile(path, "<notplist/>\n", 0));
	Orkige::String error;
	CHECK_FALSE(ExportPlist::setKeys(path, Orkige::JsonValue::object(),
		&error));
	CHECK_FALSE(error.empty());

	Orkige::JsonValue value;
	error.clear();
	CHECK_FALSE(ExportPlist::read(
		ExportFiles::join(scratch.path, "absent.plist"), value, &error));
	CHECK_FALSE(error.empty());
}
