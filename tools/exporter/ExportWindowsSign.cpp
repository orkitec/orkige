/********************************************************************
	created:	Thursday 2026/08/06 at 12:00
	filename: 	ExportWindowsSign.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportWindowsSign.h"

#include "ExportFiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace OrkigeExport
{
	const char * const WINDOWS_CERTIFICATE_ENV =
		"ORKIGE_WINDOWS_SIGNING_CERTIFICATE";
	const char * const WINDOWS_CERTIFICATE_PASSWORD_ENV =
		"ORKIGE_WINDOWS_SIGNING_PASSWORD";
	const char * const WINDOWS_THUMBPRINT_ENV =
		"ORKIGE_WINDOWS_SIGNING_THUMBPRINT";
	const char * const WINDOWS_TIMESTAMP_URL_ENV =
		"ORKIGE_WINDOWS_TIMESTAMP_URL";
	const char * const SIGNTOOL_ENV = "ORKIGE_SIGNTOOL";
	const char * const DEFAULT_TIMESTAMP_URL = "http://timestamp.digicert.com";

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
		//---------------------------------------------------------
		//! the leading run of digits of @p text as a number, and whether the
		//! whole component was one. A component that is not a number sorts
		//! before one that is (@see windowsSdkVersionLess).
		bool numericComponent(String const & text, unsigned long long & value)
		{
			if(text.empty())
			{
				return false;
			}
			value = 0;
			for(char character : text)
			{
				if(character < '0' || character > '9')
				{
					return false;
				}
				// a directory name cannot be long enough to overflow this, but
				// saturating beats wrapping if one ever is
				if(value < 0xFFFFFFFFFFFFFFULL)
				{
					value = value * 10 +
						static_cast<unsigned long long>(character - '0');
				}
			}
			return true;
		}
		//---------------------------------------------------------
		std::vector<String> splitDots(String const & text)
		{
			std::vector<String> parts;
			std::size_t start = 0;
			while(true)
			{
				const std::size_t dot = text.find('.', start);
				if(dot == String::npos)
				{
					parts.push_back(text.substr(start));
					return parts;
				}
				parts.push_back(text.substr(start, dot - start));
				start = dot + 1;
			}
		}
	}
	//---------------------------------------------------------
	std::vector<String> WindowsSigning::arguments() const
	{
		if(this->method == "store-thumbprint")
		{
			return { "/sha1", this->thumbprint };
		}
		if(this->method == "certificate-file")
		{
			return { "/f", this->certificate, "/p", this->certificatePassword };
		}
		return std::vector<String>();
	}
	//---------------------------------------------------------
	std::vector<String> WindowsSigning::secrets() const
	{
		std::vector<String> values;
		if(!this->certificatePassword.empty())
		{
			values.push_back(this->certificatePassword);
		}
		return values;
	}
	//---------------------------------------------------------
	bool resolveWindowsSigning(WindowsSigningOptions const & options,
		EnvironmentMap const & environment, WindowsSigning & outSigning,
		String * outRefusal)
	{
		outSigning = WindowsSigning();
		if(!options.requested())
		{
			// the default export: unsigned, exactly as it has always been, and
			// nothing below is consulted
			return true;
		}
		outSigning.signtool = resolved(options.signtool, SIGNTOOL_ENV,
			environment);
		outSigning.timestampUrl = resolved(options.timestampUrl,
			WINDOWS_TIMESTAMP_URL_ENV, environment);
		if(outSigning.timestampUrl.empty())
		{
			outSigning.timestampUrl = DEFAULT_TIMESTAMP_URL;
		}

		const String thumbprint = resolved(options.thumbprint,
			WINDOWS_THUMBPRINT_ENV, environment);
		const String certificate = resolved(options.certificate,
			WINDOWS_CERTIFICATE_ENV, environment);
		// the password is environment-only, deliberately: it is the one value
		// here that is a password, and a command line carrying one is readable
		// by every process on the machine
		const String password = resolved(String(),
			WINDOWS_CERTIFICATE_PASSWORD_ENV, environment);

		if(!thumbprint.empty())
		{
			// the machine store wins when both are configured: the private key
			// never leaves it, so a run taking this route holds no secret to
			// leak, put on a command line, or forget to redact
			outSigning.method = "store-thumbprint";
			outSigning.thumbprint = thumbprint;
			return true;
		}
		if(!certificate.empty())
		{
			if(password.empty())
			{
				// a PKCS#12 file with no password is an unprotected private key
				// on disk, and signtool would stop to ASK for one - which on a
				// build server is a job that hangs rather than a job that fails
				return report(outRefusal, String("a signed Windows export was "
					"given a certificate file and no password for it. Set ") +
					WINDOWS_CERTIFICATE_PASSWORD_ENV + " - it is read from the "
					"environment only, never from a file and never from a "
					"command line. Signing with a certificate already in this "
					"machine's store (--windows-thumbprint, " +
					WINDOWS_THUMBPRINT_ENV + ") needs no password at all. See "
					"Docs/desktop-export.md");
			}
			outSigning.method = "certificate-file";
			outSigning.certificate = certificate;
			outSigning.certificatePassword = password;
			return true;
		}
		return report(outRefusal, String("a signed Windows export needs a "
			"code-signing certificate, and none is set. Name a certificate "
			"already in this machine's store by its thumbprint "
			"(--windows-thumbprint, ") + WINDOWS_THUMBPRINT_ENV + ") - the "
			"route where no password exists - or name a .pfx file "
			"(--windows-certificate, " + WINDOWS_CERTIFICATE_ENV +
			") together with " + WINDOWS_CERTIFICATE_PASSWORD_ENV + ". You can "
			"also fill either in under Build > Project Settings > Signing "
			"(Windows / Distribution). An unsigned export needs none of this - "
			"it runs, and a downloaded copy meets a SmartScreen warning. See "
			"Docs/desktop-export.md");
	}
	//---------------------------------------------------------
	String windowsSigningPlatformRefusal(String const & platform)
	{
		if(platform == "windows")
		{
			return String();
		}
		if(platform == "macos")
		{
			return "--windows-certificate, --windows-thumbprint and "
				"--windows-timestamp-url are the Authenticode gate for a "
				"Windows package; a macOS package is signed with a Developer "
				"ID identity instead (--macos-identity). See "
				"Docs/desktop-export.md";
		}
		return "--windows-certificate, --windows-thumbprint and "
			"--windows-timestamp-url apply to a Windows package; '" + platform +
			"' is signed by its own platform's rules";
	}
	//---------------------------------------------------------
	bool windowsSdkVersionLess(String const & left, String const & right)
	{
		const std::vector<String> a = splitDots(left);
		const std::vector<String> b = splitDots(right);
		const std::size_t count = std::max(a.size(), b.size());
		for(std::size_t index = 0; index < count; ++index)
		{
			const String leftPart = index < a.size() ? a[index] : String();
			const String rightPart = index < b.size() ? b[index] : String();
			unsigned long long leftValue = 0;
			unsigned long long rightValue = 0;
			const bool leftNumber = numericComponent(leftPart, leftValue);
			const bool rightNumber = numericComponent(rightPart, rightValue);
			if(leftNumber && rightNumber)
			{
				if(leftValue != rightValue)
				{
					return leftValue < rightValue;
				}
				continue;
			}
			if(leftNumber != rightNumber)
			{
				// a name that is not a version sorts BELOW one that is, so a
				// stray directory beside the SDK versions is never preferred
				return rightNumber;
			}
			if(leftPart != rightPart)
			{
				return leftPart < rightPart;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	std::vector<String> windowsSigntoolArchitectures()
	{
		// signtool is an ordinary user-mode program, so what matters is what
		// this machine can EXECUTE, not what it was built for. The build's own
		// architecture is tried first because it is certain to run; the rest
		// are the ones an x64 or arm64 Windows also runs.
#if defined(_M_ARM64) || defined(__aarch64__)
		return { "arm64", "x64", "x86" };
#elif defined(_M_IX86) || (defined(__i386__) && !defined(__x86_64__))
		return { "x86" };
#else
		return { "x64", "x86" };
#endif
	}
	//---------------------------------------------------------
	std::vector<String> signtoolCandidatesInKit(String const & kitRoot,
		std::vector<String> const & versionDirectories)
	{
		std::vector<String> candidates;
		if(kitRoot.empty())
		{
			return candidates;
		}
		const String bin = ExportFiles::join(kitRoot, "bin");
		const std::vector<String> architectures = windowsSigntoolArchitectures();

		std::vector<String> versions = versionDirectories;
		// newest first: an SDK installs side by side with every earlier one it
		// was never asked to remove, so "the first directory listed" is an
		// arbitrary decade-old tool
		std::sort(versions.begin(), versions.end(),
			[](String const & left, String const & right)
			{
				return windowsSdkVersionLess(right, left);
			});
		for(String const & version : versions)
		{
			if(version.empty())
			{
				continue;
			}
			const String versioned = ExportFiles::join(bin, version);
			for(String const & architecture : architectures)
			{
				candidates.push_back(ExportFiles::join(
					ExportFiles::join(versioned, architecture),
					"signtool.exe"));
			}
		}
		// the older layout, with the tool directly under bin/<arch>. Tried
		// after every version rather than instead of one - a kit that carries
		// both is a kit whose versioned tool is the current one.
		for(String const & architecture : architectures)
		{
			candidates.push_back(ExportFiles::join(
				ExportFiles::join(bin, architecture), "signtool.exe"));
		}
		return candidates;
	}
	//---------------------------------------------------------
	std::vector<String> windowsKitRoots(EnvironmentMap const & environment)
	{
		std::vector<String> roots;
		auto add = [&roots](String const & root)
		{
			if(root.empty())
			{
				return;
			}
			if(std::find(roots.begin(), roots.end(), root) == roots.end())
			{
				roots.push_back(root);
			}
		};
		// the SDK's own variable first: a machine whose developer environment
		// is set up has already answered this question
		const EnvironmentMap::const_iterator sdkDir =
			environment.find("WindowsSdkDir");
		if(sdkDir != environment.end())
		{
			// it is conventionally written with a trailing separator, which
			// would otherwise produce a doubled one further down
			String value = trimmed(sdkDir->second);
			while(!value.empty() &&
				(value.back() == '/' || value.back() == '\\'))
			{
				value.pop_back();
			}
			add(value);
		}
		// the SDK installs on the 32-bit side of Program Files by default, so
		// that one is looked at first
		const char * const programFiles[] = { "ProgramFiles(x86)",
			"ProgramFiles" };
		for(const char * name : programFiles)
		{
			const EnvironmentMap::const_iterator found = environment.find(name);
			if(found != environment.end() && !trimmed(found->second).empty())
			{
				add(ExportFiles::join(
					ExportFiles::join(trimmed(found->second), "Windows Kits"),
					"10"));
			}
		}
		return roots;
	}
	//---------------------------------------------------------
	std::vector<String> signtoolCandidatesOnPath(String const & pathVariable)
	{
		std::vector<String> candidates;
		std::size_t start = 0;
		while(start <= pathVariable.size())
		{
			// the PATH separator is ';' on the system this searches - a Windows
			// path holds a drive colon, so ':' cannot be one
			std::size_t end = pathVariable.find(';', start);
			if(end == String::npos)
			{
				end = pathVariable.size();
			}
			const String entry = trimmed(pathVariable.substr(start, end - start));
			if(!entry.empty())
			{
				candidates.push_back(
					ExportFiles::join(entry, "signtool.exe"));
			}
			if(end == pathVariable.size())
			{
				break;
			}
			start = end + 1;
		}
		return candidates;
	}
	//---------------------------------------------------------
	FileProbe defaultFileProbe()
	{
		return [](String const & path)
		{
			return ExportFiles::isRegularFile(path);
		};
	}
	//---------------------------------------------------------
	DirectoryLister defaultDirectoryLister()
	{
		return [](String const & directory)
		{
			std::vector<String> names;
			if(!ExportFiles::isDirectory(directory))
			{
				return names;
			}
			std::error_code ignored;
			for(std::filesystem::directory_entry const & entry :
				std::filesystem::directory_iterator(
					std::filesystem::path(directory), ignored))
			{
				if(entry.is_directory())
				{
					names.push_back(entry.path().filename().string());
				}
			}
			return names;
		};
	}
	//---------------------------------------------------------
	String locateSigntool(String const & signtool,
		EnvironmentMap const & environment, FileProbe const & exists,
		DirectoryLister const & subdirectories, String * outRefusal)
	{
		const String named = trimmed(signtool);
		if(!named.empty())
		{
			if(exists && exists(named))
			{
				return named;
			}
			report(outRefusal, String("no signing tool at '") + named +
				"' - " + SIGNTOOL_ENV + " (or --signtool) names a file that is "
				"not there. Nothing else is tried: a run that quietly signed "
				"with a different tool than the one it was told to use would "
				"be the failure this check exists to prevent");
			return String();
		}
		for(String const & root : windowsKitRoots(environment))
		{
			const std::vector<String> versions = subdirectories
				? subdirectories(ExportFiles::join(root, "bin"))
				: std::vector<String>();
			for(String const & candidate :
				signtoolCandidatesInKit(root, versions))
			{
				if(exists && exists(candidate))
				{
					return candidate;
				}
			}
		}
		const EnvironmentMap::const_iterator path = environment.find("PATH");
		if(path != environment.end())
		{
			for(String const & candidate :
				signtoolCandidatesOnPath(path->second))
			{
				if(exists && exists(candidate))
				{
					return candidate;
				}
			}
		}
		report(outRefusal, String("no signing tool found. signtool.exe ships "
			"inside the Windows SDK (the 'Windows SDK Signing Tools' component) "
			"and is on no machine's PATH by default - this looked through every "
			"installed SDK version under the Windows Kits directory and then "
			"through PATH itself. Install the Windows SDK, or name the tool "
			"outright with ") + SIGNTOOL_ENV + " (or --signtool). See "
			"Docs/desktop-export.md");
		return String();
	}
	//---------------------------------------------------------
	std::vector<String> signtoolSignArguments(String const & signtool,
		String const & target, WindowsSigning const & signing)
	{
		std::vector<String> arguments = { signtool, "sign",
			// the signature's own digest, and the countersignature's. Neither
			// defaults to SHA-256 on every tool version, and defaulting is not
			// the same as choosing.
			"/fd", "SHA256",
			// /tr is RFC 3161; the older /t protocol no longer produces a
			// timestamp an operating system accepts
			"/tr", signing.timestampUrl.empty()
				? String(DEFAULT_TIMESTAMP_URL) : signing.timestampUrl,
			"/td", "SHA256" };
		const std::vector<String> credentials = signing.arguments();
		arguments.insert(arguments.end(), credentials.begin(),
			credentials.end());
		arguments.push_back(target);
		return arguments;
	}
	//---------------------------------------------------------
	std::vector<String> signtoolVerifyArguments(String const & signtool,
		String const & target)
	{
		// /pa selects the Authenticode policy - what an operating system
		// applies to a program. Without it signtool verifies against the DRIVER
		// policy, which a perfectly good application signature fails.
		return { signtool, "verify", "/pa", target };
	}
	//---------------------------------------------------------
	std::vector<SignCommand> windowsSignPlan(
		WindowsSignPlanInputs const & inputs)
	{
		std::vector<SignCommand> plan;
		auto add = [&plan, &inputs](String const & target, String const & what)
		{
			SignCommand command;
			command.arguments = signtoolSignArguments(inputs.signtool, target,
				inputs.signing);
			command.secrets = inputs.signing.secrets();
			command.what = what;
			plan.push_back(command);

			SignCommand verify;
			verify.arguments = signtoolVerifyArguments(inputs.signtool, target);
			verify.what = "verifying " +
				std::filesystem::path(target).filename().string();
			plan.push_back(verify);
		};
		// a DLL is code and carries its own signature. There is no seal over a
		// directory here - nothing records what sits beside the executable - so
		// each file stands alone and the order is a reading convenience.
		for(String const & library : inputs.libraries)
		{
			add(library, "signing " +
				std::filesystem::path(library).filename().string());
		}
		add(inputs.executable, "signing " +
			std::filesystem::path(inputs.executable).filename().string());
		return plan;
	}
	//---------------------------------------------------------
	bool signWindowsPackage(String const & executable,
		std::vector<String> const & libraries, WindowsSigning const & signing,
		ExportEnvironment const & environment, String * error)
	{
		if(!signing.real())
		{
			return true;	// nothing was asked for
		}
		if(signing.signtool.empty())
		{
			// the tool is located before anything is copied; arriving here
			// without one means that step was skipped, and signing nothing
			// while reporting a signed package is the one outcome forbidden
			return report(error, "no signing tool was resolved for this "
				"export - nothing is signed, and nothing is reported as "
				"signed");
		}
		emit(environment.log, "signing with '" + signing.signtool + "'");

		WindowsSignPlanInputs inputs;
		inputs.signtool = signing.signtool;
		inputs.executable = executable;
		inputs.libraries = libraries;
		inputs.signing = signing;
		for(SignCommand const & command : windowsSignPlan(inputs))
		{
			emit(environment.log, "$ " + redactedCommandLine(command));
			const ProcessResult result = environment.runner(command.arguments);
			if(!result.launched)
			{
				return report(error, "could not run '" + command.arguments[0] +
					"' (" + command.what + ")");
			}
			if(result.exitCode != 0)
			{
				return report(error, command.what + " failed (exit " +
					std::to_string(result.exitCode) + ")" +
					(result.output.empty() ? String()
						: " - " + result.output));
			}
		}
		emit(environment.log, signing.method == "store-thumbprint"
			? "signed with the certificate in this machine's store, "
				"countersigned by " + signing.timestampUrl
			: "signed with '" + signing.certificate + "', countersigned by " +
				signing.timestampUrl);
		return true;
	}
}
