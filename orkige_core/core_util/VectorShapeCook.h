/********************************************************************
	created:	Wednesday 2026/07/29 at 21:00
	filename: 	VectorShapeCook.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorShapeCook_h__29_7_2026__21_00_00__
#define __VectorShapeCook_h__29_7_2026__21_00_00__

//! @file VectorShapeCook.h
//! @brief the vector-shape cook's pure tail: place flattened drawing contours
//! into world units and write them as `.oshape` text
//! @remarks A cook has two halves. The FRONT half reads a drawing format and
//! hands back flattened contours in the drawing's own coordinate space (y-down,
//! arbitrary units) - that is format-specific and lives with the reader
//! (engine_gui/SvgShapeCook for `.svg`). The TAIL is the same for every source:
//! measure the drawing, center it on the origin, scale its larger extent to a
//! world size, flip y (a drawing space is y-down, the engine is y-up) and
//! serialise. That tail is pure geometry + text, so it lives here in
//! orkige_core beside the parser it feeds and is unit-tested headlessly.
//!
//! Also here: the two decisions a cook needs and nothing else does - the
//! FIXED-subdivision flatten a MORPH SET requires (adaptive subdivision cannot
//! promise that two poses flatten to the same vertex count, and morph blending
//! needs corresponding vertices) and the topology agreement check that refuses
//! a mismatched pose with a message naming both structures.
//!
//! The runtime never calls any of this: a shipped `.oshape` is already cooked.

#include "core_util/VectorShapeAsset.h"
#include "core_util/VectorTessellator.h"
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief the source-independent half of the `.oshape` cook (pure, headless)
	class VectorShapeCook
	{
	public:
		typedef VectorTessellator::Point Point;
		typedef VectorTessellator::Region Region;

		//! segments per curve in UNIFORM mode. A morph set flattens every pose
		//! at this FIXED resolution so identical path structures yield
		//! identical vertex counts and corresponding control points.
		static const int UNIFORM_CURVE_SEGMENTS = 10;
		//! the default flatten chord tolerance as a fraction of the drawing's
		//! larger side. Expressing it RELATIVELY makes the cook
		//! resolution-independent: the same artwork authored at 100 or at 1000
		//! units flattens to the same contours, which an absolute tolerance
		//! cannot promise.
		static constexpr double DEFAULT_TOLERANCE_FRACTION = 0.01;

		//! what a cook is asked for
		struct Options
		{
			//! world units the drawing's LARGER extent spans
			double	extent = 2.0;
			//! absolute flatten chord tolerance in the drawing's own units;
			//! <= 0 derives it from DEFAULT_TOLERANCE_FRACTION
			double	tolerance = 0.0;
			//! flatten curves at UNIFORM_CURVE_SEGMENTS instead of adaptively
			//! (what a morph set needs - @see UNIFORM_CURVE_SEGMENTS)
			bool	uniform = false;
		};

		//! @brief the chord tolerance a cook flattens with: an explicit
		//! Options::tolerance verbatim, else DEFAULT_TOLERANCE_FRACTION of the
		//! drawing's larger side. A degenerate document (no size) falls back to
		//! the fraction itself, so the flatten never runs at tolerance 0.
		static double resolveTolerance(Options const & options,
			double documentWidth, double documentHeight);

		//! @brief fixed-subdivision cubic Bezier flatten: `segments` evenly
		//! spaced points appended EXCLUDING p0 (the same chaining contract as
		//! VectorTessellator::flattenCubic, whose ADAPTIVE subdivision this
		//! replaces wherever a predictable vertex count matters)
		static void flattenCubicUniform(Point const & p0, Point const & p1,
			Point const & p2, Point const & p3, int segments,
			std::vector<Point> & out);
		//! @brief fixed-subdivision quadratic flatten (elevated to a cubic,
		//! then flattenCubicUniform); same append contract
		static void flattenQuadraticUniform(Point const & p0, Point const & p1,
			Point const & p2, int segments, std::vector<Point> & out);

		//! @brief is this cubic a straight segment in disguise - both inner
		//! control points on the p0..p3 chord to within a relative epsilon?
		//! @remarks Drawing formats express a LINE as a cubic whose controls sit
		//! at the chord's thirds, so a cook sees no line commands at all. An
		//! adaptive flatten discovers that by itself (a flat cubic emits just its
		//! endpoint), but a FIXED-count flatten would subdivide a straight edge
		//! into UNIFORM_CURVE_SEGMENTS points - inflating a morph set's vertex
		//! count and making a rectangle's topology depend on how it was written.
		//! Asking first keeps both modes emitting one point per straight edge.
		static bool isStraightCubic(Point const & p0, Point const & p1,
			Point const & p2, Point const & p3);

		//! @brief even-odd point-in-polygon test against a closed contour (the
		//! hole test: a subpath whose first vertex lies inside an earlier
		//! subpath is that region's inner loop, not a second filled region)
		static bool containsPoint(std::vector<Point> const & contour,
			Point const & point);

		//! @brief do two poses share a blendable structure - the same number of
		//! regions with the same vertex count in each? On a mismatch this
		//! returns false and (when `error` is given) writes ONE message naming
		//! the pose, both structures and what the author has to change.
		static bool checkTopology(std::vector<Region> const & base,
			std::vector<Region> const & pose, String const & poseName,
			String * error);

		//! @brief place a drawing into world units IN PLACE: measure the BASE
		//! pose's bounds, center it on the origin, scale so its larger extent
		//! spans `extent` world units and flip y. Every morph target is placed
		//! with the SAME transform (derived from the base only), so
		//! corresponding vertices stay corresponding.
		//! @return false when the base pose carries no vertices to measure
		static bool place(VectorShapeAsset::ParsedShape & shape, double extent);

		//! @brief the whole tail in one call: place() then
		//! VectorShapeAsset::serialize(). `shape` arrives in the drawing's own
		//! y-down space and is CONSUMED (placed in world units).
		//! @return false (outText untouched) when there is nothing to place
		static bool emit(VectorShapeAsset::ParsedShape & shape, double extent,
			String const & headerComment, String & outText);
	};
}

#endif //__VectorShapeCook_h__29_7_2026__21_00_00__
