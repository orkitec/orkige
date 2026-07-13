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
		const int GRADIENT_SUBDIVISION_DEPTH = 3;
		//! how much of a region's OWN thickness the soft edge may occupy. The
		//! feather is a constant visual weight sized off the whole shape's
		//! bounds, but a thin stroke ribbon or a tiny detail is far smaller than
		//! that: applying the full width would balloon it into a halo/streak far
		//! bigger than the shape. Capping the per-region feather at a fraction of
		//! that region's thickness (2*area/perimeter - orientation-independent,
		//! so it catches thin diagonal ribbons a bounding box misses) keeps small
		//! shapes crisp while big shapes still get the full soft edge.
		const float FEATHER_THICKNESS_FRACTION = 0.5f;

		float clamp01(float value)
		{
			return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
		}

		VectorTessellator::Colour lerpColour(
			VectorTessellator::Colour const & a,
			VectorTessellator::Colour const & b, float t)
		{
			return VectorTessellator::Colour(
				a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
				a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
		}

		VectorTessellator::Colour paintAt(
			VectorTessellator::Region const & region,
			VectorTessellator::Point const & point)
		{
			if(region.paintType == VectorTessellator::PAINT_SOLID ||
				region.gradientStops.empty())
			{
				return region.fill;
			}
			const float dx = region.gradientEnd.x - region.gradientStart.x;
			const float dy = region.gradientEnd.y - region.gradientStart.y;
			float t = 0.0f;
			if(region.paintType == VectorTessellator::PAINT_LINEAR_GRADIENT)
			{
				const float lengthSq = dx * dx + dy * dy;
				if(lengthSq > 1e-12f)
				{
					t = ((point.x - region.gradientStart.x) * dx +
						(point.y - region.gradientStart.y) * dy) / lengthSq;
				}
			}
			else
			{
				const float radius = std::sqrt(dx * dx + dy * dy);
				if(radius > 1e-6f)
				{
					// The radial ramp starts at a possibly off-centre focal
					// point and reaches 1 where that ray intersects the outer
					// circle. Solve the ray/circle quadratic, then invert its
					// intersection distance so the current point is the ramp t.
					const float rayX = point.x - region.gradientFocal.x;
					const float rayY = point.y - region.gradientFocal.y;
					const float rayLengthSq = rayX * rayX + rayY * rayY;
					if(rayLengthSq > 1e-12f)
					{
						const float focalX = region.gradientFocal.x -
							region.gradientStart.x;
						const float focalY = region.gradientFocal.y -
							region.gradientStart.y;
						const float b = 2.0f *
							(rayX * focalX + rayY * focalY);
						const float c = focalX * focalX + focalY * focalY -
							radius * radius;
						const float discriminant = b * b -
							4.0f * rayLengthSq * c;
						if(discriminant >= 0.0f)
						{
							const float root = (-b + std::sqrt(discriminant)) /
								(2.0f * rayLengthSq);
							if(root > 1e-6f)
								t = 1.0f / root;
						}
					}
				}
			}
			t = clamp01(t);
			std::vector<VectorTessellator::GradientStop> const & stops =
				region.gradientStops;
			if(t <= stops.front().offset)
			{
				return stops.front().colour;
			}
			for(std::size_t i = 0; i + 1 < stops.size(); ++i)
			{
				if(t <= stops[i + 1].offset)
				{
					const float span = stops[i + 1].offset - stops[i].offset;
					const float u = span > 1e-6f
						? (t - stops[i].offset) / span : 0.0f;
					return lerpColour(stops[i].colour, stops[i + 1].colour, u);
				}
			}
			return stops.back().colour;
		}

		void appendGradientTriangle(VectorTessellator::Region const & region,
			VectorTessellator::Point const & a,
			VectorTessellator::Point const & b,
			VectorTessellator::Point const & c, int depth,
			VectorTessellator::Mesh & out)
		{
			if(depth > 0)
			{
				const VectorTessellator::Point ab((a.x + b.x) * 0.5f,
					(a.y + b.y) * 0.5f);
				const VectorTessellator::Point bc((b.x + c.x) * 0.5f,
					(b.y + c.y) * 0.5f);
				const VectorTessellator::Point ca((c.x + a.x) * 0.5f,
					(c.y + a.y) * 0.5f);
				appendGradientTriangle(region, a, ab, ca, depth - 1, out);
				appendGradientTriangle(region, ab, b, bc, depth - 1, out);
				appendGradientTriangle(region, ca, bc, c, depth - 1, out);
				appendGradientTriangle(region, ab, bc, ca, depth - 1, out);
				return;
			}
			const unsigned int base = static_cast<unsigned int>(out.positions.size());
			out.positions.push_back(a); out.colours.push_back(paintAt(region, a));
			out.positions.push_back(b); out.colours.push_back(paintAt(region, b));
			out.positions.push_back(c); out.colours.push_back(paintAt(region, c));
			out.indices.push_back(base); out.indices.push_back(base + 1);
			out.indices.push_back(base + 2);
		}

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

		//! total edge length of a closed contour
		float contourPerimeter(std::vector<VectorTessellator::Point> const & c)
		{
			const std::size_t n = c.size();
			float total = 0.0f;
			for(std::size_t i = 0; i < n; ++i)
			{
				VectorTessellator::Point const & a = c[i];
				VectorTessellator::Point const & b = c[(i + 1) % n];
				const float dx = b.x - a.x;
				const float dy = b.y - a.y;
				total += std::sqrt(dx * dx + dy * dy);
			}
			return total;
		}

		//! the mean thickness of a filled contour: 2*area/perimeter. For a long
		//! thin ribbon this is its (small) width regardless of orientation, for a
		//! disc it is the radius - the right ceiling for a per-region feather so
		//! a thin/small shape is not swallowed by an edge sized for the whole rig.
		float contourThickness(std::vector<VectorTessellator::Point> const & c)
		{
			const float perimeter = contourPerimeter(c);
			if(perimeter <= 1e-6f)
			{
				return 0.0f;
			}
			return 2.0f * std::fabs(VectorTessellator::signedArea(c)) / perimeter;
		}

		//! append one alpha-ramp feather ring along a closed contour; the inner
		//! (on-contour) colour of each vertex comes from innerAt so a solid fill
		//! feathers in its flat colour while a gradient feathers in the gradient
		//! colour sampled AT that edge point (a white default fill would otherwise
		//! draw a bright halo around every gradient shape). The outer ring is the
		//! same rgb at alpha 0 (the soft fade). Shared by appendFeather and build.
		template <typename InnerColourFn>
		void featherRing(std::vector<VectorTessellator::Point> const & contour,
			float width, InnerColourFn innerAt, VectorTessellator::Mesh & out)
		{
			const std::size_t n = contour.size();
			if(n < 3 || width <= 0.0f)
			{
				return;
			}
			// outward normals point away from the interior; the winding sign
			// flips them so a clockwise contour feathers outward too
			const float windingSign =
				VectorTessellator::signedArea(contour) >= 0.0f ? 1.0f : -1.0f;
			const unsigned int base =
				static_cast<unsigned int>(out.positions.size());
			for(std::size_t i = 0; i < n; ++i)
			{
				VectorTessellator::Point const & prev = contour[(i + n - 1) % n];
				VectorTessellator::Point const & here = contour[i];
				VectorTessellator::Point const & next = contour[(i + 1) % n];
				// per-edge outward normal (for CCW: right of the travel direction)
				float pnx = (here.y - prev.y);
				float pny = -(here.x - prev.x);
				float nnx = (next.y - here.y);
				float nny = -(next.x - here.x);
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

				const VectorTessellator::Colour inner = innerAt(here);
				out.positions.push_back(here);
				out.colours.push_back(inner);
				out.positions.push_back(VectorTessellator::Point(
					here.x + nx * width, here.y + ny * width));
				out.colours.push_back(VectorTessellator::Colour(
					inner.r, inner.g, inner.b, 0.0f));
			}
			for(std::size_t i = 0; i < n; ++i)
			{
				const unsigned int innerHere =
					base + static_cast<unsigned int>(i * 2);
				const unsigned int outerHere = innerHere + 1;
				const std::size_t j = (i + 1) % n;
				const unsigned int innerNext =
					base + static_cast<unsigned int>(j * 2);
				const unsigned int outerNext = innerNext + 1;
				out.indices.push_back(innerHere);
				out.indices.push_back(outerHere);
				out.indices.push_back(outerNext);
				out.indices.push_back(innerHere);
				out.indices.push_back(outerNext);
				out.indices.push_back(innerNext);
			}
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
		std::vector<Point> flatPoints;
		for(std::vector<Point> const & ring : polygon)
		{
			for(Point const & point : ring)
			{
				flatPoints.push_back(point);
			}
		}
		if(region.paintType != PAINT_SOLID && !region.gradientStops.empty())
		{
			for(std::size_t index = 0; index < localIndices.size(); index += 3)
			{
				appendGradientTriangle(region, flatPoints[localIndices[index]],
					flatPoints[localIndices[index + 1]],
					flatPoints[localIndices[index + 2]],
					GRADIENT_SUBDIVISION_DEPTH, out);
			}
			return;
		}
		const unsigned int base = static_cast<unsigned int>(out.positions.size());
		for(Point const & point : flatPoints)
		{
			out.positions.push_back(point);
			out.colours.push_back(region.fill);
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
		featherRing(contour, width, [&fill](Point const &) { return fill; }, out);
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
				// clamp the shared edge width to this region's own thickness so
				// a thin ribbon / tiny detail is not swallowed by a halo sized
				// for the whole rig (@see FEATHER_THICKNESS_FRACTION)
				const float cap =
					contourThickness(region.outer) * FEATHER_THICKNESS_FRACTION;
				const float width =
					cap > 0.0f && cap < featherWidth ? cap : featherWidth;
				if(region.paintType != PAINT_SOLID &&
					!region.gradientStops.empty())
				{
					// a gradient shape feathers in its gradient colour at each
					// edge point, not the (unset, white) flat fill
					featherRing(region.outer, width,
						[&region](Point const & p) { return paintAt(region, p); },
						out);
				}
				else
				{
					featherRing(region.outer, width,
						[&region](Point const &) { return region.fill; }, out);
				}
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
