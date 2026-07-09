/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderCameraNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderCameraNext.cpp
//! @brief Ogre-Next implementation of the RenderCamera facade
//! @remarks creation, node placement, projection setup, viewport rays,
//! project-point, view/projection matrices. setWireframe: the v2
//! Camera lost the per-camera polygon-mode toggle, so the decision
//! is the Hlms macroblock road - RenderBackend::setGlobalWireframe
//! flips the polygon mode of every backend-generated datablock, i.e.
//! wireframe is GLOBAL (not per-camera) on this backend. That matches
//! every live call site (a debug-view toggle) and is recorded in
//! RenderCamera.h + Docs/render-abstraction.md.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreCamera.h>

namespace Orkige
{
	//---------------------------------------------------------
	optr<RenderCamera> RenderBackend::createCamera(
		Ogre::SceneManager* sceneManager, String const & name)
	{
		oAssert(sceneManager);
		optr<RenderCamera> handle(new RenderCamera());
		handle->mImpl->camera = sceneManager->createCamera(name);
		handle->mImpl->creator = sceneManager;
		handle->mImpl->owned = true;
		// same defaults the classic backend applies
		handle->mImpl->camera->setNearClipDistance(0.1f);
		handle->mImpl->camera->setFarClipDistance(10000.0f);
		handle->mImpl->camera->setAutoAspectRatio(false);
		return handle;
	}
	//---------------------------------------------------------
	RenderCamera::RenderCamera()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderCamera::~RenderCamera()
	{
		// late destruction guard, same rule as RenderNode
		if(this->mImpl->camera && this->mImpl->owned &&
			RenderBackend::system())
		{
			this->mImpl->camera->detachFromParent();
			this->mImpl->creator->destroyCamera(this->mImpl->camera);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderCamera::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		// v2 cameras are born attached (to a scene-manager-internal node);
		// detach from whatever holds it, then bind to the facade node
		this->mImpl->camera->detachFromParent();
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->camera);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void RenderCamera::detach()
	{
		this->mImpl->camera->detachFromParent();
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderCamera::getNode() const
	{
		return this->mImpl->attachedTo;
	}
	//---------------------------------------------------------
	void RenderCamera::setPerspective(Degree const & fovY, Real nearClip,
		Real farClip)
	{
		this->mImpl->camera->setProjectionType(Ogre::PT_PERSPECTIVE);
		this->mImpl->camera->setFOVy(fovY);
		this->mImpl->camera->setNearClipDistance(nearClip);
		this->mImpl->camera->setFarClipDistance(farClip);
	}
	//---------------------------------------------------------
	void RenderCamera::setOrthographic(Real verticalHalfExtent, Real nearClip,
		Real farClip)
	{
		this->mImpl->camera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
		this->mImpl->camera->setOrthoWindowHeight(verticalHalfExtent * 2);
		this->mImpl->camera->setNearClipDistance(nearClip);
		this->mImpl->camera->setFarClipDistance(farClip);
	}
	//---------------------------------------------------------
	RenderCamera::ProjectionType RenderCamera::getProjectionType() const
	{
		return this->mImpl->camera->getProjectionType()
			== Ogre::PT_ORTHOGRAPHIC ? PT_ORTHOGRAPHIC : PT_PERSPECTIVE;
	}
	//---------------------------------------------------------
	void RenderCamera::setFOVy(Degree const & fovY)
	{
		this->mImpl->camera->setFOVy(fovY);
	}
	//---------------------------------------------------------
	Degree RenderCamera::getFOVy() const
	{
		return Degree(this->mImpl->camera->getFOVy());
	}
	//---------------------------------------------------------
	void RenderCamera::setAspectRatio(Real aspect)
	{
		this->mImpl->camera->setAspectRatio(aspect);
	}
	//---------------------------------------------------------
	Real RenderCamera::getNearClip() const
	{
		return this->mImpl->camera->getNearClipDistance();
	}
	//---------------------------------------------------------
	Real RenderCamera::getFarClip() const
	{
		return this->mImpl->camera->getFarClipDistance();
	}
	//---------------------------------------------------------
	Ray3 RenderCamera::viewportPointToRay(Real nx, Real ny) const
	{
		return this->mImpl->camera->getCameraToViewportRay(nx, ny);
	}
	//---------------------------------------------------------
	bool RenderCamera::projectPoint(Vec3 const & worldPoint, Real & outNx,
		Real & outNy) const
	{
		// identical math to the classic backend (facade contract)
		const Ogre::Vector4 clip = this->mImpl->camera->getProjectionMatrix() *
			(this->mImpl->camera->getViewMatrix() *
				Ogre::Vector4(worldPoint.x, worldPoint.y, worldPoint.z, 1.0f));
		if(clip.w <= 0.0f)
		{
			return false;	// behind the camera
		}
		outNx = clip.x / clip.w * 0.5f + 0.5f;
		outNy = 1.0f - (clip.y / clip.w * 0.5f + 0.5f);
		return true;
	}
	//---------------------------------------------------------
	Mat4 RenderCamera::getViewMatrix() const
	{
		return this->mImpl->camera->getViewMatrix();
	}
	//---------------------------------------------------------
	Mat4 RenderCamera::getProjectionMatrix() const
	{
		return this->mImpl->camera->getProjectionMatrix();
	}
	//---------------------------------------------------------
	void RenderCamera::setWireframe(bool enabled)
	{
		// GLOBAL on this backend (macroblock polygon mode across all
		// backend-generated datablocks) - see the file remarks
		RenderBackend::setGlobalWireframe(enabled);
	}
}
