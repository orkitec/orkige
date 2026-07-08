/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderCamera.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderCamera_h__8_7_2026__12_00_00__
#define __RenderCamera_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief a camera: projection setup, placement via RenderNode and the
	//! screen<->world conversions the editor and picking need
	//! @remarks A camera renders when a target shows it: the main window
	//! (RenderSystem::showCameraOnWindow) or a RenderTexture::setCamera.
	//! Viewport state (background colour, overlays) lives on the target,
	//! not here - the same camera can feed several targets.
	//!
	//! Backend mapping (whole class): classic = Ogre::Camera (attached to a
	//! SceneNode); next = Ogre::Camera (v2 - camera IS a node child, impl
	//! hides the difference); filament = filament::Camera on an entity +
	//! per-View projection.
	class ORKIGE_ENGINE_DLL RenderCamera
	{
		//--- Types -------------------------------------------------
	public:
		//! projection selection (2D games use PT_ORTHOGRAPHIC)
		enum ProjectionType
		{
			PT_PERSPECTIVE = 0,	//!< classic 3D frustum (default)
			PT_ORTHOGRAPHIC		//!< parallel projection sized by ortho half-extent
		};
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend camera guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the backend camera
		~RenderCamera();

		//--- placement ---
		//! @brief attach to a node (cameras are placed via their node,
		//! like Engine::createDefaultCameraAndViewport does today)
		//! map: classic=SceneNode::attachObject | next=camera parented to node | filament=TransformManager parent of camera entity
		void attachTo(optr<RenderNode> const & node);
		//! map: classic/next=detach from node | filament=unparent
		void detach();

		//--- projection ---
		//! @brief perspective projection (the 3D default)
		//! map: classic/next=Camera::setProjectionType+setFOVy+clip distances | filament=Camera::setProjection(fov,...)
		void setPerspective(Degree const & fovY, Real nearClip, Real farClip);
		//! @brief orthographic projection for 2D: verticalHalfExtent world
		//! units from view center to the top edge, width follows the aspect
		//! (Engine::setCameraOrthographic / CameraComponent::setOrthoSize)
		//! map: classic/next=Camera::setProjectionType(PT_ORTHOGRAPHIC)+setOrthoWindowHeight | filament=Camera::setProjection(ORTHO,...)
		void setOrthographic(Real verticalHalfExtent, Real nearClip, Real farClip);
		//! current projection type
		ProjectionType getProjectionType() const;
		//! map: classic/next=Camera::setFOVy | filament=recompute projection
		void setFOVy(Degree const & fovY);
		//! map: classic/next=Camera::getFOVy | filament=cached facade value
		Degree getFOVy() const;
		//! @brief width/height of the target this camera renders to; targets
		//! set it automatically on (re)size - call only for special cases
		//! map: classic/next=Camera::setAspectRatio | filament=recompute projection
		void setAspectRatio(Real aspect);

		//--- screen <-> world (picking, gizmos, HUD anchoring) ---
		//! @brief world ray through a viewport point (normalized 0..1, y down)
		//! map: classic/next=Camera::getCameraToViewportRay | filament=inverse view-proj unproject
		Ray3 viewportPointToRay(Real nx, Real ny) const;
		//! @brief project a world point to normalized viewport coords
		//! @return false when the point is behind the camera
		//! map: classic/next=proj*view*point + clip-space divide (editor worldToViewportNormalized) | filament=same math via Camera matrices
		bool projectPoint(Vec3 const & worldPoint, Real & outNx, Real & outNy) const;
		//! view matrix (ImGuizmo feeds on these)
		//! map: classic/next=Camera::getViewMatrix | filament=Camera::getViewMatrix
		Mat4 getViewMatrix() const;
		//! projection matrix
		//! map: classic/next=Camera::getProjectionMatrix | filament=Camera::getProjectionMatrix
		Mat4 getProjectionMatrix() const;

		//--- debug ---
		//! @brief wireframe rendering for everything this camera shows
		//! (Engine::enable/disableWireframeMode successor)
		//! map: classic/next=Camera::setPolygonMode(PM_WIREFRAME/PM_SOLID) | filament=no direct equivalent (impl may no-op; flagged in doc)
		void setWireframe(bool enabled);
	protected:
		//! cameras are created by RenderWorld::createCamera only
		RenderCamera();
	private:
		RenderCamera(RenderCamera const &);					// non-copyable
		RenderCamera & operator=(RenderCamera const &);		// non-copyable
	};
}

#endif //__RenderCamera_h__8_7_2026__12_00_00__
