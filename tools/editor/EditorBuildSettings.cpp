/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorBuildSettings.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "EditorBuildSettings.h"

#include "EditorResourcePaths.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		//! the environment variables the exporter falls back to. Spelled here
		//! because this library stays free of the exporter (@see
		//! EditorExportPlan.h, which keeps the same distance); the settings
		//! self-check asserts these against OrkigeExport's own constants from
		//! the editor executable, which sees both.
		const char * const IOS_IDENTITY_ENV = "ORKIGE_IOS_SIGNING_IDENTITY";
		const char * const IOS_PROFILE_ENV = "ORKIGE_IOS_PROVISIONING_PROFILE";
		const char * const IOS_DIST_IDENTITY_ENV =
			"ORKIGE_IOS_DISTRIBUTION_IDENTITY";
		const char * const IOS_DIST_PROFILE_ENV =
			"ORKIGE_IOS_DISTRIBUTION_PROFILE";
		const char * const ANDROID_KEYSTORE_ENV = "ORKIGE_ANDROID_KEYSTORE";
		const char * const ANDROID_KEY_ALIAS_ENV = "ORKIGE_ANDROID_KEY_ALIAS";
		const char * const ANDROID_KEYSTORE_PASS_ENV =
			"ORKIGE_ANDROID_KEYSTORE_PASS";
		const char * const ANDROID_KEY_PASS_ENV = "ORKIGE_ANDROID_KEY_PASS";
		const char * const BUNDLETOOL_ENV = "ORKIGE_BUNDLETOOL";

		//! the machine-store subdirectory, under the editor's writable state
		const char * const SETTINGS_DIR_NAME = "buildsettings";
		const char * const SETTINGS_SUFFIX = ".buildsettings";

		Orkige::String trimmed(Orkige::String const & text)
		{
			std::size_t first = 0;
			while(first < text.size() &&
				(text[first] == ' ' || text[first] == '\t' ||
				 text[first] == '\r' || text[first] == '\n'))
			{
				++first;
			}
			std::size_t last = text.size();
			while(last > first &&
				(text[last - 1] == ' ' || text[last - 1] == '\t' ||
				 text[last - 1] == '\r' || text[last - 1] == '\n'))
			{
				--last;
			}
			return text.substr(first, last - first);
		}

		//! a Machine slot, spelled once
		BuildCredentialSlot machineSlot(const char * key, const char * label,
			const char * environmentVariable, bool isPath, const char * hint)
		{
			BuildCredentialSlot slot;
			slot.key = key;
			slot.label = label;
			slot.environmentVariable = environmentVariable;
			slot.storage = BuildCredentialStorage::Machine;
			slot.isPath = isPath;
			slot.hint = hint;
			return slot;
		}

		//! a password: no settings-file key, so there is no way to persist it
		//! there; @p vaultKey (may be "") names it in the platform's own
		//! credential store instead
		BuildCredentialSlot secretSlot(const char * label,
			const char * environmentVariable, const char * vaultKey,
			const char * hint)
		{
			BuildCredentialSlot slot;
			slot.vaultKey = vaultKey;
			slot.label = label;
			slot.environmentVariable = environmentVariable;
			slot.storage = BuildCredentialStorage::Secret;
			slot.hint = hint;
			return slot;
		}
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> buildPlatformOrder()
	{
		return { "ios", "android", "macos", "windows" };
	}
	//---------------------------------------------------------
	Orkige::String buildPlatformLabel(Orkige::String const & platform)
	{
		if(platform == "ios") { return "iOS"; }
		if(platform == "android") { return "Android"; }
		if(platform == "macos") { return "macOS"; }
		if(platform == "windows") { return "Windows"; }
		return platform;
	}
	//---------------------------------------------------------
	Orkige::String buildPurposeLabel(BuildPurpose purpose)
	{
		return purpose == BuildPurpose::Development ? "Development"
			: "Distribution";
	}
	//---------------------------------------------------------
	std::vector<BuildTargetCell> buildTargetMatrix()
	{
		// the pages the cells below point at, spelled once as doc NAMES so the
		// doc-link lint reads them off this comment and fails the build if one
		// is renamed: Docs/ios-signing.md, Docs/store-release.md,
		// Docs/device-payloads.md
		std::vector<BuildTargetCell> cells;

		//--- iOS ---------------------------------------------
		{
			// installing on a physical device needs a DEVELOPMENT identity and
			// a profile that names that device; the editor's Play-on-a-device
			// target is exactly this export
			BuildTargetCell cell;
			cell.platform = "ios";
			cell.purpose = BuildPurpose::Development;
			cell.label = "Play on a device / development install";
			cell.helpPage = "ios-signing";
			cell.state = BuildCellState::Applied;
			cell.slots.push_back(machineSlot("ios.development.identity",
				"Signing identity", IOS_IDENTITY_ENV, false,
				"an Apple Development identity from your keychain "
				"(security find-identity -v -p codesigning)"));
			cell.slots.push_back(machineSlot("ios.development.profile",
				"Provisioning profile", IOS_PROFILE_ENV, true,
				"the .mobileprovision naming the app id and the devices it "
				"may install on"));
			cells.push_back(cell);
		}
		{
			// a store upload is signed with a DIFFERENT pair, which is why the
			// export request carries four fields rather than two: a
			// distribution identity installs on no development device, and a
			// development one is rejected by App Store Connect
			BuildTargetCell cell;
			cell.platform = "ios";
			cell.purpose = BuildPurpose::Distribution;
			cell.label = "App Store upload (.ipa)";
			cell.helpPage = "store-release";
			cell.state = BuildCellState::Applied;
			cell.slots.push_back(machineSlot("ios.distribution.identity",
				"Distribution identity", IOS_DIST_IDENTITY_ENV, false,
				"an Apple Distribution identity - not the development one"));
			cell.slots.push_back(machineSlot("ios.distribution.profile",
				"Distribution profile", IOS_DIST_PROFILE_ENV, true,
				"the App Store (or ad-hoc) .mobileprovision"));
			cells.push_back(cell);
		}

		//--- Android -----------------------------------------
		{
			// nothing to configure, and saying so beats an empty box a person
			// wonders about: Android installs no unsigned package, so the
			// packaging script creates the shared debug keystore on demand
			BuildTargetCell cell;
			cell.platform = "android";
			cell.purpose = BuildPurpose::Development;
			cell.label = "Play / debug APK";
			cell.helpPage = "device-payloads";
			cell.state = BuildCellState::Automatic;
			cell.note = "Nothing to configure. A debug APK is signed with the "
				"shared Android debug keystore, which the packaging step "
				"creates on this machine the first time it is needed. What a "
				"debug package DOES need is the Android SDK build tools and a "
				"JDK - the export names any missing one.";
			cells.push_back(cell);
		}
		{
			BuildTargetCell cell;
			cell.platform = "android";
			cell.purpose = BuildPurpose::Distribution;
			cell.label = "Release App Bundle (.aab)";
			cell.helpPage = "store-release";
			cell.state = BuildCellState::Applied;
			cell.slots.push_back(machineSlot("android.release.keystore",
				"Release keystore", ANDROID_KEYSTORE_ENV, true,
				"the .jks you keep forever - losing it means you can no "
				"longer update the app"));
			cell.slots.push_back(machineSlot("android.release.keyAlias",
				"Key alias", ANDROID_KEY_ALIAS_ENV, false,
				"the alias inside the keystore that signs this app"));
			cell.slots.push_back(secretSlot("Keystore password",
				ANDROID_KEYSTORE_PASS_ENV, "android.release.keystorePassword",
				"kept in this machine's credential store, never in a file; "
				"the signing step reads it from the environment, so it never "
				"reaches a command line either"));
			cell.slots.push_back(secretSlot("Key password",
				ANDROID_KEY_PASS_ENV, "android.release.keyPassword",
				"only when the key uses a different password than the "
				"keystore"));
			cell.slots.push_back(machineSlot("android.release.bundletool",
				"bundletool jar", BUNDLETOOL_ENV, true,
				"the standalone bundletool jar that assembles the bundle "
				"(not part of the SDK build tools)"));
			cells.push_back(cell);
		}

		//--- macOS -------------------------------------------
		{
			BuildTargetCell cell;
			cell.platform = "macos";
			cell.purpose = BuildPurpose::Development;
			cell.label = "Local build";
			cell.helpPage = "store-release";
			cell.state = BuildCellState::Automatic;
			cell.note = "Nothing to configure. A macOS export is signed "
				"ad-hoc, which is enough to run it on this machine.";
			cells.push_back(cell);
		}
		{
			// present so the shape is visible and the desktop cells are a fill-in
			// rather than a redesign; disabled and unstored so nothing here
			// pretends to act
			BuildTargetCell cell;
			cell.platform = "macos";
			cell.purpose = BuildPurpose::Distribution;
			cell.label = "Distribute to other Macs";
			cell.helpPage = "store-release";
			cell.state = BuildCellState::Pending;
			cell.note = "Not wired yet. An exported app is signed ad-hoc, "
				"which runs here and is refused on anyone else's Mac. "
				"Distributing one needs a Developer ID identity and "
				"notarization, which project exports do not do yet - so "
				"these fields are shown, not stored.";
			cell.slots.push_back(machineSlot("", "Developer ID identity", "",
				false, "the Developer ID Application identity that signs a "
				"distributable app"));
			cell.slots.push_back(machineSlot("", "Notarization profile", "",
				false, "the stored notarytool credential profile that submits "
				"the signed app to Apple"));
			cells.push_back(cell);
		}

		//--- Windows -----------------------------------------
		{
			BuildTargetCell cell;
			cell.platform = "windows";
			cell.purpose = BuildPurpose::Development;
			cell.label = "Local build";
			cell.helpPage = "store-release";
			cell.state = BuildCellState::Automatic;
			cell.note = "Nothing to configure. An exported executable is "
				"unsigned and runs here.";
			cells.push_back(cell);
		}
		{
			BuildTargetCell cell;
			cell.platform = "windows";
			cell.purpose = BuildPurpose::Distribution;
			cell.label = "Distribute to other PCs";
			cell.helpPage = "store-release";
			cell.state = BuildCellState::Pending;
			cell.note = "Not wired yet. An exported executable is unsigned, so "
				"people downloading it meet a SmartScreen warning. Removing it "
				"needs your own code-signing certificate, which project "
				"exports do not use yet - so these fields are shown, not "
				"stored.";
			cell.slots.push_back(machineSlot("", "Code-signing certificate",
				"", true, "the .pfx that signs the executable"));
			// no vault key: a Pending cell stores nothing, of either kind
			cell.slots.push_back(secretSlot("Certificate password", "", "",
				"nothing is kept for a signing step that does not run yet"));
			cells.push_back(cell);
		}
		return cells;
	}
	//---------------------------------------------------------
	std::vector<ProjectSettingRow> projectSettingRows()
	{
		std::vector<ProjectSettingRow> rows;
		auto add = [&rows](const char * key, const char * label,
			const char * platform, ProjectSettingKind kind,
			const char * defaultValue,
			std::vector<Orkige::String> const & choices, const char * hint)
		{
			ProjectSettingRow row;
			row.key = key;
			row.label = label;
			row.platform = platform;
			row.kind = kind;
			row.defaultValue = defaultValue;
			row.choices = choices;
			row.hint = hint;
			rows.push_back(std::move(row));
		};
		const std::vector<Orkige::String> none;

		add("export.orientation", "Screen orientation", "",
			ProjectSettingKind::Choice, "portrait",
			{ "portrait", "landscape", "auto" },
			"locks the exported app's orientation (iOS Info.plist + Android "
			"manifest) and the runtime window");
		add("export.icon", "App icon", "", ProjectSettingKind::Text, "", none,
			"a project-relative PNG resized into every platform's icon set; "
			"empty ships the neutral engine icon");
		add("export.launch.background", "Launch background", "",
			ProjectSettingKind::Text, "", none,
			"the launch screen's #RRGGBB fill");

		add("export.macos.bundleId", "Bundle id", "macos",
			ProjectSettingKind::Text, "", none,
			"empty derives com.orkitec.<slug>");

		add("export.ios.bundleId", "Bundle id", "ios",
			ProjectSettingKind::Text, "", none,
			"empty derives com.orkitec.<slug>");
		add("export.ios.teamId", "Team id", "ios", ProjectSettingKind::Text,
			"", none, "your 10-character Apple Developer Team ID - it "
			"identifies the project, not you, so it is safe to commit");

		add("export.android.package", "Package name", "android",
			ProjectSettingKind::Text, "", none,
			"two or more dot-separated identifiers; empty derives "
			"com.orkitec.<slug>");
		add("export.android.versionCode", "Version code", "android",
			ProjectSettingKind::Integer, "1", none,
			"a whole number that must STRICTLY INCREASE with every upload");
		add("export.android.versionName", "Version name", "android",
			ProjectSettingKind::Text, "1.0", none,
			"the version players see - any format");
		add("export.android.assets", "Assets", "android",
			ProjectSettingKind::Choice, "stored", { "stored", "compressed" },
			"stored: media stays uncompressed and the app reads it in place. "
			"compressed: smaller download, extracted on first launch");
		return rows;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> machineSettingKeys()
	{
		std::vector<Orkige::String> keys;
		const std::vector<BuildTargetCell> cells = buildTargetMatrix();
		for(BuildTargetCell const & cell : cells)
		{
			if(cell.state != BuildCellState::Applied)
			{
				// an Automatic cell has nothing, and a Pending one is shown
				// rather than stored - neither may reach the file
				continue;
			}
			for(BuildCredentialSlot const & slot : cell.slots)
			{
				if(slot.storage == BuildCredentialStorage::Machine &&
					!slot.key.empty())
				{
					keys.push_back(slot.key);
				}
			}
		}
		return keys;
	}
	//---------------------------------------------------------
	bool isMachineSettingKey(Orkige::String const & key)
	{
		const std::vector<Orkige::String> keys = machineSettingKeys();
		return std::find(keys.begin(), keys.end(), key) != keys.end();
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> secretVaultKeys()
	{
		std::vector<Orkige::String> keys;
		const std::vector<BuildTargetCell> cells = buildTargetMatrix();
		for(BuildTargetCell const & cell : cells)
		{
			if(cell.state != BuildCellState::Applied)
			{
				// same rule as the file store: a cell that is not applied
				// keeps nothing anywhere
				continue;
			}
			for(BuildCredentialSlot const & slot : cell.slots)
			{
				if(slot.storage == BuildCredentialStorage::Secret &&
					!slot.vaultKey.empty())
				{
					keys.push_back(slot.vaultKey);
				}
			}
		}
		return keys;
	}
	//---------------------------------------------------------
	bool isSecretVaultKey(Orkige::String const & key)
	{
		if(key.empty())
		{
			return false;
		}
		const std::vector<Orkige::String> keys = secretVaultKeys();
		return std::find(keys.begin(), keys.end(), key) != keys.end();
	}
	//---------------------------------------------------------
	bool isProjectSettingKey(Orkige::String const & key)
	{
		const std::vector<ProjectSettingRow> rows = projectSettingRows();
		for(ProjectSettingRow const & row : rows)
		{
			if(row.key == key)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	BuildSettingMap parseBuildSettings(Orkige::String const & text)
	{
		BuildSettingMap values;
		std::istringstream stream(text);
		std::string line;
		while(std::getline(stream, line))
		{
			const Orkige::String cleaned = trimmed(line);
			if(cleaned.empty() || cleaned[0] == '#')
			{
				continue;
			}
			const std::size_t separator = cleaned.find('=');
			if(separator == Orkige::String::npos)
			{
				continue;
			}
			const Orkige::String key = trimmed(cleaned.substr(0, separator));
			const Orkige::String value =
				trimmed(cleaned.substr(separator + 1));
			if(key.empty() || !isMachineSettingKey(key))
			{
				// a hand-edited file cannot smuggle a key the model does not
				// declare - the same gate a write goes through
				continue;
			}
			values[key] = value;
		}
		return values;
	}
	//---------------------------------------------------------
	BuildSettingMap sanitizeBuildSettings(BuildSettingMap const & values)
	{
		BuildSettingMap kept;
		for(BuildSettingMap::value_type const & entry : values)
		{
			if(isMachineSettingKey(entry.first) &&
				!trimmed(entry.second).empty())
			{
				kept[entry.first] = trimmed(entry.second);
			}
		}
		return kept;
	}
	//---------------------------------------------------------
	Orkige::String serializeBuildSettings(BuildSettingMap const & values)
	{
		const BuildSettingMap kept = sanitizeBuildSettings(values);
		Orkige::String text =
			"# Orkige build credentials for one project, on this machine "
			"only.\n"
			"# Never commit this file and never copy it into a project - it "
			"names\n"
			"# signing material belonging to you, not to the game. Passwords "
			"are\n"
			"# deliberately absent: they are read from the environment.\n";
		for(BuildSettingMap::value_type const & entry : kept)
		{
			text += entry.first + " = " + entry.second + "\n";
		}
		return text;
	}
	//---------------------------------------------------------
	Orkige::String buildProjectScopeId(Orkige::String const & projectRoot)
	{
		// normalise away trailing separators so `/games/game` and
		// `/games/game/` are the same project rather than two
		Orkige::String normalized = projectRoot;
		while(!normalized.empty() &&
			(normalized.back() == '/' || normalized.back() == '\\'))
		{
			normalized.pop_back();
		}
		Orkige::String leaf = normalized;
		const std::size_t separator = leaf.find_last_of("/\\");
		if(separator != Orkige::String::npos)
		{
			leaf = leaf.substr(separator + 1);
		}
		Orkige::String safe;
		for(char character : leaf)
		{
			const bool plain = (character >= 'a' && character <= 'z') ||
				(character >= 'A' && character <= 'Z') ||
				(character >= '0' && character <= '9') ||
				character == '-' || character == '_';
			safe += plain ? character : '_';
		}
		if(safe.empty())
		{
			safe = "project";
		}
		// ...and a digest of the WHOLE path, so two projects with the same
		// leaf name keep separate credentials (FNV-1a, which needs no
		// dependency and is only asked to separate paths)
		std::uint32_t hash = 2166136261u;
		for(char character : normalized)
		{
			hash ^= static_cast<std::uint32_t>(
				static_cast<unsigned char>(character));
			hash *= 16777619u;
		}
		char digest[9] = { 0 };
		const char * const hex = "0123456789abcdef";
		for(int index = 7; index >= 0; --index)
		{
			digest[index] = hex[hash & 0xFu];
			hash >>= 4;
		}
		return safe + "-" + digest;
	}
	//---------------------------------------------------------
	Orkige::String buildSettingsFileName(Orkige::String const & projectRoot)
	{
		return buildProjectScopeId(projectRoot) + SETTINGS_SUFFIX;
	}
	//---------------------------------------------------------
	Orkige::String buildSettingsDirectory()
	{
		const Orkige::String stateDir = editorWritableStateDirectory();
		if(stateDir.empty())
		{
			// deliberately NO fallback: the historical "beside the executable"
			// answer other editor state falls back to would put credentials
			// somewhere a person might copy or commit
			return Orkige::String();
		}
		const Orkige::String directory = stateDir + SETTINGS_DIR_NAME + "/";
		std::error_code createError;
		std::filesystem::create_directories(directory, createError);
		return directory;
	}
	//---------------------------------------------------------
	Orkige::String buildSettingsPath(Orkige::String const & projectRoot)
	{
		const Orkige::String directory = buildSettingsDirectory();
		if(directory.empty() || projectRoot.empty())
		{
			return Orkige::String();
		}
		return directory + buildSettingsFileName(projectRoot);
	}
	//---------------------------------------------------------
	BuildSettingMap loadBuildSettings(Orkige::String const & projectRoot)
	{
		const Orkige::String path = buildSettingsPath(projectRoot);
		if(path.empty())
		{
			return BuildSettingMap();
		}
		std::ifstream file(path.c_str(), std::ios::binary);
		if(!file.is_open())
		{
			return BuildSettingMap();	// nothing configured is the normal state
		}
		std::ostringstream contents;
		contents << file.rdbuf();
		return parseBuildSettings(contents.str());
	}
	//---------------------------------------------------------
	bool saveBuildSettings(Orkige::String const & projectRoot,
		BuildSettingMap const & values, Orkige::String * error)
	{
		const Orkige::String path = buildSettingsPath(projectRoot);
		if(path.empty())
		{
			if(error != 0)
			{
				*error = "this machine has no per-user application directory, "
					"so the editor cannot keep build credentials for you - "
					"set them in the environment instead";
			}
			return false;
		}
		const Orkige::String text = serializeBuildSettings(values);
		const Orkige::String temporary = path + ".tmp";
		{
			std::ofstream file(temporary.c_str(), std::ios::binary |
				std::ios::trunc);
			if(!file.is_open())
			{
				if(error != 0)
				{
					*error = "could not write '" + temporary + "'";
				}
				return false;
			}
			file << text;
			if(!file.good())
			{
				if(error != 0)
				{
					*error = "could not write '" + temporary + "'";
				}
				return false;
			}
		}
		// owner-only BEFORE the rename, so the file is never briefly readable
		// under its final name (the MCP token file's precedent)
		std::error_code permissionError;
		std::filesystem::permissions(temporary,
			std::filesystem::perms::owner_read |
			std::filesystem::perms::owner_write,
			std::filesystem::perm_options::replace, permissionError);
		std::error_code renameError;
		std::filesystem::rename(temporary, path, renameError);
		if(renameError)
		{
			std::error_code removeError;
			std::filesystem::remove(temporary, removeError);
			if(error != 0)
			{
				*error = "could not replace '" + path + "' - " +
					renameError.message();
			}
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	BuildCredentials buildCredentialsFrom(BuildSettingMap const & values)
	{
		const BuildSettingMap kept = sanitizeBuildSettings(values);
		auto valueOf = [&kept](const char * key)
		{
			const BuildSettingMap::const_iterator found = kept.find(key);
			return found != kept.end() ? found->second : Orkige::String();
		};
		BuildCredentials credentials;
		credentials.iosIdentity = valueOf("ios.development.identity");
		credentials.iosProfile = valueOf("ios.development.profile");
		credentials.iosDistributionIdentity =
			valueOf("ios.distribution.identity");
		credentials.iosDistributionProfile =
			valueOf("ios.distribution.profile");
		credentials.androidKeystore = valueOf("android.release.keystore");
		credentials.androidKeyAlias = valueOf("android.release.keyAlias");
		credentials.bundletool = valueOf("android.release.bundletool");
		return credentials;
	}
}
