/********************************************************************
	created:	Thursday 2026/07/30 at 09:20
	filename: 	MeshExtrude.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __MeshExtrude_h__30_7_2026__09_20_00__
#define __MeshExtrude_h__30_7_2026__09_20_00__

//! @file MeshExtrude.h
//! @brief the 2D-to-3D operators: turn an authored `.oshape` outline into a lit
//! 3D mesh by extruding it to a depth or revolving it about the Y axis - pure,
//! headless, renderer-free
//! @remarks This is where the flat-colour vector tier feeds the 3D tier: the
//! SAME `.oshape` a `VectorShapeComponent` paints and a `RigidBodyComponent`
//! collides against becomes solid geometry. Nothing here parses text or walks
//! contours of its own:
//!   * region ELIGIBILITY and loop normalisation are `ShapeCollider::
//!     isSolidRegion` / `openLoop` - the one shared contour vocabulary the
//!     collider path already applies, so a shape collides and extrudes over
//!     exactly the same outlines;
//!   * cap TRIANGULATION is `VectorTessellator::triangulateFill`, which already
//!     earcuts a concave outline WITH its holes;
//!   * the revolve is `MeshShapes::revolveProfile`, the same lathe every round
//!     primitive is expressed as.
//! The extruder only adds the third dimension: the two caps, the side walls and
//! the normals/UVs/tangents that make them lit-renderable.
//!
//! UNITS are the shape's authored units, 1:1 into mesh units (the collider path
//! makes the same choice), and the shape's XY plane becomes the mesh's XY plane
//! with the extrusion along Z. Region COLOURS are ignored - a 3D surface takes
//! its look from a `.omat` material, not from baked vertex colours.

#include "core_util/MeshBuilder.h"
#include "core_util/VectorTessellator.h"

#include <vector>

namespace Orkige
{
	//! @brief the `.oshape` outline -> 3D mesh operators (@see MeshExtrude.h).
	//! Static functions only - no state.
	class MeshExtrude
	{
	public:
		using Mesh = MeshBuilder::Mesh;

		//! @brief extrude every solid region of @p regions along Z into a closed
		//! solid of total thickness @p depth, centred on z = 0: a front cap at
		//! +depth/2 (normal +Z), a back cap at -depth/2 (normal -Z) and side
		//! walls swept from every boundary loop - a region's outer contour AND
		//! its holes, so a shape with a cut-out extrudes as a real tunnel (the
		//! cap earcut and the wall sweep agree on the loop set).
		//! @param smoothSides false (the default) gives every wall quad its own face
		//! normal - crisp, right for a polygonal outline; true averages the
		//! adjacent edge normals at each contour vertex, so a flattened curve
		//! shades as a curve.
		//! Cap UVs are the outline's XY position normalised over the shape
		//! bounds; wall UVs run u along the loop's arc length and v across the
		//! depth. A non-positive/non-finite depth, or a region set with no solid
		//! region, is an honest failure (returns false, leaves @p out empty).
		static bool extrudeShape(Mesh & out,
			std::vector<VectorTessellator::Region> const & regions,
			float depth, bool smoothSides = false, String * outError = NULL);

		//! @brief revolve a PROFILE outline about the Y axis. The profile is the
		//! outer contour of the FIRST solid region (a `.oshape` authored as one
		//! silhouette): each point's x is read as a RADIUS and its y as a
		//! height, so the outline must stay on the x >= 0 half-plane - a point
		//! left of the axis would sweep through itself and is refused honestly.
		//! The contour is walked from its highest point downward so the swept
		//! surface faces OUTWARD, and each point's surface normal comes from the
		//! neighbouring segments (a smooth lathe over a flattened curve).
		//! @param sweepDegrees 360 closes the surface; a smaller sweep leaves
		//! the two profile-shaped ends open (@see MeshShapes::revolveProfile).
		//! Placement is the profile's own - the result is NOT re-centred, so a
		//! profile authored from y = 0 up stands on the ground plane.
		static bool revolveShape(Mesh & out,
			std::vector<VectorTessellator::Region> const & regions,
			int segments = 32, float sweepDegrees = 360.0f,
			String * outError = NULL);
	};
}

#endif //__MeshExtrude_h__30_7_2026__09_20_00__
