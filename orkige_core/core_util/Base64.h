/********************************************************************
	created:	Saturday 2026/08/08 at 10:00
	filename: 	Base64.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __Base64_h__8_8_2026__10_00_00__
#define __Base64_h__8_8_2026__10_00_00__

//! @file Base64.h
//! @brief standard base64 (RFC 4648) encode/decode over raw byte buffers.
//! @remarks The ONE codec for every place bytes have to ride a text channel:
//! the WebSocket handshake key, the MCP image content blocks and the chunked
//! screenshot road on the debug protocol. Decoding is STRICT - padding,
//! length and alphabet are all validated - because every consumer treats a
//! decode failure as "this payload is not what it claims to be" and refuses.

#include <core_util/String.h>

#include <cstddef>
#include <vector>

namespace Orkige
{
	//! @brief standard base64 (RFC 4648, '+'/'/' alphabet, '=' padded)
	class Base64
	{
	public:
		//! @brief encode length bytes at data; the result is always a multiple
		//! of four characters (an empty input encodes to an empty string)
		static String encode(unsigned char const * data, std::size_t length);
		//! @brief decode text into outBytes. STRICT: false (leaving outBytes
		//! cleared) on a length that is not a multiple of four, on any
		//! character outside the alphabet (whitespace and newlines included -
		//! the wire format never wraps) and on misplaced padding.
		static bool decode(String const & text,
			std::vector<unsigned char> & outBytes);
	};
}

#endif //__Base64_h__8_8_2026__10_00_00__
