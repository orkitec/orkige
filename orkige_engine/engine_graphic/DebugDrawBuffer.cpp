/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	DebugDrawBuffer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_graphic/DebugDrawBuffer.h"
#include <core_debug/DebugMacros.h>

#include <cmath>

namespace Orkige
{
	//---------------------------------------------------------
	const std::size_t DebugDrawBuffer::DEFAULT_CAPACITY = 4096;
	const std::size_t DebugDrawBuffer::SPHERE_SEGMENTS = 16;
	//---------------------------------------------------------
	DebugDrawBuffer::DebugDrawBuffer()
		: mCapacity(DEFAULT_CAPACITY), mDropped(0), mOverflowWarned(false)
	{
		this->mPrimitives.reserve(DEFAULT_CAPACITY);
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::setCapacity(std::size_t maxPrimitives)
	{
		this->mCapacity = maxPrimitives;
		if(maxPrimitives > 0)
		{
			this->mPrimitives.reserve(maxPrimitives);
		}
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::push(Primitive const & primitive)
	{
		if(this->mCapacity != 0 && this->mPrimitives.size() >= this->mCapacity)
		{
			++this->mDropped;
			if(!this->mOverflowWarned)
			{
				this->mOverflowWarned = true;
				oDebugWarn("engine", 0, "DebugDraw: over "
					<< this->mCapacity << " primitives in one frame - "
					"dropping the overflow (raise the cap or draw fewer)");
			}
			return;
		}
		this->mPrimitives.push_back(primitive);
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::addLine(Vec3 const & p1, Vec3 const & p2,
		Color const & colour, float seconds)
	{
		Primitive primitive;
		primitive.kind = KIND_LINE;
		primitive.a = p1;
		primitive.b = p2;
		primitive.colour = colour;
		primitive.remaining = seconds;
		primitive.frameOnly = seconds <= 0.0f;
		this->push(primitive);
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::addBox(Vec3 const & centre, Vec3 const & halfExtents,
		Color const & colour, float seconds)
	{
		Primitive primitive;
		primitive.kind = KIND_BOX;
		primitive.a = centre;
		primitive.b = halfExtents;
		primitive.colour = colour;
		primitive.remaining = seconds;
		primitive.frameOnly = seconds <= 0.0f;
		this->push(primitive);
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::addSphere(Vec3 const & centre, float radius,
		Color const & colour, float seconds)
	{
		Primitive primitive;
		primitive.kind = KIND_SPHERE;
		primitive.a = centre;
		primitive.b = Vec3(radius, 0.0f, 0.0f);
		primitive.colour = colour;
		primitive.remaining = seconds;
		primitive.frameOnly = seconds <= 0.0f;
		this->push(primitive);
	}
	//---------------------------------------------------------
	std::size_t DebugDrawBuffer::segmentVertexCount() const
	{
		std::size_t vertices = 0;
		for(Primitive const & primitive : this->mPrimitives)
		{
			switch(primitive.kind)
			{
			case KIND_LINE:		vertices += 2; break;
			case KIND_BOX:		vertices += 24; break;	// 12 edges * 2
			case KIND_SPHERE:	vertices += 3 * SPHERE_SEGMENTS * 2; break;
			}
		}
		return vertices;
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::buildSegments(
		std::vector<LineMesh::Vertex> & out) const
	{
		out.clear();
		out.reserve(this->segmentVertexCount());
		for(Primitive const & primitive : this->mPrimitives)
		{
			Color const & c = primitive.colour;
			if(primitive.kind == KIND_LINE)
			{
				out.push_back(LineMesh::Vertex(primitive.a, c));
				out.push_back(LineMesh::Vertex(primitive.b, c));
			}
			else if(primitive.kind == KIND_BOX)
			{
				Vec3 const & p = primitive.a;
				Vec3 const & h = primitive.b;
				// the eight corners, then the twelve edges (bottom, top, verticals)
				const Vec3 corner[8] = {
					Vec3(p.x - h.x, p.y - h.y, p.z - h.z),
					Vec3(p.x + h.x, p.y - h.y, p.z - h.z),
					Vec3(p.x + h.x, p.y + h.y, p.z - h.z),
					Vec3(p.x - h.x, p.y + h.y, p.z - h.z),
					Vec3(p.x - h.x, p.y - h.y, p.z + h.z),
					Vec3(p.x + h.x, p.y - h.y, p.z + h.z),
					Vec3(p.x + h.x, p.y + h.y, p.z + h.z),
					Vec3(p.x - h.x, p.y + h.y, p.z + h.z)
				};
				const int edge[12][2] = {
					{0, 1}, {1, 2}, {2, 3}, {3, 0},		// bottom (z-)
					{4, 5}, {5, 6}, {6, 7}, {7, 4},		// top (z+)
					{0, 4}, {1, 5}, {2, 6}, {3, 7}		// verticals
				};
				for(int e = 0; e < 12; ++e)
				{
					out.push_back(LineMesh::Vertex(corner[edge[e][0]], c));
					out.push_back(LineMesh::Vertex(corner[edge[e][1]], c));
				}
			}
			else	// KIND_SPHERE
			{
				const Vec3 & p = primitive.a;
				const float r = primitive.b.x;
				const std::size_t n = SPHERE_SEGMENTS;
				const float twoPi = 6.28318530718f;
				// three axis-aligned great circles (XY, XZ, YZ)
				for(int plane = 0; plane < 3; ++plane)
				{
					for(std::size_t s = 0; s < n; ++s)
					{
						const float a0 = twoPi * (float)s / (float)n;
						const float a1 = twoPi * (float)(s + 1) / (float)n;
						const float c0 = std::cos(a0), s0 = std::sin(a0);
						const float c1 = std::cos(a1), s1 = std::sin(a1);
						Vec3 v0, v1;
						if(plane == 0)		// XY
						{
							v0 = Vec3(p.x + r * c0, p.y + r * s0, p.z);
							v1 = Vec3(p.x + r * c1, p.y + r * s1, p.z);
						}
						else if(plane == 1)	// XZ
						{
							v0 = Vec3(p.x + r * c0, p.y, p.z + r * s0);
							v1 = Vec3(p.x + r * c1, p.y, p.z + r * s1);
						}
						else				// YZ
						{
							v0 = Vec3(p.x, p.y + r * c0, p.z + r * s0);
							v1 = Vec3(p.x, p.y + r * c1, p.z + r * s1);
						}
						out.push_back(LineMesh::Vertex(v0, c));
						out.push_back(LineMesh::Vertex(v1, c));
					}
				}
			}
		}
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::advance(float deltaTime)
	{
		std::size_t write = 0;
		for(std::size_t read = 0; read < this->mPrimitives.size(); ++read)
		{
			Primitive & primitive = this->mPrimitives[read];
			if(primitive.frameOnly)
			{
				continue;	// rendered its one frame - drop
			}
			primitive.remaining -= deltaTime;
			if(primitive.remaining <= 0.0f)
			{
				continue;	// TTL expired - drop
			}
			if(write != read)
			{
				this->mPrimitives[write] = primitive;
			}
			++write;
		}
		this->mPrimitives.resize(write);
	}
	//---------------------------------------------------------
	void DebugDrawBuffer::clear()
	{
		this->mPrimitives.clear();
	}
}
