/********************************************************************
	created:	Thursday 2026/07/10 at 10:00
	filename: 	VectorTessellator.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorTessellator_h__10_7_2026__10_00_00__
#define __VectorTessellator_h__10_7_2026__10_00_00__

//! @file VectorTessellator.h
//! @brief flatten + triangulate flat-colour organic vector shapes into a
//! vertex-coloured triangle mesh - pure, headless, renderer-free
//! @remarks Lives in orkige_core (not the render layer) ON PURPOSE: it is
//! straight geometry math with no Ogre/facade dependency, so the unit suite
//! pins the flattening, triangulation and edge-feather math WITHOUT booting a
//! render system. The engine side (engine_gocomponent/VectorShapeComponent)
//! converts the produced Mesh into the facade VectorMesh vertex format. The
//! output positions are shape-local XY (z comes from the node); colours are
//! straight RGBA (the component multiplies its per-instance tint in).

#include <vector>

namespace Orkige
{
	//! @brief the pure geometry core of the vector-shape paint pipeline
	//! @remarks Static functions only - no state. Types are plain PODs (no
	//! renderer math) so this compiles into orkige_core with zero Ogre
	//! coupling. Triangulation is Mapbox earcut (concave polygons + holes),
	//! confined to the .cpp TU exactly like the stb_vorbis decoder.
	class VectorTessellator
	{
	public:
		//--- Types -------------------------------------------------
		//! a 2D point in shape-local units (+x right, +y up)
		struct Point
		{
			float x;
			float y;
			Point() : x(0.0f), y(0.0f) {}
			Point(float px, float py) : x(px), y(py) {}
		};
		//! straight (non-premultiplied) RGBA, components 0..1
		struct Colour
		{
			float r;
			float g;
			float b;
			float a;
			Colour() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
			Colour(float pr, float pg, float pb, float pa)
				: r(pr), g(pg), b(pb), a(pa) {}
		};
		enum PaintType
		{
			PAINT_SOLID,
			PAINT_LINEAR_GRADIENT,
			PAINT_RADIAL_GRADIENT
		};
		//! what a region paints: an area (its contour is a closed boundary) or
		//! a stroke (its contour is a CENTRELINE swept by strokeWidth)
		enum RegionKind
		{
			REGION_FILL,
			REGION_STROKE
		};
		//! how an OPEN stroke's ends are finished
		enum StrokeCap
		{
			CAP_BUTT,		//!< the end stops at the last centreline point
			CAP_ROUND,		//!< a half-disc of radius strokeWidth/2
			CAP_SQUARE		//!< the ribbon projects strokeWidth/2 past the end
		};
		//! how a stroke turns a corner
		enum StrokeJoin
		{
			JOIN_MITER,		//!< extend both edges to their crossing (limited)
			JOIN_ROUND,		//!< an arc of radius strokeWidth/2
			JOIN_BEVEL		//!< one triangle across the corner
		};
		struct GradientStop
		{
			float offset;
			Colour colour;
			GradientStop() : offset(0.0f) {}
			GradientStop(float at, Colour const & value)
				: offset(at), colour(value) {}
		};
		//! one flattened region: a FILL (closed outer contour + optional holes)
		//! or a STROKE (outer is the centreline, swept by strokeWidth). Both
		//! carry one paint (flat colour or gradient). Contours are already
		//! bezier-flattened (the cook does it); the runtime reads polylines.
		struct Region
		{
			std::vector<Point>				outer;	//!< fill: closed outer contour (no repeated last point); stroke: the centreline
			std::vector<std::vector<Point> >	holes;	//!< optional inner loops cut out of the fill (fill regions only)
			Colour							fill;	//!< flat fill colour of the region
			PaintType						paintType;	//!< solid, linear or radial
			Point							gradientStart;
			Point							gradientEnd;
			Point							gradientFocal;	//!< radial focal point; start by default
			std::vector<GradientStop>			gradientStops;
			RegionKind						kind;		//!< area or stroke (@see RegionKind)
			float							strokeWidth;	//!< full ribbon width (stroke regions)
			StrokeCap						strokeCap;	//!< end finish of an OPEN stroke
			StrokeJoin						strokeJoin;	//!< corner finish
			float							strokeMiterLimit;	//!< miter length / half width ceiling; beyond it the corner bevels
			bool							strokeClosed;	//!< the centreline is a closed loop (no caps)
			//! optional CONVEX clip polygon (a layer mask): stroke geometry is
			//! clipped against it. Empty = unclipped. A fill region's contour is
			//! clipped where it is authored/cooked, so this is stroke-only.
			std::vector<Point>				mask;
			Region() : paintType(PAINT_SOLID), kind(REGION_FILL),
				strokeWidth(0.0f), strokeCap(CAP_BUTT), strokeJoin(JOIN_MITER),
				strokeMiterLimit(4.0f), strokeClosed(false) {}
		};
		//! a 2D axis-aligned bounds (thumbnail/fit + feather-width derivation)
		struct Bounds
		{
			float	minX;
			float	minY;
			float	maxX;
			float	maxY;
			bool	valid;	//!< false when no points contributed
			Bounds() : minX(0.0f), minY(0.0f), maxX(0.0f), maxY(0.0f),
				valid(false) {}
			//! bounding-box diagonal length (0 when invalid)
			float diagonal() const;
		};
		//! the built mesh: parallel position/colour arrays + a triangle index
		//! list (3 indices per triangle), plus the shape-local bounds
		struct Mesh
		{
			std::vector<Point>			positions;	//!< shape-local XY, z=0
			std::vector<Colour>			colours;	//!< per-vertex (fill or feather-ramped alpha)
			std::vector<unsigned int>	indices;	//!< 3 per triangle
			Bounds						bounds;		//!< local 2D bounds of the fill regions
			//! triangle count (indices/3)
			std::size_t triangleCount() const { return this->indices.size() / 3; }
			//! drop all geometry
			void clear();
		};

		//--- pure geometry -----------------------------------------
		//! @brief adaptive-subdivision flatten of a cubic Bezier to an absolute
		//! chord tolerance (max deviation of the control polygon from the
		//! chord). Appends the flattened points EXCLUDING p0 (so contours chain
		//! without duplicating shared endpoints); the final point is p3.
		//! @remarks the runtime never calls this (the .oshape is pre-flattened);
		//! it exists for the cook's C++ twin and the tolerance unit tests. A
		//! tolerance <= 0 emits just the endpoint (a straight segment).
		static void flattenCubic(Point const & p0, Point const & p1,
			Point const & p2, Point const & p3, float tolerance,
			std::vector<Point> & out);
		//! @brief adaptive-subdivision flatten of a quadratic Bezier (elevated
		//! to a cubic, then flattenCubic); same append contract as flattenCubic
		static void flattenQuadratic(Point const & p0, Point const & p1,
			Point const & p2, float tolerance, std::vector<Point> & out);

		//! @brief triangulate ONE region (outer contour + its holes) into fill
		//! triangles, APPENDING to out (positions get the region's fill colour,
		//! indices are offset past any geometry already in out). Handles concave
		//! outlines and holes (earcut). A degenerate region (< 3 outer points)
		//! contributes nothing.
		static void triangulateFill(Region const & region, Mesh & out);

		//! @brief sweep ONE stroke region's centreline into triangles, APPENDING
		//! to out. The ribbon is emitted as independently CONVEX pieces - a quad
		//! per segment, a wedge per interior corner (round fan / limited miter /
		//! bevel) and a cap per open end - NOT as one offset outline handed to a
		//! triangulator: an offset outline self-intersects wherever the path
		//! curves tighter than the half width or doubles back, and a triangulator
		//! needs a SIMPLE polygon, so such a corner produces garbage (spikes and
		//! filaments). Convex pieces cannot: each is valid on its own and pieces
		//! may overlap harmlessly. Overlap is only visible for a TRANSLUCENT
		//! stroke, where a join blends twice (an opaque stroke - the common case -
		//! is exact). A non-stroke region, a width <= 0 or fewer than 2
		//! centreline points contributes nothing. An optional convex region.mask
		//! clips every piece.
		static void appendStroke(Region const & region, Mesh & out);

		//! @brief append the alpha-ramp feather rim of a stroke region: the same
		//! soft edge appendFeather gives a fill, walked along the ribbon's two
		//! offset boundaries (joins and caps included), extruded outward by
		//! width. Where the rim crosses the ribbon's own overlap it draws the
		//! stroke's colour over the stroke's colour, which is invisible. A width
		//! <= 0 or a non-stroke region contributes nothing.
		static void appendStrokeFeather(Region const & region, float width,
			Mesh & out);

		//! @brief append an alpha-ramp feather strip along a closed contour: a
		//! one-quad-per-edge ring whose INNER edge sits on the contour at the
		//! fill colour (alpha = fill.a) and whose OUTER edge is the same rgb at
		//! alpha 0, extruded outward by width in shape-local units. This is the
		//! portable edge anti-aliasing (the engine forces FSAA 0, so hardware
		//! multisampling is unavailable). A width <= 0 or a contour with < 3
		//! points contributes nothing.
		static void appendFeather(std::vector<Point> const & contour,
			Colour const & fill, float width, Mesh & out);

		//! @brief full build: paint every region in order (a fill is triangulated,
		//! a stroke is swept into convex pieces), each immediately followed by
		//! its own feather rim, into one mesh - so a later region occludes an
		//! earlier region's body AND soft edge (a feather appended after all
		//! bodies would redraw hidden contours above the geometry covering
		//! them). featherWidth <= 0 skips the feather entirely. The mesh's
		//! bounds cover the painted regions.
		static void build(std::vector<Region> const & regions,
			float featherWidth, Mesh & out);

		//--- helpers -----------------------------------------------
		//! @brief bounds over every region's painted extent (a fill's outer
		//! contour; a stroke's centreline grown by its half width)
		static Bounds computeBounds(std::vector<Region> const & regions);
		//! @brief the default feather width for a shape of the given bounds:
		//! a small fraction of the bounding diagonal, so the soft edge stays
		//! proportional at any authored scale (0 for degenerate bounds)
		static float defaultFeatherWidth(Bounds const & bounds);
		//! @brief signed area of a closed polygon (positive = counter-clockwise)
		static float signedArea(std::vector<Point> const & contour);
	};
}

#endif //__VectorTessellator_h__10_7_2026__10_00_00__
