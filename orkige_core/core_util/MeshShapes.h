/********************************************************************
	created:	Thursday 2026/07/30 at 09:10
	filename: 	MeshShapes.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __MeshShapes_h__30_7_2026__09_10_00__
#define __MeshShapes_h__30_7_2026__09_10_00__

//! @file MeshShapes.h
//! @brief the parametric shape vocabulary a text-authored mesh is built from -
//! pure, headless, renderer-free
//! @remarks Every generator produces a finished MeshBuilder::Mesh: positions,
//! outward unit normals, in-range UVs, tangents and ONE material section (empty
//! name - the caller attributes it while merging, @see MeshBuilder::append). The
//! set is deliberately the one a blockout actually asks for: the round/regular
//! primitives (box, rounded box, plane, spheres, cylinder, cone, capsule, torus,
//! tube, disc) plus the three level-geometry solids no primitive set ever has
//! when you need them (wedge/ramp, stairs, arch).
//!
//! SHARED CONVENTIONS
//!  * right-handed, +Y up, counter-clockwise front faces seen from OUTSIDE;
//!  * every shape is centred on its own BOUNDING-BOX centre, so a placement
//!    translation puts the shape's middle there and two shapes of equal size
//!    sit flush when their centres are one size apart. The rotationally
//!    symmetric shapes are additionally centred on the Y axis;
//!  * UVs run 0..1 across each shape's natural parametrisation (per face for the
//!    box family, angle/height for the round family) with V DOWN - the texture's
//!    top row lands at the shape's top, matching the sprite/`.oshape` convention;
//!  * the same arguments always produce byte-identical buffers
//!    (@see MeshBuilder.h - determinism is a contract);
//!  * a non-positive or non-finite EXTENT is an honest failure (returns false,
//!    leaves @p out EMPTY, fills @p outError); a COUNT out of range is clamped
//!    into it (@see MeshBuilder::clampSegments). Nothing here can crash or emit
//!    a NaN.

#include "core_util/MeshBuilder.h"

namespace Orkige
{
	//! @brief the parametric shape generators (@see MeshShapes.h). Static
	//! functions only - no state.
	class MeshShapes
	{
	public:
		using Mesh = MeshBuilder::Mesh;
		using Vec2f = MeshBuilder::Vec2f;
		using Vec3f = MeshBuilder::Vec3f;

		//! @brief one row of a LATHE profile: a radius/height pair on the
		//! half-plane that is swept about the Y axis, plus the outward surface
		//! normal in that same (radial, Y) plane. A row of radius 0 is a POLE
		//! (its degenerate triangles are never emitted).
		struct ProfileRow
		{
			float	radius;
			float	y;
			float	normalRadial;	//!< outward component in the radial direction
			float	normalY;		//!< outward component along Y
			ProfileRow() : radius(0.0f), y(0.0f), normalRadial(1.0f),
				normalY(0.0f) {}
			ProfileRow(float atRadius, float atY, float radialNormal,
				float yNormal) : radius(atRadius), y(atY),
				normalRadial(radialNormal), normalY(yNormal) {}
		};

		//--- box family --------------------------------------------
		//! @brief an axis-aligned box centred on the origin: 24 vertices (4 per
		//! face, so each face keeps a crisp normal and its own 0..1 UV square)
		//! and 12 triangles
		static bool box(Mesh & out, float sizeX, float sizeY, float sizeZ,
			String * outError = NULL);
		//! @brief a box with rounded corners and edges: each of the 6 faces is a
		//! (@p segments+1)^2 grid whose points are pushed onto the rounded hull
		//! (the point's projection onto the inner box grown by @p radius), so the
		//! surface is watertight and the normals are exact (radius direction).
		//! @p radius is clamped to at most the smallest half extent; a radius of
		//! 0 degenerates to a subdivided plain box. @p segments counts the
		//! subdivisions per face edge (clamped to at least 1).
		static bool roundedBox(Mesh & out, float sizeX, float sizeY, float sizeZ,
			float radius, int segments = 4, String * outError = NULL);
		//! @brief a subdivided flat plane in the XZ ground plane, normal +Y,
		//! centred on the origin: (@p segmentsX+1) x (@p segmentsZ+1) vertices,
		//! UV u along +X and v along +Z. Counts are clamped to at least 1.
		static bool plane(Mesh & out, float sizeX, float sizeZ,
			int segmentsX = 1, int segmentsZ = 1, String * outError = NULL);

		//--- round family -----------------------------------------
		//! @brief a longitude/latitude sphere: @p segments columns around Y,
		//! @p rings latitude bands from pole to pole. The seam column is
		//! duplicated so UVs wrap cleanly; pole rows collapse (no degenerate
		//! triangles are emitted). Counts clamp to at least 3 columns / 2 bands.
		static bool uvSphere(Mesh & out, float radius, int segments = 24,
			int rings = 16, String * outError = NULL);
		//! @brief a geodesic sphere: an icosahedron whose triangles are
		//! recursively split @p subdivisions times and projected onto the
		//! sphere, so every triangle is nearly equilateral (the even-density
		//! sphere, no pole pinch). @p subdivisions clamps to 0..5 (20 to 20480
		//! triangles). UVs are a spherical projection and therefore carry the
		//! usual longitude SEAM - authored for flat/triplanar looks rather than
		//! for a wrapped texture atlas.
		static bool icosphere(Mesh & out, float radius, int subdivisions = 2,
			String * outError = NULL);
		//! @brief a cylinder about the Y axis, centred on the origin: a smooth
		//! side wall (seam column duplicated) plus optional flat end caps.
		//! @p segments clamps to at least 3.
		static bool cylinder(Mesh & out, float radius, float height,
			int segments = 24, bool caps = true, String * outError = NULL);
		//! @brief a cone about the Y axis, centred on the origin (apex at
		//! +height/2, base at -height/2): the side is one triangle per segment
		//! with its own apex normal (a shared apex vertex cannot carry the
		//! surrounding normals), plus an optional base cap.
		static bool cone(Mesh & out, float radius, float height,
			int segments = 24, bool cap = true, String * outError = NULL);
		//! @brief a capsule about the Y axis, centred on the origin: a
		//! cylindrical body of @p height (the STRAIGHT part - total height is
		//! height + 2*radius) closed by two hemispheres of @p rings latitude
		//! bands each. A @p height of 0 is a sphere.
		static bool capsule(Mesh & out, float radius, float height,
			int segments = 24, int rings = 8, String * outError = NULL);
		//! @brief a torus lying in the XZ plane, centred on the origin:
		//! @p segments steps around the main ring, @p tubeSegments around the
		//! tube. @p tubeRadius is clamped below @p radius (a self-intersecting
		//! tube is refused as an extent error only when it is non-positive).
		static bool torus(Mesh & out, float radius, float tubeRadius,
			int segments = 32, int tubeSegments = 16, String * outError = NULL);
		//! @brief a hollow tube/pipe about the Y axis, centred on the origin:
		//! an outer wall, an INWARD-facing inner wall and optional flat end
		//! rings. @p innerRadius must be positive and smaller than
		//! @p outerRadius (an honest extent error otherwise).
		static bool tube(Mesh & out, float outerRadius, float innerRadius,
			float height, int segments = 24, bool caps = true,
			String * outError = NULL);
		//! @brief a flat disc in the XZ plane at y = 0, normal +Y. A positive
		//! @p innerRadius smaller than @p radius makes it an annulus (a ring);
		//! 0 makes it a full disc fanned from a centre vertex.
		static bool disc(Mesh & out, float radius, float innerRadius = 0.0f,
			int segments = 24, String * outError = NULL);

		//--- blockout solids ---------------------------------------
		//! @brief a ramp: a right-triangular prism whose footprint is
		//! @p sizeX x @p sizeZ and which rises to @p sizeY, ascending along +X
		//! (its low edge at -X). Centred on its bounding box, so the walkable
		//! slope runs from (-sizeX/2, -sizeY/2) up to (+sizeX/2, +sizeY/2).
		//! 5 faces: bottom, the tall back wall at +X, the slope, two triangular
		//! sides.
		static bool wedge(Mesh & out, float sizeX, float sizeY, float sizeZ,
			String * outError = NULL);
		//! @brief a staircase of @p steps equal steps rising along +X over a
		//! total run of @p sizeX to a total rise of @p sizeY, @p sizeZ wide.
		//! Each step is a solid box from the ground up to its own tread height
		//! (so the staircase is one closed solid, not floating slabs), merged
		//! into ONE section. Centred on its bounding box. @p steps clamps to
		//! at least 1.
		static bool stairs(Mesh & out, float sizeX, float sizeY, float sizeZ,
			int steps = 8, String * outError = NULL);
		//! @brief an archway standing in the XY plane, @p depth deep along Z:
		//! a band of constant @p thickness following two straight legs of
		//! @p legHeight and the semicircular top over an opening of
		//! @p spanWidth. @p segments steps the arc. Centred on its bounding
		//! box. This is the sweepPath rectangle sweep over a generated path -
		//! the honest v1 arch (no keystone, no moulding profile).
		static bool arch(Mesh & out, float spanWidth, float legHeight,
			float thickness, float depth, int segments = 12,
			String * outError = NULL);

		//--- the generic lathe every round shape is built on -------
		//! @brief sweep a (radius, y) PROFILE about the Y axis - the surface of
		//! revolution every round primitive above is expressed as, and the
		//! engine behind `revolve`. Rows connect in their given order and the
		//! row ORDER decides which side faces out (top-to-bottom rows give
		//! outward-facing geometry for a convex silhouette); a row of radius 0
		//! is a pole whose degenerate triangles are never emitted. The V
		//! coordinate follows the profile's ARC LENGTH so a texture stretches
		//! evenly over the silhouette; U is the swept angle.
		//! @p sweepDegrees of 360 closes the surface; a smaller sweep leaves
		//! the two profile-shaped ends OPEN (a shell/fan - capping a partial
		//! sweep would need the profile triangulated, which this version does
		//! not do). @p segments is clamped like every count.
		static bool revolveProfile(Mesh & out, ProfileRow const * rows,
			std::size_t rowCount, int segments = 32,
			float sweepDegrees = 360.0f, String * outError = NULL);

		//--- the generic sweep the arch is built on ----------------
		//! @brief sweep a @p width x @p depth rectangle along an OPEN 2D
		//! polyline @p path (XY, the rectangle's width across the path normal
		//! and its depth along Z), producing a closed band with end caps. The
		//! rectangle is mitred at interior corners (averaged segment normals),
		//! so a smooth path gives a smooth band. Fewer than 2 path points, a
		//! non-positive width/depth or a path with no length is an honest
		//! failure. The result is NOT re-centred (the path decides placement).
		static bool sweepPath(Mesh & out, Vec2f const * path,
			std::size_t pathCount, float width, float depth,
			String * outError = NULL);
	};
}

#endif //__MeshShapes_h__30_7_2026__09_10_00__
