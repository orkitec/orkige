/**************************************************************
	created:	2026/07/29 at 21:00
	filename: 	VectorShapeCookTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the source-independent half of the `.oshape` cook: the
	`.oshape` WRITER (a round trip through the parser that reads it, and the
	grammar level it declares), the placement transform (center, scale to the
	requested world extent, flip y), the tolerance rule, the fixed-count flatten
	a morph set needs, the straight-cubic verdict, hole containment and the
	topology refusal. Pure - no SVG, no renderer.
***************************************************************/

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core_util/VectorShapeAsset.h"
#include "core_util/VectorShapeCook.h"

#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	typedef VectorTessellator::Point Point;
	typedef VectorTessellator::Region Region;

	//! an axis-aligned box region in a drawing's y-DOWN space
	Region box(float x, float y, float w, float h,
		VectorTessellator::Colour const & colour)
	{
		Region region;
		region.fill = colour;
		region.outer.push_back(Point(x, y));
		region.outer.push_back(Point(x + w, y));
		region.outer.push_back(Point(x + w, y + h));
		region.outer.push_back(Point(x, y + h));
		return region;
	}
}

TEST_CASE("shapecook_serialize_round_trips_through_the_parser",
	"[unit][vectorshape]")
{
	VectorShapeAsset::ParsedShape shape;
	Region filled = box(-1.0f, -1.0f, 2.0f, 2.0f,
		VectorTessellator::Colour(0.25f, 0.5f, 0.75f, 1.0f));
	filled.holes.push_back(std::vector<Point>());
	filled.holes.back().push_back(Point(-0.5f, -0.5f));
	filled.holes.back().push_back(Point(0.5f, -0.5f));
	filled.holes.back().push_back(Point(0.5f, 0.5f));
	shape.base.push_back(filled);

	const String text = VectorShapeAsset::serialize(shape, "a test shape");
	// the banner is comment lines, and a flat-colour shape declares v1
	REQUIRE(text.find("# a test shape\n") == 0);
	REQUIRE(text.find("version 1\n") != String::npos);

	std::vector<Region> parsed;
	REQUIRE(VectorShapeAsset::parse(text, parsed));
	REQUIRE(parsed.size() == 1);
	REQUIRE(parsed[0].outer.size() == 4);
	REQUIRE(parsed[0].holes.size() == 1);
	REQUIRE(parsed[0].holes[0].size() == 3);
	CHECK(parsed[0].fill.g == Approx(0.5f).margin(1.0e-4));
	CHECK(parsed[0].outer[2].x == Approx(1.0f).margin(1.0e-5));

	SECTION("a stroke or mask lifts the declared grammar level to 2")
	{
		Region stroke;
		stroke.kind = VectorTessellator::REGION_STROKE;
		stroke.strokeWidth = 0.25f;
		stroke.strokeCap = VectorTessellator::CAP_ROUND;
		stroke.strokeJoin = VectorTessellator::JOIN_BEVEL;
		stroke.strokeClosed = true;
		stroke.outer.push_back(Point(0.0f, 0.0f));
		stroke.outer.push_back(Point(1.0f, 0.0f));
		stroke.outer.push_back(Point(1.0f, 1.0f));
		shape.base.push_back(stroke);
		const String withStroke = VectorShapeAsset::serialize(shape);
		REQUIRE(withStroke.find("version 2\n") != String::npos);
		std::vector<Region> back;
		REQUIRE(VectorShapeAsset::parse(withStroke, back));
		REQUIRE(back.size() == 2);
		CHECK(back[1].kind == VectorTessellator::REGION_STROKE);
		CHECK(back[1].strokeCap == VectorTessellator::CAP_ROUND);
		CHECK(back[1].strokeJoin == VectorTessellator::JOIN_BEVEL);
		CHECK(back[1].strokeClosed);
		CHECK(back[1].strokeWidth == Approx(0.25f).margin(1.0e-5));
	}

	SECTION("a texture region lifts it to 3 and keeps its rect and window")
	{
		Region cutout = box(-1.0f, -1.0f, 2.0f, 2.0f,
			VectorTessellator::Colour(1.0f, 1.0f, 1.0f, 1.0f));
		cutout.texture = "arm.png";
		cutout.textureRectMin = Point(-1.0f, -1.0f);
		cutout.textureRectMax = Point(1.0f, 1.0f);
		cutout.uvMin = Point(0.25f, 0.0f);
		cutout.uvMax = Point(0.75f, 0.5f);
		VectorShapeAsset::ParsedShape textured;
		textured.base.push_back(cutout);
		const String out = VectorShapeAsset::serialize(textured);
		REQUIRE(out.find("version 3\n") != String::npos);
		std::vector<Region> back;
		REQUIRE(VectorShapeAsset::parse(out, back));
		REQUIRE(back.size() == 1);
		CHECK(back[0].texture == "arm.png");
		CHECK(back[0].textureRectMax.y == Approx(1.0f).margin(1.0e-5));
		CHECK(back[0].uvMin.x == Approx(0.25f).margin(1.0e-5));
		CHECK(back[0].uvMax.y == Approx(0.5f).margin(1.0e-5));
	}

	SECTION("morph targets survive, and an unnamed one gets a name")
	{
		VectorShapeAsset::MorphTarget target;
		target.regions.push_back(box(-2.0f, -0.5f, 4.0f, 1.0f,
			VectorTessellator::Colour(0.25f, 0.5f, 0.75f, 1.0f)));
		shape.morphs.push_back(target);
		const String out = VectorShapeAsset::serialize(shape);
		VectorShapeAsset::ParsedShape back;
		REQUIRE(VectorShapeAsset::parse(out, back));
		REQUIRE(back.morphs.size() == 1);
		// the parser accepts an empty name but a written one must be readable
		CHECK(back.morphs[0].name == "target");
		CHECK(back.morphs[0].regions.size() == 1);
	}
}

TEST_CASE("shapecook_place_centers_scales_and_flips", "[unit][vectorshape]")
{
	// a 40 x 20 box at (10, 30) in y-DOWN drawing space
	VectorShapeAsset::ParsedShape shape;
	shape.base.push_back(box(10.0f, 30.0f, 40.0f, 20.0f,
		VectorTessellator::Colour(1.0f, 1.0f, 1.0f, 1.0f)));

	REQUIRE(VectorShapeCook::place(shape, 2.0));
	// the LARGER extent (40) spans 2 world units, so x runs -1..1 and the
	// smaller axis keeps the aspect (20 -> 1 unit, -0.5..0.5)
	CHECK(shape.base[0].outer[0].x == Approx(-1.0f).margin(1.0e-5));
	CHECK(shape.base[0].outer[1].x == Approx(1.0f).margin(1.0e-5));
	// y flips: the drawing's TOP edge (the smaller y) becomes the larger y
	CHECK(shape.base[0].outer[0].y == Approx(0.5f).margin(1.0e-5));
	CHECK(shape.base[0].outer[2].y == Approx(-0.5f).margin(1.0e-5));

	SECTION("an empty shape has nothing to place")
	{
		VectorShapeAsset::ParsedShape nothing;
		CHECK_FALSE(VectorShapeCook::place(nothing, 2.0));
		String text;
		CHECK_FALSE(VectorShapeCook::emit(nothing, 2.0, "", text));
		CHECK(text.empty());
	}

	SECTION("every morph target rides the BASE's transform")
	{
		VectorShapeAsset::ParsedShape morphed;
		morphed.base.push_back(box(0.0f, 0.0f, 100.0f, 100.0f,
			VectorTessellator::Colour(1.0f, 1.0f, 1.0f, 1.0f)));
		VectorShapeAsset::MorphTarget target;
		// a pose that reaches BEYOND the base must land beyond it in world
		// units too - a per-pose normalisation would squash it back inside
		target.name = "big";
		target.regions.push_back(box(-50.0f, -50.0f, 200.0f, 200.0f,
			VectorTessellator::Colour(1.0f, 1.0f, 1.0f, 1.0f)));
		morphed.morphs.push_back(target);
		REQUIRE(VectorShapeCook::place(morphed, 2.0));
		CHECK(morphed.base[0].outer[1].x == Approx(1.0f).margin(1.0e-5));
		// the base's right edge lands at x = 1; the pose reaches to 150 in
		// drawing units, which is 2 world units out on the base's scale
		CHECK(morphed.morphs[0].regions[0].outer[1].x ==
			Approx(2.0f).margin(1.0e-5));
	}

	SECTION("the extent option scales the result")
	{
		VectorShapeAsset::ParsedShape wide;
		wide.base.push_back(box(0.0f, 0.0f, 10.0f, 10.0f,
			VectorTessellator::Colour(1.0f, 1.0f, 1.0f, 1.0f)));
		REQUIRE(VectorShapeCook::place(wide, 6.0));
		CHECK(wide.base[0].outer[1].x == Approx(3.0f).margin(1.0e-5));
	}
}

TEST_CASE("shapecook_tolerance_is_relative_to_the_document",
	"[unit][vectorshape]")
{
	VectorShapeCook::Options options;
	// the historical 100-unit drawing resolves to a chord tolerance of 1.0
	CHECK(VectorShapeCook::resolveTolerance(options, 100.0, 100.0) ==
		Approx(1.0));
	// the SAME artwork at ten times the scale flattens to the same contours
	CHECK(VectorShapeCook::resolveTolerance(options, 1000.0, 500.0) ==
		Approx(10.0));
	// the LARGER side governs
	CHECK(VectorShapeCook::resolveTolerance(options, 50.0, 200.0) ==
		Approx(2.0));
	// an explicit tolerance is taken verbatim
	options.tolerance = 0.125;
	CHECK(VectorShapeCook::resolveTolerance(options, 100.0, 100.0) ==
		Approx(0.125));
	// a document that declares no size still gets a finite, non-zero tolerance
	options.tolerance = 0.0;
	CHECK(VectorShapeCook::resolveTolerance(options, 0.0, 0.0) > 0.0);
}

TEST_CASE("shapecook_uniform_flatten_is_count_stable", "[unit][vectorshape]")
{
	std::vector<Point> a;
	std::vector<Point> b;
	// two DIFFERENT curves flatten to the same vertex count, which is what
	// makes two morph poses blendable
	VectorShapeCook::flattenCubicUniform(Point(0.0f, 0.0f), Point(0.0f, 10.0f),
		Point(10.0f, 10.0f), Point(10.0f, 0.0f),
		VectorShapeCook::UNIFORM_CURVE_SEGMENTS, a);
	VectorShapeCook::flattenCubicUniform(Point(0.0f, 0.0f), Point(0.0f, 1.0f),
		Point(1.0f, 1.0f), Point(1.0f, 0.0f),
		VectorShapeCook::UNIFORM_CURVE_SEGMENTS, b);
	REQUIRE(a.size() == std::size_t(VectorShapeCook::UNIFORM_CURVE_SEGMENTS));
	REQUIRE(a.size() == b.size());
	// the append contract EXCLUDES p0 and ends exactly on p3
	CHECK(a.back().x == Approx(10.0f).margin(1.0e-4));
	CHECK(a.back().y == Approx(0.0f).margin(1.0e-4));

	SECTION("a quadratic elevates to the same count and endpoint")
	{
		std::vector<Point> quad;
		VectorShapeCook::flattenQuadraticUniform(Point(0.0f, 0.0f),
			Point(5.0f, 10.0f), Point(10.0f, 0.0f),
			VectorShapeCook::UNIFORM_CURVE_SEGMENTS, quad);
		REQUIRE(quad.size() ==
			std::size_t(VectorShapeCook::UNIFORM_CURVE_SEGMENTS));
		CHECK(quad.back().x == Approx(10.0f).margin(1.0e-4));
		// the apex of this parabola is half the control height
		CHECK(quad[4].y == Approx(5.0f).margin(0.5));
	}
}

TEST_CASE("shapecook_straight_cubic_is_recognised", "[unit][vectorshape]")
{
	// a LINE written as a cubic: the controls sit at the chord's thirds
	CHECK(VectorShapeCook::isStraightCubic(Point(0.0f, 0.0f),
		Point(10.0f / 3.0f, 0.0f), Point(20.0f / 3.0f, 0.0f),
		Point(10.0f, 0.0f)));
	// a diagonal line, same shape
	CHECK(VectorShapeCook::isStraightCubic(Point(1.0f, 2.0f), Point(3.0f, 4.0f),
		Point(5.0f, 6.0f), Point(7.0f, 8.0f)));
	// a real curve is not
	CHECK_FALSE(VectorShapeCook::isStraightCubic(Point(0.0f, 0.0f),
		Point(0.0f, 10.0f), Point(10.0f, 10.0f), Point(10.0f, 0.0f)));
	// the verdict is SCALE-INVARIANT: the same bend at any size is a curve
	CHECK_FALSE(VectorShapeCook::isStraightCubic(Point(0.0f, 0.0f),
		Point(0.0f, 0.01f), Point(0.01f, 0.01f), Point(0.01f, 0.0f)));
	// a degenerate chord with the controls on the point is straight
	CHECK(VectorShapeCook::isStraightCubic(Point(4.0f, 4.0f), Point(4.0f, 4.0f),
		Point(4.0f, 4.0f), Point(4.0f, 4.0f)));
}

TEST_CASE("shapecook_containment_finds_holes", "[unit][vectorshape]")
{
	std::vector<Point> square;
	square.push_back(Point(0.0f, 0.0f));
	square.push_back(Point(10.0f, 0.0f));
	square.push_back(Point(10.0f, 10.0f));
	square.push_back(Point(0.0f, 10.0f));
	CHECK(VectorShapeCook::containsPoint(square, Point(5.0f, 5.0f)));
	CHECK_FALSE(VectorShapeCook::containsPoint(square, Point(15.0f, 5.0f)));
	CHECK_FALSE(VectorShapeCook::containsPoint(square, Point(-1.0f, 5.0f)));
	// a concave outline still answers correctly (the notch is OUTSIDE)
	std::vector<Point> notched;
	notched.push_back(Point(0.0f, 0.0f));
	notched.push_back(Point(10.0f, 0.0f));
	notched.push_back(Point(10.0f, 10.0f));
	notched.push_back(Point(6.0f, 10.0f));
	notched.push_back(Point(6.0f, 4.0f));
	notched.push_back(Point(4.0f, 4.0f));
	notched.push_back(Point(4.0f, 10.0f));
	notched.push_back(Point(0.0f, 10.0f));
	CHECK(VectorShapeCook::containsPoint(notched, Point(5.0f, 2.0f)));
	CHECK_FALSE(VectorShapeCook::containsPoint(notched, Point(5.0f, 8.0f)));
	// a degenerate loop contains nothing
	std::vector<Point> tooFew;
	tooFew.push_back(Point(0.0f, 0.0f));
	tooFew.push_back(Point(1.0f, 1.0f));
	CHECK_FALSE(VectorShapeCook::containsPoint(tooFew, Point(0.5f, 0.5f)));
}

TEST_CASE("shapecook_topology_mismatch_is_refused", "[unit][vectorshape]")
{
	const VectorTessellator::Colour white(1.0f, 1.0f, 1.0f, 1.0f);
	std::vector<Region> base;
	base.push_back(box(0.0f, 0.0f, 10.0f, 10.0f, white));
	std::vector<Region> matching;
	matching.push_back(box(-5.0f, 2.0f, 20.0f, 6.0f, white));
	String error = "untouched";
	CHECK(VectorShapeCook::checkTopology(base, matching, "squash", &error));
	CHECK(error == "untouched");

	SECTION("a different vertex count names both structures")
	{
		std::vector<Region> triangle;
		triangle.push_back(box(0.0f, 0.0f, 10.0f, 10.0f, white));
		triangle[0].outer.pop_back();
		error.clear();
		CHECK_FALSE(VectorShapeCook::checkTopology(base, triangle, "bad",
			&error));
		CHECK(error.find("morph target 'bad'") != String::npos);
		CHECK(error.find("[3]") != String::npos);
		CHECK(error.find("[4]") != String::npos);
		CHECK(error.find("does not match the base") != String::npos);
	}

	SECTION("a different region count is a mismatch too")
	{
		std::vector<Region> twoRegions;
		twoRegions.push_back(box(0.0f, 0.0f, 10.0f, 10.0f, white));
		twoRegions.push_back(box(20.0f, 0.0f, 10.0f, 10.0f, white));
		error.clear();
		CHECK_FALSE(VectorShapeCook::checkTopology(base, twoRegions, "", &error));
		CHECK(error.find("morph target 'target'") != String::npos);
		CHECK(error.find("[4, 4]") != String::npos);
	}
}
