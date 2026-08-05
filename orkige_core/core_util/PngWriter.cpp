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

		//! CRC-32 (the PNG/zip polynomial) over a byte span, seeded per call -
		//! chunk CRCs are short, so a per-call table build stays negligible
		std::uint32_t crc32(unsigned char const * data, std::size_t length)
		{
			std::uint32_t table[256];
			for (std::uint32_t n = 0; n < 256; ++n)
			{
				std::uint32_t c = n;
				for (int k = 0; k < 8; ++k)
				{
					c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				}
				table[n] = c;
			}
			std::uint32_t crc = 0xFFFFFFFFu;
			for (std::size_t i = 0; i < length; ++i)
			{
				crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
			}
			return crc ^ 0xFFFFFFFFu;
		}

		//! Adler-32 (the zlib stream checksum) over the raw filtered scanlines
		std::uint32_t adler32(unsigned char const * data, std::size_t length)
		{
			std::uint32_t a = 1, b = 0;
			const std::uint32_t MOD = 65521u;
			for (std::size_t i = 0; i < length; ++i)
			{
				a = (a + data[i]) % MOD;
				b = (b + a) % MOD;
			}
			return (b << 16) | a;
		}

		//! write one PNG chunk (length + type + data + CRC of type|data)
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
			const std::uint32_t crc = crc32(out.data() + typeStart,
				4 + data.size());
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

		// zlib stream around DEFLATE STORED blocks: header (0x78 0x01), then
		// each <= 65535-byte block as [BFINAL/BTYPE=00][LEN][~LEN][bytes], then
		// the Adler-32 of the raw data. Correct and reader-universal; no zlib.
		std::vector<unsigned char> idat;
		idat.push_back(0x78);	// CMF: deflate, 32K window
		idat.push_back(0x01);	// FLG: no dict, fastest (check bits consistent)
		std::size_t offset = 0;
		const std::size_t total = raw.size();
		const std::size_t MAX_BLOCK = 65535;
		if (total == 0)
		{
			// a zero-length stored block still needs a final marker
			idat.push_back(0x01);
			idat.push_back(0x00);
			idat.push_back(0x00);
			idat.push_back(0xFF);
			idat.push_back(0xFF);
		}
		while (offset < total)
		{
			const std::size_t chunk =
				(total - offset < MAX_BLOCK) ? (total - offset) : MAX_BLOCK;
			const bool finalBlock = (offset + chunk >= total);
			idat.push_back(finalBlock ? 0x01 : 0x00);
			const std::uint16_t len = static_cast<std::uint16_t>(chunk);
			const std::uint16_t nlen = static_cast<std::uint16_t>(~len);
			idat.push_back(static_cast<unsigned char>(len & 0xFF));
			idat.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
			idat.push_back(static_cast<unsigned char>(nlen & 0xFF));
			idat.push_back(static_cast<unsigned char>((nlen >> 8) & 0xFF));
			idat.insert(idat.end(), raw.begin() + offset,
				raw.begin() + offset + chunk);
			offset += chunk;
		}
		const std::uint32_t adler = adler32(raw.data(), raw.size());
		putU32BE(idat, adler);
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
