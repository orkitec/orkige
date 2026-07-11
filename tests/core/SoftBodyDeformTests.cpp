/**************************************************************
	created:	2026/07/11 at 09:30
	filename: 	SoftBodyDeformTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the fixed-topology weight-based deformer behind soft,
	deformable organic shapes: skinning reproduces the rest pose exactly, an
	impulse deforms and then decays back to the EXACT rest (no drift), the
	squash/stretch affine changes the silhouette anisotropically (compress
	one axis, bulge the other), and same-topology morph targets blend. Pure
	math on the tessellator output - no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/SoftBodyDeform.h"
#include "core_util/VectorTessellator.h"

#include <cmath>
#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	using Point = VectorTessellator::Point;
	using Region = VectorTessellator::Region;
	using Mesh = VectorTessellator::Mesh;

	//! an organic ~16-gon blob region (radius ~1), one fill
	std::vector<Region> blobRegions(float radiusScaleY = 1.0f)
	{
		Region region;
		region.fill = VectorTessellator::Colour(0.9f, 0.5f, 0.4f, 1.0f);
		const int n = 16;
		for(int i = 0; i < n; ++i)
		{
			const float a = 2.0f * 3.14159265f * i / n;
			const float r = 1.0f + 0.12f * std::sin(a * 3.0f);
			region.outer.push_back(
				Point(r * std::cos(a), r * radiusScaleY * std::sin(a)));
		}
		std::vector<Region> regions;
		regions.push_back(region);
		return regions;
	}

	//! summed unsigned triangle area over an index list + position array
	float meshArea(std::vector<Point> const & positions,
		std::vector<unsigned int> const & indices)
	{
		float total = 0.0f;
		for(std::size_t t = 0; t + 2 < indices.size(); t += 3)
		{
			Point const & a = positions[indices[t + 0]];
			Point const & b = positions[indices[t + 1]];
			Point const & c = positions[indices[t + 2]];
			total += std::fabs((b.x - a.x) * (c.y - a.y) -
				(c.x - a.x) * (b.y - a.y)) * 0.5f;
		}
		return total;
	}

	//! axis-aligned bounds of a position array
	void bounds(std::vector<Point> const & p, float & w, float & h)
	{
		float minX = p[0].x, maxX = p[0].x, minY = p[0].y, maxY = p[0].y;
		for(Point const & q : p)
		{
			minX = std::min(minX, q.x);
			maxX = std::max(maxX, q.x);
			minY = std::min(minY, q.y);
			maxY = std::max(maxY, q.y);
		}
		w = maxX - minX;
		h = maxY - minY;
	}

	//! build a deformer over a freshly tessellated blob; returns the rest mesh
	SoftBodyDeform makeDeformer(std::vector<Region> const & regions, Mesh & mesh,
		SoftBodyDeform::Params params = SoftBodyDeform::Params())
	{
		VectorTessellator::build(regions, 0.02f, mesh);
		SoftBodyDeform deform;
		deform.build(regions, mesh.positions, params);
		return deform;
	}
}

TEST_CASE("SoftBodyDeform builds control points + skinning", "[softbody]")
{
	Mesh mesh;
	std::vector<Region> regions = blobRegions();
	SoftBodyDeform deform = makeDeformer(regions, mesh);
	REQUIRE(deform.isBuilt());
	REQUIRE(deform.vertexCount() == mesh.positions.size());
	REQUIRE(deform.controlPointCount() > 0);
	REQUIRE(deform.controlPointCount() <=
		static_cast<std::size_t>(SoftBodyDeform::Params().controlPointCount));
}

TEST_CASE("SoftBodyDeform reproduces the rest pose exactly", "[softbody]")
{
	Mesh mesh;
	std::vector<Region> regions = blobRegions();
	SoftBodyDeform deform = makeDeformer(regions, mesh);
	// no drive, no update: the deformed positions ARE the rest positions
	std::vector<Point> out;
	deform.writePositions(out);
	REQUIRE(out.size() == mesh.positions.size());
	for(std::size_t i = 0; i < out.size(); ++i)
	{
		REQUIRE(out[i].x == mesh.positions[i].x);
		REQUIRE(out[i].y == mesh.positions[i].y);
	}
	REQUIRE(deform.isAtRest());
}

TEST_CASE("SoftBodyDeform impulse deforms then returns to EXACT rest",
	"[softbody]")
{
	Mesh mesh;
	std::vector<Region> regions = blobRegions();
	SoftBodyDeform deform = makeDeformer(regions, mesh);

	deform.applyImpulse(0.0f, -1.0f, 3.0f);	// a downward impact
	// a few frames in: the mesh has actually moved off rest (the dynamic path)
	bool movedDuringWobble = false;
	for(int i = 0; i < 10; ++i)
	{
		deform.update(1.0f / 60.0f);
		if(deform.maxControlDisplacement() > 1.0e-3f)
		{
			movedDuringWobble = true;
		}
	}
	REQUIRE(movedDuringWobble);
	REQUIRE_FALSE(deform.isAtRest());
	std::vector<Point> deformed;
	deform.writePositions(deformed);
	bool anyVertexMoved = false;
	for(std::size_t i = 0; i < deformed.size(); ++i)
	{
		if(std::fabs(deformed[i].x - mesh.positions[i].x) > 1.0e-4f ||
			std::fabs(deformed[i].y - mesh.positions[i].y) > 1.0e-4f)
		{
			anyVertexMoved = true;
			break;
		}
	}
	REQUIRE(anyVertexMoved);

	// let it decay: it must settle AND return bit-exactly to the rest pose
	for(int i = 0; i < 600; ++i)
	{
		deform.update(1.0f / 60.0f);
	}
	REQUIRE(deform.isAtRest());
	std::vector<Point> settled;
	deform.writePositions(settled);
	for(std::size_t i = 0; i < settled.size(); ++i)
	{
		REQUIRE(settled[i].x == mesh.positions[i].x);
		REQUIRE(settled[i].y == mesh.positions[i].y);
	}
}

TEST_CASE("SoftBodyDeform squash is anisotropic and roughly volume-preserving",
	"[softbody]")
{
	Mesh mesh;
	std::vector<Region> regions = blobRegions();
	SoftBodyDeform deform = makeDeformer(regions, mesh);

	float restW = 0.0f, restH = 0.0f;
	bounds(mesh.positions, restW, restH);
	const float restArea = meshArea(mesh.positions, mesh.indices);

	// a strong vertical impact: compress the height, bulge the width
	deform.applyImpulse(0.0f, -1.0f, 1.0f);
	float minH = restH, maxW = restW, areaAtPeak = restArea;
	std::vector<Point> out;
	for(int i = 0; i < 30; ++i)
	{
		deform.update(1.0f / 60.0f);
		deform.writePositions(out);
		float w = 0.0f, h = 0.0f;
		bounds(out, w, h);
		if(h < minH)
		{
			minH = h;
			maxW = w;
			areaAtPeak = meshArea(out, mesh.indices);
		}
	}
	REQUIRE(minH < restH);			// height squashed
	REQUIRE(maxW > restW);			// width bulged
	// volume-preserving affine, approximated by skinning: area stays near rest
	REQUIRE(areaAtPeak == Approx(restArea).epsilon(0.25));
}

TEST_CASE("SoftBodyDeform velocity stretches along motion", "[softbody]")
{
	Mesh mesh;
	std::vector<Region> regions = blobRegions();
	SoftBodyDeform deform = makeDeformer(regions, mesh);
	float restW = 0.0f, restH = 0.0f;
	bounds(mesh.positions, restW, restH);

	deform.setBodyVelocity(6.0f, 0.0f);	// moving fast along +x
	deform.update(1.0f / 60.0f);
	std::vector<Point> out;
	deform.writePositions(out);
	float w = 0.0f, h = 0.0f;
	bounds(out, w, h);
	REQUIRE(w > restW);		// elongated along the motion
	REQUIRE(h < restH);		// squeezed on the cross axis
}

TEST_CASE("SoftBodyDeform morph targets require matching topology", "[softbody]")
{
	Mesh mesh;
	std::vector<Region> regions = blobRegions();
	SoftBodyDeform deform = makeDeformer(regions, mesh);

	// a same-structure pose (the blob squished vertically) - accepted
	std::vector<Region> squished = blobRegions(0.5f);
	REQUIRE(SoftBodyDeform::sameTopology(regions, squished));
	REQUIRE(deform.addMorphTarget("squish", regions, squished));
	REQUIRE(deform.morphTargetCount() == 1);

	// a different vertex count - rejected, nothing added
	Region small;
	small.fill = VectorTessellator::Colour(1, 1, 1, 1);
	small.outer.push_back(Point(-1, -1));
	small.outer.push_back(Point(1, -1));
	small.outer.push_back(Point(0, 1));
	std::vector<Region> triangle;
	triangle.push_back(small);
	REQUIRE_FALSE(SoftBodyDeform::sameTopology(regions, triangle));
	REQUIRE_FALSE(deform.addMorphTarget("bad", regions, triangle));
	REQUIRE(deform.morphTargetCount() == 1);
}

TEST_CASE("SoftBodyDeform morph blends toward the target then stops",
	"[softbody]")
{
	Mesh mesh;
	std::vector<Region> regions = blobRegions();
	SoftBodyDeform deform = makeDeformer(regions, mesh);
	std::vector<Region> squished = blobRegions(0.5f);
	REQUIRE(deform.addMorphTarget("squish", regions, squished));

	deform.playMorph(0, 2.0f, false);	// reach the target in ~0.5s
	REQUIRE(deform.isMorphPlaying());
	float phaseEarly = 0.0f;
	for(int i = 0; i < 10; ++i)
	{
		deform.update(1.0f / 60.0f);
		if(i == 2)
		{
			phaseEarly = deform.getMorphPhase();
		}
	}
	REQUIRE(phaseEarly > 0.0f);
	// the mesh followed the blend (moved off rest)
	std::vector<Point> out;
	deform.writePositions(out);
	bool moved = false;
	for(std::size_t i = 0; i < out.size(); ++i)
	{
		if(std::fabs(out[i].y - mesh.positions[i].y) > 1.0e-3f)
		{
			moved = true;
			break;
		}
	}
	REQUIRE(moved);
	// non-looping: it reaches full blend and stops advancing
	for(int i = 0; i < 60; ++i)
	{
		deform.update(1.0f / 60.0f);
	}
	REQUIRE(deform.getMorphPhase() == Approx(1.0f));
	REQUIRE_FALSE(deform.isMorphPlaying());
}
