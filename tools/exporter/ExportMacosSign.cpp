/********************************************************************
	created:	Wednesday 2026/08/05 at 12:00
	filename: 	ExportMacosSign.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportMacosSign.h"

#include "ExportFiles.h"

#include <core_debugnet/Json.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace OrkigeExport
{
	const char * const MACOS_SIGNING_IDENTITY_ENV =
		"ORKIGE_MACOS_SIGNING_IDENTITY";
	const char * const MACOS_KEYCHAIN_ENV = "ORKIGE_MACOS_KEYCHAIN";
	const char * const NOTARY_KEY_ENV = "ORKIGE_NOTARY_KEY";
	const char * const NOTARY_KEY_ID_ENV = "ORKIGE_NOTARY_KEY_ID";
	const char * const NOTARY_ISSUER_ENV = "ORKIGE_NOTARY_ISSUER_ID";
	const char * const NOTARY_APPLE_ID_ENV = "ORKIGE_NOTARY_APPLE_ID";
	const char * const NOTARY_APP_PASSWORD_ENV = "ORKIGE_NOTARY_APP_PASSWORD";
	const char * const NOTARY_TEAM_ID_ENV = "ORKIGE_NOTARY_TEAM_ID";
	const char * const NOTARY_TIMEOUT_ENV = "ORKIGE_NOTARY_TIMEOUT";
	const char * const DEFAULT_NOTARY_TIMEOUT = "30m";

	namespace
	{
		using Orkige::String;

		//---------------------------------------------------------
		String trimmed(String const & text)
		{
			std::size_t first = 0;
			while(first < text.size() && (text[first] == ' ' ||
				text[first] == '\t' || text[first] == '\r' ||
				text[first] == '\n'))
			{
				++first;
			}
			std::size_t last = text.size();
			while(last > first && (text[last - 1] == ' ' ||
				text[last - 1] == '\t' || text[last - 1] == '\r' ||
				text[last - 1] == '\n'))
			{
				--last;
			}
			return text.substr(first, last - first);
		}
		//---------------------------------------------------------
		//! an explicit value, else the environment, else "" - the ONE
		//! precedence every credential here follows
		String resolved(String const & explicitValue, const char * variable,
			EnvironmentMap const & environment)
		{
			const String given = trimmed(explicitValue);
			if(!given.empty())
			{
				return given;
			}
			const EnvironmentMap::const_iterator found =
				environment.find(variable);
			return found == environment.end() ? String()
				: trimmed(found->second);
		}
		//---------------------------------------------------------
		//! "A, B and C" - the missing-credential list a refusal names
		String listed(std::vector<String> const & names)
		{
			String text;
			for(std::size_t index = 0; index < names.size(); ++index)
			{
				if(index > 0)
				{
					text += (index + 1 == names.size()) ? " and " : ", ";
				}
				text += names[index];
			}
			return text;
		}
		//---------------------------------------------------------
		bool report(String * error, String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		void emit(ExportLog const & log, String const & message)
		{
			if(log)
			{
				log(message);
			}
		}
	}
	//---------------------------------------------------------
	std::vector<String> NotaryCredentials::arguments() const
	{
		if(this->method == "api-key")
		{
			return { "--key", this->keyPath, "--key-id", this->keyId,
				"--issuer", this->issuer };
		}
		if(this->method == "apple-id")
		{
			return { "--apple-id", this->appleId, "--password",
				this->appPassword, "--team-id", this->teamId };
		}
		return std::vector<String>();
	}
	//---------------------------------------------------------
	std::vector<String> NotaryCredentials::secrets() const
	{
		std::vector<String> values;
		const String candidates[] = { this->keyId, this->issuer, this->appleId,
			this->appPassword, this->teamId };
		for(String const & value : candidates)
		{
			if(!value.empty())
			{
				values.push_back(value);
			}
		}
		return values;
	}
	//---------------------------------------------------------
	bool resolveMacosSigning(MacosSigningOptions const & options,
		EnvironmentMap const & environment, MacosSigning & outSigning,
		String * outRefusal)
	{
		outSigning = MacosSigning();
		if(!options.requested())
		{
			// the default export: ad-hoc, exactly as it has always been, and
			// nothing below is consulted
			return true;
		}
		const String identity = resolved(options.identity,
			MACOS_SIGNING_IDENTITY_ENV, environment);
		if(identity.empty())
		{
			return report(outRefusal, String("a signed macOS export needs a "
				"Developer ID Application identity, and none is set. Name one "
				"with --macos-identity, set ") + MACOS_SIGNING_IDENTITY_ENV +
				", or fill it in under Build > Project Settings > Signing "
				"(macOS / Distribution). An unsigned export needs none of this "
				"- it is signed ad-hoc and runs on this machine. See "
				"Docs/desktop-export.md");
		}
		outSigning.identity = identity;
		outSigning.keychain = resolved(String(), MACOS_KEYCHAIN_ENV,
			environment);
		outSigning.notaryTimeout = resolved(String(), NOTARY_TIMEOUT_ENV,
			environment);
		if(outSigning.notaryTimeout.empty())
		{
			outSigning.notaryTimeout = DEFAULT_NOTARY_TIMEOUT;
		}
		if(!options.notarize)
		{
			return true;
		}
		outSigning.notarize = true;

		// TWO routes, and a HALF-configured one is never silently ignored: it
		// is the shape a person is in the middle of setting up, and saying
		// which value is missing is the whole difference between a five-second
		// fix and an afternoon
		const String keyPath = resolved(options.notaryKey, NOTARY_KEY_ENV,
			environment);
		const String keyId = resolved(options.notaryKeyId, NOTARY_KEY_ID_ENV,
			environment);
		const String issuer = resolved(options.notaryIssuer, NOTARY_ISSUER_ENV,
			environment);
		const String appleId = resolved(options.notaryAppleId,
			NOTARY_APPLE_ID_ENV, environment);
		// the password is environment-only, deliberately: it is the one value
		// here that is a password, and a command line carrying one is readable
		// by every process on the machine
		const String password = resolved(String(), NOTARY_APP_PASSWORD_ENV,
			environment);
		const String teamId = resolved(options.notaryTeamId,
			NOTARY_TEAM_ID_ENV, environment);
		if(!keyPath.empty() && !keyId.empty() && !issuer.empty())
		{
			// the API key wins when both are complete: it is revocable on its
			// own, without touching an Apple ID
			outSigning.notary.method = "api-key";
			outSigning.notary.keyPath = keyPath;
			outSigning.notary.keyId = keyId;
			outSigning.notary.issuer = issuer;
			return true;
		}
		if(!appleId.empty() && !password.empty() && !teamId.empty())
		{
			outSigning.notary.method = "apple-id";
			outSigning.notary.appleId = appleId;
			outSigning.notary.appPassword = password;
			outSigning.notary.teamId = teamId;
			return true;
		}
		std::vector<String> apiMissing;
		if(keyPath.empty()) { apiMissing.push_back(NOTARY_KEY_ENV); }
		if(keyId.empty()) { apiMissing.push_back(NOTARY_KEY_ID_ENV); }
		if(issuer.empty()) { apiMissing.push_back(NOTARY_ISSUER_ENV); }
		std::vector<String> idMissing;
		if(appleId.empty()) { idMissing.push_back(NOTARY_APPLE_ID_ENV); }
		if(password.empty()) { idMissing.push_back(NOTARY_APP_PASSWORD_ENV); }
		if(teamId.empty()) { idMissing.push_back(NOTARY_TEAM_ID_ENV); }
		if(apiMissing.size() < 3)
		{
			return report(outRefusal, "notarization is asked for and an App "
				"Store Connect key is half configured: " + listed(apiMissing) +
				(apiMissing.size() == 1 ? " is" : " are") + " not set. See "
				"Docs/desktop-export.md");
		}
		if(idMissing.size() < 3)
		{
			return report(outRefusal, "notarization is asked for and an Apple "
				"ID login is half configured: " + listed(idMissing) +
				(idMissing.size() == 1 ? " is" : " are") + " not set. See "
				"Docs/desktop-export.md");
		}
		return report(outRefusal, String("notarization is asked for and no "
			"credentials are set. Apple takes either an App Store Connect key "
			"(") + NOTARY_KEY_ENV + ", " + NOTARY_KEY_ID_ENV + ", " +
			NOTARY_ISSUER_ENV + ") or an Apple ID login (" +
			NOTARY_APPLE_ID_ENV + ", " + NOTARY_APP_PASSWORD_ENV + ", " +
			NOTARY_TEAM_ID_ENV + "). The password is read from the environment "
			"only - it never travels on a command line or in a file. See "
			"Docs/desktop-export.md");
	}
	//---------------------------------------------------------
	String macosSigningPlatformRefusal(String const & platform)
	{
		if(platform == "macos")
		{
			return String();
		}
		if(platform == "ios" || platform == "ios-ipa" ||
			platform == "ios-simulator")
		{
			return "--sign and --notarize are the macOS Developer ID gate; an "
				"iOS package is signed with an Apple Development or "
				"Distribution identity and a provisioning profile instead "
				"(--signing-identity / --distribution-identity). See "
				"Docs/ios-signing.md";
		}
		return "--sign and --notarize apply to a macOS package; '" + platform +
			"' is signed by its own platform's rules";
	}
	//---------------------------------------------------------
	String redactedCommandLine(SignCommand const & command)
	{
		std::vector<String> shown;
		shown.reserve(command.arguments.size());
		for(String const & argument : command.arguments)
		{
			const bool secret = !argument.empty() &&
				std::find(command.secrets.begin(), command.secrets.end(),
					argument) != command.secrets.end();
			shown.push_back(secret ? String("<redacted>") : argument);
		}
		return commandLine(shown);
	}
	//---------------------------------------------------------
	std::vector<String> codesignArguments(String const & target,
		String const & identity, String const & keychain)
	{
		if(identity.empty() || identity == "-")
		{
			// the ad-hoc form: exactly the four-word command an export has
			// always used, so a run that asks for nothing produces the same
			// signature it did before this file existed
			return { "codesign", "--force", "--sign", "-", target };
		}
		std::vector<String> arguments = { "codesign", "--force", "--sign",
			identity, "--timestamp", "--options", "runtime" };
		if(!keychain.empty())
		{
			arguments.push_back("--keychain");
			arguments.push_back(keychain);
		}
		arguments.push_back(target);
		return arguments;
	}
	//---------------------------------------------------------
	std::vector<String> codesignVerifyArguments(String const & target,
		bool strict)
	{
		std::vector<String> arguments = { "codesign", "--verify" };
		if(strict)
		{
			arguments.push_back("--strict");
			arguments.push_back("--verbose=2");
		}
		arguments.push_back(target);
		return arguments;
	}
	//---------------------------------------------------------
	std::vector<String> dittoArguments(String const & app,
		String const & zipPath)
	{
		return { "ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app,
			zipPath };
	}
	//---------------------------------------------------------
	std::vector<String> notarytoolSubmitArguments(String const & artifact,
		NotaryCredentials const & notary, String const & timeout)
	{
		std::vector<String> arguments = { "xcrun", "notarytool", "submit",
			artifact };
		const std::vector<String> credentials = notary.arguments();
		arguments.insert(arguments.end(), credentials.begin(),
			credentials.end());
		arguments.push_back("--wait");
		arguments.push_back("--timeout");
		arguments.push_back(timeout.empty() ? String(DEFAULT_NOTARY_TIMEOUT)
			: timeout);
		arguments.push_back("--output-format");
		arguments.push_back("json");
		return arguments;
	}
	//---------------------------------------------------------
	std::vector<String> notarytoolLogArguments(String const & submissionId,
		NotaryCredentials const & notary)
	{
		std::vector<String> arguments = { "xcrun", "notarytool", "log",
			submissionId };
		const std::vector<String> credentials = notary.arguments();
		arguments.insert(arguments.end(), credentials.begin(),
			credentials.end());
		return arguments;
	}
	//---------------------------------------------------------
	std::vector<String> staplerStapleArguments(String const & target)
	{
		return { "xcrun", "stapler", "staple", target };
	}
	//---------------------------------------------------------
	std::vector<String> staplerValidateArguments(String const & target)
	{
		return { "xcrun", "stapler", "validate", target };
	}
	//---------------------------------------------------------
	std::vector<String> spctlAssessArguments(String const & app)
	{
		return { "spctl", "--assess", "--type", "exec", "--verbose=2", app };
	}
	//---------------------------------------------------------
	std::vector<SignCommand> macosSignPlan(MacosSignPlanInputs const & inputs)
	{
		std::vector<SignCommand> plan;
		const MacosSigning & signing = inputs.signing;
		auto add = [&plan](std::vector<String> const & arguments,
			String const & what)
		{
			SignCommand command;
			command.arguments = arguments;
			command.what = what;
			plan.push_back(command);
		};
		// nested code FIRST: a bundle signature records what is beneath it, so
		// signing a nested binary afterwards would invalidate the seal
		for(String const & path : inputs.nested)
		{
			add(codesignArguments(path, signing.identity, signing.keychain),
				"signing " + std::filesystem::path(path).filename().string());
		}
		add(codesignArguments(inputs.bundle, signing.identity,
			signing.keychain), "sealing the bundle");
		add(codesignVerifyArguments(inputs.bundle, signing.real()),
			"verifying the signature");
		if(!signing.notarize)
		{
			return plan;
		}
		add(dittoArguments(inputs.bundle, inputs.submissionZip),
			"packing the submission archive");
		{
			SignCommand submit;
			submit.arguments = notarytoolSubmitArguments(inputs.submissionZip,
				signing.notary, signing.notaryTimeout);
			submit.secrets = signing.notary.secrets();
			submit.what = "submitting to Apple";
			plan.push_back(submit);
		}
		add(staplerStapleArguments(inputs.bundle), "stapling the ticket");
		add(staplerValidateArguments(inputs.bundle), "validating the ticket");
		add(spctlAssessArguments(inputs.bundle), "asking Gatekeeper");
		return plan;
	}
	//---------------------------------------------------------
	bool notarySubmissionVerdict(String const & stdoutText,
		String & outSubmissionId, String & outStatus)
	{
		outSubmissionId.clear();
		outStatus.clear();
		Orkige::JsonValue payload;
		if(!Orkige::JsonValue::parse(stdoutText, payload) ||
			!payload.isObject())
		{
			// output that is not the expected payload reads as NOT accepted
			return false;
		}
		outSubmissionId = payload.get("id").asString();
		outStatus = payload.get("status").asString();
		return outStatus == "Accepted";
	}
	//---------------------------------------------------------
	std::vector<String> macosNestedCode(String const & bundle,
		String const & mainExecutable)
	{
		std::vector<String> nested;
		const String contents = ExportFiles::join(bundle, "Contents");
		const String frameworks = ExportFiles::join(contents, "Frameworks");
		auto collect = [&nested](String const & directory, String const & skip)
		{
			if(!ExportFiles::isDirectory(directory))
			{
				return;
			}
			std::error_code ignored;
			for(std::filesystem::directory_entry const & entry :
				std::filesystem::directory_iterator(
					std::filesystem::path(directory), ignored))
			{
				// a symlink alias points at a file that is signed on its own
				// pass; signing it twice through two names is wasted work at
				// best and a broken seal at worst
				if(entry.is_symlink() || !entry.is_regular_file())
				{
					continue;
				}
				const String name = entry.path().filename().string();
				if(!skip.empty() && name == skip)
				{
					continue;
				}
				nested.push_back(entry.path().string());
			}
		};
		collect(frameworks, String());
		collect(ExportFiles::join(contents, "MacOS"), mainExecutable);
		std::sort(nested.begin(), nested.end());
		return nested;
	}
	//---------------------------------------------------------
	bool signMacosBundle(String const & bundle, String const & mainExecutable,
		String const & workDirectory, MacosSigning const & signing,
		ExportEnvironment const & environment, String * error)
	{
		if(!signing.real())
		{
			return true;	// nothing was asked for
		}
		MacosSignPlanInputs inputs;
		inputs.bundle = bundle;
		inputs.nested = macosNestedCode(bundle, mainExecutable);
		inputs.submissionZip = ExportFiles::join(workDirectory,
			std::filesystem::path(bundle).stem().string() + "-notarize.zip");
		inputs.signing = signing;

		String submissionId;
		const std::vector<SignCommand> plan = macosSignPlan(inputs);
		for(SignCommand const & command : plan)
		{
			emit(environment.log, "$ " + redactedCommandLine(command));
			const ProcessResult result = environment.runner(command.arguments);
			if(!result.launched)
			{
				ExportFiles::removeTree(inputs.submissionZip, 0);
				return report(error, "could not run '" + command.arguments[0] +
					"' (" + command.what + ")");
			}
			const bool submitting = (command.what == "submitting to Apple");
			if(submitting)
			{
				// the VERDICT is in the payload, not in the exit code: a
				// submission that came back "Invalid" exits 0
				String status;
				const bool accepted = notarySubmissionVerdict(result.output,
					submissionId, status);
				if(!accepted)
				{
					if(!submissionId.empty())
					{
						// the verdict alone says nothing actionable - the log
						// names the binary Apple objected to
						SignCommand detail;
						detail.arguments = notarytoolLogArguments(submissionId,
							signing.notary);
						detail.secrets = signing.notary.secrets();
						emit(environment.log,
							"$ " + redactedCommandLine(detail));
						const ProcessResult log =
							environment.runner(detail.arguments);
						if(!log.output.empty())
						{
							emit(environment.log, log.output);
						}
					}
					ExportFiles::removeTree(inputs.submissionZip, 0);
					return report(error, "notarization came back '" +
						(status.empty() ? String("no verdict") : status) +
						"'" + (submissionId.empty() ? String()
							: " (submission " + submissionId + ")") +
						" - nothing is stapled from a submission Apple did "
						"not accept. A wait that ran out is not a rejection: "
						"the verdict can be collected later with xcrun "
						"notarytool log");
				}
				emit(environment.log, "Apple accepted the app (submission " +
					submissionId + ")");
				continue;
			}
			if(result.exitCode != 0)
			{
				ExportFiles::removeTree(inputs.submissionZip, 0);
				return report(error, command.what + " failed (exit " +
					std::to_string(result.exitCode) + ")" +
					(result.output.empty() ? String()
						: " - " + result.output));
			}
		}
		ExportFiles::removeTree(inputs.submissionZip, 0);
		emit(environment.log, signing.notarize
			? "signed with '" + signing.identity + "', notarized and stapled"
			: "signed with '" + signing.identity + "' (hardened runtime, "
				"secure timestamp) - not notarized");
		return true;
	}
}
