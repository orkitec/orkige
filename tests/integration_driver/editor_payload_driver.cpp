/********************************************************************
	created:	Sunday 2026/08/02 at 14:00
	filename: 	editor_payload_driver.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// The whole device-player download, headlessly, against a clean room.
//
// The tree's own HttpServer serves a fake DATED release on 127.0.0.1, carrying
// a real archive built with the platform's own packing tool and the `.sha256`
// sidecar published beside it; the REAL EditorPayloadFetcher is pointed at it
// with a scratch directory standing in for the editor's writable state.
// Everything a person would experience is asserted here: a fetch that verifies
// and installs, a release that carries no such asset, bytes that do not match
// their checksum, an archive that unpacks incomplete, a release published for
// ANOTHER build (the pairing this whole mechanism rests on), the prune that
// keeps only what this build needs, and the automated-run veto.
//
// Nothing is stubbed but the release service itself: the bytes, the digest,
// the unpacking and the install rename are all real.
//
// Exit code is the contract: 0 = every leg passed.

#include <core_debugnet/HttpServer.h>
#include <core_http/HttpClient.h>
#include <core_util/Sha256.h>

#include "EditorPayloadFetcher.h"
#include "EditorPayloads.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Orkige::HttpClient;
using Orkige::HttpRequest;
using Orkige::HttpResponse;
using Orkige::HttpServer;
using OrkigeEditor::EditorPayloadFetcher;
using OrkigeEditor::PayloadFetchStage;

namespace
{
	int gFailures = 0;

	void report(bool condition, std::string const & what)
	{
		std::cout << (condition ? "[payload]   ok   " : "[payload]  FAIL  ")
			<< what << std::endl;
		if(!condition)
		{
			++gFailures;
		}
	}
	//---------------------------------------------------------
	void note(std::string const & what)
	{
		std::cout << "[payload] " << what << std::endl;
	}
	//---------------------------------------------------------
	std::string readWholeFile(std::string const & path)
	{
		std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
		std::ostringstream text;
		text << file.rdbuf();
		return text.str();
	}
	//---------------------------------------------------------
	void writeWholeFile(std::string const & path, std::string const & bytes)
	{
		std::filesystem::create_directories(
			std::filesystem::path(path).parent_path());
		std::ofstream file(path.c_str(), std::ios::out | std::ios::binary);
		file << bytes;
	}
	//---------------------------------------------------------
	bool pathExists(std::string const & path)
	{
		std::error_code code;
		return std::filesystem::exists(std::filesystem::path(path), code);
	}
	//---------------------------------------------------------
	//! quote for a POSIX shell (the driver runs the platform's own tools)
	std::string quote(std::string const & text)
	{
		std::string quoted = "'";
		for(std::size_t index = 0; index < text.size(); ++index)
		{
			if(text[index] == '\'')
			{
				quoted += "'\\''";
			}
			else
			{
				quoted += text[index];
			}
		}
		return quoted + "'";
	}
	//---------------------------------------------------------
	bool runCaptured(std::vector<std::string> const & argv,
		std::string & output, int & exitCode)
	{
		std::string command;
		for(std::size_t index = 0; index < argv.size(); ++index)
		{
			command += (index == 0 ? "" : " ") + quote(argv[index]);
		}
		command += " 2>&1";
		output.clear();
		FILE * pipe = popen(command.c_str(), "r");
		if(pipe == NULL)
		{
			return false;
		}
		char buffer[512];
		while(fgets(buffer, sizeof(buffer), pipe) != NULL)
		{
			output += buffer;
		}
		const int status = pclose(pipe);
		exitCode = (status == -1) ? -1 : (status / 256);
		return true;
	}
	//---------------------------------------------------------
	//! pack @p directory's CONTENTS at the archive root - the shape the
	//! publishing side produces (Util/orkige_nightly_package.py)
	bool packPayload(std::string const & directory,
		std::string const & archivePath)
	{
		std::vector<std::string> argv;
#if defined(__APPLE__)
		argv.push_back("/usr/bin/ditto");
		argv.push_back("-c");
		argv.push_back("-k");
		argv.push_back("--sequesterRsrc");
		argv.push_back(directory);
		argv.push_back(archivePath);
#else
		argv.push_back("sh");
		argv.push_back("-c");
		argv.push_back("cd " + quote(directory) + " && zip -r -q -y -X " +
			quote(archivePath) + " .");
#endif
		std::string output;
		int exitCode = 0;
		if(!runCaptured(argv, output, exitCode) || exitCode != 0)
		{
			std::cout << "[payload] could not pack the fixture: " << output
				<< std::endl;
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	const char * const VERSION = "2.0.0-nightly.20260802+dea551f9e0";
	const char * const OTHER_VERSION = "2.0.0-nightly.20260801+aaaaaaaaa";
	const char * const FLAVOR = "next";
	const char * const PAYLOAD_ID = "player-ios-simulator";

	//! the release body the publishing side writes, carrying the ordered
	//! version in the marker a client reads it out of
	std::string releaseJson(std::string const & version,
		std::string const & baseUrl, std::string const & assetName,
		bool withArchive, bool withChecksum)
	{
		std::string body =
			"## Downloads\\n\\n"
			"<!-- orkige-nightly-commit: dea551f9e0 -->\\n"
			"<!-- orkige-nightly-version: " + version + " -->\\n";
		std::string json = "{\"tag_name\":\"nightly-20260802\",\"body\":\"" +
			body + "\",\"assets\":[";
		std::string entries;
		if(withArchive)
		{
			entries += "{\"name\":\"" + assetName + "\",\"size\":0,"
				"\"browser_download_url\":\"" + baseUrl + "/" + assetName +
				"\"}";
		}
		if(withChecksum)
		{
			entries += (entries.empty() ? "" : ",");
			entries += "{\"name\":\"" + assetName + ".sha256\",\"size\":0,"
				"\"browser_download_url\":\"" + baseUrl + "/" + assetName +
				".sha256\"}";
		}
		return json + entries + "]}";
	}
	//---------------------------------------------------------
	//! the fake release service: one release document and two files, in memory
	struct FakeService
	{
		std::string	release;
		std::string	assetName;
		std::string	archiveBytes;
		std::string	sidecar;

		HttpServer::Handler handler()
		{
			return [this](HttpRequest const & request)
			{
				HttpResponse response;
				if(request.target.find("/releases/tags/") == 0)
				{
					response.contentType = "application/json";
					response.body = this->release;
					return response;
				}
				if(request.target == "/" + this->assetName)
				{
					response.contentType = "application/octet-stream";
					response.body = this->archiveBytes;
					return response;
				}
				if(request.target == "/" + this->assetName + ".sha256")
				{
					response.contentType = "text/plain";
					response.body = this->sidecar;
					return response;
				}
				response.status = 404;
				response.reason = "Not Found";
				response.contentType = "text/plain";
				response.body = "no";
				return response;
			};
		}
	};
	//---------------------------------------------------------
	template <typename Predicate>
	bool pumpUntil(HttpServer & server, HttpServer::Handler const & handler,
		HttpClient & client, EditorPayloadFetcher & fetcher,
		Predicate predicate, int timeoutMs = 60000)
	{
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMs);
		while(std::chrono::steady_clock::now() < deadline)
		{
			server.update(handler);
			client.update();
			fetcher.update();
			if(predicate())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		server.update(handler);
		client.update();
		fetcher.update();
		return predicate();
	}
	//---------------------------------------------------------
	EditorPayloadFetcher::Config makeConfig(HttpServer const & server,
		std::string const & root)
	{
		EditorPayloadFetcher::Config config;
		config.releasesUrl = "http://127.0.0.1:" +
			std::to_string(server.getPort()) + "/releases";
		config.version = VERSION;
		config.flavor = FLAVOR;
		config.rootDirectory = root;
		config.allowInsecureHttp = true;	// loopback, no certificate to have
		return config;
	}
	//---------------------------------------------------------
	EditorPayloadFetcher::ProcessRunner realRunner()
	{
		return [](std::vector<std::string> const & argv, std::string & output,
			int & exitCode)
		{
			return runCaptured(argv, output, exitCode);
		};
	}
	//---------------------------------------------------------
	//! run one fetch to its end and answer with the final stage
	PayloadFetchStage fetchOnce(HttpServer & server,
		HttpServer::Handler const & handler, HttpClient & client,
		EditorPayloadFetcher & fetcher)
	{
		fetcher.beginFetch(PAYLOAD_ID);
		pumpUntil(server, handler, client, fetcher, [&fetcher]()
		{
			const PayloadFetchStage stage = fetcher.status().stage;
			return stage == PayloadFetchStage::Installed ||
				stage == PayloadFetchStage::Failed;
		});
		return fetcher.status().stage;
	}
}

int main(int argc, char ** argv)
{
	if(argc < 2)
	{
		std::cout << "usage: editor_payload_driver <scratch dir>" << std::endl;
		return 2;
	}
	const std::string scratch = argv[1];
	std::error_code code;
	std::filesystem::remove_all(std::filesystem::path(scratch), code);
	std::filesystem::create_directories(std::filesystem::path(scratch), code);

	// --- the fixture payload: exactly the shape the publishing side writes
	const std::string payloadDir = scratch + "/compose";
	writeWholeFile(payloadDir + "/orkige_payload.txt",
		std::string("platform: ios-simulator\nflavor: ") + FLAVOR +
		"\nversion: " + VERSION + "\n");
	writeWholeFile(payloadDir + "/OrkigePlayer.app/Info.plist",
		"<plist><dict/></plist>\n");
	writeWholeFile(payloadDir + "/Media/Hlms/marker.txt", "shader templates\n");
	const std::string assetName = OrkigeEditor::payloadAssetName(PAYLOAD_ID,
		FLAVOR, VERSION);
	if(assetName.empty())
	{
		std::cout << "[payload] SKIP: no asset name could be composed"
			<< std::endl;
		return 77;
	}
	const std::string archivePath = scratch + "/serve/" + assetName;
	std::filesystem::create_directories(
		std::filesystem::path(archivePath).parent_path(), code);
	if(!packPayload(payloadDir, archivePath))
	{
		std::cout << "[payload] SKIP: the fixture archive could not be packed "
			"on this machine" << std::endl;
		return 77;
	}
	note("fixture ready: " + assetName);

	HttpServer server;
	if(!server.start(0))
	{
		std::cout << "[payload] SKIP: no loopback port available" << std::endl;
		return 77;
	}
	const std::string baseUrl =
		"http://127.0.0.1:" + std::to_string(server.getPort());

	FakeService service;
	service.assetName = assetName;
	service.archiveBytes = readWholeFile(archivePath);
	const Orkige::String digest = Orkige::Sha256::hexDigest(
		service.archiveBytes.data(), service.archiveBytes.size());
	service.sidecar = digest + "  " + assetName + "\n";
	service.release = releaseJson(VERSION, baseUrl, assetName, true, true);
	HttpServer::Handler handler = service.handler();

	HttpClient client;
	if(!HttpClient::compiled())
	{
		std::cout << "[payload] SKIP: this build carries no HTTP transport"
			<< std::endl;
		return 77;
	}

	// --- leg 1: the download that verifies and installs -------------------
	std::string installed;
	{
		const std::string root = scratch + "/state-ok";
		EditorPayloadFetcher fetcher(makeConfig(server, root));
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		report(!fetcher.isInstalled(PAYLOAD_ID),
			"a fresh installation carries no device player");
		const PayloadFetchStage stage =
			fetchOnce(server, handler, client, fetcher);
		report(stage == PayloadFetchStage::Installed,
			"one click fetches, verifies and installs it: " +
				fetcher.status().message);
		installed = fetcher.installedPath(PAYLOAD_ID);
		report(!installed.empty(), "and the export can ask where it landed");
		report(pathExists(installed + "/OrkigePlayer.app/Info.plist") &&
			pathExists(installed + "/Media/Hlms/marker.txt") &&
			pathExists(installed + "/orkige_payload.txt"),
			"the player, its engine media and the manifest are all there");
		// NEVER inside the application: the install root is the editor's own
		// writable state directory, which is what the config named
		report(installed.find(root) == 0,
			"installed under the editor's writable state, not inside the app");
		report(!pathExists(root + "/download/" + assetName),
			"the archive is removed once unpacked");
		report(!pathExists(root + "/unpack"),
			"and the unpacking scratch directory is gone");
	}

	// --- leg 2: a release that carries no such asset ----------------------
	{
		const std::string root = scratch + "/state-noasset";
		service.release = releaseJson(VERSION, baseUrl, assetName, false, true);
		EditorPayloadFetcher fetcher(makeConfig(server, root));
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		const PayloadFetchStage stage =
			fetchOnce(server, handler, client, fetcher);
		report(stage == PayloadFetchStage::Failed,
			"a release with no such player is refused");
		report(fetcher.status().message.find(assetName) != std::string::npos,
			"and the refusal names the asset it looked for");
		report(!fetcher.isInstalled(PAYLOAD_ID), "nothing is installed");
	}

	// --- leg 3: a download with no checksum published beside it -----------
	{
		const std::string root = scratch + "/state-nosum";
		service.release = releaseJson(VERSION, baseUrl, assetName, true, false);
		EditorPayloadFetcher fetcher(makeConfig(server, root));
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		const PayloadFetchStage stage =
			fetchOnce(server, handler, client, fetcher);
		report(stage == PayloadFetchStage::Failed,
			"a player with no published checksum is never taken on trust");
		report(fetcher.status().message.find("checksum") != std::string::npos,
			"and the refusal says why");
	}

	// --- leg 4: bytes that do not match their checksum --------------------
	{
		const std::string root = scratch + "/state-badsum";
		service.release = releaseJson(VERSION, baseUrl, assetName, true, true);
		const std::string good = service.sidecar;
		service.sidecar = std::string(64, 'a') + "  " + assetName + "\n";
		EditorPayloadFetcher fetcher(makeConfig(server, root));
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		const PayloadFetchStage stage =
			fetchOnce(server, handler, client, fetcher);
		report(stage == PayloadFetchStage::Failed,
			"bytes that do not match their checksum are discarded");
		report(fetcher.status().message.find("checksum") != std::string::npos,
			"and the refusal says why");
		report(!fetcher.isInstalled(PAYLOAD_ID),
			"nothing is installed from a bad download");
		report(!pathExists(root + "/unpack"),
			"and nothing half-unpacked is left behind");
		service.sidecar = good;
	}

	// --- leg 5: the pairing - a release published for ANOTHER build -------
	{
		const std::string root = scratch + "/state-otherbuild";
		service.release =
			releaseJson(OTHER_VERSION, baseUrl, assetName, true, true);
		EditorPayloadFetcher fetcher(makeConfig(server, root));
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		const PayloadFetchStage stage =
			fetchOnce(server, handler, client, fetcher);
		report(stage == PayloadFetchStage::Failed,
			"a player published for another build is refused");
		report(fetcher.status().message.find(OTHER_VERSION) !=
			std::string::npos, "and the refusal names both versions");
		report(!fetcher.isInstalled(PAYLOAD_ID), "nothing is installed");
		service.release = releaseJson(VERSION, baseUrl, assetName, true, true);
	}

	// --- leg 6: an archive that unpacks incomplete ------------------------
	{
		const std::string root = scratch + "/state-incomplete";
		const std::string brokenDir = scratch + "/compose-broken";
		writeWholeFile(brokenDir + "/orkige_payload.txt", "flavor: next\n");
		writeWholeFile(brokenDir + "/OrkigePlayer.app/Info.plist", "<plist/>\n");
		const std::string brokenArchive = scratch + "/serve-broken/" + assetName;
		std::filesystem::create_directories(
			std::filesystem::path(brokenArchive).parent_path(), code);
		if(packPayload(brokenDir, brokenArchive))
		{
			const std::string goodBytes = service.archiveBytes;
			const std::string goodSidecar = service.sidecar;
			service.archiveBytes = readWholeFile(brokenArchive);
			service.sidecar = Orkige::Sha256::hexDigest(
				service.archiveBytes.data(), service.archiveBytes.size()) +
				"  " + assetName + "\n";
			EditorPayloadFetcher fetcher(makeConfig(server, root));
			fetcher.setHttpClient(&client);
			fetcher.setProcessRunner(realRunner());
			const PayloadFetchStage stage =
				fetchOnce(server, handler, client, fetcher);
			report(stage == PayloadFetchStage::Failed,
				"a payload that unpacks without its engine media is refused");
			report(fetcher.status().message.find("Media") != std::string::npos,
				"and the refusal names what is missing");
			report(!fetcher.isInstalled(PAYLOAD_ID),
				"a half payload is never handed to an export");
			service.archiveBytes = goodBytes;
			service.sidecar = goodSidecar;
		}
	}

	// --- leg 7: the prune keeps only what this build needs ----------------
	{
		const std::string root = scratch + "/state-ok";
		// a superseded version and a platform the user switched off, both
		// beside the keeper leg 1 installed
		const std::string stale = OrkigeEditor::payloadInstallDirectory(root,
			PAYLOAD_ID, FLAVOR, "2.0.0-nightly.20260801+aaaaaaaaa");
		writeWholeFile(stale + "/orkige_payload.txt", "old\n");
		const std::string android = OrkigeEditor::payloadInstallDirectory(root,
			"player-android", FLAVOR, VERSION);
		writeWholeFile(android + "/orkige_payload.txt", "unwanted\n");

		EditorPayloadFetcher fetcher(makeConfig(server, root));
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		report(fetcher.installedPayloads().size() == 3,
			"three payload directories are on disk before the prune");
		std::vector<Orkige::String> enabled;
		enabled.push_back(PAYLOAD_ID);
		const int removed = fetcher.prune(enabled);
		report(removed == 2, "the prune removes the superseded and the "
			"switched-off copies");
		report(!pathExists(stale), "the superseded version is gone");
		report(!pathExists(android),
			"a platform nobody builds for keeps no download");
		report(pathExists(installed) && fetcher.isInstalled(PAYLOAD_ID),
			"and this build's own player survives it");
		// removing by hand gives the bytes back too (the settings checkbox)
		report(fetcher.remove(PAYLOAD_ID) && !fetcher.isInstalled(PAYLOAD_ID),
			"switching the platform off gives the download back");
	}

	// --- leg 8: the automated-run veto ------------------------------------
	{
		const std::string root = scratch + "/state-automated";
		EditorPayloadFetcher::Config config = makeConfig(server, root);
		config.automatedRun = true;
		EditorPayloadFetcher fetcher(config);
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		Orkige::String reason;
		report(!fetcher.canFetch(reason),
			"an automated run never fetches: " + reason);
		fetcher.beginFetch(PAYLOAD_ID);
		pumpUntil(server, handler, client, fetcher, [&client]()
		{
			return client.getPendingCount() == 0;
		}, 1000);
		report(fetcher.status().stage != PayloadFetchStage::Downloading &&
			fetcher.status().stage != PayloadFetchStage::Locating,
			"and never reaches the network");
		report(!pathExists(root), "and writes no user state at all");
	}

	// --- leg 9: an unstamped build pairs with nothing ---------------------
	{
		EditorPayloadFetcher::Config config =
			makeConfig(server, scratch + "/state-local");
		config.version = "";
		EditorPayloadFetcher fetcher(config);
		fetcher.setHttpClient(&client);
		fetcher.setProcessRunner(realRunner());
		Orkige::String reason;
		report(!fetcher.canFetch(reason),
			"a local build has no published release to pair with: " + reason);
		report(reason.find("source tree") != Orkige::String::npos,
			"and is told what to do instead");
	}

	server.stop();
	client.cancelAll();
	std::cout << "[payload] " << (gFailures == 0 ? "PASSED" : "FAILED")
		<< ": " << gFailures << " failing check(s)" << std::endl;
	return gFailures == 0 ? 0 : 1;
}
