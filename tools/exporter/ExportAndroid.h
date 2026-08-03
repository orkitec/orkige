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
//!
//! @par Two engine sources, like every other platform
//! The player and the assembler come either from a preset BUILD TREE (the
//! developer case) or from a fetched device PAYLOAD, which is what an editor
//! with no repository packages from (Docs/device-payloads.md). A payload
//! carries the stripped `libmain.so`, the engine media, the packaging script
//! and the Java sources it compiles - everything that belongs to the ENGINE.
//!
//! @par Three prerequisite tiers, never conflated
//! What is left over is the machine's, and each tier refuses in its own words:
//! - the PLAYER for the platform: fetched, so a missing one is a download.
//! - the platform TOOLCHAIN: the Android SDK build tools and a JDK assemble
//!   and sign every package, debug builds included (Android installs no
//!   unsigned APK; the debug key itself is created on demand, so signing was
//!   never the obstacle). We ship the engine, never a toolchain - so each
//!   program is resolved by name and each missing one is named, with what
//!   installs it (@ref androidToolchainGaps).
//! - the engine SDK PACK: needed ONLY by a project that carries compiled C++
//!   game code. A Lua game has nothing to compile, so no pack is consulted and
//!   none is ever mentioned - at debug, release or signed.

namespace OrkigeExport
{
	//--- the platform toolchain --------------------------------

	//! the environment variables an Android SDK is looked up under, in order
	extern const char * const ANDROID_HOME_ENV;
	extern const char * const ANDROID_SDK_ROOT_ENV;
	//! ...and the one naming the JDK that compiles and signs
	extern const char * const JAVA_HOME_ENV;

	//! @brief the oldest Android platform a package may be linked against -
	//! the player's own `--min-api`, so an SDK with nothing newer cannot
	//! assemble one
	int androidMinimumApi();

	//! @brief where the Android packaging programs were found on this machine.
	//! Every field is a resolved path or an honest false; nothing is guessed.
	struct AndroidToolchain
	{
		//! the SDK the rest was resolved under ("" = none found at all)
		Orkige::String	sdkRoot;
		//! `<sdk>/build-tools/<version>` - the newest one installed
		Orkige::String	buildTools;
		//! `<sdk>/platforms/android-<api>/android.jar`
		Orkige::String	platformJar;
		//! the JDK `javac`, `keytool` and `java` come from
		Orkige::String	javaHome;
		bool			aapt2 = false;
		bool			zipalign = false;
		bool			apksigner = false;	//!< `lib/apksigner.jar`
		bool			d8 = false;			//!< `lib/d8.jar`
		bool			jdk = false;		//!< javac + keytool + java, all three

		//! can this machine assemble a package?
		bool complete() const;
	};

	//! @brief the newest of @p versions, comparing NUMERIC components ("9.0.0"
	//! is older than "35.0.0", which a plain string sort gets backwards).
	//! "" when the list holds no version-shaped name. PURE.
	Orkige::String newestAndroidBuildTools(
		std::vector<Orkige::String> const & versions);

	//! @brief the newest `android-<N>` in @p names with `N >= minimumApi`, or
	//! "" when none qualifies. PURE.
	Orkige::String newestAndroidPlatform(
		std::vector<Orkige::String> const & names, int minimumApi);

	//! @brief the directories an Android SDK is looked for in, in order:
	//! `ANDROID_HOME`, `ANDROID_SDK_ROOT`, then this platform's default
	//! install location under the user's home. PURE, so the precedence is
	//! asserted on a machine with no SDK at all.
	std::vector<Orkige::String> androidSdkCandidates(
		EnvironmentMap const & environment);

	//! @brief resolve the toolchain by probing the candidates in order.
	AndroidToolchain resolveAndroidToolchain(EnvironmentMap const & environment);

	//! @brief what is missing, ONE SENTENCE PER PROGRAM, each naming what
	//! installs it. Empty when a package can be assembled here. PURE.
	//! @remarks deliberately never a single lumped "install the Android SDK":
	//! a person who has the SDK and no JDK is told about the JDK.
	std::vector<Orkige::String> androidToolchainGaps(
		AndroidToolchain const & tools);

	//! @brief the gaps as the one refusal an export shows, or "" when there
	//! are none. PURE.
	Orkige::String androidToolchainRefusal(AndroidToolchain const & tools);

	//--- packaging ---------------------------------------------

	//! @brief the validated Android package name: `export.android.package`,
	//! else `com.orkitec.<slug>`. False with an honest @p error when the
	//! manifest names something that is not a package name.
	bool androidPackageName(ExportProject const & project,
		Orkige::String & out, Orkige::String * error);

	//! @brief the `package_apk.sh` command line (PURE).
	//! @param orientation the normalised `export.orientation`; only a NON-auto
	//!        lock injects `--orientation`, so an unconstrained project leaves
	//!        the manifest template byte-identical
	//! @param engineBuild the preset build tree, passed positionally
	//! @param enginePayload the fetched device payload instead - `--payload`.
	//!        Exactly ONE of the two is set: they are the two engine sources,
	//!        and the script refuses both together for the same reason the
	//!        exporter does.
	//! @param tools the resolved machine toolchain, handed DOWN rather than
	//!        looked up again: the programs the export checked for and reported
	//!        on are then exactly the ones that run. An empty one leaves the
	//!        script to resolve its own, which is what a hand run does.
	std::vector<Orkige::String> androidApkArguments(
		Orkige::String const & script, Orkige::String const & payloadDirectory,
		Orkige::String const & package, Orkige::String const & label,
		Orkige::String const & resDirectory,
		Orkige::String const & launchColour, Orkige::String const & assetsMode,
		Orkige::String const & orientation, Orkige::String const & outputPath,
		Orkige::String const & engineBuild,
		Orkige::String const & enginePayload = Orkige::String(),
		AndroidToolchain const & tools = AndroidToolchain());

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
		//! the machine's environment, for resolving the SDK and the JDK the
		//! packaging tools come from (@ref resolveAndroidToolchain)
		EnvironmentMap			environment;
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
