/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorSecretStoreTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Where a signing password lives: the keying (per project, per slot), the
// resolution order (environment, then vault, then not set), and the two
// properties this file exists to hold - that a password reaches the credential
// store and NOTHING ELSE, and that an automated run reaches no store at all.
//
// The vault here is a fake, deliberately: the whole point of the seam is that
// the suite never touches a real Keychain, never prompts, and never reads the
// credentials of whoever is running it.
#include <catch2/catch_test_macros.hpp>

#include "EditorBuildSettings.h"
#include "EditorSecretStore.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

using namespace OrkigeEditor;

namespace
{
	//! a credential store in memory, counting the calls that reach it - so a
	//! test can assert not just what was stored, but that nothing was asked of
	//! a vault at all
	class FakeVault : public SecretVault
	{
	public:
		std::map<std::string, std::string>	entries;
		mutable int							reads = 0;
		int									writes = 0;
		int									erases = 0;
		//! set to make every call fail the way a locked keyring would
		bool								refuse = false;

		Orkige::String name() const override { return "Test Vault"; }

		SecretResult read(Orkige::String const & account) const override
		{
			++this->reads;
			SecretResult result;
			if(this->refuse)
			{
				result.status = SecretStatus::Failed;
				result.message = "the store is locked";
				return result;
			}
			const std::map<std::string, std::string>::const_iterator found =
				this->entries.find(account);
			if(found == this->entries.end())
			{
				result.status = SecretStatus::Missing;
				return result;
			}
			result.status = SecretStatus::Ok;
			result.value = found->second;
			return result;
		}

		SecretResult write(Orkige::String const & account,
			Orkige::String const & secret) override
		{
			++this->writes;
			SecretResult result;
			if(this->refuse)
			{
				result.status = SecretStatus::Failed;
				result.message = "the store is locked";
				return result;
			}
			this->entries[account] = secret;
			result.status = SecretStatus::Ok;
			return result;
		}

		SecretResult erase(Orkige::String const & account) override
		{
			++this->erases;
			SecretResult result;
			if(this->refuse)
			{
				result.status = SecretStatus::Failed;
				result.message = "the store is locked";
				return result;
			}
			this->entries.erase(account);
			result.status = SecretStatus::Ok;
			return result;
		}
	};

	//! install a vault for one test and take it away afterwards, so no test
	//! can leave a store behind for the next one
	struct ScopedVault
	{
		explicit ScopedVault(SecretVault * vault) { setSecretVault(vault); }
		~ScopedVault() { setSecretVault(0); }
	};

	//! the environment as data
	SecretEnvironmentLookup lookupOf(
		std::map<std::string, std::string> const & values)
	{
		return [values](Orkige::String const & name)
		{
			const std::map<std::string, std::string>::const_iterator found =
				values.find(name);
			return found != values.end() ? found->second : std::string();
		};
	}

	//! the Android keystore password slot, straight out of the model
	BuildCredentialSlot keystorePasswordSlot()
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
		return BuildCredentialSlot();
	}
}

TEST_CASE("a password has a vault key and no file key", "[secretstore]")
{
	bool sawSecret = false;
	for(BuildTargetCell const & cell : buildTargetMatrix())
	{
		for(BuildCredentialSlot const & slot : cell.slots)
		{
			INFO(cell.platform << " " << slot.label);
			if(slot.storage == BuildCredentialStorage::Secret)
			{
				sawSecret = true;
				// unchanged and load-bearing: no settings-file key, ever
				CHECK(slot.key.empty());
				CHECK_FALSE(isMachineSettingKey(slot.vaultKey));
				if(cell.state == BuildCellState::Applied)
				{
					CHECK_FALSE(slot.vaultKey.empty());
					CHECK(isSecretVaultKey(slot.vaultKey));
				}
				else
				{
					// a cell that is not applied stores nothing, of either kind
					CHECK(slot.vaultKey.empty());
				}
			}
			else
			{
				// a name or a path is a machine setting; it never becomes a
				// vault entry, which would hide it from the file the person
				// can read
				CHECK(slot.vaultKey.empty());
			}
		}
	}
	CHECK(sawSecret);
}

TEST_CASE("the three vocabularies are disjoint", "[secretstore]")
{
	const std::vector<Orkige::String> vaultKeys = secretVaultKeys();
	REQUIRE(vaultKeys.size() == 2);	// the Android keystore + key passwords
	for(Orkige::String const & key : vaultKeys)
	{
		INFO(key);
		CHECK_FALSE(isMachineSettingKey(key));
		CHECK_FALSE(isProjectSettingKey(key));
		CHECK(key.rfind("export.", 0) != 0);
	}
	for(Orkige::String const & key : machineSettingKeys())
	{
		INFO(key);
		CHECK_FALSE(isSecretVaultKey(key));
	}
	// ...and an undeclared key opens nothing
	CHECK_FALSE(isSecretVaultKey("android.release.keystore"));
	CHECK_FALSE(isSecretVaultKey(""));
	CHECK_FALSE(isSecretVaultKey("anything.at.all"));
}

TEST_CASE("a vault account is per project and per slot", "[secretstore]")
{
	const Orkige::String projectA = "/Users/someone/games/game";
	const Orkige::String projectB = "/Users/someone/other/game";
	const Orkige::String key = "android.release.keystorePassword";
	const Orkige::String accountA = secretVaultAccount(projectA, key);
	const Orkige::String accountB = secretVaultAccount(projectB, key);
	REQUIRE_FALSE(accountA.empty());
	CHECK(accountA != accountB);				// two projects, two passwords
	CHECK(accountA == secretVaultAccount(projectA + "/", key));
	CHECK(accountA != secretVaultAccount(projectA,
		"android.release.keyPassword"));		// two slots, two passwords
	// readable in the platform's own credential UI, which is where a person
	// goes to revoke one
	CHECK(accountA.rfind("game-", 0) == 0);
	CHECK(accountA.find(key) != Orkige::String::npos);
	CHECK(secretVaultService() == "com.orkitec.orkige.signing");

	// the same gate the settings file has: an undeclared key gets no account
	CHECK(secretVaultAccount(projectA, "android.release.keystore").empty());
	CHECK(secretVaultAccount(projectA, "").empty());
	CHECK(secretVaultAccount("", key).empty());
}

TEST_CASE("the environment wins over the vault", "[secretstore]")
{
	// the rule CI depends on: a scripted build sets the variable and gets
	// exactly that, whatever a desktop keyring on the same machine holds
	FakeVault vault;
	ScopedVault installed(&vault);
	const BuildCredentialSlot slot = keystorePasswordSlot();
	const Orkige::String projectRoot = "/Users/someone/games/signed";
	REQUIRE_FALSE(slot.vaultKey.empty());
	Orkige::String error;
	REQUIRE(storeSecret(slot, projectRoot, "from-the-vault", &error));

	std::map<std::string, std::string> environment;
	environment[slot.environmentVariable] = "from-the-environment";
	const SecretState set = resolveSecret(slot, projectRoot,
		lookupOf(environment), &vault);
	CHECK(set.source == SecretSource::Environment);
	CHECK(set.sentence.find(slot.environmentVariable) !=
		Orkige::String::npos);

	// whitespace is not a password
	environment[slot.environmentVariable] = "   ";
	CHECK(resolveSecret(slot, projectRoot, lookupOf(environment), &vault)
		.source == SecretSource::Vault);

	// ...and with the variable gone the vault answers
	const SecretState stored = resolveSecret(slot, projectRoot,
		lookupOf(std::map<std::string, std::string>()), &vault);
	CHECK(stored.source == SecretSource::Vault);
	CHECK(stored.storable);
	CHECK(stored.vaultName == "Test Vault");
}

TEST_CASE("nothing set names both ways to set it", "[secretstore]")
{
	FakeVault vault;
	ScopedVault installed(&vault);
	const BuildCredentialSlot slot = keystorePasswordSlot();
	const Orkige::String projectRoot = "/Users/someone/games/blank";
	const SecretState missing = resolveSecret(slot, projectRoot,
		lookupOf(std::map<std::string, std::string>()), &vault);
	CHECK(missing.source == SecretSource::Missing);
	CHECK(missing.storable);
	CHECK(missing.sentence.find("Test Vault") != Orkige::String::npos);
	CHECK(missing.sentence.find(slot.environmentVariable) !=
		Orkige::String::npos);

	// with no vault the sentence still names the way that remains, and the
	// row is not editable (nothing could be kept)
	const SecretState nowhere = resolveSecret(slot, projectRoot,
		lookupOf(std::map<std::string, std::string>()), 0);
	CHECK(nowhere.source == SecretSource::Missing);
	CHECK_FALSE(nowhere.storable);
	CHECK(nowhere.sentence.find(slot.environmentVariable) !=
		Orkige::String::npos);
	CHECK(vault.reads == 1);	// the no-vault call asked nothing of anyone
}

TEST_CASE("a refusing vault is not an empty one", "[secretstore]")
{
	// a locked keyring must not read as "you never set a password" - that
	// sends a person hunting for something they did set
	FakeVault vault;
	ScopedVault installed(&vault);
	vault.refuse = true;
	const BuildCredentialSlot slot = keystorePasswordSlot();
	const SecretState state = resolveSecret(slot, "/Users/someone/games/game",
		lookupOf(std::map<std::string, std::string>()), &vault);
	CHECK(state.source == SecretSource::Unreadable);
	CHECK(state.sentence.find("locked") != Orkige::String::npos);
	// ...and it still names the way that works meanwhile
	CHECK(state.sentence.find(slot.environmentVariable) !=
		Orkige::String::npos);

	Orkige::String error;
	CHECK_FALSE(storeSecret(slot, "/Users/someone/games/game", "x", &error));
	CHECK(error == "the store is locked");
}

TEST_CASE("a password reaches the vault and nothing else", "[secretstore]")
{
	FakeVault vault;
	ScopedVault installed(&vault);
	const BuildCredentialSlot slot = keystorePasswordSlot();
	const Orkige::String projectRoot = "/Users/someone/games/signed";
	Orkige::String error;
	REQUIRE(storeSecret(slot, projectRoot, "SECRETPASSWORD", &error));
	CHECK(error.empty());
	CHECK(vault.writes == 1);
	REQUIRE(vault.entries.size() == 1);
	CHECK(vault.entries.begin()->first ==
		secretVaultAccount(projectRoot, slot.vaultKey));
	CHECK(vault.entries.begin()->second == "SECRETPASSWORD");

	// the value is not smuggled into the file store on the way past: the
	// settings serializer would have to be handed a key it does not have
	BuildSettingMap values;
	values[slot.vaultKey] = "SECRETPASSWORD";
	CHECK(sanitizeBuildSettings(values).empty());
	CHECK(serializeBuildSettings(values).find("SECRETPASSWORD") ==
		Orkige::String::npos);

	// forgetting is idempotent, and leaves nothing behind
	REQUIRE(forgetSecret(slot, projectRoot, &error));
	CHECK(vault.entries.empty());
	REQUIRE(forgetSecret(slot, projectRoot, &error));
	CHECK(error.empty());

	// an empty write is a removal, not an entry that reads as set and signs
	// nothing
	REQUIRE(storeSecret(slot, projectRoot, "real", &error));
	REQUIRE(storeSecret(slot, projectRoot, "   ", &error));
	CHECK(vault.entries.empty());
}

TEST_CASE("only a declared password can be stored at all", "[secretstore]")
{
	FakeVault vault;
	ScopedVault installed(&vault);
	Orkige::String error;

	// a Machine slot is a name or a path and belongs in the file
	BuildCredentialSlot machine;
	machine.key = "android.release.keystore";
	machine.storage = BuildCredentialStorage::Machine;
	CHECK_FALSE(storeSecret(machine, "/games/game", "x", &error));
	CHECK_FALSE(error.empty());

	// a Pending cell's password carries no vault key, so it stores nowhere
	BuildCredentialSlot pending;
	pending.storage = BuildCredentialStorage::Secret;
	CHECK_FALSE(storeSecret(pending, "/games/game", "x", &error));
	CHECK(resolveSecret(pending, "/games/game",
		lookupOf(std::map<std::string, std::string>()), &vault).storable ==
		false);
	CHECK(vault.writes == 0);
	CHECK(vault.entries.empty());
}

TEST_CASE("an automated run installs no credential store", "[secretstore]")
{
	// THE structural guarantee: a test run cannot prompt for keychain access
	// or read the person's real credentials, because it never reaches the
	// platform API - there is no vault installed to reach it through.
	FakeVault vault;
	ScopedVault installed(&vault);
	REQUIRE(secretVault() == &vault);

	CHECK(installPlatformSecretVault(true) == 0);
	CHECK(secretVault() == 0);

	// ...and every entry point degrades honestly rather than reaching around
	const BuildCredentialSlot slot = keystorePasswordSlot();
	const SecretState state = resolveSecret(slot, "/games/game");
	CHECK(state.source == SecretSource::Missing);
	CHECK_FALSE(state.storable);
	Orkige::String error;
	CHECK_FALSE(storeSecret(slot, "/games/game", "x", &error));
	CHECK_FALSE(error.empty());
	CHECK(forgetSecret(slot, "/games/game", &error));	// already gone
	CHECK(vault.reads == 0);
	CHECK(vault.writes == 0);
	CHECK(vault.erases == 0);
}

TEST_CASE("a scrubbed secret leaves no bytes behind", "[secretstore]")
{
	Orkige::String secret = "a-long-enough-password-to-have-a-heap-buffer";
	const char * const bytes = secret.data();
	const std::size_t length = secret.size();
	scrubSecret(secret);
	CHECK(secret.empty());
	bool anySet = false;
	for(std::size_t index = 0; index < length; ++index)
	{
		if(bytes[index] != '\0') { anySet = true; }
	}
	CHECK_FALSE(anySet);
}
