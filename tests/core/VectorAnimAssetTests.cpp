/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	VectorAnimAssetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless parser tests for the `.oanim` text asset: a well-formed rig
	(header, clips, layers, channels, shape keys with holes) round-trips
	into the document, every malformation (missing header, bad clip ranges,
	forward parents, truncated key/vertex runs, garbage) fails honestly
	with an empty document, and the fixed-topology law across a shape
	block's keys is enforced. Pure - no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/VectorAnimAsset.h"

#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	//! a complete, well-formed rig exercising every grammar element
	const char * VALID_OANIM =
		"# orkige vector animation v1\n"
		"version 1\n"
		"fps 30\n"
		"duration 60\n"
		"clip idle 0 30 loop\n"
		"clip walk 30 60 once\n"
		"layer root parent -1\n"
		"  pos k 2\n"
		"    kf 0  0 0  lin\n"
		"    kf 30 10 0 ease 0.42 0 0.58 1\n"
		"  rot k 1\n"
		"    kf 0 0 lin\n"
		"layer body parent 0\n"
		"  pos k 1\n"
		"    kf 0 1 0\n"					// no easing token: linear default
		"  opacity k 2\n"
		"    kf 0 1 hold\n"
		"    kf 30 0.5 lin\n"
		"  shape k 2\n"
		"    kf 0 lin\n"
		"      fill 0.9 0.4 0.35 1\n"
		"      contour 4\n"
		"      v -1 -1\n"
		"      v  1 -1\n"
		"      v  1  1\n"
		"      v -1  1\n"
		"      hole 3\n"
		"      v -0.5 -0.5\n"
		"      v  0.5 -0.5\n"
		"      v  0    0.5\n"
		"    kf 30\n"
		"      fill 0.1 0.4 0.35 1\n"
		"      contour 4\n"
		"      v -2 -1\n"
		"      v  2 -1\n"
		"      v  2  1\n"
		"      v -2  1\n"
		"      hole 3\n"
		"      v -0.5 -0.5\n"
		"      v  0.5 -0.5\n"
		"      v  0    0.5\n";
}

TEST_CASE("vectoranim_parse_valid_document", "[unit][vectoranim]")
{
	VectorAnimAsset::Document doc;
	REQUIRE(VectorAnimAsset::parse(VALID_OANIM, doc));

	CHECK(doc.fps == Approx(30.0f));
	CHECK(doc.duration == Approx(60.0f));

	// the clip table
	REQUIRE(doc.clips.size() == 2u);
	CHECK(doc.clips[0].name == "idle");
	CHECK(doc.clips[0].start == Approx(0.0f));
	CHECK(doc.clips[0].end == Approx(30.0f));
	CHECK(doc.clips[0].loop);
	CHECK(doc.clips[1].name == "walk");
	CHECK_FALSE(doc.clips[1].loop);

	// layers, parenting, paint order = file order
	REQUIRE(doc.layers.size() == 2u);
	CHECK(doc.layers[0].name == "root");
	CHECK(doc.layers[0].parent == -1);
	CHECK(doc.layers[1].name == "body");
	CHECK(doc.layers[1].parent == 0);
	CHECK(doc.layers[0].shapes.empty());	// a null layer (pure parent)

	// root.pos: two keys, the second carrying bezier handles
	REQUIRE(doc.layers[0].pos.keys.size() == 2u);
	CHECK(doc.layers[0].pos.keys[0].ease.mode == VectorAnimAsset::EASE_LINEAR);
	CHECK(doc.layers[0].pos.keys[1].frame == Approx(30.0f));
	CHECK(doc.layers[0].pos.keys[1].value[0] == Approx(10.0f));
	CHECK(doc.layers[0].pos.keys[1].ease.mode == VectorAnimAsset::EASE_BEZIER);
	CHECK(doc.layers[0].pos.keys[1].ease.outX == Approx(0.42f));
	CHECK(doc.layers[0].pos.keys[1].ease.inX == Approx(0.58f));

	// body: an easing-less key defaults to linear; hold parses
	REQUIRE(doc.layers[1].pos.keys.size() == 1u);
	CHECK(doc.layers[1].pos.keys[0].ease.mode == VectorAnimAsset::EASE_LINEAR);
	REQUIRE(doc.layers[1].opacity.keys.size() == 2u);
	CHECK(doc.layers[1].opacity.keys[0].ease.mode == VectorAnimAsset::EASE_HOLD);
	CHECK(doc.layers[1].opacity.keys[1].value[0] == Approx(0.5f));
	CHECK(doc.layers[1].anchor.keys.empty());	// absent channel = default
	CHECK(doc.layers[1].scale.keys.empty());

	// the shape block: two same-topology region keys with a hole
	REQUIRE(doc.layers[1].shapes.size() == 1u);
	VectorAnimAsset::Shape const & shape = doc.layers[1].shapes[0];
	REQUIRE(shape.keys.size() == 2u);
	CHECK(shape.keys[0].region.outer.size() == 4u);
	REQUIRE(shape.keys[0].region.holes.size() == 1u);
	CHECK(shape.keys[0].region.holes[0].size() == 3u);
	CHECK(shape.keys[0].region.fill.r == Approx(0.9f));
	CHECK(shape.keys[1].region.fill.r == Approx(0.1f));
	CHECK(shape.keys[1].frame == Approx(30.0f));
	CHECK(shape.keys[1].region.outer[1].x == Approx(2.0f));
}

TEST_CASE("vectoranim_parse_malformed", "[unit][vectoranim]")
{
	VectorAnimAsset::Document doc;

	SECTION("missing fps")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"duration 60\nlayer a parent -1\n", doc));
	}
	SECTION("missing duration")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nlayer a parent -1\n", doc));
	}
	SECTION("header after the first layer")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nfps 60\n", doc));
	}
	SECTION("clip after the first layer")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nclip x 0 30 loop\n",
			doc));
	}
	SECTION("clip start not before end")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nclip x 30 30 loop\nlayer a parent -1\n",
			doc));
	}
	SECTION("clip beyond the duration")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nclip x 0 61 loop\nlayer a parent -1\n",
			doc));
	}
	SECTION("duplicate clip name")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nclip x 0 30 loop\nclip x 30 60 once\n"
			"layer a parent -1\n", doc));
	}
	SECTION("unknown clip mode")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nclip x 0 30 pingpong\nlayer a parent -1\n",
			doc));
	}
	SECTION("forward/self parent")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent 0\n", doc));
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nlayer b parent 5\n",
			doc));
	}
	SECTION("layer line without the parent keyword")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a -1\n", doc));
	}
	SECTION("channel before any layer")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\npos k 1\nkf 0 0 0\n", doc));
	}
	SECTION("channel redefined")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\n"
			"pos k 1\nkf 0 0 0\npos k 1\nkf 5 1 1\n", doc));
	}
	SECTION("truncated channel key run")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\npos k 2\nkf 0 0 0\n",
			doc));
	}
	SECTION("non-increasing key frames")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\n"
			"pos k 2\nkf 10 0 0\nkf 10 1 1\n", doc));
	}
	SECTION("key frame beyond the duration")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\npos k 1\nkf 61 0 0\n",
			doc));
	}
	SECTION("garbage key values")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\npos k 1\nkf 0 zero 0\n",
			doc));
	}
	SECTION("incomplete bezier handles")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\n"
			"pos k 1\nkf 0 0 0 ease 0.42 0 0.58\n", doc));
	}
	SECTION("unknown easing word")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\n"
			"pos k 1\nkf 0 0 0 bouncy\n", doc));
	}
	SECTION("a key with nothing open to receive it")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nkf 0 0 0\n", doc));
	}
	SECTION("shape key without a fill")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 1\nkf 0\n"
			"contour 3\nv 0 0\nv 1 0\nv 0 1\n", doc));
	}
	SECTION("truncated vertex run")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 1\nkf 0\n"
			"fill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\n", doc));
	}
	SECTION("unknown keyword inside a vertex run")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 1\nkf 0\n"
			"fill 1 1 1 1\ncontour 3\nv 0 0\nglitter\nv 1 0\nv 0 1\n", doc));
	}
	SECTION("more shape keys than declared")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 1\n"
			"kf 0\nfill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n"
			"kf 30\nfill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n", doc));
	}
	SECTION("fewer shape keys than declared")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 2\n"
			"kf 0\nfill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n", doc));
	}
	SECTION("empty text")
	{
		CHECK_FALSE(VectorAnimAsset::parse("", doc));
	}
	SECTION("header only, no layers")
	{
		CHECK_FALSE(VectorAnimAsset::parse("fps 30\nduration 60\n", doc));
	}

	// every malformation leaves the document EMPTY, never half-loaded
	CHECK(doc.layers.empty());
	CHECK(doc.clips.empty());
	CHECK(doc.fps == Approx(0.0f));
}

TEST_CASE("vectoranim_parse_topology_enforced", "[unit][vectoranim]")
{
	// the fixed-vertex-count law: every key of one shape block must repeat
	// the first key's topology exactly
	VectorAnimAsset::Document doc;

	SECTION("contour count differs across keys")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 2\n"
			"kf 0\nfill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n"
			"kf 30\nfill 1 1 1 1\ncontour 4\nv 0 0\nv 1 0\nv 1 1\nv 0 1\n",
			doc));
	}
	SECTION("hole count differs across keys")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 2\n"
			"kf 0\nfill 1 1 1 1\ncontour 4\nv -1 -1\nv 1 -1\nv 1 1\nv -1 1\n"
			"hole 3\nv -0.2 -0.2\nv 0.2 -0.2\nv 0 0.2\n"
			"kf 30\nfill 1 1 1 1\ncontour 4\nv -1 -1\nv 1 -1\nv 1 1\nv -1 1\n",
			doc));
	}
	SECTION("hole vertex count differs across keys")
	{
		CHECK_FALSE(VectorAnimAsset::parse(
			"fps 30\nduration 60\nlayer a parent -1\nshape k 2\n"
			"kf 0\nfill 1 1 1 1\ncontour 4\nv -1 -1\nv 1 -1\nv 1 1\nv -1 1\n"
			"hole 3\nv -0.2 -0.2\nv 0.2 -0.2\nv 0 0.2\n"
			"kf 30\nfill 1 1 1 1\ncontour 4\nv -1 -1\nv 1 -1\nv 1 1\nv -1 1\n"
			"hole 4\nv -0.2 -0.2\nv 0.2 -0.2\nv 0.2 0.2\nv -0.2 0.2\n", doc));
	}
	CHECK(doc.layers.empty());
}

TEST_CASE("vectoranim_parse_defaults_and_reserved", "[unit][vectoranim]")
{
	// a clip-less file gets ONE implicit looping clip over the whole
	// timeline, and unknown keywords between elements are reserved/ignored
	const String text =
		"fps 24\n"
		"duration 48\n"
		"shimmer full\n"	// reserved for a later version - ignored
		"layer a parent -1\n"
		"  pos k 1\n"
		"    kf 0 3 4\n"
		"glow 0.5\n";		// ignored between complete elements
	VectorAnimAsset::Document doc;
	REQUIRE(VectorAnimAsset::parse(text, doc));
	REQUIRE(doc.clips.size() == 1u);
	CHECK(doc.clips[0].name == "default");
	CHECK(doc.clips[0].start == Approx(0.0f));
	CHECK(doc.clips[0].end == Approx(48.0f));
	CHECK(doc.clips[0].loop);
	REQUIRE(doc.layers.size() == 1u);
	CHECK(doc.layers[0].pos.keys.size() == 1u);
}

TEST_CASE("vectoranim_clip_lookup", "[unit][vectoranim]")
{
	VectorAnimAsset::Document doc;
	REQUIRE(VectorAnimAsset::parse(VALID_OANIM, doc));
	CHECK(doc.findClip("idle") == 0);
	CHECK(doc.findClip("walk") == 1);
	CHECK(doc.findClip("run") == -1);
	CHECK(doc.findClip("") == -1);
}
