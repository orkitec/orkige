/********************************************************************
	created:	Sunday 2026/07/12 at 12:00
	filename: 	PngWriterTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
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

// --- padded rows -------------------------------------------------------
// A GPU texture readback hands back rows padded to an alignment, so the
// distance between two row starts is NOT width*4. Encoding such a buffer as
// if it were packed shears the image one pad-width further left on every
// scanline, which is why the stride is an argument rather than a guess.

TEST_CASE("PngWriter drops row padding", "[unit][png]")
{
	const int w = 5, h = 4;
	const int packedRow = w * 4;
	const int paddedRow = packedRow + 13;	// a deliberately odd pad

	std::vector<unsigned char> packed(
		static_cast<std::size_t>(packedRow) * h);
	std::vector<unsigned char> padded(
		static_cast<std::size_t>(paddedRow) * h, 0xAB);	// pad = garbage
	for (int y = 0; y < h; ++y)
	{
		for (int i = 0; i < packedRow; ++i)
		{
			const unsigned char value =
				static_cast<unsigned char>((y * packedRow + i * 7) & 0xFF);
			packed[static_cast<std::size_t>(y) * packedRow + i] = value;
			padded[static_cast<std::size_t>(y) * paddedRow + i] = value;
		}
	}

	std::vector<unsigned char> fromPacked, fromPadded;
	REQUIRE(PngWriter::encode(packed.data(), w, h, fromPacked));
	REQUIRE(PngWriter::encode(padded.data(), w, h, fromPadded, paddedRow));
	// the padding is not image data: the two streams are the SAME file
	REQUIRE(fromPacked == fromPadded);
}

TEST_CASE("PngWriter treats a zero or exact stride as packed", "[unit][png]")
{
	const int w = 3, h = 3;
	std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
	for (std::size_t i = 0; i < px.size(); ++i)
	{
		px[i] = static_cast<unsigned char>((i * 11) & 0xFF);
	}
	std::vector<unsigned char> implicit, explicitStride;
	REQUIRE(PngWriter::encode(px.data(), w, h, implicit));
	REQUIRE(PngWriter::encode(px.data(), w, h, explicitStride, w * 4));
	REQUIRE(implicit == explicitStride);
}

TEST_CASE("PngWriter refuses a stride narrower than one row", "[unit][png]")
{
	const int w = 4, h = 2;
	std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4, 0);
	std::vector<unsigned char> out;
	// a stride that cannot hold a scanline describes no buffer - refusing is
	// the only safe answer, since encoding it would read past the allocation
	REQUIRE_FALSE(PngWriter::encode(px.data(), w, h, out, w * 4 - 1));
	REQUIRE(out.empty());
}

TEST_CASE("PngWriter writes the un-padded pixels, in order", "[unit][png]")
{
	// decode the encoder's own output far enough to compare PIXELS: the IDAT
	// is a zlib stream of STORED deflate blocks, so the scanlines can be
	// recovered without an inflater
	const int w = 2, h = 3;
	const int paddedRow = w * 4 + 4;
	std::vector<unsigned char> padded(
		static_cast<std::size_t>(paddedRow) * h, 0x5A);	// pad = garbage
	std::vector<unsigned char> expected;
	for (int y = 0; y < h; ++y)
	{
		for (int i = 0; i < w * 4; ++i)
		{
			const unsigned char value =
				static_cast<unsigned char>(y * 40 + i * 3 + 1);
			padded[static_cast<std::size_t>(y) * paddedRow + i] = value;
			expected.push_back(value);
		}
	}

	std::vector<unsigned char> png;
	REQUIRE(PngWriter::encode(padded.data(), w, h, png, paddedRow));

	// collect the IDAT payload
	std::vector<unsigned char> idat;
	std::size_t pos = 8;
	while (pos + 12 <= png.size())
	{
		const std::uint32_t len = readU32BE(png, pos);
		const std::size_t typeAt = pos + 4;
		const std::string type(reinterpret_cast<char const*>(&png[typeAt]), 4);
		if (type == "IDAT")
		{
			idat.insert(idat.end(), png.begin() + static_cast<long>(typeAt + 4),
				png.begin() + static_cast<long>(typeAt + 4 + len));
		}
		pos = typeAt + 4 + len + 4;
	}
	REQUIRE(idat.size() > 2);

	// unwrap the zlib header, then the stored blocks
	std::vector<unsigned char> raw;
	std::size_t at = 2;
	while (at + 5 <= idat.size())
	{
		const bool finalBlock = (idat[at] & 0x01) != 0;
		REQUIRE((idat[at] & 0x06) == 0);	// BTYPE 00 = stored
		const std::size_t len = static_cast<std::size_t>(idat[at + 1]) |
			(static_cast<std::size_t>(idat[at + 2]) << 8);
		at += 5;
		REQUIRE(at + len <= idat.size());
		raw.insert(raw.end(), idat.begin() + static_cast<long>(at),
			idat.begin() + static_cast<long>(at + len));
		at += len;
		if (finalBlock)
		{
			break;
		}
	}

	// each scanline is a filter byte (0 = None) then width*4 pixel bytes
	REQUIRE(raw.size() == static_cast<std::size_t>(h) * (1 + w * 4));
	std::vector<unsigned char> decoded;
	for (int y = 0; y < h; ++y)
	{
		const std::size_t rowAt = static_cast<std::size_t>(y) * (1 + w * 4);
		REQUIRE(raw[rowAt] == 0);
		decoded.insert(decoded.end(), raw.begin() + static_cast<long>(rowAt + 1),
			raw.begin() + static_cast<long>(rowAt + 1 + w * 4));
	}
	REQUIRE(decoded == expected);
}
