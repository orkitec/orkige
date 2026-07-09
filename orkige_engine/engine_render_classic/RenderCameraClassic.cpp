/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	RenderCameraClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderCameraClassic.cpp
//! @brief classic-OGRE implementation of the RenderCamera facade
//! @remarks wraps an Ogre::Camera placed via its RenderNode; projection
//! defaults mirror Engine::createDefaultCameraAndViewport (near 1, far
//! 100000, perspective) until setPerspective/setOrthographic runs

#include "engine_render_classic/ClassicBackend.h"

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	optr<RenderCamera> RenderBackend::createCamera(
		Ogre::SceneManager* sceneManager, String const & name)
	{
		oAssert(sceneManager);
		Ogre::Camera* camera = sceneManager->createCamera(name.empty()
			? RenderBackend::generateName("RenderFacade/Camera") : name);
		camera->setNearClipDistance(1.0f);
		camera->setFarClipDistance(100000.0f);
		camera->setAutoAspectRatio(false);	// targets push the aspect explicitly
		optr<RenderCamera> handle(new RenderCamera());
		handle->mImpl->camera = camera;
		handle->mImpl->creator = sceneManager;
		return handle;
	}
	//---------------------------------------------------------
	optr<RenderCamera> RenderBackend::wrapCamera(Ogre::Camera* camera,
		bool owned)
	{
		oAssert(camera);
		optr<RenderCamera> handle(new RenderCamera());
		handle->mImpl->camera = camera;
		handle->mImpl->creator = camera->getSceneManager();
		handle->mImpl->owned = owned;
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
		// late destruction guard: script-held handles may outlive
		// the render system - see ~RenderNode
		if(this->mImpl->camera && this->mImpl->owned && RenderBackend::system())
		{
			if(this->mImpl->camera->isAttached())
			{
				this->mImpl->camera->detachFromParent();
			}
			this->mImpl->creator->destroyCamera(this->mImpl->camera);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderCamera::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->camera->isAttached())
		{
			this->mImpl->camera->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->camera);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void RenderCamera::detach()
	{
		if(this->mImpl->camera->isAttached())
		{
			this->mImpl->camera->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderCamera::getNode() const
	{
		if(this->mImpl->attachedTo)
		{
			return this->mImpl->attachedTo;	// the facade attach path
		}
		// wrapped cameras (RenderBackend::wrapCamera) were placed by their
		// creator - back-map the backend parent node through the registry
		// (NULL for non-facade nodes, e.g. the legacy Engine camera path)
		if(this->mImpl->camera->isAttached())
		{
			return RenderBackend::findNode(static_cast<Ogre::SceneNode*>(
				this->mImpl->camera->getParentNode()));
		}
		return optr<RenderNode>();
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
		// height only - the width follows the camera's aspect ratio (the
		// same rule Engine::setCameraOrthographic applies today)
		this->mImpl->camera->setOrthoWindowHeight(
			std::max(verticalHalfExtent, Real(0.001)) * Real(2.0));
		this->mImpl->camera->setNearClipDistance(nearClip);
		this->mImpl->camera->setFarClipDistance(farClip);
	}
	//---------------------------------------------------------
	RenderCamera::ProjectionType RenderCamera::getProjectionType() const
	{
		return this->mImpl->camera->getProjectionType() == Ogre::PT_ORTHOGRAPHIC
			? PT_ORTHOGRAPHIC : PT_PERSPECTIVE;
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
		// clip-space projection + divide, normalized to 0..1 with y down
		// (what getCameraToViewportRay expects back) - the editor's
		// worldToViewportNormalized moved behind the facade
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
		// documented no-op on Filament (by design); classic and
		// Next both have the polygon-mode toggle
		this->mImpl->camera->setPolygonMode(
			enabled ? Ogre::PM_WIREFRAME : Ogre::PM_SOLID);
	}
}
