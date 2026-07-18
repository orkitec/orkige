/**************************************************************
	created:	2026/07/10 at 10:00
	filename: 	VectorTessellatorTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless geometry tests for the flat-colour vector-shape tessellator:
	Bezier flattening tolerance, earcut triangulation (convex / concave /
	with holes) with area conservation, and the alpha-ramp feather strip.
	Pure math - no window, no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/VectorTessellator.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	using Point = VectorTessellator::Point;
	using Colour = VectorTessellator::Colour;
	using Region = VectorTessellator::Region;
	using Mesh = VectorTessellator::Mesh;

	//! summed unsigned area of every triangle in a mesh's index list
	float meshTriangleArea(Mesh const & mesh)
	{
		float total = 0.0f;
		for(std::size_t t = 0; t < mesh.indices.size(); t += 3)
		{
			Point const & a = mesh.positions[mesh.indices[t + 0]];
			Point const & b = mesh.positions[mesh.indices[t + 1]];
			Point const & c = mesh.positions[mesh.indices[t + 2]];
			total += std::abs((b.x - a.x) * (c.y - a.y) -
				(c.x - a.x) * (b.y - a.y)) * 0.5f;
		}
		return total;
	}

	Region squareRegion(float half)
	{
		Region region;
		region.fill = Colour(0.9f, 0.4f, 0.4f, 1.0f);
		region.outer.push_back(Point(-half, -half));
		region.outer.push_back(Point(half, -half));
		region.outer.push_back(Point(half, half));
		region.outer.push_back(Point(-half, half));
		return region;
	}
}

TEST_CASE("vectortess_flatten_cubic_tolerance", "[unit][vectortess]")
{
	// a symmetric arch cubic
	const Point p0(0.0f, 0.0f);
	const Point p1(0.0f, 1.0f);
	const Point p2(1.0f, 1.0f);
	const Point p3(1.0f, 0.0f);

	std::vector<Point> coarse;
	VectorTessellator::flattenCubic(p0, p1, p2, p3, 0.1f, coarse);
	std::vector<Point> fine;
	VectorTessellator::flattenCubic(p0, p1, p2, p3, 0.001f, fine);

	// a tighter tolerance always yields at least as many points, and here
	// strictly more (the curve is far from straight)
	CHECK(fine.size() > coarse.size());
	// the flattener appends everything EXCEPT p0, ending exactly on p3
	CHECK(coarse.back().x == p3.x);
	CHECK(coarse.back().y == p3.y);
	CHECK(fine.back().x == p3.x);
	CHECK(fine.back().y == p3.y);

	// a straight (collinear controls) cubic flattens to a single endpoint
	std::vector<Point> straight;
	VectorTessellator::flattenCubic(Point(0, 0), Point(1, 0), Point(2, 0),
		Point(3, 0), 0.001f, straight);
	CHECK(straight.size() == 1u);
}

TEST_CASE("vectortess_triangulate_convex_area", "[unit][vectortess]")
{
	Mesh mesh;
	// a unit square (side 2): area 4, triangulates to 2 triangles
	VectorTessellator::triangulateFill(squareRegion(1.0f), mesh);
	CHECK(mesh.triangleCount() == 2u);
	CHECK(meshTriangleArea(mesh) == Approx(4.0f).margin(1e-4f));
	// every fill vertex carries the region's fill colour
	for(Colour const & colour : mesh.colours)
	{
		CHECK(colour.r == Approx(0.9f));
		CHECK(colour.a == Approx(1.0f));
	}

	// a regular pentagon: N-2 = 3 triangles, area conserved (shoelace)
	Region pentagon;
	pentagon.fill = Colour(1, 1, 1, 1);
	float polygonArea = 0.0f;
	std::vector<Point> pts;
	for(int i = 0; i < 5; ++i)
	{
		const float angle = 3.14159265f * 2.0f * static_cast<float>(i) / 5.0f;
		pts.push_back(Point(std::cos(angle), std::sin(angle)));
	}
	pentagon.outer = pts;
	for(std::size_t i = 0; i < pts.size(); ++i)
	{
		Point const & a = pts[i];
		Point const & b = pts[(i + 1) % pts.size()];
		polygonArea += (a.x * b.y - b.x * a.y);
	}
	polygonArea = std::abs(polygonArea) * 0.5f;
	Mesh pentaMesh;
	VectorTessellator::triangulateFill(pentagon, pentaMesh);
	CHECK(pentaMesh.triangleCount() == 3u);
	CHECK(meshTriangleArea(pentaMesh) == Approx(polygonArea).margin(1e-4f));
}

TEST_CASE("vectortess_triangulate_concave", "[unit][vectortess]")
{
	// an L-shape (concave): 6 vertices, area = 3 unit cells. earcut gives
	// N-2 = 4 triangles and the triangulation must conserve area exactly.
	Region shape;
	shape.fill = Colour(0.2f, 0.6f, 0.3f, 1.0f);
	shape.outer.push_back(Point(0, 0));
	shape.outer.push_back(Point(2, 0));
	shape.outer.push_back(Point(2, 1));
	shape.outer.push_back(Point(1, 1));
	shape.outer.push_back(Point(1, 2));
	shape.outer.push_back(Point(0, 2));
	Mesh mesh;
	VectorTessellator::triangulateFill(shape, mesh);
	CHECK(mesh.triangleCount() == 4u);
	CHECK(meshTriangleArea(mesh) == Approx(3.0f).margin(1e-4f));
}

TEST_CASE("vectortess_triangulate_with_hole", "[unit][vectortess]")
{
	// a 4x4 square with a 2x2 square hole: filled area = 16 - 4 = 12.
	// the hole winds OPPOSITE the outer, as earcut expects.
	Region shape = squareRegion(2.0f);
	std::vector<Point> hole;
	hole.push_back(Point(-1, -1));
	hole.push_back(Point(-1, 1));
	hole.push_back(Point(1, 1));
	hole.push_back(Point(1, -1));
	shape.holes.push_back(hole);
	Mesh mesh;
	VectorTessellator::triangulateFill(shape, mesh);
	CHECK(mesh.triangleCount() > 0u);
	CHECK(meshTriangleArea(mesh) == Approx(12.0f).margin(1e-3f));
}

TEST_CASE("vectortess_feather_alpha_ramp", "[unit][vectortess]")
{
	// a square feathered by a known width: the ring's inner verts keep the
	// fill alpha, the outer verts drop to 0, and the ring area is close to
	// perimeter * width (a thin strip).
	const float half = 1.0f;
	const float width = 0.05f;
	Region region = squareRegion(half);	// alpha 1 fill
	Mesh mesh;
	VectorTessellator::appendFeather(region.outer, region.fill, width, mesh);

	int innerOpaque = 0;
	int outerClear = 0;
	for(Colour const & colour : mesh.colours)
	{
		if(colour.a == Approx(1.0f))
		{
			++innerOpaque;
		}
		if(colour.a == Approx(0.0f))
		{
			++outerClear;
		}
	}
	// one inner + one outer vertex per contour vertex (4 each)
	CHECK(innerOpaque == 4);
	CHECK(outerClear == 4);
	// the ring is a thin strip roughly perimeter*width; at convex corners the
	// unit-normal offset pulls the strip slightly inward, so the area lands a
	// bit UNDER perimeter*width (never over) - a sane band, not an exact value
	const float perimeter = 8.0f * half;	// square side 2*half, 4 sides
	const float ringArea = meshTriangleArea(mesh);
	CHECK(ringArea > perimeter * width * 0.6f);
	CHECK(ringArea < perimeter * width * 1.05f);

	// width <= 0 produces no feather geometry
	Mesh empty;
	VectorTessellator::appendFeather(region.outer, region.fill, 0.0f, empty);
	CHECK(empty.indices.empty());
}

TEST_CASE("vectortess_build_multi_region", "[unit][vectortess]")
{
	// two fills in one shape -> two colour groups, one mesh. With a feather
	// the mesh carries both fill triangles and the ramped edge strips.
	std::vector<Region> regions;
	Region body = squareRegion(1.0f);
	body.fill = Colour(0.9f, 0.4f, 0.4f, 1.0f);
	Region accent = squareRegion(0.25f);
	accent.fill = Colour(0.1f, 0.1f, 0.2f, 1.0f);
	regions.push_back(body);
	regions.push_back(accent);

	Mesh mesh;
	VectorTessellator::build(regions, 0.02f, mesh);
	CHECK(mesh.triangleCount() > 4u);	// 2 fills (4 tris) + feather strips
	CHECK(mesh.positions.size() == mesh.colours.size());
	CHECK(mesh.bounds.valid);
	CHECK(mesh.bounds.minX == Approx(-1.0f));
	CHECK(mesh.bounds.maxX == Approx(1.0f));

	// both fill colours are present in the vertex stream
	bool sawBody = false, sawAccent = false;
	for(Colour const & colour : mesh.colours)
	{
		if(colour.r == Approx(0.9f) && colour.a == Approx(1.0f))
		{
			sawBody = true;
		}
		if(colour.b == Approx(0.2f) && colour.a == Approx(1.0f))
		{
			sawAccent = true;
		}
	}
	CHECK(sawBody);
	CHECK(sawAccent);
}

TEST_CASE("vectortess_default_feather_scales_with_bounds", "[unit][vectortess]")
{
	// the default feather is a fraction of the bounding diagonal, so a bigger
	// shape gets a proportionally wider soft edge (constant visual weight)
	std::vector<Region> small;
	small.push_back(squareRegion(1.0f));
	std::vector<Region> big;
	big.push_back(squareRegion(10.0f));
	const float smallWidth =
		VectorTessellator::defaultFeatherWidth(
			VectorTessellator::computeBounds(small));
	const float bigWidth =
		VectorTessellator::defaultFeatherWidth(
			VectorTessellator::computeBounds(big));
	CHECK(bigWidth == Approx(smallWidth * 10.0f).margin(1e-4f));
	CHECK(smallWidth > 0.0f);
}

TEST_CASE("vectortess_gpu_vertex_gradients", "[unit][vectortess][gradient]")
{
	Region linear = squareRegion(1.0f);
	linear.paintType = VectorTessellator::PAINT_LINEAR_GRADIENT;
	linear.gradientStart = Point(-1.0f, 0.0f);
	linear.gradientEnd = Point(1.0f, 0.0f);
	linear.gradientStops.push_back(VectorTessellator::GradientStop(
		0.0f, Colour(1, 0, 0, 1)));
	linear.gradientStops.push_back(VectorTessellator::GradientStop(
		1.0f, Colour(0, 0, 1, 1)));
	Mesh mesh;
	VectorTessellator::triangulateFill(linear, mesh);
	REQUIRE(mesh.triangleCount() > 2u); // deterministic gradient subdivision
	bool sawRed = false, sawBlue = false, sawBlend = false;
	for(Colour const & colour : mesh.colours)
	{
		sawRed |= colour.r > 0.95f && colour.b < 0.05f;
		sawBlue |= colour.b > 0.95f && colour.r < 0.05f;
		sawBlend |= colour.r > 0.35f && colour.r < 0.65f &&
			colour.b > 0.35f && colour.b < 0.65f;
	}
	CHECK(sawRed);
	CHECK(sawBlue);
	CHECK(sawBlend);

	Region radial = linear;
	radial.paintType = VectorTessellator::PAINT_RADIAL_GRADIENT;
	radial.gradientStart = Point(0.0f, 0.0f);
	radial.gradientEnd = Point(1.0f, 0.0f);
	Mesh radialMesh;
	VectorTessellator::triangulateFill(radial, radialMesh);
	bool sawCentreRed = false, sawCornerBlue = false;
	for(std::size_t i = 0; i < radialMesh.positions.size(); ++i)
	{
		Point const & point = radialMesh.positions[i];
		Colour const & colour = radialMesh.colours[i];
		if(std::abs(point.x) < 0.01f && std::abs(point.y) < 0.01f)
			sawCentreRed |= colour.r > 0.95f;
		if(std::abs(point.x) > 0.95f && std::abs(point.y) > 0.95f)
			sawCornerBlue |= colour.b > 0.95f;
	}
	CHECK(sawCentreRed);
	CHECK(sawCornerBlue);

	// An off-centre focal point is the zero of the radial ramp; the authored
	// centre is already part-way toward the opposite edge of the circle.
	radial.gradientFocal = Point(0.5f, 0.0f);
	Mesh focalMesh;
	VectorTessellator::triangulateFill(radial, focalMesh);
	bool sawFocalRed = false, sawCentreProgress = false;
	for(std::size_t i = 0; i < focalMesh.positions.size(); ++i)
	{
		Point const & point = focalMesh.positions[i];
		Colour const & colour = focalMesh.colours[i];
		if(std::abs(point.x - 0.5f) < 0.01f && std::abs(point.y) < 0.01f)
			sawFocalRed |= colour.r > 0.95f && colour.b < 0.05f;
		if(std::abs(point.x) < 0.01f && std::abs(point.y) < 0.01f)
			sawCentreProgress |= colour.r > 0.55f && colour.r < 0.8f &&
				colour.b > 0.2f && colour.b < 0.45f;
	}
	CHECK(sawFocalRed);
	CHECK(sawCentreProgress);
}

TEST_CASE("vectortess_gradient_feather_uses_gradient_colour",
	"[unit][vectortess][gradient]")
{
	// A gradient region carries no flat fill (region.fill is left at its white
	// default). The soft edge must feather in the GRADIENT colour sampled at
	// each edge point - not that white default, which would draw a bright halo
	// around every gradient shape. Feed a dark red->dark blue gradient and
	// assert no feather vertex is the white default.
	Region region = squareRegion(1.0f);
	region.fill = Colour(1.0f, 1.0f, 1.0f, 1.0f);	// unset gradient fill default
	region.paintType = VectorTessellator::PAINT_LINEAR_GRADIENT;
	region.gradientStart = Point(-1.0f, 0.0f);
	region.gradientEnd = Point(1.0f, 0.0f);
	region.gradientStops.push_back(VectorTessellator::GradientStop(
		0.0f, Colour(0.5f, 0.0f, 0.0f, 1.0f)));
	region.gradientStops.push_back(VectorTessellator::GradientStop(
		1.0f, Colour(0.0f, 0.0f, 0.5f, 1.0f)));

	std::vector<Region> regions;
	regions.push_back(region);
	Mesh mesh;
	VectorTessellator::build(regions, 0.05f, mesh);

	// the opaque (inner) feather vertices must carry the gradient's colours, not
	// white - a pure-white opaque vertex would be the old halo bug
	bool sawGradientEdge = false;
	for(Colour const & c : mesh.colours)
	{
		if(c.a > 0.99f)
		{
			CHECK_FALSE((c.r > 0.95f && c.g > 0.95f && c.b > 0.95f));
			if(c.g < 0.05f && (c.r > 0.2f || c.b > 0.2f))
				sawGradientEdge = true;
		}
	}
	CHECK(sawGradientEdge);
}

TEST_CASE("vectortess_feather_clamped_to_region_thickness",
	"[unit][vectortess]")
{
	// A single global feather width sized for a whole rig, applied to a THIN
	// ribbon, would extrude the soft edge far past the ribbon itself - the
	// halo/streak bug. build() must clamp the per-region feather to a fraction
	// of the region's own thickness, so a long thin strip does not balloon.
	const float thickness = 0.02f;
	const float length = 2.0f;
	Region ribbon;
	ribbon.fill = Colour(0.2f, 0.6f, 0.9f, 1.0f);
	ribbon.outer.push_back(Point(-length, -thickness * 0.5f));
	ribbon.outer.push_back(Point(length, -thickness * 0.5f));
	ribbon.outer.push_back(Point(length, thickness * 0.5f));
	ribbon.outer.push_back(Point(-length, thickness * 0.5f));

	std::vector<Region> regions;
	regions.push_back(ribbon);
	Mesh mesh;
	// a feather width MUCH larger than the ribbon's thickness (as a whole-rig
	// default would be) must NOT push the outer ring out by that full amount
	VectorTessellator::build(regions, 1.0f, mesh);

	float maxBeyond = 0.0f;
	for(Point const & p : mesh.positions)
	{
		const float beyond = std::abs(p.y) - thickness * 0.5f;
		if(beyond > maxBeyond)
			maxBeyond = beyond;
	}
	// the feather extends at most ~thickness (not the 1.0 global width): the
	// clamp keeps a thin ribbon crisp
	CHECK(maxBeyond < thickness);
	CHECK(maxBeyond > 0.0f);	// but a soft edge still exists

	// a compact region of the same feather budget keeps its full soft edge
	std::vector<Region> big;
	big.push_back(squareRegion(1.0f));
	Mesh bigMesh;
	VectorTessellator::build(big, 0.05f, bigMesh);
	float bigBeyond = 0.0f;
	for(Point const & p : bigMesh.positions)
		bigBeyond = std::max(bigBeyond, std::abs(p.x) - 1.0f);
	CHECK(bigBeyond == Approx(0.05f).margin(0.02f));
}

// --- stroke sweep ----------------------------------------------------------
// A stroke is swept as convex pieces (segment quads, join wedges, caps) instead
// of one offset OUTLINE handed to the triangulator: an outline self-intersects
// wherever the path curves tighter than the half width, and a self-intersecting
// polygon makes a triangulator emit garbage (the spikes/filaments defect).

namespace
{
	//! a stroke region over a centreline, in the given style
	Region strokeRegion(std::vector<Point> const & centreline, float width,
		VectorTessellator::StrokeCap cap, VectorTessellator::StrokeJoin join,
		bool closed)
	{
		Region region;
		region.kind = VectorTessellator::REGION_STROKE;
		region.fill = Colour(0.0f, 0.0f, 0.0f, 1.0f);
		region.outer = centreline;
		region.strokeWidth = width;
		region.strokeCap = cap;
		region.strokeJoin = join;
		region.strokeClosed = closed;
		return region;
	}

	//! is p inside (or on) any triangle of the mesh
	bool meshCovers(Mesh const & mesh, Point const & p, float epsilon = 1e-4f)
	{
		for(std::size_t t = 0; t < mesh.indices.size(); t += 3)
		{
			Point const & a = mesh.positions[mesh.indices[t + 0]];
			Point const & b = mesh.positions[mesh.indices[t + 1]];
			Point const & c = mesh.positions[mesh.indices[t + 2]];
			const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
			const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
			const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
			const bool negative = (d1 < -epsilon) || (d2 < -epsilon) ||
				(d3 < -epsilon);
			const bool positive = (d1 > epsilon) || (d2 > epsilon) ||
				(d3 > epsilon);
			if(!(negative && positive))
			{
				return true;	// same sign (or on an edge): inside
			}
		}
		return false;
	}

	//! the farthest any mesh vertex sits from the centreline's own points
	float maxDistanceFromPoints(Mesh const & mesh,
		std::vector<Point> const & points)
	{
		float worst = 0.0f;
		for(Point const & v : mesh.positions)
		{
			float best = 1e30f;
			for(Point const & p : points)
			{
				const float dx = v.x - p.x;
				const float dy = v.y - p.y;
				const float distance = std::sqrt(dx * dx + dy * dy);
				best = std::min(best, distance);
			}
			worst = std::max(worst, best);
		}
		return worst;
	}
}

TEST_CASE("vectortessellator_stroke_segment_is_a_quad",
	"[unit][vectorshape][stroke]")
{
	// one straight segment, butt caps: exactly the 2-triangle ribbon, and its
	// area is length * width
	std::vector<Point> line;
	line.push_back(Point(0.0f, 0.0f));
	line.push_back(Point(4.0f, 0.0f));
	Mesh mesh;
	VectorTessellator::appendStroke(strokeRegion(line, 0.5f,
		VectorTessellator::CAP_BUTT, VectorTessellator::JOIN_MITER, false),
		mesh);
	CHECK(mesh.triangleCount() == 2u);
	CHECK(meshTriangleArea(mesh) == Approx(4.0f * 0.5f));
	CHECK(meshCovers(mesh, Point(2.0f, 0.2f)));
	CHECK_FALSE(meshCovers(mesh, Point(2.0f, 0.4f)));	// outside the half width
}

TEST_CASE("vectortessellator_stroke_caps", "[unit][vectorshape][stroke]")
{
	std::vector<Point> line;
	line.push_back(Point(0.0f, 0.0f));
	line.push_back(Point(4.0f, 0.0f));
	const float width = 1.0f;
	Mesh butt;
	VectorTessellator::appendStroke(strokeRegion(line, width,
		VectorTessellator::CAP_BUTT, VectorTessellator::JOIN_MITER, false),
		butt);
	Mesh square;
	VectorTessellator::appendStroke(strokeRegion(line, width,
		VectorTessellator::CAP_SQUARE, VectorTessellator::JOIN_MITER, false),
		square);
	Mesh round;
	VectorTessellator::appendStroke(strokeRegion(line, width,
		VectorTessellator::CAP_ROUND, VectorTessellator::JOIN_MITER, false),
		round);

	// butt stops at the end point; square projects half a width; round adds a
	// half disc (area = pi r^2 over the two ends)
	CHECK(meshTriangleArea(butt) == Approx(4.0f));
	CHECK(meshTriangleArea(square) == Approx(4.0f + 2.0f * 0.5f * width));
	CHECK(meshTriangleArea(round) ==
		Approx(4.0f + 3.14159265f * 0.25f).margin(0.03f));
	CHECK_FALSE(meshCovers(butt, Point(4.2f, 0.0f)));
	CHECK(meshCovers(square, Point(4.2f, 0.4f)));
	CHECK(meshCovers(round, Point(4.2f, 0.0f)));
	// the disc is round where the square cap's box is not
	CHECK_FALSE(meshCovers(round, Point(4.4f, 0.4f)));
	CHECK(meshCovers(square, Point(4.4f, 0.4f)));
}

TEST_CASE("vectortessellator_stroke_joins", "[unit][vectorshape][stroke]")
{
	// a right-angle corner: the three join kinds fill the outer corner
	// differently, but all of them cover the inner corner and none spike
	std::vector<Point> corner;
	corner.push_back(Point(0.0f, 0.0f));
	corner.push_back(Point(4.0f, 0.0f));
	corner.push_back(Point(4.0f, 4.0f));
	const float width = 2.0f;	// half width 1: the corner is well inside it

	Mesh miter;
	VectorTessellator::appendStroke(strokeRegion(corner, width,
		VectorTessellator::CAP_BUTT, VectorTessellator::JOIN_MITER, false),
		miter);
	Mesh bevel;
	VectorTessellator::appendStroke(strokeRegion(corner, width,
		VectorTessellator::CAP_BUTT, VectorTessellator::JOIN_BEVEL, false),
		bevel);
	Mesh round;
	VectorTessellator::appendStroke(strokeRegion(corner, width,
		VectorTessellator::CAP_BUTT, VectorTessellator::JOIN_ROUND, false),
		round);

	// the outer corner of a 90 degree turn: the miter reaches (5,-1), the bevel
	// cuts it off, the round arc sits between the two
	CHECK(meshCovers(miter, Point(4.9f, -0.9f)));
	CHECK_FALSE(meshCovers(bevel, Point(4.9f, -0.9f)));
	CHECK_FALSE(meshCovers(round, Point(4.9f, -0.9f)));
	CHECK(meshCovers(round, Point(4.6f, -0.6f)));	// inside the arc radius
	CHECK(meshTriangleArea(miter) > meshTriangleArea(round));
	CHECK(meshTriangleArea(round) > meshTriangleArea(bevel));
	// every kind covers the inside of the corner (the segment quads overlap)
	CHECK(meshCovers(bevel, Point(3.5f, 0.5f)));
}

TEST_CASE("vectortessellator_stroke_miter_limit_falls_back",
	"[unit][vectorshape][stroke]")
{
	// a hairpin turn: an unlimited miter would spike to infinity, the limit
	// pulls the corner back to a bevel-like stub
	std::vector<Point> hairpin;
	hairpin.push_back(Point(0.0f, 0.0f));
	hairpin.push_back(Point(4.0f, 0.0f));
	hairpin.push_back(Point(0.2f, 0.4f));
	Region region = strokeRegion(hairpin, 1.0f, VectorTessellator::CAP_BUTT,
		VectorTessellator::JOIN_MITER, false);
	region.strokeMiterLimit = 2.0f;
	Mesh mesh;
	VectorTessellator::appendStroke(region, mesh);

	// no vertex may sit further from the path than the miter limit allows
	// (limit * half width), so the spike is impossible by construction
	CHECK(maxDistanceFromPoints(mesh, hairpin) <= 2.0f * 0.5f + 1e-3f);
	// a bigger limit lets the same corner reach further out
	region.strokeMiterLimit = 8.0f;
	Mesh generous;
	VectorTessellator::appendStroke(region, generous);
	CHECK(maxDistanceFromPoints(generous, hairpin) >
		maxDistanceFromPoints(mesh, hairpin));
}

TEST_CASE("vectortessellator_stroke_tight_curvature_is_clean",
	"[unit][vectorshape][stroke]")
{
	// THE regression: a path whose curvature radius is far smaller than the
	// stroke half width. Its offset OUTLINE self-intersects (the inner offset
	// doubles back through itself), which is exactly what makes a triangulator
	// emit stray spikes/filaments. The convex-piece sweep cannot: every vertex
	// stays within half a width (plus the miter allowance) of the centreline,
	// and the ribbon still covers the path.
	std::vector<Point> spiral;
	const int steps = 24;
	for(int i = 0; i <= steps; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(steps);
		const float angle = t * 6.0f;
		const float radius = 0.05f + 0.15f * t;	// far tighter than the half width
		spiral.push_back(Point(radius * std::cos(angle),
			radius * std::sin(angle)));
	}
	Region region = strokeRegion(spiral, 1.0f, VectorTessellator::CAP_ROUND,
		VectorTessellator::JOIN_ROUND, false);
	Mesh mesh;
	VectorTessellator::appendStroke(region, mesh);
	REQUIRE(mesh.triangleCount() > 0u);
	// no filament: nothing reaches past the half width from the centreline
	CHECK(maxDistanceFromPoints(mesh, spiral) <= 0.5f + 1e-3f);
	// and the ribbon really is painted along the path
	CHECK(meshCovers(mesh, spiral[steps / 2]));
}

TEST_CASE("vectortessellator_stroke_degenerate_input",
	"[unit][vectorshape][stroke]")
{
	// zero-length segments (duplicated authored points) and a zero width are
	// honest no-ops / stable, never a crash or a spike
	std::vector<Point> duplicated;
	duplicated.push_back(Point(0.0f, 0.0f));
	duplicated.push_back(Point(0.0f, 0.0f));
	duplicated.push_back(Point(1.0f, 0.0f));
	duplicated.push_back(Point(1.0f, 0.0f));
	Mesh mesh;
	VectorTessellator::appendStroke(strokeRegion(duplicated, 0.4f,
		VectorTessellator::CAP_BUTT, VectorTessellator::JOIN_MITER, false),
		mesh);
	CHECK(meshTriangleArea(mesh) == Approx(1.0f * 0.4f).margin(1e-3f));
	CHECK(maxDistanceFromPoints(mesh, duplicated) <= 0.2f + 1e-3f);

	Mesh empty;
	std::vector<Point> single;
	single.push_back(Point(0.0f, 0.0f));
	VectorTessellator::appendStroke(strokeRegion(single, 1.0f,
		VectorTessellator::CAP_ROUND, VectorTessellator::JOIN_ROUND, false),
		empty);
	CHECK(empty.triangleCount() == 0u);
	std::vector<Point> line;
	line.push_back(Point(0.0f, 0.0f));
	line.push_back(Point(1.0f, 0.0f));
	VectorTessellator::appendStroke(strokeRegion(line, 0.0f,
		VectorTessellator::CAP_ROUND, VectorTessellator::JOIN_ROUND, false),
		empty);
	CHECK(empty.triangleCount() == 0u);
}

TEST_CASE("vectortessellator_stroke_closed_ring", "[unit][vectorshape][stroke]")
{
	// a closed centreline has no caps and paints a ring: the band is covered,
	// the middle of the ring is NOT
	std::vector<Point> ring;
	const int steps = 32;
	for(int i = 0; i < steps; ++i)
	{
		const float angle = 6.2831853f * static_cast<float>(i) /
			static_cast<float>(steps);
		ring.push_back(Point(2.0f * std::cos(angle), 2.0f * std::sin(angle)));
	}
	Mesh mesh;
	VectorTessellator::appendStroke(strokeRegion(ring, 0.4f,
		VectorTessellator::CAP_BUTT, VectorTessellator::JOIN_ROUND, true),
		mesh);
	CHECK(meshCovers(mesh, Point(2.0f, 0.0f)));		// on the ring
	CHECK_FALSE(meshCovers(mesh, Point(0.0f, 0.0f)));	// the hole stays empty
	// area of an annulus of width 0.4 at radius 2 (a polygon ring is slightly
	// smaller than the circle it approximates)
	CHECK(meshTriangleArea(mesh) ==
		Approx(2.0f * 3.14159265f * 2.0f * 0.4f).margin(0.15f));
}

TEST_CASE("vectortessellator_stroke_mask_clips", "[unit][vectorshape][stroke]")
{
	// a convex mask clips the ribbon (a cooked layer mask) - pieces are clipped
	// convex-against-convex, so no triangulator is involved there either
	std::vector<Point> line;
	line.push_back(Point(-4.0f, 0.0f));
	line.push_back(Point(4.0f, 0.0f));
	Region region = strokeRegion(line, 1.0f, VectorTessellator::CAP_BUTT,
		VectorTessellator::JOIN_MITER, false);
	region.mask.push_back(Point(-1.0f, -1.0f));
	region.mask.push_back(Point(1.0f, -1.0f));
	region.mask.push_back(Point(1.0f, 1.0f));
	region.mask.push_back(Point(-1.0f, 1.0f));
	Mesh mesh;
	VectorTessellator::appendStroke(region, mesh);
	CHECK(meshTriangleArea(mesh) == Approx(2.0f * 1.0f));	// only the masked part
	CHECK(meshCovers(mesh, Point(0.0f, 0.0f)));
	CHECK_FALSE(meshCovers(mesh, Point(2.0f, 0.0f)));
}

TEST_CASE("vectortessellator_stroke_feather_rims_the_ribbon",
	"[unit][vectorshape][stroke]")
{
	// build() sweeps the stroke and feathers its rim: the soft edge fades to
	// alpha 0 just outside the ribbon, clamped to the stroke's own width so a
	// hairline is not swallowed by a halo
	std::vector<Point> line;
	line.push_back(Point(0.0f, 0.0f));
	line.push_back(Point(4.0f, 0.0f));
	std::vector<Region> regions;
	regions.push_back(strokeRegion(line, 0.2f, VectorTessellator::CAP_BUTT,
		VectorTessellator::JOIN_MITER, false));
	Mesh mesh;
	VectorTessellator::build(regions, 1.0f, mesh);	// a wildly generous feather

	float maxBeyond = 0.0f;
	bool sawTransparent = false;
	for(std::size_t i = 0; i < mesh.positions.size(); ++i)
	{
		maxBeyond = std::max(maxBeyond,
			std::fabs(mesh.positions[i].y) - 0.1f);
		if(mesh.colours[i].a < 0.01f)
		{
			sawTransparent = true;
		}
	}
	CHECK(sawTransparent);				// the alpha ramp exists
	CHECK(maxBeyond <= 0.2f + 1e-3f);	// clamped to the stroke's own width
	// the bounds of a stroke cover the ribbon, not just the centreline
	const VectorTessellator::Bounds bounds =
		VectorTessellator::computeBounds(regions);
	CHECK(bounds.minY == Approx(-0.1f));
	CHECK(bounds.maxY == Approx(0.1f));
}

TEST_CASE("vectortessellator_feather_rides_its_own_region",
	"[unit][vectorshape][stroke]")
{
	// Painter's-algorithm honesty: a region painted later must occlude an
	// earlier region's body AND its feather rim together. build() therefore
	// appends each region's feather immediately after that region's body -
	// were every rim appended after every body instead, the outline of a
	// stroke a later fill legitimately covers would be redrawn above the
	// fill and bleed through as a stray line.
	std::vector<Point> line;
	line.push_back(Point(-2.0f, 0.0f));
	line.push_back(Point(2.0f, 0.0f));
	Region stroke = strokeRegion(line, 0.5f, VectorTessellator::CAP_BUTT,
		VectorTessellator::JOIN_MITER, false);
	stroke.fill = Colour(1.0f, 0.0f, 0.0f, 1.0f);	// red, hidden below ...
	Region cover = squareRegion(3.0f);				// ... an opaque cover
	cover.fill = Colour(0.0f, 0.0f, 1.0f, 1.0f);

	std::vector<Region> regions;
	regions.push_back(stroke);
	regions.push_back(cover);
	Mesh mesh;
	VectorTessellator::build(regions, 0.5f, mesh);	// a generous feather

	// classify each triangle by its vertex rgb (the two regions use disjoint
	// colours; feather vertices keep their region's rgb at ramped alpha)
	std::ptrdiff_t lastRed = -1;
	std::ptrdiff_t firstBlue = -1;
	for(std::size_t t = 0; t < mesh.indices.size(); t += 3)
	{
		Colour const & colour = mesh.colours[mesh.indices[t]];
		if(colour.r > 0.5f && colour.b < 0.5f)
		{
			lastRed = static_cast<std::ptrdiff_t>(t);
		}
		else if(colour.b > 0.5f && colour.r < 0.5f &&
			firstBlue < 0)
		{
			firstBlue = static_cast<std::ptrdiff_t>(t);
		}
	}
	REQUIRE(lastRed >= 0);		// the stroke (body + rim) is in the mesh
	REQUIRE(firstBlue >= 0);	// so is the cover
	// every red triangle - the stroke's body AND its feather rim - precedes
	// the covering fill in draw order, so the cover hides all of it
	CHECK(lastRed < firstBlue);
}

TEST_CASE("vectortessellator_textured_runs_and_uvs", "[unit][vectortess]")
{
	// a flat + textured + textured(same) + flat + textured(other) paint
	// order: consecutive same-texture regions merge, so the run split is
	// flat | tex_a (two regions) | flat | tex_b - one draw per run
	auto quad = [](float cx, float cy, char const * texture)
	{
		VectorTessellator::Region region;
		region.fill = VectorTessellator::Colour(1, 1, 1, 1);
		region.outer.push_back(VectorTessellator::Point(cx - 1, cy - 1));
		region.outer.push_back(VectorTessellator::Point(cx + 1, cy - 1));
		region.outer.push_back(VectorTessellator::Point(cx + 1, cy + 1));
		region.outer.push_back(VectorTessellator::Point(cx - 1, cy + 1));
		if(texture != nullptr)
		{
			region.texture = texture;
			region.textureRectMin = VectorTessellator::Point(cx - 1, cy - 1);
			region.textureRectMax = VectorTessellator::Point(cx + 1, cy + 1);
			VectorTessellator::projectTextureUVs(region);
		}
		return region;
	};
	std::vector<VectorTessellator::Region> regions;
	regions.push_back(quad(0, 0, nullptr));
	regions.push_back(quad(3, 0, "a.png"));
	regions.push_back(quad(6, 0, "a.png"));
	regions.push_back(quad(9, 0, nullptr));
	regions.push_back(quad(12, 0, "b.png"));

	VectorTessellator::Mesh mesh;
	VectorTessellator::build(regions, 0.05f, mesh);

	REQUIRE(mesh.runs.size() == 4u);
	CHECK(mesh.runs[0].texture == "");
	CHECK(mesh.runs[1].texture == "a.png");
	CHECK(mesh.runs[2].texture == "");
	CHECK(mesh.runs[3].texture == "b.png");
	// the uv array parallels positions across every append path
	CHECK(mesh.uvs.size() == mesh.positions.size());
	// runs tile the arrays completely and contiguously
	std::size_t vertexCursor = 0, indexCursor = 0;
	for(VectorTessellator::Run const & run : mesh.runs)
	{
		CHECK(run.vertexStart == vertexCursor);
		CHECK(run.indexStart == indexCursor);
		vertexCursor += run.vertexCount;
		indexCursor += run.indexCount;
		// a run's indices address only its own vertex span
		for(std::size_t i = 0; i < run.indexCount; ++i)
		{
			CHECK(mesh.indices[run.indexStart + i] >= run.vertexStart);
			CHECK(mesh.indices[run.indexStart + i] <
				run.vertexStart + run.vertexCount);
		}
	}
	CHECK(vertexCursor == mesh.positions.size());
	CHECK(indexCursor == mesh.indices.size());
	// the textured quad (4 verts, 2 triangles) got NO feather rim while the
	// flat quad did (its run carries more than the bare 6 indices)
	CHECK(mesh.runs[3].indexCount == 6u);
	CHECK(mesh.runs[0].indexCount > 6u);
	// the merged a.png run is exactly two bare quads
	CHECK(mesh.runs[1].indexCount == 12u);
	// a textured vertex carries its projected uv (quad corner 0 = (0,1))
	VectorTessellator::Run const & runA = mesh.runs[1];
	CHECK(mesh.uvs[runA.vertexStart].x == Approx(0.0f));
	CHECK(mesh.uvs[runA.vertexStart].y == Approx(1.0f));
}

TEST_CASE("vectortessellator_flat_build_single_run_identity",
	"[unit][vectortess]")
{
	// an all-flat build is exactly ONE untextured run covering everything -
	// the identity guard the facade's plain setMesh path keys on - and its
	// geometry is byte-identical to the pre-run pipeline (uvs all zero)
	std::vector<VectorTessellator::Region> regions;
	VectorTessellator::Region region;
	region.fill = VectorTessellator::Colour(0.9f, 0.4f, 0.3f, 1.0f);
	region.outer.push_back(VectorTessellator::Point(0, 0));
	region.outer.push_back(VectorTessellator::Point(2, 0));
	region.outer.push_back(VectorTessellator::Point(0, 2));
	regions.push_back(region);

	VectorTessellator::Mesh mesh;
	VectorTessellator::build(regions, 0.05f, mesh);
	REQUIRE(mesh.runs.size() == 1u);
	CHECK(mesh.runs[0].texture.empty());
	CHECK(mesh.runs[0].vertexStart == 0u);
	CHECK(mesh.runs[0].vertexCount == mesh.positions.size());
	CHECK(mesh.runs[0].indexCount == mesh.indices.size());
	CHECK(mesh.uvs.size() == mesh.positions.size());
	for(VectorTessellator::Point const & uv : mesh.uvs)
	{
		CHECK(uv.x == 0.0f);
		CHECK(uv.y == 0.0f);
	}
}
