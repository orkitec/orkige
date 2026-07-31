/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	Sha256Tests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// SHA-256 against the published test vectors, plus the two properties the
// download check depends on: that feeding a message in arbitrary chunks gives
// the same digest as feeding it whole, and that a digest only ever compares
// equal to a complete, well-formed digest of the same value.
#include <catch2/catch_test_macros.hpp>

#include <core_util/Sha256.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

using Orkige::Sha256;
using Orkige::String;

namespace
{
	String digestOf(std::string const& message)
	{
		return Sha256::hexDigest(message.data(), message.size());
	}
}

TEST_CASE("Sha256 matches the published vectors", "[unit][sha256]")
{
	// FIPS 180-4 / NIST example vectors
	CHECK(digestOf("") ==
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(digestOf("abc") ==
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(digestOf(
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	CHECK(digestOf(std::string(1000000, 'a')) ==
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("Sha256 spans the padding boundaries", "[unit][sha256]")
{
	// the lengths where the padding either just fits in the final block or
	// forces one more: 55, 56, 63, 64 bytes
	CHECK(digestOf(std::string(55, 'a')) ==
		"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
	CHECK(digestOf(std::string(56, 'a')) ==
		"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
	CHECK(digestOf(std::string(63, 'a')) ==
		"7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
	CHECK(digestOf(std::string(64, 'a')) ==
		"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("Sha256 is chunk-size independent", "[unit][sha256]")
{
	// the property a streamed download relies on: the file arrives in
	// whatever pieces the disk hands over, and the digest may not care
	std::string message;
	for (int index = 0; index < 5000; ++index)
	{
		message += static_cast<char>('a' + (index % 26));
	}
	const String whole = digestOf(message);

	const std::size_t chunkSizes[] = { 1, 7, 63, 64, 65, 128, 1023, 4096 };
	for (std::size_t at = 0; at < sizeof(chunkSizes) / sizeof(chunkSizes[0]);
		++at)
	{
		Sha256 context;
		std::size_t offset = 0;
		while (offset < message.size())
		{
			const std::size_t take =
				std::min(chunkSizes[at], message.size() - offset);
			context.update(message.data() + offset, take);
			offset += take;
		}
		CHECK(context.finishHex() == whole);
	}
}

TEST_CASE("Sha256 is reusable after finishing", "[unit][sha256]")
{
	Sha256 context;
	context.update("abc", 3);
	CHECK(context.finishHex() == digestOf("abc"));
	// finishing closes the message and starts a fresh one
	context.update("abc", 3);
	CHECK(context.finishHex() == digestOf("abc"));
}

TEST_CASE("Sha256 compares digests, not strings", "[unit][sha256]")
{
	const String lower = digestOf("abc");
	String upper;
	for (std::size_t index = 0; index < lower.size(); ++index)
	{
		upper += static_cast<char>(std::toupper(
			static_cast<unsigned char>(lower[index])));
	}
	CHECK(Sha256::hexEquals(lower, upper));
	CHECK(Sha256::hexEquals("  " + lower + "\n", lower));
	CHECK_FALSE(Sha256::hexEquals(lower, digestOf("abd")));

	// anything that is not a complete digest matches NOTHING - the only safe
	// answer when what is being checked is a download
	CHECK_FALSE(Sha256::hexEquals(lower, ""));
	CHECK_FALSE(Sha256::hexEquals("", ""));
	CHECK_FALSE(Sha256::hexEquals(lower, lower.substr(0, 63)));
	CHECK_FALSE(Sha256::hexEquals(lower, lower + "0"));
	CHECK_FALSE(Sha256::hexEquals(lower,
		"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));
}
