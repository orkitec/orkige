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
	// gradient paint stays a reserved word: present but ignored, the shape
	// still parses to its fill
	const String text =
		"fill 0.5 0.5 0.5 1\n"
		"gradient 0 0 1 1\n"
		"contour 3\n"
		"v 0 0\n"
		"v 1 0\n"
		"v 0 1\n";
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(text, regions));
	REQUIRE(regions.size() == 1u);
	CHECK(regions[0].outer.size() == 3u);
}

TEST_CASE("vectorshape_parse_stroke_region", "[unit][vectorshape][stroke]")
{
	// a stroke region: `contour` is the CENTRELINE (two points is a legal
	// straight ribbon), no holes, plus an optional convex clip mask
	const String text =
		"version 2\n"
		"fill 0 0 0 1\n"
		"stroke 0.5 round bevel 4 open\n"
		"contour 2\n"
		"v 0 0\n"
		"v 2 0\n"
		"mask 4\n"
		"v -1 -1\n"
		"v 3 -1\n"
		"v 3 1\n"
		"v -1 1\n";
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(text, regions));
	REQUIRE(regions.size() == 1u);
	CHECK(regions[0].kind == VectorTessellator::REGION_STROKE);
	CHECK(regions[0].strokeWidth == Approx(0.5f));
	CHECK(regions[0].strokeCap == VectorTessellator::CAP_ROUND);
	CHECK(regions[0].strokeJoin == VectorTessellator::JOIN_BEVEL);
	CHECK_FALSE(regions[0].strokeClosed);
	CHECK(regions[0].outer.size() == 2u);
	CHECK(regions[0].mask.size() == 4u);
}

TEST_CASE("vectorshape_parse_refuses_malformed_stroke",
	"[unit][vectorshape][stroke]")
{
	std::vector<VectorTessellator::Region> regions;
	// an unknown cap word is not silently accepted
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 0 0 0 1\nstroke 1 flat miter 4 open\ncontour 2\nv 0 0\nv 1 0\n",
		regions));
	// a stroke carries no holes
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 0 0 0 1\nstroke 1 butt miter 4 open\ncontour 2\nv 0 0\nv 1 0\n"
		"hole 3\nv 0 0\nv 1 0\nv 1 1\n", regions));
	// a one-point centreline is no ribbon
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 0 0 0 1\nstroke 1 butt miter 4 open\ncontour 1\nv 0 0\n",
		regions));
	CHECK(regions.empty());
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

TEST_CASE("vectorshape_parse_textured_region", "[unit][vectorshape]")
{
	// a textured cutout part: the quad contour projects through the rect
	// into per-vertex UVs (v runs top-down - texture row 0 on the rect's
	// TOP edge), and the fill stays as the multiply tint
	const String text =
		"version 3\n"
		"fill 1 1 1 1\n"
		"texture head.png -1.0 -2.0 2.0 4.0\n"
		"contour 4\n"
		"v -1.0 -2.0\n"		// bottom-left  -> uv (0, 1)
		"v  1.0 -2.0\n"		// bottom-right -> uv (1, 1)
		"v  1.0  2.0\n"		// top-right    -> uv (1, 0)
		"v -1.0  2.0\n";	// top-left     -> uv (0, 0)
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(text, regions));
	REQUIRE(regions.size() == 1u);
	CHECK(regions[0].texture == "head.png");
	REQUIRE(regions[0].uvs.size() == 4u);
	CHECK(regions[0].uvs[0].x == Approx(0.0f));
	CHECK(regions[0].uvs[0].y == Approx(1.0f));
	CHECK(regions[0].uvs[1].x == Approx(1.0f));
	CHECK(regions[0].uvs[1].y == Approx(1.0f));
	CHECK(regions[0].uvs[2].x == Approx(1.0f));
	CHECK(regions[0].uvs[2].y == Approx(0.0f));
	CHECK(regions[0].uvs[3].x == Approx(0.0f));
	CHECK(regions[0].uvs[3].y == Approx(0.0f));
}

TEST_CASE("vectorshape_parse_textured_uv_window", "[unit][vectorshape]")
{
	// an atlas sub-rect windows the projection; a mid-rect vertex lands
	// mid-window (arbitrary polygons project per vertex, not just quads)
	const String text =
		"version 3\n"
		"fill 1 1 1 1\n"
		"texture atlas.png 0 0 2 2 0.5 0.25 1.0 0.75\n"
		"contour 3\n"
		"v 0 0\n"		// rect bottom-left -> uv (0.5, 0.75)
		"v 2 0\n"		// rect bottom-right -> uv (1.0, 0.75)
		"v 1 2\n";		// rect top-centre -> uv (0.75, 0.25)
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(text, regions));
	REQUIRE(regions.size() == 1u);
	REQUIRE(regions[0].uvs.size() == 3u);
	CHECK(regions[0].uvs[0].x == Approx(0.5f));
	CHECK(regions[0].uvs[0].y == Approx(0.75f));
	CHECK(regions[0].uvs[1].x == Approx(1.0f));
	CHECK(regions[0].uvs[1].y == Approx(0.75f));
	CHECK(regions[0].uvs[2].x == Approx(0.75f));
	CHECK(regions[0].uvs[2].y == Approx(0.25f));
}

TEST_CASE("vectorshape_parse_rejects_bad_texture_specs", "[unit][vectorshape]")
{
	std::vector<VectorTessellator::Region> regions;
	// zero-size rect
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 1 1 1 1\ntexture t.png 0 0 0 2\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n",
		regions));
	// a half-given uv window
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 1 1 1 1\ntexture t.png 0 0 2 2 0.5 0.5\n"
		"contour 3\nv 0 0\nv 1 0\nv 0 1\n", regions));
	// texture after the contour
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\ntexture t.png 0 0 2 2\n",
		regions));
	// texture repeated
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 1 1 1 1\ntexture t.png 0 0 2 2\ntexture u.png 0 0 2 2\n"
		"contour 3\nv 0 0\nv 1 0\nv 0 1\n", regions));
	// a textured region takes no hole
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 1 1 1 1\ntexture t.png -2 -2 4 4\ncontour 4\n"
		"v -1 -1\nv 1 -1\nv 1 1\nv -1 1\n"
		"hole 3\nv -0.2 -0.2\nv 0.2 -0.2\nv 0 0.2\n", regions));
	// a textured region takes no stroke (either order)
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 1 1 1 1\ntexture t.png 0 0 2 2\n"
		"stroke 0.2 round round 4 open\ncontour 2\nv 0 0\nv 1 0\n", regions));
	CHECK_FALSE(VectorShapeAsset::parse(
		"fill 1 1 1 1\nstroke 0.2 round round 4 open\n"
		"texture t.png 0 0 2 2\ncontour 2\nv 0 0\nv 1 0\n", regions));
	CHECK(regions.empty());
}

TEST_CASE("vectorshape_parse_untextured_unchanged_by_v3", "[unit][vectorshape]")
{
	// the v3 vocabulary is purely additive: a v2 document parses to regions
	// with NO texture and NO derived uvs (the byte-identity guarantee)
	const String text =
		"version 2\n"
		"fill 0.2 0.4 0.6 1\n"
		"contour 3\nv 0 0\nv 1 0\nv 0 1\n";
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(text, regions));
	REQUIRE(regions.size() == 1u);
	CHECK(regions[0].texture.empty());
	CHECK(regions[0].uvs.empty());
}
