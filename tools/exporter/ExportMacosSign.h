/********************************************************************
	created:	Wednesday 2026/08/05 at 12:00
	filename: 	ExportMacosSign.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportMacosSign_h__5_8_2026__12_00_00__
#define __ExportMacosSign_h__5_8_2026__12_00_00__

#include "ExportPayload.h"
#include "ExportProcess.h"
#include "ExportSettings.h"

#include <core_util/String.h>

#include <vector>

//! @file ExportMacosSign.h
//! @brief signing a macOS game bundle for OTHER PEOPLE'S Macs: a Developer ID
//! signature with the hardened runtime, Apple's notarization verdict, and the
//! ticket stapled into the app.
//!
//! @par Three states, and nothing in between
//! - **ad-hoc** (the default, and what an export has always produced): the
//!   bundle is internally consistent and names no developer. It runs on the
//!   machine that built it and macOS refuses a downloaded copy. Asking for
//!   nothing produces exactly this, byte for byte.
//! - **Developer ID**: signed with a certificate, hardened runtime
//!   (`--options runtime`) and a secure timestamp - the two flags notarization
//!   accepts nothing without.
//! - **notarized**: the same, submitted to Apple, and - only after an
//!   `Accepted` verdict - stapled, so the machine that opens it needs no
//!   network to learn Apple vouched for it.
//!
//! A half-signed artifact is worse than an honestly ad-hoc one, so every
//! refusal happens BEFORE the first `codesign` call: a signed export with no
//! certificate, or a notarized one with half a credential set, names what is
//! missing and where it may come from, and packages nothing.
//!
//! @par No entitlements
//! The hardened runtime's default restrictions are all things a game the engine
//! runs does not do: the scripting runtime is an interpreter and not a JIT, so
//! no executable-memory exception is needed; every dylib inside the bundle is
//! signed by the same identity in the seal below, so library validation holds.
//! An entitlement that is not needed is signed-in permission nobody asked for,
//! so the list stays empty - a game that genuinely needs one gets a reviewed
//! entitlements file and a line in `Docs/desktop-export.md` beside the reason.
//!
//! @par Credentials never reach a log
//! The signing identity is a certificate's public name and is not a secret.
//! Everything else is: `notarytool` takes its credentials on an argv and offers
//! no alternative, so each planned command carries the exact values that must
//! be replaced before it is echoed (@ref SignCommand::secrets,
//! @ref redactedCommandLine). Nothing here writes a credential to a file, and
//! no refusal quotes a value - only the NAME of the variable that carries it.
//!
//! Everything is PURE except @ref signMacosBundle, which is the ordered walk
//! over the plan - so the argument composition, the credential resolution and
//! every refusal are asserted on a machine that holds no certificate.

namespace OrkigeExport
{
	//--- the machine-local material ----------------------------
	// A Developer ID certificate belongs to a person, not to a project: none of
	// this is ever committed. The identity may be named on the command line;
	// everything else comes from the environment, which is where a build server
	// keeps it and where a desktop editor's credential store hands it over.

	extern const char * const MACOS_SIGNING_IDENTITY_ENV;
	extern const char * const MACOS_KEYCHAIN_ENV;
	//! notarization, App Store Connect API key: a key file plus two
	//! identifiers, revocable on its own without touching an Apple ID
	extern const char * const NOTARY_KEY_ENV;
	extern const char * const NOTARY_KEY_ID_ENV;
	extern const char * const NOTARY_ISSUER_ENV;
	//! notarization, Apple ID + app-specific password (the alternative route)
	extern const char * const NOTARY_APPLE_ID_ENV;
	extern const char * const NOTARY_APP_PASSWORD_ENV;
	extern const char * const NOTARY_TEAM_ID_ENV;
	//! how long one submission may wait before `notarytool` gives up
	//! (@ref DEFAULT_NOTARY_TIMEOUT unless this names another duration)
	extern const char * const NOTARY_TIMEOUT_ENV;

	//! the bounded wait a submission gets when nothing says otherwise. Apple's
	//! service usually answers in minutes; an export that blocks a person's
	//! editor cannot wait indefinitely, and a timeout that names the submission
	//! id is recoverable - the verdict can be collected later.
	extern const char * const DEFAULT_NOTARY_TIMEOUT;

	//--- what one run may do -----------------------------------

	//! @brief what an export was ASKED for. All-false is the default macOS
	//! export, which signs ad-hoc and reaches none of this file.
	struct MacosSigningOptions
	{
		//! sign with a Developer ID certificate + the hardened runtime
		bool			sign = false;
		//! ...and submit it to Apple, then staple the ticket (implies @ref sign)
		bool			notarize = false;
		//! the identity named on the command line ("" = the environment)
		Orkige::String	identity;
		//! the notarization credentials named explicitly; each falls back to
		//! its environment variable. The app-specific PASSWORD is deliberately
		//! absent - it is read from the environment and from nowhere else.
		Orkige::String	notaryKey;
		Orkige::String	notaryKeyId;
		Orkige::String	notaryIssuer;
		Orkige::String	notaryAppleId;
		Orkige::String	notaryTeamId;

		//! @brief does this run touch signing at all?
		bool requested() const { return this->sign || this->notarize; }
	};

	//! @brief how a notarization submission authenticates. Two methods, one
	//! shape: `api-key` (App Store Connect key file + key id + issuer id) and
	//! `apple-id` (Apple ID + app-specific password + team id).
	struct NotaryCredentials
	{
		//! "" | "api-key" | "apple-id"
		Orkige::String	method;
		Orkige::String	keyPath;
		Orkige::String	keyId;
		Orkige::String	issuer;
		Orkige::String	appleId;
		Orkige::String	appPassword;
		Orkige::String	teamId;

		bool resolved() const { return !this->method.empty(); }

		//! @brief the `notarytool` arguments this method contributes
		std::vector<Orkige::String> arguments() const;

		//! @brief every credential VALUE, for redaction. The key file's PATH is
		//! not one (it names a file, it is not the key); the identifiers and the
		//! password are.
		std::vector<Orkige::String> secrets() const;
	};

	//! @brief what this run can sign and vouch for - the resolved answer.
	struct MacosSigning
	{
		//! "" = the ad-hoc seal (and then nothing else here applies)
		Orkige::String		identity;
		//! a non-default keychain to find the certificate in ("" = the default
		//! search list)
		Orkige::String		keychain;
		bool				notarize = false;
		NotaryCredentials	notary;
		//! the bounded `--wait` duration one submission gets
		Orkige::String		notaryTimeout;

		//! @brief does this sign with a certificate (as opposed to ad-hoc)?
		bool real() const { return !this->identity.empty(); }
	};

	//! @brief resolve @p options against @p environment (@see
	//! MacosSigningOptions). PURE - no keychain is opened and no file is read.
	//! @param outRefusal receives the ONE sentence when signing was asked for
	//!        and cannot happen: which credential is missing, and both places
	//!        it may come from. Never quotes a value.
	//! @return false when @p outRefusal was set; true (with an ad-hoc @p
	//!         outSigning) when nothing was asked for.
	bool resolveMacosSigning(MacosSigningOptions const & options,
		EnvironmentMap const & environment, MacosSigning & outSigning,
		Orkige::String * outRefusal);

	//! @brief the one sentence refusing a signing request on a platform that
	//! does not take one, or "" when @p platform does. PURE.
	//! @remarks iOS signs through its own identity + provisioning pair, which
	//! is a different credential and a different gate (@see ExportIos.h), so a
	//! macOS flag pointed at it is a mistake worth naming rather than ignoring.
	Orkige::String macosSigningPlatformRefusal(Orkige::String const & platform);

	//--- the commands ------------------------------------------

	//! @brief one planned command, plus the values that must never be echoed
	struct SignCommand
	{
		std::vector<Orkige::String>	arguments;
		//! the credential VALUES inside @ref arguments (@see redactedCommandLine)
		std::vector<Orkige::String>	secrets;
		//! what this step is doing, for the progress line ("signing ...")
		Orkige::String				what;
	};

	//! @brief @p command as it may appear in a log: every credential value
	//! replaced by `<redacted>`. PURE, and the only reason a credentialed
	//! command is echoed at all - a step whose command nobody can see is a step
	//! nobody can debug.
	Orkige::String redactedCommandLine(SignCommand const & command);

	//! @brief the `codesign` invocation for ONE binary or bundle. PURE.
	//! @remarks The real form carries the hardened runtime and a secure
	//! timestamp, and neither is optional: a submission missing either is
	//! rejected by Apple, not by us. An empty (or `-`) identity is the ad-hoc
	//! form, which is exactly the four-word command an export has always used.
	std::vector<Orkige::String> codesignArguments(Orkige::String const & target,
		Orkige::String const & identity, Orkige::String const & keychain);

	//! @brief read back what was just written. A real signature is verified
	//! STRICTLY - the check Gatekeeper applies.
	std::vector<Orkige::String> codesignVerifyArguments(
		Orkige::String const & target, bool strict);

	//! @brief archive @p app into @p zipPath for submission. `ditto`, not a zip
	//! writer: the bundle's symlinks and executable bits have to survive the
	//! trip or Apple assesses something that is not the app.
	std::vector<Orkige::String> dittoArguments(Orkige::String const & app,
		Orkige::String const & zipPath);

	//! @brief submit one artifact and WAIT for Apple's verdict, as JSON (the
	//! verdict is read from the payload, never inferred from an exit code)
	std::vector<Orkige::String> notarytoolSubmitArguments(
		Orkige::String const & artifact, NotaryCredentials const & notary,
		Orkige::String const & timeout);

	//! @brief the log of one submission. This is the ONLY thing that names the
	//! binary Apple objected to, so a rejection is worthless without it.
	std::vector<Orkige::String> notarytoolLogArguments(
		Orkige::String const & submissionId, NotaryCredentials const & notary);

	//! @brief attach the notarization ticket, so the machine that opens the app
	//! needs no network to learn Apple vouched for it
	std::vector<Orkige::String> staplerStapleArguments(
		Orkige::String const & target);

	//! @brief prove the ticket stuck
	std::vector<Orkige::String> staplerValidateArguments(
		Orkige::String const & target);

	//! @brief the assessment Gatekeeper itself performs on an app
	std::vector<Orkige::String> spctlAssessArguments(
		Orkige::String const & app);

	//--- the plan ----------------------------------------------

	//! @brief what the walk over one bundle is given
	struct MacosSignPlanInputs
	{
		//! the finished `.app`
		Orkige::String					bundle;
		//! the nested code inside it, in the order it must be signed: the
		//! bundled dylibs and any executable beside the main one. A bundle
		//! signature SEALS what it contains, so a later nested sign would
		//! invalidate it - nested first, always.
		std::vector<Orkige::String>		nested;
		//! where the throwaway submission archive is written (notarization only)
		Orkige::String					submissionZip;
		MacosSigning					signing;
	};

	//! @brief the ordered command sequence for @p inputs. PURE - this is the
	//! whole decision, so "what a signed export runs" is asserted without a
	//! certificate, a network or an Apple account.
	//! @remarks The submission's verdict decides whether the steps after it
	//! happen, so the runner walks this list and stops at the first failure;
	//! the log-fetch that diagnoses a rejection needs the submission id and is
	//! composed then (@ref notarytoolLogArguments).
	std::vector<SignCommand> macosSignPlan(MacosSignPlanInputs const & inputs);

	//! @brief (submission id, status) out of `notarytool submit --wait
	//! --output-format json`. PURE, and deliberately strict: output that is not
	//! the expected payload reads as NOT accepted, because "we could not tell"
	//! and "Apple said yes" must never be the same answer.
	bool notarySubmissionVerdict(Orkige::String const & stdoutText,
		Orkige::String & outSubmissionId, Orkige::String & outStatus);

	//! @brief the nested code inside @p bundle that must be signed before it:
	//! `Contents/Frameworks/*` and every executable in `Contents/MacOS` that is
	//! not @p mainExecutable, sorted. Reads the filesystem; the ORDER rule it
	//! implements is the pure part above.
	std::vector<Orkige::String> macosNestedCode(Orkige::String const & bundle,
		Orkige::String const & mainExecutable);

	//--- the walk ----------------------------------------------

	//! @brief sign (and, when asked, notarize + staple) @p bundle.
	//! @param environment the process-tool seam and the progress log
	//! @return false with an honest @p error; a bundle whose signing failed is
	//!         never reported as packaged.
	bool signMacosBundle(Orkige::String const & bundle,
		Orkige::String const & mainExecutable,
		Orkige::String const & workDirectory, MacosSigning const & signing,
		ExportEnvironment const & environment, Orkige::String * error);
}

#endif //__ExportMacosSign_h__5_8_2026__12_00_00__
