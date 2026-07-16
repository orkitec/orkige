/**************************************************************
	created:	2026/07/16 at 15:00
	filename: 	Sha1Tests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Unit tests for core_util/Sha1: the FIPS 180-1 test vectors, the
	multi-block padding boundary, the RFC 6455 WebSocket handshake
	string (the digest the debug link's upgrade depends on) and the
	file digest helper (content fingerprints for the asset-import
	change detection).
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/Sha1.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>	// _getpid - unique temp file names (parallel ctest)
#define getpid _getpid
#else
#include <unistd.h>
#endif

using Orkige::Sha1;

namespace
{
	std::string hexOf(std::string const & text)
	{
		return Sha1::hexDigest(text.data(), text.size());
	}
}

TEST_CASE("sha1 known vectors", "[unit][sha1]")
{
	// FIPS 180-1 appendix A/B plus the empty message
	CHECK(hexOf("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	CHECK(hexOf("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
	CHECK(hexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
		"84983e441c3bd26ebaae4aa1f95129e5e54670f1");
	// exactly one block of 'a's and one block + 1 (the padding boundary)
	CHECK(hexOf(std::string(64, 'a')) ==
		"0098ba824b5c16427bd7a1122a5a442a25ec644d");
	CHECK(hexOf(std::string(65, 'a')) ==
		"11655326c708d70319be2610e8a57d9a5b959d3b");
}

TEST_CASE("sha1 websocket handshake string", "[unit][sha1]")
{
	// RFC 6455 section 1.3: the sample nonce + the fixed GUID digest to the
	// documented accept value (base64 "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")
	const std::string salted =
		"dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	CHECK(hexOf(salted) == "b37a4f2cc0624f1690f64606cf385945b2bec4ea");
	// the raw digest agrees with the hex form byte for byte
	unsigned char raw[20];
	Sha1::digest(reinterpret_cast<unsigned char const *>(salted.data()),
		salted.size(), raw);
	CHECK(static_cast<int>(raw[0]) == 0xb3);
	CHECK(static_cast<int>(raw[19]) == 0xea);
}

TEST_CASE("sha1 file digest", "[unit][sha1]")
{
	const std::filesystem::path path =
		std::filesystem::temp_directory_path() /
		("orkige_sha1_test_" + std::to_string(::getpid()) + ".bin");
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << "abc";
	}
	CHECK(Sha1::hexDigestOfFile(path.string()) ==
		"a9993e364706816aba3e25717850c26c9cd0d89d");
	// an empty file digests like the empty message
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
	}
	CHECK(Sha1::hexDigestOfFile(path.string()) ==
		"da39a3ee5e6b4b0d3255bfef95601890afd80709");
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	// a missing file has no fingerprint (the caller treats "" as stale)
	CHECK(Sha1::hexDigestOfFile(path.string()) == "");
}
