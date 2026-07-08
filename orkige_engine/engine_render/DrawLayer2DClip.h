/********************************************************************
	created:	Wednesday 2026/07/08 at 22:00
	filename: 	DrawLayer2DClip.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __DrawLayer2DClip_h__8_7_2026__22_00_00__
#define __DrawLayer2DClip_h__8_7_2026__22_00_00__

//! @file DrawLayer2DClip.h
//! @brief shared batch assembly of the DrawLayer2D backends: index
//! resolution + analytic scissor clipping
//! @remarks Backend-free by construction (facade types only) so both
//! backend TUs share ONE implementation of the scissor contract:
//! triangles are clipped Sutherland-Hodgman style against the pixel
//! rect at submission time. Vertex attributes (UV, colour) interpolate
//! linearly, so the clipped geometry rasterizes EXACTLY like a hardware
//! scissor would - and every backend behaves identically without
//! touching render-state plumbing. Output is always a flat triangle
//! list; the backends never see indices or scissors.

#include "engine_render/DrawLayer2D.h"

#include <cstddef>
#include <vector>

namespace Orkige
{
	namespace DrawLayer2DDetail
	{
		typedef DrawLayer2D::Vertex2D Vertex2D;

		inline Vertex2D lerpVertex(Vertex2D const & a, Vertex2D const & b,
			Real t)
		{
			Vertex2D result;
			result.x = a.x + (b.x - a.x) * t;
			result.y = a.y + (b.y - a.y) * t;
			result.u = a.u + (b.u - a.u) * t;
			result.v = a.v + (b.v - a.v) * t;
			result.colour.r = a.colour.r + (b.colour.r - a.colour.r) * t;
			result.colour.g = a.colour.g + (b.colour.g - a.colour.g) * t;
			result.colour.b = a.colour.b + (b.colour.b - a.colour.b) * t;
			result.colour.a = a.colour.a + (b.colour.a - a.colour.a) * t;
			return result;
		}

		//! one Sutherland-Hodgman pass: keep the polygon parts where
		//! "axisX ? v.x : v.y" is on the "keepGreater" side of bound
		inline void clipPolygonAgainstEdge(std::vector<Vertex2D> & polygon,
			std::vector<Vertex2D> & scratch, bool axisX, Real bound,
			bool keepGreater)
		{
			scratch.clear();
			const size_t count = polygon.size();
			for(size_t each = 0; each < count; ++each)
			{
				Vertex2D const & current = polygon[each];
				Vertex2D const & next = polygon[(each + 1) % count];
				const Real currentValue = axisX ? current.x : current.y;
				const Real nextValue = axisX ? next.x : next.y;
				const bool currentInside = keepGreater
					? (currentValue >= bound) : (currentValue <= bound);
				const bool nextInside = keepGreater
					? (nextValue >= bound) : (nextValue <= bound);
				if(currentInside)
				{
					scratch.push_back(current);
				}
				if(currentInside != nextInside)
				{
					const Real t = (bound - currentValue)
						/ (nextValue - currentValue);
					scratch.push_back(lerpVertex(current, next, t));
				}
			}
			polygon.swap(scratch);
		}

		//! resolve indices and append the triangles to a flat triangle
		//! list, scissor-clipped when a rect is given (@see file remarks)
		inline void appendTriangles(std::vector<Vertex2D> & outTriangleList,
			Vertex2D const * vertices, size_t vertexCount,
			unsigned short const * indices, size_t indexCount,
			DrawLayer2D::ScissorRect const * scissor)
		{
			const size_t triangleVertexCount =
				indices ? indexCount : vertexCount;
			if(!vertices || triangleVertexCount < 3)
			{
				return;
			}
			if(scissor && (scissor->width <= 0 || scissor->height <= 0))
			{
				return;	// empty clip rect clips everything away
			}
			const Real clipLeft = scissor ? Real(scissor->left) : Real(0);
			const Real clipTop = scissor ? Real(scissor->top) : Real(0);
			const Real clipRight =
				scissor ? Real(scissor->left + scissor->width) : Real(0);
			const Real clipBottom =
				scissor ? Real(scissor->top + scissor->height) : Real(0);
			std::vector<Vertex2D> polygon;
			std::vector<Vertex2D> scratch;
			const size_t triangleCount = triangleVertexCount / 3;
			for(size_t triangle = 0; triangle < triangleCount; ++triangle)
			{
				Vertex2D corners[3];
				for(size_t corner = 0; corner < 3; ++corner)
				{
					const size_t at = triangle * 3 + corner;
					const size_t vertexIndex = indices ? indices[at] : at;
					if(vertexIndex >= vertexCount)
					{
						return;	// out-of-range index: drop the rest, loudly
								// wrong geometry beats memory corruption
					}
					corners[corner] = vertices[vertexIndex];
				}
				if(!scissor)
				{
					outTriangleList.push_back(corners[0]);
					outTriangleList.push_back(corners[1]);
					outTriangleList.push_back(corners[2]);
					continue;
				}
				polygon.assign(corners, corners + 3);
				clipPolygonAgainstEdge(polygon, scratch, true, clipLeft, true);
				clipPolygonAgainstEdge(polygon, scratch, true, clipRight, false);
				clipPolygonAgainstEdge(polygon, scratch, false, clipTop, true);
				clipPolygonAgainstEdge(polygon, scratch, false, clipBottom, false);
				// fan-triangulate the clipped convex polygon
				for(size_t fan = 2; fan < polygon.size(); ++fan)
				{
					outTriangleList.push_back(polygon[0]);
					outTriangleList.push_back(polygon[fan - 1]);
					outTriangleList.push_back(polygon[fan]);
				}
			}
		}
	}
}

#endif //__DrawLayer2DClip_h__8_7_2026__22_00_00__
