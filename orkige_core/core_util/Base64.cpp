/********************************************************************
	created:	Saturday 2026/08/08 at 10:00
	filename: 	Base64.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/Base64.h"

namespace Orkige
{
	namespace
	{
		const char ALPHABET[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
			"0123456789+/";
		//---------------------------------------------------------
		//! the alphabet index of c, or -1 when c is not a base64 character
		int decodeCharacter(char c)
		{
			if (c >= 'A' && c <= 'Z')
			{
				return c - 'A';
			}
			if (c >= 'a' && c <= 'z')
			{
				return c - 'a' + 26;
			}
			if (c >= '0' && c <= '9')
			{
				return c - '0' + 52;
			}
			if (c == '+')
			{
				return 62;
			}
			if (c == '/')
			{
				return 63;
			}
			return -1;
		}
	}
	//---------------------------------------------------------
	String Base64::encode(unsigned char const * data, std::size_t length)
	{
		String out;
		if (data == NULL || length == 0)
		{
			return out;
		}
		out.reserve(((length + 2) / 3) * 4);
		for (std::size_t i = 0; i < length; i += 3)
		{
			const unsigned int byte0 = data[i];
			const unsigned int byte1 = i + 1 < length ? data[i + 1] : 0u;
			const unsigned int byte2 = i + 2 < length ? data[i + 2] : 0u;
			const unsigned int triple = (byte0 << 16) | (byte1 << 8) | byte2;
			out += ALPHABET[(triple >> 18) & 0x3F];
			out += ALPHABET[(triple >> 12) & 0x3F];
			out += i + 1 < length ? ALPHABET[(triple >> 6) & 0x3F] : '=';
			out += i + 2 < length ? ALPHABET[triple & 0x3F] : '=';
		}
		return out;
	}
	//---------------------------------------------------------
	bool Base64::decode(String const & text,
		std::vector<unsigned char> & outBytes)
	{
		outBytes.clear();
		if (text.empty())
		{
			return true;
		}
		if ((text.size() % 4) != 0)
		{
			return false;
		}
		outBytes.reserve((text.size() / 4) * 3);
		for (std::size_t i = 0; i < text.size(); i += 4)
		{
			int values[4] = { 0, 0, 0, 0 };
			int padding = 0;
			for (int slot = 0; slot < 4; ++slot)
			{
				const char c = text[i + slot];
				if (c == '=')
				{
					// padding is legal only in the LAST quantum, and only in
					// its last two slots ("xx==" or "xxx=")
					if (i + 4 != text.size() || slot < 2)
					{
						return false;
					}
					++padding;
					values[slot] = 0;
					continue;
				}
				if (padding != 0)
				{
					// a data character after a pad character
					return false;
				}
				const int value = decodeCharacter(c);
				if (value < 0)
				{
					return false;
				}
				values[slot] = value;
			}
			const unsigned int triple =
				(static_cast<unsigned int>(values[0]) << 18) |
				(static_cast<unsigned int>(values[1]) << 12) |
				(static_cast<unsigned int>(values[2]) << 6) |
				static_cast<unsigned int>(values[3]);
			outBytes.push_back(static_cast<unsigned char>((triple >> 16) & 0xFF));
			if (padding < 2)
			{
				outBytes.push_back(
					static_cast<unsigned char>((triple >> 8) & 0xFF));
			}
			if (padding < 1)
			{
				outBytes.push_back(static_cast<unsigned char>(triple & 0xFF));
			}
		}
		return true;
	}
}
