/**************************************************************
	created:	2026/07/29 at 21:00
	filename: 	VectorShapeCook.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file VectorShapeCook.cpp
//! @brief the source-independent half of the `.oshape` cook
//! (@see VectorShapeCook.h)

#include "core_util/VectorShapeCook.h"

#include <algorithm>
#include <sstream>

namespace Orkige
{
	namespace
	{
		//! every loop a region owns, in one walk (bounds must cover the whole
		//! region, and placement must move every vertex it carries)
		template <typename Fn>
		void forEachLoop(VectorTessellator::Region & region, Fn apply)
		{
			apply(region.outer);
			for(std::vector<VectorTessellator::Point> & hole : region.holes)
			{
				apply(hole);
			}
			apply(region.mask);
		}

		//! the structure signature a morph set has to agree on: the vertex count
		//! of every region's outer contour, in order
		String topologySignature(
			std::vector<VectorTessellator::Region> const & regions)
		{
			std::ostringstream out;
			out << "[";
			for(std::size_t i = 0; i < regions.size(); ++i)
			{
				out << (i == 0 ? "" : ", ") << regions[i].outer.size();
			}
			out << "]";
			return out.str();
		}
	}
	//---------------------------------------------------------
	double VectorShapeCook::resolveTolerance(Options const & options,
		double documentWidth, double documentHeight)
	{
		if(options.tolerance > 0.0)
		{
			return options.tolerance;
		}
		const double side = std::max(documentWidth, documentHeight);
		if(!(side > 0.0))
		{
			// a document that declares no size still has to flatten SOMEHOW;
			// the bare fraction is a small, finite tolerance
			return DEFAULT_TOLERANCE_FRACTION;
		}
		return DEFAULT_TOLERANCE_FRACTION * side;
	}
	//---------------------------------------------------------
	void VectorShapeCook::flattenCubicUniform(Point const & p0,
		Point const & p1, Point const & p2, Point const & p3, int segments,
		std::vector<Point> & out)
	{
		const int steps = segments > 0 ? segments : 1;
		for(int k = 1; k <= steps; ++k)
		{
			const double t = double(k) / double(steps);
			const double mt = 1.0 - t;
			const double a = mt * mt * mt;
			const double b = 3.0 * mt * mt * t;
			const double c = 3.0 * mt * t * t;
			const double d = t * t * t;
			out.push_back(Point(
				float(a * double(p0.x) + b * double(p1.x) +
					c * double(p2.x) + d * double(p3.x)),
				float(a * double(p0.y) + b * double(p1.y) +
					c * double(p2.y) + d * double(p3.y))));
		}
	}
	//---------------------------------------------------------
	void VectorShapeCook::flattenQuadraticUniform(Point const & p0,
		Point const & p1, Point const & p2, int segments,
		std::vector<Point> & out)
	{
		// degree elevation: a quadratic is the cubic with these controls
		const Point c1(float((double(p0.x) + 2.0 * double(p1.x)) / 3.0),
			float((double(p0.y) + 2.0 * double(p1.y)) / 3.0));
		const Point c2(float((double(p2.x) + 2.0 * double(p1.x)) / 3.0),
			float((double(p2.y) + 2.0 * double(p1.y)) / 3.0));
		VectorShapeCook::flattenCubicUniform(p0, c1, c2, p2, segments, out);
	}
	//---------------------------------------------------------
	bool VectorShapeCook::isStraightCubic(Point const & p0, Point const & p1,
		Point const & p2, Point const & p3)
	{
		// a control point's squared distance from the p0..p3 chord, measured
		// against a tolerance PROPORTIONAL to the chord (so the verdict does not
		// change when the same artwork is authored at another scale)
		const double dx = double(p3.x) - double(p0.x);
		const double dy = double(p3.y) - double(p0.y);
		const double lengthSq = dx * dx + dy * dy;
		// one part in ten thousand of the chord: far below anything a curve
		// bends by, far above the rounding of a thirds-of-the-chord control
		const double relative = 1.0e-4;
		const double limitSq = lengthSq > 0.0
			? (relative * relative * lengthSq * lengthSq)
			: 0.0;
		Point const * const controls[2] = { &p1, &p2 };
		for(Point const * control : controls)
		{
			const double cx = double(control->x) - double(p0.x);
			const double cy = double(control->y) - double(p0.y);
			if(lengthSq <= 0.0)
			{
				// a degenerate chord: the controls must sit ON the point
				if(cx * cx + cy * cy > 0.0)
				{
					return false;
				}
				continue;
			}
			// (cross product)^2 vs (relative * length)^2 * length^2, so both
			// sides carry length^2 and no square root is needed
			const double cross = cx * dy - cy * dx;
			if(cross * cross > limitSq)
			{
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool VectorShapeCook::containsPoint(std::vector<Point> const & contour,
		Point const & point)
	{
		// even-odd crossing count of a ray cast along +x
		const std::size_t count = contour.size();
		if(count < 3)
		{
			return false;
		}
		bool inside = false;
		for(std::size_t i = 0, j = count - 1; i < count; j = i++)
		{
			Point const & a = contour[i];
			Point const & b = contour[j];
			if((a.y > point.y) != (b.y > point.y))
			{
				const double t = double(point.y - a.y) / double(b.y - a.y);
				const double x = double(a.x) + t * (double(b.x) - double(a.x));
				if(double(point.x) < x)
				{
					inside = !inside;
				}
			}
		}
		return inside;
	}
	//---------------------------------------------------------
	bool VectorShapeCook::checkTopology(std::vector<Region> const & base,
		std::vector<Region> const & pose, String const & poseName,
		String * error)
	{
		bool match = base.size() == pose.size();
		for(std::size_t i = 0; match && i < base.size(); ++i)
		{
			match = base[i].outer.size() == pose[i].outer.size();
		}
		if(match)
		{
			return true;
		}
		if(error)
		{
			std::ostringstream out;
			out << "morph target '"
				<< (poseName.empty() ? String("target") : poseName)
				<< "' structure " << topologySignature(pose)
				<< " does not match the base " << topologySignature(base)
				<< " - every pose must have the same contours with the same "
					"vertex counts (author matching paths; the cook flattens "
					"curves at a fixed resolution)";
			*error = out.str();
		}
		return false;
	}
	//---------------------------------------------------------
	bool VectorShapeCook::place(VectorShapeAsset::ParsedShape & shape,
		double extent)
	{
		// measure the BASE pose only: every target rides the base's transform so
		// corresponding vertices land in the same world neighbourhood
		double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
		bool any = false;
		for(Region & region : shape.base)
		{
			forEachLoop(region, [&](std::vector<Point> const & loop)
			{
				for(Point const & point : loop)
				{
					if(!any)
					{
						minX = maxX = double(point.x);
						minY = maxY = double(point.y);
						any = true;
						continue;
					}
					minX = std::min(minX, double(point.x));
					maxX = std::max(maxX, double(point.x));
					minY = std::min(minY, double(point.y));
					maxY = std::max(maxY, double(point.y));
				}
			});
		}
		if(!any)
		{
			return false;
		}
		const double span = std::max(std::max(maxX - minX, maxY - minY), 1e-6);
		const double scale = extent / span;
		const double centreX = (minX + maxX) * 0.5;
		const double centreY = (minY + maxY) * 0.5;
		// center, scale to world units and flip y (drawing spaces are y-down,
		// the engine is y-up). The arithmetic runs in double so a cook is
		// deterministic to the last printed decimal.
		auto placeLoop = [&](std::vector<Point> & loop)
		{
			for(Point & point : loop)
			{
				point.x = float((double(point.x) - centreX) * scale);
				point.y = float(-(double(point.y) - centreY) * scale);
			}
		};
		for(Region & region : shape.base)
		{
			forEachLoop(region, placeLoop);
		}
		for(VectorShapeAsset::MorphTarget & morph : shape.morphs)
		{
			for(Region & region : morph.regions)
			{
				forEachLoop(region, placeLoop);
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool VectorShapeCook::emit(VectorShapeAsset::ParsedShape & shape,
		double extent, String const & headerComment, String & outText)
	{
		if(!VectorShapeCook::place(shape, extent))
		{
			return false;
		}
		outText = VectorShapeAsset::serialize(shape, headerComment);
		return true;
	}
}
