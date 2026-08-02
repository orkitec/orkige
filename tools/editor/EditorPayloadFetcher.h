/********************************************************************
	created:	Sunday 2026/08/02 at 14:00
	filename: 	EditorPayloadFetcher.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorPayloadFetcher_h__2_8_2026__14_00_00__
#define __EditorPayloadFetcher_h__2_8_2026__14_00_00__

//! @file EditorPayloadFetcher.h
//! @brief the impure half of the fetch: the one object that asks the release
//! service for this build's own dated release, downloads the archive an
//! enabled payload names, checks it against the `.sha256` published beside it,
//! unpacks it into the editor's writable state directory, and deletes whatever
//! it superseded. Every DECISION it makes comes from EditorPayloads.h.
//!
//! @par The same discipline as the updater next door
//! The requests go through the engine's async HTTP client, so nothing ever
//! touches the frame loop: submissions return immediately and completions
//! arrive from update(), at the frame boundary. Hashing the download and
//! running the platform's unpacker happen on ONE worker thread whose result
//! update() picks up. The subprocess seam is injected, so the whole sequence
//! drives headlessly against a loopback server and a sandbox directory.
//!
//! @par Never inside the application
//! An installed payload lands under the editor's writable state directory and
//! nowhere else. A distributed bundle is signed and read-only, and an
//! application that rewrites itself invalidates its own signature.
//!
//! @par No offline path
//! There is no way to sideload a payload and no cached fallback: a machine
//! that cannot reach the release service is told so in one line. A payload
//! half-assembled from somewhere else would package a game that does not
//! match the editor that packaged it.
//!
//! @par Nothing happens in an automated run
//! The `automatedRun` veto is absolute: no request is submitted and no
//! directory is created, so a scripted run is byte-identical to one on a
//! machine with no network.

#include "EditorPayloads.h"

#include <core_util/String.h>

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Orkige
{
	class HttpClient;
}

namespace OrkigeEditor
{
	//! @brief where a fetch is in its one sequence
	enum class PayloadFetchStage
	{
		Idle,		//!< nothing has been asked for
		Locating,	//!< asking the release service which asset to take
		Downloading,//!< fetching the archive
		Verifying,	//!< digest, then unpack
		Installed,	//!< it is on disk and complete
		Failed		//!< it did not work, and `message` says why
	};

	//! @brief what the UI shows: one stage, one honest line, and the progress
	//! of whatever is running
	struct PayloadFetchStatus
	{
		PayloadFetchStage	stage = PayloadFetchStage::Idle;
		Orkige::String		payloadId;	//!< what is being fetched ("" = none)
		Orkige::String		message;	//!< one line fit to put in front of a person
		unsigned long long	received = 0;
		unsigned long long	total = 0;

		//! is something running that deserves a progress bar?
		bool busy() const
		{
			return this->stage == PayloadFetchStage::Locating ||
				this->stage == PayloadFetchStage::Downloading ||
				this->stage == PayloadFetchStage::Verifying;
		}
		//! 0..1 while the total is known, -1 while it is not (an indeterminate
		//! bar is honest; a bar inching along a made-up total is not)
		float progress() const;
	};

	//! @brief the fetch. One instance, owned by the editor shell.
	class EditorPayloadFetcher
	{
		//--- Types -------------------------------------------
	public:
		//! @brief run @p argv, capture combined stdout+stderr and the exit
		//! code. Returns false only when the process could not be spawned.
		using ProcessRunner = std::function<bool(
			std::vector<Orkige::String> const & argv, Orkige::String & output,
			int & exitCode)>;

		//! @brief what this editor is and where its fetched pieces live
		struct Config
		{
			//! the releases collection ("" = the published default)
			Orkige::String	releasesUrl;
			//! this build's ordered identity - which names the dated release
			//! its payloads were published in. "" (an unstamped developer
			//! build) can pair with nothing and fetches nothing.
			Orkige::String	version;
			//! this editor's render flavor ("next"/"classic"): a payload is
			//! flavor-bound, like every build tree
			Orkige::String	flavor;
			//! the install root - `<writable state>/payloads`. Never inside
			//! the application bundle (@see the file comment).
			Orkige::String	rootDirectory;
			//! the absolute veto (@see the file comment)
			bool			automatedRun = false;
			//! allow a plain-http release service. The loopback test seam
			//! ONLY: a real run talks to https and refuses anything else.
			bool			allowInsecureHttp = false;
		};
		//--- Variables ---------------------------------------
	private:
		//! what the worker thread produces, read once by update()
		struct VerifyOutcome
		{
			bool			ok = false;
			Orkige::String	message;
		};

		Config					mConfig;
		PayloadFetchStatus		mStatus;
		ProcessRunner			mRunProcess;
		Orkige::HttpClient *	mHttp;			//!< not owned
		unsigned int			mReleaseRequest;//!< in-flight handle (0 = none)
		unsigned int			mChecksumRequest;
		unsigned int			mArchiveRequest;
		FetchablePayload		mPayload;		//!< what is being fetched
		Orkige::String			mAssetName;
		Orkige::String			mExpectedDigest;
		Orkige::String			mArchiveFile;
		Orkige::String			mInstallDirectory;
		std::unique_ptr<std::thread>	mWorker;
		std::mutex				mWorkerMutex;
		VerifyOutcome			mWorkerOutcome;
		bool					mWorkerRunning;
		bool					mWorkerFinished;
		//--- Methods -----------------------------------------
	public:
		explicit EditorPayloadFetcher(Config const & config);
		~EditorPayloadFetcher();

		//! @brief the async HTTP client to submit through (not owned; NULL =
		//! this build/host has none, and every fetch refuses honestly)
		void setHttpClient(Orkige::HttpClient * client);
		//! @brief the subprocess seam (the platform's unpacker)
		void setProcessRunner(ProcessRunner runner);

		Config const & config() const { return this->mConfig; }
		PayloadFetchStatus status() const;
		//! @brief can a fetch be started at all (a network client, an ordered
		//! version and a writable root)? The reason otherwise, in one line.
		bool canFetch(Orkige::String & reason) const;

		//! @brief the installed directory for @p id at THIS build's version
		//! and flavor, or "" when none is installed or the installed one is
		//! incomplete. The ONE question an export asks.
		Orkige::String installedPath(Orkige::String const & id) const;
		//! @brief is a complete payload installed for @p id?
		bool isInstalled(Orkige::String const & id) const
		{
			return !this->installedPath(id).empty();
		}

		//! @brief start fetching @p id. One at a time; a second call while one
		//! runs is ignored.
		void beginFetch(Orkige::String const & id);
		//! @brief the frame boundary: deliver HTTP completions and pick up the
		//! worker thread's result. Call once per frame.
		void update();

		//! @brief every payload directory found under the install root, in the
		//! shape @ref planPayloadPrune reads.
		std::vector<InstalledPayload> installedPayloads() const;
		//! @brief delete every installed copy that is not the current version
		//! of an enabled payload. @return how many directories were removed.
		int prune(std::vector<Orkige::String> const & enabledIds);
		//! @brief remove the installed payload for @p id (the user turned the
		//! platform off, or wants the download back)
		bool remove(Orkige::String const & id);
	private:
		void onReleaseResponse(Orkige::String const & body, bool ok,
			Orkige::String const & reason);
		void beginDownload(Orkige::String const & archiveUrl,
			Orkige::String const & checksumUrl, unsigned long long size);
		//! the worker body: digest, unpack, completeness (never touches mStatus)
		VerifyOutcome runVerify();
		void fail(Orkige::String const & message);
		Orkige::String workPath(char const * leaf) const;
		void removeTree(Orkige::String const & path);

		EditorPayloadFetcher(EditorPayloadFetcher const &) = delete;
		EditorPayloadFetcher & operator=(EditorPayloadFetcher const &) = delete;
	};

	//! the environment variable naming a directory of ALREADY-INSTALLED
	//! payloads, one subdirectory per id (`<dir>/player-ios-simulator/...`)
	extern char const * const PAYLOAD_DIRECTORY_ENV;

	//! @brief where the installed payload for @p id is, however this
	//! installation came to have one: the explicit @ref PAYLOAD_DIRECTORY_ENV
	//! override first, then the fetched install under the writable state
	//! directory. "" when neither answers with a COMPLETE payload.
	//! @remarks The override is how a payload that was NOT fetched is handed
	//! to an editor - a packaging test standing one up in a clean room, or a
	//! machine that built that platform's player from source and wants a
	//! downloaded editor to package with it. It is checked for completeness
	//! exactly like a fetched one, so it is a shortcut past the DOWNLOAD, never
	//! past the contract.
	//! @param fetcher may be NULL (an automated run creates none, and the
	//! override is then the only answer)
	Orkige::String resolveInstalledPayload(Orkige::String const & id,
		EditorPayloadFetcher const * fetcher);
}

#endif //__EditorPayloadFetcher_h__2_8_2026__14_00_00__
