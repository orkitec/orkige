/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportSettings.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportSettings.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace OrkigeExport
{
	const char * const DEFAULT_LAUNCH_BACKGROUND = "#12161f";
	const char * const PROJECT_MARKER_FILE_NAME = "orkige_project.txt";
	const char * const PAYLOAD_DIR_NAME = "project";
	const char * const PRIVACY_MANIFEST_FILE_NAME = "PrivacyInfo.xcprivacy";

	const char * const IOS_SIGNING_IDENTITY_ENV = "ORKIGE_IOS_SIGNING_IDENTITY";
	const char * const IOS_PROVISIONING_PROFILE_ENV =
		"ORKIGE_IOS_PROVISIONING_PROFILE";
	const char * const IOS_DISTRIBUTION_IDENTITY_ENV =
		"ORKIGE_IOS_DISTRIBUTION_IDENTITY";
	const char * const IOS_DISTRIBUTION_PROFILE_ENV =
		"ORKIGE_IOS_DISTRIBUTION_PROFILE";
	const char * const ANDROID_KEYSTORE_ENV = "ORKIGE_ANDROID_KEYSTORE";
	const char * const ANDROID_KEY_ALIAS_ENV = "ORKIGE_ANDROID_KEY_ALIAS";
	const char * const ANDROID_KEYSTORE_PASS_ENV = "ORKIGE_ANDROID_KEYSTORE_PASS";
	const char * const ANDROID_KEY_PASS_ENV = "ORKIGE_ANDROID_KEY_PASS";
	const char * const BUNDLETOOL_ENV = "ORKIGE_BUNDLETOOL";

	namespace
	{
		//---------------------------------------------------------
		Orkige::String trimmed(Orkige::String const & text)
		{
			std::size_t first = 0;
			std::size_t last = text.size();
			while(first < last && std::isspace(
				static_cast<unsigned char>(text[first])) != 0)
			{
				++first;
			}
			while(last > first && std::isspace(
				static_cast<unsigned char>(text[last - 1])) != 0)
			{
				--last;
			}
			return text.substr(first, last - first);
		}
		//---------------------------------------------------------
		Orkige::String lowered(Orkige::String const & text)
		{
			Orkige::String out;
			out.reserve(text.size());
			for(char character : text)
			{
				out += static_cast<char>(std::tolower(
					static_cast<unsigned char>(character)));
			}
			return out;
		}
		//---------------------------------------------------------
		Orkige::String lookup(SettingMap const & map, Orkige::String const & key)
		{
			SettingMap::const_iterator found = map.find(key);
			return (found == map.end()) ? Orkige::String() : found->second;
		}
		//---------------------------------------------------------
		//! the CLI argument wins, else the environment; whitespace-only on
		//! either side reads as absent (the gates then refuse rather than
		//! shelling out a broken identity)
		Orkige::String argOrEnv(Orkige::String const & argument,
			EnvironmentMap const & environment, const char * key)
		{
			const Orkige::String fromArgument = trimmed(argument);
			if(!fromArgument.empty())
			{
				return fromArgument;
			}
			return trimmed(lookup(environment, key));
		}
		//---------------------------------------------------------
		bool isHexColour(Orkige::String const & value)
		{
			if(value.size() != 7 || value[0] != '#')
			{
				return false;
			}
			for(std::size_t index = 1; index < value.size(); ++index)
			{
				if(std::isxdigit(static_cast<unsigned char>(value[index])) == 0)
				{
					return false;
				}
			}
			return true;
		}
		//---------------------------------------------------------
		//! one identifier of an Android package name: [A-Za-z_][A-Za-z0-9_]*
		bool isPackageIdentifier(Orkige::String const & part)
		{
			if(part.empty())
			{
				return false;
			}
			const unsigned char first = static_cast<unsigned char>(part[0]);
			if(std::isalpha(first) == 0 && part[0] != '_')
			{
				return false;
			}
			for(std::size_t index = 1; index < part.size(); ++index)
			{
				const unsigned char raw =
					static_cast<unsigned char>(part[index]);
				if(std::isalnum(raw) == 0 && part[index] != '_')
				{
					return false;
				}
			}
			return true;
		}
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> payloadSubdirs()
	{
		// DELIBERATELY without "tests": a project's `tests/*.test.lua` suite is
		// a development artefact - it is run against a project on a dev machine
		// (orkige_player --run-tests), never by a shipped game - so it stays out
		// of every payload BY CONSTRUCTION rather than by a later strip. Adding
		// it here would ship a game's test suite to players; the export suite
		// asserts its absence so that edit cannot pass silently.
		return { "scenes", "assets", "scripts", "data" };
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> configSettingKeys()
	{
		return { "input.actions", "physics.layers", "levels", "localisation" };
	}
	//---------------------------------------------------------
	Orkige::String cookPlatformToken(Orkige::String const & platform)
	{
		if(platform == "ios-simulator" || platform == "ios" ||
			platform == "ios-ipa")
		{
			return "ios";
		}
		if(platform == "android" || platform == "android-aab")
		{
			return "android";
		}
		if(platform == "web")
		{
			return "web";
		}
		return "";	// macos and anything else: the sidecar's default block
	}
	//---------------------------------------------------------
	Orkige::String launchBackground(SettingMap const & settings,
		Orkige::String * warn)
	{
		const Orkige::String value =
			trimmed(lookup(settings, "export.launch.background"));
		if(isHexColour(value))
		{
			return value;
		}
		if(!value.empty() && warn != 0)
		{
			*warn = "export.launch.background '" + value + "' is not a #RRGGBB "
				"colour - using the default " + DEFAULT_LAUNCH_BACKGROUND;
		}
		return DEFAULT_LAUNCH_BACKGROUND;
	}
	//---------------------------------------------------------
	Orkige::String orientationSetting(SettingMap const & settings,
		Orkige::String * warn)
	{
		const Orkige::String value =
			lowered(trimmed(lookup(settings, "export.orientation")));
		if(value == "portrait" || value == "landscape" || value == "auto")
		{
			return value;
		}
		if(!value.empty() && warn != 0)
		{
			*warn = "export.orientation '" + value + "' is not portrait/"
				"landscape/auto - using portrait";
		}
		return "portrait";
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> iosOrientations(
		Orkige::String const & orientation)
	{
		if(orientation == "landscape")
		{
			return { "UIInterfaceOrientationLandscapeLeft",
				"UIInterfaceOrientationLandscapeRight" };
		}
		if(orientation == "auto")
		{
			return { "UIInterfaceOrientationPortrait",
				"UIInterfaceOrientationLandscapeLeft",
				"UIInterfaceOrientationLandscapeRight" };
		}
		return { "UIInterfaceOrientationPortrait" };
	}
	//---------------------------------------------------------
	Orkige::String androidScreenOrientation(Orkige::String const & orientation)
	{
		if(orientation == "landscape")
		{
			return "sensorLandscape";
		}
		if(orientation == "auto")
		{
			return "unspecified";
		}
		return "sensorPortrait";
	}
	//---------------------------------------------------------
	bool androidVersion(SettingMap const & settings, int & versionCode,
		Orkige::String & versionName, Orkige::String * error)
	{
		Orkige::String codeText =
			trimmed(lookup(settings, "export.android.versionCode"));
		if(codeText.empty())
		{
			codeText = "1";
		}
		bool digitsOnly = true;
		for(char character : codeText)
		{
			if(std::isdigit(static_cast<unsigned char>(character)) == 0)
			{
				digitsOnly = false;
				break;
			}
		}
		const long parsed = digitsOnly ? std::strtol(codeText.c_str(), 0, 10) : 0;
		if(!digitsOnly || parsed < 1)
		{
			if(error != 0)
			{
				*error = "export.android.versionCode '" + codeText + "' is not "
					"a positive integer (Google Play requires a strictly "
					"increasing integer version code - see "
					"Docs/store-release.md)";
			}
			return false;
		}
		versionCode = static_cast<int>(parsed);
		versionName = trimmed(lookup(settings, "export.android.versionName"));
		if(versionName.empty())
		{
			versionName = "1.0";
		}
		return true;
	}
	//---------------------------------------------------------
	bool androidAssetsMode(SettingMap const & settings, Orkige::String & mode,
		Orkige::String * error)
	{
		Orkige::String value =
			trimmed(lookup(settings, "export.android.assets"));
		if(value.empty())
		{
			value = "stored";
		}
		if(value != "stored" && value != "compressed")
		{
			if(error != 0)
			{
				*error = "export.android.assets '" + value + "' is not "
					"'stored' or 'compressed' (see Docs/store-release.md)";
			}
			return false;
		}
		mode = value;
		return true;
	}
	//---------------------------------------------------------
	bool isValidAndroidPackage(Orkige::String const & package)
	{
		std::size_t start = 0;
		int parts = 0;
		while(start <= package.size())
		{
			const std::size_t dot = package.find('.', start);
			const Orkige::String part = (dot == Orkige::String::npos)
				? package.substr(start) : package.substr(start, dot - start);
			if(!isPackageIdentifier(part))
			{
				return false;
			}
			++parts;
			if(dot == Orkige::String::npos)
			{
				break;
			}
			start = dot + 1;
		}
		return parts >= 2;
	}
	//---------------------------------------------------------
	SigningPair resolveIosSigning(Orkige::String const & identityArg,
		Orkige::String const & profileArg, EnvironmentMap const & environment)
	{
		SigningPair pair;
		pair.identity =
			argOrEnv(identityArg, environment, IOS_SIGNING_IDENTITY_ENV);
		pair.profile =
			argOrEnv(profileArg, environment, IOS_PROVISIONING_PROFILE_ENV);
		return pair;
	}
	//---------------------------------------------------------
	SigningPair resolveIosDistributionSigning(
		Orkige::String const & identityArg, Orkige::String const & profileArg,
		EnvironmentMap const & environment)
	{
		SigningPair pair;
		pair.identity =
			argOrEnv(identityArg, environment, IOS_DISTRIBUTION_IDENTITY_ENV);
		pair.profile =
			argOrEnv(profileArg, environment, IOS_DISTRIBUTION_PROFILE_ENV);
		return pair;
	}
	//---------------------------------------------------------
	AndroidKeystore resolveAndroidKeystore(Orkige::String const & keystoreArg,
		Orkige::String const & aliasArg, EnvironmentMap const & environment)
	{
		AndroidKeystore resolved;
		resolved.keystore =
			argOrEnv(keystoreArg, environment, ANDROID_KEYSTORE_ENV);
		resolved.alias = argOrEnv(aliasArg, environment, ANDROID_KEY_ALIAS_ENV);
		resolved.hasStorePassword =
			!trimmed(lookup(environment, ANDROID_KEYSTORE_PASS_ENV)).empty();
		return resolved;
	}
	//---------------------------------------------------------
	Orkige::String resolveBundletool(Orkige::String const & bundletoolArg,
		EnvironmentMap const & environment,
		Orkige::String (*which)(Orkige::String const &))
	{
		const Orkige::String explicitPath =
			argOrEnv(bundletoolArg, environment, BUNDLETOOL_ENV);
		if(!explicitPath.empty())
		{
			return explicitPath;
		}
		return (which != 0) ? which("bundletool") : Orkige::String();
	}
	//---------------------------------------------------------
	Orkige::String humanSize(unsigned long long byteCount)
	{
		static const char * const UNITS[] = { "B", "KiB", "MiB", "GiB" };
		double value = static_cast<double>(byteCount);
		std::size_t unit = 0;
		while(value >= 1024.0 && unit + 1 < 4)
		{
			value /= 1024.0;
			++unit;
		}
		char buffer[64];
		if(unit == 0)
		{
			std::snprintf(buffer, sizeof(buffer), "%llu B", byteCount);
		}
		else
		{
			std::snprintf(buffer, sizeof(buffer), "%.1f %s", value,
				UNITS[unit]);
		}
		return buffer;
	}
}
