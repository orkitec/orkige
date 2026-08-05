/********************************************************************
	created:	Saturday 2026/07/12 at 17:00
	filename: 	PngWriter.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file PngWriter.cpp
//! @brief the minimal PNG encoder (@see PngWriter.h)

#include "core_util/PngWriter.h"

#include <cstdint>
#include <fstream>

#include <zlib.h>

namespace Orkige
{
	namespace
	{
		//! append a big-endian 32-bit value (PNG chunk lengths + IHDR fields)
		void putU32BE(std::vector<unsigned char> & out, std::uint32_t v)
		{
			out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
			out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
			out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
			out.push_back(static_cast<unsigned char>(v & 0xFF));
		}

		//! write one PNG chunk (length + type + data + CRC of type|data);
		//! the CRC is zlib's crc32 (the PNG/zip polynomial)
		void putChunk(std::vector<unsigned char> & out, char const type[4],
			std::vector<unsigned char> const & data)
		{
			putU32BE(out, static_cast<std::uint32_t>(data.size()));
			const std::size_t typeStart = out.size();
			for (int i = 0; i < 4; ++i)
			{
				out.push_back(static_cast<unsigned char>(type[i]));
			}
			out.insert(out.end(), data.begin(), data.end());
			const std::uint32_t crc = static_cast<std::uint32_t>(
				::crc32(0uL, out.data() + typeStart,
					static_cast<uInt>(4 + data.size())));
			putU32BE(out, crc);
		}
	}

	//---------------------------------------------------------
	bool PngWriter::encode(unsigned char const * rgba, int width, int height,
		std::vector<unsigned char> & out)
	{
		if (!rgba || width <= 0 || height <= 0)
		{
			return false;
		}
		// on any later failure the appended prefix is rewound, so a false
		// return leaves out exactly as it arrived
		const std::size_t outStart = out.size();
		// PNG signature
		static const unsigned char SIGNATURE[8] =
			{ 137, 80, 78, 71, 13, 10, 26, 10 };
		out.insert(out.end(), SIGNATURE, SIGNATURE + 8);

		// IHDR: 8-bit RGBA (colour type 6), no interlace
		std::vector<unsigned char> ihdr;
		putU32BE(ihdr, static_cast<std::uint32_t>(width));
		putU32BE(ihdr, static_cast<std::uint32_t>(height));
		ihdr.push_back(8);		// bit depth
		ihdr.push_back(6);		// colour type: truecolour + alpha
		ihdr.push_back(0);		// compression: deflate
		ihdr.push_back(0);		// filter: adaptive (only the None filter is used)
		ihdr.push_back(0);		// interlace: none
		putChunk(out, "IHDR", ihdr);

		// raw image data: each scanline prefixed with a filter byte (0 = None)
		const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
		std::vector<unsigned char> raw;
		raw.reserve((rowBytes + 1) * static_cast<std::size_t>(height));
		for (int y = 0; y < height; ++y)
		{
			raw.push_back(0);	// None filter
			unsigned char const * row = rgba +
				static_cast<std::size_t>(y) * rowBytes;
			raw.insert(raw.end(), row, row + rowBytes);
		}

		// the compressed image stream: zlib's compress2 emits the complete
		// zlib wrapper (header, DEFLATE blocks, Adler-32 trailer), which is
		// exactly what a PNG IDAT chunk carries. The screenshot suites write
		// full-resolution frames through this encoder, so real compression is
		// a requirement, not a nicety - an uncompressed stream turns a flat
		// test capture from kilobytes into megabytes
		uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
		std::vector<unsigned char> idat(compressedSize);
		if (compress2(idat.data(), &compressedSize, raw.data(),
			static_cast<uLong>(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK)
		{
			out.resize(outStart);
			return false;
		}
		idat.resize(compressedSize);
		putChunk(out, "IDAT", idat);

		putChunk(out, "IEND", std::vector<unsigned char>());
		return true;
	}

	//---------------------------------------------------------
	bool PngWriter::writeFile(String const & path, unsigned char const * rgba,
		int width, int height)
	{
		std::vector<unsigned char> bytes;
		if (!encode(rgba, width, height, bytes))
		{
			return false;
		}
		std::ofstream file(path.c_str(), std::ios::binary);
		if (!file)
		{
			return false;
		}
		file.write(reinterpret_cast<char const *>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		return file.good();
	}
}
