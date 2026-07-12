/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	VectorAnimEvalTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the `.oanim` pose evaluator: keyframe interpolation
	(linear/hold/bezier, boundary frames), parent-chain and opacity
	composition, clip looping and one-shot end detection, pose-level
	blending (transforms, path vertices, colours), the both-clips-advance
	crossfade ramp, evaluateAt statelessness and the allocation-free
	steady-state contract. Pure math - no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/VectorAnimAsset.h"
#include "core_util/VectorAnimEval.h"

#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	using Region = VectorTessellator::Region;
	using Point = VectorTessellator::Point;

	//! parse + build, asserting both succeed
	VectorAnimEval makeEval(String const & text)
	{
		VectorAnimAsset::Document doc;
		REQUIRE(VectorAnimAsset::parse(text, doc));
		VectorAnimEval eval;
		REQUIRE(eval.build(doc));
		return eval;
	}

	//! the crossfade weight ramp the evaluator applies (flat-tangent ease)
	float smoothstep(float t)
	{
		if(t < 0.0f) { t = 0.0f; }
		if(t > 1.0f) { t = 1.0f; }
		return t * t * (3.0f - 2.0f * t);
	}

	//! a one-layer triangle pose for hand-built blend tests
	VectorAnimEval::Pose trianglePose(float posX, float rotation,
		float vertexShift, VectorTessellator::Colour const & fill)
	{
		VectorAnimEval::Pose pose;
		pose.layers.resize(1);
		pose.layers[0].posX = posX;
		pose.layers[0].rotation = rotation;
		pose.shapes.resize(1);
		pose.shapes[0].fill = fill;
		pose.shapes[0].outer.push_back(Point(vertexShift, 0.0f));
		pose.shapes[0].outer.push_back(Point(vertexShift + 1.0f, 0.0f));
		pose.shapes[0].outer.push_back(Point(vertexShift, 1.0f));
		return pose;
	}
}

TEST_CASE("vectoranim_eval_interpolation_linear_hold_boundary",
	"[unit][vectoranim]")
{
	// pos keys at frames 10/20/30: linear into 20, HOLD from 20 until 30;
	// before the first and after the last key the boundary value holds
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 60\nlayer a parent -1\n"
		"pos k 3\n"
		"kf 10 0 0 lin\n"
		"kf 20 10 0 hold\n"
		"kf 30 20 0 lin\n");
	VectorAnimEval::Pose pose;

	REQUIRE(eval.evaluateAt(0, 0.0f, pose));		// frame 0: before first key
	CHECK(pose.layers[0].posX == Approx(0.0f));
	REQUIRE(eval.evaluateAt(0, 0.5f, pose));		// frame 15: linear midpoint
	CHECK(pose.layers[0].posX == Approx(5.0f));
	REQUIRE(eval.evaluateAt(0, 20.0f / 30.0f, pose));	// frame 20: exactly on key
	CHECK(pose.layers[0].posX == Approx(10.0f));
	REQUIRE(eval.evaluateAt(0, 25.0f / 30.0f, pose));	// frame 25: held at 10
	CHECK(pose.layers[0].posX == Approx(10.0f));
	REQUIRE(eval.evaluateAt(0, 1.0f, pose));		// frame 30: the next key snaps
	CHECK(pose.layers[0].posX == Approx(20.0f));
	REQUIRE(eval.evaluateAt(0, 40.0f / 30.0f, pose));	// frame 40: after last key
	CHECK(pose.layers[0].posX == Approx(20.0f));

	// absent channels evaluate to their documented defaults
	CHECK(pose.layers[0].scaleX == Approx(1.0f));
	CHECK(pose.layers[0].scaleY == Approx(1.0f));
	CHECK(pose.layers[0].rotation == Approx(0.0f));
	CHECK(pose.layers[0].opacity == Approx(1.0f));
	CHECK(pose.layers[0].anchorX == Approx(0.0f));
}

TEST_CASE("vectoranim_eval_bezier_easing", "[unit][vectoranim]")
{
	VectorAnimAsset::Ease ease;

	SECTION("hold and linear modes")
	{
		ease.mode = VectorAnimAsset::EASE_HOLD;
		CHECK(VectorAnimEval::evalEase(ease, 0.0f) == Approx(0.0f));
		CHECK(VectorAnimEval::evalEase(ease, 0.7f) == Approx(0.0f));
		ease.mode = VectorAnimAsset::EASE_LINEAR;
		CHECK(VectorAnimEval::evalEase(ease, 0.3f) == Approx(0.3f));
	}
	SECTION("a diagonal bezier IS the identity")
	{
		// handles on the diagonal make the value curve exactly linear
		ease.mode = VectorAnimAsset::EASE_BEZIER;
		ease.outX = 0.0f; ease.outY = 0.0f;
		ease.inX = 1.0f; ease.inY = 1.0f;
		for(float u = 0.0f; u <= 1.0f; u += 0.125f)
		{
			CHECK(VectorAnimEval::evalEase(ease, u) ==
				Approx(u).margin(1.0e-4));
		}
	}
	SECTION("a symmetric ease pins the midpoint and the endpoints")
	{
		// (0.42,0)/(0.58,1) is point-symmetric about (0.5,0.5): slow start,
		// exact midpoint, slow finish, monotone throughout
		ease.mode = VectorAnimAsset::EASE_BEZIER;
		ease.outX = 0.42f; ease.outY = 0.0f;
		ease.inX = 0.58f; ease.inY = 1.0f;
		CHECK(VectorAnimEval::evalEase(ease, 0.0f) == Approx(0.0f).margin(1.0e-5));
		CHECK(VectorAnimEval::evalEase(ease, 1.0f) == Approx(1.0f).margin(1.0e-5));
		CHECK(VectorAnimEval::evalEase(ease, 0.5f) == Approx(0.5f).margin(1.0e-4));
		CHECK(VectorAnimEval::evalEase(ease, 0.25f) < 0.25f);	// eased-in
		CHECK(VectorAnimEval::evalEase(ease, 0.75f) > 0.75f);	// eased-out
		float previous = 0.0f;
		for(float u = 0.05f; u <= 1.0f; u += 0.05f)
		{
			const float value = VectorAnimEval::evalEase(ease, u);
			CHECK(value >= previous);	// monotone
			previous = value;
		}
	}
}

TEST_CASE("vectoranim_eval_parent_chain_composition", "[unit][vectoranim]")
{
	// root rotates 90 degrees CCW and sits at (10,0); the child rides at
	// local (1,0). Its triangle must land rotated AND translated.
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 30\n"
		"layer root parent -1\n"
		"pos k 1\nkf 0 10 0\n"
		"rot k 1\nkf 0 90\n"
		"layer child parent 0\n"
		"pos k 1\nkf 0 1 0\n"
		"shape k 1\nkf 0\n"
		"fill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n");
	VectorAnimEval::Pose pose;
	REQUIRE(eval.evaluateAt(0, 0.0f, pose));

	std::vector<Region> out;
	eval.composeRegions(pose, out);
	REQUIRE(out.size() == 1u);
	REQUIRE(out[0].outer.size() == 3u);
	CHECK(out[0].outer[0].x == Approx(10.0f).margin(1.0e-4));	// (0,0)
	CHECK(out[0].outer[0].y == Approx(1.0f).margin(1.0e-4));
	CHECK(out[0].outer[1].x == Approx(10.0f).margin(1.0e-4));	// (1,0)
	CHECK(out[0].outer[1].y == Approx(2.0f).margin(1.0e-4));
	CHECK(out[0].outer[2].x == Approx(9.0f).margin(1.0e-4));	// (0,1)
	CHECK(out[0].outer[2].y == Approx(1.0f).margin(1.0e-4));
}

TEST_CASE("vectoranim_eval_anchor_scale_composition", "[unit][vectoranim]")
{
	// anchor (1,0), uniform scale 2, rot 90: the anchor point stays put and
	// the rest of the shape rotates/scales about it
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 30\n"
		"layer solo parent -1\n"
		"anchor k 1\nkf 0 1 0\n"
		"scale k 1\nkf 0 2 2\n"
		"rot k 1\nkf 0 90\n"
		"shape k 1\nkf 0\n"
		"fill 1 1 1 1\ncontour 3\nv 1 0\nv 2 0\nv 1 1\n");
	VectorAnimEval::Pose pose;
	REQUIRE(eval.evaluateAt(0, 0.0f, pose));
	std::vector<Region> out;
	eval.composeRegions(pose, out);
	REQUIRE(out.size() == 1u);
	CHECK(out[0].outer[0].x == Approx(0.0f).margin(1.0e-4));	// the anchor
	CHECK(out[0].outer[0].y == Approx(0.0f).margin(1.0e-4));
	CHECK(out[0].outer[1].x == Approx(0.0f).margin(1.0e-4));	// (2,0)
	CHECK(out[0].outer[1].y == Approx(2.0f).margin(1.0e-4));
	CHECK(out[0].outer[2].x == Approx(-2.0f).margin(1.0e-4));	// (1,1)
	CHECK(out[0].outer[2].y == Approx(0.0f).margin(1.0e-4));
}

TEST_CASE("vectoranim_eval_opacity_composition", "[unit][vectoranim]")
{
	// opacity multiplies down the parent chain and into the fill alpha
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 30\n"
		"layer root parent -1\n"
		"opacity k 1\nkf 0 0.5\n"
		"layer child parent 0\n"
		"opacity k 1\nkf 0 0.5\n"
		"shape k 1\nkf 0\n"
		"fill 0.2 0.4 0.6 0.8\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n");
	VectorAnimEval::Pose pose;
	REQUIRE(eval.evaluateAt(0, 0.0f, pose));
	std::vector<Region> out;
	eval.composeRegions(pose, out);
	REQUIRE(out.size() == 1u);
	CHECK(out[0].fill.r == Approx(0.2f));		// rgb untouched
	CHECK(out[0].fill.a == Approx(0.2f));		// 0.8 * 0.5 * 0.5
}

TEST_CASE("vectoranim_clip_playback_loop_once", "[unit][vectoranim]")
{
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 60\n"
		"clip a 0 30 loop\n"
		"clip b 30 60 once\n"
		"layer body parent -1\npos k 1\nkf 0 0 0\n");

	// a looping clip wraps its window and never ends
	eval.setClip(0);
	CHECK(eval.currentClip() == 0);
	CHECK(eval.currentFrame() == Approx(0.0f));
	eval.update(0.5f);
	CHECK(eval.currentFrame() == Approx(15.0f).margin(1.0e-3));
	eval.update(0.7f);	// cursor 1.2s -> wrapped to 0.2s
	CHECK(eval.currentFrame() == Approx(6.0f).margin(1.0e-3));
	CHECK_FALSE(eval.isAtEnd());

	// a once clip clamps at its end and reports it
	eval.setClip(1);
	CHECK(eval.currentFrame() == Approx(30.0f));	// the clip window start
	CHECK_FALSE(eval.isAtEnd());
	eval.update(2.0f);	// far past the 1s window
	CHECK(eval.currentFrame() == Approx(60.0f));
	CHECK(eval.isAtEnd());
	eval.update(0.1f);	// stays clamped
	CHECK(eval.currentFrame() == Approx(60.0f));
	CHECK(eval.isAtEnd());

	// setClip resets the cursor
	eval.setClip(1);
	CHECK(eval.currentFrame() == Approx(30.0f));
	CHECK_FALSE(eval.isAtEnd());
}

TEST_CASE("vectoranim_pose_lerp", "[unit][vectoranim]")
{
	using Colour = VectorTessellator::Colour;
	VectorAnimEval::Pose a = trianglePose(0.0f, 350.0f, 0.0f,
		Colour(1.0f, 0.0f, 0.0f, 1.0f));
	VectorAnimEval::Pose b = trianglePose(10.0f, 10.0f, 2.0f,
		Colour(0.0f, 0.0f, 1.0f, 0.5f));
	VectorAnimEval::Pose out;

	// the endpoints reproduce the inputs exactly
	REQUIRE(VectorAnimEval::blendPose(a, b, 0.0f, out));
	CHECK(out.layers[0].posX == Approx(0.0f));
	CHECK(out.shapes[0].outer[0].x == Approx(0.0f));
	CHECK(out.shapes[0].fill.r == Approx(1.0f));
	REQUIRE(VectorAnimEval::blendPose(a, b, 1.0f, out));
	CHECK(out.layers[0].posX == Approx(10.0f));
	CHECK(out.shapes[0].outer[0].x == Approx(2.0f));
	CHECK(out.shapes[0].fill.b == Approx(1.0f));

	// the midpoint lerps transforms, vertices AND colours; 2D rotation is a
	// plain scalar lerp (350 -> 10 passes through 180, no angle wrapping)
	REQUIRE(VectorAnimEval::blendPose(a, b, 0.5f, out));
	CHECK(out.layers[0].posX == Approx(5.0f));
	CHECK(out.layers[0].rotation == Approx(180.0f));
	CHECK(out.shapes[0].outer[0].x == Approx(1.0f));
	CHECK(out.shapes[0].outer[1].x == Approx(2.0f));
	CHECK(out.shapes[0].fill.r == Approx(0.5f));
	CHECK(out.shapes[0].fill.b == Approx(0.5f));
	CHECK(out.shapes[0].fill.a == Approx(0.75f));

	// the weight clamps to 0..1
	REQUIRE(VectorAnimEval::blendPose(a, b, 2.0f, out));
	CHECK(out.layers[0].posX == Approx(10.0f));

	// a structure mismatch never blends (different rigs)
	VectorAnimEval::Pose mismatch = trianglePose(0.0f, 0.0f, 0.0f,
		Colour(1.0f, 1.0f, 1.0f, 1.0f));
	mismatch.shapes[0].outer.push_back(Point(3.0f, 3.0f));	// 4th vertex
	CHECK_FALSE(VectorAnimEval::blendPose(a, mismatch, 0.5f, out));
	VectorAnimEval::Pose extraLayer = trianglePose(0.0f, 0.0f, 0.0f,
		Colour(1.0f, 1.0f, 1.0f, 1.0f));
	extraLayer.layers.resize(2);
	CHECK_FALSE(VectorAnimEval::blendPose(a, extraLayer, 0.5f, out));
}

TEST_CASE("vectoranim_crossfade", "[unit][vectoranim]")
{
	// rot ramps 0 -> 300 over clip a's window; clip b holds 300 throughout
	// its own window. The crossfade must keep BOTH clips advancing while the
	// smoothstepped weight ramps 0 -> 1.
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 60\n"
		"clip a 0 30 loop\n"
		"clip b 30 60 loop\n"
		"layer body parent -1\n"
		"rot k 2\nkf 0 0 lin\nkf 30 300 lin\n");

	eval.setClip(0);
	eval.update(0.2f);	// clip a at 0.2s -> rot 60
	CHECK(eval.pose().layers[0].rotation == Approx(60.0f).margin(1.0e-3));
	CHECK_FALSE(eval.isCrossFading());

	eval.crossFadeTo(1, 1.0f);
	CHECK(eval.isCrossFading());
	CHECK(eval.currentClip() == 1);

	// mid-fade: the outgoing clip ADVANCED (a at 0.45s -> rot 135) and the
	// weight is smoothstep(0.25); the exact blend pins both-clips-advance
	eval.update(0.25f);
	const float w1 = smoothstep(0.25f);
	CHECK(eval.crossFadeWeight() == Approx(w1).margin(1.0e-4));
	const float expected1 = 135.0f + w1 * (300.0f - 135.0f);
	CHECK(eval.pose().layers[0].rotation ==
		Approx(expected1).margin(1.0e-2));
	// the blend differs from BOTH pure poses
	CHECK(eval.pose().layers[0].rotation != Approx(135.0f).margin(1.0f));
	CHECK(eval.pose().layers[0].rotation != Approx(300.0f).margin(1.0f));

	// the weight ramp is monotone
	eval.update(0.25f);
	const float w2 = smoothstep(0.5f);
	CHECK(w2 > w1);
	CHECK(eval.crossFadeWeight() == Approx(w2).margin(1.0e-4));
	const float expected2 = 210.0f + w2 * (300.0f - 210.0f);	// a at 0.7s
	CHECK(eval.pose().layers[0].rotation ==
		Approx(expected2).margin(1.0e-2));

	// completion: the outgoing clip is dropped, evaluation is single-clip
	eval.update(0.6f);
	CHECK_FALSE(eval.isCrossFading());
	CHECK(eval.currentClip() == 1);
	CHECK(eval.crossFadeWeight() == Approx(1.0f));
	CHECK(eval.pose().layers[0].rotation == Approx(300.0f).margin(1.0e-3));

	// a zero-length fade is an immediate clip switch
	eval.crossFadeTo(0, 0.0f);
	CHECK_FALSE(eval.isCrossFading());
	CHECK(eval.currentClip() == 0);
	CHECK(eval.currentFrame() == Approx(0.0f));
}

TEST_CASE("vectoranim_evaluateAt_stateless", "[unit][vectoranim]")
{
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 60\n"
		"clip a 0 30 loop\n"
		"clip b 30 60 loop\n"
		"layer body parent -1\n"
		"rot k 2\nkf 0 0 lin\nkf 60 600 lin\n");
	eval.setClip(0);
	eval.update(0.4f);
	const float frameBefore = eval.currentFrame();
	const float rotationBefore = eval.pose().layers[0].rotation;

	// two identical stateless queries agree and disturb nothing
	VectorAnimEval::Pose first, second;
	REQUIRE(eval.evaluateAt(1, 0.3f, first));
	REQUIRE(eval.evaluateAt(1, 0.3f, second));
	CHECK(first.layers[0].rotation == Approx(second.layers[0].rotation));
	CHECK(first.layers[0].rotation == Approx(390.0f).margin(1.0e-3));	// frame 39
	CHECK(eval.currentFrame() == Approx(frameBefore));
	CHECK(eval.pose().layers[0].rotation == Approx(rotationBefore));

	// a bad clip index is refused honestly
	CHECK_FALSE(eval.evaluateAt(7, 0.0f, first));
	CHECK_FALSE(eval.evaluateAt(-1, 0.0f, first));

	// an unbuilt evaluator refuses everything and no-ops safely
	VectorAnimEval unbuilt;
	CHECK_FALSE(unbuilt.isBuilt());
	CHECK_FALSE(unbuilt.evaluateAt(0, 0.0f, first));
	unbuilt.update(0.1f);
	unbuilt.setClip(0);
	CHECK_FALSE(unbuilt.isAtEnd());
}

TEST_CASE("vectoranim_eval_rejects_unsound_documents", "[unit][vectoranim]")
{
	// build() guards hand-built documents with the same honesty as the
	// parser: fixed topology across a shape's keys, parents before children
	VectorAnimAsset::Document doc;
	REQUIRE(VectorAnimAsset::parse(
		"fps 30\nduration 30\nlayer a parent -1\n"
		"shape k 1\nkf 0\nfill 1 1 1 1\ncontour 3\nv 0 0\nv 1 0\nv 0 1\n",
		doc));

	SECTION("mismatched shape-key topology")
	{
		VectorAnimAsset::ShapeKey key = doc.layers[0].shapes[0].keys[0];
		key.frame = 10.0f;
		key.region.outer.push_back(VectorTessellator::Point(2.0f, 2.0f));
		doc.layers[0].shapes[0].keys.push_back(key);
		VectorAnimEval eval;
		CHECK_FALSE(eval.build(doc));
		CHECK_FALSE(eval.isBuilt());
	}
	SECTION("a parent that does not precede its child")
	{
		doc.layers[0].parent = 0;	// self-parent
		VectorAnimEval eval;
		CHECK_FALSE(eval.build(doc));
	}
	SECTION("a shape without keys")
	{
		doc.layers[0].shapes[0].keys.clear();
		VectorAnimEval eval;
		CHECK_FALSE(eval.build(doc));
	}
}

TEST_CASE("vectoranim_allocation_stability", "[unit][vectoranim]")
{
	// the SoftBodyDeform contract: every buffer is sized at build; the
	// steady-state tick (including crossfades) never grows a capacity
	VectorAnimEval eval = makeEval(
		"fps 30\nduration 60\n"
		"clip a 0 30 loop\n"
		"clip b 30 60 loop\n"
		"layer root parent -1\n"
		"pos k 2\nkf 0 0 0 lin\nkf 30 4 0 lin\n"
		"layer body parent 0\n"
		"rot k 2\nkf 0 0 lin\nkf 60 360 lin\n"
		"shape k 2\n"
		"kf 0\nfill 1 0 0 1\ncontour 4\nv -1 -1\nv 1 -1\nv 1 1\nv -1 1\n"
		"hole 3\nv -0.2 -0.2\nv 0.2 -0.2\nv 0 0.2\n"
		"kf 30\nfill 0 1 0 1\ncontour 4\nv -2 -1\nv 2 -1\nv 2 1\nv -2 1\n"
		"hole 3\nv -0.2 -0.2\nv 0.2 -0.2\nv 0 0.2\n");

	// warm every path once: plain playback, a full crossfade, composition
	std::vector<Region> out;
	eval.setClip(0);
	eval.crossFadeTo(1, 0.1f);
	for(int i = 0; i < 12; ++i)
	{
		eval.update(1.0f / 60.0f);
		eval.writeRegions(out);
	}
	REQUIRE(out.size() == 1u);
	REQUIRE(out[0].outer.size() == 4u);

	const std::size_t outCapacity = out.capacity();
	const std::size_t outerCapacity = out[0].outer.capacity();
	const std::size_t holesCapacity = out[0].holes.capacity();
	const std::size_t holeCapacity = out[0].holes[0].capacity();
	const std::size_t poseOuterCapacity = eval.pose().shapes[0].outer.capacity();
	const std::size_t poseLayerCapacity = eval.pose().layers.capacity();

	// a long steady-state run with another crossfade in the middle
	eval.crossFadeTo(0, 0.5f);
	for(int i = 0; i < 300; ++i)
	{
		eval.update(1.0f / 60.0f);
		eval.writeRegions(out);
		if(i == 150)
		{
			eval.crossFadeTo(1, 0.25f);
		}
	}

	CHECK(out.capacity() == outCapacity);
	CHECK(out[0].outer.capacity() == outerCapacity);
	CHECK(out[0].holes.capacity() == holesCapacity);
	CHECK(out[0].holes[0].capacity() == holeCapacity);
	CHECK(eval.pose().shapes[0].outer.capacity() == poseOuterCapacity);
	CHECK(eval.pose().layers.capacity() == poseLayerCapacity);

	// the composed output keeps the CONSTANT topology consumers rely on
	CHECK(out.size() == 1u);
	CHECK(out[0].outer.size() == 4u);
	REQUIRE(out[0].holes.size() == 1u);
	CHECK(out[0].holes[0].size() == 3u);
}
