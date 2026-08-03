/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorBuildSettings.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorBuildSettings_h__3_8_2026__12_00_00__
#define __EditorBuildSettings_h__3_8_2026__12_00_00__

#include <core_util/String.h>

#include <map>
#include <vector>

//! @file EditorBuildSettings.h
//! @brief the two groups of build settings, and the line between them.
//!
//! Packaging a game for a phone or a store needs two very different kinds of
//! value, and mixing them is how signing material ends up in a repository:
//!
//! - **project settings** describe the APP - its bundle id, package name, team
//!   id, version, orientation, icon. They belong to the project, every person
//!   working on it needs the same ones, and they are committed. They live in
//!   the manifest as `export.*` Settings and nowhere else (@ref
//!   projectSettingRows).
//! - **machine settings** are the credentials that prove WHO is shipping it -
//!   a signing identity, a provisioning profile, a keystore. They belong to one
//!   developer's machine, differ per person, and must never be committed. They
//!   live in a per-project file under the editor's writable state directory
//!   (@ref buildSettingsPath), which is outside every project tree by
//!   construction.
//!
//! @par Passwords are not the third group - they live in the OS vault
//! A keystore password written into any file the editor owns would be a
//! plaintext secret sitting on disk for the lifetime of the project, protected
//! by nothing but file permissions. So a password slot (@ref
//! BuildCredentialStorage::Secret) carries no @ref BuildCredentialSlot::key at
//! all - it is structurally unstorable HERE - and names itself in the
//! platform's own credential store instead through @ref
//! BuildCredentialSlot::vaultKey (@see EditorSecretStore.h), keyed per project
//! and per slot. An explicit environment variable still WINS over the vault, so
//! CI, headless runs and scripted builds never depend on a desktop keyring.
//!
//! @par The split is enforced, not just documented
//! @ref machineSettingKeys and @ref projectSettingRows are disjoint by
//! assertion, @ref sanitizeBuildSettings drops any key that is not a machine
//! key before a write, and a Secret slot has an empty @ref
//! BuildCredentialSlot::key so there is no route for one to be persisted. The
//! vault vocabulary (@ref secretVaultKeys) is a THIRD disjoint set: a key that
//! opens a credential store is never a key a file may hold.
//!
//! Everything here is PURE except the four file functions at the bottom, which
//! is what lets the whole vocabulary - and the secret split itself - be
//! asserted headlessly on a machine that holds no credential of any kind.

namespace OrkigeEditor
{
	//--- the matrix ------------------------------------------

	//! what a build is FOR. The credentials differ per purpose, which is the
	//! whole reason this is a matrix and not a list: an iOS development
	//! identity does not sign an App Store upload, and a distribution identity
	//! installs on no development device.
	enum class BuildPurpose
	{
		Development,	//!< run it here: Play on a device, a local package
		Distribution	//!< hand it to other people: a store upload, a release
	};

	//! where a credential is allowed to live
	enum class BuildCredentialStorage
	{
		//! a name or a path: kept in this project's machine-settings file
		Machine,
		//! a password: stored NOWHERE by the editor, read from the environment
		//! by the tool that needs it (@see EditorBuildSettings.h)
		Secret
	};

	//! how far a cell's values travel today. Three honest answers beat one
	//! optimistic one: a person configuring a build should know whether they
	//! are setting something that acts, something that is merely kept, or
	//! something that is not wired at all yet.
	enum class BuildCellState
	{
		//! nothing to configure - @ref BuildTargetCell::note says what happens
		//! instead (a debug keystore the packaging script generates, an
		//! ad-hoc signature a local build gets)
		Automatic,
		//! the values are stored and handed to the export this editor runs
		Applied,
		//! the platform has no signing seam yet: the fields are shown so the
		//! shape is visible, disabled so nothing is promised, and stored never
		Pending
	};

	//! one credential a build purpose needs
	struct BuildCredentialSlot
	{
		//! the machine-settings key. EMPTY for a @ref
		//! BuildCredentialStorage::Secret - a password has no key BECAUSE it
		//! has no home here.
		Orkige::String			key;
		//! the credential-store key of a @ref BuildCredentialStorage::Secret:
		//! what the platform vault files this password under, per project
		//! (@see EditorSecretStore.h). EMPTY for a Machine slot, and empty for
		//! a Secret the editor does not keep at all - a cell that is not
		//! @ref BuildCellState::Applied stores nothing, of either kind.
		Orkige::String			vaultKey;
		Orkige::String			label;
		//! the environment variable the exporter falls back to (and, for a
		//! Secret, the ONLY place the value ever lives)
		Orkige::String			environmentVariable;
		BuildCredentialStorage	storage = BuildCredentialStorage::Machine;
		//! is the value a file path? (the editor then reports whether the file
		//! is actually there, rather than failing at export time)
		bool					isPath = false;
		//! the one line under the field: what it is, in the words the platform
		//! uses for it
		Orkige::String			hint;
	};

	//! one cell of the platform x purpose matrix
	struct BuildTargetCell
	{
		//! "ios" | "android" | "macos" | "windows"
		Orkige::String			platform;
		BuildPurpose			purpose = BuildPurpose::Development;
		//! what this cell produces, in a person's words ("Play on a device")
		Orkige::String			label;
		//! the doc STEM this cell is explained in, for @ref helpUrl - never a
		//! URL, so `doc_link_lint` can check it against the corpus on disk
		Orkige::String			helpPage;
		BuildCellState			state = BuildCellState::Applied;
		//! the honest sentence for an Automatic or Pending cell; empty for an
		//! Applied one, whose slots speak for themselves
		Orkige::String			note;
		std::vector<BuildCredentialSlot>	slots;
	};

	//! @brief the platforms, in the order the settings surface shows them
	std::vector<Orkige::String> buildPlatformOrder();

	//! @brief a platform's display name ("ios" -> "iOS")
	Orkige::String buildPlatformLabel(Orkige::String const & platform);

	//! @brief a purpose's display name
	Orkige::String buildPurposeLabel(BuildPurpose purpose);

	//! @brief the whole matrix: every platform x purpose cell, each carrying
	//! the credentials it needs and how far they travel. PURE - this table IS
	//! the model, and the settings surface renders exactly it.
	std::vector<BuildTargetCell> buildTargetMatrix();

	//--- the committed group ---------------------------------

	//! how a project setting is edited
	enum class ProjectSettingKind
	{
		Text,		//!< free text (an id, a version name, an asset path)
		Choice,		//!< one of @ref ProjectSettingRow::choices
		Integer		//!< a non-negative whole number
	};

	//! one manifest `export.*` Setting the settings surface edits
	struct ProjectSettingRow
	{
		Orkige::String		key;			//!< the manifest Setting key
		Orkige::String		label;
		//! "" for a setting every platform reads, else the platform it shapes
		Orkige::String		platform;
		ProjectSettingKind	kind = ProjectSettingKind::Text;
		//! what the exporter uses when the manifest says nothing
		Orkige::String		defaultValue;
		//! Choice only, in menu order
		std::vector<Orkige::String>	choices;
		Orkige::String		hint;
	};

	//! @brief the committed group: the manifest Settings that describe the app.
	//! PURE. Every key here is one the exporter already reads - this table
	//! gives them an editor, it does not invent a parallel vocabulary.
	std::vector<ProjectSettingRow> projectSettingRows();

	//--- the line between them -------------------------------

	//! @brief every key the machine-settings file may hold, and nothing else.
	//! Derived from @ref buildTargetMatrix, so a new credential cannot be
	//! storable without appearing in the model first.
	std::vector<Orkige::String> machineSettingKeys();

	//! @brief may @p key be written into the machine-settings file?
	bool isMachineSettingKey(Orkige::String const & key);

	//! @brief every key the PLATFORM VAULT may hold, and nothing else - the
	//! passwords of the Applied cells. Derived from @ref buildTargetMatrix the
	//! same way @ref machineSettingKeys is, and disjoint from it by assertion:
	//! a vault key never names a file entry and a file key never names a
	//! secret. @see EditorSecretStore.h
	std::vector<Orkige::String> secretVaultKeys();

	//! @brief may @p key be handed to the credential store?
	bool isSecretVaultKey(Orkige::String const & key);

	//! @brief is @p key one of the committed manifest Settings?
	bool isProjectSettingKey(Orkige::String const & key);

	//--- the machine-settings file ---------------------------

	typedef std::map<Orkige::String, Orkige::String> BuildSettingMap;

	//! @brief parse the `key = value` file (PURE). Blank lines and `#`
	//! comments are skipped; an unknown key is dropped rather than kept, so a
	//! hand-edited file cannot smuggle anything in either.
	BuildSettingMap parseBuildSettings(Orkige::String const & text);

	//! @brief drop everything that is not a machine key (PURE). The ONE gate
	//! every write goes through, so "a project setting cannot land in the
	//! credential file" and "a credential cannot land in the manifest" are the
	//! same rule read from two sides.
	BuildSettingMap sanitizeBuildSettings(BuildSettingMap const & values);

	//! @brief the file text for @p values (PURE, deterministic key order)
	Orkige::String serializeBuildSettings(BuildSettingMap const & values);

	//! @brief the name ONE project's machine-local credentials are filed under
	//! (PURE): the readable leaf name plus a short digest of the absolute
	//! root, so two projects called `game` on the same machine keep separate
	//! credentials and a moved project starts from none rather than silently
	//! inheriting. Carries no separator and no whitespace, because both the
	//! settings file name and the vault account are built from it.
	Orkige::String buildProjectScopeId(Orkige::String const & projectRoot);

	//! @brief the machine-settings file NAME for a project (PURE).
	//! @see buildProjectScopeId
	Orkige::String buildSettingsFileName(Orkige::String const & projectRoot);

	//! @brief the machine-settings directory: a subdirectory of the editor's
	//! writable state directory, created on demand. "" when this platform has
	//! no per-user application directory - the editor then stores no
	//! credentials at all rather than falling back to somewhere near the
	//! project, which is the failure this whole file exists to prevent.
	Orkige::String buildSettingsDirectory();

	//! @brief the machine-settings file for @p projectRoot, or "" when there
	//! is no writable state directory (@see buildSettingsDirectory).
	Orkige::String buildSettingsPath(Orkige::String const & projectRoot);

	//! @brief read the machine settings for @p projectRoot. An absent file is
	//! an empty map, not an error - nothing configured is the normal state.
	BuildSettingMap loadBuildSettings(Orkige::String const & projectRoot);

	//! @brief write the machine settings for @p projectRoot, OWNER-ONLY.
	//! @return false with an honest @p error when there is nowhere to write.
	//! @remarks the values are sanitized first, so a caller cannot persist a
	//! key the model does not declare - passwords included, which have no key.
	bool saveBuildSettings(Orkige::String const & projectRoot,
		BuildSettingMap const & values, Orkige::String * error);

	//--- what an export is handed ----------------------------

	//! @brief the credentials one export run is given, named exactly as the
	//! export request's own fields are, so the hand-over is six assignments
	//! with nothing to get wrong (@see tools/exporter/ExportRun.h).
	//! @remarks a value here OVERRIDES the matching environment variable: it
	//! was set deliberately in this editor for this project, whereas the
	//! environment is the machine-wide fallback. An empty one falls through.
	struct BuildCredentials
	{
		Orkige::String	iosIdentity;
		Orkige::String	iosProfile;
		Orkige::String	iosDistributionIdentity;
		Orkige::String	iosDistributionProfile;
		Orkige::String	androidKeystore;
		Orkige::String	androidKeyAlias;
		Orkige::String	bundletool;
	};

	//! @brief the credentials in @p values, whichever of them are set (PURE)
	BuildCredentials buildCredentialsFrom(BuildSettingMap const & values);
}

#endif //__EditorBuildSettings_h__3_8_2026__12_00_00__
