/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	HttpServerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/HttpServer.h>

#ifndef _WIN32
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using Orkige::HttpServer;
using Orkige::HttpRequest;
using Orkige::HttpResponse;
using Orkige::String;

namespace
{
	//! connect a blocking client socket to 127.0.0.1:port
	int connectClient(unsigned short port)
	{
		const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (fd < 0)
		{
			return -1;
		}
		struct sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
		if (::connect(fd, reinterpret_cast<struct sockaddr*>(&address),
			sizeof(address)) != 0)
		{
			::close(fd);
			return -1;
		}
		return fd;
	}
	//! pump the server while draining whatever the client can read, until the
	//! accumulated buffer contains `count` complete "\r\n\r\n"-delimited
	//! responses or the deadline passes
	String pumpForResponses(HttpServer& server, HttpServer::Handler const& handler,
		int fd, int count)
	{
		String buffer;
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline)
		{
			server.update(handler);
			char chunk[2048];
			const long n = ::recv(fd, chunk, sizeof(chunk), MSG_DONTWAIT);
			if (n > 0)
			{
				buffer.append(chunk, static_cast<size_t>(n));
			}
			// count complete header sections as a cheap "have N responses" proxy
			int seen = 0;
			size_t at = 0;
			while ((at = buffer.find("\r\n\r\n", at)) != String::npos)
			{
				++seen;
				at += 4;
			}
			if (seen >= count)
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return buffer;
	}
}

TEST_CASE("HttpServer parses requests and frames responses", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));	// ephemeral port
	REQUIRE(server.isListening());
	const unsigned short port = server.getPort();
	REQUIRE(port != 0);

	std::vector<HttpRequest> seen;
	HttpServer::Handler handler = [&seen](HttpRequest const& request)
	{
		seen.push_back(request);
		HttpResponse response;
		response.status = 200;
		response.contentType = "application/json";
		response.body = "{\"method\":\"" + request.method + "\",\"target\":\"" +
			request.target + "\",\"body\":\"" + request.body + "\"}";
		return response;
	};

	const int fd = connectClient(port);
	REQUIRE(fd >= 0);

	// two PIPELINED requests on one keep-alive connection; header names must be
	// matched case-insensitively (Content-Length here, content-length is fine)
	const String body1 = "{\"a\":1}";
	const String body2 = "hello world";
	String wire =
		"POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/"
		"json\r\nContent-Length: " + std::to_string(body1.size()) +
		"\r\n\r\n" + body1 +
		"POST /mcp?x=1 HTTP/1.1\r\nHost: 127.0.0.1\r\ncontent-length: " +
		std::to_string(body2.size()) + "\r\n\r\n" + body2;
	REQUIRE(::send(fd, wire.data(), wire.size(), 0) ==
		static_cast<long>(wire.size()));

	const String responses = pumpForResponses(server, handler, fd, 2);

	REQUIRE(seen.size() == 2);
	CHECK(seen[0].method == "POST");
	CHECK(seen[0].target == "/mcp");
	CHECK(seen[0].body == body1);
	CHECK(seen[0].header("content-type") == "application/json");
	CHECK(seen[1].target == "/mcp?x=1");
	CHECK(seen[1].body == body2);

	// both responses carry a Content-Length and the echoed body
	CHECK(responses.find("Content-Length:") != String::npos);
	CHECK(responses.find(body1) != String::npos);
	CHECK(responses.find("hello world") != String::npos);
	CHECK(responses.find("HTTP/1.1 200") != String::npos);

	::close(fd);
	// the abrupt client close is reaped without wedging the server
	server.update(handler);
	server.stop();
}

TEST_CASE("HttpServer serves several connections and survives junk", "[http]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	const unsigned short port = server.getPort();

	HttpServer::Handler handler = [](HttpRequest const&)
	{
		HttpResponse response;
		response.body = "{}";
		return response;
	};

	// a connection that sends garbage (no valid request line) must be answered
	// with a 400 and dropped, never crash the server
	const int junk = connectClient(port);
	REQUIRE(junk >= 0);
	const char* garbage = "not http at all\r\n\r\n";
	REQUIRE(::send(junk, garbage, std::strlen(garbage), 0) > 0);

	// a well-formed connection served in parallel still works
	const int good = connectClient(port);
	REQUIRE(good >= 0);
	const char* request =
		"POST /mcp HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}";
	REQUIRE(::send(good, request, std::strlen(request), 0) > 0);

	const String responses = pumpForResponses(server, handler, good, 1);
	CHECK(responses.find("HTTP/1.1 200") != String::npos);

	::close(junk);
	::close(good);
	server.update(handler);
	server.stop();
}

#endif // !_WIN32
