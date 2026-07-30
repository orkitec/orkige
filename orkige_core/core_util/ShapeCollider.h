/********************************************************************
	created:	Friday 2026/07/25 at 10:00
	filename: 	ShapeCollider.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ShapeCollider_h__25_7_2026__10_00_00__
#define __ShapeCollider_h__25_7_2026__10_00_00__

//! @file ShapeCollider.h
//! @brief derive planar collision geometry from a tessellated `.oshape` -
//! pure, headless, renderer- and physics-free
//! @remarks Lives in orkige_core next to VectorTessellator (the flat-colour
//! vector-shape tessellator) ON PURPOSE: it is straight geometry math with no
//! Ogre/Jolt dependency, so the unit suite pins contour extraction, convex-hull
//! generation and the planar prism extrusion WITHOUT booting a physics or render
//! system. The engine side (engine_gocomponent/RigidBodyComponent) feeds the
//! produced geometry to the Jolt facade (engine_physic/PhysicsWorld):
//!   * STATIC / KINEMATIC bodies take the concave EXTRUDED MESH (buildExtrudedMesh)
//!     as a Jolt MeshShape (a static-only triangle collider that honours the
//!     shape's true concave outline - an L or U cup collides on its inner edges);
//!   * DYNAMIC bodies take the CONVEX HULL of the outline (convexHull) extruded to
//!     a prism, because Jolt dynamic bodies need a convex shape. A concave dynamic
//!     request degrades to that hull (@see isConvex - the caller warns once).
//!
//! Holes are IGNORED in v1: only fill regions' OUTER contours become collision
//! geometry (a shape with a cut-out collides as though the cut-out were solid).
//! Bezier curves are already flattened in the `.oshape` (the cook does it with the
//! VectorTessellator flatten machinery), so contour extraction consumes polylines.
//! Transform / shape scale is NOT applied here (the primitive box/sphere/capsule
//! colliders ignore it too): the geometry is in the shape's authored units.

#include "core_util/VectorTessellator.h"

#include <vector>

namespace Orkige
{
	//! @brief the pure geometry core of the shape-collider pipeline
	//! @remarks Static functions only - no state. Consumes VectorTessellator
	//! Region/Point PODs and produces plain 3D vertices, so it compiles into
	//! orkige_core with zero Ogre or Jolt coupling.
	class ShapeCollider
	{
	public:
		//--- Types -------------------------------------------------
		//! reuse the tessellator's 2D shape-local point (+x right, +y up)
		typedef VectorTessellator::Point Point;
		//! a 3D collision-mesh vertex (the planar shape extruded along z)
		struct Vertex
		{
			float x;
			float y;
			float z;
			Vertex() : x(0.0f), y(0.0f), z(0.0f) {}
			Vertex(float px, float py, float pz) : x(px), y(py), z(pz) {}
		};

		//--- the shared contour vocabulary -------------------------
		//! @brief does this region carry a usable closed BOUNDARY - a FILL
		//! region (a stroke is a swept centreline enclosing no area) whose outer
		//! loop has at least 3 distinct points? This is the eligibility test
		//! extractContours applies, exposed so a consumer that needs the REGION
		//! itself rather than a bare point loop (the mesh extruder pairs each
		//! outer contour with that region's holes) selects EXACTLY the same set
		//! instead of re-deriving it.
		static bool isSolidRegion(VectorTessellator::Region const & region);
		//! @brief a point loop with any repeated closing vertex dropped (many
		//! authored/cooked contours repeat the first point as the last), so
		//! every consumer sees one clean open loop. The shared normalisation
		//! step behind extractContours - a caller reading `region.holes`
		//! directly runs it too.
		static std::vector<Point> openLoop(std::vector<Point> const & loop);

		//--- pure geometry -----------------------------------------
		//! @brief extract the OUTER contours of every FILL region into outContours
		//! (one point loop per region, in region order; each loop has no repeated
		//! last point). Stroke regions and holes are skipped (v1: only filled area
		//! outlines are collidable). A region with fewer than 3 outer points
		//! contributes nothing. outContours is cleared first.
		static void extractContours(
			std::vector<VectorTessellator::Region> const & regions,
			std::vector<std::vector<Point> > & outContours);

		//! @brief the convex hull of a point set (Andrew's monotone chain),
		//! returned counter-clockwise with no repeated endpoint. Fewer than 3
		//! unique points return the deduplicated input unchanged (a degenerate
		//! hull). Collinear interior points are dropped.
		static std::vector<Point> convexHull(std::vector<Point> const & points);

		//! @brief is a closed polygon convex (every turn the same way, collinear
		//! vertices allowed)? A polygon with fewer than 3 points is treated as
		//! convex (nothing to degrade). This is the DYNAMIC-body gate: a concave
		//! contour must fall back to its convex hull.
		static bool isConvex(std::vector<Point> const & contour);

		//! @brief build the extruded concave collision MESH for the given outer
		//! contours: each contour is triangulated (its front and back caps at
		//! +/-halfThickness along z) and its edges are swept into vertical side-wall
		//! quads, so the result is a closed prism per contour honouring the true
		//! concave outline. Appends into outVertices / outIndices (3 indices per
		//! triangle, addressing outVertices), which are cleared first. A
		//! halfThickness <= 0 or an empty contour set produces no geometry.
		static void buildExtrudedMesh(
			std::vector<std::vector<Point> > const & contours,
			float halfThickness,
			std::vector<Vertex> & outVertices,
			std::vector<unsigned int> & outIndices);

		//! @brief extrude a 2D outline into the 3D point cloud a convex-hull
		//! collider consumes: every outline point duplicated at +halfThickness and
		//! -halfThickness along z. Appends into outVertices (cleared first). The
		//! outline is taken verbatim (hull it with convexHull first for a dynamic
		//! body). A halfThickness <= 0 flattens both faces onto z=0.
		static void extrudeOutlinePoints(std::vector<Point> const & outline,
			float halfThickness, std::vector<Vertex> & outVertices);
	};
}

#endif //__ShapeCollider_h__25_7_2026__10_00_00__
