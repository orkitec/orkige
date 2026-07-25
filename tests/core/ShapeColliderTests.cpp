/**************************************************************
	created:	2026/07/25 at 10:00
	filename: 	ShapeColliderTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless geometry tests for the shape-collider pipeline: outer-contour
	extraction from parsed .oshape regions (concave outline preserved, strokes
	and holes skipped), convex-hull generation (a concave outline hulls to its
	convex boundary), the convexity gate that drives the dynamic-body degrade,
	and the planar prism extrusion (vertex/index counts and z depth). Pure math -
	no physics, no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/ShapeCollider.h"
#include "core_util/VectorShapeAsset.h"

#include <algorithm>
#include <cmath>

using Orkige::ShapeCollider;
using Orkige::VectorTessellator;
using Point = ShapeCollider::Point;

namespace
{
	//! a U (cup) polygon opening upward - the canonical CONCAVE fixture. Eight
	//! CCW vertices; the interior x in [-1,1], y in [-0.5,1.5] is OUTSIDE the
	//! solid (a ball drops into it), which is the concavity a box cannot express.
	std::vector<Point> uCupContour()
	{
		return {
			Point(-2.0f, -1.5f), Point(2.0f, -1.5f),
			Point(2.0f, 1.5f), Point(1.0f, 1.5f),
			Point(1.0f, -0.5f), Point(-1.0f, -0.5f),
			Point(-1.0f, 1.5f), Point(-2.0f, 1.5f)
		};
	}

	VectorTessellator::Region fillRegion(std::vector<Point> const& outer)
	{
		VectorTessellator::Region region;
		region.kind = VectorTessellator::REGION_FILL;
		region.outer = outer;
		return region;
	}
}

TEST_CASE("extractContours keeps a concave fill outline and skips strokes",
	"[shapecollider]")
{
	std::vector<VectorTessellator::Region> regions;
	regions.push_back(fillRegion(uCupContour()));
	// a stroke region contributes no enclosed area - it must be skipped
	VectorTessellator::Region stroke;
	stroke.kind = VectorTessellator::REGION_STROKE;
	stroke.outer = { Point(0.0f, 0.0f), Point(1.0f, 0.0f) };
	regions.push_back(stroke);

	std::vector<std::vector<Point>> contours;
	ShapeCollider::extractContours(regions, contours);

	REQUIRE(contours.size() == 1);
	REQUIRE(contours[0].size() == 8);
	// the vertices survive in authored order
	REQUIRE(contours[0][0].x == Catch::Approx(-2.0f));
	REQUIRE(contours[0][3].x == Catch::Approx(1.0f));
	REQUIRE(contours[0][3].y == Catch::Approx(1.5f));
}

TEST_CASE("extractContours drops a repeated closing vertex", "[shapecollider]")
{
	std::vector<Point> closed = uCupContour();
	closed.push_back(closed.front());	// explicit closing duplicate
	std::vector<VectorTessellator::Region> regions;
	regions.push_back(fillRegion(closed));

	std::vector<std::vector<Point>> contours;
	ShapeCollider::extractContours(regions, contours);
	REQUIRE(contours.size() == 1);
	REQUIRE(contours[0].size() == 8);	// the duplicate is gone
}

TEST_CASE("extractContours parses a .oshape and yields its outline",
	"[shapecollider]")
{
	// a small L authored in the .oshape grammar
	const std::string text =
		"version 3\n"
		"fill 1 1 1 1\n"
		"contour 6\n"
		"v 0 0\n"
		"v 2 0\n"
		"v 2 1\n"
		"v 1 1\n"
		"v 1 2\n"
		"v 0 2\n";
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(Orkige::VectorShapeAsset::parse(text, regions));
	std::vector<std::vector<Point>> contours;
	ShapeCollider::extractContours(regions, contours);
	REQUIRE(contours.size() == 1);
	REQUIRE(contours[0].size() == 6);
	// the L is concave (the notch at (1,1))
	REQUIRE_FALSE(ShapeCollider::isConvex(contours[0]));
}

TEST_CASE("isConvex distinguishes a convex box from a concave cup",
	"[shapecollider]")
{
	const std::vector<Point> box = {
		Point(-1.0f, -1.0f), Point(1.0f, -1.0f),
		Point(1.0f, 1.0f), Point(-1.0f, 1.0f)
	};
	REQUIRE(ShapeCollider::isConvex(box));
	REQUIRE_FALSE(ShapeCollider::isConvex(uCupContour()));
}

TEST_CASE("convexHull of a concave cup is its convex boundary", "[shapecollider]")
{
	const std::vector<Point> hull = ShapeCollider::convexHull(uCupContour());
	// the cup's interior notch vertices ((+-1, -0.5) and (+-1, 1.5)) are not on
	// the hull; the hull is the outer rectangle's four corners
	REQUIRE(hull.size() == 4);
	float minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
	for (Point const& p : hull)
	{
		minX = std::min(minX, p.x);
		maxX = std::max(maxX, p.x);
		minY = std::min(minY, p.y);
		maxY = std::max(maxY, p.y);
	}
	REQUIRE(minX == Catch::Approx(-2.0f));
	REQUIRE(maxX == Catch::Approx(2.0f));
	REQUIRE(minY == Catch::Approx(-1.5f));
	REQUIRE(maxY == Catch::Approx(1.5f));
}

TEST_CASE("convexHull drops collinear and duplicate points", "[shapecollider]")
{
	const std::vector<Point> pts = {
		Point(0.0f, 0.0f), Point(1.0f, 0.0f), Point(2.0f, 0.0f),	// collinear edge
		Point(2.0f, 2.0f), Point(0.0f, 2.0f), Point(0.0f, 2.0f)		// a duplicate
	};
	const std::vector<Point> hull = ShapeCollider::convexHull(pts);
	REQUIRE(hull.size() == 4);	// only the four corners survive
}

TEST_CASE("buildExtrudedMesh extrudes a contour to a closed prism",
	"[shapecollider]")
{
	std::vector<std::vector<Point>> contours;
	contours.push_back(uCupContour());
	const float halfThickness = 0.5f;

	// the exact cap triangle count the shared tessellator emits (N-2 for a
	// simple polygon), so the assertion tracks the builder, not a magic number
	VectorTessellator::Region region = fillRegion(uCupContour());
	VectorTessellator::Mesh cap;
	VectorTessellator::triangulateFill(region, cap);
	const std::size_t capIndices = cap.indices.size();
	REQUIRE(capIndices == 3 * (8 - 2));

	std::vector<ShapeCollider::Vertex> verts;
	std::vector<unsigned int> indices;
	ShapeCollider::buildExtrudedMesh(contours, halfThickness, verts, indices);

	// 2N vertices (front + back cap, reused by the side walls)
	REQUIRE(verts.size() == 16);
	// both caps + one quad (6 indices) per contour edge
	REQUIRE(indices.size() == 2 * capIndices + 6 * 8);
	// every index addresses a real vertex
	for (unsigned int i : indices)
	{
		REQUIRE(i < verts.size());
	}
	// the prism spans exactly +/- halfThickness in z
	float minZ = 1.0e9f, maxZ = -1.0e9f;
	for (ShapeCollider::Vertex const& v : verts)
	{
		minZ = std::min(minZ, v.z);
		maxZ = std::max(maxZ, v.z);
	}
	REQUIRE(minZ == Catch::Approx(-halfThickness));
	REQUIRE(maxZ == Catch::Approx(halfThickness));
}

TEST_CASE("buildExtrudedMesh produces nothing for a degenerate request",
	"[shapecollider]")
{
	std::vector<std::vector<Point>> contours;
	contours.push_back(uCupContour());
	std::vector<ShapeCollider::Vertex> verts;
	std::vector<unsigned int> indices;
	// non-positive thickness is not a solid
	ShapeCollider::buildExtrudedMesh(contours, 0.0f, verts, indices);
	REQUIRE(verts.empty());
	REQUIRE(indices.empty());
}

TEST_CASE("extrudeOutlinePoints doubles the outline across z", "[shapecollider]")
{
	const std::vector<Point> hull = ShapeCollider::convexHull(uCupContour());
	std::vector<ShapeCollider::Vertex> verts;
	ShapeCollider::extrudeOutlinePoints(hull, 0.25f, verts);
	REQUIRE(verts.size() == hull.size() * 2);
	for (std::size_t i = 0; i < hull.size(); ++i)
	{
		REQUIRE(verts[2 * i].z == Catch::Approx(0.25f));
		REQUIRE(verts[2 * i + 1].z == Catch::Approx(-0.25f));
	}
}
