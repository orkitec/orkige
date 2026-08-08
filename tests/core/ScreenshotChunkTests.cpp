/**************************************************************
	created:	2026/08/08 at 11:00
	filename: 	ScreenshotChunkTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/DebugProtocol.h>
#include <core_debugnet/ScreenshotChunks.h>
#include <core_util/Base64.h>

#include <cstddef>
#include <string>
#include <vector>

using Orkige::Base64;
using Orkige::ScreenshotChunkAssembler;
using Orkige::splitScreenshotChunks;

namespace
{
	//! a deterministic pseudo-image of the given size (never uniform, so a
	//! truncation shows up as a byte mismatch rather than a lucky match)
	std::vector<unsigned char> makeImage(std::size_t length)
	{
		std::vector<unsigned char> bytes(length);
		for (std::size_t i = 0; i < length; ++i)
		{
			bytes[i] = static_cast<unsigned char>((i * 37u + (i >> 8)) & 0xFF);
		}
		return bytes;
	}
	//! feed a whole chunk list; returns the final result and fills outError
	ScreenshotChunkAssembler::Result feedAll(ScreenshotChunkAssembler & assembler,
		Orkige::String const & path, Orkige::StringVector const & chunks,
		Orkige::String & outError)
	{
		ScreenshotChunkAssembler::Result result =
			ScreenshotChunkAssembler::Result::NeedMore;
		for (std::size_t i = 0; i < chunks.size(); ++i)
		{
			result = assembler.addChunk(path, static_cast<unsigned int>(i),
				static_cast<unsigned int>(chunks.size()), chunks[i], outError);
			if (result == ScreenshotChunkAssembler::Result::Failed)
			{
				break;
			}
		}
		return result;
	}
}

TEST_CASE("base64 round-trips every tail length", "[unit][base64]")
{
	for (std::size_t length = 0; length < 64; ++length)
	{
		const std::vector<unsigned char> bytes = makeImage(length);
		const Orkige::String text =
			Base64::encode(bytes.empty() ? NULL : bytes.data(), bytes.size());
		REQUIRE((text.size() % 4) == 0);
		std::vector<unsigned char> decoded;
		REQUIRE(Base64::decode(text, decoded));
		REQUIRE(decoded == bytes);
	}
}

TEST_CASE("base64 decoding is strict", "[unit][base64]")
{
	std::vector<unsigned char> out;
	// a length that is not a whole number of quanta
	REQUIRE_FALSE(Base64::decode("AAA", out));
	// characters outside the alphabet - whitespace and newlines included
	REQUIRE_FALSE(Base64::decode("AA A=", out));
	REQUIRE_FALSE(Base64::decode("AA\nA=", out));
	REQUIRE_FALSE(Base64::decode("A-A=", out));
	// padding in the wrong place
	REQUIRE_FALSE(Base64::decode("A===", out));
	REQUIRE_FALSE(Base64::decode("A=AA", out));
	REQUIRE_FALSE(Base64::decode("AA==AAAA", out));
	// the honest empty case
	REQUIRE(Base64::decode("", out));
	REQUIRE(out.empty());
}

TEST_CASE("a screenshot sequence reassembles byte-exactly",
	"[unit][screenshot_chunks]")
{
	// deliberately several chunks long, with a partial tail quantum
	const std::vector<unsigned char> image =
		makeImage(Orkige::screenshotChunkPayloadChars() * 2u + 517u);
	const Orkige::StringVector chunks =
		splitScreenshotChunks(image.data(), image.size());
	REQUIRE(chunks.size() > 2u);
	for (std::size_t i = 0; i + 1 < chunks.size(); ++i)
	{
		// every chunk but the last is a whole quantum run, so no padding
		REQUIRE(chunks[i].size() == Orkige::screenshotChunkPayloadChars());
		REQUIRE(chunks[i].find('=') == Orkige::String::npos);
	}
	// each line has to fit the transport's cap with room for the envelope
	REQUIRE(chunks[0].size() < Orkige::DebugProtocol::MAX_LINE_LENGTH);

	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	REQUIRE(feedAll(assembler, "/tmp/frame.png", chunks, error) ==
		ScreenshotChunkAssembler::Result::Complete);
	REQUIRE(error.empty());
	REQUIRE(assembler.bytes() == image);
	REQUIRE(assembler.path() == "/tmp/frame.png");
	REQUIRE_FALSE(assembler.inProgress());
}

TEST_CASE("a single-chunk sequence completes on its first chunk",
	"[unit][screenshot_chunks]")
{
	const std::vector<unsigned char> image = makeImage(97);
	const Orkige::StringVector chunks =
		splitScreenshotChunks(image.data(), image.size());
	REQUIRE(chunks.size() == 1u);
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	REQUIRE(assembler.addChunk("f.png", 0, 1, chunks[0], error) ==
		ScreenshotChunkAssembler::Result::Complete);
	REQUIRE(assembler.bytes() == image);
}

TEST_CASE("an empty image produces no chunks at all",
	"[unit][screenshot_chunks]")
{
	REQUIRE(splitScreenshotChunks(NULL, 0).empty());
	const std::vector<unsigned char> nothing;
	REQUIRE(splitScreenshotChunks(nothing.data(), 0).empty());
}

TEST_CASE("a reordered sequence fails closed", "[unit][screenshot_chunks]")
{
	const std::vector<unsigned char> image =
		makeImage(Orkige::screenshotChunkPayloadChars() * 2u);
	const Orkige::StringVector chunks =
		splitScreenshotChunks(image.data(), image.size());
	REQUIRE(chunks.size() >= 3u);
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	const unsigned int total = static_cast<unsigned int>(chunks.size());
	REQUIRE(assembler.addChunk("f.png", 0, total, chunks[0], error) ==
		ScreenshotChunkAssembler::Result::NeedMore);
	// chunk 2 where chunk 1 was expected
	REQUIRE(assembler.addChunk("f.png", 2, total, chunks[2], error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("out of order") != Orkige::String::npos);
	REQUIRE(assembler.bytes().empty());
	REQUIRE_FALSE(assembler.inProgress());
}

TEST_CASE("a duplicated chunk fails closed", "[unit][screenshot_chunks]")
{
	const std::vector<unsigned char> image =
		makeImage(Orkige::screenshotChunkPayloadChars() * 2u);
	const Orkige::StringVector chunks =
		splitScreenshotChunks(image.data(), image.size());
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	const unsigned int total = static_cast<unsigned int>(chunks.size());
	REQUIRE(assembler.addChunk("f.png", 0, total, chunks[0], error) ==
		ScreenshotChunkAssembler::Result::NeedMore);
	REQUIRE(assembler.addChunk("f.png", 0, total, chunks[0], error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("out of order") != Orkige::String::npos);
	REQUIRE(assembler.bytes().empty());
}

TEST_CASE("a truncated sequence never completes", "[unit][screenshot_chunks]")
{
	const std::vector<unsigned char> image =
		makeImage(Orkige::screenshotChunkPayloadChars() * 2u);
	const Orkige::StringVector chunks =
		splitScreenshotChunks(image.data(), image.size());
	REQUIRE(chunks.size() >= 3u);
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	const unsigned int total = static_cast<unsigned int>(chunks.size());
	for (std::size_t i = 0; i + 1 < chunks.size(); ++i)
	{
		REQUIRE(assembler.addChunk("f.png", static_cast<unsigned int>(i), total,
			chunks[i], error) == ScreenshotChunkAssembler::Result::NeedMore);
	}
	// the tail never arrives: the transfer stays open and hands out nothing
	REQUIRE(assembler.inProgress());
	REQUIRE(assembler.bytes().empty() == false);	// buffered, not delivered
	// a stray tail from a LATER sequence cannot close this one either
	REQUIRE(assembler.addChunk("f.png", total, total, chunks[0], error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("outside its sequence") != Orkige::String::npos);
}

TEST_CASE("a tail with no sequence in progress is refused",
	"[unit][screenshot_chunks]")
{
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	REQUIRE(assembler.addChunk("f.png", 3, 8, "AAAA", error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("no sequence in progress") != Orkige::String::npos);
}

TEST_CASE("a sequence that changes shape mid transfer is refused",
	"[unit][screenshot_chunks]")
{
	Orkige::String error;
	{
		ScreenshotChunkAssembler assembler;
		REQUIRE(assembler.addChunk("f.png", 0, 3, "AAAA", error) ==
			ScreenshotChunkAssembler::Result::NeedMore);
		REQUIRE(assembler.addChunk("f.png", 1, 4, "AAAA", error) ==
			ScreenshotChunkAssembler::Result::Failed);
		REQUIRE(error.find("length changed") != Orkige::String::npos);
	}
	{
		ScreenshotChunkAssembler assembler;
		REQUIRE(assembler.addChunk("f.png", 0, 3, "AAAA", error) ==
			ScreenshotChunkAssembler::Result::NeedMore);
		REQUIRE(assembler.addChunk("other.png", 1, 3, "AAAA", error) ==
			ScreenshotChunkAssembler::Result::Failed);
		REQUIRE(error.find("path changed") != Orkige::String::npos);
	}
}

TEST_CASE("a chunk that is not base64 is refused", "[unit][screenshot_chunks]")
{
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	REQUIRE(assembler.addChunk("f.png", 0, 2, "not base64!!", error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("not valid base64") != Orkige::String::npos);
	REQUIRE(assembler.bytes().empty());
}

TEST_CASE("an absurd or empty sequence claim is refused",
	"[unit][screenshot_chunks]")
{
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	// zero-length sequence
	REQUIRE(assembler.addChunk("f.png", 0, 0, "AAAA", error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("zero-length") != Orkige::String::npos);
	// an index outside its own sequence
	REQUIRE(assembler.addChunk("f.png", 5, 2, "AAAA", error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("outside its sequence") != Orkige::String::npos);
	// more chunks than the image ceiling could ever need
	const unsigned int absurd = 0xFFFFFFFFu;
	REQUIRE(assembler.addChunk("f.png", 0, absurd, "AAAA", error) ==
		ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("too long") != Orkige::String::npos);
}

TEST_CASE("a sequence carrying more bytes than the ceiling is refused",
	"[unit][screenshot_chunks]")
{
	// the longest sequence the assembler accepts, every chunk at full size:
	// the byte total passes the ceiling on the very last one, and the honest
	// refusal is by SIZE, before the buffer can grow past it
	const std::size_t payloadBytes =
		(Orkige::screenshotChunkPayloadChars() / 4u) * 3u;
	const unsigned int total = static_cast<unsigned int>(
		(Orkige::screenshotMaxImageBytes() + payloadBytes - 1u) / payloadBytes);
	const std::vector<unsigned char> block = makeImage(payloadBytes);
	const Orkige::String payload =
		Base64::encode(block.data(), block.size());
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	ScreenshotChunkAssembler::Result result =
		ScreenshotChunkAssembler::Result::NeedMore;
	for (unsigned int i = 0; i < total; ++i)
	{
		result = assembler.addChunk("f.png", i, total, payload, error);
		if (result != ScreenshotChunkAssembler::Result::NeedMore)
		{
			break;
		}
	}
	REQUIRE(result == ScreenshotChunkAssembler::Result::Failed);
	REQUIRE(error.find("transfer limit") != Orkige::String::npos);
	REQUIRE(assembler.bytes().empty());
}

TEST_CASE("a failed sequence resets to accept the next one",
	"[unit][screenshot_chunks]")
{
	ScreenshotChunkAssembler assembler;
	Orkige::String error;
	REQUIRE(assembler.addChunk("f.png", 0, 2, "AAAA", error) ==
		ScreenshotChunkAssembler::Result::NeedMore);
	REQUIRE(assembler.addChunk("f.png", 0, 2, "AAAA", error) ==
		ScreenshotChunkAssembler::Result::Failed);
	// a fresh capture starts clean at chunk 0
	const std::vector<unsigned char> image = makeImage(64);
	const Orkige::StringVector chunks =
		splitScreenshotChunks(image.data(), image.size());
	REQUIRE(feedAll(assembler, "next.png", chunks, error) ==
		ScreenshotChunkAssembler::Result::Complete);
	REQUIRE(assembler.bytes() == image);
	REQUIRE(assembler.path() == "next.png");
}

TEST_CASE("a chunk carries the protocol's own field names",
	"[unit][screenshot_chunks]")
{
	// the wire shape both ends build against, encoded and read back through
	// the transport's own codec
	Orkige::DebugMessage chunk(Orkige::DebugProtocol::MSG_SCREENSHOT_DATA);
	chunk.set(Orkige::DebugProtocol::FIELD_PATH, "/tmp/frame.png");
	chunk.set(Orkige::DebugProtocol::FIELD_SEQ, "0");
	chunk.set(Orkige::DebugProtocol::FIELD_TOTAL, "1");
	chunk.set(Orkige::DebugProtocol::FIELD_DATA, "AAAA");
	Orkige::DebugMessage decoded;
	REQUIRE(Orkige::DebugMessage::decode(chunk.encode(), decoded));
	REQUIRE(decoded.type == "screenshot_data");
	REQUIRE(decoded.get(Orkige::DebugProtocol::FIELD_SEQ) == "0");
	REQUIRE(decoded.get(Orkige::DebugProtocol::FIELD_TOTAL) == "1");
	REQUIRE(decoded.get(Orkige::DebugProtocol::FIELD_DATA) == "AAAA");
}
