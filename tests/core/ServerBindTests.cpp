/**************************************************************
	created:	2026/07/20 at 12:00
	filename: 	ServerBindTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The security-regression seam for the network control surfaces: both the MCP
// HttpServer and the player DebugServer must bind 127.0.0.1 ONLY by default, so
// neither the editor-control surface nor the play-mode debug link is reachable
// off the machine. Asserting interface isolation from another host is not
// possible in a unit harness (a single loopback NIC), so this pins the bind via
// the isLoopbackOnly() seam plus a live loopback connect proving reachability,
// and checks the explicit opt-in flips it.

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/DebugServer.h>
#include <core_debugnet/HttpServer.h>

#ifndef _WIN32
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <unistd.h>

#include <cstring>

using Orkige::DebugServer;
using Orkige::HttpServer;

namespace
{
	//! true when a blocking TCP connect to 127.0.0.1:port succeeds
	bool loopbackReachable(unsigned short port)
	{
		const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (fd < 0)
		{
			return false;
		}
		struct sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
		const bool ok = ::connect(fd,
			reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) == 0;
		::close(fd);
		return ok;
	}
}

TEST_CASE("HttpServer binds loopback only by default", "[http][security]")
{
	HttpServer server;
	// a fresh server reports loopback-only before it ever listened
	CHECK(server.isLoopbackOnly());
	REQUIRE(server.start(0));	// default: no exposeNonLoopback
	const unsigned short port = server.getPort();
	REQUIRE(port != 0);
	// the security regression: the control surface is bound to 127.0.0.1 only
	CHECK(server.isLoopbackOnly());
	// and it really is reachable on loopback (the endpoint still works)
	CHECK(loopbackReachable(port));
	server.stop();
	// stop() restores the safe default
	CHECK(server.isLoopbackOnly());
}

TEST_CASE("HttpServer non-loopback bind is an explicit opt-in",
	"[http][security]")
{
	HttpServer server;
	REQUIRE(server.start(0, /*exposeNonLoopback=*/true));
	const unsigned short port = server.getPort();
	REQUIRE(port != 0);
	// the opt-in flips the seam - the surface is now on every interface
	CHECK_FALSE(server.isLoopbackOnly());
	// INADDR_ANY includes loopback, so the endpoint stays reachable locally
	CHECK(loopbackReachable(port));
	server.stop();
}

TEST_CASE("DebugServer binds loopback only by default", "[debug][security]")
{
	DebugServer server;
	CHECK(server.isLoopbackOnly());
	REQUIRE(server.start(0));	// default
	const unsigned short port = server.getPort();
	REQUIRE(port != 0);
	CHECK(server.isLoopbackOnly());
	CHECK(loopbackReachable(port));
	server.stop();
	CHECK(server.isLoopbackOnly());
}

TEST_CASE("DebugServer non-loopback bind is an explicit opt-in",
	"[debug][security]")
{
	DebugServer server;
	REQUIRE(server.start(0, /*exposeNonLoopback=*/true));
	const unsigned short port = server.getPort();
	REQUIRE(port != 0);
	CHECK_FALSE(server.isLoopbackOnly());
	CHECK(loopbackReachable(port));
	server.stop();
}

#endif // !_WIN32
