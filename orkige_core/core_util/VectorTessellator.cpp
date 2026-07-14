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
		//! steps of a round join/cap arc - a fixed count keeps a stroke's
		//! triangle count constant while its centreline animates
		const int STROKE_ARC_STEPS = 8;
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

		//--- stroke sweep ------------------------------------------
		// A stroke is swept as independently CONVEX pieces (@see appendStroke):
		// no triangulator is involved, so a corner tighter than the half width -
		// which makes an offset OUTLINE self-intersect, and a self-intersecting
		// polygon makes earcut emit garbage - simply produces overlapping pieces,
		// which for an opaque stroke is exact.

		typedef VectorTessellator::Point TessPoint;
		typedef VectorTessellator::Colour TessColour;

		TessPoint pointAdd(TessPoint const & a, TessPoint const & b, float scale)
		{
			return TessPoint(a.x + b.x * scale, a.y + b.y * scale);
		}
		//! normalize in place; a zero vector stays zero
		TessPoint normalized(float x, float y)
		{
			const float length = std::sqrt(x * x + y * y);
			if(length <= 1e-9f)
			{
				return TessPoint(0.0f, 0.0f);
			}
			return TessPoint(x / length, y / length);
		}

		//! the prepared sweep frame of one stroke centreline
		struct StrokeFrame
		{
			std::vector<TessPoint>	work;		//!< centreline (a closed one drops its repeated end point)
			std::vector<TessPoint>	direction;	//!< unit travel direction per segment
			std::vector<TessPoint>	normal;		//!< unit LEFT normal per segment
			std::size_t				segments;	//!< direction/normal count
			bool					closed;
			float					half;		//!< half the stroke width
			StrokeFrame() : segments(0), closed(false), half(0.0f) {}
		};

		//! @brief prepare a stroke region for sweeping. A zero-length segment
		//! inherits the previous direction (a duplicated authored point can not
		//! spin the frame); the first one falls back to +x.
		bool buildStrokeFrame(VectorTessellator::Region const & region,
			StrokeFrame & frame)
		{
			frame.work.clear();
			frame.direction.clear();
			frame.normal.clear();
			frame.segments = 0;
			frame.closed = region.strokeClosed;
			frame.half = region.strokeWidth * 0.5f;
			if(region.kind != VectorTessellator::REGION_STROKE ||
				frame.half <= 0.0f || region.outer.size() < 2)
			{
				return false;
			}
			frame.work = region.outer;
			if(frame.closed && frame.work.size() > 2)
			{
				TessPoint const & first = frame.work.front();
				TessPoint const & last = frame.work.back();
				if(std::fabs(first.x - last.x) <= 1e-6f &&
					std::fabs(first.y - last.y) <= 1e-6f)
				{
					frame.work.pop_back();	// the closing point is implicit
				}
			}
			const std::size_t n = frame.work.size();
			if(n < 2)
			{
				return false;
			}
			frame.segments = frame.closed ? n : n - 1;
			TessPoint previous(1.0f, 0.0f);
			for(std::size_t s = 0; s < frame.segments; ++s)
			{
				TessPoint const & a = frame.work[s];
				TessPoint const & b = frame.work[(s + 1) % n];
				TessPoint step = normalized(b.x - a.x, b.y - a.y);
				if(step.x == 0.0f && step.y == 0.0f)
				{
					step = previous;	// degenerate segment: keep the frame stable
				}
				previous = step;
				frame.direction.push_back(step);
				frame.normal.push_back(TessPoint(-step.y, step.x));
			}
			return frame.segments >= 1;
		}

		//! @brief the limited miter point of a corner on one side: the offset
		//! edges' crossing, pulled back to a bevel-like position once the miter
		//! grows past miterLimit half-widths (the spike guard)
		TessPoint miterPoint(TessPoint const & corner, TessPoint const & previous,
			TessPoint const & next, float side, float half, float miterLimit)
		{
			TessPoint bisector = normalized(previous.x + next.x,
				previous.y + next.y);
			float factor = half;
			if(bisector.x != 0.0f || bisector.y != 0.0f)
			{
				const float cosine = std::fabs(bisector.x * next.x +
					bisector.y * next.y);
				const float limit = miterLimit > 1.0f ? miterLimit : 1.0f;
				factor = half / (cosine > 1e-3f ? cosine : 1e-3f);
				if(factor > half * limit)
				{
					factor = half * limit;
				}
			}
			else
			{
				bisector = next;	// a full reversal: fall back to the next edge
			}
			return TessPoint(corner.x + side * bisector.x * factor,
				corner.y + side * bisector.y * factor);
		}

		//! clip a convex polygon (with per-vertex colours) against a convex mask;
		//! the colour of an introduced vertex interpolates along the cut edge
		void clipConvex(std::vector<TessPoint> & points,
			std::vector<TessColour> & colours,
			std::vector<TessPoint> const & mask)
		{
			if(mask.size() < 3 || points.size() < 3)
			{
				if(mask.size() >= 3)
				{
					points.clear();
					colours.clear();
				}
				return;
			}
			const float sign =
				VectorTessellator::signedArea(mask) >= 0.0f ? 1.0f : -1.0f;
			std::vector<TessPoint> nextPoints;
			std::vector<TessColour> nextColours;
			for(std::size_t edge = 0; edge < mask.size(); ++edge)
			{
				TessPoint const & a = mask[edge];
				TessPoint const & b = mask[(edge + 1) % mask.size()];
				nextPoints.clear();
				nextColours.clear();
				if(points.empty())
				{
					break;
				}
				auto inside = [&](TessPoint const & p)
				{
					return sign * ((b.x - a.x) * (p.y - a.y) -
						(b.y - a.y) * (p.x - a.x)) >= -1e-9f;
				};
				auto cut = [&](std::size_t from, std::size_t to)
				{
					TessPoint const & p = points[from];
					TessPoint const & q = points[to];
					const float rx = q.x - p.x;
					const float ry = q.y - p.y;
					const float sx = b.x - a.x;
					const float sy = b.y - a.y;
					const float denominator = rx * sy - ry * sx;
					float t = 0.0f;
					if(std::fabs(denominator) > 1e-12f)
					{
						t = ((a.x - p.x) * sy - (a.y - p.y) * sx) / denominator;
						t = clamp01(t);
					}
					nextPoints.push_back(TessPoint(p.x + t * rx, p.y + t * ry));
					nextColours.push_back(lerpColour(colours[from],
						colours[to], t));
				};
				std::size_t previous = points.size() - 1;
				bool previousInside = inside(points[previous]);
				for(std::size_t current = 0; current < points.size(); ++current)
				{
					const bool currentInside = inside(points[current]);
					if(currentInside)
					{
						if(!previousInside)
						{
							cut(previous, current);
						}
						nextPoints.push_back(points[current]);
						nextColours.push_back(colours[current]);
					}
					else if(previousInside)
					{
						cut(previous, current);
					}
					previous = current;
					previousInside = currentInside;
				}
				points.swap(nextPoints);
				colours.swap(nextColours);
			}
		}

		//! @brief emit ONE convex piece (optionally clipped by the region's
		//! convex mask) as a triangle fan - the only geometry path a stroke uses
		void emitConvexPiece(VectorTessellator::Region const & region,
			std::vector<TessPoint> & points, std::vector<TessColour> & colours,
			VectorTessellator::Mesh & out)
		{
			if(!region.mask.empty())
			{
				clipConvex(points, colours, region.mask);
			}
			if(points.size() < 3)
			{
				return;
			}
			const unsigned int base =
				static_cast<unsigned int>(out.positions.size());
			for(std::size_t i = 0; i < points.size(); ++i)
			{
				out.positions.push_back(points[i]);
				out.colours.push_back(colours[i]);
			}
			for(std::size_t i = 1; i + 1 < points.size(); ++i)
			{
				out.indices.push_back(base);
				out.indices.push_back(base + static_cast<unsigned int>(i));
				out.indices.push_back(base + static_cast<unsigned int>(i + 1));
			}
		}

		//! emit a convex piece painted with the region's paint at every vertex
		void emitPaintedPiece(VectorTessellator::Region const & region,
			std::vector<TessPoint> & points, VectorTessellator::Mesh & out)
		{
			std::vector<TessColour> colours;
			colours.reserve(points.size());
			for(TessPoint const & point : points)
			{
				colours.push_back(paintAt(region, point));
			}
			emitConvexPiece(region, points, colours, out);
		}

		//! one point of the ribbon's outline, with the direction its soft edge
		//! extrudes (radial at an arc, the corner bisector at a cap corner)
		struct RimPoint
		{
			TessPoint	point;
			TessPoint	outward;
			RimPoint() {}
			RimPoint(TessPoint const & p, TessPoint const & o)
				: point(p), outward(o) {}
		};

		//! @brief append the boundary points of ONE corner on ONE side of the
		//! ribbon. The OUTER side of the turn gets the join geometry (a limited
		//! miter, a bevel edge or an arc), the inner side the single miter point
		//! - the two segment quads already cover the inside. forceOuter keeps a
		//! join wedge's point COUNT constant even when a frame happens to
		//! straighten the corner (a degenerate wedge, not a missing one).
		void appendCornerRim(StrokeFrame const & frame,
			VectorTessellator::Region const & region, std::size_t index,
			float side, bool forceOuter, std::vector<RimPoint> & chain)
		{
			TessPoint const & corner = frame.work[index];
			const std::size_t previousSegment =
				(index + frame.segments - 1) % frame.segments;
			const std::size_t nextSegment = index % frame.segments;
			TessPoint const & previousNormal = frame.normal[previousSegment];
			TessPoint const & nextNormal = frame.normal[nextSegment];
			TessPoint const & previousDirection = frame.direction[previousSegment];
			TessPoint const & nextDirection = frame.direction[nextSegment];
			const float turn = previousDirection.x * nextDirection.y -
				previousDirection.y * nextDirection.x;
			const bool outer = forceOuter || turn * side < -1e-9f;
			const TessPoint first(corner.x + side * previousNormal.x * frame.half,
				corner.y + side * previousNormal.y * frame.half);
			const TessPoint last(corner.x + side * nextNormal.x * frame.half,
				corner.y + side * nextNormal.y * frame.half);
			if(!outer)
			{
				// the inside of a corner takes the single (limited) miter point:
				// the two segment quads already overlap there
				const TessPoint m = miterPoint(corner, previousNormal, nextNormal,
					side, frame.half, region.strokeMiterLimit);
				chain.push_back(RimPoint(m,
					normalized(m.x - corner.x, m.y - corner.y)));
				return;
			}
			if(region.strokeJoin != VectorTessellator::JOIN_ROUND)
			{
				// miter: the two offset edges meet at the (limited) miter point;
				// bevel: they are joined straight across
				chain.push_back(RimPoint(first, normalized(
					first.x - corner.x, first.y - corner.y)));
				if(region.strokeJoin == VectorTessellator::JOIN_MITER)
				{
					const TessPoint m = miterPoint(corner, previousNormal,
						nextNormal, side, frame.half, region.strokeMiterLimit);
					chain.push_back(RimPoint(m,
						normalized(m.x - corner.x, m.y - corner.y)));
				}
				chain.push_back(RimPoint(last, normalized(
					last.x - corner.x, last.y - corner.y)));
				return;
			}
			// round: a fixed-step arc between the two offset points
			float startAngle = std::atan2(first.y - corner.y, first.x - corner.x);
			float endAngle = std::atan2(last.y - corner.y, last.x - corner.x);
			const float twoPi = 6.2831853071795864f;
			if(turn > 0.0f)
			{
				while(endAngle < startAngle) { endAngle += twoPi; }
			}
			else
			{
				while(endAngle > startAngle) { endAngle -= twoPi; }
			}
			for(int step = 0; step <= STROKE_ARC_STEPS; ++step)
			{
				const float angle = startAngle + (endAngle - startAngle) *
					static_cast<float>(step) /
					static_cast<float>(STROKE_ARC_STEPS);
				const TessPoint radial(std::cos(angle), std::sin(angle));
				chain.push_back(RimPoint(pointAdd(corner, radial, frame.half),
					radial));
			}
		}

		//! @brief the ribbon outline of a stroke: one closed loop for an open
		//! centreline (both sides + the two caps), two for a closed one (the
		//! outer and the inner boundary). Feather-only: the FILL never sees it.
		void strokeRimLoops(StrokeFrame const & frame,
			VectorTessellator::Region const & region,
			std::vector<std::vector<RimPoint> > & loops)
		{
			loops.clear();
			const std::size_t n = frame.work.size();
			std::vector<RimPoint> left;
			std::vector<RimPoint> right;
			const std::size_t firstCorner = frame.closed ? 0 : 1;
			const std::size_t lastCorner = frame.closed ? n : n - 1;
			if(!frame.closed)
			{
				// the terminal offset points; their outward direction is fixed up
				// with the cap below
				left.push_back(RimPoint(pointAdd(frame.work[0], frame.normal[0],
					frame.half), frame.normal[0]));
				right.push_back(RimPoint(pointAdd(frame.work[0],
					frame.normal[0], -frame.half),
					TessPoint(-frame.normal[0].x, -frame.normal[0].y)));
			}
			for(std::size_t index = firstCorner; index < lastCorner; ++index)
			{
				appendCornerRim(frame, region, index, 1.0f, false, left);
				appendCornerRim(frame, region, index, -1.0f, false, right);
			}
			if(frame.closed)
			{
				loops.push_back(left);
				loops.push_back(right);
				return;
			}
			TessPoint const & endNormal = frame.normal[frame.segments - 1];
			left.push_back(RimPoint(pointAdd(frame.work[n - 1], endNormal,
				frame.half), endNormal));
			right.push_back(RimPoint(pointAdd(frame.work[n - 1], endNormal,
				-frame.half), TessPoint(-endNormal.x, -endNormal.y)));

			const TessPoint startOut(-frame.direction[0].x,
				-frame.direction[0].y);
			const TessPoint endOut = frame.direction[frame.segments - 1];
			std::vector<RimPoint> loop;
			if(region.strokeCap == VectorTessellator::CAP_SQUARE)
			{
				// the ribbon projects half a width past each end; the corner's
				// soft edge extrudes along the corner bisector
				left.front().point = pointAdd(left.front().point, startOut,
					frame.half);
				right.front().point = pointAdd(right.front().point, startOut,
					frame.half);
				left.back().point = pointAdd(left.back().point, endOut,
					frame.half);
				right.back().point = pointAdd(right.back().point, endOut,
					frame.half);
			}
			if(region.strokeCap != VectorTessellator::CAP_ROUND)
			{
				left.front().outward = normalized(
					left.front().outward.x + startOut.x,
					left.front().outward.y + startOut.y);
				right.front().outward = normalized(
					right.front().outward.x + startOut.x,
					right.front().outward.y + startOut.y);
				left.back().outward = normalized(
					left.back().outward.x + endOut.x,
					left.back().outward.y + endOut.y);
				right.back().outward = normalized(
					right.back().outward.x + endOut.x,
					right.back().outward.y + endOut.y);
			}
			// walk the outline: down the left side, around the end cap, back up
			// the right side, around the start cap
			loop.insert(loop.end(), left.begin(), left.end());
			if(region.strokeCap == VectorTessellator::CAP_ROUND)
			{
				const float angle = std::atan2(endOut.y, endOut.x);
				const float halfPi = 1.5707963267948966f;
				const float pi = 3.14159265358979f;
				for(int step = 1; step < STROKE_ARC_STEPS; ++step)
				{
					const float a = angle + halfPi - pi *
						static_cast<float>(step) /
						static_cast<float>(STROKE_ARC_STEPS);
					const TessPoint radial(std::cos(a), std::sin(a));
					loop.push_back(RimPoint(pointAdd(frame.work[n - 1], radial,
						frame.half), radial));
				}
			}
			for(std::size_t i = right.size(); i > 0; --i)
			{
				loop.push_back(right[i - 1]);
			}
			if(region.strokeCap == VectorTessellator::CAP_ROUND)
			{
				const float angle = std::atan2(startOut.y, startOut.x);
				const float halfPi = 1.5707963267948966f;
				const float pi = 3.14159265358979f;
				for(int step = 1; step < STROKE_ARC_STEPS; ++step)
				{
					const float a = angle + halfPi - pi *
						static_cast<float>(step) /
						static_cast<float>(STROKE_ARC_STEPS);
					const TessPoint radial(std::cos(a), std::sin(a));
					loop.push_back(RimPoint(pointAdd(frame.work[0], radial,
						frame.half), radial));
				}
			}
			loops.push_back(loop);
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
		if(region.kind != REGION_FILL || region.outer.size() < 3)
		{
			return;	// not a fillable area (a stroke sweeps, @see appendStroke)
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
	void VectorTessellator::appendStroke(Region const & region, Mesh & out)
	{
		StrokeFrame frame;
		if(!buildStrokeFrame(region, frame))
		{
			return;
		}
		const std::size_t n = frame.work.size();
		std::vector<Point> piece;

		// one quad per segment: the ribbon between the two offset edges
		for(std::size_t s = 0; s < frame.segments; ++s)
		{
			Point const & a = frame.work[s];
			Point const & b = frame.work[(s + 1) % n];
			Point const & normal = frame.normal[s];
			piece.clear();
			piece.push_back(pointAdd(a, normal, frame.half));
			piece.push_back(pointAdd(b, normal, frame.half));
			piece.push_back(pointAdd(b, normal, -frame.half));
			piece.push_back(pointAdd(a, normal, -frame.half));
			emitPaintedPiece(region, piece, out);
		}

		// one wedge per interior corner, on the OUTER side of the turn (the
		// inner side is already covered by the two overlapping segment quads)
		const std::size_t firstCorner = frame.closed ? 0 : 1;
		const std::size_t lastCorner = frame.closed ? n : n - 1;
		for(std::size_t index = firstCorner; index < lastCorner; ++index)
		{
			Point const & corner = frame.work[index];
			const std::size_t previousSegment =
				(index + frame.segments - 1) % frame.segments;
			const std::size_t nextSegment = index % frame.segments;
			Point const & previousDirection = frame.direction[previousSegment];
			Point const & nextDirection = frame.direction[nextSegment];
			const float turn = previousDirection.x * nextDirection.y -
				previousDirection.y * nextDirection.x;
			// the outer side of the corner is the side the path turns away from
			const float side = turn > 0.0f ? -1.0f : 1.0f;
			piece.clear();
			piece.push_back(corner);
			std::vector<RimPoint> rim;
			appendCornerRim(frame, region, index, side, true, rim);
			for(RimPoint const & each : rim)
			{
				piece.push_back(each.point);
			}
			emitPaintedPiece(region, piece, out);
		}
		if(frame.closed || region.strokeCap == CAP_BUTT)
		{
			return;		// a closed ribbon has no ends; a butt end adds nothing
		}
		// the two open ends
		const Point starts[2] = { frame.work[0], frame.work[n - 1] };
		const Point normals[2] = { frame.normal[0],
			frame.normal[frame.segments - 1] };
		const Point outs[2] = {
			Point(-frame.direction[0].x, -frame.direction[0].y),
			frame.direction[frame.segments - 1] };
		for(int end = 0; end < 2; ++end)
		{
			const Point centre = starts[end];
			const Point normal = normals[end];
			const Point outward = outs[end];
			piece.clear();
			if(region.strokeCap == CAP_SQUARE)
			{
				const Point a = pointAdd(centre, normal, frame.half);
				const Point b = pointAdd(centre, normal, -frame.half);
				piece.push_back(a);
				piece.push_back(pointAdd(a, outward, frame.half));
				piece.push_back(pointAdd(b, outward, frame.half));
				piece.push_back(b);
			}
			else	// round: a half-disc fan of fixed step count
			{
				const float angle = std::atan2(outward.y, outward.x);
				const float halfPi = 1.5707963267948966f;
				const float pi = 3.14159265358979f;
				piece.push_back(centre);
				for(int step = 0; step <= STROKE_ARC_STEPS; ++step)
				{
					const float a = angle - halfPi + pi *
						static_cast<float>(step) /
						static_cast<float>(STROKE_ARC_STEPS);
					piece.push_back(pointAdd(centre,
						Point(std::cos(a), std::sin(a)), frame.half));
				}
			}
			emitPaintedPiece(region, piece, out);
		}
	}
	//---------------------------------------------------------
	void VectorTessellator::appendStrokeFeather(Region const & region,
		float width, Mesh & out)
	{
		StrokeFrame frame;
		if(width <= 0.0f || !buildStrokeFrame(region, frame))
		{
			return;
		}
		std::vector<std::vector<RimPoint> > loops;
		strokeRimLoops(frame, region, loops);
		std::vector<Point> quad;
		std::vector<Colour> colours;
		for(std::vector<RimPoint> const & loop : loops)
		{
			const std::size_t count = loop.size();
			if(count < 2)
			{
				continue;
			}
			// one alpha-ramp quad per outline edge: the inner edge sits ON the
			// ribbon in the paint colour, the outer edge is the same colour at
			// alpha 0, extruded along each point's own outward direction
			for(std::size_t i = 0; i < count; ++i)
			{
				RimPoint const & here = loop[i];
				RimPoint const & next = loop[(i + 1) % count];
				const Colour innerHere = paintAt(region, here.point);
				const Colour innerNext = paintAt(region, next.point);
				quad.clear();
				colours.clear();
				quad.push_back(here.point);
				colours.push_back(innerHere);
				quad.push_back(pointAdd(here.point, here.outward, width));
				colours.push_back(Colour(innerHere.r, innerHere.g, innerHere.b,
					0.0f));
				quad.push_back(pointAdd(next.point, next.outward, width));
				colours.push_back(Colour(innerNext.r, innerNext.g, innerNext.b,
					0.0f));
				quad.push_back(next.point);
				colours.push_back(innerNext);
				emitConvexPiece(region, quad, colours, out);
			}
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
			// paint the region's body, then IMMEDIATELY its feather rim: the
			// soft edge belongs to the region it wraps, so a region painted
			// later occludes body and rim together. (Feathering every region
			// after every body would redraw each region's outline above ALL
			// the geometry painted over it - the contour of a shape a later
			// region legitimately hides would bleed through as a stray line.)
			if(region.kind == REGION_STROKE)
			{
				appendStroke(region, out);
			}
			else
			{
				triangulateFill(region, out);
			}
			if(featherWidth <= 0.0f)
			{
				continue;
			}
			// clamp the shared edge width to this region's own thickness so
			// a thin ribbon / tiny detail is not swallowed by a halo sized
			// for the whole rig (@see FEATHER_THICKNESS_FRACTION). A stroke's
			// thickness IS its width.
			const float cap = (region.kind == REGION_STROKE
				? region.strokeWidth
				: contourThickness(region.outer)) *
				FEATHER_THICKNESS_FRACTION;
			const float width =
				cap > 0.0f && cap < featherWidth ? cap : featherWidth;
			if(region.kind == REGION_STROKE)
			{
				appendStrokeFeather(region, width, out);
			}
			else if(region.paintType != PAINT_SOLID &&
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
		out.bounds = computeBounds(regions);
	}
	//---------------------------------------------------------
	VectorTessellator::Bounds VectorTessellator::computeBounds(
		std::vector<Region> const & regions)
	{
		Bounds bounds;
		for(Region const & region : regions)
		{
			// a stroke paints half a width either side of its centreline
			const float grow = region.kind == REGION_STROKE
				? region.strokeWidth * 0.5f : 0.0f;
			for(Point const & point : region.outer)
			{
				if(!bounds.valid)
				{
					bounds.minX = point.x - grow;
					bounds.maxX = point.x + grow;
					bounds.minY = point.y - grow;
					bounds.maxY = point.y + grow;
					bounds.valid = true;
					continue;
				}
				bounds.minX = point.x - grow < bounds.minX
					? point.x - grow : bounds.minX;
				bounds.minY = point.y - grow < bounds.minY
					? point.y - grow : bounds.minY;
				bounds.maxX = point.x + grow > bounds.maxX
					? point.x + grow : bounds.maxX;
				bounds.maxY = point.y + grow > bounds.maxY
					? point.y + grow : bounds.maxY;
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
