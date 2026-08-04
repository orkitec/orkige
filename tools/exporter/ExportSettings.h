/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportSettings.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportSettings_h__31_7_2026__12_00_00__
#define __ExportSettings_h__31_7_2026__12_00_00__

#include <core_util/String.h>

#include <map>
#include <vector>

//! @file ExportSettings.h
//! @brief the manifest-Setting decisions an export makes, as pure functions.
//!
//! Everything here maps a project's `Settings` (and, for the machine-local
//! signing material, the environment) onto the exact value a platform
//! artifact carries. No filesystem, no subprocess, no globals - which is what
//! lets the whole table be asserted headlessly, including the credential
//! gates on a machine that holds no certificate.

namespace OrkigeExport
{
	typedef std::map<Orkige::String, Orkige::String> SettingMap;

	//! launch-screen background when a project sets no `export.launch.background`
	//! (the engine dark, matching the default icon's gradient bottom)
	extern const char * const DEFAULT_LAUNCH_BACKGROUND;

	//! the marker file name PlayerBundle reads (@see engine_runtime/PlayerRuntime.h)
	extern const char * const PROJECT_MARKER_FILE_NAME;
	//! the project payload's directory name inside every bundle (marker content)
	extern const char * const PAYLOAD_DIR_NAME;
	//! the iOS privacy manifest's file name (bundle root, flat)
	extern const char * const PRIVACY_MANIFEST_FILE_NAME;

	//! what of a project ships: the manifest plus these subdirectories
	//! (native/ and builds/ stay home - compiled code ships as the packaged
	//! binary). `data/` is the home of AUTHORED DATA FILES - level tables,
	//! item lists, dialogue trees, tuning tables - which a script reads by
	//! project-relative name through the `data` Lua table
	//! (@see core_filesystem/DataResource.h); it ships like any other content
	//! so the same read serves the editor, a pak and a phone.
	std::vector<Orkige::String> payloadSubdirs();

	//! @brief PROJECT-CONFIG assets ride along too: manifest Settings keys
	//! whose value is a project-relative path (file OR directory) copied
	//! verbatim - the config-asset convention (@see engine_input/InputActionMap.h).
	std::vector<Orkige::String> configSettingKeys();

	//! @brief the texture-cook platform token for an export platform ("macos"
	//! -> "" the default block, "ios-simulator"/"ios" -> "ios", ...)
	Orkige::String cookPlatformToken(Orkige::String const & platform);

	//--- look ------------------------------------------------

	//! @brief the project's launch-screen background as a validated #RRGGBB
	//! string; an absent or malformed value degrades to the engine default
	//! (reported through @p warn when it was set but malformed).
	Orkige::String launchBackground(SettingMap const & settings,
		Orkige::String * warn = 0);

	//--- orientation -----------------------------------------

	//! @brief the normalised `export.orientation` - "portrait" | "landscape" |
	//! "auto". PORTRAIT is the default and the degrade target: a mobile game is
	//! portrait unless it says otherwise, and it keeps the boot orientation
	//! deterministic (iOS picks it from the allowed set by window aspect).
	Orkige::String orientationSetting(SettingMap const & settings,
		Orkige::String * warn = 0);

	//! @brief the `UISupportedInterfaceOrientations` array for an orientation
	std::vector<Orkige::String> iosOrientations(
		Orkige::String const & orientation);

	//! @brief the activity's `android:screenOrientation` for an orientation
	Orkige::String androidScreenOrientation(
		Orkige::String const & orientation);

	//--- Android release ---------------------------------------

	//! @brief (versionCode, versionName) for a release bundle from
	//! `export.android.versionCode` / `.versionName`. False with an honest
	//! @p error when the code is not a positive integer - Google Play requires
	//! a STRICTLY INCREASING integer across uploads.
	bool androidVersion(SettingMap const & settings, int & versionCode,
		Orkige::String & versionName, Orkige::String * error);

	//! @brief the APK/AAB asset packaging mode from `export.android.assets`:
	//! "stored" (the DEFAULT - assets stay uncompressed so the player mounts
	//! the package and reads them in place) or "compressed" (deflated, and
	//! extracted on first launch). False with an @p error on anything else.
	bool androidAssetsMode(SettingMap const & settings, Orkige::String & mode,
		Orkige::String * error);

	//! @brief is @p package a valid Android package name (two or more
	//! dot-separated identifiers)
	bool isValidAndroidPackage(Orkige::String const & package);

	//! @brief the Android LIBRARY ARCHIVES a project depends on, from
	//! `export.android.libraries`: a semicolon-separated list of
	//! project-relative `.aar` paths, returned in the order written.
	//!
	//! Nothing is downloaded and no dependency graph is resolved - a project
	//! points at files it already has (@see ExportAndroidLibrary.h). False with
	//! an honest @p error naming the offending entry when one is absolute,
	//! escapes the project with `..`, or is not an `.aar`: an export must not
	//! read a path a manifest can point anywhere.
	bool androidLibrarySettings(SettingMap const & settings,
		std::vector<Orkige::String> & out, Orkige::String * error);

	//--- machine-local signing material ------------------------
	// The identity/profile/keystore are developer-machine specific and must
	// never be committed - they come from the CLI or the environment. Only the
	// Team ID (export.ios.teamId) is a project-level, safe-to-commit value.

	extern const char * const IOS_SIGNING_IDENTITY_ENV;
	extern const char * const IOS_PROVISIONING_PROFILE_ENV;
	extern const char * const IOS_DISTRIBUTION_IDENTITY_ENV;
	extern const char * const IOS_DISTRIBUTION_PROFILE_ENV;
	extern const char * const ANDROID_KEYSTORE_ENV;
	extern const char * const ANDROID_KEY_ALIAS_ENV;
	extern const char * const ANDROID_KEYSTORE_PASS_ENV;
	extern const char * const ANDROID_KEY_PASS_ENV;
	extern const char * const BUNDLETOOL_ENV;

	//! @brief the environment as a lookup, injected so every resolver below is
	//! testable without touching the real one
	typedef std::map<Orkige::String, Orkige::String> EnvironmentMap;

	//! @brief an (identity, profile) pair; each empty when unresolved, which
	//! is what the export gates refuse on
	struct SigningPair
	{
		Orkige::String identity;
		Orkige::String profile;
	};

	//! @brief resolve the DEVELOPMENT iOS identity + profile: the CLI argument
	//! wins, else the environment. Whitespace-only reads as absent.
	SigningPair resolveIosSigning(Orkige::String const & identityArg,
		Orkige::String const & profileArg, EnvironmentMap const & environment);

	//! @brief resolve the DISTRIBUTION (App Store) identity + profile off
	//! their OWN environment pair, independent of the development one.
	SigningPair resolveIosDistributionSigning(
		Orkige::String const & identityArg, Orkige::String const & profileArg,
		EnvironmentMap const & environment);

	//! @brief the release keystore + alias for a signed .aab. Passwords are
	//! NOT returned - they stay in the environment and are read straight by
	//! jarsigner via -storepass:env, so no secret ever reaches a command line.
	struct AndroidKeystore
	{
		Orkige::String	keystore;
		Orkige::String	alias;
		bool			hasStorePassword = false;
	};

	//! @see AndroidKeystore
	AndroidKeystore resolveAndroidKeystore(Orkige::String const & keystoreArg,
		Orkige::String const & aliasArg, EnvironmentMap const & environment);

	//! @brief resolve the bundletool jar: the CLI argument wins, else
	//! ORKIGE_BUNDLETOOL, else a `bundletool` launcher found by @p which
	//! (injected so the precedence is testable without bundletool installed).
	Orkige::String resolveBundletool(Orkige::String const & bundletoolArg,
		EnvironmentMap const & environment,
		Orkige::String (*which)(Orkige::String const &));

	//--- reporting ---------------------------------------------

	//! @brief a byte count as "512 B" / "1.5 MiB" - the size line an export
	//! ends on
	Orkige::String humanSize(unsigned long long byteCount);
}

#endif //__ExportSettings_h__31_7_2026__12_00_00__
