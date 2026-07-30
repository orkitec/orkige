/**************************************************************
	created:	2026/07/30 at 18:00
	filename: 	VersionOrderTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless proof of the ordered build identity (VersionOrder): the
	composition every surface derives from (archive filename, VERSION file,
	--version, the published manifest), and the precedence an updater
	depends on - date ordering, same date with different commits ranking as
	the SAME version, a release outranking a nightly of the same base, a
	base bump outranking any date, and malformed input answered as
	incomparable instead of guessed at. The composed strings are pinned to
	literals the packaging tooling asserts too (Util/orkige_nightly_package.py
	--selftest), so the two implementations of the grammar cannot drift
	silently; the end-to-end agreement is the nightly's smoke test, which
	matches the packaged version against the one the binary reports.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/VersionOrder.h>

using namespace Orkige;
using namespace Orkige::VersionOrder;

TEST_CASE("VersionOrder composes the nightly identity from one stamp", "[unit]")
{
	// THE cross-language literal: the packaging tooling's selftest asserts the
	// same string from the same inputs
	CHECK(compose("2.0.0", "2026-07-30", "dea551f9e") ==
		"2.0.0-nightly.20260730+dea551f9e");
	// the compact date form is accepted too (the same value, one rendering)
	CHECK(compose("2.0.0", "20260730", "dea551f9e") ==
		"2.0.0-nightly.20260730+dea551f9e");
	// no commit: still an ordered identity, just without the metadata
	CHECK(compose("2.0.0", "2026-07-30", "") == "2.0.0-nightly.20260730");
	// an incomplete or nonsensical stamp NEVER invents an identity
	CHECK(compose("2.0.0", "", "dea551f9e").empty());
	CHECK(compose("2.0.0", "2026-7-3", "dea551f9e").empty());
	CHECK(compose("", "2026-07-30", "dea551f9e").empty());
	CHECK(compose("2.0", "2026-07-30", "dea551f9e").empty());
	CHECK(compose("2.0.0-nightly.1", "2026-07-30", "abc").empty());
	CHECK(compose("2.0.0", "2026-07-30", "not a commit").empty());

	// what a composed identity is made of, read back
	CHECK(commitOf("2.0.0-nightly.20260730+dea551f9e") == "dea551f9e");
	CHECK(commitOf("2.0.0-nightly.20260730").empty());
	CHECK(commitOf("2.0.0 (local build)").empty());
}

TEST_CASE("VersionOrder parses the version grammar strictly", "[unit]")
{
	Version parsed;
	REQUIRE(parse("2.0.0-nightly.20260730+dea551f9e", parsed));
	CHECK(parsed.mMajor == 2u);
	CHECK(parsed.mMinor == 0u);
	CHECK(parsed.mPatch == 0u);
	REQUIRE(parsed.mPrerelease.size() == 2u);
	CHECK(parsed.mPrerelease[0] == "nightly");
	CHECK(parsed.mPrerelease[1] == "20260730");
	CHECK(parsed.mBuild == "dea551f9e");

	// a release version: no prerelease, no metadata
	REQUIRE(parse("2.1.0", parsed));
	CHECK(parsed.mPrerelease.empty());
	CHECK(parsed.mBuild.empty());
	// the shape a git tag carries
	CHECK(parse("v2.1.0", parsed));

	// the FILENAME rendering reads back as the same version
	Version token;
	REQUIRE(parse("2.0.0-nightly.20260730_dea551f9e", token));
	CHECK(token.mBuild == "dea551f9e");
	CHECK(compare("2.0.0-nightly.20260730_dea551f9e",
		"2.0.0-nightly.20260730+dea551f9e") == VO_SAME);
	CHECK(filenameToken("2.0.0-nightly.20260730+dea551f9e") ==
		"2.0.0-nightly.20260730_dea551f9e");
	// idempotent, and honest about a non-version
	CHECK(filenameToken("2.0.0-nightly.20260730_dea551f9e") ==
		"2.0.0-nightly.20260730_dea551f9e");
	CHECK(filenameToken("local build").empty());

	// everything that is NOT a version
	Version rejected;
	CHECK_FALSE(parse("", rejected));
	CHECK_FALSE(parse("2.0", rejected));
	CHECK_FALSE(parse("2.0.0.1", rejected));
	CHECK_FALSE(parse("banana", rejected));
	CHECK_FALSE(parse("2.0.0 (local build)", rejected));
	CHECK_FALSE(parse("2.0.0-", rejected));
	CHECK_FALSE(parse("2.0.0+", rejected));
	CHECK_FALSE(parse("02.0.0", rejected));
	CHECK_FALSE(parse("2.0.0-nightly.007", rejected));	// leading-zero number
	CHECK_FALSE(parse("-2.0.0", rejected));
	CHECK_FALSE(parse("2.0.0-nightly.1+a_b", rejected));	// mixed metadata marks
	// a base field nobody can represent is refused, not wrapped
	CHECK_FALSE(parse("99999999999999999999.0.0", rejected));
}

TEST_CASE("VersionOrder ranks nightlies by date", "[unit]")
{
	CHECK(compare("2.0.0-nightly.20260731+aaaaaaaaa",
		"2.0.0-nightly.20260730+bbbbbbbbb") == VO_NEWER);
	CHECK(compare("2.0.0-nightly.20260730+aaaaaaaaa",
		"2.0.0-nightly.20260731+bbbbbbbbb") == VO_OLDER);
	// across a month and a year boundary (the date is one number, so this is
	// ordering and not string comparison luck)
	CHECK(compare("2.0.0-nightly.20260801", "2.0.0-nightly.20260731") == VO_NEWER);
	CHECK(compare("2.0.0-nightly.20270101", "2.0.0-nightly.20261231") == VO_NEWER);

	// the updater's question
	CHECK(isUpdate("2.0.0-nightly.20260731+aaaaaaaaa",
		"2.0.0-nightly.20260730+bbbbbbbbb"));
	CHECK_FALSE(isUpdate("2.0.0-nightly.20260730+aaaaaaaaa",
		"2.0.0-nightly.20260731+bbbbbbbbb"));
}

TEST_CASE("VersionOrder treats one date's builds as one version", "[unit]")
{
	// SAME date, DIFFERENT commit: the commit is metadata, so this is not an
	// update - a client that downloaded today's build must not download it
	// again because the tree was rebuilt
	CHECK(compare("2.0.0-nightly.20260730+aaaaaaaaa",
		"2.0.0-nightly.20260730+bbbbbbbbb") == VO_SAME);
	CHECK_FALSE(isUpdate("2.0.0-nightly.20260730+aaaaaaaaa",
		"2.0.0-nightly.20260730+bbbbbbbbb"));
	// metadata present on one side only changes nothing either
	CHECK(compare("2.0.0-nightly.20260730",
		"2.0.0-nightly.20260730+bbbbbbbbb") == VO_SAME);
	// and the identical string is of course the same version
	CHECK(compare("2.0.0-nightly.20260730+aaaaaaaaa",
		"2.0.0-nightly.20260730+aaaaaaaaa") == VO_SAME);
}

TEST_CASE("VersionOrder ranks a release above its own nightlies", "[unit]")
{
	// the semantic-versioning rule: a prerelease PRECEDES the release of the
	// same base, however late its date
	CHECK(compare("2.0.0", "2.0.0-nightly.20261231") == VO_NEWER);
	CHECK(compare("2.0.0-nightly.20261231", "2.0.0") == VO_OLDER);
	CHECK(isUpdate("2.0.0", "2.0.0-nightly.20260730+dea551f9e"));
	CHECK_FALSE(isUpdate("2.0.0-nightly.20260730", "2.0.0"));
	// two releases order by their base alone
	CHECK(compare("2.0.1", "2.0.0") == VO_NEWER);
	CHECK(compare("2.0.0", "2.0.0") == VO_SAME);
}

TEST_CASE("VersionOrder ranks a base bump above any date", "[unit]")
{
	// THE boundary: the engine's declared version moves. Tonight's nightly of
	// the new base outranks every nightly of the old one, whatever the dates.
	CHECK(compare("2.1.0-nightly.20260101", "2.0.0-nightly.20261231") == VO_NEWER);
	CHECK(compare("3.0.0-nightly.20250101", "2.9.9-nightly.20261231") == VO_NEWER);
	CHECK(compare("2.0.10-nightly.20260101",
		"2.0.9-nightly.20261231") == VO_NEWER);	// numeric, not lexical
	CHECK(compare("2.10.0-nightly.20260101", "2.9.0-nightly.20260101") == VO_NEWER);
}

TEST_CASE("VersionOrder refuses to rank what it cannot read", "[unit]")
{
	// an unstamped developer build has NO ordered identity: the honest answer
	// is "not comparable", never "older" (which would offer it an update it
	// cannot verify) and never "same" (which would hide one)
	CHECK(compare("2.0.0 (local build)",
		"2.0.0-nightly.20260730+dea551f9e") == VO_INCOMPARABLE);
	CHECK(compare("2.0.0-nightly.20260730+dea551f9e", "") == VO_INCOMPARABLE);
	CHECK(compare("", "") == VO_INCOMPARABLE);
	CHECK(compare("nightly", "2.0.0") == VO_INCOMPARABLE);
	CHECK_FALSE(isUpdate("2.0.0-nightly.20260730", "local build"));
	CHECK_FALSE(isUpdate("garbage", "2.0.0-nightly.20260730"));
}

TEST_CASE("VersionOrder orders prerelease identifiers by the standard rules",
	"[unit]")
{
	// numbers below alphanumerics, alphanumerics by ASCII, a longer identifier
	// set after its prefix - the rules that keep a future channel ("beta",
	// "rc") orderable against the nightly channel without new code
	CHECK(compare("2.0.0-alpha", "2.0.0-1") == VO_NEWER);
	CHECK(compare("2.0.0-alpha", "2.0.0-beta") == VO_OLDER);
	CHECK(compare("2.0.0-alpha.1", "2.0.0-alpha") == VO_NEWER);
	CHECK(compare("2.0.0-alpha.2", "2.0.0-alpha.10") == VO_OLDER);
	CHECK(compare("2.0.0-rc.1", "2.0.0-nightly.20261231") == VO_NEWER);
}
