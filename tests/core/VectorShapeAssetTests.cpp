/**************************************************************
	created:	2026/07/10 at 10:00
	filename: 	VectorShapeAssetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless parser tests for the `.oshape` text asset: a well-formed shape
	(fills, holes, colours) round-trips into regions, and every malformation
	(bad/negative counts, truncated vertex runs, garbage, no fill) fails
	honestly with an empty region list. Pure - no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/VectorShapeAsset.h"

#include <vector>

using namespace Orkige;
using Catch::Approx;

TEST_CASE("vectorshape_parse_oshape_valid", "[unit][vectorshape]")
{
	const String text =
		"# orkige vector shape v1\n"
		"version 1\n"
		"fill 0.90 0.42 0.38 1.00\n"
		"contour 4\n"
		"v -1.0 -1.0\n"
		"v  1.0 -1.0\n"
		"v  1.0  1.0\n"
		"v -1.0  1.0\n"
		"hole 4\n"
		"v -0.5  0.5\n"
		"v -0.5 -0.5\n"
		"v  0.5 -0.5\n"
		"v  0.5  0.5\n"
		"fill 0.20 0.20 0.24 1.00   # an accent region\n"
		"contour 3\n"
		"v  0.0  0.2\n"
		"v -0.2 -0.2\n"
		"v  0.2 -0.2\n";

	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(text, regions));
	REQUIRE(regions.size() == 2u);

	// region 0: 4-vertex outer + one 4-vertex hole, salmon fill
	CHECK(regions[0].outer.size() == 4u);
	REQUIRE(regions[0].holes.size() == 1u);
	CHECK(regions[0].holes[0].size() == 4u);
	CHECK(regions[0].fill.r == Approx(0.90f));
	CHECK(regions[0].fill.g == Approx(0.42f));
	CHECK(regions[0].fill.a == Approx(1.0f));
	CHECK(regions[0].outer[1].x == Approx(1.0f));
	CHECK(regions[0].outer[1].y == Approx(-1.0f));

	// region 1: a 3-vertex triangle accent
	CHECK(regions[1].outer.size() == 3u);
	CHECK(regions[1].holes.empty());
	CHECK(regions[1].fill.b == Approx(0.24f));
}

TEST_CASE("vectorshape_parse_oshape_malformed", "[unit][vectorshape]")
{
	std::vector<VectorTessellator::Region> regions;

	SECTION("truncated vertex run (fewer v lines than the count)")
	{
		const String text =
			"fill 1 1 1 1\n"
			"contour 4\n"
			"v 0 0\n"
			"v 1 0\n";
		CHECK_FALSE(VectorShapeAsset::parse(text, regions));
		CHECK(regions.empty());
	}
	SECTION("negative contour count")
	{
		const String text = "fill 1 1 1 1\ncontour -3\n";
		CHECK_FALSE(VectorShapeAsset::parse(text, regions));
		CHECK(regions.empty());
	}
	SECTION("contour before any fill")
	{
		const String text = "contour 3\nv 0 0\nv 1 0\nv 0 1\n";
		CHECK_FALSE(VectorShapeAsset::parse(text, regions));
		CHECK(regions.empty());
	}
	SECTION("garbage vertex coordinates")
	{
		const String text =
			"fill 1 1 1 1\ncontour 3\nv zero zero\nv 1 0\nv 0 1\n";
		CHECK_FALSE(VectorShapeAsset::parse(text, regions));
		CHECK(regions.empty());
	}
	SECTION("a fill with too few vertices to fill")
	{
		const String text = "fill 1 1 1 1\ncontour 2\nv 0 0\nv 1 0\n";
		CHECK_FALSE(VectorShapeAsset::parse(text, regions));
		CHECK(regions.empty());
	}
	SECTION("empty text (no fill at all)")
	{
		CHECK_FALSE(VectorShapeAsset::parse("", regions));
		CHECK(regions.empty());
	}
}

TEST_CASE("vectorshape_parse_ignores_reserved_words", "[unit][vectorshape]")
{
	// stroke/gradient are reserved for later phases: present but ignored, the
	// shape still parses to its fill
	const String text =
		"fill 0.5 0.5 0.5 1\n"
		"stroke 0 0 0 1 0.1\n"
		"contour 3\n"
		"v 0 0\n"
		"v 1 0\n"
		"v 0 1\n";
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(text, regions));
	REQUIRE(regions.size() == 1u);
	CHECK(regions[0].outer.size() == 3u);
}

TEST_CASE("vectorshape_parse_morph_targets", "[unit][vectorshape]")
{
	// a base square plus one same-structure morph target (a squished square):
	// the full parse returns the base AND the named target, each well-formed
	const String text =
		"version 1\n"
		"fill 1 0 0 1\n"
		"contour 4\n"
		"v -1 -1\nv 1 -1\nv 1 1\nv -1 1\n"
		"morph squish\n"
		"fill 1 0 0 1\n"
		"contour 4\n"
		"v -1 -0.5\nv 1 -0.5\nv 1 0.5\nv -1 0.5\n";
	VectorShapeAsset::ParsedShape parsed;
	REQUIRE(VectorShapeAsset::parse(text, parsed));
	REQUIRE(parsed.base.size() == 1u);
	CHECK(parsed.base[0].outer.size() == 4u);
	REQUIRE(parsed.morphs.size() == 1u);
	CHECK(parsed.morphs[0].name == "squish");
	REQUIRE(parsed.morphs[0].regions.size() == 1u);
	CHECK(parsed.morphs[0].regions[0].outer.size() == 4u);

	// the base-only convenience overload discards the morphs but still succeeds
	std::vector<VectorTessellator::Region> baseOnly;
	REQUIRE(VectorShapeAsset::parse(text, baseOnly));
	REQUIRE(baseOnly.size() == 1u);
	CHECK(baseOnly[0].outer.size() == 4u);
}

TEST_CASE("vectorshape_parse_rejects_malformed_morph", "[unit][vectorshape]")
{
	// a morph target with a truncated contour is a malformation: the whole
	// parse fails and yields nothing (never a half-loaded shape)
	const String text =
		"fill 1 1 1 1\n"
		"contour 3\nv 0 0\nv 1 0\nv 0 1\n"
		"morph broken\n"
		"fill 1 1 1 1\n"
		"contour 3\nv 0 0\nv 1 0\n";	// one vertex short
	VectorShapeAsset::ParsedShape parsed;
	CHECK_FALSE(VectorShapeAsset::parse(text, parsed));
	CHECK(parsed.base.empty());
	CHECK(parsed.morphs.empty());

	// a morph before any base pose is also rejected
	const String noBase = "morph early\nfill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n";
	CHECK_FALSE(VectorShapeAsset::parse(noBase, parsed));
}
