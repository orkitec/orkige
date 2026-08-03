/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorBuildSettingsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// The two groups of build settings and the line between them: the matrix
// (platform x purpose), the committed manifest vocabulary, and - the case this
// file exists for - the proof that a credential set in the editor does not
// appear anywhere in the project a person commits.
#include <catch2/catch_test_macros.hpp>

#include "EditorBuildSettings.h"
#include "EditorSecretStore.h"

#include <core_project/Project.h>

#ifdef ORKIGE_HAVE_EXPORTER
#include <ExportSettings.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace OrkigeEditor;

namespace
{
	//! a temp directory that removes itself, so a failing assertion still
	//! leaves the machine clean
	struct ScopedDirectory
	{
		std::filesystem::path path;

		explicit ScopedDirectory(std::string const & name)
		{
			this->path = std::filesystem::temp_directory_path() /
				("orkige_buildsettings_" + name);
			std::error_code ignored;
			std::filesystem::remove_all(this->path, ignored);
			std::filesystem::create_directories(this->path, ignored);
		}
		~ScopedDirectory()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->path, ignored);
		}
	};

	//! point the editor's writable state directory at a temp tree for the
	//! duration of a test (the documented isolation seam - the real one
	//! follows the user account, not HOME)
	struct ScopedStateDirectory
	{
		std::string previous;
		bool hadPrevious = false;

		explicit ScopedStateDirectory(std::filesystem::path const & directory)
		{
			if(const char * const existing =
				std::getenv("ORKIGE_EDITOR_STATE_DIR"))
			{
				this->previous = existing;
				this->hadPrevious = true;
			}
			set((directory.string() + "/").c_str());
		}
		~ScopedStateDirectory()
		{
			set(this->hadPrevious ? this->previous.c_str() : "");
		}

		static void set(const char * value)
		{
#ifdef _WIN32
			_putenv_s("ORKIGE_EDITOR_STATE_DIR", value);
#else
			if(value[0] == '\0') { ::unsetenv("ORKIGE_EDITOR_STATE_DIR"); }
			else { ::setenv("ORKIGE_EDITOR_STATE_DIR", value, 1); }
#endif
		}
	};

	std::string readWholeFile(std::filesystem::path const & path)
	{
		std::ifstream file(path, std::ios::binary);
		std::ostringstream contents;
		contents << file.rdbuf();
		return contents.str();
	}

	//! the credential store as a map, so "the password went to the vault and
	//! nowhere else" is one assertion on each side. Never the real one: the
	//! suite must not prompt for keychain access or read whoever runs it.
	class CountingVault : public OrkigeEditor::SecretVault
	{
	public:
		std::map<std::string, std::string> entries;

		Orkige::String name() const override { return "Test Vault"; }

		OrkigeEditor::SecretResult read(
			Orkige::String const & account) const override
		{
			OrkigeEditor::SecretResult result;
			const std::map<std::string, std::string>::const_iterator found =
				this->entries.find(account);
			if(found == this->entries.end())
			{
				result.status = OrkigeEditor::SecretStatus::Missing;
				return result;
			}
			result.status = OrkigeEditor::SecretStatus::Ok;
			result.value = found->second;
			return result;
		}

		OrkigeEditor::SecretResult write(Orkige::String const & account,
			Orkige::String const & secret) override
		{
			this->entries[account] = secret;
			OrkigeEditor::SecretResult result;
			result.status = OrkigeEditor::SecretStatus::Ok;
			return result;
		}

		OrkigeEditor::SecretResult erase(
			Orkige::String const & account) override
		{
			this->entries.erase(account);
			OrkigeEditor::SecretResult result;
			result.status = OrkigeEditor::SecretStatus::Ok;
			return result;
		}
	};

	struct ScopedVault
	{
		explicit ScopedVault(OrkigeEditor::SecretVault * vault)
		{
			OrkigeEditor::setSecretVault(vault);
		}
		~ScopedVault() { OrkigeEditor::setSecretVault(0); }
	};

	//! the Android keystore password slot, straight out of the model
	OrkigeEditor::BuildCredentialSlot keystorePasswordSlot()
	{
		for(BuildTargetCell const & cell : buildTargetMatrix())
		{
			for(BuildCredentialSlot const & slot : cell.slots)
			{
				if(slot.vaultKey == "android.release.keystorePassword")
				{
					return slot;
				}
			}
		}
		return OrkigeEditor::BuildCredentialSlot();
	}
}

TEST_CASE("the matrix covers every platform for both purposes",
	"[buildsettings]")
{
	const std::vector<BuildTargetCell> cells = buildTargetMatrix();
	const std::vector<Orkige::String> platforms = buildPlatformOrder();
	REQUIRE(platforms.size() == 4);
	for(Orkige::String const & platform : platforms)
	{
		int development = 0;
		int distribution = 0;
		for(BuildTargetCell const & cell : cells)
		{
			if(cell.platform != platform) { continue; }
			if(cell.purpose == BuildPurpose::Development) { ++development; }
			else { ++distribution; }
		}
		INFO("platform " << platform);
		CHECK(development == 1);
		CHECK(distribution == 1);
	}
	CHECK(cells.size() == platforms.size() * 2);
}

TEST_CASE("development and distribution never share a credential slot",
	"[buildsettings]")
{
	// the reason this is a matrix: pasting a distribution identity into the
	// development field would stop Play-on-a-device working, so the two pairs
	// must be distinct keys AND distinct environment variables
	const std::vector<BuildTargetCell> cells = buildTargetMatrix();
	std::vector<Orkige::String> keys;
	std::vector<Orkige::String> variables;
	for(BuildTargetCell const & cell : cells)
	{
		for(BuildCredentialSlot const & slot : cell.slots)
		{
			if(!slot.key.empty()) { keys.push_back(slot.key); }
			if(!slot.environmentVariable.empty())
			{
				variables.push_back(slot.environmentVariable);
			}
		}
	}
	std::vector<Orkige::String> uniqueKeys = keys;
	std::sort(uniqueKeys.begin(), uniqueKeys.end());
	uniqueKeys.erase(std::unique(uniqueKeys.begin(), uniqueKeys.end()),
		uniqueKeys.end());
	CHECK(uniqueKeys.size() == keys.size());

	std::vector<Orkige::String> uniqueVariables = variables;
	std::sort(uniqueVariables.begin(), uniqueVariables.end());
	uniqueVariables.erase(
		std::unique(uniqueVariables.begin(), uniqueVariables.end()),
		uniqueVariables.end());
	CHECK(uniqueVariables.size() == variables.size());
}

TEST_CASE("an empty cell says what happens instead of showing a blank field",
	"[buildsettings]")
{
	for(BuildTargetCell const & cell : buildTargetMatrix())
	{
		INFO(cell.platform << " / " << buildPurposeLabel(cell.purpose));
		CHECK_FALSE(cell.label.empty());
		// a doc STEM, never a URL and never a path - what makes the reference
		// checkable against the corpus on disk (the spellings are pinned in a
		// comment beside the matrix, which the doc-link lint reads)
		CHECK_FALSE(cell.helpPage.empty());
		CHECK(cell.helpPage.find('/') == Orkige::String::npos);
		CHECK(cell.helpPage.find(".md") == Orkige::String::npos);
		CHECK(cell.helpPage.find(':') == Orkige::String::npos);
		if(cell.state == BuildCellState::Automatic)
		{
			CHECK(cell.slots.empty());
			CHECK_FALSE(cell.note.empty());
		}
		else if(cell.state == BuildCellState::Pending)
		{
			CHECK_FALSE(cell.note.empty());
		}
		else
		{
			CHECK_FALSE(cell.slots.empty());
		}
	}
}

TEST_CASE("a password has no key, so it cannot be stored", "[buildsettings]")
{
	bool sawSecret = false;
	for(BuildTargetCell const & cell : buildTargetMatrix())
	{
		for(BuildCredentialSlot const & slot : cell.slots)
		{
			if(slot.storage == BuildCredentialStorage::Secret)
			{
				sawSecret = true;
				// structurally unstorable: no key means no route into the file
				CHECK(slot.key.empty());
				CHECK_FALSE(isMachineSettingKey(slot.key));
			}
		}
	}
	CHECK(sawSecret);
	// ...and no storable key is password-shaped
	for(Orkige::String const & key : machineSettingKeys())
	{
		INFO(key);
		CHECK(key.find("assword") == Orkige::String::npos);
		CHECK(key.find("ecret") == Orkige::String::npos);
	}
}

TEST_CASE("only an Applied cell contributes storable keys", "[buildsettings]")
{
	const std::vector<Orkige::String> keys = machineSettingKeys();
	CHECK(keys.size() == 7);	// the two iOS pairs + the Android release trio
	for(BuildTargetCell const & cell : buildTargetMatrix())
	{
		if(cell.state == BuildCellState::Applied) { continue; }
		for(BuildCredentialSlot const & slot : cell.slots)
		{
			INFO(cell.platform << " " << slot.label);
			CHECK_FALSE(isMachineSettingKey(slot.key));
		}
	}
}

#ifdef ORKIGE_HAVE_EXPORTER
TEST_CASE("the model names the exporter's own environment variables",
	"[buildsettings]")
{
	// the drift alarm: the credential model mirrors these names rather than
	// including the exporter, so this is the one place both spellings are
	// visible at once
	std::vector<std::pair<Orkige::String, Orkige::String>> expected;
	expected.emplace_back("ios.development.identity",
		OrkigeExport::IOS_SIGNING_IDENTITY_ENV);
	expected.emplace_back("ios.development.profile",
		OrkigeExport::IOS_PROVISIONING_PROFILE_ENV);
	expected.emplace_back("ios.distribution.identity",
		OrkigeExport::IOS_DISTRIBUTION_IDENTITY_ENV);
	expected.emplace_back("ios.distribution.profile",
		OrkigeExport::IOS_DISTRIBUTION_PROFILE_ENV);
	expected.emplace_back("android.release.keystore",
		OrkigeExport::ANDROID_KEYSTORE_ENV);
	expected.emplace_back("android.release.keyAlias",
		OrkigeExport::ANDROID_KEY_ALIAS_ENV);
	expected.emplace_back("android.release.bundletool",
		OrkigeExport::BUNDLETOOL_ENV);

	std::map<Orkige::String, Orkige::String> byKey;
	std::vector<Orkige::String> secretVariables;
	for(BuildTargetCell const & cell : buildTargetMatrix())
	{
		for(BuildCredentialSlot const & slot : cell.slots)
		{
			if(!slot.key.empty())
			{
				byKey[slot.key] = slot.environmentVariable;
			}
			else if(slot.storage == BuildCredentialStorage::Secret &&
				!slot.environmentVariable.empty())
			{
				secretVariables.push_back(slot.environmentVariable);
			}
		}
	}
	for(std::pair<Orkige::String, Orkige::String> const & pair : expected)
	{
		INFO(pair.first);
		REQUIRE(byKey.count(pair.first) == 1);
		CHECK(byKey.at(pair.first) == pair.second);
	}
	CHECK(byKey.size() == expected.size());
	// the passwords: named so a person can set them, stored nowhere
	CHECK(std::find(secretVariables.begin(), secretVariables.end(),
		Orkige::String(OrkigeExport::ANDROID_KEYSTORE_PASS_ENV)) !=
		secretVariables.end());
	CHECK(std::find(secretVariables.begin(), secretVariables.end(),
		Orkige::String(OrkigeExport::ANDROID_KEY_PASS_ENV)) !=
		secretVariables.end());
}
#endif

TEST_CASE("the two groups are disjoint vocabularies", "[buildsettings]")
{
	for(Orkige::String const & key : machineSettingKeys())
	{
		INFO(key);
		CHECK_FALSE(isProjectSettingKey(key));
		// a machine key is never manifest-shaped either, so a mistaken
		// setSetting call would be visible on sight
		CHECK(key.rfind("export.", 0) != 0);
	}
	for(ProjectSettingRow const & row : projectSettingRows())
	{
		INFO(row.key);
		CHECK_FALSE(isMachineSettingKey(row.key));
		CHECK(row.key.rfind("export.", 0) == 0);
		CHECK_FALSE(row.label.empty());
		if(row.kind == ProjectSettingKind::Choice)
		{
			CHECK_FALSE(row.choices.empty());
		}
	}
}

TEST_CASE("the machine store keeps only declared keys", "[buildsettings]")
{
	BuildSettingMap values;
	values["ios.development.identity"] = "Apple Development: me (ABCDE12345)";
	values["export.ios.teamId"] = "ABCDE12345";	// a project setting: dropped
	values["android.release.keystorePassword"] = "hunter2";	// no such key
	values["android.release.keyAlias"] = "  yourgame  ";	// trimmed
	values["android.release.bundletool"] = "   ";	// blank: not kept at all

	const BuildSettingMap kept = sanitizeBuildSettings(values);
	CHECK(kept.size() == 2);
	CHECK(kept.count("ios.development.identity") == 1);
	CHECK(kept.at("android.release.keyAlias") == "yourgame");
	CHECK(kept.count("export.ios.teamId") == 0);
	CHECK(kept.count("android.release.keystorePassword") == 0);
	CHECK(kept.count("android.release.bundletool") == 0);

	// ...and a hand-edited file is read through the same gate
	const Orkige::String text = serializeBuildSettings(values) +
		"android.release.keystorePassword = hunter2\n"
		"export.ios.teamId = ABCDE12345\n";
	const BuildSettingMap round = parseBuildSettings(text);
	CHECK(round == kept);
	CHECK(text.find("hunter2") != Orkige::String::npos);	// the input had it...
	CHECK(serializeBuildSettings(round).find("hunter2") ==
		Orkige::String::npos);	// ...and a written file never does
}

TEST_CASE("machine settings are per project and outside every project tree",
	"[buildsettings]")
{
	ScopedDirectory state("paths");
	ScopedStateDirectory redirect(state.path);

	const Orkige::String projectA = "/Users/someone/games/game";
	const Orkige::String projectB = "/Users/someone/other/game";
	CHECK(buildSettingsFileName(projectA) != buildSettingsFileName(projectB));
	// a trailing separator is the same project, not a second one
	CHECK(buildSettingsFileName(projectA) ==
		buildSettingsFileName(projectA + "/"));
	// readable at a glance, and never a path
	CHECK(buildSettingsFileName(projectA).rfind("game-", 0) == 0);
	CHECK(buildSettingsFileName(projectA).find('/') == Orkige::String::npos);
	CHECK(buildSettingsFileName("/Users/someone/my games/A Game!")
		.find(' ') == Orkige::String::npos);

	const Orkige::String path = buildSettingsPath(projectA);
	REQUIRE_FALSE(path.empty());
	// the whole point: the file lives under the editor's own state directory,
	// never inside the project a person commits
	CHECK(path.rfind(state.path.string(), 0) == 0);
	CHECK(path.rfind(projectA, 0) != 0);
}

TEST_CASE("a saved credential file is owner-only and holds no password",
	"[buildsettings]")
{
	ScopedDirectory state("save");
	ScopedStateDirectory redirect(state.path);

	const Orkige::String projectRoot = "/Users/someone/games/signed";
	BuildSettingMap values;
	values["android.release.keystore"] = "/Users/someone/keys/upload.jks";
	values["android.release.keyAlias"] = "signed";
	Orkige::String error;
	REQUIRE(saveBuildSettings(projectRoot, values, &error));
	CHECK(error.empty());

	const std::filesystem::path path = buildSettingsPath(projectRoot);
	REQUIRE(std::filesystem::exists(path));
	const std::filesystem::perms permissions =
		std::filesystem::status(path).permissions();
	CHECK((permissions & std::filesystem::perms::group_all) ==
		std::filesystem::perms::none);
	CHECK((permissions & std::filesystem::perms::others_all) ==
		std::filesystem::perms::none);

	const BuildSettingMap reloaded = loadBuildSettings(projectRoot);
	CHECK(reloaded == sanitizeBuildSettings(values));

	// an unconfigured project reads as empty rather than as an error
	CHECK(loadBuildSettings("/Users/someone/games/other").empty());
}

TEST_CASE("credentials map onto the export request's own fields",
	"[buildsettings]")
{
	BuildSettingMap values;
	values["ios.development.identity"] = "Apple Development: me";
	values["ios.development.profile"] = "/keys/dev.mobileprovision";
	values["ios.distribution.identity"] = "Apple Distribution: me";
	values["ios.distribution.profile"] = "/keys/store.mobileprovision";
	values["android.release.keystore"] = "/keys/upload.jks";
	values["android.release.keyAlias"] = "upload";
	values["android.release.bundletool"] = "/tools/bundletool.jar";

	const BuildCredentials credentials = buildCredentialsFrom(values);
	CHECK(credentials.iosIdentity == "Apple Development: me");
	CHECK(credentials.iosProfile == "/keys/dev.mobileprovision");
	CHECK(credentials.iosDistributionIdentity == "Apple Distribution: me");
	CHECK(credentials.iosDistributionProfile == "/keys/store.mobileprovision");
	CHECK(credentials.androidKeystore == "/keys/upload.jks");
	CHECK(credentials.androidKeyAlias == "upload");
	CHECK(credentials.bundletool == "/tools/bundletool.jar");

	// nothing configured hands over nothing, so the exporter's environment
	// fallback still decides
	const BuildCredentials none = buildCredentialsFrom(BuildSettingMap());
	CHECK(none.iosIdentity.empty());
	CHECK(none.androidKeystore.empty());
}

TEST_CASE("a credential set in the editor never reaches the project",
	"[buildsettings]")
{
	// the acceptance proof for the whole file: set every storable credential
	// AND a password, write the project settings a person WOULD commit, save
	// the manifest, and read the bytes back - out of the project a person
	// commits AND out of every file the editor itself writes - looking for any
	// of the secrets. The password must be in the vault and nowhere else.
	ScopedDirectory state("split_state");
	ScopedDirectory projectDirectory("split_project");
	ScopedStateDirectory redirect(state.path);
	CountingVault vault;
	ScopedVault installed(&vault);
	const Orkige::String projectRoot = projectDirectory.path.string();

	BuildSettingMap credentials;
	credentials["ios.development.identity"] =
		"Apple Development: nobody (SECRETIDENT1)";
	credentials["ios.development.profile"] =
		"/Users/nobody/keys/SECRETPROFILE1.mobileprovision";
	credentials["ios.distribution.identity"] =
		"Apple Distribution: nobody (SECRETIDENT2)";
	credentials["ios.distribution.profile"] =
		"/Users/nobody/keys/SECRETPROFILE2.mobileprovision";
	credentials["android.release.keystore"] =
		"/Users/nobody/keys/SECRETKEYSTORE.jks";
	credentials["android.release.keyAlias"] = "SECRETALIAS";
	credentials["android.release.bundletool"] =
		"/Users/nobody/tools/SECRETBUNDLETOOL.jar";
	Orkige::String error;
	REQUIRE(saveBuildSettings(projectRoot, credentials, &error));

	// ...and the password, which takes the other road entirely
	const OrkigeEditor::BuildCredentialSlot password = keystorePasswordSlot();
	REQUIRE_FALSE(password.vaultKey.empty());
	REQUIRE(OrkigeEditor::storeSecret(password, projectRoot,
		"SECRETPASSPHRASE", &error));
	REQUIRE(vault.entries.size() == 1);
	CHECK(vault.entries.begin()->second == "SECRETPASSPHRASE");

	Orkige::Project project;
	Orkige::String createError;
	REQUIRE(Orkige::Project::create(projectRoot, "split", project,
		&createError));
	for(ProjectSettingRow const & row : projectSettingRows())
	{
		// the committed group, exactly as the settings surface writes it
		project.setSetting(row.key, row.defaultValue.empty() ? "committed"
			: row.defaultValue);
	}
	Orkige::String saveError;
	REQUIRE(project.save(&saveError));

	const std::filesystem::path manifest =
		projectDirectory.path / "project.orkproj";
	REQUIRE(std::filesystem::exists(manifest));
	const std::string manifestText = readWholeFile(manifest);
	for(BuildSettingMap::value_type const & entry : credentials)
	{
		INFO(entry.first);
		CHECK(manifestText.find(entry.second) == std::string::npos);
		CHECK(manifestText.find(entry.first) == std::string::npos);
	}
	CHECK(manifestText.find("SECRET") == std::string::npos);
	// the committed group DID land, so the test is not passing by writing
	// nothing at all
	CHECK(manifestText.find("export.ios.teamId") != std::string::npos);

	// ...and no credential file was left anywhere under the project
	for(std::filesystem::directory_entry const & entry :
		std::filesystem::recursive_directory_iterator(projectDirectory.path))
	{
		INFO(entry.path().string());
		CHECK(entry.path().extension() != ".buildsettings");
	}

	// THE password rule, from both sides. Every file the editor itself wrote
	// - the machine-settings file included, which is where a plaintext one
	// would have gone - is read back byte for byte: the password appears in
	// none of them, and neither does the key that would name it.
	bool sawEditorFile = false;
	for(std::filesystem::directory_entry const & entry :
		std::filesystem::recursive_directory_iterator(state.path))
	{
		if(!entry.is_regular_file())
		{
			continue;
		}
		sawEditorFile = true;
		const std::string text = readWholeFile(entry.path());
		INFO(entry.path().string());
		CHECK(text.find("SECRETPASSPHRASE") == std::string::npos);
		CHECK(text.find(password.vaultKey) == std::string::npos);
		// the machine group DID land in one of them, so this is not passing
		// by writing nothing at all
		if(text.find("SECRETKEYSTORE") != std::string::npos)
		{
			CHECK(text.find("android.release.keystore") != std::string::npos);
		}
	}
	CHECK(sawEditorFile);
	// the vault is what holds it, under this project's own account
	CHECK(vault.entries.count(OrkigeEditor::secretVaultAccount(projectRoot,
		password.vaultKey)) == 1);
	project.close();	// leave no process-wide asset database behind
}
