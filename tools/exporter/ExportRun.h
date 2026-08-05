/********************************************************************
	created:	Friday 2026/08/01 at 09:00
	filename: 	ExportRun.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportRun_h__1_8_2026__09_00_00__
#define __ExportRun_h__1_8_2026__09_00_00__

#include "ExportMacosSign.h"
#include "ExportPayload.h"
#include "ExportProcess.h"
#include "ExportProject.h"
#include "ExportSettings.h"

#include <core_util/String.h>

//! @file ExportRun.h
//! @brief ONE export, from a validated request to an artifact path.
//!
//! Everything above this line differs between the two callers - the CLI parses
//! argv, the editor reads the project it already has open - and everything
//! below it is identical, so the platform dispatch, the payload-source
//! invariants and the progress lines live here once. `orkige_export` is a thin
//! argv face over it; the editor calls it directly on a worker thread.
//!
//! @par The beside-itself invariant
//! An export resolves files RELATIVE TO ITS ENGINE SOURCE: the media it
//! bundles, the module build scripts it drives, the browser shell template it
//! stamps. @ref ExportRequest::repoRoot is the ONLY input naming that source,
//! and a staged-payload export leaves it EMPTY - a distributed app has no
//! repository, and everything it packages rides in
//! @ref EngineSource::bundleResources. The exporter and the payload therefore
//! cannot come from two different places: there is one field to disagree on
//! and one rule about it, checked here.

namespace OrkigeExport
{
	//! @brief what to package, beyond the project itself
	struct ExportRequest
	{
		//! "macos" | "linux" | "ios-simulator" | "ios" | "ios-ipa" |
		//! "android" | "android-aab" | "web"
		Orkige::String	platform;
		//! the engine pieces: a preset build tree, or a staged payload
		EngineSource	source;
		//! where the artifact lands ("" = `<project>/builds/<platform>`)
		Orkige::String	outputDirectory;
		//! the engine SOURCE TREE the export resolves its beside-itself files
		//! from. EMPTY when @ref source is a staged payload (@see ExportRun.h).
		Orkige::String	repoRoot;
		//! the neutral engine icon for a project that sets no `export.icon`
		//! ("" = derive `<repoRoot>/Util/media/orkige_default_icon.png`)
		Orkige::String	defaultIconPath;
		//! native-module build programs ("" = resolve `cmake`/`ninja` on PATH)
		Orkige::String	cmake;
		Orkige::String	ninja;
		//! signing overrides; each falls back to @ref environment
		Orkige::String	signingIdentity;
		Orkige::String	provisioningProfile;
		Orkige::String	distributionIdentity;
		Orkige::String	distributionProfile;
		Orkige::String	androidKeystore;
		Orkige::String	androidKeyAlias;
		Orkige::String	bundletool;
		//! macOS only: sign the finished bundle for OTHER people's Macs
		//! (Developer ID + hardened runtime, optionally notarized and
		//! stapled). Default off - a macOS package is ad-hoc signed and this
		//! request is byte-identical to one that never named it (@see
		//! ExportMacosSign.h). Deliberately NOT an `export_project` field: a
		//! signed distribution build needs machine-local secrets a remote
		//! agent does not hold, the same rule that keeps `ios-ipa` and
		//! `android-aab` off the endpoint.
		MacosSigningOptions	macosSigning;
		//! android-aab only: stop after the unsigned bundle module
		bool			unsignedBundleModule = false;
		//! a TEST BUILD: carry the project's `tests/` suite into the payload
		//! and mark the artifact to run it instead of the game (@see
		//! testRunPlatformRefusal for where that is and is not possible).
		//! Default false, so a shipping export is untouched.
		bool			withTests = false;
		//! a test build's `--test-filter` substring ("" = the whole suite)
		Orkige::String	testFilter;
		//! the machine-local signing environment (@see currentEnvironment)
		EnvironmentMap	environment;
		//! the platform-tool seam ("" = @ref defaultProcessRunner)
		ProcessRunner	runner;
	};

	//! @brief is @p platform one @ref runExport packages?
	bool isPackagedPlatform(Orkige::String const & platform);

	//! @brief is @p platform a DESKTOP package - one whose artifact is built
	//! around the host's own player binary? (PURE)
	bool isDesktopPlatform(Orkige::String const & platform);

	//! @brief the desktop platform THIS exporter build packages natively -
	//! "macos" on an Apple build, "linux" on a Linux one - or "" where the
	//! exporter has no desktop packaging target for the OS it was built for.
	//! @remarks A compile-time fact, so it is a lookup rather than a probe.
	Orkige::String hostDesktopPlatform();

	//! @brief "" when a host whose own desktop platform is @p hostDesktop can
	//! package @p platform; the refusal sentence otherwise (PURE).
	//! @remarks A desktop package is assembled AROUND A BINARY, and a build
	//! tree holds exactly one operating system's. Nothing here cross-compiles,
	//! so an export that would have to invent the other OS's player says so by
	//! name instead of writing a directory that cannot run.
	Orkige::String desktopHostRefusal(Orkige::String const & platform,
		Orkige::String const & hostDesktop);

	//! @brief "" when @p platform can carry a test build, otherwise the one
	//! sentence saying why it cannot (PURE - no filesystem, no environment).
	//! @remarks The line is drawn by how the SUITE IS DISCOVERED, not by how
	//! the artifact is shaped. The runner enumerates `<project>/tests/` with a
	//! directory walk, because there is no other way to learn which files
	//! declare tests. `macos` and the three iOS targets lay their payload out
	//! as loose files inside the bundle, so the walk finds the suite exactly as
	//! it does in a source tree. An Android package and a browser payload put
	//! the payload inside an ARCHIVE the runtime mounts in place: a mounted
	//! entry is not a directory entry, so the walk would find nothing and the
	//! run would report a green verdict over zero tests - the one outcome a
	//! test harness must never produce. Refusing by name is the honest answer
	//! until discovery has a second road there.
	Orkige::String testRunPlatformRefusal(Orkige::String const & platform);

	//! @brief the signing environment variables read from the process
	//! environment - the ONE lookup every resolver below sees.
	EnvironmentMap currentEnvironment();

	//! @brief package @p project as @p request asks, writing progress to
	//! @p log and the artifact path to @p artifact.
	//! @return false with an honest @p error on any refusal or failure; no
	//! half-signed, half-copied artifact is ever reported as success.
	bool runExport(ExportProject const & project, ExportRequest const & request,
		ExportLog const & log, Orkige::String & artifact,
		Orkige::String * error);
}

#endif //__ExportRun_h__1_8_2026__09_00_00__
