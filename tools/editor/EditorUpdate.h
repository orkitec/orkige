/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	EditorUpdate.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorUpdate_h__31_7_2026__09_00_00__
#define __EditorUpdate_h__31_7_2026__09_00_00__

//! @file EditorUpdate.h
//! @brief every DECISION the editor's updater makes, as pure functions over
//! plain data: whether to check at all, whether what was published is newer,
//! which asset belongs to this platform, whether the digest matches, whether
//! this install location may be replaced, and exactly which moves the swap
//! consists of - including how to undo a swap that fails halfway.
//!
//! Nothing here reads a clock, opens a socket, touches a file or spawns a
//! process. The impure half (EditorUpdater.h) supplies the facts and carries
//! out the plan, which is what makes the whole decision table unit-testable
//! headlessly, on any platform, with no network and no installed app.
//!
//! @par What it reads
//! One request against the rolling release tag returns a JSON body carrying
//! the release notes, from which two things are taken: the ordered build
//! identity out of a machine-readable marker, and the PORTABLE archive for
//! this platform out of the asset list (`Docs/nightly-builds.md`). The
//! installable shapes - a disk image, an installer - are never candidates:
//! they are how a PERSON installs, not how a running program replaces
//! itself.
//!
//! @par The swap
//! A running application cannot overwrite itself, so the update is applied by
//! a helper that outlives it: it waits for the editor's process to end, moves
//! the installed copy ASIDE, moves the verified new one in, and puts the old
//! one back if the second move fails. The script is composed here
//! (composeUpdateHelperScript) so its whole shape - the wait, the two moves,
//! the rollback, the relaunch - is asserted by unit tests rather than trusted.

#include <string>
#include <vector>

namespace OrkigeEditor
{
	//! @brief the three states of the update setting (View Settings, persisted
	//! with the rest of the editor's settings).
	enum class UpdatePolicy
	{
		Off,		//!< never check on its own; the menu item still works
		Notify,		//!< check, and say when something newer exists (the default)
		Download	//!< check, and fetch it in the background when it does
	};

	//! @brief the stable token an @p policy persists as ("off"/"notify"/"download")
	char const* updatePolicyName(UpdatePolicy policy);
	//! @brief the label the settings UI shows for @p policy
	char const* updatePolicyLabel(UpdatePolicy policy);
	//! @brief read a persisted token back; @p fallback for anything else
	UpdatePolicy parseUpdatePolicy(std::string const& text,
		UpdatePolicy fallback);

	//! @brief how long the editor waits between checks of its own accord. A
	//! published build changes at most once a day, so a shorter interval
	//! cannot find anything a longer one misses.
	long long updateCheckIntervalSeconds();

	//! @brief why a check is or is not being run right now
	enum class UpdateCheckDecision
	{
		Run,			//!< go ahead
		PolicyOff,		//!< the setting is Off and nobody asked
		Automated,		//!< an automated run: never the network, never user state
		TooSoon,		//!< checked less than the interval ago
		NoOrderedVersion	//!< this build has no ordered identity to compare
	};

	//! @brief the facts decideUpdateCheck answers over
	struct UpdateCheckContext
	{
		UpdatePolicy	policy = UpdatePolicy::Notify;
		//! an automated run (the `automatedRun` probe): an absolute veto - a
		//! scripted run must never reach the network or touch user state
		bool			automatedRun = false;
		//! the user asked for it (the Check for Updates… item). An explicit
		//! click is consent, so it overrides both Off and the interval.
		bool			manual = false;
		//! does this binary carry an ordered version (@see editorBuildVersion)?
		//! An unstamped developer build cannot be compared with anything and
		//! is told so rather than offered an update it cannot justify.
		bool			hasOrderedVersion = true;
		//! when the last check ran, in seconds since the epoch (0 = never)
		long long		lastCheckEpochSeconds = 0;
		//! now, in seconds since the epoch
		long long		nowEpochSeconds = 0;
	};

	//! @brief the cadence gate: once per launch, and at most once a day
	UpdateCheckDecision decideUpdateCheck(UpdateCheckContext const& context);
	//! @brief the one line a refused decision reports ("" for Run)
	char const* updateCheckDecisionReason(UpdateCheckDecision decision);

	//! @brief one downloadable file named by a release
	struct UpdateAsset
	{
		std::string			name;
		std::string			url;
		unsigned long long	size = 0;

		bool empty() const { return this->name.empty() || this->url.empty(); }
	};

	//! @brief a release as an updater reads it
	struct UpdateRelease
	{
		//! did the body carry the version marker? A release without one is
		//! not something this client understands, and "nothing to do" is the
		//! honest answer to that.
		bool						valid = false;
		std::string					version;	//!< the ordered identity
		//! the "Changes since …" section of the notes ("" when absent)
		std::string					changelog;
		std::vector<UpdateAsset>	assets;
		//! why !valid, in one line fit to show a person
		std::string					problem;
	};

	//! @brief parse the release JSON (`body` + `assets[]`) into the two things
	//! a client needs. Never throws; malformed input yields !valid with a
	//! reason rather than a guess.
	UpdateRelease parseUpdateRelease(std::string const& json);

	//! @brief the desktop platforms a published build exists for
	enum class UpdatePlatform
	{
		MacOS,
		Linux,
		Windows
	};

	//! @brief the platform this binary was compiled for
	UpdatePlatform hostUpdatePlatform();

	//! @brief the archive an updater takes on @p platform, and its digest
	//! sidecar
	struct UpdateAssetChoice
	{
		bool		found = false;
		UpdateAsset	archive;
		UpdateAsset	checksum;
		std::string	problem;	//!< why !found, in one line
	};

	//! @brief the PORTABLE archive for @p platform at @p version, plus the
	//! `.sha256` sidecar beside it.
	//! @remarks Deliberately an exact-name match against the one name the
	//! publishing convention produces, so the installable shapes (a disk
	//! image, an installer) can never be selected by accident - they are
	//! install shapes, not update payloads.
	UpdateAssetChoice selectUpdateAssets(std::vector<UpdateAsset> const& assets,
		UpdatePlatform platform, std::string const& version);

	//! @brief the name the portable archive carries on @p platform at
	//! @p version ("" when @p version is not an ordered identity)
	std::string updateArchiveName(UpdatePlatform platform,
		std::string const& version);

	//! @brief how a published version relates to the running one
	enum class UpdateVerdict
	{
		Offer,			//!< strictly newer - the only case an update happens
		UpToDate,		//!< the same version (a rebuild of one day's tree)
		Older,			//!< a DOWNGRADE - refused, never offered nor installed
		Incomparable	//!< one side is not an ordered version
	};

	//! @brief compare @p published with @p current through the ONE ordering
	//! (@see core_util/VersionOrder.h). Only Offer ever leads anywhere.
	UpdateVerdict judgeUpdate(std::string const& published,
		std::string const& current);
	//! @brief the one line each verdict reports
	char const* updateVerdictReason(UpdateVerdict verdict);

	//! @brief the digest a `.sha256` sidecar names for @p fileName, lower-cased
	//! ("" when it names none).
	//! @remarks Reads the format the publishing side writes - the one
	//! `sha256sum -c` takes, `<64 hex><spaces><name>` - and also accepts a
	//! bare digest on its own line, which is the other shape such a sidecar
	//! is commonly written in. A sidecar naming a DIFFERENT file yields ""
	//! rather than its digest: matching bytes against a digest issued for
	//! something else is not a check.
	std::string parseChecksumSidecar(std::string const& text,
		std::string const& fileName);

	//! @brief whether the place this editor is installed may be replaced
	enum class InstallLocationVerdict
	{
		Updatable,		//!< ours, present, and its container is writable
		Missing,		//!< the installed path is not there at all
		ReadOnly,		//!< the container cannot be written (a shared install)
		Translocated,	//!< running from a randomised read-only quarantine path
		BuildTree		//!< a developer build tree - never rearranged by us
	};

	//! @brief the facts judgeInstallLocation answers over. The impure side
	//! gathers them; every one is a plain observation, never a policy.
	struct InstallLocationFacts
	{
		//! what would be replaced (the app bundle on macOS, the install
		//! directory elsewhere)
		std::string	installPath;
		//! the directory holding it - where the swap's two moves happen
		std::string	containerPath;
		bool		installExists = false;
		bool		containerWritable = false;
		//! the path is a randomised read-only copy the system made
		bool		translocated = false;
		//! a build tree marker (a CMake cache) sits at or above the install
		bool		insideBuildTree = false;
	};

	//! @brief may this install be replaced in place? @see InstallLocationVerdict
	InstallLocationVerdict judgeInstallLocation(
		InstallLocationFacts const& facts);
	//! @brief the sentence a refused location tells the user, which always
	//! ends in "download it yourself" rather than in a rearrangement we would
	//! have to guess at
	char const* installLocationReason(InstallLocationVerdict verdict);

	//! @brief does @p path sit in the randomised read-only location the
	//! system gives a downloaded app that was launched out of the folder it
	//! was unpacked into? A pure string probe over the one path shape that
	//! means it.
	bool isTranslocatedPath(std::string const& path);

	//! @brief the two moves an update consists of, and the one that undoes the
	//! first. Both endpoints are SIBLINGS inside one directory, so each move
	//! is a rename within a single filesystem rather than a copy that could
	//! run out of space halfway.
	struct UpdateSwapPlan
	{
		bool		valid = false;
		std::string	installedPath;	//!< moved aside first
		std::string	stagedPath;		//!< moved in second
		std::string	backupPath;		//!< where the old one waits until it works
		std::string	problem;		//!< why !valid
	};

	//! @brief compose the swap. @p backupSuffix distinguishes one attempt's
	//! backup from another's (the version being installed is what the caller
	//! passes). Refuses anything it cannot describe as two safe renames.
	UpdateSwapPlan planUpdateSwap(std::string const& installedPath,
		std::string const& stagedPath, std::string const& backupSuffix);

	//! @brief quote @p text for a POSIX shell (single quotes, with the one
	//! escape a single-quoted string needs)
	std::string shellQuotePosix(std::string const& text);
	//! @brief quote @p text for a Windows command script; returns "" for a
	//! path carrying a character that cannot be quoted safely there, which
	//! the caller must treat as a refusal rather than a best effort
	std::string shellQuoteWindows(std::string const& text);

	//! @brief everything the out-of-process swap helper is told
	struct UpdateHelperSpec
	{
		UpdateSwapPlan	plan;
		//! the process the helper waits for before touching anything - the
		//! editor's own, so the swap can never race a running instance
		long long		pid = 0;
		//! what to launch once the swap succeeded ("" = do not relaunch,
		//! which is the install-at-quit case)
		std::string		relaunchPath;
		UpdatePlatform	platform = UpdatePlatform::MacOS;
		//! how long to wait for the editor to exit before giving up, in
		//! seconds - a helper that waits forever is a stuck process nobody
		//! sees
		int				waitTimeoutSeconds = 120;
	};

	//! @brief the helper's script, verbatim. "" when the spec cannot be
	//! expressed safely (an invalid plan, an unquotable path).
	//! @remarks The helper does the LEAST it can: it verifies nothing and
	//! decides nothing. The digest was checked, the signature was checked and
	//! the location was judged before this text was ever written - all this
	//! does is wait, move, move, and undo the first move if the second fails.
	std::string composeUpdateHelperScript(UpdateHelperSpec const& spec);

	//! @brief the file name the helper script is written under on @p platform
	char const* updateHelperFileName(UpdatePlatform platform);

	//! @brief the argument vector that runs the composed helper detached
	std::vector<std::string> updateHelperCommand(UpdatePlatform platform,
		std::string const& scriptPath);

	//! @brief the argument vector that unpacks @p archivePath into
	//! @p destination using a tool the platform itself provides; empty when
	//! the archive's shape is not one this platform unpacks.
	//! @remarks macOS unpacks with the same tool the archive was made with,
	//! because a bundle's symlinks and executable bits have to survive.
	std::vector<std::string> updateExtractCommand(UpdatePlatform platform,
		std::string const& archivePath, std::string const& destination);

	//! @brief the argument vector that asks the platform whether a staged
	//! payload carries a valid signature; empty on a platform where builds
	//! carry none, which the caller reports rather than papering over.
	std::vector<std::string> updateSignatureVerifyCommand(
		UpdatePlatform platform, std::string const& payloadPath);
	//! @brief the second, stricter question macOS answers: would the system
	//! itself let this run? Empty elsewhere.
	std::vector<std::string> updateSignatureAssessCommand(
		UpdatePlatform platform, std::string const& payloadPath);

	//! @brief the name of the thing inside an unpacked archive that IS the
	//! editor on @p platform - what the swap moves into place
	char const* updatePayloadName(UpdatePlatform platform);

	//! @brief where the updater is in its one sequence
	enum class UpdateStage
	{
		Idle,			//!< nothing has been asked yet
		Checking,		//!< the one request is in flight
		UpToDate,		//!< checked, nothing newer
		Available,		//!< something newer exists (Notify, or Download starting)
		Downloading,	//!< fetching the archive
		Verifying,		//!< digest, unpack, signature
		Ready,			//!< verified and staged; it installs on restart
		Failed			//!< it did not work, and `message` says why
	};

	//! @brief what the UI shows: one stage, one honest line, and the progress
	//! of whatever is running
	struct UpdateStatus
	{
		UpdateStage			stage = UpdateStage::Idle;
		//! the version being offered/downloaded ("" when there is none)
		std::string			version;
		//! one line fit to put in front of a person
		std::string			message;
		//! the changelog section the offered release carries ("" when none)
		std::string			changelog;
		unsigned long long	received = 0;
		unsigned long long	total = 0;

		//! is something running that deserves the footer's progress bar?
		bool busy() const
		{
			return this->stage == UpdateStage::Checking ||
				this->stage == UpdateStage::Downloading ||
				this->stage == UpdateStage::Verifying;
		}
		//! 0..1 while the total is known, -1 while it is not (an indeterminate
		//! bar is honest; a bar inching along a made-up total is not)
		float progress() const
		{
			if (this->total == 0)
			{
				return -1.0f;
			}
			const double fraction = static_cast<double>(this->received) /
				static_cast<double>(this->total);
			return static_cast<float>(fraction < 0.0 ? 0.0
				: (fraction > 1.0 ? 1.0 : fraction));
		}
	};

	//! @brief the short label the footer shows for @p stage
	char const* updateStageLabel(UpdateStage stage);
}

#endif //__EditorUpdate_h__31_7_2026__09_00_00__
