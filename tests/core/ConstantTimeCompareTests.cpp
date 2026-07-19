/**************************************************************
	created:	2026/07/20 at 12:00
	filename: 	ConstantTimeCompareTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/ConstantTimeCompare.h>

using Orkige::constantTimeEquals;
using Orkige::String;

TEST_CASE("constantTimeEquals matches std::string == for equality", "[auth]")
{
	// identical strings compare equal
	CHECK(constantTimeEquals(String("s3cr3t-token"), String("s3cr3t-token")));
	// a single differing byte anywhere is inequality
	CHECK_FALSE(constantTimeEquals(String("s3cr3t-token"),
		String("s3cr3t-tokeX")));		// last byte differs
	CHECK_FALSE(constantTimeEquals(String("s3cr3t-token"),
		String("X3cr3t-token")));		// first byte differs
	CHECK_FALSE(constantTimeEquals(String("s3cr3t-token"),
		String("s3cr3X-token")));		// middle byte differs
}

TEST_CASE("constantTimeEquals folds length differences", "[auth]")
{
	// a prefix of the secret must NOT be accepted (early-exit compares that
	// bail on the first mismatch could otherwise leak the length)
	CHECK_FALSE(constantTimeEquals(String("s3cr3t"), String("s3cr3t-token")));
	CHECK_FALSE(constantTimeEquals(String("s3cr3t-token"), String("s3cr3t")));
	CHECK_FALSE(constantTimeEquals(String(), String("x")));
	CHECK_FALSE(constantTimeEquals(String("x"), String()));
}

TEST_CASE("constantTimeEquals handles empty and NUL-bearing inputs", "[auth]")
{
	// two empty strings are equal (the no-token dev path passes an empty token
	// through the same helper)
	CHECK(constantTimeEquals(String(), String()));
	// embedded NULs are compared as bytes, not treated as terminators
	String withNulA;
	withNulA.push_back('a');
	withNulA.push_back('\0');
	withNulA.push_back('b');
	String withNulB;
	withNulB.push_back('a');
	withNulB.push_back('\0');
	withNulB.push_back('b');
	String withNulC;
	withNulC.push_back('a');
	withNulC.push_back('\0');
	withNulC.push_back('c');
	CHECK(constantTimeEquals(withNulA, withNulB));
	CHECK_FALSE(constantTimeEquals(withNulA, withNulC));
}
