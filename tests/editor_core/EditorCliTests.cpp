/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	EditorCliTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// The editor's command-line front door, decided purely (@see EditorCli.h).
// The case that matters most is the hazard one: a mistyped subcommand on a
// build server must EXIT, never open a window nobody will ever close.

#include "EditorCli.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using OrkigeEditor::EditorCliCommand;
using OrkigeEditor::EditorCliVerb;
using OrkigeEditor::editorCliUsage;
using OrkigeEditor::editorCliUsageExitCode;
using OrkigeEditor::parseEditorCli;

namespace
{
	//! the argv the editor would see, without argv[0]
	EditorCliCommand parse(std::vector<std::string> const & arguments)
	{
		return parseEditorCli(arguments);
	}
}

TEST_CASE("no arguments launches the editor", "[editorcli]")
{
	const EditorCliCommand command = parse({});
	REQUIRE(command.verb == EditorCliVerb::None);
	REQUIRE_FALSE(command.headless());
	REQUIRE_FALSE(command.usageError);
}

TEST_CASE("the windowed editor's own flags still launch it", "[editorcli]")
{
	// every historical option keeps working EXACTLY as before: the router sees
	// a flag first and hands the whole vector back to the scans in main()
	for (std::vector<std::string> const & arguments :
		std::vector<std::vector<std::string>>{
			{ "--version" },
			{ "-v" },
			{ "--changelog" },
			{ "--mcp-port", "0" },
			{ "--mcp-token-file", "/tmp/t" },
			{ "--claude-ide" },
			{ "-ApplePersistenceIgnoreState", "YES" },
			{ "--some-option-added-next-year" } })
	{
		const EditorCliCommand command = parse(arguments);
		REQUIRE(command.verb == EditorCliVerb::None);
		REQUIRE_FALSE(command.headless());
		REQUIRE_FALSE(command.usageError);
	}
}

TEST_CASE("an unknown subcommand is refused, never launched", "[editorcli]")
{
	// THE HAZARD: `exprot` used to be ignored and the editor opened a window,
	// which on a build server means a hung job and an empty log
	const EditorCliCommand command = parse({ "exprot", "--project", "/g" });
	REQUIRE(command.usageError);
	REQUIRE(command.verb == EditorCliVerb::None);
	// ...and it still stays OFF the window road, which is the whole point
	REQUIRE(command.headless());
	REQUIRE(command.error.find("exprot") != std::string::npos);
	REQUIRE(editorCliUsageExitCode() == 2);
}

TEST_CASE("a bare stray word is refused too", "[editorcli]")
{
	// a project path handed in positionally is not a thing the editor takes;
	// silently opening a window on it would be the same failure
	for (std::string const & word :
		std::vector<std::string>{ "/games/roller", "Export", "EXPORT", "-" })
	{
		const EditorCliCommand command = parse({ word });
		REQUIRE(command.usageError);
		REQUIRE(command.headless());
	}
}

TEST_CASE("export takes a project and a platform", "[editorcli]")
{
	const EditorCliCommand command = parse(
		{ "export", "--project", "/games/roller", "--platform", "macos" });
	REQUIRE_FALSE(command.usageError);
	REQUIRE(command.verb == EditorCliVerb::Export);
	REQUIRE(command.headless());
	REQUIRE(command.projectPath == "/games/roller");
	REQUIRE(command.platform == "macos");
	REQUIRE(command.outputDirectory.empty());
}

TEST_CASE("export without its two required options is a usage error",
	"[editorcli]")
{
	REQUIRE(parse({ "export" }).usageError);
	REQUIRE(parse({ "export", "--project", "/g" }).usageError);
	REQUIRE(parse({ "export", "--platform", "macos" }).usageError);
	// a value-taking option at the end of the vector has no value
	REQUIRE(parse({ "export", "--project" }).usageError);
	// an option this door does not have is named rather than ignored
	const EditorCliCommand unknown = parse(
		{ "export", "--project", "/g", "--platform", "macos", "--turbo" });
	REQUIRE(unknown.usageError);
	REQUIRE(unknown.error.find("--turbo") != std::string::npos);
}

TEST_CASE("export carries the store platforms' credentials", "[editorcli]")
{
	// the signed platforms are reachable here BECAUSE they are CLI-only: a
	// headless agent has no secrets, a build script does
	const EditorCliCommand command = parse({
		"export", "--project", "/g", "--platform", "ios-ipa",
		"--output", "/out",
		"--signing-identity", "Apple Development: A",
		"--provisioning-profile", "/p/dev.mobileprovision",
		"--distribution-identity", "Apple Distribution: A",
		"--distribution-profile", "/p/dist.mobileprovision",
		"--android-keystore", "/k/release.jks",
		"--android-key-alias", "upload",
		"--bundletool", "/opt/bundletool.jar" });
	REQUIRE_FALSE(command.usageError);
	REQUIRE(command.platform == "ios-ipa");
	REQUIRE(command.outputDirectory == "/out");
	REQUIRE(command.credentials.iosIdentity == "Apple Development: A");
	REQUIRE(command.credentials.iosProfile == "/p/dev.mobileprovision");
	REQUIRE(command.credentials.iosDistributionIdentity ==
		"Apple Distribution: A");
	REQUIRE(command.credentials.iosDistributionProfile ==
		"/p/dist.mobileprovision");
	REQUIRE(command.credentials.androidKeystore == "/k/release.jks");
	REQUIRE(command.credentials.androidKeyAlias == "upload");
	REQUIRE(command.credentials.bundletool == "/opt/bundletool.jar");
}

TEST_CASE("test takes a project, and nothing else is required", "[editorcli]")
{
	const EditorCliCommand command =
		parse({ "test", "--project", "/games/roller" });
	REQUIRE_FALSE(command.usageError);
	REQUIRE(command.verb == EditorCliVerb::Test);
	REQUIRE(command.headless());
	REQUIRE(command.projectPath == "/games/roller");
	// the two optional flags stay empty, so nothing is filtered and the
	// runner's own artifact location is left alone
	REQUIRE(command.testFilter.empty());
	REQUIRE(command.reportDirectory.empty());
}

TEST_CASE("test carries the filter and the report directory through",
	"[editorcli]")
{
	const EditorCliCommand command = parse({ "test",
		"--project", "/games/roller.orkproj",
		"--test-filter", "is symmetric",
		"--report-dir", "/ci/out/reports" });
	REQUIRE_FALSE(command.usageError);
	REQUIRE(command.verb == EditorCliVerb::Test);
	REQUIRE(command.projectPath == "/games/roller.orkproj");
	// a filter with a space is ONE value, not two arguments
	REQUIRE(command.testFilter == "is symmetric");
	REQUIRE(command.reportDirectory == "/ci/out/reports");
}

TEST_CASE("test without a project is a usage error, not a run", "[editorcli]")
{
	// a suite belongs to a project (its tests/ directory and its scripts/
	// libraries), never to a loose scene
	const EditorCliCommand bare = parse({ "test" });
	REQUIRE(bare.usageError);
	REQUIRE(bare.headless());
	REQUIRE(bare.error.find("--project") != std::string::npos);

	// a filter alone names no project either
	REQUIRE(parse({ "test", "--test-filter", "movement" }).usageError);
	// an option whose value is missing takes the value of nothing
	REQUIRE(parse({ "test", "--project" }).usageError);
	REQUIRE(parse({ "test", "--project", "/g", "--report-dir" }).usageError);
	// and a flag this door does not have is refused rather than passed on to
	// the player, where it would fail much later and less clearly
	REQUIRE(parse({ "test", "--project", "/g", "--verbose" }).usageError);
	REQUIRE(parse({ "test", "--project", "/g", "--scene", "a.oscene" })
		.usageError);
}

TEST_CASE("fetch-payload takes one id, positionally or named", "[editorcli]")
{
	const EditorCliCommand positional =
		parse({ "fetch-payload", "player-ios-simulator" });
	REQUIRE_FALSE(positional.usageError);
	REQUIRE(positional.verb == EditorCliVerb::FetchPayload);
	REQUIRE(positional.payloadId == "player-ios-simulator");

	const EditorCliCommand named =
		parse({ "fetch-payload", "--payload", "player-android" });
	REQUIRE_FALSE(named.usageError);
	REQUIRE(named.payloadId == "player-android");

	const EditorCliCommand listing = parse({ "fetch-payload", "--list" });
	REQUIRE_FALSE(listing.usageError);
	REQUIRE(listing.listPayloads);
	REQUIRE(listing.payloadId.empty());

	REQUIRE(parse({ "fetch-payload" }).usageError);
	REQUIRE(parse({ "fetch-payload", "a", "b" }).usageError);
	REQUIRE(parse({ "fetch-payload", "--nope" }).usageError);
}

TEST_CASE("the wordless subcommands take nothing", "[editorcli]")
{
	REQUIRE(parse({ "version" }).verb == EditorCliVerb::Version);
	REQUIRE(parse({ "changelog" }).verb == EditorCliVerb::Changelog);
	REQUIRE(parse({ "help" }).verb == EditorCliVerb::Help);
	REQUIRE(parse({ "version", "--extra" }).usageError);
}

TEST_CASE("help is reachable as a word and as a flag", "[editorcli]")
{
	REQUIRE(parse({ "--help" }).verb == EditorCliVerb::Help);
	REQUIRE(parse({ "-h" }).verb == EditorCliVerb::Help);
	REQUIRE(parse({ "export", "--help" }).verb == EditorCliVerb::Help);
	REQUIRE(parse({ "test", "--help" }).verb == EditorCliVerb::Help);
	REQUIRE(parse({ "fetch-payload", "-h" }).verb == EditorCliVerb::Help);
	// and asking for help is never a usage error
	REQUIRE_FALSE(parse({ "--help" }).usageError);
}

TEST_CASE("a near-miss of a real subcommand is still refused", "[editorcli]")
{
	// `test` joining the vocabulary must not soften the hazard rule for words
	// that merely look like it
	for (std::string const & word :
		std::vector<std::string>{ "tset", "tests", "run-tests" })
	{
		const EditorCliCommand command = parse({ word, "--project", "/g" });
		REQUIRE(command.usageError);
		REQUIRE(command.verb == EditorCliVerb::None);
		REQUIRE(command.headless());
		REQUIRE(command.error.find(word) != std::string::npos);
	}
}

TEST_CASE("the usage text stays honest about what v1 covers", "[editorcli]")
{
	const std::string usage = editorCliUsage();
	REQUIRE(usage.find("export") != std::string::npos);
	REQUIRE(usage.find("fetch-payload") != std::string::npos);
	REQUIRE(usage.find("test") != std::string::npos);
	// the exit-code contract callers key on
	REQUIRE(usage.find("0 success") != std::string::npos);
	// ...and it promises NOTHING this process can only do with a live game
	// world of its own. Running a game's tests is not an exception: that road
	// leads to the runtime, which has one (@see EditorCli.h)
	REQUIRE(usage.find("Scene, asset and editor-script operations are NOT") !=
		std::string::npos);
	for (std::string const & absent :
		std::vector<std::string>{ "add-object", "open-scene", "run-script",
			"save-scene" })
	{
		REQUIRE(usage.find(absent) == std::string::npos);
	}
}
