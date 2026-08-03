/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorSecretStore.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "EditorSecretStore.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		//! the one reverse-DNS name the whole set is filed under
		const char * const VAULT_SERVICE = "com.orkitec.orkige.signing";

		//! the vault this process uses. NOTHING is installed by default: a
		//! consumer that never installs one (every unit test, every automated
		//! editor run) reaches no platform API at all.
		SecretVault * gVault = 0;

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

		//! "$NAME" on POSIX shells, "%NAME%" on Windows ones - a person is
		//! going to paste this, so it should be paste-able where they are
		Orkige::String variableReference(Orkige::String const & name)
		{
#ifdef _WIN32
			return "%" + name + "%";
#else
			return "$" + name;
#endif
		}
	}
	//---------------------------------------------------------
	SecretVault::~SecretVault()
	{
	}
	//---------------------------------------------------------
	Orkige::String secretVaultService()
	{
		return VAULT_SERVICE;
	}
	//---------------------------------------------------------
	Orkige::String secretVaultAccount(Orkige::String const & projectRoot,
		Orkige::String const & vaultKey)
	{
		// the same gate the settings file has: a key the model does not
		// declare gets no account, so nothing undeclared can be filed
		if(projectRoot.empty() || !isSecretVaultKey(vaultKey))
		{
			return Orkige::String();
		}
		return buildProjectScopeId(projectRoot) + "/" + vaultKey;
	}
	//---------------------------------------------------------
	SecretVault * installPlatformSecretVault(bool automatedRun)
	{
		if(automatedRun)
		{
			// the structural half of "a test run never touches the vault":
			// nothing is installed, so every path below degrades to
			// Unavailable long before any platform call could happen
			gVault = 0;
			return 0;
		}
		gVault = platformSecretVault();
		return gVault;
	}
	//---------------------------------------------------------
	void setSecretVault(SecretVault * vault)
	{
		gVault = vault;
	}
	//---------------------------------------------------------
	SecretVault * secretVault()
	{
		return gVault;
	}
	//---------------------------------------------------------
	SecretEnvironmentLookup processEnvironmentLookup()
	{
		return [](Orkige::String const & name)
		{
			if(name.empty())
			{
				return Orkige::String();
			}
			const char * const value = std::getenv(name.c_str());
			return value != 0 ? Orkige::String(value) : Orkige::String();
		};
	}
	//---------------------------------------------------------
	SecretState resolveSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot,
		SecretEnvironmentLookup const & environment, SecretVault const * vault)
	{
		SecretState state;
		if(slot.storage != BuildCredentialStorage::Secret)
		{
			// only a password lives in a vault; a name or a path is a machine
			// setting and belongs in the file
			state.sentence = "not a password";
			return state;
		}
		const Orkige::String account =
			secretVaultAccount(projectRoot, slot.vaultKey);
		state.storable = vault != 0 && !account.empty();
		if(vault != 0)
		{
			state.vaultName = vault->name();
		}

		// 1. the environment wins - always, and whatever is stored
		Orkige::String fromEnvironment;
		if(environment && !slot.environmentVariable.empty())
		{
			fromEnvironment = trimmed(environment(slot.environmentVariable));
		}
		if(!fromEnvironment.empty())
		{
			state.source = SecretSource::Environment;
			state.sentence = "Set in " +
				variableReference(slot.environmentVariable) +
				" - the environment wins over anything stored here.";
			return state;
		}

		// 2. then the vault
		if(state.storable)
		{
			const SecretResult found = vault->read(account);
			if(found.ok() && !found.value.empty())
			{
				state.source = SecretSource::Vault;
				state.sentence = "Stored in your " + state.vaultName +
					" for this project.";
				return state;
			}
			if(found.status == SecretStatus::Failed)
			{
				// an unreadable vault is not an empty one, and pretending
				// otherwise would send a person hunting for a password they
				// did set
				state.source = SecretSource::Unreadable;
				state.sentence = state.vaultName + " could not be read - " +
					found.message + ". Set " +
					variableReference(slot.environmentVariable) +
					" in the shell that runs the build until it can.";
				return state;
			}
		}

		// 3. not set - said with BOTH ways to provide one
		state.source = SecretSource::Missing;
		if(state.storable)
		{
			state.sentence = "Not set. Type it here to keep it in your " +
				state.vaultName + ", or set " +
				variableReference(slot.environmentVariable) +
				" in the shell that runs the build.";
		}
		else if(!slot.environmentVariable.empty())
		{
			state.sentence = "Not set. Set " +
				variableReference(slot.environmentVariable) +
				" in the shell that runs the build - this machine has no "
				"credential store the editor can keep it in.";
		}
		else
		{
			state.sentence = "Not set. Nothing is kept for a signing step "
				"that does not run yet.";
		}
		return state;
	}
	//---------------------------------------------------------
	SecretState resolveSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot)
	{
		return resolveSecret(slot, projectRoot, processEnvironmentLookup(),
			secretVault());
	}
	//---------------------------------------------------------
	bool storeSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot, Orkige::String const & secret,
		Orkige::String * error)
	{
		auto refuse = [error](const char * message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		};
		if(slot.storage != BuildCredentialStorage::Secret)
		{
			return refuse("only a password is kept in the credential store");
		}
		const Orkige::String account =
			secretVaultAccount(projectRoot, slot.vaultKey);
		if(account.empty())
		{
			return refuse("this password is not one the editor keeps");
		}
		if(gVault == 0)
		{
			return refuse("this machine has no credential store the editor "
				"can use - set the environment variable instead");
		}
		if(trimmed(secret).empty())
		{
			// an empty write is a removal asked for the other way round;
			// storing "" would leave an entry that reads as set and signs
			// nothing
			return forgetSecret(slot, projectRoot, error);
		}
		const SecretResult result = gVault->write(account, secret);
		if(!result.ok())
		{
			if(error != 0)
			{
				*error = result.message.empty()
					? Orkige::String("the credential store refused the write")
					: result.message;
			}
			return false;
		}
		if(error != 0)
		{
			error->clear();
		}
		return true;
	}
	//---------------------------------------------------------
	bool forgetSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot, Orkige::String * error)
	{
		const Orkige::String account =
			secretVaultAccount(projectRoot, slot.vaultKey);
		if(account.empty() || gVault == 0)
		{
			// nowhere to remove it FROM is the same outcome as removed
			if(error != 0)
			{
				error->clear();
			}
			return true;
		}
		const SecretResult result = gVault->erase(account);
		if(!result.ok())
		{
			if(error != 0)
			{
				*error = result.message.empty()
					? Orkige::String("the credential store refused the removal")
					: result.message;
			}
			return false;
		}
		if(error != 0)
		{
			error->clear();
		}
		return true;
	}
	//---------------------------------------------------------
	void scrubSecret(Orkige::String & secret)
	{
		volatile char * bytes = const_cast<volatile char *>(secret.data());
		for(std::size_t index = 0; index < secret.size(); ++index)
		{
			bytes[index] = '\0';
		}
		secret.clear();
	}
}
