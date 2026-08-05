/********************************************************************
	created:	Wednesday 2026/08/05 at 12:00
	filename: 	ExportMacosSignTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	Signing a macOS game bundle for other people's Macs, asserted on a machine
	that holds no certificate, no Apple account and no network:
	- the DEFAULT is untouched - asking for nothing plans nothing;
	- the exact command sequence a signed and a notarized export runs, in the
	  order that makes a bundle seal hold (nested code first, staple last);
	- every refusal, each naming the missing credential and where it may come
	  from, and none of them quoting a value;
	- the redaction that keeps a credential off an echoed command line;
	- the verdict read out of Apple's own JSON, where "we could not tell" and
	  "Apple said yes" must never be the same answer.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportMacosSign.h"

#include <algorithm>

using namespace OrkigeExport;
using Orkige::String;

namespace
{
	//! the whole plan as one string, for the "does it contain / in what order"
	//! questions
	std::vector<String> flattened(std::vector<SignCommand> const & plan)
	{
		std::vector<String> lines;
		for(SignCommand const & command : plan)
		{
			lines.push_back(commandLine(command.arguments));
		}
		return lines;
	}

	std::size_t indexOf(std::vector<String> const & lines,
		String const & needle)
	{
		for(std::size_t index = 0; index < lines.size(); ++index)
		{
			if(lines[index].find(needle) != String::npos)
			{
				return index;
			}
		}
		return lines.size();
	}

	MacosSigning developerId()
	{
		MacosSigning signing;
		signing.identity = "Developer ID Application: Someone (TEAM123456)";
		signing.notaryTimeout = DEFAULT_NOTARY_TIMEOUT;
		return signing;
	}
}

TEST_CASE("asking for nothing signs ad-hoc, exactly as before",
	"[unit][export][macossign]")
{
	MacosSigningOptions options;
	CHECK_FALSE(options.requested());

	MacosSigning signing;
	String refusal = "untouched";
	CHECK(resolveMacosSigning(options, EnvironmentMap(), signing, &refusal));
	CHECK(refusal == "untouched");		// no refusal was written
	CHECK_FALSE(signing.real());
	CHECK_FALSE(signing.notarize);

	// ...and even with a certificate sitting in the environment, nothing is
	// signed with it: the default export is opt-in-free by construction
	EnvironmentMap environment;
	environment[MACOS_SIGNING_IDENTITY_ENV] = "Developer ID Application: Me";
	CHECK(resolveMacosSigning(options, environment, signing, 0));
	CHECK_FALSE(signing.real());

	// the ad-hoc argv is the four-word command an export has always run
	CHECK(codesignArguments("/out/Game.app", "", "") ==
		std::vector<String>({ "codesign", "--force", "--sign", "-",
			"/out/Game.app" }));
}

TEST_CASE("a signed export needs an identity, by name",
	"[unit][export][macossign]")
{
	MacosSigningOptions options;
	options.sign = true;
	MacosSigning signing;
	String refusal;
	CHECK_FALSE(resolveMacosSigning(options, EnvironmentMap(), signing,
		&refusal));
	CHECK(refusal.find(MACOS_SIGNING_IDENTITY_ENV) != String::npos);
	CHECK(refusal.find("--macos-identity") != String::npos);
	CHECK(refusal.find("Project Settings") != String::npos);
	CHECK_FALSE(signing.real());
}

TEST_CASE("an explicit identity outranks the environment",
	"[unit][export][macossign]")
{
	EnvironmentMap environment;
	environment[MACOS_SIGNING_IDENTITY_ENV] = "Developer ID Application: env";
	environment[MACOS_KEYCHAIN_ENV] = "/keys/build.keychain";

	MacosSigningOptions options;
	options.sign = true;
	MacosSigning signing;
	CHECK(resolveMacosSigning(options, environment, signing, 0));
	CHECK(signing.identity == "Developer ID Application: env");
	CHECK(signing.keychain == "/keys/build.keychain");
	CHECK_FALSE(signing.notarize);
	CHECK(signing.notaryTimeout == DEFAULT_NOTARY_TIMEOUT);

	options.identity = "Developer ID Application: argument";
	CHECK(resolveMacosSigning(options, environment, signing, 0));
	CHECK(signing.identity == "Developer ID Application: argument");

	// whitespace is not a credential
	options.identity = "   ";
	CHECK(resolveMacosSigning(options, environment, signing, 0));
	CHECK(signing.identity == "Developer ID Application: env");
}

TEST_CASE("the API key route wins when it is complete",
	"[unit][export][macossign]")
{
	EnvironmentMap environment;
	environment[MACOS_SIGNING_IDENTITY_ENV] = "Developer ID Application: me";
	environment[NOTARY_KEY_ENV] = "/keys/AuthKey.p8";
	environment[NOTARY_KEY_ID_ENV] = "ABCDE12345";
	environment[NOTARY_ISSUER_ENV] = "69a6de70-issuer";
	// a complete Apple ID login beside it: the revocable key still wins
	environment[NOTARY_APPLE_ID_ENV] = "someone@example.com";
	environment[NOTARY_APP_PASSWORD_ENV] = "abcd-efgh-ijkl-mnop";
	environment[NOTARY_TEAM_ID_ENV] = "TEAM123456";

	MacosSigningOptions options;
	options.notarize = true;
	options.sign = true;
	MacosSigning signing;
	REQUIRE(resolveMacosSigning(options, environment, signing, 0));
	CHECK(signing.notary.method == "api-key");
	CHECK(signing.notary.arguments() == std::vector<String>({ "--key",
		"/keys/AuthKey.p8", "--key-id", "ABCDE12345", "--issuer",
		"69a6de70-issuer" }));
	// the key FILE's path is not a secret (it names a file, it is not the key)
	const std::vector<String> secrets = signing.notary.secrets();
	CHECK(std::find(secrets.begin(), secrets.end(), "/keys/AuthKey.p8") ==
		secrets.end());
	CHECK(std::find(secrets.begin(), secrets.end(), "ABCDE12345") !=
		secrets.end());
}

TEST_CASE("the Apple ID route is taken when it is the complete one",
	"[unit][export][macossign]")
{
	EnvironmentMap environment;
	environment[MACOS_SIGNING_IDENTITY_ENV] = "Developer ID Application: me";
	environment[NOTARY_APPLE_ID_ENV] = "someone@example.com";
	environment[NOTARY_APP_PASSWORD_ENV] = "abcd-efgh-ijkl-mnop";
	environment[NOTARY_TEAM_ID_ENV] = "TEAM123456";

	MacosSigningOptions options;
	options.notarize = true;
	MacosSigning signing;
	REQUIRE(resolveMacosSigning(options, environment, signing, 0));
	CHECK(signing.notary.method == "apple-id");
	const std::vector<String> secrets = signing.notary.secrets();
	CHECK(std::find(secrets.begin(), secrets.end(), "abcd-efgh-ijkl-mnop") !=
		secrets.end());

	// the password has no argument and no settings key: the environment is the
	// only place it may come from
	MacosSigningOptions named;
	named.notarize = true;
	named.notaryAppleId = "someone@example.com";
	named.notaryTeamId = "TEAM123456";
	EnvironmentMap identityOnly;
	identityOnly[MACOS_SIGNING_IDENTITY_ENV] = "Developer ID Application: me";
	String refusal;
	CHECK_FALSE(resolveMacosSigning(named, identityOnly, signing, &refusal));
	CHECK(refusal.find(NOTARY_APP_PASSWORD_ENV) != String::npos);
}

TEST_CASE("a half-configured notarization login names what is missing",
	"[unit][export][macossign]")
{
	EnvironmentMap environment;
	environment[MACOS_SIGNING_IDENTITY_ENV] = "Developer ID Application: me";
	environment[NOTARY_KEY_ENV] = "/keys/AuthKey.p8";
	environment[NOTARY_KEY_ID_ENV] = "ABCDE12345";
	// ...and no issuer

	MacosSigningOptions options;
	options.notarize = true;
	MacosSigning signing;
	String refusal;
	CHECK_FALSE(resolveMacosSigning(options, environment, signing, &refusal));
	CHECK(refusal.find(NOTARY_ISSUER_ENV) != String::npos);
	// never the value, only the name of what carries it
	CHECK(refusal.find("ABCDE12345") == String::npos);

	// nothing configured at all names BOTH routes
	EnvironmentMap identityOnly;
	identityOnly[MACOS_SIGNING_IDENTITY_ENV] = "Developer ID Application: me";
	refusal.clear();
	CHECK_FALSE(resolveMacosSigning(options, identityOnly, signing, &refusal));
	CHECK(refusal.find(NOTARY_KEY_ENV) != String::npos);
	CHECK(refusal.find(NOTARY_APPLE_ID_ENV) != String::npos);
}

TEST_CASE("a signing flag pointed at another platform is refused by name",
	"[unit][export][macossign]")
{
	CHECK(macosSigningPlatformRefusal("macos").empty());
	CHECK_FALSE(macosSigningPlatformRefusal("ios-ipa").empty());
	CHECK(macosSigningPlatformRefusal("ios").find("ios-signing") !=
		String::npos);
	CHECK(macosSigningPlatformRefusal("android").find("android") !=
		String::npos);
}

TEST_CASE("a real signature carries the hardened runtime and a timestamp",
	"[unit][export][macossign]")
{
	const std::vector<String> arguments = codesignArguments("/out/Game.app",
		"Developer ID Application: me", "");
	CHECK(arguments == std::vector<String>({ "codesign", "--force", "--sign",
		"Developer ID Application: me", "--timestamp", "--options", "runtime",
		"/out/Game.app" }));
	// NO entitlements: an unneeded one is signed-in permission nobody asked for
	CHECK(std::find(arguments.begin(), arguments.end(), "--entitlements") ==
		arguments.end());

	const std::vector<String> keyed = codesignArguments("/out/Game.app",
		"Developer ID Application: me", "/keys/build.keychain");
	CHECK(std::find(keyed.begin(), keyed.end(), "/keys/build.keychain") !=
		keyed.end());

	// a real signature is verified the way Gatekeeper checks it
	CHECK(codesignVerifyArguments("/out/Game.app", true) ==
		std::vector<String>({ "codesign", "--verify", "--strict",
			"--verbose=2", "/out/Game.app" }));
}

TEST_CASE("the signed plan signs nested code before the bundle",
	"[unit][export][macossign]")
{
	MacosSignPlanInputs inputs;
	inputs.bundle = "/out/Game.app";
	inputs.nested = { "/out/Game.app/Contents/Frameworks/libSDL3.dylib",
		"/out/Game.app/Contents/Frameworks/libogg.dylib" };
	inputs.signing = developerId();

	const std::vector<String> lines = flattened(macosSignPlan(inputs));
	REQUIRE(lines.size() == 4);
	CHECK(indexOf(lines, "libSDL3.dylib") < indexOf(lines, "/out/Game.app "));
	CHECK(indexOf(lines, "libogg.dylib") < lines.size() - 2);
	// the bundle seal, then the read-back
	CHECK(lines[2].find("--options runtime") != String::npos);
	CHECK(lines[3].find("--verify") != String::npos);
	// nothing was submitted anywhere
	CHECK(indexOf(lines, "notarytool") == lines.size());
	CHECK(indexOf(lines, "stapler") == lines.size());
}

TEST_CASE("the notarized plan submits, then staples, then asks Gatekeeper",
	"[unit][export][macossign]")
{
	MacosSignPlanInputs inputs;
	inputs.bundle = "/out/Game.app";
	inputs.submissionZip = "/out/Game-notarize.zip";
	inputs.signing = developerId();
	inputs.signing.notarize = true;
	inputs.signing.notary.method = "api-key";
	inputs.signing.notary.keyPath = "/keys/AuthKey.p8";
	inputs.signing.notary.keyId = "ABCDE12345";
	inputs.signing.notary.issuer = "69a6de70-issuer";

	const std::vector<SignCommand> plan = macosSignPlan(inputs);
	const std::vector<String> lines = flattened(plan);
	const std::size_t seal = indexOf(lines, "--options runtime");
	const std::size_t archive = indexOf(lines, "ditto");
	const std::size_t submit = indexOf(lines, "notarytool submit");
	const std::size_t staple = indexOf(lines, "stapler staple");
	const std::size_t validate = indexOf(lines, "stapler validate");
	const std::size_t assess = indexOf(lines, "spctl");
	CHECK(seal < archive);
	CHECK(archive < submit);
	// the TICKET is attached only after a verdict - never before it
	CHECK(submit < staple);
	CHECK(staple < validate);
	CHECK(validate < assess);
	CHECK(assess == lines.size() - 1);

	// the archive is made with ditto: a zip writer would drop the symlinks and
	// executable bits, and Apple would assess something that is not the app
	CHECK(lines[archive].find("--sequesterRsrc") != String::npos);
	CHECK(lines[archive].find("--keepParent") != String::npos);
	// the submission waits, bounded, and reports as JSON
	CHECK(lines[submit].find("--wait") != String::npos);
	CHECK(lines[submit].find(DEFAULT_NOTARY_TIMEOUT) != String::npos);
	CHECK(lines[submit].find("--output-format json") != String::npos);

	// the ONE credentialed step names its own secrets, so the echoed line can
	// never carry one
	const SignCommand & submitCommand = plan[submit];
	CHECK_FALSE(submitCommand.secrets.empty());
	const String echoed = redactedCommandLine(submitCommand);
	CHECK(echoed.find("ABCDE12345") == String::npos);
	CHECK(echoed.find("69a6de70-issuer") == String::npos);
	CHECK(echoed.find("<redacted>") != String::npos);
	// the key FILE path stays visible - it names a file, and a step nobody can
	// see is a step nobody can debug
	CHECK(echoed.find("/keys/AuthKey.p8") != String::npos);
}

TEST_CASE("a password never reaches an echoed log line",
	"[unit][export][macossign]")
{
	NotaryCredentials notary;
	notary.method = "apple-id";
	notary.appleId = "someone@example.com";
	notary.appPassword = "abcd-efgh-ijkl-mnop";
	notary.teamId = "TEAM123456";

	SignCommand command;
	command.arguments = notarytoolSubmitArguments("/out/Game.zip", notary,
		"30m");
	command.secrets = notary.secrets();
	const String echoed = redactedCommandLine(command);
	CHECK(echoed.find("abcd-efgh-ijkl-mnop") == String::npos);
	CHECK(echoed.find("someone@example.com") == String::npos);
	CHECK(echoed.find("TEAM123456") == String::npos);
	CHECK(echoed.find("notarytool submit") != String::npos);

	// the log fetch carries the same credentials and the same redaction
	SignCommand detail;
	detail.arguments = notarytoolLogArguments("12345-abcde", notary);
	detail.secrets = notary.secrets();
	CHECK(redactedCommandLine(detail).find("abcd-efgh-ijkl-mnop") ==
		String::npos);
	CHECK(detail.arguments[3] == "12345-abcde");
}

TEST_CASE("Apple's verdict is read from the payload, never assumed",
	"[unit][export][macossign]")
{
	String id;
	String status;
	CHECK(notarySubmissionVerdict(
		"{\"id\":\"a-b-c\",\"status\":\"Accepted\"}", id, status));
	CHECK(id == "a-b-c");
	CHECK(status == "Accepted");

	CHECK_FALSE(notarySubmissionVerdict(
		"{\"id\":\"a-b-c\",\"status\":\"Invalid\"}", id, status));
	CHECK(id == "a-b-c");		// the id survives: the log needs it
	CHECK(status == "Invalid");

	// output that is not the expected payload is NOT an acceptance
	CHECK_FALSE(notarySubmissionVerdict("", id, status));
	CHECK_FALSE(notarySubmissionVerdict("Processing complete", id, status));
	CHECK_FALSE(notarySubmissionVerdict("[]", id, status));
	CHECK_FALSE(notarySubmissionVerdict("{\"status\":\"\"}", id, status));
}
