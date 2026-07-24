/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorCameraGizmo.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorCameraGizmo_h__24_7_2026__12_00_00__
#define __EditorCameraGizmo_h__24_7_2026__12_00_00__

// The Scene panel's frustum gizmo for a selected camera object: a pure,
// headless-unit-tested builder that turns a CameraComponent's reflected
// projection state into a line-list wireframe (drawn through the same facade
// line-mesh path as the reference grid). Perspective -> a truncated frustum,
// orthographic -> the view box (honouring the 2D fit policy via the shared
// CameraFit math). Kept pure (no engine node/mesh handling) so the geometry is
// asserted in tests/editor_core; the panel-side node/mesh plumbing lives in
// EditorScenePanel.cpp.

#include <engine_render/RenderMath.h>	// Vec3, Color
#include <core_util/CameraFit.h>		// orthographic half-height (2D fit policy)
#include <core_util/String.h>

#include <cmath>
#include <vector>

namespace Orkige
{
	//! @brief the reflected camera projection state the frustum gizmo needs.
	//! projectionMode / fitMode are the CameraComponent enum values as ints
	//! (PM_ORTHOGRAPHIC == 1; FitMode mirrors CameraFit::FitMode 0/1/2).
	struct CameraFrustumParams
	{
		int		projectionMode = 0;		//!< 0 = perspective, 1 = orthographic
		float	orthoSize = 5.0f;		//!< orthographic vertical half-extent
		int		fitMode = 0;			//!< 2D aspect policy (CameraFit::FitMode)
		float	designWidth = 10.0f;	//!< design rect full width (FIT_WIDTH/EXPAND)
		float	designHeight = 10.0f;	//!< design rect full height (EXPAND)
	};

	//! the frustum gizmo colour - a neutral white-ish tint that reads against
	//! the grey reference grid without competing with the axis colours
	inline Color editorCameraFrustumColour()
	{
		return Color(0.85f, 0.85f, 0.92f);
	}

	//! representative vertical field of view (degrees) for the perspective
	//! frustum gizmo. CameraComponent does not author a FOV - the runtime
	//! perspective camera keeps the window camera's default - so the gizmo
	//! draws a sensible representative cone.
	inline float editorCameraFrustumFovDegrees()
	{
		return 45.0f;
	}

	//! near/far depths (world units) the gizmo draws to - small, editor-visible
	//! values (NOT the runtime clip planes, which would be far too large to see)
	inline float editorCameraFrustumNear() { return 0.3f; }
	inline float editorCameraFrustumFar()  { return 5.0f; }

	//! @brief build the camera frustum/box wireframe in the camera's LOCAL space
	//! (the camera looks down local -Z, the engine convention), as consecutive
	//! point PAIRS (one segment each) ready for RenderWorld::createLineListMesh.
	//! @param params the reflected projection state
	//! @param aspect the viewport aspect (width/height) the width is derived from
	//! @param outPoints  receives 24 points (12 segments), cleared first
	//! @param outColours receives one colour per point (same colour throughout)
	//! @remarks 12 segments both modes: near rect (4) + far rect (4) + the 4
	//! side edges connecting them. Perspective grows the far rect with depth;
	//! orthographic keeps both rects the same size (a box).
	inline void buildCameraFrustumLines(CameraFrustumParams const& params,
		float aspect, std::vector<Vec3>& outPoints,
		std::vector<Color>& outColours)
	{
		outPoints.clear();
		outColours.clear();
		const float safeAspect = aspect > 1.0e-4f ? aspect : 1.0f;
		const float nearZ = editorCameraFrustumNear();
		const float farZ = editorCameraFrustumFar();

		float halfHNear = 0.0f;
		float halfWNear = 0.0f;
		float halfHFar = 0.0f;
		float halfWFar = 0.0f;
		if (params.projectionMode == 1)	// PM_ORTHOGRAPHIC: a constant-size box
		{
			float halfH = params.orthoSize;
			if (params.fitMode != static_cast<int>(CameraFit::FIT_HEIGHT))
			{
				halfH = CameraFit::orthoHalfHeight(
					static_cast<CameraFit::FitMode>(params.fitMode),
					params.designWidth, params.designHeight, safeAspect);
			}
			const float halfW = halfH * safeAspect;
			halfHNear = halfHFar = halfH;
			halfWNear = halfWFar = halfW;
		}
		else	// PM_PERSPECTIVE: a truncated cone growing with depth
		{
			const float pi = 3.14159265358979323846f;
			const float halfFovRad = 0.5f *
				editorCameraFrustumFovDegrees() * pi / 180.0f;
			const float tanHalf = std::tan(halfFovRad);
			halfHNear = nearZ * tanHalf;
			halfWNear = halfHNear * safeAspect;
			halfHFar = farZ * tanHalf;
			halfWFar = halfHFar * safeAspect;
		}

		// four corners of a plane at depth z (top-left, top-right, bottom-right,
		// bottom-left), looking down -Z so the plane sits at negative z
		auto planeCorners = [](float halfW, float halfH, float z,
			Vec3 out[4])
		{
			out[0] = Vec3(-halfW,  halfH, -z);	// top-left
			out[1] = Vec3( halfW,  halfH, -z);	// top-right
			out[2] = Vec3( halfW, -halfH, -z);	// bottom-right
			out[3] = Vec3(-halfW, -halfH, -z);	// bottom-left
		};
		Vec3 nearC[4];
		Vec3 farC[4];
		planeCorners(halfWNear, halfHNear, nearZ, nearC);
		planeCorners(halfWFar, halfHFar, farZ, farC);

		const Color colour = editorCameraFrustumColour();
		auto segment = [&](Vec3 const& a, Vec3 const& b)
		{
			outPoints.push_back(a);
			outPoints.push_back(b);
			outColours.push_back(colour);
			outColours.push_back(colour);
		};
		for (int i = 0; i < 4; ++i)
		{
			segment(nearC[i], nearC[(i + 1) % 4]);	// near rectangle
			segment(farC[i], farC[(i + 1) % 4]);	// far rectangle
			segment(nearC[i], farC[i]);				// side edge
		}
	}

	//! @brief number of line SEGMENTS the frustum gizmo emits (both modes) - the
	//! selfcheck asserts the panel actually built the gizmo against this.
	inline int editorCameraFrustumSegmentCount() { return 12; }

	//--- panel-side gizmo state (defined in EditorScenePanel.cpp) --------------
	//! @brief the object id whose camera frustum the Scene panel last drew, or ""
	//! when no camera-carrying object is selected. The editor camera selfcheck
	//! reads this to prove the gizmo drew for the selected camera.
	String const& editorSceneCameraGizmoObjectId();
	//! @brief line vertices the Scene panel last uploaded for the frustum gizmo
	//! (0 when inactive) - a non-pixel "the gizmo drew" seam for the selfcheck.
	std::size_t editorSceneCameraGizmoVertexCount();
	//! @brief drop the frustum gizmo's persistent render node + mesh instance.
	//! MUST be called before the render system is torn down (the gizmo state is
	//! process-lifetime, so its facade handles would otherwise destruct after the
	//! backend is gone). Safe to call when the gizmo was never created.
	void editorSceneCameraGizmoRelease();
}

#endif //__EditorCameraGizmo_h__24_7_2026__12_00_00__
