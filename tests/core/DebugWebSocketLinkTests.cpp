/**************************************************************
	created:	2026/07/16 at 12:00
	filename: 	DebugWebSocketLinkTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The browser debug transport end to end, without a browser: an HttpServer
// answers a real WebSocket upgrade over a loopback socket and hands the
// connection to a DebugClient (adoptWebSocket - the editor side of a Play
// in Browser session), while this test plays the browser player: it sends
// client-masked binary frames carrying DebugMessage lines exactly the way
// the wasm player's POSIX-socket emulation does, and reads back the
// server's unmasked frames.

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/DebugClient.h>
#include <core_debugnet/DebugProtocol.h>
#include <core_debugnet/HttpServer.h>
#include <core_debugnet/WebSocket.h>

#ifndef _WIN32
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using Orkige::DebugClient;
using Orkige::DebugMessage;
using Orkige::HttpRequest;
using Orkige::HttpResponse;
using Orkige::HttpServer;
using Orkige::String;
namespace Protocol = Orkige::DebugProtocol;
namespace Ws = Orkige::WebSocketUtil;

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
	//! pump server + adopted client while collecting the peer's bytes until
	//! predicate() or the deadline
	template <typename Predicate>
	bool pumpUntil(HttpServer& server, HttpServer::Handler const& handler,
		DebugClient* adopted, int fd, String& received, Predicate predicate)
	{
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline)
		{
			server.update(handler);
			if (adopted)
			{
				adopted->update();
			}
			char chunk[4096];
			const long n = ::recv(fd, chunk, sizeof(chunk), MSG_DONTWAIT);
			if (n > 0)
			{
				received.append(chunk, static_cast<size_t>(n));
			}
			if (predicate())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}
}

TEST_CASE("HttpServer upgrade takeover feeds a WebSocket DebugClient",
	"[websocket]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	const unsigned short port = server.getPort();

	// the editor-side wiring: an upgrade request answers with the RFC
	// handshake and the flushed socket lands in the DebugClient
	DebugClient editorSide;
	bool takenOver = false;
	server.setTakeoverHandler(
		[&editorSide, &takenOver](Orkige::DebugSocketUtil::SocketHandle handle,
			String const& leftover)
	{
		editorSide.adoptWebSocket(handle, leftover);
		takenOver = true;
	});
	HttpServer::Handler handler = [](HttpRequest const& request)
	{
		if (Ws::isUpgradeRequest(request))
		{
			return Ws::buildHandshakeResponse(request);
		}
		HttpResponse response;
		response.status = 404;
		response.reason = "Not Found";
		return response;
	};

	// --- the synthetic browser peer: handshake ---
	const int fd = connectClient(port);
	REQUIRE(fd >= 0);
	const String upgrade =
		"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: Upgrade\r\n"
		"Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Protocol: binary\r\n\r\n";
	REQUIRE(::send(fd, upgrade.data(), upgrade.size(), 0) ==
		static_cast<long>(upgrade.size()));

	String received;
	REQUIRE(pumpUntil(server, handler, nullptr, fd, received,
		[&takenOver, &received]
	{
		return takenOver && received.find("\r\n\r\n") != String::npos;
	}));
	CHECK(received.find("HTTP/1.1 101") != String::npos);
	CHECK(received.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != String::npos);
	CHECK(received.find("Sec-WebSocket-Protocol: binary") != String::npos);
	// the upgrade response is no ordinary HTTP response
	CHECK(received.find("Content-Length") == String::npos);
	REQUIRE(editorSide.isConnected());
	received.erase(0, received.find("\r\n\r\n") + 4);

	// --- masked frames carry protocol lines to the editor ---
	DebugMessage hello(Protocol::MSG_HELLO);
	hello.set(Protocol::FIELD_SCENE, "scenes/level.oscene");
	DebugMessage log(Protocol::MSG_LOG);
	log.set(Protocol::FIELD_MESSAGE, "booted");
	// two lines in ONE frame plus a line SPLIT across two frames: WebSocket
	// message boundaries must carry no meaning for the line stream
	const String twoLines = hello.encode() + "\n" + log.encode() + "\n";
	String frames =
		Ws::encodeMaskedFrame(Ws::OP_BINARY, twoLines, 0xA1B2C3D4u);
	DebugMessage error(Protocol::MSG_SCRIPT_ERROR);
	error.set(Protocol::FIELD_ID, "player");
	const String split = error.encode() + "\n";
	frames += Ws::encodeMaskedFrame(Ws::OP_BINARY,
		split.substr(0, split.size() / 2), 0x55u);
	frames += Ws::encodeMaskedFrame(Ws::OP_BINARY,
		split.substr(split.size() / 2), 0x66u);
	// and a ping the server side must answer
	frames += Ws::encodeMaskedFrame(Ws::OP_PING, "ka", 0x77u);
	REQUIRE(::send(fd, frames.data(), frames.size(), 0) ==
		static_cast<long>(frames.size()));

	DebugMessage message;
	std::vector<DebugMessage> deliveries;
	REQUIRE(pumpUntil(server, handler, &editorSide, fd, received,
		[&editorSide, &deliveries, &message]
	{
		while (editorSide.receive(message))
		{
			deliveries.push_back(message);
		}
		return deliveries.size() >= 3;
	}));
	REQUIRE(deliveries.size() == 3);
	CHECK(deliveries[0].type == Protocol::MSG_HELLO);
	CHECK(deliveries[0].get(Protocol::FIELD_SCENE) == "scenes/level.oscene");
	CHECK(deliveries[1].type == Protocol::MSG_LOG);
	CHECK(deliveries[2].type == Protocol::MSG_SCRIPT_ERROR);

	// --- the pong answered the ping (it may trail the data frames, so keep
	// pumping until it lands) ---
	{
		bool pongSeen = false;
		REQUIRE(pumpUntil(server, handler, &editorSide, fd, received,
			[&received, &pongSeen]
		{
			String wsBytes = received;
			Ws::Frame frame;
			std::size_t consumed = 0;
			while (Ws::decodeFrame(wsBytes, consumed, frame) ==
				Ws::DecodeResult::Ok)
			{
				wsBytes.erase(0, consumed);
				if (frame.opcode == Ws::OP_PONG && frame.payload == "ka")
				{
					pongSeen = true;
				}
			}
			return pongSeen;
		}));
		CHECK(pongSeen);
	}

	// --- the editor's sends arrive as unmasked binary frames ---
	received.clear();
	DebugMessage pause(Protocol::MSG_PAUSE);
	REQUIRE(editorSide.send(pause));
	String peerLine;
	REQUIRE(pumpUntil(server, handler, &editorSide, fd, received,
		[&received, &peerLine]
	{
		// scan past any control frames (a straggling pong) to the first
		// binary frame
		String wsBytes = received;
		Ws::Frame frame;
		std::size_t consumed = 0;
		while (Ws::decodeFrame(wsBytes, consumed, frame) ==
			Ws::DecodeResult::Ok)
		{
			wsBytes.erase(0, consumed);
			if (frame.opcode == Ws::OP_BINARY)
			{
				peerLine = frame.payload;
				return true;
			}
		}
		return false;
	}));
	DebugMessage decoded;
	REQUIRE(!peerLine.empty());
	REQUIRE(peerLine.back() == '\n');
	peerLine.pop_back();
	REQUIRE(DebugMessage::decode(peerLine, decoded));
	CHECK(decoded.type == Protocol::MSG_PAUSE);

	// --- a close frame ends the link like a peer drop ---
	const String closeFrame =
		Ws::encodeMaskedFrame(Ws::OP_CLOSE, String(), 0x99u);
	REQUIRE(::send(fd, closeFrame.data(), closeFrame.size(), 0) ==
		static_cast<long>(closeFrame.size()));
	received.clear();
	REQUIRE(pumpUntil(server, handler, &editorSide, fd, received,
		[&editorSide]
	{
		return editorSide.getState() == DebugClient::State::Disconnected;
	}));

	::close(fd);
	server.stop();
}

TEST_CASE("HttpServer without a takeover handler closes upgrades",
	"[websocket]")
{
	HttpServer server;
	REQUIRE(server.start(0));
	HttpServer::Handler handler = [](HttpRequest const& request)
	{
		if (Ws::isUpgradeRequest(request))
		{
			return Ws::buildHandshakeResponse(request);
		}
		return HttpResponse();
	};
	const int fd = connectClient(server.getPort());
	REQUIRE(fd >= 0);
	const String upgrade =
		"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: Upgrade\r\n"
		"Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
	REQUIRE(::send(fd, upgrade.data(), upgrade.size(), 0) ==
		static_cast<long>(upgrade.size()));
	// the head still flushes, then the connection just ends (recv sees EOF)
	String received;
	REQUIRE(pumpUntil(server, handler, nullptr, fd, received, [&received, fd]
	{
		char probe;
		return received.find("\r\n\r\n") != String::npos &&
			::recv(fd, &probe, 1, MSG_DONTWAIT) == 0;
	}));
	CHECK(received.find("HTTP/1.1 101") != String::npos);
	::close(fd);
	server.stop();
}

#endif // !_WIN32
