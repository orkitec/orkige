// PngWriterTests.cpp - the minimal dependency-free PNG encoder: a valid 8-bit
// RGBA stream (signature + IHDR carrying the dimensions + IDAT + IEND), a
// correct IDAT CRC, and honest refusal of bad arguments. The encoder is what
// lets the CPU vector-animation preview write a PNG headlessly.
#include <catch2/catch_test_macros.hpp>

#include "core_util/PngWriter.h"

#include <cstdint>
#include <vector>

using Orkige::PngWriter;

namespace
{
	//! big-endian u32 at offset (PNG chunk lengths / IHDR fields)
	std::uint32_t readU32BE(std::vector<unsigned char> const& b, std::size_t o)
	{
		return (static_cast<std::uint32_t>(b[o]) << 24) |
			(static_cast<std::uint32_t>(b[o + 1]) << 16) |
			(static_cast<std::uint32_t>(b[o + 2]) << 8) |
			static_cast<std::uint32_t>(b[o + 3]);
	}

	//! the reference CRC-32 (PNG polynomial) over a byte span
	std::uint32_t crc32(unsigned char const* data, std::size_t len)
	{
		std::uint32_t crc = 0xFFFFFFFFu;
		for (std::size_t i = 0; i < len; ++i)
		{
			crc ^= data[i];
			for (int k = 0; k < 8; ++k)
			{
				crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
			}
		}
		return crc ^ 0xFFFFFFFFu;
	}
}

TEST_CASE("PngWriter encodes a valid RGBA PNG header", "[unit][png]")
{
	// a 3x2 image, arbitrary pixels
	const int w = 3, h = 2;
	std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 4);
	for (std::size_t i = 0; i < pixels.size(); ++i)
	{
		pixels[i] = static_cast<unsigned char>((i * 37) & 0xFF);
	}
	std::vector<unsigned char> png;
	REQUIRE(PngWriter::encode(pixels.data(), w, h, png));

	// signature
	const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	REQUIRE(png.size() > 8);
	for (int i = 0; i < 8; ++i)
	{
		REQUIRE(png[i] == sig[i]);
	}
	// first chunk is IHDR (length 13) carrying the dimensions + RGBA type
	REQUIRE(readU32BE(png, 8) == 13u);
	REQUIRE(png[12] == 'I');
	REQUIRE(png[13] == 'H');
	REQUIRE(png[14] == 'D');
	REQUIRE(png[15] == 'R');
	REQUIRE(readU32BE(png, 16) == static_cast<std::uint32_t>(w));
	REQUIRE(readU32BE(png, 20) == static_cast<std::uint32_t>(h));
	REQUIRE(png[24] == 8);	// bit depth
	REQUIRE(png[25] == 6);	// colour type RGBA
}

TEST_CASE("PngWriter chunk CRCs are correct and the stream ends in IEND",
	"[unit][png]")
{
	const int w = 4, h = 4;
	std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 4, 128);
	std::vector<unsigned char> png;
	REQUIRE(PngWriter::encode(pixels.data(), w, h, png));

	// walk the chunks from just after the 8-byte signature, verifying each CRC
	std::size_t pos = 8;
	bool sawIhdr = false, sawIdat = false, sawIend = false;
	while (pos + 12 <= png.size())
	{
		const std::uint32_t len = readU32BE(png, pos);
		const std::size_t typeAt = pos + 4;
		const std::uint32_t crc = readU32BE(png, typeAt + 4 + len);
		REQUIRE(crc == crc32(png.data() + typeAt, 4 + len));
		const std::string type(reinterpret_cast<char const*>(&png[typeAt]), 4);
		if (type == "IHDR") sawIhdr = true;
		if (type == "IDAT") sawIdat = true;
		if (type == "IEND") sawIend = true;
		pos = typeAt + 4 + len + 4;
	}
	REQUIRE(pos == png.size());	// chunks tile the file exactly
	REQUIRE(sawIhdr);
	REQUIRE(sawIdat);
	REQUIRE(sawIend);
}

TEST_CASE("PngWriter refuses bad arguments", "[unit][png]")
{
	std::vector<unsigned char> px(16, 0);
	std::vector<unsigned char> out;
	REQUIRE_FALSE(PngWriter::encode(nullptr, 2, 2, out));
	REQUIRE_FALSE(PngWriter::encode(px.data(), 0, 2, out));
	REQUIRE_FALSE(PngWriter::encode(px.data(), 2, -1, out));
	REQUIRE(out.empty());
}
