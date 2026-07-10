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
