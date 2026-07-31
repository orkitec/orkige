/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	Sha256.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __Sha256_h__31_7_2026__09_00_00__
#define __Sha256_h__31_7_2026__09_00_00__

//! @file Sha256.h
//! @brief SHA-256 (FIPS 180-4) over a message fed in one piece or in chunks.
//! @remarks The digest a DOWNLOAD is checked against: every published build
//! artifact carries a `.sha256` sidecar, and the bytes that arrived have to
//! match it before anything is done with them. That is an integrity check on
//! a file measured in hundreds of megabytes, so the interface is INCREMENTAL:
//! a caller streams the file through update() a block at a time and never
//! holds it in memory.
//!
//! Deliberately free of the filesystem. Reading bytes is the caller's
//! business (and the funnel's - @see core_filesystem/FileWriter.h); this
//! computes a digest and nothing else, which is what keeps it a pure,
//! headlessly unit-testable primitive with no platform edge at all.
//!
//! SHA-256 rather than the SHA-1 next door because this one IS
//! security-relevant: it is the only thing standing between a download and
//! the bytes being trusted. @see Sha1.h, whose comment says why that one is
//! not.

#include <core_module/OrkigePrerequisites.h>
#include <core_util/String.h>

#include <cstddef>

namespace Orkige
{
	//! @brief SHA-256 digests, fed at once or in chunks (@see the file comment)
	class ORKIGE_CORE_DLL Sha256
	{
		//--- Variables ---------------------------------------
	public:
		//! the digest length in bytes
		static const std::size_t DIGEST_BYTES = 32;
		//! the digest length as lower-case hex characters
		static const std::size_t HEX_LENGTH = 64;
	private:
		unsigned int		mState[8];		//!< the eight working words
		unsigned long long	mBitLength;		//!< message length so far, in bits
		unsigned char		mBlock[64];		//!< the partial block update() holds
		std::size_t			mBuffered;		//!< bytes currently in mBlock
		//--- Methods -----------------------------------------
	public:
		//! constructor - an empty message
		Sha256();

		//! @brief start over (the object is reusable)
		void reset();
		//! @brief feed @p length bytes of the message
		void update(void const * data, std::size_t length);
		//! @brief finish and write the raw 32-byte digest.
		//! @remarks Finishing CLOSES the message: a later update() would be a
		//! caller mistake, so this resets afterwards and the object starts a
		//! fresh message.
		void finish(unsigned char outDigest[DIGEST_BYTES]);
		//! @brief finish and return the digest as 64 lower-case hex characters
		String finishHex();

		//! @brief the whole-message convenience: digest @p length bytes as 64
		//! lower-case hex characters
		static String hexDigest(void const * data, std::size_t length);

		//! @brief compare two hex digests as DIGESTS rather than as strings:
		//! surrounding whitespace ignored, letter case ignored, and the
		//! comparison itself constant-time over the normalised forms.
		//! @return false unless both sides are 64 hex characters - a
		//! truncated or malformed digest matches nothing, which is the only
		//! safe answer when the thing being checked is a download.
		static bool hexEquals(String const & left, String const & right);
	private:
		//! mix one 64-byte block into the state
		void transform(unsigned char const * block);
	};
}

#endif //__Sha256_h__31_7_2026__09_00_00__
