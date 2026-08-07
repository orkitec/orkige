/********************************************************************
	created:	Thursday 2026/08/06 at 12:00
	filename: 	ExportWindowsSign.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportWindowsSign_h__6_8_2026__12_00_00__
#define __ExportWindowsSign_h__6_8_2026__12_00_00__

#include "ExportPayload.h"
#include "ExportProcess.h"
#include "ExportSettings.h"

#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file ExportWindowsSign.h
//! @brief signing a Windows game for OTHER PEOPLE'S PCs: an Authenticode
//! signature over the packaged executable, countersigned by a timestamp
//! authority so it outlives the certificate that made it.
//!
//! @par Two states, and nothing in between
//! - **unsigned** (the default, and what an export has always produced): the
//!   executable is a plain copy of the player binary and names no publisher.
//!   It runs, and a downloaded copy meets a SmartScreen warning. Asking for
//!   nothing produces exactly this, byte for byte.
//! - **signed**: an Authenticode signature naming a publisher, made with a
//!   SHA-256 digest and countersigned through RFC 3161.
//!
//! There is no third state, and that is the honest difference from the macOS
//! tier: nothing on Windows corresponds to notarization. No service is asked
//! for a verdict, so there is nothing to poll and nothing to staple - the
//! timestamp is a countersignature over the signature, not an opinion about the
//! program. Reputation is earned by the certificate over time rather than
//! granted per build.
//!
//! A half-signed artifact is worse than an honestly unsigned one, so every
//! refusal happens BEFORE the first `signtool` call, including the one that
//! finds `signtool` itself.
//!
//! @par The timestamp is not optional
//! An Authenticode signature with no countersignature stops verifying the day
//! the certificate expires, which turns every copy already in people's hands
//! into an unsigned one. So the timestamp URL has a default
//! (@ref DEFAULT_TIMESTAMP_URL) and can be pointed elsewhere
//! (@ref WINDOWS_TIMESTAMP_URL_ENV), but there is no way to ask for a signature
//! without one.
//!
//! @par Two credential routes, and the one that carries no secret wins
//! - **a machine-store certificate** (`/sha1 <thumbprint>`): the private key
//!   stays in the certificate store - the shape a hardware token or an HSM has
//!   - and nothing secret reaches a command line at all. A thumbprint is a
//!   public hash of a public certificate, so it is not redacted; there is
//!   nothing there to hide.
//! - **a certificate file** (`/f <pfx> /p <password>`): a PKCS#12 file and the
//!   password protecting its private key. `signtool` takes that password on an
//!   argv and offers no alternative, so it is read from the ENVIRONMENT only
//!   and never from a settings file, and the planned command carries the exact
//!   value to replace before it is echoed (@ref WindowsSigning::secrets,
//!   @ref redactedCommandLine).
//!
//! When both are configured the machine store is taken, because a run in which
//! no secret exists cannot leak one.
//!
//! @par signtool is LOCATED, never assumed
//! There is no `xcrun` here: `signtool.exe` ships inside the Windows SDK, under
//! a per-SDK-version directory, and is on no machine's `PATH` by default. So it
//! is searched for - an explicit override first, then every installed SDK
//! version newest-first, then the `PATH` - and a machine that has none is told
//! what to install rather than handed "could not run 'signtool'". The ORDERING
//! and the candidate PATHS are pure (@ref signtoolCandidatesInKit); the
//! existence check and the directory listing are injected seams, so the whole
//! search is asserted on a host that has no Windows SDK and no Windows.
//!
//! Everything is PURE except @ref signWindowsPackage, which is the ordered walk
//! over the plan.

namespace OrkigeExport
{
	//--- the machine-local material ----------------------------
	// A code-signing certificate belongs to a publisher, not to a project:
	// none of this is ever committed. The thumbprint and the certificate PATH
	// may be named on a command line; the password may not, and comes from the
	// environment alone.
	//
	// The names follow the vocabulary the other platforms already use -
	// ORKIGE_<PLATFORM>_<WHAT>, as ORKIGE_MACOS_SIGNING_IDENTITY and
	// ORKIGE_ANDROID_KEYSTORE do - rather than naming the signature FORMAT.
	// Nothing else in the export environment is named after a technology, and
	// a person looking for "the Windows signing variables" should find them by
	// the platform they are packaging for.

	//! the PKCS#12 certificate file (`.pfx`) that signs the executable
	extern const char * const WINDOWS_CERTIFICATE_ENV;
	//! the password protecting that file's private key - environment ONLY
	extern const char * const WINDOWS_CERTIFICATE_PASSWORD_ENV;
	//! the SHA-1 thumbprint of a certificate already in this machine's store -
	//! the route where no secret exists at all
	extern const char * const WINDOWS_THUMBPRINT_ENV;
	//! the RFC 3161 timestamp authority (@ref DEFAULT_TIMESTAMP_URL otherwise)
	extern const char * const WINDOWS_TIMESTAMP_URL_ENV;
	//! `signtool.exe` named outright, for a machine whose SDK is somewhere this
	//! does not look. Named like ORKIGE_BUNDLETOOL, which is the other variable
	//! that names a PROGRAM rather than a credential.
	extern const char * const SIGNTOOL_ENV;

	//! the timestamp authority used when nothing names another. Any RFC 3161
	//! responder works; this one is free, public and does not require an
	//! account, which is what makes it usable as a default.
	extern const char * const DEFAULT_TIMESTAMP_URL;

	//--- what one run may do -----------------------------------

	//! @brief what an export was ASKED for. `sign == false` is the default
	//! Windows export, which reaches none of this file.
	struct WindowsSigningOptions
	{
		//! sign the packaged executable with an Authenticode signature
		bool			sign = false;
		//! the certificate file named on the command line ("" = the
		//! environment). Its PASSWORD is deliberately absent - it is read from
		//! the environment and from nowhere else.
		Orkige::String	certificate;
		//! the machine-store certificate's SHA-1 thumbprint ("" = the
		//! environment)
		Orkige::String	thumbprint;
		//! the RFC 3161 responder ("" = the environment, then
		//! @ref DEFAULT_TIMESTAMP_URL)
		Orkige::String	timestampUrl;
		//! `signtool.exe` named outright ("" = the environment, then the search)
		Orkige::String	signtool;

		//! @brief does this run touch signing at all?
		bool requested() const { return this->sign; }
	};

	//! @brief what this run can sign with - the resolved answer.
	struct WindowsSigning
	{
		//! "" = unsigned (and then nothing else here applies) |
		//! "store-thumbprint" | "certificate-file"
		Orkige::String	method;
		Orkige::String	certificate;
		//! the PKCS#12 password. Present ONLY for the certificate-file route,
		//! and only ever filled from the environment.
		Orkige::String	certificatePassword;
		Orkige::String	thumbprint;
		Orkige::String	timestampUrl;
		//! `signtool.exe` named outright ("" = locate it)
		Orkige::String	signtool;

		//! @brief does this sign at all?
		bool real() const { return !this->method.empty(); }

		//! @brief the credential arguments this method contributes
		std::vector<Orkige::String> arguments() const;

		//! @brief every credential VALUE, for redaction. The certificate's PATH
		//! is not one (it names a file, it is not the key) and neither is the
		//! thumbprint (a public hash of a public certificate). The password is,
		//! and is the only one.
		std::vector<Orkige::String> secrets() const;
	};

	//! @brief resolve @p options against @p environment. PURE - no certificate
	//! store is opened and no file is read.
	//! @param outRefusal receives the ONE sentence when signing was asked for
	//!        and cannot happen: which credential is missing, and both places
	//!        it may come from. Never quotes a value.
	//! @return false when @p outRefusal was set; true (with an unsigned @p
	//!         outSigning) when nothing was asked for.
	bool resolveWindowsSigning(WindowsSigningOptions const & options,
		EnvironmentMap const & environment, WindowsSigning & outSigning,
		Orkige::String * outRefusal);

	//! @brief the one sentence refusing an Authenticode request on a platform
	//! that does not take one, or "" when @p platform does. PURE.
	Orkige::String windowsSigningPlatformRefusal(
		Orkige::String const & platform);

	//--- locating signtool -------------------------------------

	//! @brief does @p left name an OLDER Windows SDK than @p right? (PURE)
	//! @remarks Compared component by component as NUMBERS, which is the whole
	//! point: sorted as text, `10.0.9000.0` outranks `10.0.22621.0` and the
	//! search would take a decade-old tool off a machine that has a current
	//! one. A component that is not a number sorts before one that is, so a
	//! stray directory never wins.
	bool windowsSdkVersionLess(Orkige::String const & left,
		Orkige::String const & right);

	//! @brief the architecture subdirectories a Windows SDK `bin` is searched
	//! in, most preferred first (PURE): this build's own architecture, then the
	//! ones an x64 machine can still execute.
	//! @remarks A compile-time fact about the HOST, so it is a lookup rather
	//! than a probe - and `signtool` is an ordinary program, so a machine that
	//! runs the exporter runs whichever of these it has.
	std::vector<Orkige::String> windowsSigntoolArchitectures();

	//! @brief every `signtool.exe` path to try under ONE Windows Kits root,
	//! in search order (PURE).
	//! @param kitRoot e.g. `C:/Program Files (x86)/Windows Kits/10`
	//! @param versionDirectories the names found under `<kitRoot>/bin` (in any
	//!        order; they are sorted newest-first here)
	//! @remarks The versioned layout comes first because it is where a current
	//! SDK puts the tool; the unversioned `bin/<arch>` tail is the older layout
	//! and is tried after every version, never instead of one.
	std::vector<Orkige::String> signtoolCandidatesInKit(
		Orkige::String const & kitRoot,
		std::vector<Orkige::String> const & versionDirectories);

	//! @brief the Windows Kits roots @p environment points at, in search order
	//! (PURE). The SDK installs 32-bit-side by default, so the `(x86)` program
	//! directory is looked at first.
	std::vector<Orkige::String> windowsKitRoots(
		EnvironmentMap const & environment);

	//! @brief `signtool.exe` under each entry of a `PATH` value (PURE).
	//! @remarks Split and probed EXPLICITLY rather than handed to the process
	//! launcher as a bare name: a bare name that resolves to nothing fails as
	//! "could not run 'signtool'", which names neither what is missing nor how
	//! to get it.
	std::vector<Orkige::String> signtoolCandidatesOnPath(
		Orkige::String const & pathVariable);

	//! @brief does this path exist as a file? (the injected existence check)
	typedef std::function<bool(Orkige::String const &)> FileProbe;
	//! @brief the subdirectory NAMES of a directory, or empty when there is no
	//! such directory (the injected listing)
	typedef std::function<std::vector<Orkige::String>(Orkige::String const &)>
		DirectoryLister;

	//! @brief the real seams, reading this machine's filesystem
	FileProbe defaultFileProbe();
	DirectoryLister defaultDirectoryLister();

	//! @brief find `signtool.exe`, or "" with a refusal naming what to install.
	//! @param signtool the path named outright ("" = search). A named one that
	//!        is not there REFUSES rather than falling back - somebody who
	//!        names a tool means that tool, and quietly signing with another
	//!        one is the failure this whole file exists to prevent.
	Orkige::String locateSigntool(Orkige::String const & signtool,
		EnvironmentMap const & environment, FileProbe const & exists,
		DirectoryLister const & subdirectories, Orkige::String * outRefusal);

	//--- the commands ------------------------------------------

	//! @brief the `signtool sign` invocation for ONE file. PURE.
	//! @remarks `/fd SHA256` is the digest of the signature itself and
	//! `/td SHA256` the digest of the countersignature; SHA-1 is not accepted
	//! for either any more, and defaulting is not the same as choosing, so
	//! both are stated. `/tr` is the RFC 3161 form - `/t` is the older
	//! Authenticode protocol, which no longer produces a usable timestamp.
	std::vector<Orkige::String> signtoolSignArguments(
		Orkige::String const & signtool, Orkige::String const & target,
		WindowsSigning const & signing);

	//! @brief read back what was just written. `/pa` is the Authenticode
	//! policy - the one an operating system applies to a program, rather than
	//! the driver policy `signtool` verifies against by default.
	std::vector<Orkige::String> signtoolVerifyArguments(
		Orkige::String const & signtool, Orkige::String const & target);

	//--- the plan ----------------------------------------------

	//! @brief what the walk over one package is given
	struct WindowsSignPlanInputs
	{
		//! the located `signtool.exe`
		Orkige::String					signtool;
		//! the packaged game executable
		Orkige::String					executable;
		//! the DLLs that rode into the package beside it. A library is code and
		//! carries its own signature - there is no seal over a directory here,
		//! so each file is signed on its own and order is a convenience only.
		std::vector<Orkige::String>		libraries;
		WindowsSigning					signing;
	};

	//! @brief the ordered command sequence for @p inputs. PURE - this is the
	//! whole decision, so "what a signed export runs" is asserted without a
	//! certificate, a Windows SDK or a Windows machine.
	std::vector<SignCommand> windowsSignPlan(
		WindowsSignPlanInputs const & inputs);

	//--- the walk ----------------------------------------------

	//! @brief sign @p executable (and @p libraries) in place.
	//! @param signing a RESOLVED one whose @ref WindowsSigning::signtool has
	//!        already been located - the tool is found before a single file is
	//!        copied (@see locateSigntool), so a machine with no Windows SDK
	//!        refuses instead of producing an unsigned package it called signed
	//! @param environment the process-tool seam and the progress log
	//! @return false with an honest @p error; a package whose signing failed is
	//!         never reported as packaged.
	bool signWindowsPackage(Orkige::String const & executable,
		std::vector<Orkige::String> const & libraries,
		WindowsSigning const & signing, ExportEnvironment const & environment,
		Orkige::String * error);
}

#endif //__ExportWindowsSign_h__6_8_2026__12_00_00__
