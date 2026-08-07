/********************************************************************
	created:	Thursday 2026/08/06 at 12:00
	filename: 	ExportWindowsSignTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	Signing a Windows game for other people's PCs, asserted on a machine that
	holds no certificate, no Windows SDK and no Windows:
	- the DEFAULT is untouched - asking for nothing plans nothing;
	- the search for signtool.exe, which is the part with no xcrun to lean on:
	  the version ordering (where sorting as text picks a decade-old tool), the
	  candidate paths, the PATH split, and an explicit override that refuses
	  rather than falling back;
	- the exact commands a signed export runs, both credential routes;
	- every refusal, each naming the missing credential and where it may come
	  from, and none of them quoting a value;
	- the redaction that keeps the certificate password off an echoed line,
	  and the deliberate NON-redaction of a thumbprint, which is public.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportWindowsSign.h"

#include <algorithm>

using namespace OrkigeExport;
using Orkige::String;

namespace
{
	//! the plan as one line per command, for the "does it contain / in what
	//! order" questions
	std::vector<String> flattened(std::vector<SignCommand> const & plan)
	{
		std::vector<String> lines;
		for(SignCommand const & command : plan)
		{
			lines.push_back(commandLine(command.arguments));
		}
		return lines;
	}

	std::size_t indexOf(std::vector<String> const & lines, String const & needle)
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

	bool contains(std::vector<String> const & values, String const & needle)
	{
		return std::find(values.begin(), values.end(), needle) != values.end();
	}

	//! the host-separator path a candidate is spelled with, built the way the
	//! code builds it - so the assertion is about the DECISION, not about which
	//! slash this machine prefers
	String kitPath(String const & kitRoot, String const & version,
		String const & architecture)
	{
		return ExportFiles::join(ExportFiles::join(ExportFiles::join(
			ExportFiles::join(kitRoot, "bin"), version), architecture),
			"signtool.exe");
	}

	WindowsSigning storeCertificate()
	{
		WindowsSigning signing;
		signing.method = "store-thumbprint";
		signing.thumbprint = "A1B2C3D4E5F60718293A4B5C6D7E8F9012345678";
		signing.timestampUrl = DEFAULT_TIMESTAMP_URL;
		signing.signtool = "C:/sdk/signtool.exe";
		return signing;
	}

	WindowsSigning certificateFile()
	{
		WindowsSigning signing;
		signing.method = "certificate-file";
		signing.certificate = "C:/keys/publisher.pfx";
		signing.certificatePassword = "hunter2-and-then-some";
		signing.timestampUrl = DEFAULT_TIMESTAMP_URL;
		signing.signtool = "C:/sdk/signtool.exe";
		return signing;
	}
}

//--- the default -------------------------------------------

TEST_CASE("asking for nothing signs nothing, exactly as before",
	"[exporter][windows-sign]")
{
	WindowsSigningOptions options;			// sign == false
	EnvironmentMap environment;
	// a machine that HAS credentials must still produce the unsigned package
	// when nobody asked for a signed one
	environment[WINDOWS_THUMBPRINT_ENV] = "A1B2C3";
	environment[WINDOWS_CERTIFICATE_ENV] = "C:/keys/publisher.pfx";
	environment[WINDOWS_CERTIFICATE_PASSWORD_ENV] = "secret";

	WindowsSigning signing;
	String refusal;
	REQUIRE(resolveWindowsSigning(options, environment, signing, &refusal));
	CHECK(refusal.empty());
	CHECK_FALSE(signing.real());
	CHECK(signing.method.empty());
	CHECK(signing.certificatePassword.empty());
	CHECK(signing.arguments().empty());
}

//--- the credential resolution -----------------------------

TEST_CASE("a signed export needs a certificate, by name",
	"[exporter][windows-sign]")
{
	WindowsSigningOptions options;
	options.sign = true;
	const EnvironmentMap empty;

	WindowsSigning signing;
	String refusal;
	REQUIRE_FALSE(resolveWindowsSigning(options, empty, signing, &refusal));
	CHECK_FALSE(signing.real());
	// both roads, and the flag as well as the variable for each
	CHECK(refusal.find(WINDOWS_THUMBPRINT_ENV) != String::npos);
	CHECK(refusal.find(WINDOWS_CERTIFICATE_ENV) != String::npos);
	CHECK(refusal.find(WINDOWS_CERTIFICATE_PASSWORD_ENV) != String::npos);
	CHECK(refusal.find("--windows-thumbprint") != String::npos);
	CHECK(refusal.find("--windows-certificate") != String::npos);
	// ...and what the person who wanted none of this gets instead
	CHECK(refusal.find("unsigned") != String::npos);
}

TEST_CASE("a certificate file with no password refuses rather than prompting",
	"[exporter][windows-sign]")
{
	WindowsSigningOptions options;
	options.sign = true;
	options.certificate = "C:/keys/publisher.pfx";
	const EnvironmentMap empty;

	WindowsSigning signing;
	String refusal;
	REQUIRE_FALSE(resolveWindowsSigning(options, empty, signing, &refusal));
	CHECK_FALSE(signing.real());
	CHECK(refusal.find(WINDOWS_CERTIFICATE_PASSWORD_ENV) != String::npos);
	// and it points at the route that needs no password at all
	CHECK(refusal.find(WINDOWS_THUMBPRINT_ENV) != String::npos);
	// the refusal never quotes the path it was given a password for... the
	// path is not a secret, but the sentence is about the MISSING value
	CHECK(refusal.find("environment only") != String::npos);
}

TEST_CASE("an explicit credential outranks the environment",
	"[exporter][windows-sign]")
{
	WindowsSigningOptions options;
	options.sign = true;
	options.thumbprint = "  FROM-THE-COMMAND-LINE  ";	// and it is trimmed
	EnvironmentMap environment;
	environment[WINDOWS_THUMBPRINT_ENV] = "from-the-environment";

	WindowsSigning signing;
	String refusal;
	REQUIRE(resolveWindowsSigning(options, environment, signing, &refusal));
	CHECK(signing.thumbprint == "FROM-THE-COMMAND-LINE");
}

TEST_CASE("the machine store wins, because that run holds no secret",
	"[exporter][windows-sign]")
{
	WindowsSigningOptions options;
	options.sign = true;
	EnvironmentMap environment;
	environment[WINDOWS_THUMBPRINT_ENV] = "A1B2C3D4";
	environment[WINDOWS_CERTIFICATE_ENV] = "C:/keys/publisher.pfx";
	environment[WINDOWS_CERTIFICATE_PASSWORD_ENV] = "secret";

	WindowsSigning signing;
	String refusal;
	REQUIRE(resolveWindowsSigning(options, environment, signing, &refusal));
	CHECK(signing.method == "store-thumbprint");
	// the whole point: nothing secret was carried over at all
	CHECK(signing.certificatePassword.empty());
	CHECK(signing.secrets().empty());
	CHECK(contains(signing.arguments(), "/sha1"));
	CHECK_FALSE(contains(signing.arguments(), "/p"));
}

TEST_CASE("the certificate file route is taken when it is the only one",
	"[exporter][windows-sign]")
{
	WindowsSigningOptions options;
	options.sign = true;
	EnvironmentMap environment;
	environment[WINDOWS_CERTIFICATE_ENV] = "C:/keys/publisher.pfx";
	environment[WINDOWS_CERTIFICATE_PASSWORD_ENV] = "secret";

	WindowsSigning signing;
	String refusal;
	REQUIRE(resolveWindowsSigning(options, environment, signing, &refusal));
	CHECK(signing.method == "certificate-file");
	CHECK(signing.certificate == "C:/keys/publisher.pfx");
	CHECK(signing.certificatePassword == "secret");
	// the PASSWORD is the only secret: the path names a file and the
	// thumbprint is a public hash
	REQUIRE(signing.secrets().size() == 1);
	CHECK(signing.secrets()[0] == "secret");
}

TEST_CASE("a password never arrives from a command line",
	"[exporter][windows-sign]")
{
	// there is no options field to put one in, which is the point - this
	// asserts the shape rather than a behaviour: the certificate route needs
	// the environment even when everything else was named explicitly
	WindowsSigningOptions options;
	options.sign = true;
	options.certificate = "C:/keys/publisher.pfx";
	options.thumbprint.clear();
	const EnvironmentMap empty;

	WindowsSigning signing;
	String refusal;
	CHECK_FALSE(resolveWindowsSigning(options, empty, signing, &refusal));
}

TEST_CASE("the timestamp is never optional", "[exporter][windows-sign]")
{
	WindowsSigningOptions options;
	options.sign = true;
	options.thumbprint = "A1B2C3";
	const EnvironmentMap empty;

	WindowsSigning signing;
	String refusal;
	REQUIRE(resolveWindowsSigning(options, empty, signing, &refusal));
	// nobody named one, and a signature with no countersignature dies with the
	// certificate - so there is a default rather than an absence
	CHECK(signing.timestampUrl == DEFAULT_TIMESTAMP_URL);

	EnvironmentMap named;
	named[WINDOWS_TIMESTAMP_URL_ENV] = "http://timestamp.example.invalid";
	WindowsSigning other;
	REQUIRE(resolveWindowsSigning(options, named, other, &refusal));
	CHECK(other.timestampUrl == "http://timestamp.example.invalid");
}

TEST_CASE("an Authenticode flag pointed at another platform is refused by name",
	"[exporter][windows-sign]")
{
	CHECK(windowsSigningPlatformRefusal("windows").empty());

	const String macos = windowsSigningPlatformRefusal("macos");
	REQUIRE_FALSE(macos.empty());
	CHECK(macos.find("--macos-identity") != String::npos);

	const String android = windowsSigningPlatformRefusal("android");
	REQUIRE_FALSE(android.empty());
	CHECK(android.find("android") != String::npos);
}

//--- locating signtool -------------------------------------

TEST_CASE("SDK versions are ordered as numbers, never as text",
	"[exporter][windows-sign]")
{
	// the bug this exists to prevent: sorted as text, "10.0.9000.0" outranks
	// "10.0.22621.0" and the search takes a decade-old tool off a machine that
	// has a current one
	CHECK(windowsSdkVersionLess("10.0.9000.0", "10.0.22621.0"));
	CHECK_FALSE(windowsSdkVersionLess("10.0.22621.0", "10.0.9000.0"));
	CHECK(windowsSdkVersionLess("10.0.19041.0", "10.0.22621.0"));
	CHECK_FALSE(windowsSdkVersionLess("10.0.22621.0", "10.0.22621.0"));
	// a shorter version is the earlier one
	CHECK(windowsSdkVersionLess("10.0", "10.0.1"));
	// a name that is not a version sorts BELOW one that is, so a stray
	// directory beside the SDK versions is never preferred
	CHECK(windowsSdkVersionLess("Redist", "10.0.22621.0"));
	CHECK_FALSE(windowsSdkVersionLess("10.0.22621.0", "Redist"));
}

TEST_CASE("the newest installed SDK is tried first",
	"[exporter][windows-sign]")
{
	const String kit = "C:/Program Files (x86)/Windows Kits/10";
	const std::vector<String> versions = { "10.0.19041.0", "10.0.22621.0",
		"10.0.9000.0", "Redist" };
	const std::vector<String> candidates =
		signtoolCandidatesInKit(kit, versions);
	REQUIRE_FALSE(candidates.empty());

	const std::vector<String> architectures = windowsSigntoolArchitectures();
	REQUIRE_FALSE(architectures.empty());
	const String preferred = architectures.front();

	const std::size_t newest =
		indexOf(candidates, kitPath(kit, "10.0.22621.0", preferred));
	const std::size_t middle =
		indexOf(candidates, kitPath(kit, "10.0.19041.0", preferred));
	const std::size_t oldest =
		indexOf(candidates, kitPath(kit, "10.0.9000.0", preferred));
	CHECK(newest < candidates.size());
	CHECK(newest < middle);
	CHECK(middle < oldest);

	// the unversioned layout is tried AFTER every version, never instead of one
	const String legacy = ExportFiles::join(ExportFiles::join(
		ExportFiles::join(kit, "bin"), preferred), "signtool.exe");
	const std::size_t legacyIndex = indexOf(candidates, legacy);
	CHECK(legacyIndex < candidates.size());
	CHECK(oldest < legacyIndex);

	// and every candidate is the tool, not a directory
	for(String const & candidate : candidates)
	{
		CHECK(candidate.size() > 12);
		CHECK(candidate.substr(candidate.size() - 12) == "signtool.exe");
	}
}

TEST_CASE("the kit roots come from the environment, SDK variable first",
	"[exporter][windows-sign]")
{
	EnvironmentMap environment;
	environment["WindowsSdkDir"] = "D:/WinKits/10/";	// trailing separator
	environment["ProgramFiles(x86)"] = "C:/Program Files (x86)";
	environment["ProgramFiles"] = "C:/Program Files";

	const std::vector<String> roots = windowsKitRoots(environment);
	REQUIRE(roots.size() == 3);
	CHECK(roots[0] == "D:/WinKits/10");		// the doubled separator is gone
	CHECK(roots[1] == ExportFiles::join(
		ExportFiles::join("C:/Program Files (x86)", "Windows Kits"), "10"));
	CHECK(roots[2] == ExportFiles::join(
		ExportFiles::join("C:/Program Files", "Windows Kits"), "10"));

	CHECK(windowsKitRoots(EnvironmentMap()).empty());
}

TEST_CASE("PATH is split and probed, never handed over as a bare name",
	"[exporter][windows-sign]")
{
	// ';' is the separator on the system this searches - a Windows path holds
	// a drive colon, so ':' cannot be one
	const std::vector<String> candidates = signtoolCandidatesOnPath(
		"C:/Windows/system32;;D:/tools ; ");
	REQUIRE(candidates.size() == 2);
	CHECK(candidates[0] == ExportFiles::join("C:/Windows/system32",
		"signtool.exe"));
	CHECK(candidates[1] == ExportFiles::join("D:/tools", "signtool.exe"));
	CHECK(signtoolCandidatesOnPath("").empty());
}

TEST_CASE("the search finds the tool in an installed SDK",
	"[exporter][windows-sign]")
{
	EnvironmentMap environment;
	environment["ProgramFiles(x86)"] = "C:/Program Files (x86)";
	const String kit = ExportFiles::join(ExportFiles::join(
		"C:/Program Files (x86)", "Windows Kits"), "10");
	const String architecture = windowsSigntoolArchitectures().front();
	const String wanted = kitPath(kit, "10.0.22621.0", architecture);

	const FileProbe exists = [&wanted](String const & path)
	{
		return path == wanted;
	};
	const DirectoryLister lister = [&kit](String const & directory)
	{
		if(directory == ExportFiles::join(kit, "bin"))
		{
			return std::vector<String>{ "10.0.19041.0", "10.0.22621.0" };
		}
		return std::vector<String>();
	};
	String refusal;
	CHECK(locateSigntool(String(), environment, exists, lister, &refusal) ==
		wanted);
	CHECK(refusal.empty());
}

TEST_CASE("the search falls through to PATH, then refuses with what to install",
	"[exporter][windows-sign]")
{
	EnvironmentMap environment;
	environment["ProgramFiles(x86)"] = "C:/Program Files (x86)";
	environment["PATH"] = "C:/nowhere;D:/tools";
	const String onPath = ExportFiles::join("D:/tools", "signtool.exe");

	const DirectoryLister empty = [](String const &)
	{
		return std::vector<String>();
	};
	String refusal;
	CHECK(locateSigntool(String(), environment,
		[&onPath](String const & path) { return path == onPath; },
		empty, &refusal) == onPath);
	CHECK(refusal.empty());

	// ...and a machine with neither is told what to install, not handed
	// "could not run 'signtool'"
	CHECK(locateSigntool(String(), environment,
		[](String const &) { return false; }, empty, &refusal).empty());
	REQUIRE_FALSE(refusal.empty());
	CHECK(refusal.find("Windows SDK") != String::npos);
	CHECK(refusal.find(SIGNTOOL_ENV) != String::npos);
}

TEST_CASE("a named tool that is not there refuses instead of falling back",
	"[exporter][windows-sign]")
{
	EnvironmentMap environment;
	environment["PATH"] = "D:/tools";
	const String onPath = ExportFiles::join("D:/tools", "signtool.exe");

	String refusal;
	// the tool IS findable by the search, and that must not rescue a run that
	// named a different one: signing with a tool nobody asked for is exactly
	// the silent substitution the check exists to prevent
	const String found = locateSigntool("E:/custom/signtool.exe", environment,
		[&onPath](String const & path) { return path == onPath; },
		[](String const &) { return std::vector<String>(); }, &refusal);
	CHECK(found.empty());
	REQUIRE_FALSE(refusal.empty());
	CHECK(refusal.find("E:/custom/signtool.exe") != String::npos);
	CHECK(refusal.find(SIGNTOOL_ENV) != String::npos);

	// ...and a named tool that IS there is taken as given
	String none;
	CHECK(locateSigntool("E:/custom/signtool.exe", environment,
		[](String const &) { return true; },
		[](String const &) { return std::vector<String>(); }, &none) ==
		"E:/custom/signtool.exe");
	CHECK(none.empty());
}

//--- the commands ------------------------------------------

TEST_CASE("a signature states both digests and an RFC 3161 timestamp",
	"[exporter][windows-sign]")
{
	const std::vector<String> arguments = signtoolSignArguments(
		"C:/sdk/signtool.exe", "C:/out/Game.exe", storeCertificate());
	REQUIRE(arguments.size() >= 2);
	CHECK(arguments[0] == "C:/sdk/signtool.exe");
	CHECK(arguments[1] == "sign");
	CHECK(contains(arguments, "/fd"));
	CHECK(contains(arguments, "/td"));
	// SHA-1 is not accepted for either any more, and defaulting is not the
	// same as choosing
	CHECK(std::count(arguments.begin(), arguments.end(), String("SHA256")) == 2);
	// /tr is the RFC 3161 form; the older /t protocol no longer produces a
	// timestamp an operating system accepts
	CHECK(contains(arguments, "/tr"));
	CHECK_FALSE(contains(arguments, "/t"));
	CHECK(contains(arguments, DEFAULT_TIMESTAMP_URL));
	// the target is last
	CHECK(arguments.back() == "C:/out/Game.exe");
}

TEST_CASE("each credential route produces its own command shape",
	"[exporter][windows-sign]")
{
	const std::vector<String> store = signtoolSignArguments("signtool.exe",
		"Game.exe", storeCertificate());
	CHECK(contains(store, "/sha1"));
	CHECK(contains(store, "A1B2C3D4E5F60718293A4B5C6D7E8F9012345678"));
	CHECK_FALSE(contains(store, "/f"));
	CHECK_FALSE(contains(store, "/p"));

	const std::vector<String> file = signtoolSignArguments("signtool.exe",
		"Game.exe", certificateFile());
	CHECK(contains(file, "/f"));
	CHECK(contains(file, "C:/keys/publisher.pfx"));
	CHECK(contains(file, "/p"));
	CHECK(contains(file, "hunter2-and-then-some"));
	CHECK_FALSE(contains(file, "/sha1"));
}

TEST_CASE("verification asks for the Authenticode policy",
	"[exporter][windows-sign]")
{
	const std::vector<String> arguments =
		signtoolVerifyArguments("C:/sdk/signtool.exe", "C:/out/Game.exe");
	REQUIRE(arguments.size() == 4);
	CHECK(arguments[0] == "C:/sdk/signtool.exe");
	CHECK(arguments[1] == "verify");
	// without /pa signtool verifies against the DRIVER policy, which a
	// perfectly good application signature fails
	CHECK(arguments[2] == "/pa");
	CHECK(arguments[3] == "C:/out/Game.exe");
}

//--- the plan ----------------------------------------------

TEST_CASE("the plan signs then verifies every file it packaged",
	"[exporter][windows-sign]")
{
	WindowsSignPlanInputs inputs;
	inputs.signtool = "C:/sdk/signtool.exe";
	inputs.executable = "C:/out/Game/Game.exe";
	inputs.libraries = { "C:/out/Game/helper.dll" };
	inputs.signing = storeCertificate();

	const std::vector<SignCommand> plan = windowsSignPlan(inputs);
	const std::vector<String> lines = flattened(plan);
	// a DLL is code and carries its own signature - there is no seal over a
	// directory here, so both are signed and each is read back
	REQUIRE(plan.size() == 4);
	CHECK(indexOf(lines, "sign") < lines.size());
	CHECK(lines[0].find("helper.dll") != String::npos);
	CHECK(lines[1].find("verify") != String::npos);
	CHECK(lines[2].find("Game.exe") != String::npos);
	CHECK(lines[3].find("verify") != String::npos);

	// a package with no companion libraries is the normal one: the closure is
	// linked statically
	inputs.libraries.clear();
	const std::vector<SignCommand> lean = windowsSignPlan(inputs);
	REQUIRE(lean.size() == 2);
	CHECK(flattened(lean)[0].find("Game.exe") != String::npos);
	CHECK(flattened(lean)[1].find("verify") != String::npos);
}

TEST_CASE("a password never reaches an echoed log line",
	"[exporter][windows-sign]")
{
	WindowsSignPlanInputs inputs;
	inputs.signtool = "C:/sdk/signtool.exe";
	inputs.executable = "C:/out/Game/Game.exe";
	inputs.signing = certificateFile();

	const std::vector<SignCommand> plan = windowsSignPlan(inputs);
	REQUIRE_FALSE(plan.empty());
	const String shown = redactedCommandLine(plan[0]);
	CHECK(shown.find("hunter2-and-then-some") == String::npos);
	CHECK(shown.find("<redacted>") != String::npos);
	// ...while everything a person needs in order to debug the step survives
	CHECK(shown.find("C:/keys/publisher.pfx") != String::npos);
	CHECK(shown.find("/fd SHA256") != String::npos);
	CHECK(shown.find("Game.exe") != String::npos);
	// the un-redacted line is what actually runs
	CHECK(commandLine(plan[0].arguments).find("hunter2-and-then-some") !=
		String::npos);
}

TEST_CASE("a thumbprint is not redacted, because it is not a secret",
	"[exporter][windows-sign]")
{
	WindowsSignPlanInputs inputs;
	inputs.signtool = "C:/sdk/signtool.exe";
	inputs.executable = "C:/out/Game/Game.exe";
	inputs.signing = storeCertificate();

	const std::vector<SignCommand> plan = windowsSignPlan(inputs);
	REQUIRE_FALSE(plan.empty());
	CHECK(plan[0].secrets.empty());
	const String shown = redactedCommandLine(plan[0]);
	// a public hash of a public certificate; hiding it would only make the
	// log useless for telling which certificate was used
	CHECK(shown.find("A1B2C3D4E5F60718293A4B5C6D7E8F9012345678") !=
		String::npos);
	CHECK(shown.find("<redacted>") == String::npos);
}
