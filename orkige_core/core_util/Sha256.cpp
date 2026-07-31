/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	Sha256.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Sha256 - FIPS 180-4 SHA-256 (see the header for why it exists).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "core_util/Sha256.h"

#include "core_util/ConstantTimeCompare.h"

#include <cctype>
#include <cstring>

namespace Orkige
{
	namespace
	{
		//! the round constants: the first 32 bits of the fractional parts of
		//! the cube roots of the first 64 primes (FIPS 180-4, 4.2.2)
		const unsigned int SHA256_K[64] = {
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
			0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
			0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
			0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
			0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
			0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
			0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
			0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
			0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
		};

		inline unsigned int rotateRight(unsigned int value, unsigned int bits)
		{
			return (value >> bits) | (value << (32u - bits));
		}

		//! is @p character a hex digit (the digest alphabet)
		inline bool isHexDigit(char character)
		{
			const unsigned char raw = static_cast<unsigned char>(character);
			return (raw >= '0' && raw <= '9') || (raw >= 'a' && raw <= 'f') ||
				(raw >= 'A' && raw <= 'F');
		}
	}
	//---------------------------------------------------------
	const std::size_t Sha256::DIGEST_BYTES;
	const std::size_t Sha256::HEX_LENGTH;
	//---------------------------------------------------------
	Sha256::Sha256()
	{
		this->reset();
	}
	//---------------------------------------------------------
	void Sha256::reset()
	{
		// the first 32 bits of the fractional parts of the square roots of
		// the first eight primes (FIPS 180-4, 5.3.3)
		this->mState[0] = 0x6a09e667u;
		this->mState[1] = 0xbb67ae85u;
		this->mState[2] = 0x3c6ef372u;
		this->mState[3] = 0xa54ff53au;
		this->mState[4] = 0x510e527fu;
		this->mState[5] = 0x9b05688cu;
		this->mState[6] = 0x1f83d9abu;
		this->mState[7] = 0x5be0cd19u;
		this->mBitLength = 0;
		this->mBuffered = 0;
		std::memset(this->mBlock, 0, sizeof(this->mBlock));
	}
	//---------------------------------------------------------
	void Sha256::transform(unsigned char const * block)
	{
		unsigned int schedule[64];
		for (std::size_t index = 0; index < 16; ++index)
		{
			schedule[index] =
				(static_cast<unsigned int>(block[index * 4 + 0]) << 24) |
				(static_cast<unsigned int>(block[index * 4 + 1]) << 16) |
				(static_cast<unsigned int>(block[index * 4 + 2]) << 8) |
				(static_cast<unsigned int>(block[index * 4 + 3]));
		}
		for (std::size_t index = 16; index < 64; ++index)
		{
			const unsigned int previous = schedule[index - 15];
			const unsigned int recent = schedule[index - 2];
			const unsigned int sigma0 = rotateRight(previous, 7) ^
				rotateRight(previous, 18) ^ (previous >> 3);
			const unsigned int sigma1 = rotateRight(recent, 17) ^
				rotateRight(recent, 19) ^ (recent >> 10);
			schedule[index] = schedule[index - 16] + sigma0 +
				schedule[index - 7] + sigma1;
		}

		unsigned int a = this->mState[0];
		unsigned int b = this->mState[1];
		unsigned int c = this->mState[2];
		unsigned int d = this->mState[3];
		unsigned int e = this->mState[4];
		unsigned int f = this->mState[5];
		unsigned int g = this->mState[6];
		unsigned int h = this->mState[7];
		for (std::size_t round = 0; round < 64; ++round)
		{
			const unsigned int sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^
				rotateRight(e, 25);
			const unsigned int choose = (e & f) ^ ((~e) & g);
			const unsigned int temp1 = h + sum1 + choose + SHA256_K[round] +
				schedule[round];
			const unsigned int sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^
				rotateRight(a, 22);
			const unsigned int majority = (a & b) ^ (a & c) ^ (b & c);
			const unsigned int temp2 = sum0 + majority;
			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}
		this->mState[0] += a;
		this->mState[1] += b;
		this->mState[2] += c;
		this->mState[3] += d;
		this->mState[4] += e;
		this->mState[5] += f;
		this->mState[6] += g;
		this->mState[7] += h;
	}
	//---------------------------------------------------------
	void Sha256::update(void const * data, std::size_t length)
	{
		if (data == NULL || length == 0)
		{
			return;
		}
		unsigned char const * bytes = static_cast<unsigned char const *>(data);
		this->mBitLength += static_cast<unsigned long long>(length) * 8ull;
		// top up the partial block first, then run whole blocks straight out
		// of the caller's buffer (a streamed file never gets copied twice)
		if (this->mBuffered > 0)
		{
			const std::size_t room = 64 - this->mBuffered;
			const std::size_t take = (length < room) ? length : room;
			std::memcpy(this->mBlock + this->mBuffered, bytes, take);
			this->mBuffered += take;
			bytes += take;
			length -= take;
			if (this->mBuffered == 64)
			{
				this->transform(this->mBlock);
				this->mBuffered = 0;
			}
		}
		while (length >= 64)
		{
			this->transform(bytes);
			bytes += 64;
			length -= 64;
		}
		if (length > 0)
		{
			std::memcpy(this->mBlock, bytes, length);
			this->mBuffered = length;
		}
	}
	//---------------------------------------------------------
	void Sha256::finish(unsigned char outDigest[DIGEST_BYTES])
	{
		const unsigned long long bitLength = this->mBitLength;
		// the padding, applied to the partial block directly (never back
		// through update(), which would count the padding as message bytes):
		// a 0x80 byte, zeroes up to offset 56, then the message length as a
		// big-endian 64-bit count of BITS (FIPS 180-4, 5.1.1)
		this->mBlock[this->mBuffered] = 0x80;
		++this->mBuffered;
		if (this->mBuffered > 56)
		{
			std::memset(this->mBlock + this->mBuffered, 0,
				64 - this->mBuffered);
			this->transform(this->mBlock);
			this->mBuffered = 0;
		}
		std::memset(this->mBlock + this->mBuffered, 0, 56 - this->mBuffered);
		for (std::size_t index = 0; index < 8; ++index)
		{
			this->mBlock[63 - index] =
				static_cast<unsigned char>((bitLength >> (index * 8)) & 0xffull);
		}
		this->transform(this->mBlock);

		for (std::size_t word = 0; word < 8; ++word)
		{
			outDigest[word * 4 + 0] =
				static_cast<unsigned char>((this->mState[word] >> 24) & 0xffu);
			outDigest[word * 4 + 1] =
				static_cast<unsigned char>((this->mState[word] >> 16) & 0xffu);
			outDigest[word * 4 + 2] =
				static_cast<unsigned char>((this->mState[word] >> 8) & 0xffu);
			outDigest[word * 4 + 3] =
				static_cast<unsigned char>(this->mState[word] & 0xffu);
		}
		this->reset();
	}
	//---------------------------------------------------------
	String Sha256::finishHex()
	{
		unsigned char digest[DIGEST_BYTES];
		this->finish(digest);
		static const char HEX[] = "0123456789abcdef";
		String text;
		text.reserve(HEX_LENGTH);
		for (std::size_t index = 0; index < DIGEST_BYTES; ++index)
		{
			text += HEX[(digest[index] >> 4) & 0x0f];
			text += HEX[digest[index] & 0x0f];
		}
		return text;
	}
	//---------------------------------------------------------
	String Sha256::hexDigest(void const * data, std::size_t length)
	{
		Sha256 context;
		context.update(data, length);
		return context.finishHex();
	}
	//---------------------------------------------------------
	bool Sha256::hexEquals(String const & left, String const & right)
	{
		// normalise: trim surrounding whitespace, lower-case, and demand a
		// complete digest on BOTH sides. Anything else is not a digest, and
		// "not a digest" must never compare equal to one.
		String normalised[2];
		String const * inputs[2] = { &left, &right };
		for (std::size_t side = 0; side < 2; ++side)
		{
			String const & source = *inputs[side];
			std::size_t begin = 0;
			std::size_t end = source.size();
			while (begin < end && std::isspace(
				static_cast<unsigned char>(source[begin])) != 0)
			{
				++begin;
			}
			while (end > begin && std::isspace(
				static_cast<unsigned char>(source[end - 1])) != 0)
			{
				--end;
			}
			if (end - begin != HEX_LENGTH)
			{
				return false;
			}
			normalised[side].reserve(HEX_LENGTH);
			for (std::size_t index = begin; index < end; ++index)
			{
				if (!isHexDigit(source[index]))
				{
					return false;
				}
				normalised[side] += static_cast<char>(std::tolower(
					static_cast<unsigned char>(source[index])));
			}
		}
		return constantTimeEquals(normalised[0], normalised[1]);
	}
	//---------------------------------------------------------
}
