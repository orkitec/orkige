/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	EditorUpdateTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Every decision the updater makes, exercised with no network, no clock and
// no installed app: the cadence gate, the version comparison (downgrade and
// unstamped included), the release-feed reading, the per-platform asset
// choice, the checksum sidecar, the "may this location be replaced" verdict
// and the swap plan together with the script that carries it out - including
// the branch that puts the old copy back.
#include <catch2/catch_test_macros.hpp>

#include "EditorUpdate.h"

#include <string>
#include <vector>

using namespace OrkigeEditor;

namespace
{
	//! a release body shaped like the one the publishing side writes
	std::string releaseBody(std::string const& version)
	{
		return
			"## Downloads\n"
			"\n"
			"| Platform | Download |\n"
			"| --- | --- |\n"
			"\n"
			"## Changes since `0123456789`\n"
			"\n"
			"- `abcdef123` A thing that landed\n"
			"- `abcdef122` Another thing\n"
			"\n"
			"<!-- orkige-nightly-commit: abcdef1230000000000000000000000000 -->\n"
			"<!-- orkige-nightly-version: " + version + " -->\n";
	}

	//! the JSON the release endpoint answers with
	std::string releaseJson(std::string const& version,
		std::vector<std::string> const& assetNames)
	{
		std::string json = "{\"tag_name\":\"nightly\",\"body\":\"";
		const std::string body = releaseBody(version);
		for (std::size_t index = 0; index < body.size(); ++index)
		{
			if (body[index] == '"')
			{
				json += "\\\"";
			}
			else if (body[index] == '\n')
			{
				json += "\\n";
			}
			else if (body[index] == '\\')
			{
				json += "\\\\";
			}
			else
			{
				json += body[index];
			}
		}
		json += "\",\"assets\":[";
		for (std::size_t index = 0; index < assetNames.size(); ++index)
		{
			if (index > 0)
			{
				json += ",";
			}
			json += "{\"name\":\"" + assetNames[index] +
				"\",\"size\":1234,\"browser_download_url\":"
				"\"https://example.invalid/" + assetNames[index] + "\"}";
		}
		json += "]}";
		return json;
	}

	const char* const CURRENT = "2.0.0-nightly.20260730+dea551f9e";
	const char* const NEWER = "2.0.0-nightly.20260731+abcdef123";
	const char* const NEWER_TOKEN = "2.0.0-nightly.20260731_abcdef123";
}

// --- the setting -----------------------------------------------------------

TEST_CASE("the update setting round-trips through its token", "[unit][update]")
{
	CHECK(std::string(updatePolicyName(UpdatePolicy::Off)) == "off");
	CHECK(std::string(updatePolicyName(UpdatePolicy::Notify)) == "notify");
	CHECK(std::string(updatePolicyName(UpdatePolicy::Download)) == "download");
	for (UpdatePolicy policy : { UpdatePolicy::Off, UpdatePolicy::Notify,
		UpdatePolicy::Download })
	{
		CHECK(parseUpdatePolicy(updatePolicyName(policy),
			UpdatePolicy::Off) == policy);
	}
	// an unreadable value keeps whatever the caller decided the default is,
	// rather than silently switching a user's choice to something else
	CHECK(parseUpdatePolicy("wat", UpdatePolicy::Download) ==
		UpdatePolicy::Download);
	CHECK(parseUpdatePolicy("", UpdatePolicy::Notify) == UpdatePolicy::Notify);
	CHECK(parseUpdatePolicy(" DOWNLOAD ", UpdatePolicy::Off) ==
		UpdatePolicy::Download);
}

// --- the cadence gate ------------------------------------------------------

TEST_CASE("the cadence gate checks once per launch, once a day",
	"[unit][update]")
{
	UpdateCheckContext context;
	context.policy = UpdatePolicy::Notify;
	context.nowEpochSeconds = 1000000;

	SECTION("never checked before")
	{
		context.lastCheckEpochSeconds = 0;
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::Run);
	}
	SECTION("checked an hour ago")
	{
		context.lastCheckEpochSeconds = context.nowEpochSeconds - 3600;
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::TooSoon);
	}
	SECTION("checked a day ago exactly")
	{
		context.lastCheckEpochSeconds =
			context.nowEpochSeconds - updateCheckIntervalSeconds();
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::Run);
	}
	SECTION("checked a day and a second ago")
	{
		context.lastCheckEpochSeconds =
			context.nowEpochSeconds - updateCheckIntervalSeconds() - 1;
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::Run);
	}
	SECTION("a stamp from the future does not lock checking out")
	{
		// a clock that moved backwards must not leave the editor unable to
		// check until it catches up
		context.lastCheckEpochSeconds = context.nowEpochSeconds + 100000;
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::Run);
	}
}

TEST_CASE("the cadence gate honours the setting and the vetoes",
	"[unit][update]")
{
	UpdateCheckContext context;
	context.nowEpochSeconds = 1000000;

	SECTION("Off does not check on its own")
	{
		context.policy = UpdatePolicy::Off;
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::PolicyOff);
	}
	SECTION("an explicit request overrides Off and the interval")
	{
		context.policy = UpdatePolicy::Off;
		context.manual = true;
		context.lastCheckEpochSeconds = context.nowEpochSeconds - 5;
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::Run);
	}
	SECTION("an automated run never checks, however it was asked")
	{
		context.policy = UpdatePolicy::Download;
		context.automatedRun = true;
		context.manual = true;
		CHECK(decideUpdateCheck(context) == UpdateCheckDecision::Automated);
	}
	SECTION("an unstamped build is told it cannot be compared")
	{
		context.policy = UpdatePolicy::Download;
		context.hasOrderedVersion = false;
		CHECK(decideUpdateCheck(context) ==
			UpdateCheckDecision::NoOrderedVersion);
		context.manual = true;
		CHECK(decideUpdateCheck(context) ==
			UpdateCheckDecision::NoOrderedVersion);
	}
	SECTION("every refusal carries a sentence, and Run carries none")
	{
		CHECK(std::string(updateCheckDecisionReason(
			UpdateCheckDecision::Run)).empty());
		for (UpdateCheckDecision decision : {
			UpdateCheckDecision::PolicyOff, UpdateCheckDecision::Automated,
			UpdateCheckDecision::TooSoon,
			UpdateCheckDecision::NoOrderedVersion })
		{
			CHECK_FALSE(
				std::string(updateCheckDecisionReason(decision)).empty());
		}
	}
}

// --- the comparison --------------------------------------------------------

TEST_CASE("only a strictly newer version is ever offered", "[unit][update]")
{
	CHECK(judgeUpdate(NEWER, CURRENT) == UpdateVerdict::Offer);
	// a rebuild of one day's tree is the SAME version, whatever its commit
	CHECK(judgeUpdate("2.0.0-nightly.20260730+0000000aa", CURRENT) ==
		UpdateVerdict::UpToDate);
	// a downgrade is refused, not offered as "different"
	CHECK(judgeUpdate("2.0.0-nightly.20260729+0000000aa", CURRENT) ==
		UpdateVerdict::Older);
	CHECK(judgeUpdate("1.9.0", CURRENT) == UpdateVerdict::Older);
	// a stable release outranks every prerelease of its base
	CHECK(judgeUpdate("2.0.0", CURRENT) == UpdateVerdict::Offer);
	// an unstamped developer build is neither offered anything nor told it
	// is current
	CHECK(judgeUpdate(NEWER, "2.0.0 (local build)") ==
		UpdateVerdict::Incomparable);
	CHECK(judgeUpdate(NEWER, "") == UpdateVerdict::Incomparable);
	CHECK(judgeUpdate("not a version", CURRENT) ==
		UpdateVerdict::Incomparable);
	CHECK(std::string(updateVerdictReason(UpdateVerdict::UpToDate)) ==
		"You are on the latest version.");
}

// --- reading the release ---------------------------------------------------

TEST_CASE("the release is read from its marker, never from prose",
	"[unit][update]")
{
	std::vector<std::string> assets;
	assets.push_back(std::string("Orkige-macos-") + NEWER_TOKEN + ".zip");
	assets.push_back(std::string("Orkige-macos-") + NEWER_TOKEN +
		".zip.sha256");
	const UpdateRelease release =
		parseUpdateRelease(releaseJson(NEWER, assets));
	REQUIRE(release.valid);
	CHECK(release.version == NEWER);
	CHECK(release.assets.size() == 2);
	CHECK(release.changelog.find("## Changes since `0123456789`") == 0);
	CHECK(release.changelog.find("A thing that landed") !=
		std::string::npos);
	// the section stops before the markers - they are machinery, not notes
	CHECK(release.changelog.find("orkige-nightly-version") ==
		std::string::npos);
}

TEST_CASE("a release this client does not understand is refused",
	"[unit][update]")
{
	SECTION("not JSON at all")
	{
		const UpdateRelease release = parseUpdateRelease("<html>nope</html>");
		CHECK_FALSE(release.valid);
		CHECK_FALSE(release.problem.empty());
	}
	SECTION("no version marker")
	{
		const UpdateRelease release = parseUpdateRelease(
			"{\"body\":\"a release somebody wrote by hand\",\"assets\":[]}");
		CHECK_FALSE(release.valid);
		CHECK_FALSE(release.problem.empty());
	}
	SECTION("a truncated marker")
	{
		const UpdateRelease release = parseUpdateRelease(
			"{\"body\":\"<!-- orkige-nightly-version: 2.0.0\",\"assets\":[]}");
		CHECK_FALSE(release.valid);
	}
}

// --- picking the asset -----------------------------------------------------

TEST_CASE("the updater takes the portable archive and nothing else",
	"[unit][update]")
{
	std::vector<UpdateAsset> assets;
	const char* names[] = {
		"Orkige-macos-2.0.0-nightly.20260731_abcdef123.dmg",
		"Orkige-macos-2.0.0-nightly.20260731_abcdef123.dmg.sha256",
		"Orkige-macos-2.0.0-nightly.20260731_abcdef123.zip",
		"Orkige-macos-2.0.0-nightly.20260731_abcdef123.zip.sha256",
		"Orkige-linux-2.0.0-nightly.20260731_abcdef123.tar.gz",
		"Orkige-linux-2.0.0-nightly.20260731_abcdef123.tar.gz.sha256",
		"Orkige-windows-2.0.0-nightly.20260731_abcdef123-setup.exe",
		"Orkige-windows-2.0.0-nightly.20260731_abcdef123.zip",
		"Orkige-windows-2.0.0-nightly.20260731_abcdef123.zip.sha256",
		"CHANGELOG.md"
	};
	for (std::size_t index = 0; index < sizeof(names) / sizeof(names[0]);
		++index)
	{
		UpdateAsset asset;
		asset.name = names[index];
		asset.url = std::string("https://example.invalid/") + names[index];
		asset.size = 100;
		assets.push_back(asset);
	}

	SECTION("macOS takes the zip, never the disk image")
	{
		const UpdateAssetChoice choice =
			selectUpdateAssets(assets, UpdatePlatform::MacOS, NEWER);
		REQUIRE(choice.found);
		CHECK(choice.archive.name ==
			"Orkige-macos-2.0.0-nightly.20260731_abcdef123.zip");
		CHECK(choice.checksum.name == choice.archive.name + ".sha256");
	}
	SECTION("Linux takes the tarball")
	{
		const UpdateAssetChoice choice =
			selectUpdateAssets(assets, UpdatePlatform::Linux, NEWER);
		REQUIRE(choice.found);
		CHECK(choice.archive.name ==
			"Orkige-linux-2.0.0-nightly.20260731_abcdef123.tar.gz");
	}
	SECTION("Windows takes the zip, never the installer")
	{
		const UpdateAssetChoice choice =
			selectUpdateAssets(assets, UpdatePlatform::Windows, NEWER);
		REQUIRE(choice.found);
		CHECK(choice.archive.name ==
			"Orkige-windows-2.0.0-nightly.20260731_abcdef123.zip");
	}
	SECTION("a platform whose build failed simply has no asset")
	{
		std::vector<UpdateAsset> onlyMac;
		onlyMac.push_back(assets[2]);
		onlyMac.push_back(assets[3]);
		const UpdateAssetChoice choice =
			selectUpdateAssets(onlyMac, UpdatePlatform::Linux, NEWER);
		CHECK_FALSE(choice.found);
		CHECK_FALSE(choice.problem.empty());
	}
	SECTION("an archive with no checksum beside it is not usable")
	{
		std::vector<UpdateAsset> noSidecar;
		noSidecar.push_back(assets[2]);
		const UpdateAssetChoice choice =
			selectUpdateAssets(noSidecar, UpdatePlatform::MacOS, NEWER);
		CHECK_FALSE(choice.found);
		CHECK(choice.problem.find("checksum") != std::string::npos);
	}
	SECTION("a version that is not one names no download")
	{
		CHECK(updateArchiveName(UpdatePlatform::MacOS, "local build").empty());
		const UpdateAssetChoice choice =
			selectUpdateAssets(assets, UpdatePlatform::MacOS, "local build");
		CHECK_FALSE(choice.found);
	}
	SECTION("the wanted name carries the filename rendering of the version")
	{
		// '+' does not survive every download path; the token is what the
		// published file is actually called
		CHECK(updateArchiveName(UpdatePlatform::MacOS, NEWER) ==
			std::string("Orkige-macos-") + NEWER_TOKEN + ".zip");
	}
}

// --- the checksum sidecar --------------------------------------------------

TEST_CASE("the checksum sidecar is read for THIS file", "[unit][update]")
{
	const std::string digest(64, 'a');
	const std::string other(64, 'b');

	SECTION("the checking tool's own format")
	{
		CHECK(parseChecksumSidecar(digest + "  Orkige-macos-x.zip\n",
			"Orkige-macos-x.zip") == digest);
	}
	SECTION("the binary-mode marker")
	{
		CHECK(parseChecksumSidecar(digest + " *Orkige-macos-x.zip\n",
			"Orkige-macos-x.zip") == digest);
	}
	SECTION("a leading directory in the name")
	{
		CHECK(parseChecksumSidecar(digest + "  out/Orkige-macos-x.zip\n",
			"Orkige-macos-x.zip") == digest);
	}
	SECTION("several entries, ours picked")
	{
		const std::string text =
			other + "  Orkige-linux-x.tar.gz\n" +
			digest + "  Orkige-macos-x.zip\n";
		CHECK(parseChecksumSidecar(text, "Orkige-macos-x.zip") == digest);
	}
	SECTION("a sidecar naming a different file yields nothing")
	{
		CHECK(parseChecksumSidecar(other + "  Orkige-linux-x.tar.gz\n",
			"Orkige-macos-x.zip").empty());
	}
	SECTION("a bare digest on its own line is accepted")
	{
		CHECK(parseChecksumSidecar(digest + "\n", "Orkige-macos-x.zip") ==
			digest);
	}
	SECTION("upper case is normalised")
	{
		CHECK(parseChecksumSidecar(std::string(64, 'A') +
			"  Orkige-macos-x.zip", "Orkige-macos-x.zip") ==
			std::string(64, 'a'));
	}
	SECTION("nonsense yields nothing rather than a partial digest")
	{
		CHECK(parseChecksumSidecar("", "x.zip").empty());
		CHECK(parseChecksumSidecar("not a digest  x.zip", "x.zip").empty());
		CHECK(parseChecksumSidecar(std::string(63, 'a') + "  x.zip",
			"x.zip").empty());
	}
}

// --- the install location --------------------------------------------------

TEST_CASE("an install location we do not own is refused", "[unit][update]")
{
	InstallLocationFacts facts;
	facts.installPath = "/Applications/Orkige.app";
	facts.containerPath = "/Applications";
	facts.installExists = true;
	facts.containerWritable = true;

	SECTION("ours, present and writable")
	{
		CHECK(judgeInstallLocation(facts) ==
			InstallLocationVerdict::Updatable);
		CHECK(std::string(installLocationReason(
			InstallLocationVerdict::Updatable)).empty());
	}
	SECTION("not there")
	{
		facts.installExists = false;
		CHECK(judgeInstallLocation(facts) == InstallLocationVerdict::Missing);
	}
	SECTION("nothing to replace")
	{
		facts.installPath.clear();
		CHECK(judgeInstallLocation(facts) == InstallLocationVerdict::Missing);
	}
	SECTION("a directory we cannot write")
	{
		facts.containerWritable = false;
		CHECK(judgeInstallLocation(facts) == InstallLocationVerdict::ReadOnly);
	}
	SECTION("a build tree is never rearranged")
	{
		facts.insideBuildTree = true;
		CHECK(judgeInstallLocation(facts) ==
			InstallLocationVerdict::BuildTree);
	}
	SECTION("a translocated copy is refused even where it looks writable")
	{
		facts.installPath = "/private/var/folders/ab/xy/d/AppTranslocation/"
			"1234-5678/d/Orkige.app";
		CHECK(judgeInstallLocation(facts) ==
			InstallLocationVerdict::Translocated);
		// the flag alone is enough too, for a platform whose path shape
		// differs
		facts.installPath = "/Applications/Orkige.app";
		facts.translocated = true;
		CHECK(judgeInstallLocation(facts) ==
			InstallLocationVerdict::Translocated);
	}
	SECTION("every refusal tells the user what to do instead")
	{
		for (InstallLocationVerdict verdict : {
			InstallLocationVerdict::Missing,
			InstallLocationVerdict::ReadOnly,
			InstallLocationVerdict::Translocated,
			InstallLocationVerdict::BuildTree })
		{
			CHECK_FALSE(std::string(installLocationReason(verdict)).empty());
		}
	}
}

TEST_CASE("a translocated path is recognised by its shape", "[unit][update]")
{
	CHECK(isTranslocatedPath("/private/var/folders/x/y/d/AppTranslocation/"
		"AAAA/d/Orkige.app"));
	CHECK_FALSE(isTranslocatedPath("/Applications/Orkige.app"));
	CHECK_FALSE(isTranslocatedPath(""));
}

// --- the swap --------------------------------------------------------------

TEST_CASE("the swap plan is two renames inside one directory",
	"[unit][update]")
{
	const UpdateSwapPlan plan = planUpdateSwap("/Applications/Orkige.app",
		"/tmp/stage/Orkige-macos-x/Orkige.app", "2.0.0-nightly.20260731");
	REQUIRE(plan.valid);
	CHECK(plan.installedPath == "/Applications/Orkige.app");
	CHECK(plan.stagedPath == "/tmp/stage/Orkige-macos-x/Orkige.app");
	// the backup is a SIBLING of what it replaces: both moves stay inside one
	// filesystem, so neither can half-copy
	CHECK(plan.backupPath.find("/Applications/Orkige.app.orkige-previous-")
		== 0);
	CHECK(plan.backupPath != plan.installedPath);
}

TEST_CASE("the swap plan refuses what it cannot describe safely",
	"[unit][update]")
{
	CHECK_FALSE(planUpdateSwap("", "/tmp/x", "v").valid);
	CHECK_FALSE(planUpdateSwap("/Applications/Orkige.app", "", "v").valid);
	CHECK_FALSE(planUpdateSwap("/Applications/Orkige.app",
		"/Applications/Orkige.app", "v").valid);
	// a top-level entry has no container to rename inside
	CHECK_FALSE(planUpdateSwap("/Orkige.app", "/tmp/x", "v").valid);
	CHECK_FALSE(planUpdateSwap("Orkige.app", "/tmp/x", "v").valid);
	CHECK_FALSE(planUpdateSwap("/Applications/Orkige.app", "/tmp/x", "").valid);
	CHECK_FALSE(planUpdateSwap("/Applications/Orkige.app", "/tmp/x",
		"../escape").valid);
	// a trailing separator names the same directory
	const UpdateSwapPlan trailing =
		planUpdateSwap("/opt/orkige/", "/tmp/stage/orkige", "v");
	REQUIRE(trailing.valid);
	CHECK(trailing.installedPath == "/opt/orkige");
	CHECK(trailing.backupPath == "/opt/orkige.orkige-previous-v");
}

TEST_CASE("shell quoting survives the paths people really have",
	"[unit][update]")
{
	CHECK(shellQuotePosix("/Applications/Orkige.app") ==
		"'/Applications/Orkige.app'");
	CHECK(shellQuotePosix("/Users/a b/My Apps/Orkige.app") ==
		"'/Users/a b/My Apps/Orkige.app'");
	// the one escape a single-quoted string needs
	CHECK(shellQuotePosix("/it's/here") == "'/it'\\''s/here'");
	CHECK(shellQuotePosix("; rm -rf /") == "'; rm -rf /'");

	CHECK(shellQuoteWindows("C:\\Program Files\\Orkige") ==
		"\"C:\\Program Files\\Orkige\"");
	// what a command script has no escape for is REFUSED rather than emitted
	CHECK(shellQuoteWindows("C:\\x%PATH%").empty());
	CHECK(shellQuoteWindows("C:\\x\"y").empty());
	CHECK(shellQuoteWindows("C:\\x&y").empty());
}

TEST_CASE("the helper waits, swaps and undoes a half-swap", "[unit][update]")
{
	UpdateHelperSpec spec;
	spec.plan = planUpdateSwap("/Applications/Orkige.app",
		"/tmp/stage/Orkige.app", "2.0.0");
	REQUIRE(spec.plan.valid);
	spec.pid = 4242;
	spec.platform = UpdatePlatform::MacOS;
	spec.relaunchPath = "/Applications/Orkige.app";
	const std::string script = composeUpdateHelperScript(spec);
	REQUIRE_FALSE(script.empty());

	// it waits for the editor's own process before touching anything
	CHECK(script.find("PID=4242") != std::string::npos);
	CHECK(script.find("kill -0") != std::string::npos);
	// the two moves, in that order
	const std::size_t aside =
		script.find("mv \"$INSTALLED\" \"$BACKUP\"");
	const std::size_t moveIn = script.find("mv \"$STAGED\" \"$INSTALLED\"");
	REQUIRE(aside != std::string::npos);
	REQUIRE(moveIn != std::string::npos);
	CHECK(aside < moveIn);
	// the rollback: if the second move fails, the first one is undone
	const std::size_t rollback =
		script.find("mv \"$BACKUP\" \"$INSTALLED\"");
	REQUIRE(rollback != std::string::npos);
	CHECK(rollback > moveIn);
	// and it relaunches, then removes itself
	CHECK(script.find("/usr/bin/open '/Applications/Orkige.app'") !=
		std::string::npos);
	CHECK(script.find("rm -f \"$0\"") != std::string::npos);
	// the paths are quoted, so a space in them is not two arguments
	CHECK(script.find("INSTALLED='/Applications/Orkige.app'") !=
		std::string::npos);
}

TEST_CASE("the helper leaves the app alone when it cannot proceed",
	"[unit][update]")
{
	UpdateHelperSpec spec;
	spec.plan = planUpdateSwap("/opt/orkige", "/tmp/stage/orkige", "2.0.0");
	spec.pid = 9;
	spec.platform = UpdatePlatform::Linux;
	spec.waitTimeoutSeconds = 30;
	const std::string script = composeUpdateHelperScript(spec);
	REQUIRE_FALSE(script.empty());
	// a staged copy that is gone changes nothing
	CHECK(script.find("if [ ! -e \"$STAGED\" ]") != std::string::npos);
	// an editor that never exits changes nothing
	CHECK(script.find("LIMIT=30") != std::string::npos);
	CHECK(script.find("nothing was changed") != std::string::npos);
	// no relaunch was asked for, so none is emitted
	CHECK(script.find("/usr/bin/open") == std::string::npos);
}

TEST_CASE("an invalid plan produces no helper at all", "[unit][update]")
{
	UpdateHelperSpec spec;		// plan.valid is false
	CHECK(composeUpdateHelperScript(spec).empty());

	// a Windows path that cannot be quoted refuses rather than emitting a
	// line that would mean something else
	UpdateHelperSpec windows;
	windows.platform = UpdatePlatform::Windows;
	windows.plan = planUpdateSwap("C:\\Apps\\Ork%ige", "C:\\stage\\orkige",
		"2.0.0");
	REQUIRE(windows.plan.valid);
	CHECK(composeUpdateHelperScript(windows).empty());
}

TEST_CASE("the Windows helper carries the same five steps", "[unit][update]")
{
	UpdateHelperSpec spec;
	spec.platform = UpdatePlatform::Windows;
	spec.plan = planUpdateSwap("C:\\Apps\\Orkige", "C:\\stage\\orkige",
		"2.0.0");
	REQUIRE(spec.plan.valid);
	spec.pid = 77;
	const std::string script = composeUpdateHelperScript(spec);
	REQUIRE_FALSE(script.empty());
	CHECK(script.find("PID eq 77") != std::string::npos);
	const std::size_t aside =
		script.find("move \"C:\\Apps\\Orkige\" \"C:\\Apps\\Orkige"
			".orkige-previous-2.0.0\"");
	const std::size_t moveIn =
		script.find("move \"C:\\stage\\orkige\" \"C:\\Apps\\Orkige\"");
	REQUIRE(aside != std::string::npos);
	REQUIRE(moveIn != std::string::npos);
	CHECK(aside < moveIn);
	// the rollback move comes after the one that can fail
	CHECK(script.rfind("move \"C:\\Apps\\Orkige.orkige-previous-2.0.0\" "
		"\"C:\\Apps\\Orkige\"") > moveIn);
	CHECK(script.find("del \"%~f0\"") != std::string::npos);
}

// --- the platform commands -------------------------------------------------

TEST_CASE("each platform unpacks with a tool it actually has",
	"[unit][update]")
{
	const std::vector<std::string> mac = updateExtractCommand(
		UpdatePlatform::MacOS, "/tmp/a.zip", "/tmp/stage");
	REQUIRE(mac.size() >= 3);
	// the same tool the archive was made with: a bundle's symlinks and
	// executable bits have to survive
	CHECK(mac[0] == "/usr/bin/ditto");
	CHECK(updateExtractCommand(UpdatePlatform::Linux, "/tmp/a.tar.gz",
		"/tmp/stage")[0] == "tar");
	CHECK(updateExtractCommand(UpdatePlatform::Windows, "C:\\a.zip",
		"C:\\stage")[0] == "tar.exe");
	CHECK(updateExtractCommand(UpdatePlatform::MacOS, "", "/tmp").empty());
}

TEST_CASE("the signature is checked where one exists", "[unit][update]")
{
	const std::vector<std::string> verify =
		updateSignatureVerifyCommand(UpdatePlatform::MacOS,
			"/tmp/stage/Orkige.app");
	REQUIRE(verify.size() >= 2);
	CHECK(verify[0] == "/usr/bin/codesign");
	CHECK(verify[1] == "--verify");
	// the stricter question: would the system itself let this run
	CHECK(updateSignatureAssessCommand(UpdatePlatform::MacOS,
		"/tmp/stage/Orkige.app")[0] == "/usr/sbin/spctl");
	// no signature exists on the other platforms' published builds; the
	// updater reports that rather than pretending to have checked one
	CHECK(updateSignatureVerifyCommand(UpdatePlatform::Linux,
		"/tmp/orkige").empty());
	CHECK(updateSignatureVerifyCommand(UpdatePlatform::Windows,
		"C:\\orkige").empty());
	CHECK(updateSignatureAssessCommand(UpdatePlatform::Windows,
		"C:\\orkige").empty());
}

TEST_CASE("the status reports honest progress", "[unit][update]")
{
	UpdateStatus status;
	CHECK_FALSE(status.busy());
	CHECK(status.progress() < 0.0f);		// indeterminate, not a fake zero
	status.stage = UpdateStage::Downloading;
	CHECK(status.busy());
	status.total = 200;
	status.received = 50;
	CHECK(status.progress() == 0.25f);
	status.received = 900;					// a server that undercounted
	CHECK(status.progress() == 1.0f);
	status.stage = UpdateStage::Ready;
	CHECK_FALSE(status.busy());
	CHECK(std::string(updateStageLabel(UpdateStage::Ready)) ==
		"Update ready");
	CHECK(std::string(updateStageLabel(UpdateStage::Idle)).empty());
}
