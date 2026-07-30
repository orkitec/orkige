/**************************************************************
	created:	2026/07/30 at 09:40
	filename: 	MeshBuilderTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless geometry tests for the procedural mesh tier: every parametric
	shape's invariants (vertex/index counts, closed-manifold edge pairing where
	it applies, unit OUTWARD normals, UVs in range, bounds, byte-level
	determinism), the builder operators (transform incl. the mirroring
	winding flip, multi-section merge, flat vs smooth normals, the UV
	strategies, tangent generation) and the refusal verdicts for degenerate
	parameters. Pure math - no renderer, no physics, no filesystem.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/MeshBuilder.h"
#include "core_util/MeshShapes.h"

#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <vector>

using Orkige::MeshBuilder;
using Orkige::MeshShapes;
using Orkige::String;
using Mesh = MeshBuilder::Mesh;
using Vec2f = MeshBuilder::Vec2f;
using Vec3f = MeshBuilder::Vec3f;

namespace
{
	//! every normal is unit length (the lit road needs that, and a zero or NaN
	//! normal is exactly the bug this catches)
	bool normalsAreUnit(Mesh const& mesh, float tolerance = 1.0e-3f)
	{
		for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			const float length = MeshBuilder::length(mesh.vertices[each].normal);
			if (std::fabs(length - 1.0f) > tolerance)
			{
				return false;
			}
		}
		return true;
	}

	//! every tangent is unit length AND perpendicular to its normal with a
	//! handedness of exactly +/-1 (the Hlms rejects anything else)
	bool tangentsAreSane(Mesh const& mesh)
	{
		for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			MeshBuilder::Vertex const& vertex = mesh.vertices[each];
			const Vec3f tangent(vertex.tangent.x, vertex.tangent.y,
				vertex.tangent.z);
			if (std::fabs(MeshBuilder::length(tangent) - 1.0f) > 1.0e-3f)
			{
				return false;
			}
			if (std::fabs(MeshBuilder::dot(tangent, vertex.normal)) > 1.0e-2f)
			{
				return false;
			}
			if (std::fabs(std::fabs(vertex.tangent.w) - 1.0f) > 1.0e-6f)
			{
				return false;
			}
		}
		return true;
	}

	//! UVs inside the documented 0..1 window (a small epsilon for float drift)
	bool uvsInRange(Mesh const& mesh, float slack = 1.0e-3f)
	{
		for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			Vec2f const& uv = mesh.vertices[each].uv;
			if (uv.x < -slack || uv.x > 1.0f + slack ||
				uv.y < -slack || uv.y > 1.0f + slack)
			{
				return false;
			}
		}
		return true;
	}

	//! @brief every triangle's geometric normal points AWAY from @p interior -
	//! the outward-winding check for a shape that encloses that point
	bool windsOutwardFrom(Mesh const& mesh, Vec3f const& interior)
	{
		const std::size_t triangles = mesh.triangleCount();
		for (std::size_t each = 0; each < triangles; ++each)
		{
			Vec3f const& a = mesh.vertices[mesh.indices[each * 3 + 0]].position;
			Vec3f const& b = mesh.vertices[mesh.indices[each * 3 + 1]].position;
			Vec3f const& c = mesh.vertices[mesh.indices[each * 3 + 2]].position;
			const Vec3f face = MeshBuilder::cross(MeshBuilder::subtract(b, a),
				MeshBuilder::subtract(c, a));
			if (MeshBuilder::length(face) < 1.0e-12f)
			{
				continue;	// a degenerate triangle has no opinion
			}
			const Vec3f centroid(
				(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f,
				(a.z + b.z + c.z) / 3.0f);
			if (MeshBuilder::dot(face,
				MeshBuilder::subtract(centroid, interior)) <= 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	//! @brief is the surface CLOSED: welding coincident positions, every
	//! undirected edge is shared by exactly two triangles and the two uses run
	//! in opposite directions (a watertight, consistently wound manifold)
	bool isClosedManifold(Mesh const& mesh, float weld = 1.0e-4f)
	{
		// weld positions onto a quantised key so the seam duplicates a
		// generator emits for UV continuity collapse into one topological vertex
		std::map<std::tuple<long long, long long, long long>, long long> weldMap;
		std::vector<long long> welded(mesh.vertices.size(), 0);
		for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			Vec3f const& position = mesh.vertices[each].position;
			const auto key = std::make_tuple(
				static_cast<long long>(std::lround(position.x / weld)),
				static_cast<long long>(std::lround(position.y / weld)),
				static_cast<long long>(std::lround(position.z / weld)));
			auto found = weldMap.find(key);
			if (found == weldMap.end())
			{
				const long long index = static_cast<long long>(weldMap.size());
				weldMap[key] = index;
				welded[each] = index;
			}
			else
			{
				welded[each] = found->second;
			}
		}
		std::map<std::pair<long long, long long>, int> directed;
		const std::size_t triangles = mesh.triangleCount();
		for (std::size_t each = 0; each < triangles; ++each)
		{
			const long long corner[3] = {
				welded[mesh.indices[each * 3 + 0]],
				welded[mesh.indices[each * 3 + 1]],
				welded[mesh.indices[each * 3 + 2]] };
			for (int edge = 0; edge < 3; ++edge)
			{
				const long long from = corner[edge];
				const long long to = corner[(edge + 1) % 3];
				if (from == to)
				{
					continue;	// a collapsed pole edge
				}
				++directed[std::make_pair(from, to)];
			}
		}
		for (auto const& entry : directed)
		{
			if (entry.second != 1)
			{
				return false;	// the same directed edge used twice
			}
			auto opposite = directed.find(
				std::make_pair(entry.first.second, entry.first.first));
			if (opposite == directed.end())
			{
				return false;	// a boundary edge - the surface has a hole
			}
		}
		return true;
	}

	//! the raw bytes of both buffers, for the determinism assertions
	bool sameBuffers(Mesh const& a, Mesh const& b)
	{
		if (a.vertices.size() != b.vertices.size() ||
			a.indices.size() != b.indices.size() ||
			a.sections.size() != b.sections.size())
		{
			return false;
		}
		if (!a.vertices.empty() && std::memcmp(a.vertices.data(),
			b.vertices.data(),
			a.vertices.size() * sizeof(MeshBuilder::Vertex)) != 0)
		{
			return false;
		}
		if (!a.indices.empty() && std::memcmp(a.indices.data(),
			b.indices.data(), a.indices.size() * sizeof(unsigned int)) != 0)
		{
			return false;
		}
		for (std::size_t each = 0; each < a.sections.size(); ++each)
		{
			if (a.sections[each].material != b.sections[each].material ||
				a.sections[each].vertexStart != b.sections[each].vertexStart ||
				a.sections[each].vertexCount != b.sections[each].vertexCount ||
				a.sections[each].indexStart != b.sections[each].indexStart ||
				a.sections[each].indexCount != b.sections[each].indexCount)
			{
				return false;
			}
		}
		return true;
	}
}

//--- the box family -------------------------------------------------------

TEST_CASE("box has 24 vertices, 12 triangles, outward normals and 0..1 UVs",
	"[meshbuilder]")
{
	Mesh mesh;
	String error;
	REQUIRE(MeshShapes::box(mesh, 2.0f, 1.0f, 4.0f, &error));
	CHECK(error.empty());
	// 4 vertices per face keeps each face's normal crisp and gives it its own
	// UV square - a shared-corner cube could do neither
	CHECK(mesh.vertices.size() == 24);
	CHECK(mesh.triangleCount() == 12);
	CHECK(mesh.sections.size() == 1);
	CHECK(mesh.sections[0].vertexCount == 24);
	CHECK(mesh.sections[0].indexCount == 36);
	CHECK(MeshBuilder::validate(mesh));
	CHECK(normalsAreUnit(mesh));
	CHECK(tangentsAreSane(mesh));
	CHECK(uvsInRange(mesh));
	CHECK(windsOutwardFrom(mesh, Vec3f(0.0f, 0.0f, 0.0f)));
	CHECK(isClosedManifold(mesh));

	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	REQUIRE(bounds.valid);
	CHECK(bounds.size().x == Catch::Approx(2.0f));
	CHECK(bounds.size().y == Catch::Approx(1.0f));
	CHECK(bounds.size().z == Catch::Approx(4.0f));
	// centred on its own origin, so a placement translation puts the middle there
	CHECK(bounds.centre().x == Catch::Approx(0.0f));
	CHECK(bounds.centre().y == Catch::Approx(0.0f));
	CHECK(bounds.centre().z == Catch::Approx(0.0f));
}

TEST_CASE("box refuses a non-positive or non-finite extent", "[meshbuilder]")
{
	Mesh mesh;
	String error;
	CHECK_FALSE(MeshShapes::box(mesh, 0.0f, 1.0f, 1.0f, &error));
	CHECK(mesh.empty());
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(MeshShapes::box(mesh, -2.0f, 1.0f, 1.0f, &error));
	CHECK(mesh.empty());

	error.clear();
	const float notANumber = std::nan("");
	CHECK_FALSE(MeshShapes::box(mesh, notANumber, 1.0f, 1.0f, &error));
	CHECK(mesh.empty());

	error.clear();
	const float infinity = std::numeric_limits<float>::infinity();
	CHECK_FALSE(MeshShapes::box(mesh, 1.0f, infinity, 1.0f, &error));
	CHECK(mesh.empty());
}

TEST_CASE("roundedBox rounds the corners, clamps the radius and stays sane",
	"[meshbuilder]")
{
	Mesh mesh;
	String error;
	REQUIRE(MeshShapes::roundedBox(mesh, 2.0f, 2.0f, 2.0f, 0.5f, 3, &error));
	// 6 faces of a (segments+1)^2 grid
	CHECK(mesh.vertices.size() == 6 * 4 * 4);
	CHECK(mesh.triangleCount() == 6 * 2 * 3 * 3);
	CHECK(normalsAreUnit(mesh));
	CHECK(tangentsAreSane(mesh));
	CHECK(uvsInRange(mesh));
	CHECK(windsOutwardFrom(mesh, Vec3f(0.0f, 0.0f, 0.0f)));
	// rounding pulls the shape strictly inside the sharp box
	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	CHECK(bounds.size().x == Catch::Approx(2.0f).margin(1.0e-4f));

	// an absurd radius clamps to the smallest half extent (a sphere-ish blob)
	Mesh clamped;
	REQUIRE(MeshShapes::roundedBox(clamped, 2.0f, 2.0f, 2.0f, 50.0f, 4,
		&error));
	const MeshBuilder::Bounds clampedBounds = clamped.computeBounds();
	CHECK(clampedBounds.size().x == Catch::Approx(2.0f).margin(1.0e-3f));
	CHECK(normalsAreUnit(clamped));

	// radius 0 degenerates to a subdivided plain box
	Mesh sharp;
	REQUIRE(MeshShapes::roundedBox(sharp, 2.0f, 1.0f, 1.0f, 0.0f, 1, &error));
	CHECK(sharp.vertices.size() == 24);
	CHECK(sharp.triangleCount() == 12);
	CHECK(isClosedManifold(sharp));

	CHECK_FALSE(MeshShapes::roundedBox(mesh, 1.0f, 1.0f, 1.0f, -1.0f, 3,
		&error));
	CHECK(mesh.empty());
}

TEST_CASE("plane is a subdivided XZ grid facing +Y", "[meshbuilder]")
{
	Mesh mesh;
	String error;
	REQUIRE(MeshShapes::plane(mesh, 4.0f, 6.0f, 2, 3, &error));
	CHECK(mesh.vertices.size() == 3 * 4);
	CHECK(mesh.triangleCount() == 2 * 2 * 3);
	CHECK(normalsAreUnit(mesh));
	CHECK(uvsInRange(mesh));
	for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
	{
		CHECK(mesh.vertices[each].normal.y == Catch::Approx(1.0f));
		CHECK(mesh.vertices[each].position.y == Catch::Approx(0.0f));
	}
	// the front faces look up: every triangle's geometric normal is +Y
	const std::size_t triangles = mesh.triangleCount();
	for (std::size_t each = 0; each < triangles; ++each)
	{
		Vec3f const& a = mesh.vertices[mesh.indices[each * 3 + 0]].position;
		Vec3f const& b = mesh.vertices[mesh.indices[each * 3 + 1]].position;
		Vec3f const& c = mesh.vertices[mesh.indices[each * 3 + 2]].position;
		const Vec3f face = MeshBuilder::cross(MeshBuilder::subtract(b, a),
			MeshBuilder::subtract(c, a));
		CHECK(face.y > 0.0f);
	}
	CHECK_FALSE(MeshShapes::plane(mesh, 0.0f, 1.0f, 1, 1, &error));
}

//--- the round family -----------------------------------------------------

TEST_CASE("uvSphere collapses its poles and encloses a closed surface",
	"[meshbuilder]")
{
	Mesh mesh;
	String error;
	const int segments = 8;
	const int rings = 4;
	REQUIRE(MeshShapes::uvSphere(mesh, 2.0f, segments, rings, &error));
	CHECK(mesh.vertices.size() ==
		static_cast<std::size_t>(segments + 1) *
		static_cast<std::size_t>(rings + 1));
	// the two pole bands lose one triangle per column
	CHECK(mesh.triangleCount() ==
		static_cast<std::size_t>(2 * segments * (rings - 1)));
	CHECK(normalsAreUnit(mesh));
	CHECK(tangentsAreSane(mesh));
	CHECK(uvsInRange(mesh));
	CHECK(windsOutwardFrom(mesh, Vec3f(0.0f, 0.0f, 0.0f)));
	CHECK(isClosedManifold(mesh));
	// every point sits on the sphere and its normal is the radius direction
	for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
	{
		Vec3f const& position = mesh.vertices[each].position;
		CHECK(MeshBuilder::length(position) == Catch::Approx(2.0f).margin(1.0e-4f));
		const Vec3f radial = MeshBuilder::normalise(position);
		CHECK(MeshBuilder::dot(radial, mesh.vertices[each].normal) ==
			Catch::Approx(1.0f).margin(1.0e-3f));
	}
}

TEST_CASE("segment and ring counts clamp instead of degenerating",
	"[meshbuilder]")
{
	Mesh tiny;
	String error;
	// a count below the structural minimum is CLAMPED, not refused
	REQUIRE(MeshShapes::uvSphere(tiny, 1.0f, 1, 0, &error));
	CHECK(tiny.vertices.size() ==
		static_cast<std::size_t>(MeshBuilder::MIN_SEGMENTS + 1) * 3);
	CHECK(tiny.triangleCount() > 0);
	CHECK(normalsAreUnit(tiny));

	// an absurd count is clamped DOWN, so a typo cannot try to allocate the
	// machine
	Mesh huge;
	REQUIRE(MeshShapes::cylinder(huge, 1.0f, 1.0f, 100000000, false, &error));
	CHECK(huge.vertices.size() ==
		static_cast<std::size_t>(MeshBuilder::MAX_SEGMENTS + 1) * 2);

	CHECK(MeshBuilder::clampSegments(-5) == MeshBuilder::MIN_SEGMENTS);
	CHECK(MeshBuilder::clampSegments(7) == 7u);
	CHECK(MeshBuilder::clampSegments(99999) == MeshBuilder::MAX_SEGMENTS);
	CHECK(MeshBuilder::clampSegments(0, 1) == 1u);
}

TEST_CASE("icosphere subdivides to the geodesic counts", "[meshbuilder]")
{
	String error;
	for (int level = 0; level <= 3; ++level)
	{
		Mesh mesh;
		REQUIRE(MeshShapes::icosphere(mesh, 1.5f, level, &error));
		std::size_t faces = 20;
		std::size_t points = 12;
		for (int step = 0; step < level; ++step)
		{
			points += faces * 3 / 2;	// one new vertex per edge
			faces *= 4;
		}
		CHECK(mesh.triangleCount() == faces);
		CHECK(mesh.vertices.size() == points);
		CHECK(normalsAreUnit(mesh));
		CHECK(windsOutwardFrom(mesh, Vec3f(0.0f, 0.0f, 0.0f)));
		CHECK(isClosedManifold(mesh));
		for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			CHECK(MeshBuilder::length(mesh.vertices[each].position) ==
				Catch::Approx(1.5f).margin(1.0e-4f));
		}
	}
	// the subdivision level is clamped to a sane ceiling
	Mesh capped;
	REQUIRE(MeshShapes::icosphere(capped, 1.0f, 99, &error));
	CHECK(capped.triangleCount() == 20u * 1024u);
	CHECK_FALSE(MeshShapes::icosphere(capped, 0.0f, 1, &error));
}

TEST_CASE("cylinder, cone, capsule, torus and tube close and face outward",
	"[meshbuilder]")
{
	String error;
	SECTION("cylinder")
	{
		Mesh mesh;
		REQUIRE(MeshShapes::cylinder(mesh, 1.0f, 3.0f, 12, true, &error));
		CHECK(normalsAreUnit(mesh));
		CHECK(tangentsAreSane(mesh));
		CHECK(uvsInRange(mesh));
		CHECK(windsOutwardFrom(mesh, Vec3f(0.0f, 0.0f, 0.0f)));
		CHECK(isClosedManifold(mesh));
		const MeshBuilder::Bounds bounds = mesh.computeBounds();
		CHECK(bounds.size().y == Catch::Approx(3.0f));
		CHECK(bounds.centre().y == Catch::Approx(0.0f));
		// capless is the honest open tube: no longer a closed surface
		Mesh open;
		REQUIRE(MeshShapes::cylinder(open, 1.0f, 3.0f, 12, false, &error));
		CHECK_FALSE(isClosedManifold(open));
		CHECK(open.triangleCount() == 2u * 12u);
	}
	SECTION("cone")
	{
		Mesh mesh;
		REQUIRE(MeshShapes::cone(mesh, 1.0f, 2.0f, 10, true, &error));
		// one side triangle per segment (the apex normal cannot be shared)
		CHECK(mesh.triangleCount() == 10u + 10u);
		CHECK(normalsAreUnit(mesh));
		CHECK(windsOutwardFrom(mesh, Vec3f(0.0f, -0.5f, 0.0f)));
		CHECK(isClosedManifold(mesh));
	}
	SECTION("capsule")
	{
		Mesh mesh;
		REQUIRE(MeshShapes::capsule(mesh, 0.5f, 2.0f, 12, 4, &error));
		CHECK(normalsAreUnit(mesh));
		CHECK(windsOutwardFrom(mesh, Vec3f(0.0f, 0.0f, 0.0f)));
		CHECK(isClosedManifold(mesh));
		// total height is the straight part plus the two hemisphere caps
		const MeshBuilder::Bounds bounds = mesh.computeBounds();
		CHECK(bounds.size().y == Catch::Approx(3.0f).margin(1.0e-4f));
		CHECK(bounds.size().x == Catch::Approx(1.0f).margin(1.0e-3f));
		// a zero straight part IS a sphere
		Mesh ball;
		REQUIRE(MeshShapes::capsule(ball, 1.0f, 0.0f, 12, 6, &error));
		const MeshBuilder::Bounds ballBounds = ball.computeBounds();
		CHECK(ballBounds.size().y == Catch::Approx(2.0f).margin(1.0e-4f));
		CHECK(isClosedManifold(ball));
		CHECK_FALSE(MeshShapes::capsule(mesh, 1.0f, -1.0f, 12, 4, &error));
	}
	SECTION("torus")
	{
		Mesh mesh;
		REQUIRE(MeshShapes::torus(mesh, 2.0f, 0.5f, 16, 8, &error));
		CHECK(mesh.vertices.size() == 17u * 9u);
		CHECK(mesh.triangleCount() == 2u * 16u * 8u);
		CHECK(normalsAreUnit(mesh));
		CHECK(isClosedManifold(mesh));
		const MeshBuilder::Bounds bounds = mesh.computeBounds();
		CHECK(bounds.size().x == Catch::Approx(5.0f).margin(1.0e-3f));
		CHECK(bounds.size().y == Catch::Approx(1.0f).margin(1.0e-3f));
	}
	SECTION("tube")
	{
		Mesh mesh;
		REQUIRE(MeshShapes::tube(mesh, 1.0f, 0.6f, 2.0f, 12, true, &error));
		CHECK(normalsAreUnit(mesh));
		CHECK(isClosedManifold(mesh));
		// the bore wall faces INWARD, so an outward test against the centre
		// must fail - that is the point of a hollow shape
		CHECK_FALSE(windsOutwardFrom(mesh, Vec3f(0.0f, 0.0f, 0.0f)));
		CHECK_FALSE(MeshShapes::tube(mesh, 1.0f, 1.0f, 1.0f, 12, true, &error));
		CHECK(mesh.empty());
		CHECK_FALSE(MeshShapes::tube(mesh, 1.0f, 2.0f, 1.0f, 12, true, &error));
	}
	SECTION("disc")
	{
		Mesh full;
		REQUIRE(MeshShapes::disc(full, 1.0f, 0.0f, 12, &error));
		CHECK(full.triangleCount() == 12u);
		CHECK(uvsInRange(full));
		for (std::size_t each = 0; each < full.vertices.size(); ++each)
		{
			CHECK(full.vertices[each].normal.y == Catch::Approx(1.0f));
		}
		Mesh ring;
		REQUIRE(MeshShapes::disc(ring, 1.0f, 0.5f, 12, &error));
		CHECK(ring.triangleCount() == 24u);
		CHECK_FALSE(MeshShapes::disc(ring, 1.0f, 1.0f, 12, &error));
	}
}

//--- the blockout solids --------------------------------------------------

TEST_CASE("wedge is a closed 5-face ramp rising along +X", "[meshbuilder]")
{
	Mesh mesh;
	String error;
	REQUIRE(MeshShapes::wedge(mesh, 4.0f, 2.0f, 3.0f, &error));
	// 3 quads + 2 triangles, every face with its own vertices
	CHECK(mesh.triangleCount() == 8u);
	CHECK(mesh.vertices.size() == 3u * 4u + 2u * 3u);
	CHECK(normalsAreUnit(mesh));
	CHECK(uvsInRange(mesh));
	CHECK(isClosedManifold(mesh));
	// the interior of a wedge is near its low-back corner
	CHECK(windsOutwardFrom(mesh, Vec3f(1.0f, -0.6f, 0.0f)));
	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	CHECK(bounds.size().x == Catch::Approx(4.0f));
	CHECK(bounds.size().y == Catch::Approx(2.0f));
	CHECK(bounds.size().z == Catch::Approx(3.0f));
	// the low edge is at -X and the full-height wall at +X
	CHECK(bounds.centre().x == Catch::Approx(0.0f));
	CHECK_FALSE(MeshShapes::wedge(mesh, 1.0f, 0.0f, 1.0f, &error));
}

TEST_CASE("stairs closes over its stepped cross-section", "[meshbuilder]")
{
	Mesh mesh;
	String error;
	const int steps = 4;
	REQUIRE(MeshShapes::stairs(mesh, 4.0f, 2.0f, 2.0f, steps, &error));
	CHECK(normalsAreUnit(mesh));
	CHECK(uvsInRange(mesh));
	CHECK(isClosedManifold(mesh));
	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	CHECK(bounds.size().x == Catch::Approx(4.0f));
	CHECK(bounds.size().y == Catch::Approx(2.0f));
	CHECK(bounds.size().z == Catch::Approx(2.0f));
	// more steps means more geometry, monotonically
	Mesh coarse;
	REQUIRE(MeshShapes::stairs(coarse, 4.0f, 2.0f, 2.0f, 2, &error));
	CHECK(coarse.triangleCount() < mesh.triangleCount());
	// one step is a plain box-shaped solid
	Mesh single;
	REQUIRE(MeshShapes::stairs(single, 2.0f, 1.0f, 1.0f, 1, &error));
	CHECK(isClosedManifold(single));
	CHECK_FALSE(MeshShapes::stairs(mesh, 0.0f, 1.0f, 1.0f, 4, &error));
}

TEST_CASE("arch sweeps a band over its opening and is closed", "[meshbuilder]")
{
	Mesh mesh;
	String error;
	REQUIRE(MeshShapes::arch(mesh, 2.0f, 1.0f, 0.4f, 0.5f, 8, &error));
	CHECK(normalsAreUnit(mesh));
	CHECK(tangentsAreSane(mesh));
	CHECK(uvsInRange(mesh));
	CHECK(isClosedManifold(mesh));
	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	// the outer span is the opening plus a band thickness on each side
	CHECK(bounds.size().x == Catch::Approx(2.0f + 2.0f * 0.4f).margin(1.0e-3f));
	CHECK(bounds.size().z == Catch::Approx(0.5f).margin(1.0e-4f));
	// leg + the semicircular top, over the centreline radius. The mitred outer
	// corner of a polygonal arc reaches slightly PAST the ideal circle (that is
	// what keeps the band's thickness constant round a corner), so the height
	// is bounded from both sides rather than pinned.
	const float centreRadius = 1.0f + 0.2f;
	const float idealHeight = 1.0f + centreRadius + 0.2f;
	CHECK(bounds.size().y >= idealHeight - 1.0e-3f);
	CHECK(bounds.size().y <= idealHeight * 1.05f);
	CHECK(bounds.centre().y == Catch::Approx(0.0f).margin(1.0e-4f));
	// a legless arch is a plain semicircular band
	Mesh legless;
	REQUIRE(MeshShapes::arch(legless, 2.0f, 0.0f, 0.4f, 0.5f, 8, &error));
	CHECK(isClosedManifold(legless));
	CHECK_FALSE(MeshShapes::arch(mesh, 0.0f, 1.0f, 0.4f, 0.5f, 8, &error));
	CHECK_FALSE(MeshShapes::arch(mesh, 2.0f, -1.0f, 0.4f, 0.5f, 8, &error));
}

TEST_CASE("sweepPath refuses a path with no length", "[meshbuilder]")
{
	Mesh mesh;
	String error;
	const Vec2f single[1] = { Vec2f(0.0f, 0.0f) };
	CHECK_FALSE(MeshShapes::sweepPath(mesh, single, 1, 1.0f, 1.0f, &error));
	const Vec2f doubled[2] = { Vec2f(1.0f, 1.0f), Vec2f(1.0f, 1.0f) };
	CHECK_FALSE(MeshShapes::sweepPath(mesh, doubled, 2, 1.0f, 1.0f, &error));
	CHECK(mesh.empty());
	const Vec2f line[2] = { Vec2f(0.0f, 0.0f), Vec2f(0.0f, 2.0f) };
	CHECK_FALSE(MeshShapes::sweepPath(mesh, line, 2, 0.0f, 1.0f, &error));
	REQUIRE(MeshShapes::sweepPath(mesh, line, 2, 0.5f, 0.5f, &error));
	CHECK(isClosedManifold(mesh));
}

TEST_CASE("revolveProfile refuses a degenerate profile", "[meshbuilder]")
{
	Mesh mesh;
	String error;
	const MeshShapes::ProfileRow one[1] = {
		MeshShapes::ProfileRow(1.0f, 0.0f, 1.0f, 0.0f) };
	CHECK_FALSE(MeshShapes::revolveProfile(mesh, one, 1, 8, 360.0f, &error));
	const MeshShapes::ProfileRow negative[2] = {
		MeshShapes::ProfileRow(1.0f, 1.0f, 1.0f, 0.0f),
		MeshShapes::ProfileRow(-1.0f, 0.0f, 1.0f, 0.0f) };
	CHECK_FALSE(MeshShapes::revolveProfile(mesh, negative, 2, 8, 360.0f,
		&error));
	const MeshShapes::ProfileRow poles[2] = {
		MeshShapes::ProfileRow(0.0f, 1.0f, 0.0f, 1.0f),
		MeshShapes::ProfileRow(0.0f, 0.0f, 0.0f, -1.0f) };
	CHECK_FALSE(MeshShapes::revolveProfile(mesh, poles, 2, 8, 360.0f, &error));
	CHECK(mesh.empty());
	// a partial sweep is legitimate and leaves the ends open (documented)
	const MeshShapes::ProfileRow wall[2] = {
		MeshShapes::ProfileRow(1.0f, 1.0f, 1.0f, 0.0f),
		MeshShapes::ProfileRow(1.0f, -1.0f, 1.0f, 0.0f) };
	REQUIRE(MeshShapes::revolveProfile(mesh, wall, 2, 8, 90.0f, &error));
	CHECK(mesh.triangleCount() == 16u);
	CHECK_FALSE(isClosedManifold(mesh));
}

//--- determinism ----------------------------------------------------------

TEST_CASE("the same parameters produce byte-identical buffers",
	"[meshbuilder]")
{
	String error;
	Mesh a;
	Mesh b;
	REQUIRE(MeshShapes::icosphere(a, 1.25f, 3, &error));
	REQUIRE(MeshShapes::icosphere(b, 1.25f, 3, &error));
	CHECK(sameBuffers(a, b));

	Mesh c;
	Mesh d;
	REQUIRE(MeshShapes::stairs(c, 3.0f, 1.5f, 2.0f, 7, &error));
	REQUIRE(MeshShapes::stairs(d, 3.0f, 1.5f, 2.0f, 7, &error));
	CHECK(sameBuffers(c, d));

	// the smooth-normal weld sorts on a quantised key with the source index as
	// the tie-break, so even coincident vertices group in a stable order
	Mesh e;
	Mesh f;
	REQUIRE(MeshShapes::torus(e, 2.0f, 0.4f, 12, 6, &error));
	REQUIRE(MeshShapes::torus(f, 2.0f, 0.4f, 12, 6, &error));
	MeshBuilder::computeSmoothNormals(e);
	MeshBuilder::computeSmoothNormals(f);
	CHECK(sameBuffers(e, f));
}

//--- the operators --------------------------------------------------------

TEST_CASE("Xform composes TRS and transforms normals under scale",
	"[meshbuilder]")
{
	const MeshBuilder::Xform identity;
	const Vec3f point(1.0f, 2.0f, 3.0f);
	const Vec3f same = identity.transformPoint(point);
	CHECK(same.x == Catch::Approx(1.0f));
	CHECK(same.y == Catch::Approx(2.0f));
	CHECK(same.z == Catch::Approx(3.0f));
	CHECK(identity.linearDeterminant() == Catch::Approx(1.0f));

	// a 90 degree yaw takes +X to -Z (right-handed, +Y up)
	const MeshBuilder::Xform yaw = MeshBuilder::Xform::fromTRS(
		Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 90.0f, 0.0f),
		Vec3f(1.0f, 1.0f, 1.0f));
	const Vec3f turned = yaw.transformPoint(Vec3f(1.0f, 0.0f, 0.0f));
	CHECK(turned.x == Catch::Approx(0.0f).margin(1.0e-5f));
	CHECK(turned.z == Catch::Approx(-1.0f).margin(1.0e-5f));

	// under NON-UNIFORM scale a normal must ride the inverse-transpose: a 45
	// degree normal on a shape squashed in x tips TOWARD x, not away
	const MeshBuilder::Xform squash = MeshBuilder::Xform::fromTRS(
		Vec3f(), Vec3f(), Vec3f(0.25f, 1.0f, 1.0f));
	const Vec3f diagonal = MeshBuilder::normalise(Vec3f(1.0f, 1.0f, 0.0f));
	const Vec3f moved = squash.transformNormal(diagonal);
	CHECK(MeshBuilder::length(moved) == Catch::Approx(1.0f).margin(1.0e-5f));
	CHECK(moved.x > moved.y);

	// composition: scale then translate
	const MeshBuilder::Xform shift = MeshBuilder::Xform::fromTRS(
		Vec3f(10.0f, 0.0f, 0.0f), Vec3f(), Vec3f(1.0f, 1.0f, 1.0f));
	const MeshBuilder::Xform both = squash.then(shift);
	const Vec3f composed = both.transformPoint(Vec3f(4.0f, 0.0f, 0.0f));
	CHECK(composed.x == Catch::Approx(11.0f));
}

TEST_CASE("a mirroring transform flips winding so faces stay outward",
	"[meshbuilder]")
{
	String error;
	Mesh source;
	REQUIRE(MeshShapes::wedge(source, 2.0f, 1.0f, 1.0f, &error));
	CHECK(windsOutwardFrom(source, Vec3f(0.5f, -0.3f, 0.0f)));

	Mesh mirrored;
	MeshBuilder::append(mirrored, source, MeshBuilder::Xform::fromTRS(
		Vec3f(), Vec3f(), Vec3f(-1.0f, 1.0f, 1.0f)), String());
	CHECK(mirrored.vertices.size() == source.vertices.size());
	// the mirrored solid's interior is at -x now, and the faces still look out
	CHECK(windsOutwardFrom(mirrored, Vec3f(-0.5f, -0.3f, 0.0f)));
	CHECK(normalsAreUnit(mirrored));
	CHECK(isClosedManifold(mirrored));
	// the handedness flag flips with the mirror
	CHECK(mirrored.vertices[0].tangent.w ==
		Catch::Approx(-source.vertices[0].tangent.w));

	// transform() in place does the same
	Mesh inPlace = source;
	MeshBuilder::transform(inPlace, MeshBuilder::Xform::fromTRS(
		Vec3f(), Vec3f(), Vec3f(1.0f, 1.0f, -1.0f)));
	CHECK(windsOutwardFrom(inPlace, Vec3f(0.5f, -0.3f, 0.0f)));
}

TEST_CASE("append merges same-material runs and keeps sections contiguous",
	"[meshbuilder]")
{
	String error;
	Mesh cube;
	REQUIRE(MeshShapes::box(cube, 1.0f, 1.0f, 1.0f, &error));
	Mesh ball;
	REQUIRE(MeshShapes::uvSphere(ball, 0.5f, 6, 4, &error));

	Mesh merged;
	const MeshBuilder::Xform here;
	MeshBuilder::append(merged, cube, here, "stone");
	MeshBuilder::append(merged, ball, MeshBuilder::Xform::fromTRS(
		Vec3f(2.0f, 0.0f, 0.0f), Vec3f(), Vec3f(1.0f, 1.0f, 1.0f)), "stone");
	MeshBuilder::append(merged, cube, MeshBuilder::Xform::fromTRS(
		Vec3f(4.0f, 0.0f, 0.0f), Vec3f(), Vec3f(1.0f, 1.0f, 1.0f)), "metal");
	// two materials => two sections, and the two stone shapes share the first
	REQUIRE(merged.sections.size() == 2);
	CHECK(merged.sections[0].material == "stone");
	CHECK(merged.sections[1].material == "metal");
	CHECK(merged.sections[0].vertexCount ==
		cube.vertices.size() + ball.vertices.size());
	CHECK(merged.sections[1].vertexCount == cube.vertices.size());
	CHECK(MeshBuilder::validate(merged));
	CHECK(merged.triangleCount() ==
		cube.triangleCount() * 2 + ball.triangleCount());

	// appendSections lifts a multi-section source whole, spans rebased
	Mesh nested;
	MeshBuilder::appendSections(nested, merged, here);
	REQUIRE(nested.sections.size() == 2);
	CHECK(MeshBuilder::validate(nested));
	CHECK(nested.triangleCount() == merged.triangleCount());

	// an empty append is a no-op
	Mesh empty;
	const std::size_t before = nested.vertices.size();
	MeshBuilder::append(nested, empty, here, "stone");
	CHECK(nested.vertices.size() == before);
}

TEST_CASE("flat normals split, smooth normals keep box edges crisp",
	"[meshbuilder]")
{
	String error;
	Mesh mesh;
	REQUIRE(MeshShapes::uvSphere(mesh, 1.0f, 8, 4, &error));
	const std::size_t triangles = mesh.triangleCount();
	MeshBuilder::computeFlatNormals(mesh);
	CHECK(mesh.vertices.size() == triangles * 3);
	CHECK(mesh.indices.size() == triangles * 3);
	CHECK(mesh.triangleCount() == triangles);
	CHECK(MeshBuilder::validate(mesh));
	CHECK(normalsAreUnit(mesh));
	// every triangle's three vertices now share one normal
	for (std::size_t each = 0; each < triangles; ++each)
	{
		Vec3f const& first = mesh.vertices[each * 3 + 0].normal;
		Vec3f const& third = mesh.vertices[each * 3 + 2].normal;
		CHECK(MeshBuilder::dot(first, third) ==
			Catch::Approx(1.0f).margin(1.0e-4f));
	}

	// a box's corner vertices weld by position but its faces meet at 90
	// degrees, so a 60 degree threshold must NOT average them away
	Mesh cube;
	REQUIRE(MeshShapes::box(cube, 1.0f, 1.0f, 1.0f, &error));
	Mesh smoothed = cube;
	MeshBuilder::computeSmoothNormals(smoothed, 60.0f);
	for (std::size_t each = 0; each < cube.vertices.size(); ++each)
	{
		CHECK(MeshBuilder::dot(cube.vertices[each].normal,
			smoothed.vertices[each].normal) ==
			Catch::Approx(1.0f).margin(1.0e-4f));
	}
	// a 180 degree threshold averages everything: a cube corner normal points
	// diagonally then
	Mesh rounded = cube;
	MeshBuilder::computeSmoothNormals(rounded, 180.0f);
	CHECK(normalsAreUnit(rounded));
	CHECK(std::fabs(rounded.vertices[0].normal.x) < 0.9f);
}

TEST_CASE("the UV strategies land in range and follow their axis",
	"[meshbuilder]")
{
	String error;
	Mesh mesh;
	REQUIRE(MeshShapes::box(mesh, 2.0f, 2.0f, 2.0f, &error));

	MeshBuilder::applyUV(mesh, MeshBuilder::UV_PLANAR_XZ);
	CHECK(uvsInRange(mesh));
	for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
	{
		MeshBuilder::Vertex const& vertex = mesh.vertices[each];
		CHECK(vertex.uv.x ==
			Catch::Approx((vertex.position.x + 1.0f) * 0.5f).margin(1.0e-4f));
	}

	MeshBuilder::applyUV(mesh, MeshBuilder::UV_BOX);
	CHECK(uvsInRange(mesh));

	MeshBuilder::applyUV(mesh, MeshBuilder::UV_CYLINDRICAL);
	CHECK(uvsInRange(mesh));

	MeshBuilder::applyUV(mesh, MeshBuilder::UV_SPHERICAL);
	CHECK(uvsInRange(mesh));

	// a tiling scale multiplies the projection (deliberately out of 0..1)
	MeshBuilder::applyUV(mesh, MeshBuilder::UV_PLANAR_XZ, Vec2f(4.0f, 4.0f));
	CHECK_FALSE(uvsInRange(mesh));
	// a zero scale is read as 1 rather than collapsing every UV
	MeshBuilder::applyUV(mesh, MeshBuilder::UV_PLANAR_XZ, Vec2f(0.0f, 0.0f));
	CHECK(uvsInRange(mesh));
	bool anyNonZero = false;
	for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
	{
		anyNonZero = anyNonZero || mesh.vertices[each].uv.x > 0.1f;
	}
	CHECK(anyNonZero);
}

TEST_CASE("tangents survive a UV-less mesh without a NaN", "[meshbuilder]")
{
	Mesh mesh;
	MeshBuilder::openSection(mesh, String());
	// one triangle whose UVs are all identical: the UV gradient is degenerate,
	// the classic source of a zero-length tangent the Hlms then rejects
	for (int each = 0; each < 3; ++each)
	{
		MeshBuilder::Vertex vertex;
		vertex.position = Vec3f(each == 1 ? 1.0f : 0.0f,
			each == 2 ? 1.0f : 0.0f, 0.0f);
		vertex.normal = Vec3f(0.0f, 0.0f, 1.0f);
		vertex.uv = Vec2f(0.5f, 0.5f);
		mesh.vertices.push_back(vertex);
		mesh.indices.push_back(static_cast<unsigned int>(each));
	}
	MeshBuilder::closeSection(mesh);
	MeshBuilder::computeTangents(mesh);
	CHECK(tangentsAreSane(mesh));
	CHECK(MeshBuilder::validate(mesh));
}

TEST_CASE("validate catches a broken section layout", "[meshbuilder]")
{
	String error;
	Mesh mesh;
	REQUIRE(MeshShapes::box(mesh, 1.0f, 1.0f, 1.0f, &error));
	REQUIRE(MeshBuilder::validate(mesh, &error));

	Mesh oddIndices = mesh;
	oddIndices.indices.pop_back();
	CHECK_FALSE(MeshBuilder::validate(oddIndices, &error));
	CHECK_FALSE(error.empty());

	Mesh outOfSpan = mesh;
	outOfSpan.sections[0].vertexCount = 3;
	CHECK_FALSE(MeshBuilder::validate(outOfSpan, &error));

	Mesh sectionless = mesh;
	sectionless.sections.clear();
	CHECK_FALSE(MeshBuilder::validate(sectionless, &error));

	Mesh poisoned = mesh;
	poisoned.vertices[4].position.y = std::nan("");
	CHECK_FALSE(MeshBuilder::validate(poisoned, &error));

	// an empty mesh with no sections is legitimately valid (nothing to check)
	Mesh nothing;
	CHECK(MeshBuilder::validate(nothing, &error));
}

TEST_CASE("normalise never returns a NaN", "[meshbuilder]")
{
	const Vec3f zero = MeshBuilder::normalise(Vec3f(0.0f, 0.0f, 0.0f));
	CHECK(MeshBuilder::length(zero) == Catch::Approx(1.0f));
	const Vec3f poisoned = MeshBuilder::normalise(
		Vec3f(std::nan(""), 0.0f, 0.0f));
	CHECK(MeshBuilder::isFinite(poisoned.x));
	CHECK(MeshBuilder::length(poisoned) == Catch::Approx(1.0f));
	CHECK_FALSE(MeshBuilder::isPositiveExtent(
		std::numeric_limits<float>::infinity()));
	CHECK_FALSE(MeshBuilder::isPositiveExtent(0.0f));
	CHECK(MeshBuilder::isPositiveExtent(0.001f));
}
