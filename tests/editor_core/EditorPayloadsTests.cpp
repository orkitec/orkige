/********************************************************************
	created:	Sunday 2026/08/02 at 14:00
	filename: 	EditorPayloadsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Every decision behind the fetched pieces of an installation, exercised with
// no network and no files on disk: the catalogue and what each payload
// unlocks, the published asset name, the dated release a build pairs with, the
// install layout, what makes a payload complete, the prune plan (including the
// platform a user turned off), the settings codec and the refusal sentences.
#include <catch2/catch_test_macros.hpp>

#include "EditorPayloads.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

using namespace OrkigeEditor;

namespace
{
	//! the ordered identity a nightly build carries
	const char* const VERSION = "2.0.0-nightly.20260802+dea551f9e0";
	//! its filename rendering (the '+' becomes '_')
	const char* const TOKEN = "2.0.0-nightly.20260802_dea551f9e0";

	//! an existence probe over an explicit set - the filesystem question,
	//! answered from a list instead of from a disk
	std::function<bool(Orkige::String const&)> presence(
		std::vector<Orkige::String> const& present)
	{
		return [present](Orkige::String const& path)
		{
			return std::find(present.begin(), present.end(), path) !=
				present.end();
		};
	}

	FetchablePayload iosPayload()
	{
		FetchablePayload payload;
		REQUIRE(findFetchablePayload("player-ios-simulator", payload));
		return payload;
	}
}

TEST_CASE("the catalogue names what each download unlocks",
	"[editor][payloads]")
{
	const std::vector<FetchablePayload> catalogue = fetchablePayloads();
	REQUIRE_FALSE(catalogue.empty());
	for (FetchablePayload const& payload : catalogue)
	{
		// an id travels in published file names and install paths, so every
		// entry must have one, and no two may share it
		CHECK_FALSE(payload.id.empty());
		CHECK_FALSE(payload.label.empty());
		CHECK_FALSE(payload.platformLabel.empty());
		CHECK(std::count_if(catalogue.begin(), catalogue.end(),
			[&payload](FetchablePayload const& other)
			{
				return other.id == payload.id;
			}) == 1);
	}
	CHECK(payloadIdForExportPlatform("ios-simulator") ==
		"player-ios-simulator");
	CHECK(payloadIdForExportPlatform("android") == "player-android");
	// the two targets a released editor packages out of ITSELF need no
	// download at all, and neither does something that is not a platform
	CHECK(payloadIdForExportPlatform("macos").empty());
	CHECK(payloadIdForExportPlatform("web").empty());
	CHECK(payloadIdForExportPlatform("nonsense").empty());
}

TEST_CASE("the Android payload carries what ASSEMBLES a package",
	"[editor][payloads]")
{
	// an iOS `.app` is copied whole into the package, so its payload needs
	// nothing but the bundle. An APK is ASSEMBLED around the player, so this
	// one also carries what the assembly builds the package out of: the
	// manifest template, the platform policy resources and the Java it
	// compiles - engine pieces, all of them. What it does NOT carry is the
	// Android SDK's own programs: those are the machine's, and the export
	// names each missing one instead (OrkigeExport::androidToolchainGaps).
	FetchablePayload android;
	REQUIRE(findFetchablePayload("player-android", android));
	CHECK(android.kind == PayloadKind::Player);
	CHECK(android.exportPlatform == "android");
	CHECK(android.marker == "libmain.so");

	const std::vector<Orkige::String> required = payloadRequiredPaths(android);
	for (char const* piece : { "orkige_payload.txt", "libmain.so", "Media",
		"android/AndroidManifest.xml", "android/res", "android/java" })
	{
		INFO(piece);
		CHECK(std::find(required.begin(), required.end(),
			Orkige::String(piece)) != required.end());
	}
	// ...and each of them is genuinely REQUIRED: a payload unpacked without a
	// piece of the assembly is refused rather than handed to an export that
	// would fail deep inside the packaging run
	std::vector<Orkige::String> present;
	for (Orkige::String const& relative : required)
	{
		present.push_back("/p/" + relative);
	}
	CHECK(payloadProblems(android, "/p", presence(present)).empty());
	for (std::size_t index = 0; index < required.size(); ++index)
	{
		std::vector<Orkige::String> partial = present;
		partial.erase(partial.begin() + static_cast<long>(index));
		const std::vector<Orkige::String> problems =
			payloadProblems(android, "/p", presence(partial));
		REQUIRE(problems.size() == 1);
		CHECK(problems[0] == required[index]);
	}
}

TEST_CASE("the published asset name is one grammar", "[editor][payloads]")
{
	// the SAME name the publishing side composes
	// (Util/orkige_nightly_package.py device_payload_asset_name) - the two
	// spellings are pinned on both sides because neither library sees the other
	CHECK(payloadAssetName("player-ios-simulator", "next", VERSION) ==
		"orkige-player-ios-simulator-next-" + std::string(TOKEN) + ".zip");
	// ...and the same rule for every other id, on both sides
	CHECK(payloadAssetName("player-android", "classic", VERSION) ==
		"orkige-player-android-classic-" + std::string(TOKEN) + ".zip");
	// an unstamped build names no asset rather than one nobody published
	CHECK(payloadAssetName("player-ios-simulator", "next", "").empty());
	CHECK(payloadAssetName("player-ios-simulator", "next",
		"local build").empty());
	CHECK(payloadAssetName("", "next", VERSION).empty());
	CHECK(payloadAssetName("player-ios-simulator", "", VERSION).empty());
}

TEST_CASE("a build pairs with the DATED release it was published in",
	"[editor][payloads]")
{
	// the rolling `nightly` tag is replaced every night, so a build a day old
	// would find its own assets gone; the dated one never moves
	CHECK(payloadReleaseTag(VERSION) == "nightly-20260802");
	CHECK(payloadReleaseTag("2.0.0-nightly.20260802_dea551f9e0") ==
		"nightly-20260802");
	CHECK(payloadReleaseTag("").empty());
	CHECK(payloadReleaseTag("2.0.0").empty());
	CHECK(payloadReleaseTag("2.0.0-nightly.2026080+abc").empty());
	// a day that runs on into something else is not a day this client reads
	CHECK(payloadReleaseTag("2.0.0-nightly.202608021+abc").empty());

	CHECK(payloadReleaseUrl("https://example.invalid/releases", VERSION) ==
		"https://example.invalid/releases/tags/nightly-20260802");
	CHECK(payloadReleaseUrl("", VERSION) ==
		std::string(defaultPayloadReleasesUrl()) + "/tags/nightly-20260802");
	CHECK(payloadReleaseUrl("https://example.invalid/releases", "").empty());
}

TEST_CASE("an install path carries the version", "[editor][payloads]")
{
	// two versions can never share a directory, which is what makes a prune a
	// set operation and an interrupted install visible
	CHECK(payloadInstallDirectory("/state/payloads", "player-ios-simulator",
		"next", VERSION) ==
		"/state/payloads/player-ios-simulator/next/" + std::string(TOKEN));
	// a trailing separator on the root is absorbed rather than doubled
	CHECK(payloadInstallDirectory("/state/payloads/", "player-ios-simulator",
		"classic", VERSION) ==
		"/state/payloads/player-ios-simulator/classic/" + std::string(TOKEN));
	CHECK(payloadInstallDirectory("", "player-ios-simulator", "next",
		VERSION).empty());
	CHECK(payloadInstallDirectory("/state", "player-ios-simulator", "next",
		"").empty());
}

TEST_CASE("a payload is complete or it is not handed to an export",
	"[editor][payloads]")
{
	const FetchablePayload payload = iosPayload();
	const std::vector<Orkige::String> required = payloadRequiredPaths(payload);
	// the manifest is required: a payload that cannot say which flavor it is
	// would be packaged as the wrong one. The name is spelled in the exporter
	// too (OrkigeExport::DEVICE_PAYLOAD_MANIFEST_FILE_NAME) - this pins it.
	CHECK(std::find(required.begin(), required.end(), "orkige_payload.txt") !=
		required.end());
	CHECK(std::find(required.begin(), required.end(), "Media") !=
		required.end());

	std::vector<Orkige::String> present;
	for (Orkige::String const& relative : required)
	{
		present.push_back("/p/" + relative);
	}
	CHECK(payloadProblems(payload, "/p", presence(present)).empty());

	// half-unpacked: the missing piece is NAMED, so the refusal is actionable
	present.pop_back();
	const std::vector<Orkige::String> problems =
		payloadProblems(payload, "/p", presence(present));
	REQUIRE(problems.size() == 1);
	CHECK(problems[0] == required[required.size() - 1]);
	// nothing there at all: everything is missing, and nothing throws
	CHECK(payloadProblems(payload, "/p",
		presence(std::vector<Orkige::String>())).size() == required.size());
}

TEST_CASE("the prune keeps exactly what this build needs",
	"[editor][payloads]")
{
	std::vector<InstalledPayload> installed;
	const auto add = [&installed](char const* id, char const* flavor,
		char const* version)
	{
		InstalledPayload entry;
		entry.id = id;
		entry.flavor = flavor;
		entry.version = version;
		entry.path = std::string("/state/") + id + "/" + flavor + "/" +
			version;
		installed.push_back(entry);
	};
	add("player-ios-simulator", "next", TOKEN);				// the keeper
	add("player-ios-simulator", "next", "2.0.0-nightly.20260801_aaaaaaaaa");
	add("player-ios-simulator", "classic", TOKEN);			// other flavor
	// a payload for something this installation no longer wants - a platform
	// switched off, or one an older build fetched and this one does not offer
	add("player-android", "next", TOKEN);

	std::vector<Orkige::String> enabled;
	enabled.push_back("player-ios-simulator");
	const std::vector<Orkige::String> doomed =
		planPayloadPrune(installed, enabled, "next", VERSION);
	REQUIRE(doomed.size() == 3);
	CHECK(std::find(doomed.begin(), doomed.end(), installed[0].path) ==
		doomed.end());
	CHECK(std::find(doomed.begin(), doomed.end(), installed[1].path) !=
		doomed.end());
	CHECK(std::find(doomed.begin(), doomed.end(), installed[2].path) !=
		doomed.end());
	CHECK(std::find(doomed.begin(), doomed.end(), installed[3].path) !=
		doomed.end());

	// an unstamped build cannot name a keeper, so nothing is kept on a guess
	CHECK(planPayloadPrune(installed, enabled, "next", "").size() ==
		installed.size());
	// ...and nothing enabled means nothing kept
	CHECK(planPayloadPrune(installed, std::vector<Orkige::String>(), "next",
		VERSION).size() == installed.size());
}

TEST_CASE("the build-targets setting round-trips and drops strangers",
	"[editor][payloads]")
{
	// the DEFAULT is empty: the host alone, so nothing is ever fetched for a
	// platform nobody asked about
	CHECK(parseEnabledPayloads("").empty());
	const std::vector<Orkige::String> parsed =
		parseEnabledPayloads(" player-ios-simulator ");
	REQUIRE(parsed.size() == 1);
	CHECK(isPayloadEnabled(parsed, "player-ios-simulator"));
	CHECK_FALSE(isPayloadEnabled(parsed, "player-toaster"));
	// catalogue order, so one set always persists as one line
	CHECK(formatEnabledPayloads(parsed) == "player-ios-simulator");
	CHECK(formatEnabledPayloads(parseEnabledPayloads(
		formatEnabledPayloads(parsed))) == formatEnabledPayloads(parsed));
	// a setting written by ANOTHER build never makes this one fetch something
	// it cannot use, and a duplicate is not carried twice
	const std::vector<Orkige::String> filtered = parseEnabledPayloads(
		"player-ios-simulator,player-toaster,player-ios-simulator,,");
	REQUIRE(filtered.size() == 1);
	CHECK(filtered[0] == "player-ios-simulator");
	// ...and every id the catalogue DOES know survives the round trip, in
	// catalogue order, so a user who builds for two platforms keeps both
	const std::vector<Orkige::String> both = parseEnabledPayloads(
		"player-android, player-ios-simulator");
	REQUIRE(both.size() == 2);
	CHECK(formatEnabledPayloads(both) ==
		"player-ios-simulator,player-android");
}

TEST_CASE("the unpack command names a tool the platform provides",
	"[editor][payloads]")
{
	const std::vector<Orkige::String> command =
		payloadExtractCommand("/tmp/thing.zip", "/tmp/out");
	REQUIRE_FALSE(command.empty());
	CHECK(std::find(command.begin(), command.end(),
		Orkige::String("/tmp/thing.zip")) != command.end());
	CHECK(std::find(command.begin(), command.end(),
		Orkige::String("/tmp/out")) != command.end());
	// a shape this does not unpack is refused rather than half-attempted
	CHECK(payloadExtractCommand("/tmp/thing.tar.gz", "/tmp/out").empty());
	CHECK(payloadExtractCommand("", "/tmp/out").empty());
	CHECK(payloadExtractCommand("/tmp/thing.zip", "").empty());
}

TEST_CASE("a missing payload is refused with a next step",
	"[editor][payloads]")
{
	const FetchablePayload payload = iosPayload();
	// three different states, three different sentences - and every one of
	// them says what to do next
	const Orkige::String notEnabled = payloadMissingMessage(payload, false,
		true);
	CHECK(notEnabled.find("Build Targets") != Orkige::String::npos);
	CHECK(notEnabled.find(payload.platformLabel) != Orkige::String::npos);

	const Orkige::String enabled = payloadMissingMessage(payload, true, true);
	CHECK(enabled.find("not installed") != Orkige::String::npos);
	CHECK(enabled.find("Build Targets") != Orkige::String::npos);

	const Orkige::String offline = payloadMissingMessage(payload, true, false);
	CHECK(offline.find("network") != Orkige::String::npos);
	CHECK(offline.find("source tree") != Orkige::String::npos);
}
