/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpClientTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The HTTP client end to end, with no network: the tree's own HttpServer
// answers on 127.0.0.1, so status codes, headers, bodies, POST payloads, large
// transfers, the progress callback, the size cap, timeouts, cancellation,
// save-to-file and a refused connection are all deterministic and offline.
// TLS itself cannot be exercised against a loopback plain server - the opt-in
// network test (tests/http_network) is what proves certificate verification.

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/HttpServer.h>
#include <core_filesystem/FileWriter.h>
#include <core_http/HttpClient.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using Orkige::HttpClient;
using Orkige::HttpClientRequest;
using Orkige::HttpClientResponse;
using Orkige::HttpRequest;
using Orkige::HttpRequestId;
using Orkige::HttpResponse;
using Orkige::HttpServer;
using Orkige::String;

namespace
{
	//! @brief what one submitted request recorded: the single completion plus
	//! every progress step - the two contracts under test
	struct Recorder
	{
		int						completions = 0;
		HttpClientResponse		response;
		std::vector<std::pair<unsigned long long, unsigned long long> > progress;

		Orkige::HttpCompleteCallback onComplete()
		{
			return [this](HttpClientResponse const & answer)
			{
				++this->completions;
				this->response = answer;
			};
		}
		Orkige::HttpProgressCallback onProgress()
		{
			return [this](unsigned long long received, unsigned long long total)
			{
				this->progress.push_back(std::make_pair(received, total));
			};
		}
	};
	//! @brief pump BOTH ends until the predicate holds or the deadline passes.
	//! The server has no thread of its own (it must be pumped, like the editor
	//! pumps it per frame) and the client only ever delivers from update() - so
	//! this loop IS the frame boundary the contract talks about.
	template <typename Predicate>
	bool pumpUntil(HttpServer & server, HttpServer::Handler const & handler,
		HttpClient & client, Predicate predicate, int timeoutMs = 10000)
	{
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMs);
		while (std::chrono::steady_clock::now() < deadline)
		{
			server.update(handler);
			client.update();
			if (predicate())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		// one last drain so a result that landed on the deadline is not lost
		server.update(handler);
		client.update();
		return predicate();
	}
	//! the loopback URL of a running server
	String loopbackUrl(HttpServer const & server, String const & path)
	{
		return "http://127.0.0.1:" + std::to_string(server.getPort()) + path;
	}
	//! a plain-http request (the loopback opt-in every case below needs)
	HttpClientRequest localRequest(HttpServer const & server,
		String const & path)
	{
		HttpClientRequest request;
		request.url = loopbackUrl(server, path);
		request.allowInsecureHttp = true;
		request.timeoutMs = 8000;
		return request;
	}
	//! a body of `size` bytes with recognisable content
	String filler(std::size_t size)
	{
		String body;
		body.reserve(size);
		while (body.size() < size)
		{
			body += static_cast<char>('a' + (body.size() % 26));
		}
		return body;
	}
	//! a scratch path under the system temp dir, removed on destruction
	struct ScratchFile
	{
		std::filesystem::path path;
		explicit ScratchFile(char const * name)
		{
			this->path = std::filesystem::temp_directory_path() /
				("orkige_http_test" + std::to_string(
					static_cast<unsigned long long>(
						std::chrono::steady_clock::now()
							.time_since_epoch().count()))) / name;
		}
		~ScratchFile()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->path.parent_path(), ignored);
		}
		String string() const { return this->path.string(); }
		//! the file's bytes ("" when it does not exist)
		String read() const
		{
			std::ifstream file(this->path, std::ios::binary);
			if (!file)
			{
				return String();
			}
			return String((std::istreambuf_iterator<char>(file)),
				std::istreambuf_iterator<char>());
		}
		bool exists() const
		{
			std::error_code ignored;
			return std::filesystem::exists(this->path, ignored);
		}
	};
}

TEST_CASE("HttpClient GETs a loopback response with headers and progress",
	"[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	REQUIRE(client.available());

	std::vector<HttpRequest> seen;
	HttpServer::Handler handler = [&seen](HttpRequest const & request)
	{
		seen.push_back(request);
		HttpResponse response;
		response.status = 200;
		response.contentType = "application/json";
		response.body = "{\"score\":42}";
		response.extraHeaders.push_back(
			std::make_pair(String("X-Server-Note"), String("hello")));
		return response;
	};

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/scores?top=10");
	request.headers.push_back(std::make_pair(String("Authorization"),
		String("Bearer test-token")));
	const HttpRequestId id = client.submit(request, recorder.onComplete(),
		recorder.onProgress());
	REQUIRE(id != 0);

	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));

	// exactly ONE completion, ever
	CHECK(recorder.completions == 1);
	CHECK(recorder.response.id == id);
	CHECK(recorder.response.completed);
	CHECK(recorder.response.ok());
	CHECK(recorder.response.status == 200);
	CHECK(recorder.response.body == "{\"score\":42}");
	CHECK(recorder.response.bytes == 12);
	CHECK(recorder.response.failure == Orkige::HF_NONE);
	CHECK(recorder.response.reason.empty());
	// response header names come back lower-cased, whatever the server sent
	CHECK(recorder.response.header("content-type") == "application/json");
	CHECK(recorder.response.header("x-server-note") == "hello");
	CHECK(recorder.response.header("absent-header").empty());
	// the request reached the server intact, headers and all
	REQUIRE(seen.size() == 1);
	CHECK(seen[0].method == "GET");
	CHECK(seen[0].target == "/scores?top=10");
	CHECK(seen[0].header("authorization") == "Bearer test-token");
	// the client identifies the engine unless the caller says otherwise
	CHECK(seen[0].header("user-agent").find("orkige") != String::npos);
	// progress fired, and its last step matches the delivered size
	REQUIRE_FALSE(recorder.progress.empty());
	CHECK(recorder.progress.back().first == 12);
	CHECK(recorder.progress.back().second == 12);
	// nothing is left owing
	CHECK(client.getPendingCount() == 0);
	CHECK(client.getCompletedCount() == 1);
	server.stop();
}

TEST_CASE("HttpClient POSTs a body with its content type", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	std::vector<HttpRequest> seen;
	HttpServer::Handler handler = [&seen](HttpRequest const & request)
	{
		seen.push_back(request);
		HttpResponse response;
		response.status = 201;
		response.body = "created";
		return response;
	};

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/scores");
	request.method = "POST";
	request.body = "{\"name\":\"ada\",\"score\":7}";
	request.contentType = "application/json";
	client.submit(request, recorder.onComplete());

	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));
	CHECK(recorder.response.status == 201);
	CHECK(recorder.response.body == "created");
	REQUIRE(seen.size() == 1);
	CHECK(seen[0].method == "POST");
	CHECK(seen[0].body == "{\"name\":\"ada\",\"score\":7}");
	CHECK(seen[0].header("content-type") == "application/json");
	server.stop();
}

TEST_CASE("HttpClient reports an HTTP error status as a completed exchange",
	"[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	HttpServer::Handler handler = [](HttpRequest const &)
	{
		HttpResponse response;
		response.status = 404;
		response.reason = "Not Found";
		response.body = "{\"error\":\"no such score\"}";
		return response;
	};

	Recorder recorder;
	client.submit(localRequest(server, "/missing"), recorder.onComplete());
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));

	// a 404 is an ANSWER: the exchange completed, the status carries the verdict
	CHECK(recorder.response.completed);
	CHECK_FALSE(recorder.response.ok());
	CHECK(recorder.response.status == 404);
	CHECK(recorder.response.failure == Orkige::HF_NONE);
	CHECK(recorder.response.body == "{\"error\":\"no such score\"}");
	server.stop();
}

TEST_CASE("HttpClient delivers refusals through the completion callback",
	"[http][security]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	int served = 0;
	HttpServer::Handler handler = [&served](HttpRequest const &)
	{
		++served;
		return HttpResponse();
	};

	// a plain-http URL WITHOUT the opt-in is refused before any socket work
	Recorder insecure;
	HttpClientRequest plain;
	plain.url = loopbackUrl(server, "/health");
	const HttpRequestId insecureId =
		client.submit(plain, insecure.onComplete());
	CHECK(insecureId != 0);				// still a handle: ONE error path
	CHECK(insecure.completions == 0);	// and nothing before update()
	REQUIRE(pumpUntil(server, handler, client,
		[&insecure]() { return insecure.completions > 0; }, 2000));
	CHECK(insecure.completions == 1);
	CHECK_FALSE(insecure.response.completed);
	CHECK(insecure.response.failure == Orkige::HF_INSECURE_SCHEME);
	CHECK_FALSE(insecure.response.reason.empty());

	// the same for a malformed URL, a bad header and URL credentials
	Recorder malformed;
	HttpClientRequest bad;
	bad.url = "https://";
	client.submit(bad, malformed.onComplete());

	Recorder injected;
	HttpClientRequest header = localRequest(server, "/x");
	header.headers.push_back(std::make_pair(String("X-Evil"),
		String("a\r\nX-Admin: 1")));
	client.submit(header, injected.onComplete());

	Recorder credentials;
	HttpClientRequest inline_creds;
	inline_creds.url = "https://user:secret@example.com/x";
	client.submit(inline_creds, credentials.onComplete());

	REQUIRE(pumpUntil(server, handler, client, [&]()
	{
		return malformed.completions > 0 && injected.completions > 0 &&
			credentials.completions > 0;
	}, 2000));
	CHECK(malformed.response.failure == Orkige::HF_BAD_URL);
	CHECK(injected.response.failure == Orkige::HF_BAD_HEADER);
	CHECK(credentials.response.failure == Orkige::HF_CREDENTIALS_IN_URL);
	// not one of the four ever reached the server
	CHECK(served == 0);
	server.stop();
}

TEST_CASE("HttpClient holds every answer until update() runs", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	HttpServer::Handler handler = [](HttpRequest const &)
	{
		HttpResponse response;
		response.body = "done";
		return response;
	};

	Recorder recorder;
	client.submit(localRequest(server, "/x"), recorder.onComplete(),
		recorder.onProgress());
	// pump ONLY the server: the transfer runs to completion off the main
	// thread, and the callback still must not have fired
	const std::chrono::steady_clock::time_point until =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
	while (std::chrono::steady_clock::now() < until)
	{
		server.update(handler);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(recorder.completions == 0);
	CHECK(recorder.progress.empty());
	CHECK(client.getPendingCount() == 1);

	// the frame boundary is the ONLY place a callback runs
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));
	CHECK(recorder.response.body == "done");
	server.stop();
}

TEST_CASE("HttpClient reports progress over a large transfer", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	const String body = filler(2 * 1024 * 1024);
	HttpServer::Handler handler = [&body](HttpRequest const &)
	{
		HttpResponse response;
		response.contentType = "application/octet-stream";
		response.body = body;
		return response;
	};

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/big");
	request.maxResponseBytes = 8 * 1024 * 1024;
	client.submit(request, recorder.onComplete(), recorder.onProgress());
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }, 20000));

	CHECK(recorder.response.completed);
	CHECK(recorder.response.body.size() == body.size());
	CHECK(recorder.response.body == body);
	CHECK(recorder.response.bytes == body.size());
	// the announced total is known from the very first step, and progress is
	// monotonic and ends at the full size. The COUNT is deliberately not
	// asserted: steps coalesce per drain by design, so a frame that drains
	// slowly (a loaded test machine) legitimately sees fewer of them - what a
	// progress bar needs is the latest number, not every number.
	REQUIRE_FALSE(recorder.progress.empty());
	CHECK(recorder.progress.front().second == body.size());
	CHECK(recorder.progress.back().first == body.size());
	for (std::size_t at = 1; at < recorder.progress.size(); ++at)
	{
		CHECK(recorder.progress[at].first >= recorder.progress[at - 1].first);
	}
	server.stop();
}

TEST_CASE("HttpClient refuses a response over its size cap", "[http][security]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	const String body = filler(256 * 1024);
	HttpServer::Handler handler = [&body](HttpRequest const &)
	{
		HttpResponse response;
		response.body = body;
		return response;
	};

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/big");
	request.maxResponseBytes = 4096;
	client.submit(request, recorder.onComplete());
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));

	CHECK(recorder.completions == 1);
	CHECK_FALSE(recorder.response.completed);
	CHECK(recorder.response.failure == Orkige::HF_TOO_LARGE);
	CHECK(recorder.response.reason.find("cap") != String::npos);
	server.stop();
}

TEST_CASE("HttpClient times out a server that never answers", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	// the server is LISTENING (so the connection completes) but never pumped,
	// so the request is accepted by the kernel and never answered
	HttpServer::Handler handler = [](HttpRequest const &)
	{
		return HttpResponse();
	};

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/silent");
	request.timeoutMs = 700;
	client.submit(request, recorder.onComplete());

	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while (recorder.completions == 0 &&
		std::chrono::steady_clock::now() < deadline)
	{
		client.update();	// the server is deliberately NOT pumped
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(recorder.completions == 1);
	CHECK_FALSE(recorder.response.completed);
	CHECK(recorder.response.failure == Orkige::HF_TIMEOUT);
	CHECK_FALSE(recorder.response.reason.empty());
	server.stop();
	(void)handler;
}

TEST_CASE("HttpClient reports a refused connection honestly", "[http]")
{
	// take an ephemeral port, then give it back - nothing listens there
	unsigned short port = 0;
	{
		HttpServer probe;
		REQUIRE(probe.start(0));
		port = probe.getPort();
		probe.stop();
	}
	REQUIRE(port != 0);

	HttpClient client;
	Recorder recorder;
	HttpClientRequest request;
	request.url = "http://127.0.0.1:" + std::to_string(port) + "/gone";
	request.allowInsecureHttp = true;
	request.timeoutMs = 5000;
	client.submit(request, recorder.onComplete());

	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while (recorder.completions == 0 &&
		std::chrono::steady_clock::now() < deadline)
	{
		client.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	REQUIRE(recorder.completions == 1);
	CHECK_FALSE(recorder.response.completed);
	CHECK(recorder.response.failure == Orkige::HF_CONNECT_FAILED);
	CHECK_FALSE(recorder.response.reason.empty());
}

TEST_CASE("HttpClient cancels a transfer and still answers exactly once",
	"[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	// The server LISTENS but is never pumped, so the request is accepted by the
	// kernel and left unanswered - the one transfer state that is reliably
	// in-flight on every transport. Cancelling mid-BODY is not a test a
	// stopwatch can win: a transport that hands the body over in few large
	// reads (WinHTTP does) delivers the first progress step and the completion
	// within one drain, so "it has started" and "it has finished" are the same
	// moment and the cancel is racing an answer that already exists.
	HttpServer::Handler handler = [](HttpRequest const &)
	{
		return HttpResponse();
	};

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/silent");
	request.timeoutMs = 30000;		// far beyond the test: the cancel ends it
	const HttpRequestId id = client.submit(request, recorder.onComplete(),
		recorder.onProgress());

	// let the transfer reach the wire (the server is deliberately NOT pumped)
	for (int round = 0; round < 50 && client.getPendingCount() == 0; ++round)
	{
		client.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(client.getPendingCount() == 1);
	REQUIRE(recorder.completions == 0);
	CHECK(client.cancel(id));
	CHECK_FALSE(client.cancel(id));		// a second cancel is not a new answer

	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));
	CHECK(recorder.completions == 1);
	CHECK_FALSE(recorder.response.completed);
	CHECK(recorder.response.failure == Orkige::HF_CANCELLED);
	CHECK(client.getPendingCount() == 0);

	// the transport's own completion, arriving after the cancel, is dropped -
	// the caller was already answered
	pumpUntil(server, handler, client, []() { return false; }, 300);
	CHECK(recorder.completions == 1);
	server.stop();
}

TEST_CASE("HttpClient retires an owner's requests without calling back",
	"[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	HttpServer::Handler handler = [](HttpRequest const &)
	{
		HttpResponse response;
		response.body = "x";
		return response;
	};

	int owner = 0;
	Recorder owned;
	Recorder other;
	client.submit(localRequest(server, "/a"), owned.onComplete(),
		Orkige::HttpProgressCallback(), &owner);
	client.submit(localRequest(server, "/b"), other.onComplete());

	// the owner goes away (a script sandbox being torn down): its callback must
	// NOT run - it would dispatch into a dead sandbox
	CHECK(client.cancelOwner(&owner) == 1);
	REQUIRE(pumpUntil(server, handler, client,
		[&other]() { return other.completions > 0; }));
	pumpUntil(server, handler, client, []() { return false; }, 200);
	CHECK(owned.completions == 0);
	CHECK(other.completions == 1);
	server.stop();
}

TEST_CASE("HttpClient saves a body to a file byte-exactly", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	const String body = filler(300 * 1024);
	HttpServer::Handler handler = [&body](HttpRequest const &)
	{
		HttpResponse response;
		response.contentType = "application/octet-stream";
		response.body = body;
		return response;
	};

	ScratchFile target("payload.bin");
	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/download");
	request.savePath = target.string();
	request.maxResponseBytes = 4 * 1024 * 1024;
	client.submit(request, recorder.onComplete(), recorder.onProgress());
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }, 20000));

	CHECK(recorder.response.completed);
	CHECK(recorder.response.ok());
	CHECK(recorder.response.savedPath == target.string());
	// save-to-file keeps NOTHING in memory
	CHECK(recorder.response.body.empty());
	CHECK(recorder.response.bytes == body.size());
	// the file is the transfer, byte for byte, and the parent dir was created
	REQUIRE(target.exists());
	CHECK(target.read() == body);
	// no temp file is left behind
	std::error_code ignored;
	CHECK_FALSE(std::filesystem::exists(target.string() + ".orkpart", ignored));
	server.stop();
}

TEST_CASE("A capped save-to-file leaves the previous file untouched",
	"[http][security]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	const String body = filler(256 * 1024);
	HttpServer::Handler handler = [&body](HttpRequest const &)
	{
		HttpResponse response;
		response.body = body;
		return response;
	};

	ScratchFile target("existing.bin");
	String error;
	REQUIRE(Orkige::FileWriter::writeWholeFile(target.string(),
		"the previous good file", error));

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/download");
	request.savePath = target.string();
	request.maxResponseBytes = 2048;	// smaller than the response
	client.submit(request, recorder.onComplete());
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));

	CHECK(recorder.response.failure == Orkige::HF_TOO_LARGE);
	// THE contract: a failed download never destroys what was there
	CHECK(target.read() == "the previous good file");
	std::error_code ignored;
	CHECK_FALSE(std::filesystem::exists(target.string() + ".orkpart", ignored));
	server.stop();
}

TEST_CASE("HttpClient refuses a save path it cannot open", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	HttpServer::Handler handler = [](HttpRequest const &)
	{
		HttpResponse response;
		response.body = "x";
		return response;
	};

	Recorder recorder;
	HttpClientRequest request = localRequest(server, "/download");
	// a directory that cannot be created (a path under a regular file)
	ScratchFile blocker("blocker");
	String error;
	REQUIRE(Orkige::FileWriter::writeWholeFile(blocker.string(), "x", error));
	request.savePath = blocker.string() + "/inside/file.bin";
	client.submit(request, recorder.onComplete());
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }));
	CHECK(recorder.response.failure == Orkige::HF_BAD_SAVE_PATH);
	CHECK_FALSE(recorder.response.reason.empty());
	server.stop();
}

TEST_CASE("HttpClient serves several requests in one frame", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	HttpServer::Handler handler = [](HttpRequest const & request)
	{
		HttpResponse response;
		response.body = request.target;
		return response;
	};

	Recorder first;
	Recorder second;
	Recorder third;
	client.submit(localRequest(server, "/one"), first.onComplete());
	client.submit(localRequest(server, "/two"), second.onComplete());
	client.submit(localRequest(server, "/three"), third.onComplete());
	CHECK(client.getPendingCount() == 3);

	REQUIRE(pumpUntil(server, handler, client, [&]()
	{
		return first.completions && second.completions && third.completions;
	}));
	CHECK(first.response.body == "/one");
	CHECK(second.response.body == "/two");
	CHECK(third.response.body == "/three");
	CHECK(client.getPendingCount() == 0);
	CHECK(client.getCompletedCount() == 3);
	server.stop();
}

TEST_CASE("An https URL against a plain-http server fails as a TLS error",
	"[http][security]")
{
	// The offline half of the TLS proof: our loopback server speaks PLAIN
	// HTTP, so an https:// request to it must fail the handshake. What this
	// pins down is that the client really does attempt TLS and reports the
	// failure honestly - it never silently continues in the clear when the
	// caller asked for https. That a VALID certificate chain verifies (and an
	// invalid one is refused) can only be shown against a real server: the
	// opt-in http_network test is what proves it.
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	int served = 0;
	HttpServer::Handler handler = [&served](HttpRequest const &)
	{
		++served;
		HttpResponse response;
		response.body = "plain http";
		return response;
	};

	Recorder recorder;
	HttpClientRequest request;
	request.url = "https://127.0.0.1:" + std::to_string(server.getPort()) + "/x";
	request.timeoutMs = 1200;
	client.submit(request, recorder.onComplete());
	REQUIRE(pumpUntil(server, handler, client,
		[&recorder]() { return recorder.completions > 0; }, 15000));

	CHECK(recorder.completions == 1);
	// NOT a success, and above all not the plain-http answer the server was
	// ready to give: no status, no body
	CHECK_FALSE(recorder.response.completed);
	CHECK(recorder.response.status == 0);
	CHECK(recorder.response.body.empty());
	CHECK_FALSE(recorder.response.reason.empty());
	// the failure is whatever the stalled handshake produced (a TLS error, a
	// dropped connection, or the timeout - our plain server simply never
	// answers a ClientHello); the CONTRACT under test is that the client
	// spoke TLS and never fell back, which is why the server never saw a
	// single valid HTTP request
	CHECK((recorder.response.failure == Orkige::HF_TLS_FAILED ||
		recorder.response.failure == Orkige::HF_CONNECT_FAILED ||
		recorder.response.failure == Orkige::HF_TIMEOUT));
	CHECK(served == 0);
	server.stop();
}

TEST_CASE("HttpClient reports no handle for a request nobody listens to",
	"[http]")
{
	HttpClient client;
	HttpClientRequest request;
	request.url = "https://example.com/x";
	// a submission with no completion callback is a caller mistake: it gets no
	// handle and does nothing (the log line says why)
	CHECK(client.submit(request, Orkige::HttpCompleteCallback()) == 0);
	CHECK(client.getPendingCount() == 0);
}
