/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	EditorUpdater.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorUpdater_h__31_7_2026__09_00_00__
#define __EditorUpdater_h__31_7_2026__09_00_00__

//! @file EditorUpdater.h
//! @brief the impure half of the editor's updater: the one object that asks
//! the network, writes the download, drives the platform's own unpacking and
//! signature tools, and hands the finished swap to a helper that outlives
//! this process. Every DECISION it makes comes from EditorUpdate.h.
//!
//! @par Nothing blocks a frame
//! The check and the download go through the engine's async HTTP client, so
//! neither ever touches the frame loop: submissions return immediately and
//! completions arrive from update(), at the frame boundary, exactly like
//! every other consumer. The part that cannot be async - hashing a hundred
//! megabytes, running the unpacker, asking the system about a signature -
//! runs on ONE worker thread whose result update() picks up. The frame loop
//! reads a small status struct and nothing else; no per-frame disk read
//! exists anywhere in here.
//!
//! @par Nothing happens mid-session
//! A verified download is STAGED, never applied. The swap runs from a helper
//! process after this one has exited - on request ("Restart now") or at the
//! next clean quit. A running application cannot replace itself, and a
//! half-replaced one is worse than one that was never updated.
//!
//! @par Nothing happens in an automated run
//! The `automatedRun` probe is an absolute veto: no request is submitted, no
//! stamp is written, no staging directory is created. A scripted run must be
//! byte-identical to one on a machine with no network.
//!
//! @par Subprocesses are injected
//! Like the git seam next door, the unpacker/signature calls and the helper
//! launch arrive as callables, so the whole sequence is drivable headlessly
//! against a loopback server and a sandbox directory.

#include "EditorUpdate.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Orkige
{
	class HttpClient;
}

namespace OrkigeEditor
{
	//! @brief the whole updater. One instance, owned by the editor shell.
	class EditorUpdater
	{
		//--- Types -------------------------------------------
	public:
		//! @brief run @p argv, capture combined stdout+stderr and the exit
		//! code. Returns false only when the process could not be spawned.
		using ProcessRunner = std::function<bool(
			std::vector<std::string> const& argv, std::string& output,
			int& exitCode)>;
		//! @brief launch @p argv and DO NOT wait for it - the helper has to
		//! outlive this process. Returns false when it could not be spawned.
		using DetachedSpawn = std::function<bool(
			std::vector<std::string> const& argv)>;
		//! @brief now, in seconds since the epoch
		using Clock = std::function<long long()>;

		//! @brief what this editor is and where it lives
		struct Config
		{
			//! the release endpoint polled ("" = the published default)
			std::string		feedUrl;
			//! this build's ordered identity ("" = an unstamped build, which
			//! is never offered anything)
			std::string		currentVersion;
			//! what would be replaced: the app bundle on macOS, the install
			//! directory elsewhere ("" = unknown, so nothing is installed)
			std::string		installPath;
			//! a writable directory for the stamp file, the download and the
			//! unpacked staging (the editor's own application-support dir)
			std::string		workDirectory;
			//! what "Restart now" launches once the swap succeeded
			std::string		relaunchPath;
			//! this process, for the helper to wait on
			long long		pid = 0;
			UpdatePlatform	platform = hostUpdatePlatform();
			//! the absolute veto (@see the file comment)
			bool			automatedRun = false;
			//! this editor resolved its resources from a developer TREE
			//! rather than from the app it was copied as - which is the
			//! exact answer to "was this built here", and a build tree is
			//! never rearranged by an updater. The shell reads it off the
			//! ONE resource locator; a CMake cache found at or just above
			//! the install is the cheap second opinion.
			bool			builtFromTree = false;
			//! allow a plain-http feed. The loopback test seam ONLY: a real
			//! run talks to an https endpoint and refuses anything else.
			bool			allowInsecureHttp = false;
			//! treat the install location as updatable without probing it.
			//! The sandbox test seam - a scratch directory is not a real
			//! install, and the location verdict is unit-tested separately.
			bool			assumeUpdatableLocation = false;
		};
		//--- Variables ---------------------------------------
	private:
		//! what the worker thread produces, read once by update()
		struct VerifyOutcome
		{
			bool		ok = false;
			std::string	payloadPath;	//!< the verified staged copy
			std::string	message;		//!< the honest line either way
			bool		signatureChecked = false;
		};

		Config					mConfig;
		UpdatePolicy			mPolicy;
		UpdateStatus			mStatus;
		ProcessRunner			mRunProcess;
		DetachedSpawn			mSpawnDetached;
		Clock					mClock;
		Orkige::HttpClient*		mHttp;			//!< not owned
		unsigned int			mFeedRequest;	//!< in-flight handle (0 = none)
		unsigned int			mChecksumRequest;
		unsigned int			mArchiveRequest;
		long long				mLastCheckEpoch;
		bool					mCheckedThisLaunch;
		std::string				mExpectedDigest;//!< from the sidecar
		std::string				mArchiveFile;	//!< where the download landed
		std::string				mArchiveName;	//!< the asset's own name
		std::string				mPendingVersion;//!< the staged version
		std::string				mPendingPayload;//!< the staged payload path
		//! set once a helper was spawned, so a second quit cannot spawn a
		//! second one against a payload the first already moved
		bool					mHelperSpawned;
		//! did the last check come from the menu? (only an asked-for check
		//! gets an "you are on the latest version" popup)
		bool					mLastCheckManual;
		//! is there an answer the UI has not put in front of the user yet?
		bool					mUnseenResult;
		std::unique_ptr<std::thread>	mWorker;
		std::mutex				mWorkerMutex;
		VerifyOutcome			mWorkerOutcome;
		bool					mWorkerRunning;
		bool					mWorkerFinished;
		//--- Methods -----------------------------------------
	public:
		//! @brief the default release endpoint an unset feedUrl resolves to
		static char const* defaultFeedUrl();

		explicit EditorUpdater(Config const& config);
		~EditorUpdater();

		//! @brief the async HTTP client to submit through (not owned; NULL =
		//! this build/host has none, and every check refuses honestly)
		void setHttpClient(Orkige::HttpClient* client);
		//! @brief the subprocess seam (unpacking, signature verification)
		void setProcessRunner(ProcessRunner runner);
		//! @brief the detached-launch seam (the swap helper)
		void setDetachedSpawn(DetachedSpawn spawn);
		//! @brief the wall clock (injected so the cadence gate is testable)
		void setClock(Clock clock);

		UpdatePolicy policy() const { return this->mPolicy; }
		//! @brief the setting changed (persisted by the caller with the rest
		//! of the editor's settings)
		void setPolicy(UpdatePolicy policy);

		UpdateStatus status() const;
		Config const& config() const { return this->mConfig; }

		//! @brief read the persisted stamp (last check, and a payload a
		//! previous session verified but never installed). Call once at boot;
		//! a no-op in an automated run.
		void loadState();

		//! @brief the once-per-launch check, gated by the setting, the
		//! automated-run veto and the 24 hour interval. Cheap to call every
		//! frame: it does its work once and then does nothing.
		void tickAutomaticCheck();
		//! @brief the user asked (the Check for Updates… item): overrides
		//! both the Off setting and the interval, never the automated veto.
		void requestManualCheck();
		//! @brief did the last check come from the menu? (the UI only shows
		//! "you are on the latest version" for a check somebody asked for)
		bool lastCheckWasManual() const { return this->mLastCheckManual; }
		//! @brief the user saw the "up to date" or "ready" answer
		void acknowledge();
		//! @brief is there an answer the UI has not shown yet?
		bool hasUnseenResult() const { return this->mUnseenResult; }

		//! @brief the frame boundary: deliver HTTP completions and pick up
		//! the worker thread's result. Call once per frame.
		void update();

		//! @brief is a verified update staged and waiting for a restart?
		bool isReadyToInstall() const;
		//! @brief the version that would be installed ("" when none)
		std::string readyVersion() const { return this->mPendingVersion; }

		//! @brief re-verify the staged payload and hand the swap to the
		//! helper process. The caller then quits: the helper waits for this
		//! process to exit before touching anything.
		//! @param relaunch launch the editor again once the swap succeeded
		//! @return false with @p error set (nothing was changed)
		bool installOnExit(bool relaunch, std::string& error);

		//! @brief drop a staged payload and its download (the user declined,
		//! or a newer one arrived)
		void discardStaged();
	private:
		void beginCheck(bool manual);
		void onFeedResponse(std::string const& body, bool ok,
			std::string const& reason);
		void beginDownload(UpdateAssetChoice const& choice,
			std::string const& version);
		void onChecksumResponse(std::string const& body, bool ok,
			std::string const& reason);
		void onArchiveResponse(std::string const& savedPath, bool ok,
			std::string const& reason);
		void startVerify();
		//! the worker body: digest, unpack, signature (never touches mStatus)
		VerifyOutcome runVerify();
		//! ask the platform whether the staged payload is signed as expected
		bool verifySignature(std::string const& payloadPath,
			std::string& message);
		void fail(std::string const& message);
		void saveState();
		std::string workPath(char const* leaf) const;
		//! remove a directory tree, ignoring what is not there
		void removeTree(std::string const& path);

		EditorUpdater(EditorUpdater const&) = delete;
		EditorUpdater& operator=(EditorUpdater const&) = delete;
	};
}

#endif //__EditorUpdater_h__31_7_2026__09_00_00__
