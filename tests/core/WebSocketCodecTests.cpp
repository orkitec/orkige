/**************************************************************
	created:	2026/07/16 at 12:00
	filename: 	WebSocketCodecTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/HttpServer.h>
#include <core_debugnet/WebSocket.h>

using Orkige::HttpRequest;
using Orkige::HttpResponse;
using Orkige::String;
namespace Ws = Orkige::WebSocketUtil;

namespace
{
	//! the extra header value for a (lower-cased) name, "" when absent
	String extraHeader(HttpResponse const& response, String const& name)
	{
		for (auto const& [key, value] : response.extraHeaders)
		{
			String lower(key);
			for (char& c : lower)
			{
				if (c >= 'A' && c <= 'Z')
				{
					c = static_cast<char>(c - 'A' + 'a');
				}
			}
			if (lower == name)
			{
				return value;
			}
		}
		return String();
	}
}

TEST_CASE("WebSocket accept key matches the RFC 6455 vector", "[websocket]")
{
	// the worked example from RFC 6455 section 1.3
	CHECK(Ws::computeAcceptKey("dGhlIHNhbXBsZSBub25jZQ==") ==
		"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("WebSocket upgrade requests are recognised", "[websocket]")
{
	HttpRequest upgrade;
	upgrade.method = "GET";
	upgrade.target = "/";
	upgrade.headers["connection"] = "keep-alive, Upgrade";
	upgrade.headers["upgrade"] = "websocket";
	upgrade.headers["sec-websocket-key"] = "dGhlIHNhbXBsZSBub25jZQ==";
	upgrade.headers["sec-websocket-protocol"] = "binary";
	CHECK(Ws::isUpgradeRequest(upgrade));

	// a plain GET is not an upgrade
	HttpRequest plain;
	plain.method = "GET";
	plain.target = "/index.html";
	CHECK_FALSE(Ws::isUpgradeRequest(plain));

	// an upgrade without a key is refused (nothing to accept against)
	HttpRequest keyless = upgrade;
	keyless.headers.erase("sec-websocket-key");
	CHECK_FALSE(Ws::isUpgradeRequest(keyless));

	// POST never upgrades
	HttpRequest post = upgrade;
	post.method = "POST";
	CHECK_FALSE(Ws::isUpgradeRequest(post));
}

TEST_CASE("WebSocket handshake response carries the accept headers",
	"[websocket]")
{
	HttpRequest upgrade;
	upgrade.method = "GET";
	upgrade.target = "/";
	upgrade.headers["connection"] = "Upgrade";
	upgrade.headers["upgrade"] = "websocket";
	upgrade.headers["sec-websocket-key"] = "dGhlIHNhbXBsZSBub25jZQ==";
	upgrade.headers["sec-websocket-protocol"] = "binary, base64";

	const HttpResponse response = Ws::buildHandshakeResponse(upgrade);
	CHECK(response.status == 101);
	CHECK(response.takeover);
	CHECK(response.body.empty());
	CHECK(response.contentType.empty());
	CHECK(extraHeader(response, "upgrade") == "websocket");
	CHECK(extraHeader(response, "connection") == "Upgrade");
	CHECK(extraHeader(response, "sec-websocket-accept") ==
		"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
	// the FIRST offered subprotocol is echoed
	CHECK(extraHeader(response, "sec-websocket-protocol") == "binary");
}

TEST_CASE("WebSocket frames round-trip", "[websocket]")
{
	const String payload = "{\"v\":1,\"type\":\"log\"}\n";

	SECTION("unmasked (server-to-client)")
	{
		const String wire = Ws::encodeFrame(Ws::OP_BINARY, payload);
		Ws::Frame frame;
		std::size_t consumed = 0;
		REQUIRE(Ws::decodeFrame(wire, consumed, frame) ==
			Ws::DecodeResult::Ok);
		CHECK(consumed == wire.size());
		CHECK(frame.fin);
		CHECK(frame.opcode == Ws::OP_BINARY);
		CHECK(frame.payload == payload);
	}

	SECTION("masked (client-to-server)")
	{
		const String wire =
			Ws::encodeMaskedFrame(Ws::OP_BINARY, payload, 0xDEADBEEFu);
		// the mask bit hides the payload on the wire
		CHECK(wire.find(payload) == String::npos);
		Ws::Frame frame;
		std::size_t consumed = 0;
		REQUIRE(Ws::decodeFrame(wire, consumed, frame) ==
			Ws::DecodeResult::Ok);
		CHECK(consumed == wire.size());
		CHECK(frame.payload == payload);
	}

	SECTION("16-bit extended length")
	{
		const String big(60000, 'x');
		const String wire = Ws::encodeMaskedFrame(Ws::OP_BINARY, big, 1u);
		Ws::Frame frame;
		std::size_t consumed = 0;
		REQUIRE(Ws::decodeFrame(wire, consumed, frame) ==
			Ws::DecodeResult::Ok);
		CHECK(frame.payload == big);
	}

	SECTION("64-bit extended length")
	{
		const String big(70000, 'y');
		const String wire = Ws::encodeFrame(Ws::OP_BINARY, big);
		// 70000 > 0xFFFF forces the 8-byte length form
		CHECK(static_cast<unsigned char>(wire[1]) == 127);
		Ws::Frame frame;
		std::size_t consumed = 0;
		REQUIRE(Ws::decodeFrame(wire, consumed, frame) ==
			Ws::DecodeResult::Ok);
		CHECK(frame.payload == big);
	}

	SECTION("control frames keep their opcode")
	{
		const String wire = Ws::encodeFrame(Ws::OP_PING, "sting");
		Ws::Frame frame;
		std::size_t consumed = 0;
		REQUIRE(Ws::decodeFrame(wire, consumed, frame) ==
			Ws::DecodeResult::Ok);
		CHECK(frame.opcode == Ws::OP_PING);
		CHECK(frame.payload == "sting");
	}
}

TEST_CASE("WebSocket decode is incremental and defensive", "[websocket]")
{
	const String payload = "hello frames";
	const String wire =
		Ws::encodeMaskedFrame(Ws::OP_BINARY, payload, 0x01020304u);

	SECTION("partial bytes ask for more")
	{
		Ws::Frame frame;
		std::size_t consumed = 0;
		for (std::size_t cut = 0; cut < wire.size(); ++cut)
		{
			CHECK(Ws::decodeFrame(wire.substr(0, cut), consumed, frame) ==
				Ws::DecodeResult::NeedMore);
		}
	}

	SECTION("two frames in one buffer decode one at a time")
	{
		const String second = Ws::encodeMaskedFrame(Ws::OP_BINARY, "next", 7u);
		String buffer = wire + second;
		Ws::Frame frame;
		std::size_t consumed = 0;
		REQUIRE(Ws::decodeFrame(buffer, consumed, frame) ==
			Ws::DecodeResult::Ok);
		CHECK(frame.payload == payload);
		buffer.erase(0, consumed);
		REQUIRE(Ws::decodeFrame(buffer, consumed, frame) ==
			Ws::DecodeResult::Ok);
		CHECK(frame.payload == "next");
		CHECK(consumed == buffer.size());
	}

	SECTION("a reserved-bit frame is a protocol error")
	{
		String bad = wire;
		bad[0] = static_cast<char>(
			static_cast<unsigned char>(bad[0]) | 0x40u);
		Ws::Frame frame;
		std::size_t consumed = 0;
		CHECK(Ws::decodeFrame(bad, consumed, frame) ==
			Ws::DecodeResult::Error);
	}

	SECTION("an absurd 64-bit length is refused, never allocated")
	{
		String bad;
		bad += static_cast<char>(0x82);	// FIN + binary
		bad += static_cast<char>(127);	// 8-byte length follows
		for (int i = 0; i < 8; ++i)
		{
			bad += static_cast<char>(0x7F);	// ~9 exabytes
		}
		Ws::Frame frame;
		std::size_t consumed = 0;
		CHECK(Ws::decodeFrame(bad, consumed, frame) ==
			Ws::DecodeResult::Error);
	}
}
