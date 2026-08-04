/********************************************************************
	created:	Sunday 2026/08/02 at 14:00
	filename: 	EditorPayloads.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorPayloads_h__2_8_2026__14_00_00__
#define __EditorPayloads_h__2_8_2026__14_00_00__

#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file EditorPayloads.h
//! @brief every DECISION behind the pieces of an installation the editor
//! FETCHES rather than carries: which ones exist, what each is called where it
//! is published, where an installed one lives, whether one is complete, and
//! which of the installed copies are superseded.
//!
//! @par Why fetched
//! The browser player rides INSIDE a released editor because it is small and
//! every host can package a browser build. A device player is far larger, and
//! most people never build for the platform it serves - so it is fetched on
//! demand, once, into the editor's writable state directory, and never
//! embedded. A signed application bundle is read-only and must not modify
//! itself, which is why the writable state directory is the only place an
//! installed payload can live (the installed SDK pack next door lands there for
//! the same reason).
//!
//! @par Paired on the RELEASE TAG
//! A payload belongs to the build that fetches it: the two were produced by one
//! night's publish, from one commit. The pairing is therefore the dated release
//! TAG this editor's own ordered version names (@ref payloadReleaseTag), never
//! an ABI stamp - a payload records a stamp over its own surface while a
//! source-built editor's stamp also hashes every implementation file, so the
//! two numbers differ by construction and could never match. The dated tag is
//! also the only one that keeps working: the rolling `nightly` tag is replaced
//! every night, so an editor a day old would find its own assets gone.
//!
//! @par What a user opted into
//! Nothing is fetched for a platform nobody asked about. The editor's settings
//! carry the set of target platforms this user builds for (@ref
//! parseEnabledPayloads), defaulting to the host alone, and both the fetch and
//! the prune read that set.
//!
//! @par Pruned, not accumulated
//! The published archive keeps every release forever - that is a server's job.
//! A client keeps exactly what its own build needs: the current version of each
//! enabled payload, and nothing else (@ref planPayloadPrune). Those are two
//! different problems and the client only solves the small one.
//!
//! Nothing here reads a clock, opens a socket or touches a file: the impure
//! half (EditorPayloadFetcher.h) supplies the facts and carries the plan out,
//! which is what makes the whole table unit-testable headlessly.

namespace OrkigeEditor
{
	//! @brief what a fetchable payload IS - what the editor does with it once
	//! it is installed.
	enum class PayloadKind
	{
		//! a prebuilt player for a platform this host cannot build one for,
		//! plus the engine media that player renders through. A project export
		//! packages FROM it.
		Player,
		//! a relocatable engine SDK a project's compiled C++ game code is
		//! built against (Docs/sdk-pack.md). Declared here so the mechanism has
		//! its second consumer in view; nothing fetches one yet.
		Sdk
	};

	//! @brief one piece of an installation that is fetched rather than carried.
	struct FetchablePayload
	{
		//! the stable identity - it appears in the published asset name and in
		//! the install path, so it never changes once published
		Orkige::String	id;
		//! what a person reads in the settings row and in a refusal
		Orkige::String	label;
		//! the target platform's own name, for the sentence that talks about
		//! packaging rather than about the download ("iOS Simulator")
		Orkige::String	platformLabel;
		PayloadKind		kind = PayloadKind::Player;
		//! the export platform this payload unlocks ("" when it unlocks none -
		//! an SDK is not a packaging target)
		Orkige::String	exportPlatform;
		//! the path INSIDE a payload directory that marks it complete and
		//! usable - probed rather than trusted, so a half-unpacked copy is
		//! never handed to an export
		Orkige::String	marker;
		//! anything else the platform needs beside the player and the media.
		//! A `.app` is COPIED whole, so it needs nothing; an Android package
		//! is ASSEMBLED around the player, so the assembler and the Java it
		//! compiles travel with it - those are engine pieces, unlike the SDK's
		//! own programs, which stay the machine's.
		std::vector<Orkige::String>	extraPaths;
	};

	//! @brief every payload this editor knows how to fetch, in the order the
	//! settings UI lists them.
	std::vector<FetchablePayload> fetchablePayloads();

	//! @brief look one up by @p id; false when there is no such payload.
	bool findFetchablePayload(Orkige::String const & id,
		FetchablePayload & out);

	//! @brief the payload an export of @p platform needs, or "" when that
	//! platform packages out of the editor's own bundle (macOS, web) or is not
	//! a fetchable target at all.
	Orkige::String payloadIdForExportPlatform(Orkige::String const & platform);

	//! @brief the file name the published archive carries for @p id at
	//! @p flavor and @p version ("" when @p version is not an ordered
	//! identity).
	//! @remarks ONE composition, shared with the publishing side
	//! (Util/orkige_nightly_package.py writes exactly this name), so an asset
	//! that exists and an asset that is looked for cannot drift.
	Orkige::String payloadAssetName(Orkige::String const & id,
		Orkige::String const & flavor, Orkige::String const & version);

	//! @brief the DATED release tag @p version was published under
	//! ("2.0.0-nightly.20260802+abc" -> "nightly-20260802"), "" when the
	//! version carries no such day. @see the file comment on pairing.
	Orkige::String payloadReleaseTag(Orkige::String const & version);

	//! @brief the releases endpoint a fetch reads, for the tag @p version
	//! belongs to. @p base is the releases collection ("" = the published
	//! default); "" when @p version names no dated release.
	Orkige::String payloadReleaseUrl(Orkige::String const & base,
		Orkige::String const & version);

	//! @brief the default releases collection @ref payloadReleaseUrl builds on.
	char const * defaultPayloadReleasesUrl();

	//! @brief where an installed payload lives: `<root>/<id>/<flavor>/<token>`.
	//! The version is part of the PATH, so two versions can never share a
	//! directory and an interrupted install is visible rather than mistaken
	//! for a complete one.
	Orkige::String payloadInstallDirectory(Orkige::String const & root,
		Orkige::String const & id, Orkige::String const & flavor,
		Orkige::String const & version);

	//! @brief one payload directory found under the install root.
	struct InstalledPayload
	{
		Orkige::String	id;
		Orkige::String	flavor;
		Orkige::String	version;	//!< the filename token the directory is named
		Orkige::String	path;
	};

	//! @brief which installed copies to delete: everything that is not the
	//! CURRENT @p version of an ENABLED id at this editor's @p flavor.
	//! @remarks A payload for a platform the user turned off goes too - the
	//! set the settings carry is the whole answer to "what does this
	//! installation need".
	std::vector<Orkige::String> planPayloadPrune(
		std::vector<InstalledPayload> const & installed,
		std::vector<Orkige::String> const & enabledIds,
		Orkige::String const & flavor, Orkige::String const & version);

	//! @brief the payload-relative paths a complete payload carries. The ONE
	//! description of a usable payload: the fetch checks it after unpacking,
	//! the export plan checks it before packaging, and the publishing side
	//! composes exactly these.
	std::vector<Orkige::String> payloadRequiredPaths(
		FetchablePayload const & payload);

	//! @brief which of @ref payloadRequiredPaths are missing under
	//! @p directory (empty = complete).
	//! @param exists the ONE filesystem question, injected so the table is
	//! testable with no files on disk
	std::vector<Orkige::String> payloadProblems(
		FetchablePayload const & payload, Orkige::String const & directory,
		std::function<bool(Orkige::String const &)> const & exists);

	//! @brief the target platforms this user builds for, parsed from the
	//! persisted comma-separated list. Unknown ids are dropped rather than
	//! carried: a setting written by a newer build must not make an older one
	//! fetch something it cannot use.
	std::vector<Orkige::String> parseEnabledPayloads(
		Orkige::String const & text);

	//! @brief persist a set back (sorted into catalogue order, so the setting
	//! file is stable)
	Orkige::String formatEnabledPayloads(
		std::vector<Orkige::String> const & ids);

	//! @brief is @p id in @p enabled?
	bool isPayloadEnabled(std::vector<Orkige::String> const & enabled,
		Orkige::String const & id);

	//! @brief the argument vector that unpacks @p archivePath into
	//! @p destination with a tool the platform itself provides; empty where
	//! this platform unpacks none, which the caller reports rather than
	//! papering over.
	//! @remarks macOS uses the tool the archive was MADE with, because a
	//! bundle's executable bits have to survive the round trip - an unpacked
	//! player that cannot be executed is not a player.
	std::vector<Orkige::String> payloadExtractCommand(
		Orkige::String const & archivePath, Orkige::String const & destination);

	//! @brief the doc that explains payloads, as its FILE STEM - what
	//! @ref OrkigeEditor::helpUrl turns into a published link, and what
	//! `doc_link_lint` checks against `Docs/`.
	char const * payloadHelpPage();

	//! @brief the one sentence an export shows when the payload it needs is
	//! not installed - always naming BOTH the platform and the way to get it,
	//! because "cannot export" with no next step is not an answer.
	//! @param enabled has the user opted this platform in? A platform nobody
	//! asked for is a different sentence from one that was asked for and has
	//! not been fetched.
	//! @param hasNetworkClient does this build carry an HTTP transport at all?
	Orkige::String payloadMissingMessage(FetchablePayload const & payload,
		bool enabled, bool hasNetworkClient);

	//! @brief the same refusal for the HEADLESS door, where the sentence above
	//! would be unactionable: a build server has no settings window to switch a
	//! platform on in, and the subcommand that installs a payload is the whole
	//! answer there.
	//!
	//! @remarks There is no "switched on" half here. That setting decides what
	//! an interactive installation OFFERS to download and keeps pruned; a
	//! command line names the payload it wants, so an installed one is used and
	//! a missing one is fetched by id. The two sentences stay two rather than
	//! one with a branch, because they end on different instructions.
	//!
	//! @param hasNetworkClient does this build carry an HTTP transport at all?
	//! A build without one cannot fetch anything, and saying "run
	//! fetch-payload" to somebody it would also refuse is a dead end.
	Orkige::String payloadMissingCommandMessage(
		FetchablePayload const & payload, bool hasNetworkClient);
}

#endif //__EditorPayloads_h__2_8_2026__14_00_00__
