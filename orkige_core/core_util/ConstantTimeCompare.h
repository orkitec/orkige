/**************************************************************
	created:	2026/07/20 at 12:00
	filename: 	ConstantTimeCompare.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ConstantTimeCompare_h__7_20_2026__12_00_00__
#define __ConstantTimeCompare_h__7_20_2026__12_00_00__

#include "core_util/String.h"

#include <cstddef>

namespace Orkige
{
	//! @brief compare two byte strings for equality in time that does NOT
	//! depend on WHERE the first mismatching byte lies. A plain '=='/strcmp
	//! returns as soon as it finds a differing byte, so an attacker who can
	//! measure the reply latency learns how many leading bytes of a secret
	//! (a bearer token) they guessed right and can recover it one byte at a
	//! time. This helper folds EVERY byte into one accumulator with no early
	//! exit, so the running time reveals nothing about the match position.
	//! @remarks The length difference is folded into the accumulator too
	//! (rather than short-circuiting), and the loop walks max(a,b) bytes so a
	//! length mismatch does not leak the shorter operand's contents either.
	//! Timing still scales with the LENGTH of the longer operand - true
	//! length-independence is not achievable without a fixed-size buffer, and
	//! the token length is not the secret the byte values are; this is the
	//! standard practical fixed-time compare.
	inline bool constantTimeEquals(String const & a, String const & b)
	{
		const std::size_t lengthA = a.size();
		const std::size_t lengthB = b.size();
		const std::size_t walk = lengthA > lengthB ? lengthA : lengthB;
		// seed with the length difference so unequal lengths can never be
		// reported equal, and a zero-length pair still runs the same shape
		unsigned int accumulator =
			static_cast<unsigned int>(lengthA ^ lengthB);
		for (std::size_t i = 0; i < walk; ++i)
		{
			const unsigned char byteA = i < lengthA
				? static_cast<unsigned char>(a[i]) : 0u;
			const unsigned char byteB = i < lengthB
				? static_cast<unsigned char>(b[i]) : 0u;
			accumulator |= static_cast<unsigned int>(byteA ^ byteB);
		}
		return accumulator == 0u;
	}
}

#endif //__ConstantTimeCompare_h__7_20_2026__12_00_00__
