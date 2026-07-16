/********************************************************************
	created:	Thursday 2026/07/16 at 15:00
	filename: 	Sha1.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/Sha1.h"

#include <cstdint>
#include <fstream>
#include <vector>

namespace Orkige
{
	//---------------------------------------------------------
	void Sha1::digest(unsigned char const * data, std::size_t length,
		unsigned char outDigest[20])
	{
		std::uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu,
			0x10325476u, 0xC3D2E1F0u };
		// message + 0x80 pad + zeros + 64-bit big-endian bit length,
		// processed in 64-byte blocks
		const std::uint64_t bitLength =
			static_cast<std::uint64_t>(length) * 8u;
		std::size_t paddedLength = length + 1;
		while (paddedLength % 64 != 56)
		{
			++paddedLength;
		}
		paddedLength += 8;
		for (std::size_t blockStart = 0; blockStart < paddedLength;
			blockStart += 64)
		{
			unsigned char block[64];
			for (std::size_t i = 0; i < 64; ++i)
			{
				const std::size_t at = blockStart + i;
				if (at < length)
				{
					block[i] = data[at];
				}
				else if (at == length)
				{
					block[i] = 0x80;
				}
				else if (at >= paddedLength - 8)
				{
					const int shift = static_cast<int>(
						(paddedLength - 1 - at) * 8);
					block[i] = static_cast<unsigned char>(
						(bitLength >> shift) & 0xFFu);
				}
				else
				{
					block[i] = 0;
				}
			}
			std::uint32_t w[80];
			for (int i = 0; i < 16; ++i)
			{
				w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
					(static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
					(static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
					static_cast<std::uint32_t>(block[i * 4 + 3]);
			}
			for (int i = 16; i < 80; ++i)
			{
				const std::uint32_t value =
					w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
				w[i] = (value << 1) | (value >> 31);
			}
			std::uint32_t a = h[0];
			std::uint32_t b = h[1];
			std::uint32_t c = h[2];
			std::uint32_t d = h[3];
			std::uint32_t e = h[4];
			for (int i = 0; i < 80; ++i)
			{
				std::uint32_t f;
				std::uint32_t k;
				if (i < 20)
				{
					f = (b & c) | ((~b) & d);
					k = 0x5A827999u;
				}
				else if (i < 40)
				{
					f = b ^ c ^ d;
					k = 0x6ED9EBA1u;
				}
				else if (i < 60)
				{
					f = (b & c) | (b & d) | (c & d);
					k = 0x8F1BBCDCu;
				}
				else
				{
					f = b ^ c ^ d;
					k = 0xCA62C1D6u;
				}
				const std::uint32_t temp =
					((a << 5) | (a >> 27)) + f + e + k + w[i];
				e = d;
				d = c;
				c = (b << 30) | (b >> 2);
				b = a;
				a = temp;
			}
			h[0] += a;
			h[1] += b;
			h[2] += c;
			h[3] += d;
			h[4] += e;
		}
		for (int i = 0; i < 5; ++i)
		{
			outDigest[i * 4] = static_cast<unsigned char>(h[i] >> 24);
			outDigest[i * 4 + 1] =
				static_cast<unsigned char>((h[i] >> 16) & 0xFFu);
			outDigest[i * 4 + 2] =
				static_cast<unsigned char>((h[i] >> 8) & 0xFFu);
			outDigest[i * 4 + 3] =
				static_cast<unsigned char>(h[i] & 0xFFu);
		}
	}
	//---------------------------------------------------------
	String Sha1::hexDigest(void const * data, std::size_t length)
	{
		unsigned char raw[20];
		digest(static_cast<unsigned char const *>(data), length, raw);
		static const char HEX[] = "0123456789abcdef";
		String out;
		out.reserve(40);
		for (unsigned char byte : raw)
		{
			out += HEX[byte >> 4];
			out += HEX[byte & 0x0F];
		}
		return out;
	}
	//---------------------------------------------------------
	String Sha1::hexDigestOfFile(String const & filePath)
	{
		std::ifstream in(filePath, std::ios::binary);
		if (!in)
		{
			return String();
		}
		std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		if (in.bad())
		{
			return String();
		}
		return hexDigest(bytes.empty() ? "" : bytes.data(), bytes.size());
	}
	//---------------------------------------------------------
}
