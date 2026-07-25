/**************************************************************
	created:	2026/07/25 at 12:00
	filename: 	DebugDrawBufferTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the pure immediate-mode debug-draw collector
	(DebugDrawBuffer): the deterministic primitive -> line-segment expansion
	(line=2, box=24, sphere=3*SPHERE_SEGMENTS*2 vertices), the per-entry
	lifetime (frame-only dropped after one advance, a TTL aged down and dropped
	when it elapses) and the capacity cap (overflow dropped + counted). The
	rendered proof (the segments reach the GPU and colour the frame) is the
	player debug-draw selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <engine_graphic/DebugDrawBuffer.h>

#include <vector>

using Orkige::DebugDrawBuffer;
using Orkige::LineMesh;
using Orkige::Vec3;
using Orkige::Color;

TEST_CASE("DebugDrawBuffer expands primitives to the exact segment vertex count", "[unit][debugdraw]")
{
	DebugDrawBuffer buffer;
	const Color white(1, 1, 1, 1);

	buffer.addLine(Vec3(0, 0, 0), Vec3(1, 0, 0), white, 0.0f);
	buffer.addBox(Vec3(0, 0, 0), Vec3(1, 1, 1), white, 0.0f);
	buffer.addSphere(Vec3(0, 0, 0), 1.0f, white, 0.0f);

	REQUIRE(buffer.getPrimitiveCount() == 3);
	const std::size_t expected = 2 /*line*/ + 24 /*box: 12 edges*2*/
		+ 3 * DebugDrawBuffer::SPHERE_SEGMENTS * 2 /*sphere: 3 circles*/;
	REQUIRE(buffer.segmentVertexCount() == expected);

	std::vector<LineMesh::Vertex> out;
	buffer.buildSegments(out);
	REQUIRE(out.size() == expected);
	// the built vertices carry the requested colour, and the line's endpoints
	// are its exact arguments (the first primitive is the line)
	REQUIRE(out[0].position == Vec3(0, 0, 0));
	REQUIRE(out[1].position == Vec3(1, 0, 0));
	REQUIRE(out[0].colour == white);
}

TEST_CASE("DebugDrawBuffer drops frame-only primitives after one advance", "[unit][debugdraw]")
{
	DebugDrawBuffer buffer;
	buffer.addLine(Vec3::ZERO, Vec3(1, 0, 0), Color(1, 0, 0, 1), 0.0f);
	REQUIRE(buffer.getPrimitiveCount() == 1);
	// a frame-only primitive is rendered for one frame then dropped by advance
	buffer.advance(0.016f);
	REQUIRE(buffer.getPrimitiveCount() == 0);
	REQUIRE(buffer.empty());
}

TEST_CASE("DebugDrawBuffer ages a TTL primitive and drops it when it elapses", "[unit][debugdraw]")
{
	DebugDrawBuffer buffer;
	buffer.addBox(Vec3::ZERO, Vec3(1, 1, 1), Color(0, 1, 0, 1), 1.0f);
	REQUIRE(buffer.getPrimitiveCount() == 1);

	// half a second: still alive
	buffer.advance(0.5f);
	REQUIRE(buffer.getPrimitiveCount() == 1);
	// the remaining half plus a hair: elapsed, dropped
	buffer.advance(0.51f);
	REQUIRE(buffer.getPrimitiveCount() == 0);
}

TEST_CASE("DebugDrawBuffer keeps TTL entries but drops frame-only alongside", "[unit][debugdraw]")
{
	DebugDrawBuffer buffer;
	buffer.addLine(Vec3::ZERO, Vec3(1, 0, 0), Color(1, 1, 1, 1), 2.0f);	// TTL
	buffer.addLine(Vec3::ZERO, Vec3(0, 1, 0), Color(1, 1, 1, 1), 0.0f);	// frame-only
	REQUIRE(buffer.getPrimitiveCount() == 2);
	buffer.advance(0.1f);
	// the frame-only one is gone; the TTL one survives
	REQUIRE(buffer.getPrimitiveCount() == 1);
}

TEST_CASE("DebugDrawBuffer caps at capacity and counts the overflow", "[unit][debugdraw]")
{
	DebugDrawBuffer buffer;
	buffer.setCapacity(4);
	for(int each = 0; each < 10; ++each)
	{
		buffer.addLine(Vec3::ZERO, Vec3(1, 0, 0), Color(1, 1, 1, 1), 0.0f);
	}
	REQUIRE(buffer.getPrimitiveCount() == 4);	// capped
	REQUIRE(buffer.getDroppedCount() == 6);		// the rest counted, not stored
}

TEST_CASE("DebugDrawBuffer clear drops everything", "[unit][debugdraw]")
{
	DebugDrawBuffer buffer;
	buffer.addSphere(Vec3::ZERO, 1.0f, Color(1, 1, 1, 1), 5.0f);
	buffer.clear();
	REQUIRE(buffer.empty());
	std::vector<LineMesh::Vertex> out;
	buffer.buildSegments(out);
	REQUIRE(out.empty());
}
