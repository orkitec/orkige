/**************************************************************
	created:	2026/07/10 at 10:00
	filename: 	VectorTessellator.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file VectorTessellator.cpp
//! @brief the flatten/triangulate/feather implementation
//! @remarks THE single place the earcut single-header library is compiled -
//! nothing else in the tree includes it, so the triangulator stays out of
//! every other TU and the precompiled headers (the stb_vorbis discipline).
//! Its include path is scoped to this TU by the CMake
//! set_source_files_properties INCLUDE_DIRECTORIES on the file.

#include "core_util/VectorTessellator.h"

#include <cmath>

// earcut adapts to any point type through mapbox::util::nth; teach it to read
// this file's Point before including the header (kept local to this TU)
#include <mapbox/earcut.hpp>

namespace mapbox
{
	namespace util
	{
		template <>
		struct nth<0, Orkige::VectorTessellator::Point>
		{
			static float get(Orkige::VectorTessellator::Point const & p)
			{
				return p.x;
			}
		};
		template <>
		struct nth<1, Orkige::VectorTessellator::Point>
		{
			static float get(Orkige::VectorTessellator::Point const & p)
			{
				return p.y;
			}
		};
	}
}

namespace Orkige
{
	namespace
	{
		//! recursion cap so a pathological control net cannot spin forever
		const int FLATTEN_MAX_DEPTH = 16;

		//! squared distance of point p from the infinite line through a,b
		float distanceSqToLine(VectorTessellator::Point const & p,
			VectorTessellator::Point const & a,
			VectorTessellator::Point const & b)
		{
			const float dx = b.x - a.x;
			const float dy = b.y - a.y;
			const float lengthSq = dx * dx + dy * dy;
			if(lengthSq <= 0.0f)
			{
				const float px = p.x - a.x;
				const float py = p.y - a.y;
				return px * px + py * py;
			}
			// cross product of (b-a) x (p-a), normalized by |b-a|
			const float cross = (p.x - a.x) * dy - (p.y - a.y) * dx;
			return (cross * cross) / lengthSq;
		}

		//! de-Casteljau recursive cubic flatten (see flattenCubic contract)
		void flattenCubicRecursive(VectorTessellator::Point const & p0,
			VectorTessellator::Point const & p1,
			VectorTessellator::Point const & p2,
			VectorTessellator::Point const & p3, float toleranceSq, int depth,
			std::vector<VectorTessellator::Point> & out)
		{
			// flat enough when both inner control points hug the p0-p3 chord
			const float d1 = distanceSqToLine(p1, p0, p3);
			const float d2 = distanceSqToLine(p2, p0, p3);
			if(depth >= FLATTEN_MAX_DEPTH ||
				(d1 <= toleranceSq && d2 <= toleranceSq))
			{
				out.push_back(p3);
				return;
			}
			// split at t = 0.5
			const VectorTessellator::Point p01(
				(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
			const VectorTessellator::Point p12(
				(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
			const VectorTessellator::Point p23(
				(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f);
			const VectorTessellator::Point p012(
				(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f);
			const VectorTessellator::Point p123(
				(p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f);
			const VectorTessellator::Point mid(
				(p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f);
			flattenCubicRecursive(p0, p01, p012, mid, toleranceSq, depth + 1, out);
			flattenCubicRecursive(mid, p123, p23, p3, toleranceSq, depth + 1, out);
		}
	}

	//---------------------------------------------------------
	float VectorTessellator::Bounds::diagonal() const
	{
		if(!this->valid)
		{
			return 0.0f;
		}
		const float dx = this->maxX - this->minX;
		const float dy = this->maxY - this->minY;
		return std::sqrt(dx * dx + dy * dy);
	}
	//---------------------------------------------------------
	void VectorTessellator::Mesh::clear()
	{
		this->positions.clear();
		this->colours.clear();
		this->indices.clear();
		this->bounds = Bounds();
	}
	//---------------------------------------------------------
	void VectorTessellator::flattenCubic(Point const & p0, Point const & p1,
		Point const & p2, Point const & p3, float tolerance,
		std::vector<Point> & out)
	{
		if(tolerance <= 0.0f)
		{
			out.push_back(p3);	// no budget to curve: a straight segment
			return;
		}
		flattenCubicRecursive(p0, p1, p2, p3, tolerance * tolerance, 0, out);
	}
	//---------------------------------------------------------
	void VectorTessellator::flattenQuadratic(Point const & p0, Point const & p1,
		Point const & p2, float tolerance, std::vector<Point> & out)
	{
		// elevate the quadratic to an equivalent cubic, then reuse flattenCubic
		const Point c1((p0.x + 2.0f * p1.x) / 3.0f, (p0.y + 2.0f * p1.y) / 3.0f);
		const Point c2((p2.x + 2.0f * p1.x) / 3.0f, (p2.y + 2.0f * p1.y) / 3.0f);
		flattenCubic(p0, c1, c2, p2, tolerance, out);
	}
	//---------------------------------------------------------
	void VectorTessellator::triangulateFill(Region const & region, Mesh & out)
	{
		if(region.outer.size() < 3)
		{
			return;	// not a fillable area
		}
		// earcut input: ring 0 = outer, rings 1.. = holes. Indices it returns
		// address the outer+holes vertices in concatenation order.
		std::vector<std::vector<Point> > polygon;
		polygon.push_back(region.outer);
		for(std::vector<Point> const & hole : region.holes)
		{
			if(hole.size() >= 3)
			{
				polygon.push_back(hole);
			}
		}
		std::vector<unsigned int> localIndices =
			mapbox::earcut<unsigned int>(polygon);
		if(localIndices.empty())
		{
			return;	// earcut could not triangulate (degenerate) - skip honestly
		}
		const unsigned int base =
			static_cast<unsigned int>(out.positions.size());
		for(std::vector<Point> const & ring : polygon)
		{
			for(Point const & point : ring)
			{
				out.positions.push_back(point);
				out.colours.push_back(region.fill);
			}
		}
		for(unsigned int index : localIndices)
		{
			out.indices.push_back(base + index);
		}
	}
	//---------------------------------------------------------
	void VectorTessellator::appendFeather(std::vector<Point> const & contour,
		Colour const & fill, float width, Mesh & out)
	{
		const std::size_t n = contour.size();
		if(n < 3 || width <= 0.0f)
		{
			return;
		}
		// outward normals point away from the interior; the winding sign flips
		// them so a clockwise contour feathers outward too
		const float windingSign = signedArea(contour) >= 0.0f ? 1.0f : -1.0f;
		const Colour outerColour(fill.r, fill.g, fill.b, 0.0f);
		const Colour innerColour(fill.r, fill.g, fill.b, fill.a);

		const unsigned int base =
			static_cast<unsigned int>(out.positions.size());
		for(std::size_t i = 0; i < n; ++i)
		{
			Point const & prev = contour[(i + n - 1) % n];
			Point const & here = contour[i];
			Point const & next = contour[(i + 1) % n];
			// per-edge outward normal (for CCW: right of the travel direction)
			float pnx = (here.y - prev.y);
			float pny = -(here.x - prev.x);
			float nnx = (next.y - here.y);
			float nny = -(next.x - here.x);
			// normalize each, then average, then normalize the result
			auto norm = [](float & x, float & y)
			{
				const float len = std::sqrt(x * x + y * y);
				if(len > 1e-6f)
				{
					x /= len;
					y /= len;
				}
			};
			norm(pnx, pny);
			norm(nnx, nny);
			float nx = (pnx + nnx) * windingSign;
			float ny = (pny + nny) * windingSign;
			norm(nx, ny);

			// inner vertex sits on the contour (opaque), outer is extruded out
			out.positions.push_back(here);
			out.colours.push_back(innerColour);
			out.positions.push_back(Point(here.x + nx * width,
				here.y + ny * width));
			out.colours.push_back(outerColour);
		}
		for(std::size_t i = 0; i < n; ++i)
		{
			const unsigned int innerHere = base + static_cast<unsigned int>(i * 2);
			const unsigned int outerHere = innerHere + 1;
			const std::size_t j = (i + 1) % n;
			const unsigned int innerNext = base + static_cast<unsigned int>(j * 2);
			const unsigned int outerNext = innerNext + 1;
			out.indices.push_back(innerHere);
			out.indices.push_back(outerHere);
			out.indices.push_back(outerNext);
			out.indices.push_back(innerHere);
			out.indices.push_back(outerNext);
			out.indices.push_back(innerNext);
		}
	}
	//---------------------------------------------------------
	void VectorTessellator::build(std::vector<Region> const & regions,
		float featherWidth, Mesh & out)
	{
		out.clear();
		for(Region const & region : regions)
		{
			triangulateFill(region, out);
		}
		if(featherWidth > 0.0f)
		{
			for(Region const & region : regions)
			{
				appendFeather(region.outer, region.fill, featherWidth, out);
			}
		}
		out.bounds = computeBounds(regions);
	}
	//---------------------------------------------------------
	VectorTessellator::Bounds VectorTessellator::computeBounds(
		std::vector<Region> const & regions)
	{
		Bounds bounds;
		for(Region const & region : regions)
		{
			for(Point const & point : region.outer)
			{
				if(!bounds.valid)
				{
					bounds.minX = bounds.maxX = point.x;
					bounds.minY = bounds.maxY = point.y;
					bounds.valid = true;
					continue;
				}
				bounds.minX = point.x < bounds.minX ? point.x : bounds.minX;
				bounds.minY = point.y < bounds.minY ? point.y : bounds.minY;
				bounds.maxX = point.x > bounds.maxX ? point.x : bounds.maxX;
				bounds.maxY = point.y > bounds.maxY ? point.y : bounds.maxY;
			}
		}
		return bounds;
	}
	//---------------------------------------------------------
	float VectorTessellator::defaultFeatherWidth(Bounds const & bounds)
	{
		// a small fraction of the bounding diagonal keeps the soft edge a
		// constant visual proportion regardless of the authored shape scale
		return bounds.diagonal() * 0.01f;
	}
	//---------------------------------------------------------
	float VectorTessellator::signedArea(std::vector<Point> const & contour)
	{
		const std::size_t n = contour.size();
		if(n < 3)
		{
			return 0.0f;
		}
		float twiceArea = 0.0f;
		for(std::size_t i = 0; i < n; ++i)
		{
			Point const & a = contour[i];
			Point const & b = contour[(i + 1) % n];
			twiceArea += (a.x * b.y) - (b.x * a.y);
		}
		return twiceArea * 0.5f;
	}
}
