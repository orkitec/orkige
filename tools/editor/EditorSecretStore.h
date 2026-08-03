/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorSecretStore.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorSecretStore_h__3_8_2026__12_00_00__
#define __EditorSecretStore_h__3_8_2026__12_00_00__

#include "EditorBuildSettings.h"

#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file EditorSecretStore.h
//! @brief where a signing PASSWORD lives: the operating system's own
//! credential store, keyed per project and per slot.
//!
//! The rest of a build's credentials are names and paths, so they sit in a
//! per-project file (@see EditorBuildSettings.h). A password is different in
//! kind: a file holding one is a plaintext secret on disk for the lifetime of
//! the project, backed up with everything else and readable by anything running
//! as this user. So passwords never reach that file - they go to the vault the
//! platform already runs for exactly this, and the editor keeps only the key.
//!
//! @par The three answers, in order
//! 1. **the environment WINS.** `ORKIGE_ANDROID_KEYSTORE_PASS` in the shell
//!    beats anything stored here, always. CI, headless runs and scripted builds
//!    must never depend on a desktop keyring, and a person debugging a signing
//!    problem must be able to override what is stored without first emptying
//!    it.
//! 2. **then the vault**, under this project's own account name.
//! 3. **then "not set"**, said with BOTH ways to provide one - the field here,
//!    and the variable name - because a missing password is a build that will
//!    refuse, and the refusal should already be answered.
//!
//! @par What a vault does and does not protect
//! It removes three real exposures: a secret that can be committed, a secret
//! that rides into every backup as readable text, and a secret readable by
//! glancing at a file. It does NOT make a secret unreadable by a process the
//! person has already authorised - the same user, on the same machine, can ask
//! the vault for it, which is precisely how the editor reads it back. This is
//! the honest boundary, and the docs state it in the same words.
//!
//! @par No MCP verb reaches any of this
//! Deliberately, on the same reasoning that keeps the git tooling's mutations
//! out of the endpoint: a tool that reads or writes a signing password would
//! launder the machine-local rule the whole split exists to enforce. An agent
//! that needs a build signed asks the person to configure it here, or the
//! environment carries it.
//!
//! @par Automated runs never reach the vault
//! Structurally, not by convention: editor_core installs NO backend of its own,
//! @ref installPlatformSecretVault refuses when the `automatedRun` probe is
//! set, and every entry point below degrades to @ref SecretStatus::Unavailable
//! when no vault is installed. A test run therefore cannot prompt for keychain
//! access or read the person's real credentials, because it never calls the
//! platform API at all. The unit suite injects its own @ref SecretVault, which
//! is what makes the whole resolution order assertable headlessly.

namespace OrkigeEditor
{
	//! what a vault call did
	enum class SecretStatus
	{
		Ok,				//!< the value is there (read) / it landed (write)
		Missing,		//!< the vault has no entry under that account
		Unavailable,	//!< no vault on this machine, build, or run
		Failed			//!< the vault refused - @ref SecretResult::message says
	};

	//! the outcome of one vault call
	struct SecretResult
	{
		SecretStatus	status = SecretStatus::Unavailable;
		//! the secret, on a successful READ only - never logged, never
		//! serialized, and cleared by the caller as soon as it is used
		Orkige::String	value;
		//! the honest sentence for Unavailable/Failed ("" otherwise)
		Orkige::String	message;

		bool ok() const { return this->status == SecretStatus::Ok; }
	};

	//! @brief the platform credential store behind ONE seam.
	//! @remarks The seam exists for the same reason `EditorGit`'s runner and
	//! `EditorResourceLocator`'s existence probe do: the DECISIONS around it
	//! (which key, which source wins, what to say when there is nothing) are
	//! the part that can be wrong, and they are only testable if the
	//! irreversible platform call is injectable.
	class SecretVault
	{
		//--- Methods -----------------------------------------
	public:
		virtual ~SecretVault();

		//! @brief what to call this store in a sentence a person reads
		//! ("Keychain", "Credential Manager")
		virtual Orkige::String name() const = 0;

		//! @brief read the secret filed under @p account
		virtual SecretResult read(Orkige::String const & account) const = 0;

		//! @brief file @p secret under @p account, replacing any entry there
		virtual SecretResult write(Orkige::String const & account,
			Orkige::String const & secret) = 0;

		//! @brief remove @p account. Removing what is not there is @ref
		//! SecretStatus::Ok - the caller asked for it to be gone.
		virtual SecretResult erase(Orkige::String const & account) = 0;
	};

	//--- the keying ------------------------------------------

	//! @brief the service every Orkige credential is filed under. One
	//! reverse-DNS name, so a person can find (and revoke) the whole set in
	//! their platform's own credential UI.
	Orkige::String secretVaultService();

	//! @brief the account ONE secret is filed under (PURE): this project's
	//! scope id and the slot's vault key, so two projects on one machine never
	//! share a password and a moved project starts from none.
	//! @return "" when @p vaultKey is not one the model declares (@see
	//! isSecretVaultKey) - an undeclared key gets no account, which is the same
	//! gate the settings file has against an undeclared setting.
	Orkige::String secretVaultAccount(Orkige::String const & projectRoot,
		Orkige::String const & vaultKey);

	//--- the installed vault ---------------------------------

	//! @brief this build's platform vault, or 0 where there is none.
	//! @remarks Defined once per platform (Keychain / Credential Manager /
	//! none). Constructing it touches nothing - only a read or a write reaches
	//! the operating system - so asking for it is always safe.
	SecretVault * platformSecretVault();

	//! @brief install the vault the editor uses for the rest of the process.
	//! @param automatedRun the editor's own probe: true INSTALLS NOTHING, so a
	//! scripted run cannot reach the person's credentials however deep it goes.
	//! @return the installed vault, or 0 (automated run, or no vault here).
	SecretVault * installPlatformSecretVault(bool automatedRun);

	//! @brief install @p vault directly - the unit suite's fake. 0 uninstalls.
	void setSecretVault(SecretVault * vault);

	//! @brief the installed vault, or 0 when nothing is installed
	SecretVault * secretVault();

	//--- resolution ------------------------------------------

	//! where a secret is coming from right now
	enum class SecretSource
	{
		Environment,	//!< the environment holds it, and it wins
		Vault,			//!< the credential store holds it
		//! the store is there but would not answer (locked, denied). NOT the
		//! same as Missing: telling someone they never set a password they did
		//! set sends them off to look for the wrong problem.
		Unreadable,
		Missing			//!< neither - the build will refuse until one does
	};

	//! @brief the environment as a lookup, injected so the order below is
	//! testable without touching the real one ("" = the variable is absent)
	typedef std::function<Orkige::String(Orkige::String const &)>
		SecretEnvironmentLookup;

	//! @brief the process environment (@see std::getenv)
	SecretEnvironmentLookup processEnvironmentLookup();

	//! what one password slot's state is, and the one line to say about it
	struct SecretState
	{
		SecretSource	source = SecretSource::Missing;
		//! could this editor keep this password at all? (false for a slot the
		//! model does not store, and on a machine with no vault installed)
		bool			storable = false;
		//! the store's name for a sentence ("" when nothing is installed)
		Orkige::String	vaultName;
		//! what the row says: where the value comes from, or - when there is
		//! none - BOTH ways to provide one
		Orkige::String	sentence;
	};

	//! @brief resolve one password slot: environment, then vault, then not set
	//! (@see EditorSecretStore.h). Reads nothing when @p vault is 0.
	SecretState resolveSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot,
		SecretEnvironmentLookup const & environment, SecretVault const * vault);

	//! @brief @ref resolveSecret against the process environment and the
	//! installed vault - what the settings surface calls
	SecretState resolveSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot);

	//! @brief keep @p secret for @p slot in the installed vault.
	//! @return false with an honest @p error: no vault, a slot that stores
	//! nothing, an empty value (which is a @ref forgetSecret, not a write), or
	//! the platform's own refusal verbatim.
	bool storeSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot, Orkige::String const & secret,
		Orkige::String * error);

	//! @brief remove @p slot's secret from the installed vault. Removing what
	//! is not there succeeds.
	bool forgetSecret(BuildCredentialSlot const & slot,
		Orkige::String const & projectRoot, Orkige::String * error);

	//! @brief overwrite @p secret's bytes in place before it goes out of
	//! scope. Not a security boundary (a copy may already have been made by an
	//! allocator or a UI buffer) - it just keeps a plain password from sitting
	//! in freed editor memory for the rest of the session.
	void scrubSecret(Orkige::String & secret);
}

#endif //__EditorSecretStore_h__3_8_2026__12_00_00__
