/********************************************************************
	created:	Thursday 2026/07/16 at 15:00
	filename: 	Sha1.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __Sha1_h__16_7_2026__15_00_00__
#define __Sha1_h__16_7_2026__15_00_00__

//! @file Sha1.h
//! @brief SHA-1 over one in-memory message or one file (FIPS 180-1).
//! @remarks A compact self-contained implementation shared by the small
//! digest consumers in the tree - the WebSocket handshake (one digest of a
//! ~60-byte string) and the asset-import change detection (content
//! fingerprints of small text assets) - where a crypto dependency would be
//! disproportionate. This is NOT a general-purpose cryptographic facility:
//! SHA-1 is fine as a content fingerprint and as the fixed RFC 6455
//! handshake digest, and is used for nothing security-sensitive.

#include <core_util/String.h>
#include <cstddef>

namespace Orkige
{
	//! @brief SHA-1 digests as lower-case hex strings (@see the file comment)
	class Sha1
	{
	public:
		//! @brief digest one in-memory message into the raw 20-byte form (the
		//! WebSocket handshake base64-encodes this)
		static void digest(unsigned char const * data, std::size_t length,
			unsigned char outDigest[20]);
		//! @brief digest one in-memory message as 40 lower-case hex characters
		static String hexDigest(void const * data, std::size_t length);
		//! @brief digest a whole file's bytes as 40 lower-case hex characters;
		//! "" when the file cannot be read (a missing file has no fingerprint)
		static String hexDigestOfFile(String const & filePath);
	};
}

#endif //__Sha1_h__16_7_2026__15_00_00__
