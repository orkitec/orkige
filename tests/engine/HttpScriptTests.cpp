/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpScriptTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The Lua `http` table end to end, headlessly and with no network: real Lua
	calls a real HttpClient against the tree's own loopback HttpServer, and the
	answer table a game script receives is read back out of the Lua state. The
	frame-boundary contract is visible here too - nothing reaches the script
	until HttpClient::update() runs, which the player loop calls in its
	async-answers slot.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <core_debugnet/HttpServer.h>
#include <core_http/HttpClient.h>
#include <core_script/ScriptRuntime.h>
#include <engine_gocomponent/ScriptComponent.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using Orkige::HttpClient;
using Orkige::HttpRequest;
using Orkige::HttpResponse;
using Orkige::HttpServer;
using Orkige::ScriptComponent;
using Orkige::ScriptRuntime;
using Orkige::String;

namespace
{
	//! the global path "<table>.<name>" the readback accessors walk
	Orkige::StringVector at(char const * table, char const * name)
	{
		Orkige::StringVector path;
		path.push_back(table);
		path.push_back(name);
		return path;
	}
	//! pump the loopback server and the client until the predicate holds
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
		server.update(handler);
		client.update();
		return predicate();
	}
	//! run a Lua chunk, asserting it compiled and ran
	void run(String const & source)
	{
		const ScriptRuntime::Result result =
			ScriptRuntime::getSingleton().runString(source);
		INFO(result.error);
		REQUIRE(result.success);
	}
	//! has the script recorded an answer yet
	bool answered()
	{
		return ScriptRuntime::getSingleton().getBool(at("probe", "done"), false);
	}
	//! the loopback base URL of a running server
	String base(HttpServer const & server)
	{
		return "http://127.0.0.1:" + std::to_string(server.getPort());
	}
}

TEST_CASE("the Lua http table GETs a loopback response", "[http][script]")
{
	Orkige::EngineTestEnvironment::get();
	ScriptComponent::ensureScriptApi();
	if (!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled in this build - the table is absent by "
			"construction");
		return;
	}
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	REQUIRE(ScriptRuntime::getSingleton().runString(
		"return http.isAvailable()").success);

	std::vector<HttpRequest> seen;
	HttpServer::Handler handler = [&seen](HttpRequest const & request)
	{
		seen.push_back(request);
		HttpResponse response;
		response.status = 200;
		response.contentType = "application/json";
		response.body = "{\"top\":\"ada\"}";
		return response;
	};

	run(
		"probe = { done = false }\n"
		"probe.id = http.request{\n"
		"  url = '" + base(server) + "/scores',\n"
		"  allowInsecureHttp = true,\n"
		"  timeout = 8,\n"
		"  headers = { 'X-Api-Key: k-123', 'Accept: application/json' },\n"
		"  onProgress = function(received, total)\n"
		"    probe.progress = (probe.progress or 0) + 1\n"
		"  end,\n"
		"  onComplete = function(res)\n"
		"    probe.done = true\n"
		"    probe.ok = res.ok\n"
		"    probe.status = res.status\n"
		"    probe.body = res.body\n"
		"    probe.error = res.error\n"
		"    probe.contentType = res.headers['content-type']\n"
		"  end,\n"
		"}\n");
	// a handle came back immediately, and nothing has been delivered yet
	CHECK(ScriptRuntime::getSingleton().getNumber(at("probe", "id"), 0.0) > 0.0);
	CHECK_FALSE(answered());

	REQUIRE(pumpUntil(server, handler, client, answered));
	ScriptRuntime & runtime = ScriptRuntime::getSingleton();
	CHECK(runtime.getBool(at("probe", "ok"), false));
	CHECK(runtime.getNumber(at("probe", "status"), 0.0) == 200.0);
	CHECK(runtime.getString(at("probe", "body"), "") == "{\"top\":\"ada\"}");
	// the failure token is EMPTY for a completed exchange
	CHECK(runtime.getString(at("probe", "error"), "unset").empty());
	// response headers arrive as a nested table, keyed lower-case
	CHECK(runtime.getString(at("probe", "contentType"), "") == "application/json");
	CHECK(runtime.getNumber(at("probe", "progress"), 0.0) >= 1.0);
	// the authored headers reached the server as written
	REQUIRE(seen.size() == 1);
	CHECK(seen[0].header("x-api-key") == "k-123");
	CHECK(seen[0].header("accept") == "application/json");
	server.stop();
}

TEST_CASE("the Lua http table POSTs and reads an error status", "[http][script]")
{
	Orkige::EngineTestEnvironment::get();
	ScriptComponent::ensureScriptApi();
	if (!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled in this build");
		return;
	}
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	std::vector<HttpRequest> seen;
	HttpServer::Handler handler = [&seen](HttpRequest const & request)
	{
		seen.push_back(request);
		HttpResponse response;
		response.status = 503;
		response.body = "busy";
		return response;
	};

	run(
		"probe = { done = false }\n"
		"http.post('" + base(server) + "/scores', '{\"score\":9}',\n"
		"  'application/json', function(res)\n"
		"    probe.done = true\n"
		"    probe.ok = res.ok\n"
		"    probe.status = res.status\n"
		"    probe.body = res.body\n"
		"  end)\n");
	// NOTE the plain-http refusal: http.post has no options table, so the
	// insecure opt-in is unavailable and the request is REFUSED - which is
	// exactly what the completion reports
	REQUIRE(pumpUntil(server, handler, client, answered));
	ScriptRuntime & runtime = ScriptRuntime::getSingleton();
	CHECK_FALSE(runtime.getBool(at("probe", "ok"), true));
	CHECK(runtime.getNumber(at("probe", "status"), -1.0) == 0.0);
	CHECK(seen.empty());

	// the same POST through http.request{} carries the opt-in and goes through
	run(
		"probe = { done = false }\n"
		"http.request{\n"
		"  url = '" + base(server) + "/scores',\n"
		"  method = 'POST',\n"
		"  body = '{\"score\":9}',\n"
		"  contentType = 'application/json',\n"
		"  allowInsecureHttp = true,\n"
		"  onComplete = function(res)\n"
		"    probe.done = true\n"
		"    probe.ok = res.ok\n"
		"    probe.status = res.status\n"
		"    probe.body = res.body\n"
		"  end,\n"
		"}\n");
	REQUIRE(pumpUntil(server, handler, client, answered));
	// an HTTP status is an ANSWER, not a failure: ok is false, status is 503
	CHECK_FALSE(runtime.getBool(at("probe", "ok"), true));
	CHECK(runtime.getNumber(at("probe", "status"), 0.0) == 503.0);
	CHECK(runtime.getString(at("probe", "body"), "") == "busy");
	REQUIRE(seen.size() == 1);
	CHECK(seen[0].method == "POST");
	CHECK(seen[0].body == "{\"score\":9}");
	server.stop();
}

TEST_CASE("the Lua http table reports refusals as failure tokens",
	"[http][script][security]")
{
	Orkige::EngineTestEnvironment::get();
	ScriptComponent::ensureScriptApi();
	if (!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled in this build");
		return;
	}
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;
	int served = 0;
	HttpServer::Handler handler = [&served](HttpRequest const &)
	{
		++served;
		return HttpResponse();
	};

	// a plain-http URL with no opt-in: the script learns WHY, by token and in
	// words, through the one completion path
	run(
		"probe = { done = false }\n"
		"http.get('" + base(server) + "/x', function(res)\n"
		"  probe.done = true\n"
		"  probe.ok = res.ok\n"
		"  probe.error = res.error\n"
		"  probe.reason = res.reason\n"
		"end)\n");
	REQUIRE(pumpUntil(server, handler, client, answered, 3000));
	ScriptRuntime & runtime = ScriptRuntime::getSingleton();
	CHECK_FALSE(runtime.getBool(at("probe", "ok"), true));
	CHECK(runtime.getString(at("probe", "error"), "") == "insecure-scheme");
	CHECK_FALSE(runtime.getString(at("probe", "reason"), "").empty());

	// and a URL that is not a URL
	run(
		"probe = { done = false }\n"
		"http.get('not a url', function(res)\n"
		"  probe.done = true\n"
		"  probe.error = res.error\n"
		"end)\n");
	REQUIRE(pumpUntil(server, handler, client, answered, 3000));
	CHECK(runtime.getString(at("probe", "error"), "") == "bad-url");
	CHECK(served == 0);
	server.stop();
}

TEST_CASE("the Lua http table cancels a request", "[http][script]")
{
	Orkige::EngineTestEnvironment::get();
	ScriptComponent::ensureScriptApi();
	if (!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled in this build");
		return;
	}
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	String body;
	body.assign(3 * 1024 * 1024, 'z');
	HttpServer::Handler handler = [&body](HttpRequest const &)
	{
		HttpResponse response;
		response.body = body;
		return response;
	};

	run(
		"probe = { done = false }\n"
		"probe.id = http.request{\n"
		"  url = '" + base(server) + "/big',\n"
		"  allowInsecureHttp = true,\n"
		"  maxBytes = 8388608,\n"
		"  onComplete = function(res)\n"
		"    probe.done = true\n"
		"    probe.error = res.error\n"
		"  end,\n"
		"}\n"
		"probe.pending = http.pending()\n");
	ScriptRuntime & runtime = ScriptRuntime::getSingleton();
	CHECK(runtime.getNumber(at("probe", "pending"), 0.0) == 1.0);

	run("probe.cancelled = http.cancel(probe.id)\n");
	CHECK(runtime.getBool(at("probe", "cancelled"), false));
	REQUIRE(pumpUntil(server, handler, client, answered));
	// a cancelled request still answers exactly once, and says so
	CHECK(runtime.getString(at("probe", "error"), "") == "cancelled");
	server.stop();
}

TEST_CASE("the Lua http table downloads to a file", "[http][script]")
{
	Orkige::EngineTestEnvironment::get();
	ScriptComponent::ensureScriptApi();
	if (!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled in this build");
		return;
	}
	HttpServer server;
	REQUIRE(server.start(0));
	HttpClient client;

	String body;
	body.assign(64 * 1024, 'q');
	HttpServer::Handler handler = [&body](HttpRequest const &)
	{
		HttpResponse response;
		response.body = body;
		return response;
	};

	const std::filesystem::path target =
		std::filesystem::temp_directory_path() /
		("orkige_http_script_" + std::to_string(
			static_cast<unsigned long long>(std::chrono::steady_clock::now()
				.time_since_epoch().count()))) / "asset.bin";
	String targetPath = target.string();
	// Lua string literal: keep the path escape-free (a temp path has no
	// backslashes on the platforms this test runs on)
	run(
		"probe = { done = false }\n"
		"http.request{\n"
		"  url = '" + base(server) + "/asset',\n"
		"  savePath = '" + targetPath + "',\n"
		"  allowInsecureHttp = true,\n"
		"  onComplete = function(res)\n"
		"    probe.done = true\n"
		"    probe.ok = res.ok\n"
		"    probe.path = res.path\n"
		"    probe.bytes = res.bytes\n"
		"    probe.body = res.body\n"
		"  end,\n"
		"}\n");
	REQUIRE(pumpUntil(server, handler, client, answered, 20000));
	ScriptRuntime & runtime = ScriptRuntime::getSingleton();
	CHECK(runtime.getBool(at("probe", "ok"), false));
	CHECK(runtime.getString(at("probe", "path"), "") == targetPath);
	CHECK(runtime.getNumber(at("probe", "bytes"), 0.0) ==
		static_cast<double>(body.size()));
	// a download keeps nothing in memory - the file IS the payload
	CHECK(runtime.getString(at("probe", "body"), "unset").empty());
	std::error_code ignored;
	CHECK(std::filesystem::file_size(target, ignored) == body.size());
	std::filesystem::remove_all(target.parent_path(), ignored);
	server.stop();
}

TEST_CASE("the Lua http table refuses honestly with no client", "[http][script]")
{
	Orkige::EngineTestEnvironment::get();
	ScriptComponent::ensureScriptApi();
	if (!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled in this build");
		return;
	}
	// no HttpClient exists in this scope - the shape of the editor's edit mode
	REQUIRE(HttpClient::getSingletonPtr() == NULL);
	run(
		"probe = { done = false }\n"
		"probe.available = http.isAvailable()\n"
		"probe.pending = http.pending()\n"
		"probe.id = http.get('https://example.com/x', function() end)\n"
		"probe.cancelled = http.cancel(1)\n");
	ScriptRuntime & runtime = ScriptRuntime::getSingleton();
	CHECK_FALSE(runtime.getBool(at("probe", "available"), true));
	CHECK(runtime.getNumber(at("probe", "pending"), -1.0) == 0.0);
	// no handle, no crash, one honest log line
	CHECK(runtime.getNumber(at("probe", "id"), -1.0) == 0.0);
	CHECK_FALSE(runtime.getBool(at("probe", "cancelled"), true));
}
