/**************************************************************
	created:	2026/07/24 at 15:00
	filename: 	EditorOverlayGeometry.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorOverlayGeometry_h__24_7_2026__15_00_00__
#define __EditorOverlayGeometry_h__24_7_2026__15_00_00__

// Pure, headless-unit-tested line-list builders for the Scene panel's
// display-option overlays (collider wireframes + renderable bounding boxes).
// Each function APPENDS consecutive point PAIRS (one segment each) and a colour
// per point, exactly the layout RenderWorld::createLineListMesh consumes and the
// camera frustum gizmo (EditorCameraGizmo.h) already uses. Kept engine-node-free
// so the geometry is asserted in tests/editor_core; the panel-side node/mesh
// plumbing lives in EditorScenePanel.cpp and draws every overlay through the SAME
// facade line-mesh path as the reference grid and the frustum gizmo.

#include <engine_render/RenderMath.h>	// Vec3, Quat, Color
#include <core_util/String.h>			// String (the overlay mesh-name seam)

#include <algorithm>
#include <cmath>
#include <vector>

namespace Orkige
{
	//! collider wireframe colour - a saturated green that reads as "physics"
	//! against the neutral grid without clashing with the axis colours
	inline Color editorColliderColour()
	{
		return Color(0.30f, 0.85f, 0.40f);
	}

	//! renderable bounding-box colour - a cool cyan, distinct from both the
	//! green colliders and the white-ish camera frustums
	inline Color editorBoundingBoxColour()
	{
		return Color(0.30f, 0.75f, 0.90f);
	}

	//! default number of segments a collider circle/ring is drawn with (kept
	//! modest - these are editor overlays, not shaded geometry)
	inline int editorColliderCircleSegments() { return 24; }

	//! @brief append ONE line segment (point pair + colour per point)
	inline void appendOverlaySegment(Vec3 const& a, Vec3 const& b,
		Color const& colour, std::vector<Vec3>& outPoints,
		std::vector<Color>& outColours)
	{
		outPoints.push_back(a);
		outPoints.push_back(b);
		outColours.push_back(colour);
		outColours.push_back(colour);
	}

	//! @brief append an arc as `segments` connected line segments in the plane
	//! spanned by uAxis/vAxis, centred at `centre`, radius `radius`, sweeping
	//! from `startAngle` to `endAngle` (radians). A full circle is 0..2*pi.
	inline void appendOverlayArc(Vec3 const& centre, Vec3 const& uAxis,
		Vec3 const& vAxis, float radius, float startAngle, float endAngle,
		int segments, Color const& colour, std::vector<Vec3>& outPoints,
		std::vector<Color>& outColours)
	{
		if (segments < 1)
		{
			segments = 1;
		}
		const float step = (endAngle - startAngle) /
			static_cast<float>(segments);
		Vec3 prev = centre + uAxis * (radius * std::cos(startAngle)) +
			vAxis * (radius * std::sin(startAngle));
		for (int i = 1; i <= segments; ++i)
		{
			const float angle = startAngle + step * static_cast<float>(i);
			const Vec3 point = centre + uAxis * (radius * std::cos(angle)) +
				vAxis * (radius * std::sin(angle));
			appendOverlaySegment(prev, point, colour, outPoints, outColours);
			prev = point;
		}
	}

	//! @brief append an ORIENTED box wireframe (12 edges) centred at `centre`,
	//! rotated by `orientation`, with the given half extents.
	inline void appendOverlayBox(Vec3 const& centre, Quat const& orientation,
		Vec3 const& halfExtents, Color const& colour,
		std::vector<Vec3>& outPoints, std::vector<Color>& outColours)
	{
		Vec3 corner[8];
		int index = 0;
		for (int sx = -1; sx <= 1; sx += 2)
		{
			for (int sy = -1; sy <= 1; sy += 2)
			{
				for (int sz = -1; sz <= 1; sz += 2)
				{
					const Vec3 local(sx * halfExtents.x, sy * halfExtents.y,
						sz * halfExtents.z);
					corner[index++] = centre + orientation * local;
				}
			}
		}
		// corner index bit layout: bit2 = x sign, bit1 = y sign, bit0 = z sign.
		// the 12 edges connect corners differing in exactly one axis bit.
		static const int edges[12][2] = {
			{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },	// edges along z
			{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },	// edges along y
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }	// edges along x
		};
		for (auto const& edge : edges)
		{
			appendOverlaySegment(corner[edge[0]], corner[edge[1]], colour,
				outPoints, outColours);
		}
	}

	//! @brief append an AXIS-ALIGNED box wireframe (12 edges) from its min/max
	//! world corners - the renderable bounding-box overlay.
	inline void appendOverlayAabb(Vec3 const& minCorner, Vec3 const& maxCorner,
		Color const& colour, std::vector<Vec3>& outPoints,
		std::vector<Color>& outColours)
	{
		const Vec3 centre = (minCorner + maxCorner) * 0.5f;
		const Vec3 halfExtents = (maxCorner - minCorner) * 0.5f;
		appendOverlayBox(centre, Quat::IDENTITY, halfExtents, colour, outPoints,
			outColours);
	}

	//! @brief append a sphere wireframe as three orthogonal great circles.
	inline void appendOverlaySphere(Vec3 const& centre, float radius,
		int segments, Color const& colour, std::vector<Vec3>& outPoints,
		std::vector<Color>& outColours)
	{
		const float twoPi = 6.28318530717958647692f;
		appendOverlayArc(centre, Vec3::UNIT_X, Vec3::UNIT_Y, radius, 0.0f, twoPi,
			segments, colour, outPoints, outColours);	// XY circle
		appendOverlayArc(centre, Vec3::UNIT_X, Vec3::UNIT_Z, radius, 0.0f, twoPi,
			segments, colour, outPoints, outColours);	// XZ circle
		appendOverlayArc(centre, Vec3::UNIT_Y, Vec3::UNIT_Z, radius, 0.0f, twoPi,
			segments, colour, outPoints, outColours);	// YZ circle
	}

	//! @brief the exact segment count appendOverlayCapsule emits (for tests):
	//! two end rings (2*segments) + four vertical connectors + four cap dome
	//! arcs of segments/2 each (2*segments).
	inline int editorCapsuleSegmentCount(int segments)
	{
		return 4 * segments + 4;
	}

	//! @brief append a capsule wireframe: a cylinder of half-height `halfHeight`
	//! along the LOCAL Y axis capped by two hemispheres of radius `radius`,
	//! centred at `centre` and rotated by `orientation` (Jolt's capsule axis is
	//! Y, so a planar-2D body draws upright in the XY plane).
	inline void appendOverlayCapsule(Vec3 const& centre, Quat const& orientation,
		float halfHeight, float radius, int segments, Color const& colour,
		std::vector<Vec3>& outPoints, std::vector<Color>& outColours)
	{
		const float pi = 3.14159265358979323846f;
		const float twoPi = 2.0f * pi;
		const Vec3 xAxis = orientation * Vec3::UNIT_X;
		const Vec3 yAxis = orientation * Vec3::UNIT_Y;
		const Vec3 zAxis = orientation * Vec3::UNIT_Z;
		const Vec3 topCentre = centre + yAxis * halfHeight;
		const Vec3 bottomCentre = centre - yAxis * halfHeight;
		const int halfSegs = std::max(1, segments / 2);

		// the two cylinder-end rings (in the local XZ plane)
		appendOverlayArc(topCentre, xAxis, zAxis, radius, 0.0f, twoPi, segments,
			colour, outPoints, outColours);
		appendOverlayArc(bottomCentre, xAxis, zAxis, radius, 0.0f, twoPi,
			segments, colour, outPoints, outColours);
		// four vertical connectors between the rings (+X, -X, +Z, -Z)
		appendOverlaySegment(topCentre + xAxis * radius,
			bottomCentre + xAxis * radius, colour, outPoints, outColours);
		appendOverlaySegment(topCentre - xAxis * radius,
			bottomCentre - xAxis * radius, colour, outPoints, outColours);
		appendOverlaySegment(topCentre + zAxis * radius,
			bottomCentre + zAxis * radius, colour, outPoints, outColours);
		appendOverlaySegment(topCentre - zAxis * radius,
			bottomCentre - zAxis * radius, colour, outPoints, outColours);
		// the four cap dome arcs (top + bottom, each in the XY and ZY planes)
		appendOverlayArc(topCentre, xAxis, yAxis, radius, 0.0f, pi, halfSegs,
			colour, outPoints, outColours);
		appendOverlayArc(topCentre, zAxis, yAxis, radius, 0.0f, pi, halfSegs,
			colour, outPoints, outColours);
		appendOverlayArc(bottomCentre, xAxis, yAxis, radius, pi, twoPi, halfSegs,
			colour, outPoints, outColours);
		appendOverlayArc(bottomCentre, zAxis, yAxis, radius, pi, twoPi, halfSegs,
			colour, outPoints, outColours);
	}

	//! @brief append the collider outline for a RigidBodyComponent shape,
	//! dispatched on the PhysicsWorld::ShapeType value (0 = box, 1 = sphere,
	//! 2 = capsule). The single entry the Scene panel calls per body; all
	//! geometry is emitted in WORLD space at the body's world pose.
	inline void appendColliderOutline(int shapeType, Vec3 const& worldCentre,
		Quat const& worldOrientation, Vec3 const& halfExtents, float radius,
		float halfHeight, int circleSegments, Color const& colour,
		std::vector<Vec3>& outPoints, std::vector<Color>& outColours)
	{
		switch (shapeType)
		{
		case 1:	// ST_SPHERE
			appendOverlaySphere(worldCentre, radius, circleSegments, colour,
				outPoints, outColours);
			break;
		case 2:	// ST_CAPSULE
			appendOverlayCapsule(worldCentre, worldOrientation, halfHeight,
				radius, circleSegments, colour, outPoints, outColours);
			break;
		case 0:	// ST_BOX
		default:
			appendOverlayBox(worldCentre, worldOrientation, halfExtents, colour,
				outPoints, outColours);
			break;
		}
	}

	//! @brief append the collider outline for an ST_SHAPE body: each shape-local
	//! contour (a closed loop of XY points, z ignored) drawn as connected line
	//! segments at the body's world pose (worldCentre + worldOrientation * local).
	//! A contour of N points emits N segments (2N vertices) - the closing edge
	//! back to the first point is included. This draws the ACTUAL collidable
	//! outline the shape collider is built from (@see core_util/ShapeCollider);
	//! the extruded prism's depth is not drawn (a 2D outline reads cleaner in the
	//! XY authoring view). Transform / shape scale is not applied, matching the
	//! collider geometry.
	inline void appendColliderShapeOutline(
		std::vector<std::vector<Vec3> > const& contours, Vec3 const& worldCentre,
		Quat const& worldOrientation, Color const& colour,
		std::vector<Vec3>& outPoints, std::vector<Color>& outColours)
	{
		for (std::vector<Vec3> const& contour : contours)
		{
			const std::size_t n = contour.size();
			if (n < 2)
			{
				continue;
			}
			for (std::size_t i = 0; i < n; ++i)
			{
				const Vec3 a = worldCentre + worldOrientation * contour[i];
				const Vec3 b = worldCentre +
					worldOrientation * contour[(i + 1) % n];
				appendOverlaySegment(a, b, colour, outPoints, outColours);
			}
		}
	}

	//--- panel-side overlay state (defined in EditorScenePanel.cpp) ------------
	//! @brief line vertices the Scene panel last uploaded for the collider
	//! wireframe overlay (0 when the toggle is off / no bodies) - a non-pixel
	//! "the overlay drew" seam for the editor selfcheck.
	std::size_t editorSceneColliderOverlayVertexCount();
	//! @brief the collider overlay's CURRENT mesh resource name ("" when none) -
	//! the leak-probe seam: after a rebuild the previous name must no longer
	//! exist (RenderWorld::generatedMeshExists) while this one does.
	String const& editorSceneColliderOverlayMeshName();
	//! @brief line vertices the Scene panel last uploaded for the renderable
	//! bounding-box overlay (0 when off / no renderables).
	std::size_t editorSceneBoundingBoxOverlayVertexCount();
	//! @brief line vertices the Scene panel last uploaded for the all-cameras
	//! frames overlay (frustums + design-aspect rects; 0 when off / no cameras).
	std::size_t editorSceneCameraFramesOverlayVertexCount();
	//! @brief drop the display-option overlays' persistent nodes + meshes.
	//! MUST be called before the render system is torn down (their state is
	//! process-lifetime, like the frustum gizmo's). Safe when never created.
	void editorSceneOverlaysRelease();
}

#endif //__EditorOverlayGeometry_h__24_7_2026__15_00_00__
