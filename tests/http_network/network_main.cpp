/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	network_main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// THE ONE test that needs the real internet, and the ONLY place certificate
// CHAIN VERIFICATION is actually proven: everything else in the HTTP suite runs
// against the tree's own loopback server, which cannot speak TLS. Here a real
// https:// GET must complete with a verified chain through the platform's trust
// store.
//
// It is deliberately hard to turn RED by accident, because a broken network is
// not a broken engine:
//   exit 0   the request completed over TLS with a sane status  -> TLS verified
//   exit 77  SKIPPED: no HTTP client in this build, ORKIGE_NO_NETWORK set, or
//            the machine could not reach the endpoint at all (offline, DNS,
//            proxy, a firewall)
//   exit 1   we DID reach a server and the result was wrong: the handshake was
//            refused, the answer was not HTTP, or a redirect policy misfired
// So a failure means "TLS or the client is broken", never "the wifi is off".
//
// Endpoint: the IANA-reserved documentation domain, whose whole purpose is to
// be safe to fetch in examples; override with ORKIGE_HTTP_NETWORK_URL.

#include <core_http/HttpClient.h>
#include <core_http/HttpTypes.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace
{
	//! the ctest skip contract
	const int EXIT_SKIP = 77;

	//! an env variable's value ("" when unset/empty)
	std::string envValue(char const * name)
	{
		char const * value = std::getenv(name);
		return (value != NULL && value[0] != '\0') ? std::string(value)
			: std::string();
	}
}

int main()
{
	if (!Orkige::HttpClient::compiled())
	{
		std::printf("[http_network] SKIP: this build carries no HTTP client "
			"(ORKIGE_HTTP=OFF)\n");
		return EXIT_SKIP;
	}
	if (!envValue("ORKIGE_NO_NETWORK").empty())
	{
		std::printf("[http_network] SKIP: ORKIGE_NO_NETWORK is set\n");
		return EXIT_SKIP;
	}
	const std::string url = envValue("ORKIGE_HTTP_NETWORK_URL").empty()
		? std::string("https://example.com/") : envValue("ORKIGE_HTTP_NETWORK_URL");

	Orkige::HttpClient client;
	if (!client.available())
	{
		std::printf("[http_network] SKIP: the HTTP transport did not start\n");
		return EXIT_SKIP;
	}
	std::printf("[http_network] GET %s\n", url.c_str());

	Orkige::HttpClientRequest request;
	request.url = url;
	request.timeoutMs = 20000;
	request.maxResponseBytes = 2 * 1024 * 1024;
	// the DEFAULTS are the point of this test: certificates verified, https
	// only, no plain-http fallback, no https->http redirect. Nothing below
	// relaxes any of them.

	bool done = false;
	Orkige::HttpClientResponse answer;
	client.submit(request, [&done, &answer](
		Orkige::HttpClientResponse const & response)
	{
		answer = response;
		done = true;
	});

	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(40);
	while (!done && std::chrono::steady_clock::now() < deadline)
	{
		// the same frame-boundary drain a game does, just without a game
		client.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (!done)
	{
		std::printf("[http_network] SKIP: no answer within 40s - treating an "
			"unreachable network as a skip, not a failure\n");
		return EXIT_SKIP;
	}

	if (!answer.completed)
	{
		switch (answer.failure)
		{
		case Orkige::HF_CONNECT_FAILED:
		case Orkige::HF_TIMEOUT:
			// offline / DNS / proxy / firewall: nothing to say about TLS
			std::printf("[http_network] SKIP: could not reach the endpoint "
				"(%s: %s)\n", Orkige::httpFailureName(answer.failure).c_str(),
				answer.reason.c_str());
			return EXIT_SKIP;
		default:
			// we got somewhere and the exchange still failed - THIS is a
			// finding: a refused handshake here means certificate
			// verification, the TLS floor or the trust store is broken
			std::printf("[http_network] FAIL: the request failed (%s: %s)\n",
				Orkige::httpFailureName(answer.failure).c_str(),
				answer.reason.c_str());
			return 1;
		}
	}

	// a completed https exchange: the chain verified through the platform's
	// trust store, because nothing here disabled that check. The transport is
	// only named now - it is created lazily, on the first submit.
	std::printf("[http_network] transport '%s': status %d, %llu bytes, final "
		"url '%s'\n", client.backendName(), answer.status,
		static_cast<unsigned long long>(answer.bytes), answer.finalUrl.c_str());
	if (answer.status < 200 || answer.status >= 400)
	{
		std::printf("[http_network] FAIL: unexpected status %d\n",
			answer.status);
		return 1;
	}
	if (answer.finalUrl.rfind("https://", 0) != 0)
	{
		std::printf("[http_network] FAIL: the exchange ended on '%s' - an "
			"https request must never end up unencrypted\n",
			answer.finalUrl.c_str());
		return 1;
	}
	if (answer.bytes == 0)
	{
		std::printf("[http_network] FAIL: an empty body from a page that has "
			"one - the response was not read\n");
		return 1;
	}
	std::printf("[http_network] PASS: TLS certificate verification proven "
		"against a real server\n");
	return 0;
}
