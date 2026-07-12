/**************************************************************
	created:	2026/07/12 at 18:00
	filename: 	CookVectorAnimRoundTripTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The animation-cook round trip: tests/assets/vectoranim/roundtrip.oanim
	is the EXACT cook output over roundtrip.json beside it (the cook's
	--selftest gate enforces byte identity, so the committed fixture can
	never drift from the live cook), and this test feeds that output
	through the REAL parser and evaluator - proving the cooked grammar,
	unit conversions (y flip, scale, CCW rotation) and clip table are what
	the runtime actually consumes.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/VectorAnimAsset.h"
#include "core_util/VectorAnimEval.h"

#include <cmath>
#include <fstream>
#include <sstream>

using namespace Orkige;
using Catch::Approx;

namespace
{
	//! centroid of a region's outer contour
	VectorTessellator::Point contourCentroid(
		std::vector<VectorTessellator::Point> const & points)
	{
		VectorTessellator::Point sum(0.0f, 0.0f);
		for(VectorTessellator::Point const & p : points)
		{
			sum.x += p.x;
			sum.y += p.y;
		}
		sum.x /= static_cast<float>(points.size());
		sum.y /= static_cast<float>(points.size());
		return sum;
	}
}

TEST_CASE("cook_vector_anim_roundtrip", "[unit][vectoranim]")
{
	std::ifstream file(
		ORKIGE_TESTS_ASSET_DIR "/vectoranim/roundtrip.oanim");
	REQUIRE(file.is_open());
	std::stringstream buffer;
	buffer << file.rdbuf();

	// the cooked text parses in the real grammar
	VectorAnimAsset::Document doc;
	REQUIRE(VectorAnimAsset::parse(buffer.str(), doc));
	CHECK(doc.fps == Approx(30.0f));
	CHECK(doc.duration == Approx(60.0f));

	// markers became the clip table ('#once' suffix made walk one-shot)
	REQUIRE(doc.clips.size() == 2u);
	CHECK(doc.clips[0].name == "idle");
	CHECK(doc.clips[0].start == Approx(0.0f));
	CHECK(doc.clips[0].end == Approx(30.0f));
	CHECK(doc.clips[0].loop);
	CHECK(doc.clips[1].name == "walk");
	CHECK_FALSE(doc.clips[1].loop);

	// the rig: the synthetic centering root, the null, then the paint
	// layers in paint order (source lists top-first; the file bottom-first)
	REQUIRE(doc.layers.size() == 4u);
	CHECK(doc.layers[0].name == "comp");
	CHECK(doc.layers[0].parent == -1);
	CHECK(doc.layers[1].name == "rig");
	CHECK(doc.layers[1].parent == 0);
	CHECK(doc.layers[2].name == "head");
	CHECK(doc.layers[2].parent == 1);
	CHECK(doc.layers[3].name == "body");
	CHECK(doc.layers[3].parent == 1);

	// rotation negated to CCW (+y up), the value bezier preserved 1:1
	VectorAnimAsset::Channel const & rot = doc.layers[1].rot;
	REQUIRE(rot.keys.size() == 3u);
	CHECK(rot.keys[0].value[0] == Approx(0.0f));
	CHECK(rot.keys[1].value[0] == Approx(-20.0f));
	CHECK(rot.keys[2].value[0] == Approx(0.0f));
	CHECK(rot.keys[0].ease.mode == VectorAnimAsset::EASE_BEZIER);
	CHECK(rot.keys[0].ease.outX == Approx(0.42f));
	CHECK(rot.keys[0].ease.inX == Approx(0.58f));

	// the topology law: path keyframes share one vertex count; the colour
	// animation rides shape keys over identical geometry
	REQUIRE(doc.layers[2].shapes.size() == 1u);
	VectorAnimAsset::Shape const & head = doc.layers[2].shapes[0];
	REQUIRE(head.keys.size() == 2u);
	CHECK(head.keys[0].region.outer.size() ==
		head.keys[1].region.outer.size());
	REQUIRE(doc.layers[3].shapes.size() == 1u);
	VectorAnimAsset::Shape const & body = doc.layers[3].shapes[0];
	REQUIRE(body.keys.size() == 2u);
	CHECK(body.keys[0].region.outer.size() >= 12u);
	CHECK(body.keys[0].region.fill.r == Approx(0.9f));
	CHECK(body.keys[1].region.fill.r == Approx(0.42f));

	// the evaluator consumes it
	VectorAnimEval eval;
	REQUIRE(eval.build(doc));
	const int idle = eval.findClip("idle");
	const int walk = eval.findClip("walk");
	REQUIRE(idle >= 0);
	REQUIRE(walk >= 0);

	// frame 0: rest pose
	VectorAnimEval::Pose pose;
	REQUIRE(eval.evaluateAt(idle, 0.0f, pose));
	CHECK(pose.layers[1].rotation == Approx(0.0f));

	// mid-clip (frame 15): the eased rotation is strictly between its keys
	REQUIRE(eval.evaluateAt(idle, 0.5f, pose));
	CHECK(pose.layers[1].rotation < -0.5f);
	CHECK(pose.layers[1].rotation > -19.5f);

	// walk starts at frame 30: the -20 degree key exactly
	REQUIRE(eval.evaluateAt(walk, 0.0f, pose));
	CHECK(pose.layers[1].rotation == Approx(-20.0f));

	// composed world regions at rest: head sits 0.6 units above the
	// origin (100 px at scale 0.01, y flipped), body at the origin
	REQUIRE(eval.evaluateAt(idle, 0.0f, pose));
	std::vector<VectorAnimEval::Region> regions;
	eval.composeRegions(pose, regions);
	REQUIRE(regions.size() == 2u);		// paint order: head below body
	VectorTessellator::Point headAt = contourCentroid(regions[0].outer);
	CHECK(headAt.x == Approx(0.0f).margin(0.01f));
	CHECK(headAt.y == Approx(0.6f).margin(0.01f));
	VectorTessellator::Point bodyAt = contourCentroid(regions[1].outer);
	CHECK(bodyAt.x == Approx(0.0f).margin(0.01f));
	CHECK(bodyAt.y == Approx(0.0f).margin(0.01f));

	// at walk's first frame the parent null has swung -20 degrees: the
	// head orbits the rig origin (rotation composes through parenting)
	REQUIRE(eval.evaluateAt(walk, 0.0f, pose));
	eval.composeRegions(pose, regions);
	headAt = contourCentroid(regions[0].outer);
	CHECK(headAt.x == Approx(0.6f * std::sin(20.0f * 3.14159265f / 180.0f))
		.margin(0.02f));
	CHECK(headAt.y == Approx(0.6f * std::cos(20.0f * 3.14159265f / 180.0f))
		.margin(0.02f));

	// the animated fill colour arrives at the walk end pose
	REQUIRE(eval.evaluateAt(walk, 1.0f, pose));	// once: clamps at frame 60
	eval.composeRegions(pose, regions);
	CHECK(regions[1].fill.r == Approx(0.42f).margin(0.01f));
	CHECK(regions[1].fill.g == Approx(0.9f).margin(0.01f));
}
