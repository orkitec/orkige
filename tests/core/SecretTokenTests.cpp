/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	SecretTokenTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/SecretToken.h>

#include <set>

using Orkige::mintSecretToken;
using Orkige::String;

TEST_CASE("a minted secret is 128 bits of lowercase hex", "[auth]")
{
	const String token = mintSecretToken();
	CHECK(token.size() == 32);
	for (char const character : token)
	{
		const bool digit = character >= '0' && character <= '9';
		const bool lower = character >= 'a' && character <= 'f';
		CHECK((digit || lower));
	}
}

TEST_CASE("minted secrets do not repeat", "[auth]")
{
	// a repeat would mean the draws share state that is not being advanced -
	// the shape a seeded engine takes when it is default-constructed per call
	std::set<String> seen;
	for (int i = 0; i < 64; ++i)
	{
		seen.insert(mintSecretToken());
	}
	CHECK(seen.size() == 64);
	// and no draw is a constant string
	CHECK(seen.count(String(32, '0')) == 0);
	// What a test CANNOT show is the property this function exists for: that
	// the draws come from the platform's entropy source rather than from a
	// seeded engine whose state a few hundred outputs reconstruct. Two such
	// generators are indistinguishable from their output at this sample size -
	// the guarantee is structural (@see SecretToken.h), and what is testable is
	// asserted above.
}
