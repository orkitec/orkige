/********************************************************************
	created:	Friday 2026/07/25 at 10:00
	filename: 	ShapeCollider.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/ShapeCollider.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! 2D cross product of (b-a) x (c-a) - sign gives the turn direction
		float cross2(ShapeCollider::Point const & a, ShapeCollider::Point const & b,
			ShapeCollider::Point const & c)
		{
			return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
		}
	}
	//---------------------------------------------------------
	std::vector<ShapeCollider::Point> ShapeCollider::openLoop(
		std::vector<Point> const & loop)
	{
		std::vector<Point> out = loop;
		while (out.size() >= 2)
		{
			Point const & first = out.front();
			Point const & last = out.back();
			if (std::fabs(first.x - last.x) < 1.0e-6f &&
				std::fabs(first.y - last.y) < 1.0e-6f)
			{
				out.pop_back();
			}
			else
			{
				break;
			}
		}
		return out;
	}
	//---------------------------------------------------------
	bool ShapeCollider::isSolidRegion(VectorTessellator::Region const & region)
	{
		// only filled AREAS carry a closed boundary; a stroke is a swept
		// centreline enclosing no area
		if (region.kind != VectorTessellator::REGION_FILL)
		{
			return false;
		}
		return openLoop(region.outer).size() >= 3;
	}
	//---------------------------------------------------------
	void ShapeCollider::extractContours(
		std::vector<VectorTessellator::Region> const & regions,
		std::vector<std::vector<Point> > & outContours)
	{
		outContours.clear();
		for (VectorTessellator::Region const & region : regions)
		{
			// the ONE eligibility test + the ONE loop normalisation every
			// consumer of these contours shares (holes are ignored in v1)
			if (!isSolidRegion(region))
			{
				continue;
			}
			outContours.push_back(openLoop(region.outer));
		}
	}
	//---------------------------------------------------------
	std::vector<ShapeCollider::Point> ShapeCollider::convexHull(
		std::vector<Point> const & points)
	{
		// deduplicate coincident points first (Andrew's monotone chain needs a
		// clean set; duplicates create zero-length edges the collinear test trips on)
		std::vector<Point> pts;
		pts.reserve(points.size());
		for (Point const & p : points)
		{
			bool duplicate = false;
			for (Point const & q : pts)
			{
				if (std::fabs(p.x - q.x) < 1.0e-6f &&
					std::fabs(p.y - q.y) < 1.0e-6f)
				{
					duplicate = true;
					break;
				}
			}
			if (!duplicate)
			{
				pts.push_back(p);
			}
		}
		if (pts.size() < 3)
		{
			return pts;	// a degenerate hull (a point or a segment)
		}
		std::sort(pts.begin(), pts.end(), [](Point const & a, Point const & b)
		{
			return a.x < b.x || (a.x == b.x && a.y < b.y);
		});
		const std::size_t n = pts.size();
		std::vector<Point> hull(2 * n);
		std::size_t k = 0;
		// lower hull
		for (std::size_t i = 0; i < n; ++i)
		{
			while (k >= 2 && cross2(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f)
			{
				--k;
			}
			hull[k++] = pts[i];
		}
		// upper hull
		const std::size_t lower = k + 1;
		for (std::size_t i = n - 1; i-- > 0; )
		{
			while (k >= lower && cross2(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f)
			{
				--k;
			}
			hull[k++] = pts[i];
		}
		hull.resize(k - 1);	// drop the repeated start point
		return hull;
	}
	//---------------------------------------------------------
	bool ShapeCollider::isConvex(std::vector<Point> const & contour)
	{
		std::vector<Point> loop = openLoop(contour);
		const std::size_t n = loop.size();
		if (n < 3)
		{
			return true;	// nothing to degrade
		}
		bool sawPositive = false;
		bool sawNegative = false;
		for (std::size_t i = 0; i < n; ++i)
		{
			const float turn = cross2(loop[i], loop[(i + 1) % n],
				loop[(i + 2) % n]);
			if (turn > 1.0e-6f)
			{
				sawPositive = true;
			}
			else if (turn < -1.0e-6f)
			{
				sawNegative = true;
			}
			// collinear (|turn| ~ 0) does not break convexity
			if (sawPositive && sawNegative)
			{
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	void ShapeCollider::buildExtrudedMesh(
		std::vector<std::vector<Point> > const & contours, float halfThickness,
		std::vector<Vertex> & outVertices, std::vector<unsigned int> & outIndices)
	{
		outVertices.clear();
		outIndices.clear();
		if (halfThickness <= 0.0f)
		{
			return;
		}
		for (std::vector<Point> const & rawContour : contours)
		{
			std::vector<Point> contour = openLoop(rawContour);
			if (contour.size() < 3)
			{
				continue;
			}
			// normalize to CCW so the extrusion winding is deterministic: the
			// front cap (+z) then faces +z, the back cap -z and the side walls
			// OUTWARD - the ball collides with the outward faces (mesh triangles
			// are one-sided in the simulation)
			if (VectorTessellator::signedArea(contour) < 0.0f)
			{
				std::reverse(contour.begin(), contour.end());
			}
			// triangulate the cap through the shared tessellator (earcut, concave
			// safe). A hole-free fill emits its outer points in order, so the cap
			// vertices line up 1:1 with the contour for the side-wall indexing.
			VectorTessellator::Region region;
			region.kind = VectorTessellator::REGION_FILL;
			region.outer = contour;
			VectorTessellator::Mesh cap;
			VectorTessellator::triangulateFill(region, cap);
			if (cap.positions.size() != contour.size() || cap.indices.empty())
			{
				continue;	// degenerate: earcut could not triangulate
			}
			const unsigned int n = static_cast<unsigned int>(contour.size());
			const unsigned int frontBase =
				static_cast<unsigned int>(outVertices.size());
			for (Point const & p : contour)
			{
				outVertices.push_back(Vertex(p.x, p.y, halfThickness));
			}
			const unsigned int backBase =
				static_cast<unsigned int>(outVertices.size());
			for (Point const & p : contour)
			{
				outVertices.push_back(Vertex(p.x, p.y, -halfThickness));
			}
			// front cap (facing +z) and back cap (reversed winding, facing -z)
			for (std::size_t t = 0; t + 3 <= cap.indices.size(); t += 3)
			{
				const unsigned int a = cap.indices[t];
				const unsigned int b = cap.indices[t + 1];
				const unsigned int c = cap.indices[t + 2];
				outIndices.push_back(frontBase + a);
				outIndices.push_back(frontBase + b);
				outIndices.push_back(frontBase + c);
				outIndices.push_back(backBase + c);
				outIndices.push_back(backBase + b);
				outIndices.push_back(backBase + a);
			}
			// side walls: one quad (two triangles) per contour edge, wound so the
			// XY normal points OUTWARD (right of the CCW walk direction), toward
			// whatever collides with the solid from outside
			for (unsigned int i = 0; i < n; ++i)
			{
				const unsigned int j = (i + 1) % n;
				outIndices.push_back(frontBase + i);
				outIndices.push_back(backBase + j);
				outIndices.push_back(frontBase + j);
				outIndices.push_back(frontBase + i);
				outIndices.push_back(backBase + i);
				outIndices.push_back(backBase + j);
			}
		}
	}
	//---------------------------------------------------------
	void ShapeCollider::extrudeOutlinePoints(std::vector<Point> const & outline,
		float halfThickness, std::vector<Vertex> & outVertices)
	{
		outVertices.clear();
		std::vector<Point> loop = openLoop(outline);
		for (Point const & p : loop)
		{
			outVertices.push_back(Vertex(p.x, p.y, halfThickness));
			outVertices.push_back(Vertex(p.x, p.y, -halfThickness));
		}
	}
}
