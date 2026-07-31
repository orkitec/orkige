/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportAndroid.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportAndroid_h__1_8_2026__10_00_00__
#define __ExportAndroid_h__1_8_2026__10_00_00__

#include "ExportPayload.h"
#include "ExportProject.h"
#include "ExportSettings.h"

#include <core_util/String.h>

#include <vector>

//! @file ExportAndroid.h
//! @brief packaging a project as an Android APK or App Bundle.
//!
//! The ASSEMBLY of an Android package is `aapt2` + `d8` + `apksigner` (or
//! bundletool + jarsigner) driven by two shell scripts that live beside the
//! player: `tools/player/android/package_apk.sh` and `build_aab.sh`. They own
//! the SDK-tool choreography and are driven unchanged - what an export
//! contributes is everything before them: the staged project payload, the
//! launcher-icon `res/` tree, the launch-screen colour, the validated package
//! name and version, and the credential gate a signed bundle sits behind.
//!
//! So the exporter's Android surface is argument composition, which is exactly
//! what the pure builders below expose: what breaks a package is a wrong flag
//! or a missing gate, never the SDK tools themselves.

namespace OrkigeExport
{
	//! @brief the validated Android package name: `export.android.package`,
	//! else `com.orkitec.<slug>`. False with an honest @p error when the
	//! manifest names something that is not a package name.
	bool androidPackageName(ExportProject const & project,
		Orkige::String & out, Orkige::String * error);

	//! @brief the `package_apk.sh` command line (PURE).
	//! @param orientation the normalised `export.orientation`; only a NON-auto
	//!        lock injects `--orientation`, so an unconstrained project leaves
	//!        the manifest template byte-identical
	std::vector<Orkige::String> androidApkArguments(
		Orkige::String const & script, Orkige::String const & payloadDirectory,
		Orkige::String const & package, Orkige::String const & label,
		Orkige::String const & resDirectory,
		Orkige::String const & launchColour, Orkige::String const & assetsMode,
		Orkige::String const & orientation, Orkige::String const & outputPath,
		Orkige::String const & engineBuild);

	//! @brief what a release App Bundle needs beyond the APK arguments
	struct AndroidBundleOptions
	{
		int				versionCode = 1;
		Orkige::String	versionName = "1.0";
		//! build ONLY the unsigned proto bundle module - the structural slice
		//! that needs neither bundletool nor a keystore
		bool			moduleOnly = false;
		AndroidKeystore	keystore;
		Orkige::String	bundletool;
	};

	//! @brief the `build_aab.sh` command line (PURE)
	std::vector<Orkige::String> androidBundleArguments(
		Orkige::String const & script, Orkige::String const & payloadDirectory,
		Orkige::String const & package, Orkige::String const & label,
		Orkige::String const & resDirectory,
		Orkige::String const & launchColour, Orkige::String const & assetsMode,
		Orkige::String const & orientation, Orkige::String const & outputPath,
		Orkige::String const & engineBuild,
		AndroidBundleOptions const & options);

	//! @brief the credential gate a SIGNED App Bundle sits behind: the list of
	//! missing pieces, empty when the bundle can be built and signed. PURE, so
	//! the gate is asserted on a machine that holds none of them.
	//! @remarks a half-signed artifact is worse than no artifact, so the
	//! export refuses and produces nothing rather than emitting one.
	std::vector<Orkige::String> androidSigningGaps(
		AndroidKeystore const & keystore, Orkige::String const & bundletool);

	//! @brief what an Android export packages
	struct AndroidRequest
	{
		//! package an App Bundle (`.aab`) rather than an APK
		bool					bundle = false;
		AndroidBundleOptions	options;
	};

	//! @brief package @p project as `<output>/<Exe>.apk` (or the App Bundle
	//! the request asks for).
	//! @param outArtifact receives the packaged file on success
	bool exportAndroid(ExportProject const & project,
		EngineSource const & source, Orkige::String const & outputDirectory,
		AndroidRequest const & request,
		ExportEnvironment const & environment, Orkige::String & outArtifact,
		Orkige::String * error);
}

#endif //__ExportAndroid_h__1_8_2026__10_00_00__
