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
	//! @brief the pure geometry core of the flat-colour vector-shape pipeline
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
		//! one flattened filled region: a closed outer contour, optional inner
		//! loops (holes), and one flat fill colour. Contours are already
		//! bezier-flattened (the cook does it); the runtime reads polylines.
		struct Region
		{
			std::vector<Point>				outer;	//!< closed outer contour (no repeated last point)
			std::vector<std::vector<Point> >	holes;	//!< optional inner loops cut out of the fill
			Colour							fill;	//!< flat fill colour of the region
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

		//! @brief append an alpha-ramp feather strip along a closed contour: a
		//! one-quad-per-edge ring whose INNER edge sits on the contour at the
		//! fill colour (alpha = fill.a) and whose OUTER edge is the same rgb at
		//! alpha 0, extruded outward by width in shape-local units. This is the
		//! portable edge anti-aliasing (the engine forces FSAA 0, so hardware
		//! multisampling is unavailable). A width <= 0 or a contour with < 3
		//! points contributes nothing.
		static void appendFeather(std::vector<Point> const & contour,
			Colour const & fill, float width, Mesh & out);

		//! @brief full build: triangulate every region's fill, then feather
		//! every region's OUTER contour, into one mesh. featherWidth <= 0 skips
		//! the feather entirely. The mesh's bounds cover the fill regions.
		static void build(std::vector<Region> const & regions,
			float featherWidth, Mesh & out);

		//--- helpers -----------------------------------------------
		//! @brief bounds over every region's OUTER contour (fill extent)
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
