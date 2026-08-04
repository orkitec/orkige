/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	SecretToken.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __SecretToken_h__3_8_2026__10_00_00__
#define __SecretToken_h__3_8_2026__10_00_00__

#include "core_util/String.h"

#include <random>

namespace Orkige
{
	//! @brief mint a fresh session secret: 32 lowercase hex characters (128
	//! bits) drawn from the platform's non-deterministic entropy source.
	//!
	//! WHY its own function rather than any handy id generator: a bearer token
	//! is guessed by an attacker, and an id is not. A seeded pseudo-random
	//! engine (mt19937 and friends) has a RECOVERABLE state - observing enough
	//! of its outputs reconstructs the generator and predicts every further
	//! draw - and generators whose outputs are PUBLISHED elsewhere (asset ids
	//! land in committed sidecar files) hand those observations out for free.
	//! std::random_device is the entropy source the platform itself keeps
	//! unpredictable, and every draw here comes straight from it, so no engine
	//! state exists to recover.
	//! @remarks Where a platform has no entropy source std::random_device is
	//! permitted to fall back to a deterministic sequence. Every platform this
	//! engine ships on backs it with the OS facility (getentropy/BCrypt/the
	//! browser's crypto), so the guarantee holds; the honest limit is that the
	//! standard does not state it.
	inline String mintSecretToken()
	{
		static const char * const HEX = "0123456789abcdef";
		std::random_device device;
		std::uniform_int_distribution<int> nibble(0, 15);
		String token;
		token.reserve(32);
		for (int i = 0; i < 32; ++i)
		{
			token += HEX[nibble(device)];
		}
		return token;
	}
}

#endif //__SecretToken_h__3_8_2026__10_00_00__
