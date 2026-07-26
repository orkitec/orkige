/**************************************************************
	created:	2026/07/24 at 15:00
	filename: 	EditorOverlayGeometryTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the pure Scene-panel display-option overlay builders
	(tools/editor/EditorOverlayGeometry.h) - the collider + bounding-box line
	geometry the Scene panel uploads through the facade line-mesh path. The panel-
	side node/mesh plumbing + the toggles are exercised by the editor_overlays
	integration selfcheck and the editor_control MCP leg.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <EditorOverlayGeometry.h>

#include <cmath>
#include <algorithm>

namespace
{
	float maxAbs(std::vector<Orkige::Vec3> const& points,
		float Orkige::Vec3::* axis)
	{
		float m = 0.0f;
		for (auto const& p : points)
		{
			m = std::max(m, std::abs(p.*axis));
		}
		return m;
	}
}

TEST_CASE("AABB overlay emits a 12-edge (24-vertex) box within its corners",
	"[editor][overlay]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	const Orkige::Vec3 minCorner(-1.0f, -2.0f, -3.0f);
	const Orkige::Vec3 maxCorner(4.0f, 5.0f, 6.0f);
	Orkige::appendOverlayAabb(minCorner, maxCorner,
		Orkige::editorBoundingBoxColour(), points, colours);

	REQUIRE(points.size() == 24);		// 12 segments * 2 endpoints
	REQUIRE(colours.size() == 24);
	// every vertex is one of the 8 AABB corners (bounded by min/max)
	for (auto const& p : points)
	{
		REQUIRE(p.x >= Catch::Approx(minCorner.x));
		REQUIRE(p.x <= Catch::Approx(maxCorner.x));
		REQUIRE(p.y >= Catch::Approx(minCorner.y));
		REQUIRE(p.y <= Catch::Approx(maxCorner.y));
		REQUIRE(p.z >= Catch::Approx(minCorner.z));
		REQUIRE(p.z <= Catch::Approx(maxCorner.z));
	}
	const Orkige::Color expected = Orkige::editorBoundingBoxColour();
	for (auto const& c : colours)
	{
		REQUIRE(c.r == expected.r);
		REQUIRE(c.g == expected.g);
		REQUIRE(c.b == expected.b);
	}
}

TEST_CASE("box collider overlay is 12 edges sized by its half extents",
	"[editor][overlay]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	const Orkige::Vec3 centre(2.0f, 0.0f, -1.0f);
	const Orkige::Vec3 half(0.5f, 1.5f, 0.25f);
	Orkige::appendOverlayBox(centre, Orkige::Quat::IDENTITY, half,
		Orkige::editorColliderColour(), points, colours);

	REQUIRE(points.size() == 24);
	// identity orientation: extents about the centre match the half extents
	REQUIRE(maxAbs(points, &Orkige::Vec3::x) ==
		Catch::Approx(centre.x + half.x));
	// widest deviation from centre on each axis equals the half extent
	float maxDy = 0.0f, maxDz = 0.0f;
	for (auto const& p : points)
	{
		maxDy = std::max(maxDy, std::abs(p.y - centre.y));
		maxDz = std::max(maxDz, std::abs(p.z - centre.z));
	}
	REQUIRE(maxDy == Catch::Approx(half.y));
	REQUIRE(maxDz == Catch::Approx(half.z));
}

TEST_CASE("sphere collider overlay is three circles at the sphere radius",
	"[editor][overlay]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	const int segments = 24;
	const float radius = 3.0f;
	Orkige::appendOverlaySphere(Orkige::Vec3::ZERO, radius, segments,
		Orkige::editorColliderColour(), points, colours);

	// three great circles, each `segments` segments = 2*segments vertices
	REQUIRE(points.size() ==
		static_cast<std::size_t>(3 * segments * 2));
	for (auto const& p : points)
	{
		REQUIRE(p.length() == Catch::Approx(radius).margin(1e-4f));
	}
}

TEST_CASE("capsule collider overlay matches its documented segment count",
	"[editor][overlay]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	const int segments = 24;
	const float halfHeight = 1.0f;
	const float radius = 0.5f;
	Orkige::appendOverlayCapsule(Orkige::Vec3::ZERO, Orkige::Quat::IDENTITY,
		halfHeight, radius, segments, Orkige::editorColliderColour(),
		points, colours);

	REQUIRE(points.size() ==
		static_cast<std::size_t>(2 * Orkige::editorCapsuleSegmentCount(segments)));
	REQUIRE(colours.size() == points.size());
	// the capsule reaches halfHeight + radius along Y and radius across X/Z
	REQUIRE(maxAbs(points, &Orkige::Vec3::y) ==
		Catch::Approx(halfHeight + radius).margin(1e-3f));
	REQUIRE(maxAbs(points, &Orkige::Vec3::x) ==
		Catch::Approx(radius).margin(1e-3f));
}

TEST_CASE("collider dispatch selects the shape by ShapeType value",
	"[editor][overlay]")
{
	const int segments = 24;
	auto build = [&](int shapeType)
	{
		std::vector<Orkige::Vec3> points;
		std::vector<Orkige::Color> colours;
		Orkige::appendColliderOutline(shapeType, Orkige::Vec3::ZERO,
			Orkige::Quat::IDENTITY, Orkige::Vec3(0.5f, 0.5f, 0.5f), 0.5f, 0.5f,
			segments, Orkige::editorColliderColour(), points, colours);
		return points.size();
	};
	REQUIRE(build(0) == 24);								// ST_BOX
	REQUIRE(build(1) == static_cast<std::size_t>(3 * segments * 2));	// ST_SPHERE
	REQUIRE(build(2) == static_cast<std::size_t>(
		2 * Orkige::editorCapsuleSegmentCount(segments)));			// ST_CAPSULE
}

TEST_CASE("shape collider overlay draws each contour as a closed loop",
	"[editor][overlay]")
{
	// two contours: a triangle (3 pts) and a quad (4 pts)
	std::vector<std::vector<Orkige::Vec3>> contours = {
		{ Orkige::Vec3(0.0f, 0.0f, 0.0f), Orkige::Vec3(1.0f, 0.0f, 0.0f),
		  Orkige::Vec3(0.0f, 1.0f, 0.0f) },
		{ Orkige::Vec3(2.0f, 0.0f, 0.0f), Orkige::Vec3(3.0f, 0.0f, 0.0f),
		  Orkige::Vec3(3.0f, 1.0f, 0.0f), Orkige::Vec3(2.0f, 1.0f, 0.0f) }
	};
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	Orkige::appendColliderShapeOutline(contours, Orkige::Vec3::ZERO,
		Orkige::Quat::IDENTITY, Orkige::editorColliderColour(), points, colours);

	// N points -> N segments -> 2N vertices, summed over the contours (3+4)
	REQUIRE(points.size() == static_cast<std::size_t>(2 * (3 + 4)));
	REQUIRE(colours.size() == points.size());
}

TEST_CASE("shape collider overlay applies the body world pose",
	"[editor][overlay]")
{
	std::vector<std::vector<Orkige::Vec3>> contours = {
		{ Orkige::Vec3(1.0f, 0.0f, 0.0f), Orkige::Vec3(0.0f, 1.0f, 0.0f),
		  Orkige::Vec3(-1.0f, 0.0f, 0.0f) }
	};
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	const Orkige::Vec3 centre(5.0f, 2.0f, 0.0f);
	Orkige::appendColliderShapeOutline(contours, centre, Orkige::Quat::IDENTITY,
		Orkige::editorColliderColour(), points, colours);
	// the first segment starts at the first contour point offset by the centre
	REQUIRE(points.front().x == Catch::Approx(6.0f));
	REQUIRE(points.front().y == Catch::Approx(2.0f));
}
