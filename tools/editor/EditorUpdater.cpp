/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	EditorUpdater.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorUpdater - the updater's moving parts (see the header for the shape).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorUpdater.h"

#include <core_debug/DebugMacros.h>
#include <core_filesystem/FileWriter.h>
#include <core_http/HttpClient.h>
#include <core_http/HttpTypes.h>
#include <core_util/Sha256.h>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		//! the stamp file inside the work directory
		char const* const STATE_FILE = "update-state.ini";
		//! subdirectories of the work directory
		char const* const DOWNLOAD_DIR = "download";
		char const* const STAGE_DIR = "staged";
		//! how much of a release body is ever read (the notes, not a payload)
		const unsigned long long FEED_BYTE_CAP = 4ull * 1024ull * 1024ull;
		//! the ceiling on an archive - generous, but not unbounded
		const unsigned long long ARCHIVE_BYTE_CAP =
			2048ull * 1024ull * 1024ull;
		//! how many bytes are hashed at a time (never the whole file)
		const std::size_t DIGEST_CHUNK = 1024u * 256u;

		std::string joinPath(std::string const& directory,
			std::string const& leaf)
		{
			if (directory.empty())
			{
				return leaf;
			}
			const char last = directory[directory.size() - 1];
			if (last == '/' || last == '\\')
			{
				return directory + leaf;
			}
			return directory + "/" + leaf;
		}

		long long systemEpochSeconds()
		{
			return static_cast<long long>(
				std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::system_clock::now().time_since_epoch())
				.count());
		}
	}
	//---------------------------------------------------------
	char const* EditorUpdater::defaultFeedUrl()
	{
		// the ROLLING tag: it always names the newest build, so one request
		// answers "is there something newer than what I run". The dated
		// releases are an archive a person browses, not something a client
		// enumerates.
		return "https://api.github.com/repos/orkitec/orkige/releases/tags/"
			"nightly";
	}
	//---------------------------------------------------------
	EditorUpdater::EditorUpdater(Config const& config)
		: mConfig(config)
		, mPolicy(UpdatePolicy::Notify)
		, mHttp(NULL)
		, mFeedRequest(0)
		, mChecksumRequest(0)
		, mArchiveRequest(0)
		, mLastCheckEpoch(0)
		, mCheckedThisLaunch(false)
		, mHelperSpawned(false)
		, mLastCheckManual(false)
		, mUnseenResult(false)
		, mWorkerRunning(false)
		, mWorkerFinished(false)
	{
		if (this->mConfig.feedUrl.empty())
		{
			this->mConfig.feedUrl = defaultFeedUrl();
		}
		this->mClock = &systemEpochSeconds;
	}
	//---------------------------------------------------------
	EditorUpdater::~EditorUpdater()
	{
		if (this->mHttp != NULL)
		{
			// the owner is going away - the client must not call back into it
			this->mHttp->cancelOwner(this);
		}
		if (this->mWorker && this->mWorker->joinable())
		{
			this->mWorker->join();
		}
	}
	//---------------------------------------------------------
	void EditorUpdater::setHttpClient(Orkige::HttpClient* client)
	{
		this->mHttp = client;
	}
	//---------------------------------------------------------
	void EditorUpdater::setProcessRunner(ProcessRunner runner)
	{
		this->mRunProcess = runner;
	}
	//---------------------------------------------------------
	void EditorUpdater::setDetachedSpawn(DetachedSpawn spawn)
	{
		this->mSpawnDetached = spawn;
	}
	//---------------------------------------------------------
	void EditorUpdater::setClock(Clock clock)
	{
		if (clock)
		{
			this->mClock = clock;
		}
	}
	//---------------------------------------------------------
	void EditorUpdater::setPolicy(UpdatePolicy policy)
	{
		if (policy == this->mPolicy)
		{
			return;
		}
		const UpdatePolicy previous = this->mPolicy;
		this->mPolicy = policy;
		// switching checking ON mid-session re-arms this launch's one check,
		// so the setting takes effect when it is made rather than at the next
		// start. Never while something is already running.
		if (previous == UpdatePolicy::Off && policy != UpdatePolicy::Off &&
			!this->mStatus.busy())
		{
			this->mCheckedThisLaunch = false;
		}
	}
	//---------------------------------------------------------
	UpdateStatus EditorUpdater::status() const
	{
		return this->mStatus;
	}
	//---------------------------------------------------------
	std::string EditorUpdater::workPath(char const* leaf) const
	{
		return joinPath(this->mConfig.workDirectory, leaf);
	}
	//---------------------------------------------------------
	void EditorUpdater::removeTree(std::string const& path)
	{
		if (path.empty())
		{
			return;
		}
		std::error_code code;
		std::filesystem::remove_all(std::filesystem::path(path), code);
	}
	//---------------------------------------------------------
	void EditorUpdater::loadState()
	{
		// an automated run keeps its hands off user state entirely - it does
		// not even read the stamp
		if (this->mConfig.automatedRun || this->mConfig.workDirectory.empty())
		{
			return;
		}
		std::ifstream file(this->workPath(STATE_FILE));
		std::string line;
		std::string version;
		std::string payload;
		while (std::getline(file, line))
		{
			const std::size_t equals = line.find('=');
			if (equals == std::string::npos)
			{
				continue;
			}
			const std::string key = line.substr(0, equals);
			const std::string value = line.substr(equals + 1);
			if (key == "last_check")
			{
				this->mLastCheckEpoch = std::strtoll(value.c_str(), NULL, 10);
			}
			else if (key == "pending_version")
			{
				version = value;
			}
			else if (key == "pending_payload")
			{
				payload = value;
			}
		}
		if (version.empty() || payload.empty())
		{
			return;
		}
		// a payload a previous session verified but never installed: keep it
		// only while it is still there AND still newer than what runs now (a
		// build installed by other means makes it stale, and a stale payload
		// must never be swapped in - that would be the downgrade the whole
		// comparison exists to refuse)
		std::error_code code;
		const bool present =
			std::filesystem::exists(std::filesystem::path(payload), code);
		if (!present ||
			judgeUpdate(version, this->mConfig.currentVersion) !=
				UpdateVerdict::Offer)
		{
			this->removeTree(payload);
			this->mPendingVersion.clear();
			this->mPendingPayload.clear();
			this->saveState();
			return;
		}
		this->mPendingVersion = version;
		this->mPendingPayload = payload;
		this->mStatus.stage = UpdateStage::Ready;
		this->mStatus.version = version;
		this->mStatus.message = "Version " + version +
			" is ready and will be installed when the editor restarts.";
	}
	//---------------------------------------------------------
	void EditorUpdater::saveState()
	{
		if (this->mConfig.automatedRun || this->mConfig.workDirectory.empty())
		{
			return;
		}
		std::ostringstream text;
		text << "last_check=" << this->mLastCheckEpoch << "\n";
		text << "pending_version=" << this->mPendingVersion << "\n";
		text << "pending_payload=" << this->mPendingPayload << "\n";
		Orkige::String error;
		if (!Orkige::FileWriter::writeWholeFile(this->workPath(STATE_FILE),
			text.str(), error))
		{
			oDebugWarn("update", 0, "could not record the update state: "
				<< error);
		}
	}
	//---------------------------------------------------------
	void EditorUpdater::tickAutomaticCheck()
	{
		if (this->mCheckedThisLaunch)
		{
			return;
		}
		UpdateCheckContext context;
		context.policy = this->mPolicy;
		context.automatedRun = this->mConfig.automatedRun;
		context.manual = false;
		context.hasOrderedVersion = !this->mConfig.currentVersion.empty();
		context.lastCheckEpochSeconds = this->mLastCheckEpoch;
		context.nowEpochSeconds = this->mClock();
		const UpdateCheckDecision decision = decideUpdateCheck(context);
		// whatever the answer, this launch has now made its one decision -
		// the gate is never re-evaluated per frame
		this->mCheckedThisLaunch = true;
		if (decision != UpdateCheckDecision::Run)
		{
			return;
		}
		this->beginCheck(false);
	}
	//---------------------------------------------------------
	void EditorUpdater::requestManualCheck()
	{
		UpdateCheckContext context;
		context.policy = this->mPolicy;
		context.automatedRun = this->mConfig.automatedRun;
		context.manual = true;
		context.hasOrderedVersion = !this->mConfig.currentVersion.empty();
		context.lastCheckEpochSeconds = this->mLastCheckEpoch;
		context.nowEpochSeconds = this->mClock();
		const UpdateCheckDecision decision = decideUpdateCheck(context);
		this->mCheckedThisLaunch = true;
		if (decision != UpdateCheckDecision::Run)
		{
			this->mLastCheckManual = true;
			this->mUnseenResult = true;
			this->mStatus.stage = UpdateStage::Failed;
			this->mStatus.message = updateCheckDecisionReason(decision);
			return;
		}
		this->beginCheck(true);
	}
	//---------------------------------------------------------
	void EditorUpdater::acknowledge()
	{
		this->mUnseenResult = false;
	}
	//---------------------------------------------------------
	void EditorUpdater::fail(std::string const& message)
	{
		this->mStatus.stage = UpdateStage::Failed;
		this->mStatus.message = message;
		this->mStatus.received = 0;
		this->mStatus.total = 0;
		this->mUnseenResult = true;
		oDebugWarn("update", 0, message);
	}
	//---------------------------------------------------------
	void EditorUpdater::beginCheck(bool manual)
	{
		this->mLastCheckManual = manual;
		if (this->mStatus.busy() || this->mFeedRequest != 0)
		{
			return;		// one conversation at a time
		}
		if (this->mHttp == NULL || !this->mHttp->available())
		{
			this->fail("This build cannot reach the network, so it cannot "
				"check for updates.");
			return;
		}
		this->mStatus.stage = UpdateStage::Checking;
		this->mStatus.message = "Checking for updates...";
		this->mStatus.received = 0;
		this->mStatus.total = 0;
		this->mUnseenResult = false;

		Orkige::HttpClientRequest request;
		request.url = this->mConfig.feedUrl;
		request.headers.push_back(
			std::make_pair(Orkige::String("Accept"),
				Orkige::String("application/vnd.github+json")));
		request.timeoutMs = 20000;
		request.maxResponseBytes = FEED_BYTE_CAP;
		request.allowInsecureHttp = this->mConfig.allowInsecureHttp;
		this->mFeedRequest = this->mHttp->submit(request,
			[this](Orkige::HttpClientResponse const& response)
			{
				this->mFeedRequest = 0;
				this->onFeedResponse(response.body, response.ok(),
					response.completed
						? ("the update service answered " +
							std::to_string(response.status))
						: response.reason);
			},
			Orkige::HttpProgressCallback(), this);
		// the stamp records that a check RAN, not that it found something -
		// a failed check must not make the next launch retry immediately in
		// a loop, and a manual check is always available
		this->mLastCheckEpoch = this->mClock();
		this->saveState();
	}
	//---------------------------------------------------------
	void EditorUpdater::onFeedResponse(std::string const& body, bool ok,
		std::string const& reason)
	{
		if (!ok)
		{
			this->fail("Could not check for updates: " + reason);
			return;
		}
		const UpdateRelease release = parseUpdateRelease(body);
		if (!release.valid)
		{
			this->fail(release.problem);
			return;
		}
		const UpdateVerdict verdict =
			judgeUpdate(release.version, this->mConfig.currentVersion);
		if (verdict != UpdateVerdict::Offer)
		{
			// UpToDate, a downgrade and an incomparable pair are all "nothing
			// to do" - each with its own sentence
			this->mStatus.stage = verdict == UpdateVerdict::UpToDate
				? UpdateStage::UpToDate : UpdateStage::Failed;
			this->mStatus.version = release.version;
			this->mStatus.message = updateVerdictReason(verdict);
			this->mStatus.changelog.clear();
			this->mUnseenResult = true;
			return;
		}
		// something newer exists; a payload staged for an older version is
		// now stale
		if (!this->mPendingVersion.empty() &&
			this->mPendingVersion != release.version)
		{
			this->discardStaged();
		}
		this->mStatus.version = release.version;
		this->mStatus.changelog = release.changelog;
		if (this->mPendingVersion == release.version &&
			!this->mPendingPayload.empty())
		{
			this->mStatus.stage = UpdateStage::Ready;
			this->mStatus.message = "Version " + release.version +
				" is ready and will be installed when the editor restarts.";
			this->mUnseenResult = true;
			return;
		}
		const UpdateAssetChoice choice = selectUpdateAssets(release.assets,
			this->mConfig.platform, release.version);
		this->mStatus.stage = UpdateStage::Available;
		this->mStatus.message = "Version " + release.version +
			" is available.";
		this->mUnseenResult = true;
		if (this->mPolicy != UpdatePolicy::Download)
		{
			return;		// Notify (and Off, for a manual check): say so, stop
		}
		if (!choice.found)
		{
			this->fail(choice.problem);
			return;
		}
		this->beginDownload(choice, release.version);
	}
	//---------------------------------------------------------
	void EditorUpdater::beginDownload(UpdateAssetChoice const& choice,
		std::string const& version)
	{
		if (this->mHttp == NULL || this->mConfig.workDirectory.empty())
		{
			this->fail("There is nowhere to download the update to.");
			return;
		}
		this->mArchiveName = choice.archive.name;
		this->mArchiveFile = joinPath(this->workPath(DOWNLOAD_DIR),
			choice.archive.name);
		this->mStatus.version = version;
		this->mStatus.stage = UpdateStage::Downloading;
		this->mStatus.message = "Downloading version " + version + "...";
		this->mStatus.received = 0;
		this->mStatus.total = choice.archive.size;

		// the sidecar first: without a digest to check against there is no
		// point spending a hundred megabytes of somebody's connection
		Orkige::HttpClientRequest request;
		request.url = choice.checksum.url;
		request.timeoutMs = 20000;
		request.maxResponseBytes = 64ull * 1024ull;
		request.allowInsecureHttp = this->mConfig.allowInsecureHttp;
		const std::string archiveUrl = choice.archive.url;
		this->mChecksumRequest = this->mHttp->submit(request,
			[this, archiveUrl](Orkige::HttpClientResponse const& response)
			{
				this->mChecksumRequest = 0;
				this->onChecksumResponse(response.body, response.ok(),
					response.completed
						? ("the checksum file answered " +
							std::to_string(response.status))
						: response.reason);
				if (this->mStatus.stage != UpdateStage::Downloading)
				{
					return;
				}
				Orkige::HttpClientRequest archive;
				archive.url = archiveUrl;
				archive.savePath = this->mArchiveFile;
				archive.timeoutMs = 0;		// the backend's own long default
				archive.maxResponseBytes = ARCHIVE_BYTE_CAP;
				archive.allowInsecureHttp = this->mConfig.allowInsecureHttp;
				this->mArchiveRequest = this->mHttp->submit(archive,
					[this](Orkige::HttpClientResponse const& answer)
					{
						this->mArchiveRequest = 0;
						this->onArchiveResponse(answer.savedPath, answer.ok(),
							answer.completed
								? ("the download answered " +
									std::to_string(answer.status))
								: answer.reason);
					},
					[this](unsigned long long received,
						unsigned long long total)
					{
						this->mStatus.received = received;
						if (total > 0)
						{
							this->mStatus.total = total;
						}
					}, this);
			}, Orkige::HttpProgressCallback(), this);
	}
	//---------------------------------------------------------
	void EditorUpdater::onChecksumResponse(std::string const& body, bool ok,
		std::string const& reason)
	{
		if (!ok)
		{
			this->fail("Could not fetch the update's checksum: " + reason);
			return;
		}
		this->mExpectedDigest = parseChecksumSidecar(body, this->mArchiveName);
		if (this->mExpectedDigest.empty())
		{
			this->fail("The update's checksum file does not name the "
				"download, so the bytes cannot be checked.");
		}
	}
	//---------------------------------------------------------
	void EditorUpdater::onArchiveResponse(std::string const& savedPath,
		bool ok, std::string const& reason)
	{
		if (!ok)
		{
			this->fail("The update download did not finish: " + reason);
			return;
		}
		if (!savedPath.empty())
		{
			this->mArchiveFile = savedPath;
		}
		this->startVerify();
	}
	//---------------------------------------------------------
	void EditorUpdater::startVerify()
	{
		if (this->mWorkerRunning)
		{
			return;
		}
		this->mStatus.stage = UpdateStage::Verifying;
		this->mStatus.message = "Verifying the download...";
		this->mStatus.received = 0;
		this->mStatus.total = 0;
		{
			std::lock_guard<std::mutex> lock(this->mWorkerMutex);
			this->mWorkerOutcome = VerifyOutcome();
			this->mWorkerFinished = false;
		}
		this->mWorkerRunning = true;
		if (this->mWorker && this->mWorker->joinable())
		{
			this->mWorker->join();
		}
		// hashing a hundred megabytes and running the platform's unpacker are
		// the only parts that genuinely take time; they happen HERE, off the
		// frame loop, and update() picks the answer up
		this->mWorker.reset(new std::thread([this]()
		{
			const VerifyOutcome outcome = this->runVerify();
			std::lock_guard<std::mutex> lock(this->mWorkerMutex);
			this->mWorkerOutcome = outcome;
			this->mWorkerFinished = true;
		}));
	}
	//---------------------------------------------------------
	EditorUpdater::VerifyOutcome EditorUpdater::runVerify()
	{
		// THE WORKER THREAD. It reads the fields startVerify() settled before
		// it was created (the archive path, the expected digest, the config)
		// and writes NOTHING back except the outcome its caller publishes
		// under the mutex - so mStatus stays a main-thread-only value and
		// there is no shared mutable state to race over.
		VerifyOutcome outcome;
		// 1. the digest, streamed - the download is never held in memory
		std::ifstream archive(this->mArchiveFile.c_str(),
			std::ios::in | std::ios::binary);
		if (!archive)
		{
			outcome.message = "The downloaded file could not be read.";
			return outcome;
		}
		Orkige::Sha256 digest;
		std::vector<char> chunk(DIGEST_CHUNK);
		while (archive)
		{
			archive.read(&chunk[0], static_cast<std::streamsize>(DIGEST_CHUNK));
			const std::streamsize read = archive.gcount();
			if (read > 0)
			{
				digest.update(&chunk[0], static_cast<std::size_t>(read));
			}
		}
		archive.close();
		const Orkige::String actual = digest.finishHex();
		if (!Orkige::Sha256::hexEquals(actual, this->mExpectedDigest))
		{
			outcome.message = "The download does not match its checksum and "
				"was discarded.";
			return outcome;
		}

		// 2. unpack into a fresh staging directory
		const std::string stage = this->workPath(STAGE_DIR);
		this->removeTree(stage);
		std::error_code code;
		std::filesystem::create_directories(std::filesystem::path(stage),
			code);
		if (code)
		{
			outcome.message = "The update could not be unpacked: " +
				code.message();
			return outcome;
		}
		const std::vector<std::string> extract = updateExtractCommand(
			this->mConfig.platform, this->mArchiveFile, stage);
		if (extract.empty() || !this->mRunProcess)
		{
			outcome.message = "This build cannot unpack the update.";
			return outcome;
		}
		std::string output;
		int exitCode = 0;
		if (!this->mRunProcess(extract, output, exitCode) || exitCode != 0)
		{
			outcome.message = "The update could not be unpacked" +
				(output.empty() ? std::string(".") : (": " + output));
			return outcome;
		}

		// 3. find the payload: the archive has ONE top-level directory, and
		// what has to be moved into place is the app inside it (macOS) or
		// that directory itself (everywhere else)
		std::string top;
		for (std::filesystem::directory_iterator entry(
				std::filesystem::path(stage), code);
			!code && entry != std::filesystem::directory_iterator(); ++entry)
		{
			if (entry->is_directory())
			{
				if (!top.empty())
				{
					top.clear();
					break;		// more than one: not a shape we understand
				}
				top = entry->path().string();
			}
		}
		if (top.empty())
		{
			outcome.message = "The update's contents are not the expected "
				"shape.";
			return outcome;
		}
		std::string payload = top;
		if (this->mConfig.platform == UpdatePlatform::MacOS)
		{
			payload = joinPath(top, updatePayloadName(this->mConfig.platform));
			if (!std::filesystem::exists(std::filesystem::path(payload), code))
			{
				outcome.message = "The update does not contain an editor.";
				return outcome;
			}
		}

		// 4. the signature, where the platform has one
		std::string signatureNote;
		if (!this->verifySignature(payload, signatureNote))
		{
			outcome.message = signatureNote;
			return outcome;
		}
		outcome.signatureChecked = !signatureNote.empty();
		outcome.ok = true;
		outcome.payloadPath = payload;
		outcome.message = signatureNote;
		return outcome;
	}
	//---------------------------------------------------------
	bool EditorUpdater::verifySignature(std::string const& payloadPath,
		std::string& message)
	{
		const std::vector<std::string> verify = updateSignatureVerifyCommand(
			this->mConfig.platform, payloadPath);
		if (verify.empty())
		{
			// no signature exists on this platform's published builds; the
			// digest was the whole check, and saying so beats implying the
			// platforms are equal
			message.clear();
			return true;
		}
		if (!this->mRunProcess)
		{
			message = "The update's signature could not be checked.";
			return false;
		}
		std::string output;
		int exitCode = 0;
		if (!this->mRunProcess(verify, output, exitCode))
		{
			message = "The update's signature could not be checked.";
			return false;
		}
		if (exitCode != 0)
		{
			message = "The update is not correctly signed and was discarded"
				+ (output.empty() ? std::string(".") : (": " + output));
			return false;
		}
		const std::vector<std::string> assess = updateSignatureAssessCommand(
			this->mConfig.platform, payloadPath);
		if (!assess.empty())
		{
			output.clear();
			exitCode = 0;
			if (!this->mRunProcess(assess, output, exitCode) || exitCode != 0)
			{
				message = "The system refused the update's signature and it "
					"was discarded"
					+ (output.empty() ? std::string(".") : (": " + output));
				return false;
			}
		}
		message = "signature verified";
		return true;
	}
	//---------------------------------------------------------
	void EditorUpdater::update()
	{
		if (!this->mWorkerRunning)
		{
			return;
		}
		VerifyOutcome outcome;
		{
			std::lock_guard<std::mutex> lock(this->mWorkerMutex);
			if (!this->mWorkerFinished)
			{
				return;
			}
			outcome = this->mWorkerOutcome;
		}
		this->mWorkerRunning = false;
		if (this->mWorker && this->mWorker->joinable())
		{
			this->mWorker->join();
		}
		if (!outcome.ok)
		{
			this->removeTree(this->workPath(STAGE_DIR));
			this->removeTree(this->mArchiveFile);
			this->fail(outcome.message);
			return;
		}
		// the download has served its purpose; the unpacked copy is what gets
		// installed, and keeping a second hundred megabytes around helps
		// nobody
		this->removeTree(this->mArchiveFile);
		this->mPendingVersion = this->mStatus.version;
		this->mPendingPayload = outcome.payloadPath;
		this->saveState();
		this->mStatus.stage = UpdateStage::Ready;
		this->mStatus.message = "Version " + this->mPendingVersion +
			" is ready and will be installed when the editor restarts.";
		this->mUnseenResult = true;
	}
	//---------------------------------------------------------
	bool EditorUpdater::isReadyToInstall() const
	{
		return !this->mPendingPayload.empty() &&
			!this->mPendingVersion.empty() && !this->mHelperSpawned;
	}
	//---------------------------------------------------------
	void EditorUpdater::discardStaged()
	{
		this->removeTree(this->workPath(STAGE_DIR));
		this->removeTree(this->mArchiveFile);
		this->mPendingVersion.clear();
		this->mPendingPayload.clear();
		this->saveState();
	}
	//---------------------------------------------------------
	bool EditorUpdater::installOnExit(bool relaunch, std::string& error)
	{
		error.clear();
		if (!this->isReadyToInstall())
		{
			error = "There is no update staged.";
			return false;
		}
		if (this->mConfig.automatedRun)
		{
			error = "Automated runs never install updates.";
			return false;
		}
		// the version is compared ONE more time here: a payload verified in a
		// previous session could have been overtaken by a build installed
		// another way, and a swap that went backwards is exactly what the
		// ordering exists to prevent
		if (judgeUpdate(this->mPendingVersion, this->mConfig.currentVersion) !=
			UpdateVerdict::Offer)
		{
			this->discardStaged();
			error = "The staged update is no longer newer than this build.";
			return false;
		}

		InstallLocationFacts facts;
		facts.installPath = this->mConfig.installPath;
		if (!facts.installPath.empty())
		{
			const std::filesystem::path installed(facts.installPath);
			facts.containerPath = installed.parent_path().string();
			std::error_code code;
			facts.installExists = std::filesystem::exists(installed, code);
			// "can this directory be written" answered by DOING it: a probe
			// file created and removed beside the install, which is the only
			// answer that accounts for every layer that could refuse
			if (!facts.containerPath.empty())
			{
				const std::filesystem::path probe =
					std::filesystem::path(facts.containerPath) /
						".orkige-update-probe";
				std::ofstream test(probe.string().c_str());
				facts.containerWritable = test.is_open();
				test.close();
				std::filesystem::remove(probe, code);
			}
			facts.translocated = isTranslocatedPath(facts.installPath);
			// a build tree carries a CMake cache at or just above the
			// install (the shape an editor built in one sits in); the
			// resource locator's own answer is the exact one and comes in
			// through the config
			facts.insideBuildTree = this->mConfig.builtFromTree;
			std::filesystem::path walk = installed;
			for (int depth = 0; depth < 4 && !walk.empty(); ++depth)
			{
				if (std::filesystem::exists(walk / "CMakeCache.txt", code))
				{
					facts.insideBuildTree = true;
					break;
				}
				const std::filesystem::path parent = walk.parent_path();
				if (parent == walk)
				{
					break;
				}
				walk = parent;
			}
		}
		if (this->mConfig.assumeUpdatableLocation)
		{
			facts.insideBuildTree = false;
			facts.translocated = false;
		}
		const InstallLocationVerdict location = judgeInstallLocation(facts);
		if (location != InstallLocationVerdict::Updatable)
		{
			error = installLocationReason(location);
			return false;
		}

		// re-verify the staged payload immediately before handing it over:
		// it was checked when it arrived, and this closes the window between
		// then and now
		std::error_code code;
		if (!std::filesystem::exists(
			std::filesystem::path(this->mPendingPayload), code))
		{
			this->discardStaged();
			error = "The staged update is no longer there.";
			return false;
		}
		std::string signatureNote;
		if (!this->verifySignature(this->mPendingPayload, signatureNote))
		{
			this->discardStaged();
			error = signatureNote;
			return false;
		}

		UpdateHelperSpec spec;
		spec.plan = planUpdateSwap(this->mConfig.installPath,
			this->mPendingPayload, this->mPendingVersion);
		if (!spec.plan.valid)
		{
			error = spec.plan.problem;
			return false;
		}
		spec.pid = this->mConfig.pid;
		spec.platform = this->mConfig.platform;
		spec.relaunchPath = relaunch
			? (this->mConfig.relaunchPath.empty()
				? this->mConfig.installPath : this->mConfig.relaunchPath)
			: std::string();
		const std::string script = composeUpdateHelperScript(spec);
		if (script.empty())
		{
			error = "The update could not be prepared for installation.";
			return false;
		}
		const std::string scriptPath = this->workPath(
			updateHelperFileName(this->mConfig.platform));
		Orkige::String writeError;
		if (!Orkige::FileWriter::writeWholeFile(scriptPath, script,
			writeError))
		{
			error = "The update helper could not be written: " + writeError;
			return false;
		}
		std::filesystem::permissions(std::filesystem::path(scriptPath),
			std::filesystem::perms::owner_all, code);
		const std::vector<std::string> command = updateHelperCommand(
			this->mConfig.platform, scriptPath);
		if (!this->mSpawnDetached || !this->mSpawnDetached(command))
		{
			error = "The update helper could not be started.";
			return false;
		}
		this->mHelperSpawned = true;
		// the payload is the helper's now; forgetting it here keeps a later
		// launch from finding a stamp pointing at a directory that moved
		this->mPendingPayload.clear();
		this->mPendingVersion.clear();
		this->saveState();
		return true;
	}
	//---------------------------------------------------------
}
