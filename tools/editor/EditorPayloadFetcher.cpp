/********************************************************************
	created:	Sunday 2026/08/02 at 14:00
	filename: 	EditorPayloadFetcher.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorPayloadFetcher - the orchestration around EditorPayloads' decisions
// (see the header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorPayloadFetcher.h"

#include "EditorUpdate.h"

#include <core_http/HttpClient.h>
#include <core_util/Sha256.h>
#include <core_util/VersionOrder.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		//! the release body is notes plus an asset list - generous, bounded
		const unsigned long long RELEASE_BYTE_CAP = 4ull * 1024ull * 1024ull;
		//! a device player and its media; larger than any editor archive, and
		//! still a ceiling rather than an open pipe
		const unsigned long long ARCHIVE_BYTE_CAP =
			2048ull * 1024ull * 1024ull;
		const std::size_t DIGEST_CHUNK = 1024u * 1024u;
		//! the scratch leaves under the install root
		char const * const DOWNLOAD_DIR = "download";
		char const * const UNPACK_DIR = "unpack";

		Orkige::String joinPath(Orkige::String const & directory,
			Orkige::String const & leaf)
		{
			if(directory.empty())
			{
				return leaf;
			}
			const char last = directory[directory.size() - 1];
			return (last == '/' || last == '\\') ? (directory + leaf)
				: (directory + "/" + leaf);
		}
		//---------------------------------------------------------
		bool pathExists(Orkige::String const & path)
		{
			std::error_code code;
			return std::filesystem::exists(std::filesystem::path(path), code);
		}
		//---------------------------------------------------------
		//! the directory names directly under @p parent, sorted by the
		//! iterator's own order (the caller only ever compares them)
		std::vector<Orkige::String> childDirectories(
			Orkige::String const & parent)
		{
			std::vector<Orkige::String> names;
			std::error_code code;
			for(std::filesystem::directory_iterator entry(
					std::filesystem::path(parent), code);
				!code && entry != std::filesystem::directory_iterator(); ++entry)
			{
				if(entry->is_directory())
				{
					names.push_back(entry->path().filename().string());
				}
			}
			return names;
		}
	}
	//---------------------------------------------------------
	float PayloadFetchStatus::progress() const
	{
		if(this->total == 0)
		{
			return -1.0f;
		}
		const double fraction = static_cast<double>(this->received) /
			static_cast<double>(this->total);
		return static_cast<float>(fraction < 0.0 ? 0.0
			: (fraction > 1.0 ? 1.0 : fraction));
	}
	//---------------------------------------------------------
	EditorPayloadFetcher::EditorPayloadFetcher(Config const & config)
		: mConfig(config)
		, mHttp(NULL)
		, mReleaseRequest(0)
		, mChecksumRequest(0)
		, mArchiveRequest(0)
		, mWorkerRunning(false)
		, mWorkerFinished(false)
	{
	}
	//---------------------------------------------------------
	EditorPayloadFetcher::~EditorPayloadFetcher()
	{
		if(this->mHttp != NULL)
		{
			this->mHttp->cancelOwner(this);
		}
		if(this->mWorker && this->mWorker->joinable())
		{
			this->mWorker->join();
		}
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::setHttpClient(Orkige::HttpClient * client)
	{
		this->mHttp = client;
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::setProcessRunner(ProcessRunner runner)
	{
		this->mRunProcess = runner;
	}
	//---------------------------------------------------------
	PayloadFetchStatus EditorPayloadFetcher::status() const
	{
		return this->mStatus;
	}
	//---------------------------------------------------------
	bool EditorPayloadFetcher::canFetch(Orkige::String & reason) const
	{
		if(this->mConfig.automatedRun)
		{
			reason = "This is an automated run, which never reaches the "
				"network.";
			return false;
		}
		if(this->mConfig.rootDirectory.empty())
		{
			reason = "This installation has nowhere to keep a downloaded "
				"player.";
			return false;
		}
		if(Orkige::VersionOrder::filenameToken(this->mConfig.version).empty())
		{
			// a payload belongs to ONE published build; a binary that was
			// never stamped names no release to take one from
			reason = "This is a local build, so there is no published release "
				"to fetch a matching player from. Package from the engine "
				"source tree instead.";
			return false;
		}
		if(this->mHttp == NULL || !this->mHttp->available())
		{
			reason = "This build cannot reach the network.";
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	Orkige::String EditorPayloadFetcher::installedPath(
		Orkige::String const & id) const
	{
		FetchablePayload payload;
		if(!findFetchablePayload(id, payload))
		{
			return Orkige::String();
		}
		const Orkige::String directory = payloadInstallDirectory(
			this->mConfig.rootDirectory, id, this->mConfig.flavor,
			this->mConfig.version);
		if(directory.empty())
		{
			return Orkige::String();
		}
		// COMPLETE or absent - a half-unpacked copy is never handed to an
		// export, which would fail deep inside a packaging run instead
		return payloadProblems(payload, directory, &pathExists).empty()
			? directory : Orkige::String();
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::beginFetch(Orkige::String const & id)
	{
		if(this->mStatus.busy())
		{
			return;		// one fetch at a time
		}
		FetchablePayload payload;
		if(!findFetchablePayload(id, payload))
		{
			this->fail("There is no such download.");
			return;
		}
		Orkige::String reason;
		if(!this->canFetch(reason))
		{
			this->mStatus.payloadId = id;
			this->fail(reason);
			return;
		}
		this->mPayload = payload;
		this->mAssetName = payloadAssetName(id, this->mConfig.flavor,
			this->mConfig.version);
		this->mInstallDirectory = payloadInstallDirectory(
			this->mConfig.rootDirectory, id, this->mConfig.flavor,
			this->mConfig.version);
		if(this->mAssetName.empty() || this->mInstallDirectory.empty())
		{
			this->fail("This build's version cannot be turned into a download "
				"name.");
			return;
		}
		const Orkige::String url = payloadReleaseUrl(this->mConfig.releasesUrl,
			this->mConfig.version);
		if(url.empty())
		{
			this->fail("This build names no published release to fetch from.");
			return;
		}
		this->mStatus.stage = PayloadFetchStage::Locating;
		this->mStatus.payloadId = id;
		this->mStatus.message = "Looking up the " + payload.label + "...";
		this->mStatus.received = 0;
		this->mStatus.total = 0;

		Orkige::HttpClientRequest request;
		request.url = url;
		request.headers.push_back(std::make_pair(Orkige::String("Accept"),
			Orkige::String("application/vnd.github+json")));
		request.timeoutMs = 20000;
		request.maxResponseBytes = RELEASE_BYTE_CAP;
		request.allowInsecureHttp = this->mConfig.allowInsecureHttp;
		this->mReleaseRequest = this->mHttp->submit(request,
			[this](Orkige::HttpClientResponse const & response)
			{
				this->mReleaseRequest = 0;
				this->onReleaseResponse(response.body, response.ok(),
					response.completed
						? ("the release service answered " +
							std::to_string(response.status))
						: response.reason);
			}, Orkige::HttpProgressCallback(), this);
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::onReleaseResponse(Orkige::String const & body,
		bool ok, Orkige::String const & reason)
	{
		if(!ok)
		{
			this->fail("Could not reach the download service: " + reason);
			return;
		}
		const UpdateRelease release = parseUpdateRelease(body);
		if(!release.valid)
		{
			this->fail(release.problem);
			return;
		}
		// THE PAIRING, asserted rather than assumed: the dated release this
		// build's own version names must be the release that published this
		// build. Anything else and the player would not be the one this
		// editor was built beside.
		if(release.version != this->mConfig.version)
		{
			this->fail("The published release for this build carries version " +
				release.version + ", not " + this->mConfig.version +
				" - Orkige will not mix a player with an editor from another "
				"build.");
			return;
		}
		Orkige::String archiveUrl;
		Orkige::String checksumUrl;
		unsigned long long size = 0;
		for(std::size_t index = 0; index < release.assets.size(); ++index)
		{
			// exact names: the asset an id/flavor/version composes, and the
			// digest sidecar published beside it
			if(release.assets[index].name == this->mAssetName)
			{
				archiveUrl = release.assets[index].url;
				size = release.assets[index].size;
			}
			else if(release.assets[index].name == this->mAssetName + ".sha256")
			{
				checksumUrl = release.assets[index].url;
			}
		}
		if(archiveUrl.empty())
		{
			this->fail("This release carries no " + this->mPayload.label +
				" (" + this->mAssetName + ").");
			return;
		}
		if(checksumUrl.empty())
		{
			// the digest is the WHOLE integrity story here, so its absence is
			// a refusal rather than a download taken on trust
			this->fail("The " + this->mPayload.label + " has no checksum "
				"published beside it, so its bytes cannot be checked.");
			return;
		}
		this->beginDownload(archiveUrl, checksumUrl, size);
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::beginDownload(Orkige::String const & archiveUrl,
		Orkige::String const & checksumUrl, unsigned long long size)
	{
		this->mArchiveFile =
			joinPath(this->workPath(DOWNLOAD_DIR), this->mAssetName);
		this->mStatus.stage = PayloadFetchStage::Downloading;
		this->mStatus.message = "Downloading the " + this->mPayload.label +
			"...";
		this->mStatus.received = 0;
		this->mStatus.total = size;

		// the sidecar first: without a digest to check against there is no
		// point spending hundreds of megabytes of somebody's connection
		Orkige::HttpClientRequest request;
		request.url = checksumUrl;
		request.timeoutMs = 20000;
		request.maxResponseBytes = 64ull * 1024ull;
		request.allowInsecureHttp = this->mConfig.allowInsecureHttp;
		const Orkige::String archive = archiveUrl;
		this->mChecksumRequest = this->mHttp->submit(request,
			[this, archive](Orkige::HttpClientResponse const & response)
			{
				this->mChecksumRequest = 0;
				if(!response.ok())
				{
					this->fail("Could not fetch the download's checksum: " +
						(response.completed
							? ("the checksum file answered " +
								std::to_string(response.status))
							: response.reason));
					return;
				}
				this->mExpectedDigest =
					parseChecksumSidecar(response.body, this->mAssetName);
				if(this->mExpectedDigest.empty())
				{
					this->fail("The checksum file does not name the download, "
						"so its bytes cannot be checked.");
					return;
				}
				Orkige::HttpClientRequest payload;
				payload.url = archive;
				payload.savePath = this->mArchiveFile;
				payload.timeoutMs = 0;		// the backend's own long default
				payload.maxResponseBytes = ARCHIVE_BYTE_CAP;
				payload.allowInsecureHttp = this->mConfig.allowInsecureHttp;
				this->mArchiveRequest = this->mHttp->submit(payload,
					[this](Orkige::HttpClientResponse const & answer)
					{
						this->mArchiveRequest = 0;
						if(!answer.ok())
						{
							this->fail("The download did not finish: " +
								(answer.completed
									? ("the download answered " +
										std::to_string(answer.status))
									: answer.reason));
							return;
						}
						if(!answer.savedPath.empty())
						{
							this->mArchiveFile = answer.savedPath;
						}
						this->mStatus.stage = PayloadFetchStage::Verifying;
						this->mStatus.message = "Verifying and installing...";
						this->mStatus.received = 0;
						this->mStatus.total = 0;
						{
							std::lock_guard<std::mutex> lock(
								this->mWorkerMutex);
							this->mWorkerOutcome = VerifyOutcome();
							this->mWorkerFinished = false;
						}
						this->mWorkerRunning = true;
						if(this->mWorker && this->mWorker->joinable())
						{
							this->mWorker->join();
						}
						// hashing hundreds of megabytes and running the
						// platform's unpacker are the only parts that take
						// real time; they happen HERE, off the frame loop
						this->mWorker.reset(new std::thread([this]()
						{
							const VerifyOutcome outcome = this->runVerify();
							std::lock_guard<std::mutex> lock(
								this->mWorkerMutex);
							this->mWorkerOutcome = outcome;
							this->mWorkerFinished = true;
						}));
					},
					[this](unsigned long long received,
						unsigned long long total)
					{
						this->mStatus.received = received;
						if(total > 0)
						{
							this->mStatus.total = total;
						}
					}, this);
			}, Orkige::HttpProgressCallback(), this);
	}
	//---------------------------------------------------------
	EditorPayloadFetcher::VerifyOutcome EditorPayloadFetcher::runVerify()
	{
		// THE WORKER THREAD. It reads the fields the download completion
		// settled before it was created and writes NOTHING back except the
		// outcome its caller publishes under the mutex.
		VerifyOutcome outcome;
		std::ifstream archive(this->mArchiveFile.c_str(),
			std::ios::in | std::ios::binary);
		if(!archive)
		{
			outcome.message = "The downloaded file could not be read.";
			return outcome;
		}
		Orkige::Sha256 digest;
		std::vector<char> chunk(DIGEST_CHUNK);
		while(archive)
		{
			archive.read(&chunk[0], static_cast<std::streamsize>(DIGEST_CHUNK));
			const std::streamsize read = archive.gcount();
			if(read > 0)
			{
				digest.update(&chunk[0], static_cast<std::size_t>(read));
			}
		}
		archive.close();
		if(!Orkige::Sha256::hexEquals(digest.finishHex(), this->mExpectedDigest))
		{
			outcome.message = "The download does not match its checksum and "
				"was discarded.";
			return outcome;
		}

		// unpack into a scratch directory FIRST, so a failed unpack can never
		// leave a partial tree where a complete payload is expected
		const Orkige::String unpack = this->workPath(UNPACK_DIR);
		this->removeTree(unpack);
		std::error_code code;
		std::filesystem::create_directories(std::filesystem::path(unpack),
			code);
		if(code)
		{
			outcome.message = "The download could not be unpacked: " +
				code.message();
			return outcome;
		}
		const std::vector<Orkige::String> extract =
			payloadExtractCommand(this->mArchiveFile, unpack);
		if(extract.empty() || !this->mRunProcess)
		{
			outcome.message = "This build cannot unpack the download.";
			return outcome;
		}
		Orkige::String output;
		int exitCode = 0;
		if(!this->mRunProcess(extract, output, exitCode) || exitCode != 0)
		{
			outcome.message = "The download could not be unpacked" +
				(output.empty() ? Orkige::String(".")
					: (Orkige::String(": ") + output));
			return outcome;
		}
		const std::vector<Orkige::String> problems =
			payloadProblems(this->mPayload, unpack, &pathExists);
		if(!problems.empty())
		{
			Orkige::String joined;
			for(std::size_t index = 0; index < problems.size(); ++index)
			{
				joined += (index == 0 ? "" : ", ") + problems[index];
			}
			outcome.message = "The download is incomplete (missing " + joined +
				").";
			return outcome;
		}

		// the move-in: the scratch tree becomes the install directory in ONE
		// rename inside the same root, so a payload directory that exists is
		// always a complete one
		this->removeTree(this->mInstallDirectory);
		std::filesystem::create_directories(
			std::filesystem::path(this->mInstallDirectory).parent_path(), code);
		std::filesystem::rename(std::filesystem::path(unpack),
			std::filesystem::path(this->mInstallDirectory), code);
		if(code)
		{
			outcome.message = "The download could not be installed: " +
				code.message();
			this->removeTree(unpack);
			return outcome;
		}
		std::filesystem::remove(std::filesystem::path(this->mArchiveFile),
			code);
		outcome.ok = true;
		return outcome;
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::update()
	{
		if(!this->mWorkerRunning)
		{
			return;
		}
		VerifyOutcome outcome;
		{
			std::lock_guard<std::mutex> lock(this->mWorkerMutex);
			if(!this->mWorkerFinished)
			{
				return;
			}
			outcome = this->mWorkerOutcome;
		}
		this->mWorkerRunning = false;
		if(this->mWorker && this->mWorker->joinable())
		{
			this->mWorker->join();
		}
		if(!outcome.ok)
		{
			this->removeTree(this->workPath(UNPACK_DIR));
			this->fail(outcome.message);
			return;
		}
		this->mStatus.stage = PayloadFetchStage::Installed;
		this->mStatus.message = this->mPayload.label + " installed.";
		this->mStatus.received = 0;
		this->mStatus.total = 0;
	}
	//---------------------------------------------------------
	std::vector<InstalledPayload> EditorPayloadFetcher::installedPayloads() const
	{
		std::vector<InstalledPayload> found;
		if(this->mConfig.rootDirectory.empty())
		{
			return found;
		}
		const std::vector<Orkige::String> ids =
			childDirectories(this->mConfig.rootDirectory);
		for(std::size_t idIndex = 0; idIndex < ids.size(); ++idIndex)
		{
			const Orkige::String idPath =
				joinPath(this->mConfig.rootDirectory, ids[idIndex]);
			const std::vector<Orkige::String> flavors =
				childDirectories(idPath);
			for(std::size_t flavorIndex = 0; flavorIndex < flavors.size();
				++flavorIndex)
			{
				const Orkige::String flavorPath =
					joinPath(idPath, flavors[flavorIndex]);
				const std::vector<Orkige::String> versions =
					childDirectories(flavorPath);
				for(std::size_t index = 0; index < versions.size(); ++index)
				{
					InstalledPayload entry;
					entry.id = ids[idIndex];
					entry.flavor = flavors[flavorIndex];
					entry.version = versions[index];
					entry.path = joinPath(flavorPath, versions[index]);
					found.push_back(entry);
				}
			}
		}
		return found;
	}
	//---------------------------------------------------------
	int EditorPayloadFetcher::prune(
		std::vector<Orkige::String> const & enabledIds)
	{
		const std::vector<Orkige::String> doomed = planPayloadPrune(
			this->installedPayloads(), enabledIds, this->mConfig.flavor,
			this->mConfig.version);
		int removed = 0;
		for(std::size_t index = 0; index < doomed.size(); ++index)
		{
			if(pathExists(doomed[index]))
			{
				this->removeTree(doomed[index]);
				++removed;
			}
		}
		return removed;
	}
	//---------------------------------------------------------
	bool EditorPayloadFetcher::remove(Orkige::String const & id)
	{
		const Orkige::String directory = payloadInstallDirectory(
			this->mConfig.rootDirectory, id, this->mConfig.flavor,
			this->mConfig.version);
		if(directory.empty() || !pathExists(directory))
		{
			return false;
		}
		this->removeTree(directory);
		if(this->mStatus.payloadId == id)
		{
			this->mStatus = PayloadFetchStatus();
		}
		return true;
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::fail(Orkige::String const & message)
	{
		if(this->mHttp != NULL)
		{
			this->mHttp->cancelOwner(this);
		}
		this->mReleaseRequest = 0;
		this->mChecksumRequest = 0;
		this->mArchiveRequest = 0;
		this->mStatus.stage = PayloadFetchStage::Failed;
		this->mStatus.message = message;
		this->mStatus.received = 0;
		this->mStatus.total = 0;
	}
	//---------------------------------------------------------
	Orkige::String EditorPayloadFetcher::workPath(char const * leaf) const
	{
		return joinPath(this->mConfig.rootDirectory, leaf);
	}
	//---------------------------------------------------------
	void EditorPayloadFetcher::removeTree(Orkige::String const & path)
	{
		if(path.empty())
		{
			return;
		}
		std::error_code code;
		std::filesystem::remove_all(std::filesystem::path(path), code);
	}
	//---------------------------------------------------------
	char const * const PAYLOAD_DIRECTORY_ENV = "ORKIGE_EDITOR_PAYLOAD_DIR";
	//---------------------------------------------------------
	Orkige::String resolveInstalledPayload(Orkige::String const & id,
		EditorPayloadFetcher const * fetcher)
	{
		FetchablePayload payload;
		if(!findFetchablePayload(id, payload))
		{
			return Orkige::String();
		}
		const char * const override_ = std::getenv(PAYLOAD_DIRECTORY_ENV);
		if(override_ != NULL && *override_ != '\0')
		{
			const Orkige::String directory = joinPath(override_, id);
			// COMPLETE or not offered: the override skips the download, never
			// the contract a payload has to meet
			if(payloadProblems(payload, directory, &pathExists).empty())
			{
				return directory;
			}
		}
		return fetcher != NULL ? fetcher->installedPath(id) : Orkige::String();
	}
}
