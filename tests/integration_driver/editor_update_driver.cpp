/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	editor_update_driver.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// The whole update loop, headlessly, against a clean room.
//
// The tree's own HttpServer serves a fake release feed, a real archive built
// with the platform's own packing tool and a checksum sidecar on 127.0.0.1;
// the REAL EditorUpdater is pointed at it and driven with a scratch directory
// standing in for an installed editor. Everything a person would experience is
// asserted here: nothing newer, a downgrade refused, a notify-only check, a
// download that verifies and stages, a download whose digest does not match, a
// payload whose signature is rejected, the swap itself, the swap's rollback
// when the second move fails, the refusal to rearrange a build tree, and the
// automated-run veto.
//
// What is stubbed, and why: the signature VERDICT. The commands are the real
// ones (EditorUpdateTests asserts their argv), but a fixture directory cannot
// be Developer ID signed and notarized, so the injected process runner answers
// codesign/spctl with a scripted verdict while passing the unpacking straight
// through to the platform's real tool. That buys the leg a rejected signature
// deserves, which a run against an unsignable fixture could not otherwise
// have. Everything else - the request, the bytes, the digest, the unpacking,
// the moves - is real.
//
// Exit code is the contract: 0 = every leg passed.

#include <core_debugnet/HttpServer.h>
#include <core_filesystem/FileWriter.h>
#include <core_http/HttpClient.h>
#include <core_util/Sha256.h>

#include "EditorUpdate.h"
#include "EditorUpdater.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using OrkigeEditor::EditorUpdater;
using OrkigeEditor::UpdatePlatform;
using OrkigeEditor::UpdatePolicy;
using OrkigeEditor::UpdateStage;
using Orkige::HttpClient;
using Orkige::HttpRequest;
using Orkige::HttpResponse;
using Orkige::HttpServer;

namespace
{
	int gFailures = 0;

	void report(bool condition, std::string const& what)
	{
		std::cout << (condition ? "[update]   ok   " : "[update]  FAIL  ")
			<< what << std::endl;
		if (!condition)
		{
			++gFailures;
		}
	}

	void note(std::string const& what)
	{
		std::cout << "[update] " << what << std::endl;
	}

	std::string readWholeFile(std::string const& path)
	{
		std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
		std::ostringstream text;
		text << file.rdbuf();
		return text.str();
	}

	void writeWholeFile(std::string const& path, std::string const& bytes)
	{
		std::filesystem::create_directories(
			std::filesystem::path(path).parent_path());
		std::ofstream file(path.c_str(), std::ios::out | std::ios::binary);
		file << bytes;
	}

	bool pathExists(std::string const& path)
	{
		std::error_code code;
		return std::filesystem::exists(std::filesystem::path(path), code);
	}

	//! run a command, capture its combined output and exit code
	bool runCaptured(std::vector<std::string> const& argv, std::string& output,
		int& exitCode)
	{
		std::string command;
		for (std::size_t index = 0; index < argv.size(); ++index)
		{
			if (index > 0)
			{
				command += " ";
			}
			command += OrkigeEditor::shellQuotePosix(argv[index]);
		}
		command += " 2>&1";
		output.clear();
		FILE* pipe = popen(command.c_str(), "r");
		if (pipe == NULL)
		{
			return false;
		}
		char buffer[512];
		while (fgets(buffer, sizeof(buffer), pipe) != NULL)
		{
			output += buffer;
		}
		const int status = pclose(pipe);
		exitCode = (status == -1) ? -1 : (status / 256);
		return true;
	}

	//! the platform this driver's fixture archive is built for
	UpdatePlatform hostPlatform()
	{
		return OrkigeEditor::hostUpdatePlatform();
	}

	//! build a real archive out of @p topDirectory using the platform's own
	//! packing tool; returns "" when it could not be made
	std::string packArchive(std::string const& topDirectory,
		std::string const& archivePath)
	{
		std::vector<std::string> argv;
		const std::filesystem::path top(topDirectory);
		if (hostPlatform() == UpdatePlatform::MacOS)
		{
			// the same tool the published archive is made with, so the
			// unpacking side is exercised exactly as it will be in the field
			argv.push_back("/usr/bin/ditto");
			argv.push_back("-c");
			argv.push_back("-k");
			argv.push_back("--keepParent");
			argv.push_back(topDirectory);
			argv.push_back(archivePath);
		}
		else
		{
			argv.push_back("tar");
			argv.push_back("-czf");
			argv.push_back(archivePath);
			argv.push_back("-C");
			argv.push_back(top.parent_path().string());
			argv.push_back(top.filename().string());
		}
		std::string output;
		int exitCode = 0;
		if (!runCaptured(argv, output, exitCode) || exitCode != 0)
		{
			std::cout << "[update] could not pack the fixture archive: "
				<< output << std::endl;
			return std::string();
		}
		return archivePath;
	}

	//! the fixture's version identities
	const char* const CURRENT_VERSION = "2.0.0-nightly.20260730+dea551f9e";
	const char* const NEWER_VERSION = "2.0.0-nightly.20260731+abcdef123";
	const char* const OLDER_VERSION = "2.0.0-nightly.20260729+000000abc";

	//! the release feed the fake service answers with
	std::string composeFeed(std::string const& version,
		std::string const& baseUrl, std::string const& archiveName,
		bool includeAssets)
	{
		std::string body =
			"## Downloads\\n\\n"
			"## Changes since `0123456789`\\n\\n"
			"- `abcdef123` The thing this fixture pretends landed\\n\\n"
			"<!-- orkige-nightly-commit: abcdef123 -->\\n"
			"<!-- orkige-nightly-version: " + version + " -->\\n";
		std::string json = "{\"tag_name\":\"nightly\",\"body\":\"" + body +
			"\",\"assets\":[";
		if (includeAssets)
		{
			json += "{\"name\":\"" + archiveName + "\",\"size\":0,"
				"\"browser_download_url\":\"" + baseUrl + "/" + archiveName +
				"\"},";
			json += "{\"name\":\"" + archiveName + ".sha256\",\"size\":0,"
				"\"browser_download_url\":\"" + baseUrl + "/" + archiveName +
				".sha256\"}";
		}
		json += "]}";
		return json;
	}

	//! the fake release service: one feed and two files, all in memory
	struct FakeService
	{
		std::string				feed;
		std::string				archiveName;
		std::string				archiveBytes;
		std::string				sidecar;
		int						feedStatus = 200;

		HttpServer::Handler handler()
		{
			return [this](HttpRequest const& request)
			{
				HttpResponse response;
				if (request.target == "/release")
				{
					response.status = this->feedStatus;
					response.contentType = "application/json";
					response.body = this->feed;
					return response;
				}
				if (request.target == "/" + this->archiveName)
				{
					response.contentType = "application/octet-stream";
					response.body = this->archiveBytes;
					return response;
				}
				if (request.target == "/" + this->archiveName + ".sha256")
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

	//! the process runner the updater is given: the real tool for unpacking,
	//! a scripted verdict for the signature (@see the file comment)
	struct StubbedRunner
	{
		bool	signatureAccepted = true;
		int		signatureCalls = 0;

		EditorUpdater::ProcessRunner runner()
		{
			return [this](std::vector<std::string> const& argv,
				std::string& output, int& exitCode) -> bool
			{
				if (!argv.empty() &&
					(argv[0].find("codesign") != std::string::npos ||
					 argv[0].find("spctl") != std::string::npos))
				{
					++this->signatureCalls;
					output = this->signatureAccepted
						? "valid on disk" : "code object is not signed at all";
					exitCode = this->signatureAccepted ? 0 : 1;
					return true;
				}
				return runCaptured(argv, output, exitCode);
			};
		}
	};

	//! pump the server, the client and the updater until the predicate holds
	template <typename Predicate>
	bool pumpUntil(HttpServer& server, HttpServer::Handler const& handler,
		HttpClient& client, EditorUpdater& updater, Predicate predicate,
		int timeoutMs = 60000)
	{
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMs);
		while (std::chrono::steady_clock::now() < deadline)
		{
			server.update(handler);
			client.update();
			updater.update();
			if (predicate())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		server.update(handler);
		client.update();
		updater.update();
		return predicate();
	}

	//! the fixture an "installed" editor and a "new version" are made of
	struct Fixture
	{
		std::string scratch;
		std::string installPath;	//!< what a swap would replace
		std::string archivePath;
		std::string archiveName;
		std::string payloadTop;		//!< the archive's one top-level directory

		//! the marker file inside a payload, so a swap is provable
		static std::string markerName() { return "VERSION"; }
	};

	//! build the fixture: an installed copy carrying one marker, and a packed
	//! archive whose payload carries another
	bool buildFixture(Fixture& fixture, std::string const& scratchRoot)
	{
		fixture.scratch = scratchRoot;
		std::error_code code;
		std::filesystem::remove_all(std::filesystem::path(scratchRoot), code);
		std::filesystem::create_directories(
			std::filesystem::path(scratchRoot), code);

		const bool mac = hostPlatform() == UpdatePlatform::MacOS;
		const std::string payloadName =
			OrkigeEditor::updatePayloadName(hostPlatform());

		// the "installed" editor
		fixture.installPath = scratchRoot + "/install/" + payloadName;
		writeWholeFile(fixture.installPath + "/" + Fixture::markerName(),
			std::string("version: ") + CURRENT_VERSION + "\n");

		// the new version, in the archive layout the publishing side produces:
		// ONE top-level directory holding the app (macOS) or the whole tree
		fixture.archiveName =
			OrkigeEditor::updateArchiveName(hostPlatform(), NEWER_VERSION);
		if (fixture.archiveName.empty())
		{
			return false;
		}
		const std::string topName = fixture.archiveName.substr(0,
			fixture.archiveName.find_first_of('.'));
		fixture.payloadTop = scratchRoot + "/pack/" + topName;
		const std::string payloadPath = mac
			? (fixture.payloadTop + "/" + payloadName) : fixture.payloadTop;
		writeWholeFile(payloadPath + "/" + Fixture::markerName(),
			std::string("version: ") + NEWER_VERSION + "\n");
		if (!mac)
		{
			// the non-macOS layout keeps the text files at the top too
			writeWholeFile(fixture.payloadTop + "/CHANGELOG.md", "# nothing\n");
		}
		fixture.archivePath = scratchRoot + "/serve/" + fixture.archiveName;
		std::filesystem::create_directories(
			std::filesystem::path(fixture.archivePath).parent_path(), code);
		return !packArchive(fixture.payloadTop, fixture.archivePath).empty();
	}

	//! the version marker inside a payload directory
	std::string markerOf(std::string const& payloadPath)
	{
		return readWholeFile(payloadPath + "/" + Fixture::markerName());
	}

	//! a fresh updater configured against the fake service
	EditorUpdater::Config makeConfig(Fixture const& fixture,
		HttpServer const& server, std::string const& workDirectory)
	{
		EditorUpdater::Config config;
		config.feedUrl = "http://127.0.0.1:" +
			std::to_string(server.getPort()) + "/release";
		config.currentVersion = CURRENT_VERSION;
		config.installPath = fixture.installPath;
		config.workDirectory = workDirectory;
		config.platform = hostPlatform();
		config.allowInsecureHttp = true;	// loopback, no certificate to have
		config.assumeUpdatableLocation = true;
		config.pid = 0;						// the helper waits for nobody here
		return config;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cout << "usage: editor_update_driver <scratch dir>" << std::endl;
		return 2;
	}
	const std::string scratchRoot = argv[1];

	Fixture fixture;
	if (!buildFixture(fixture, scratchRoot))
	{
		std::cout << "[update] SKIP: the fixture archive could not be packed "
			"on this machine" << std::endl;
		return 77;
	}
	note("fixture ready: " + fixture.archiveName);

	HttpServer server;
	if (!server.start(0))
	{
		std::cout << "[update] SKIP: no loopback port available" << std::endl;
		return 77;
	}
	const std::string baseUrl =
		"http://127.0.0.1:" + std::to_string(server.getPort());

	FakeService service;
	service.archiveName = fixture.archiveName;
	service.archiveBytes = readWholeFile(fixture.archivePath);
	const Orkige::String realDigest = Orkige::Sha256::hexDigest(
		service.archiveBytes.data(), service.archiveBytes.size());
	service.sidecar = realDigest + "  " + fixture.archiveName + "\n";
	HttpServer::Handler handler = service.handler();

	HttpClient client;
	if (!HttpClient::compiled())
	{
		std::cout << "[update] SKIP: this build carries no HTTP transport"
			<< std::endl;
		return 77;
	}

	// --- leg 1: nothing newer -------------------------------------------
	{
		service.feed = composeFeed(CURRENT_VERSION, baseUrl,
			fixture.archiveName, true);
		EditorUpdater updater(makeConfig(fixture, server, scratchRoot +
			"/work-uptodate"));
		StubbedRunner stub;
		updater.setHttpClient(&client);
		updater.setProcessRunner(stub.runner());
		updater.setPolicy(UpdatePolicy::Download);
		updater.requestManualCheck();
		pumpUntil(server, handler, client, updater, [&updater]()
		{
			return updater.status().stage != UpdateStage::Checking;
		});
		report(updater.status().stage == UpdateStage::UpToDate,
			"a rebuild of the same day is not an update");
		report(updater.status().message ==
			"You are on the latest version.",
			"and the answer says exactly that");
	}

	// --- leg 2: a downgrade is refused -----------------------------------
	{
		service.feed = composeFeed(OLDER_VERSION, baseUrl,
			fixture.archiveName, true);
		EditorUpdater updater(makeConfig(fixture, server, scratchRoot +
			"/work-older"));
		StubbedRunner stub;
		updater.setHttpClient(&client);
		updater.setProcessRunner(stub.runner());
		updater.setPolicy(UpdatePolicy::Download);
		updater.requestManualCheck();
		pumpUntil(server, handler, client, updater, [&updater]()
		{
			return updater.status().stage != UpdateStage::Checking;
		});
		report(updater.status().stage == UpdateStage::Failed,
			"an older published build is never offered");
		report(!updater.isReadyToInstall(),
			"and nothing is staged from it");
		report(!pathExists(scratchRoot + "/work-older/staged"),
			"a downgrade downloads nothing at all");
	}

	// --- leg 3: notify only ----------------------------------------------
	{
		service.feed = composeFeed(NEWER_VERSION, baseUrl,
			fixture.archiveName, true);
		EditorUpdater updater(makeConfig(fixture, server, scratchRoot +
			"/work-notify"));
		StubbedRunner stub;
		updater.setHttpClient(&client);
		updater.setProcessRunner(stub.runner());
		updater.setPolicy(UpdatePolicy::Notify);
		updater.requestManualCheck();
		pumpUntil(server, handler, client, updater, [&updater]()
		{
			return updater.status().stage != UpdateStage::Checking;
		});
		report(updater.status().stage == UpdateStage::Available,
			"Notify says a newer version exists");
		report(updater.status().version == NEWER_VERSION,
			"and names it");
		report(updater.status().changelog.find("## Changes since") == 0,
			"and carries the release's own changelog section");
		report(!pathExists(scratchRoot + "/work-notify/download"),
			"Notify downloads nothing");
	}

	// --- leg 4: a checksum that does not match ---------------------------
	{
		service.feed = composeFeed(NEWER_VERSION, baseUrl,
			fixture.archiveName, true);
		const std::string good = service.sidecar;
		service.sidecar = std::string(64, 'a') + "  " + fixture.archiveName +
			"\n";
		const std::string work = scratchRoot + "/work-badsum";
		EditorUpdater updater(makeConfig(fixture, server, work));
		StubbedRunner stub;
		updater.setHttpClient(&client);
		updater.setProcessRunner(stub.runner());
		updater.setPolicy(UpdatePolicy::Download);
		updater.requestManualCheck();
		pumpUntil(server, handler, client, updater, [&updater]()
		{
			const UpdateStage stage = updater.status().stage;
			return stage == UpdateStage::Failed ||
				stage == UpdateStage::Ready;
		});
		report(updater.status().stage == UpdateStage::Failed,
			"bytes that do not match their checksum are refused");
		report(updater.status().message.find("checksum") !=
			std::string::npos, "and the refusal says why");
		report(!updater.isReadyToInstall(),
			"nothing is staged from a bad download");
		report(!pathExists(work + "/staged"),
			"and the unpacking directory is cleaned up");
		report(markerOf(fixture.installPath).find(CURRENT_VERSION) !=
			std::string::npos,
			"the installed editor was not touched");
		service.sidecar = good;
	}

	// --- leg 5: a signature the system rejects ---------------------------
	{
		service.feed = composeFeed(NEWER_VERSION, baseUrl,
			fixture.archiveName, true);
		const std::string work = scratchRoot + "/work-badsig";
		EditorUpdater updater(makeConfig(fixture, server, work));
		StubbedRunner stub;
		stub.signatureAccepted = false;
		updater.setHttpClient(&client);
		updater.setProcessRunner(stub.runner());
		updater.setPolicy(UpdatePolicy::Download);
		updater.requestManualCheck();
		pumpUntil(server, handler, client, updater, [&updater]()
		{
			const UpdateStage stage = updater.status().stage;
			return stage == UpdateStage::Failed ||
				stage == UpdateStage::Ready;
		});
		if (hostPlatform() == UpdatePlatform::MacOS)
		{
			report(updater.status().stage == UpdateStage::Failed,
				"a payload whose signature is rejected is discarded");
			report(stub.signatureCalls > 0,
				"and the signature really was asked about");
			report(!pathExists(work + "/staged"),
				"the rejected payload is removed");
		}
		else
		{
			// no signature exists on this platform's published builds; the
			// digest is the whole check, and the run says so rather than
			// pretending otherwise
			report(updater.status().stage == UpdateStage::Ready,
				"a platform with no signature stages on the digest alone");
			report(stub.signatureCalls == 0,
				"and no signature tool is invoked there");
		}
	}

	// --- leg 6: download, verify, stage, swap ----------------------------
	std::vector<std::string> helperCommand;
	{
		service.feed = composeFeed(NEWER_VERSION, baseUrl,
			fixture.archiveName, true);
		const std::string work = scratchRoot + "/work-install";
		EditorUpdater updater(makeConfig(fixture, server, work));
		StubbedRunner stub;
		updater.setHttpClient(&client);
		updater.setProcessRunner(stub.runner());
		updater.setDetachedSpawn(
			[&helperCommand](std::vector<std::string> const& command)
			{
				// the helper must outlive the editor, so the real one is
				// launched detached; here it is recorded and run below, which
				// is what lets the swap be asserted
				helperCommand = command;
				return true;
			});
		updater.setPolicy(UpdatePolicy::Download);
		updater.requestManualCheck();
		const bool finished = pumpUntil(server, handler, client, updater,
			[&updater]()
			{
				const UpdateStage stage = updater.status().stage;
				return stage == UpdateStage::Failed ||
					stage == UpdateStage::Ready;
			});
		report(finished && updater.status().stage == UpdateStage::Ready,
			"a good download verifies and stages: " +
				updater.status().message);
		report(updater.isReadyToInstall(),
			"and reports itself ready to install on restart");
		report(updater.readyVersion() == NEWER_VERSION,
			"under the version that was published");
		report(!pathExists(work + "/download/" + fixture.archiveName),
			"the archive is removed once unpacked");

		// nothing has happened to the installed copy yet - the whole point
		report(markerOf(fixture.installPath).find(CURRENT_VERSION) !=
			std::string::npos,
			"the installed editor is untouched until the swap runs");

		// a build tree is never rearranged, even with a verified payload
		{
			EditorUpdater::Config strict = makeConfig(fixture, server, work);
			strict.assumeUpdatableLocation = false;
			// what a developer build reports about itself: its resources came
			// out of a tree, not out of an app somebody copied
			strict.builtFromTree = true;
			EditorUpdater guard(strict);
			guard.setProcessRunner(stub.runner());
			guard.setDetachedSpawn(
				[](std::vector<std::string> const&) { return true; });
			guard.loadState();
			std::string error;
			const bool installed = guard.installOnExit(false, error);
			report(!installed && error.find("built from source") !=
				std::string::npos,
				"an install inside a build tree is refused, with a reason: " +
					error);
		}

		std::string error;
		const bool handedOver = updater.installOnExit(false, error);
		report(handedOver, "the swap is handed to a helper process: " + error);
		report(!helperCommand.empty(), "and the helper was launched");
	}

	// --- run the helper and assert the swap -------------------------------
	if (!helperCommand.empty())
	{
		std::string output;
		int exitCode = -1;
		runCaptured(helperCommand, output, exitCode);
		report(exitCode == 0, "the helper completed: " + output);
		report(markerOf(fixture.installPath).find(NEWER_VERSION) !=
			std::string::npos,
			"the installed editor IS the new version after the swap");
		bool leftovers = false;
		std::error_code code;
		const std::filesystem::path container =
			std::filesystem::path(fixture.installPath).parent_path();
		for (std::filesystem::directory_iterator entry(container, code);
			!code && entry != std::filesystem::directory_iterator(); ++entry)
		{
			if (entry->path().string().find(".orkige-previous-") !=
				std::string::npos)
			{
				leftovers = true;
			}
		}
		report(!leftovers, "and the previous copy was cleaned up");
		report(!pathExists(helperCommand.back()),
			"the helper removed itself");
	}

	// --- the rollback: the second move fails ------------------------------
	{
		// put an "installed" copy back and stage a new one beside it, then run
		// a helper whose move-in is made to fail. A half-swapped install is
		// the one outcome that must be impossible.
		const std::string root = scratchRoot + "/rollback";
		std::error_code code;
		std::filesystem::remove_all(std::filesystem::path(root), code);
		const std::string installed = root + "/install/Payload";
		const std::string staged = root + "/staged/Payload";
		writeWholeFile(installed + "/" + Fixture::markerName(), "old\n");
		writeWholeFile(staged + "/" + Fixture::markerName(), "new\n");

		// a shim `mv` that fails ONLY the move-in (neither endpoint is the
		// backup); the move-aside and the rollback both go through
		const std::string shimDir = root + "/shim";
		const std::string shim = shimDir + "/mv";
		writeWholeFile(shim,
			"#!/bin/sh\n"
			"case \"$1\" in *.orkige-previous-*) exec /bin/mv \"$@\";; esac\n"
			"case \"$2\" in *.orkige-previous-*) exec /bin/mv \"$@\";; esac\n"
			"echo 'simulated failure' >&2\n"
			"exit 1\n");
		std::filesystem::permissions(std::filesystem::path(shim),
			std::filesystem::perms::owner_all, code);

		OrkigeEditor::UpdateHelperSpec spec;
		spec.plan = OrkigeEditor::planUpdateSwap(installed, staged, "2.0.0");
		spec.pid = 0;
		spec.platform = UpdatePlatform::Linux;	// the POSIX helper, either way
		const std::string script =
			OrkigeEditor::composeUpdateHelperScript(spec);
		const std::string scriptPath = root + "/helper.sh";
		writeWholeFile(scriptPath, script);

		const std::string command = "PATH=" +
			OrkigeEditor::shellQuotePosix(shimDir) + ":$PATH /bin/sh " +
			OrkigeEditor::shellQuotePosix(scriptPath) + " 2>&1";
		FILE* pipe = popen(command.c_str(), "r");
		std::string output;
		if (pipe != NULL)
		{
			char buffer[256];
			while (fgets(buffer, sizeof(buffer), pipe) != NULL)
			{
				output += buffer;
			}
			const int status = pclose(pipe);
			const int exitCode = (status == -1) ? -1 : (status / 256);
			report(exitCode == 5,
				"a failed move-in exits with the rollback code");
		}
		report(readWholeFile(installed + "/" + Fixture::markerName()) ==
			"old\n",
			"the previous version is back exactly where it was");
		report(pathExists(staged + "/" + Fixture::markerName()),
			"and the staged copy is still there to retry from");
		bool leftovers = false;
		for (std::filesystem::directory_iterator entry(
				std::filesystem::path(root + "/install"), code);
			!code && entry != std::filesystem::directory_iterator(); ++entry)
		{
			if (entry->path().string().find(".orkige-previous-") !=
				std::string::npos)
			{
				leftovers = true;
			}
		}
		report(!leftovers, "no half-swapped state is left behind");
	}

	// --- the automated-run veto -------------------------------------------
	{
		const std::string work = scratchRoot + "/work-automated";
		EditorUpdater::Config config = makeConfig(fixture, server, work);
		config.automatedRun = true;
		EditorUpdater updater(config);
		StubbedRunner stub;
		updater.setHttpClient(&client);
		updater.setProcessRunner(stub.runner());
		updater.setPolicy(UpdatePolicy::Download);
		updater.loadState();
		updater.tickAutomaticCheck();
		updater.requestManualCheck();		// even asked for, it must not run
		pumpUntil(server, handler, client, updater, [&client]()
		{
			return client.getPendingCount() == 0;
		}, 1000);
		report(updater.status().stage != UpdateStage::Checking &&
			updater.status().stage != UpdateStage::Downloading,
			"an automated run never reaches the network");
		report(!pathExists(work),
			"and writes no user state at all");
		std::string error;
		report(!updater.installOnExit(false, error),
			"and never installs anything");
	}

	server.stop();
	client.cancelAll();
	std::cout << "[update] " << (gFailures == 0 ? "PASSED" : "FAILED")
		<< ": " << gFailures << " failing check(s)" << std::endl;
	return gFailures == 0 ? 0 : 1;
}
