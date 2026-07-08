/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderSystemNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderSystemNext.cpp
//! @brief Ogre-Next implementation of the RenderSystem facade
//! @remarks REAL at B1: frame loop, window camera/background (through
//! the compositor workspace - Next renders nothing without one), window
//! size/resize, screenshots, resource locations, fps stats. Stubs:
//! createRenderTexture (B2 workspace-per-target), Zip/BigZip locations.

#include "engine_render_next/NextBackend.h"

#include <OgreRoot.h>
#include <OgreWindow.h>
#include <OgreTextureGpu.h>
#include <OgreImage2.h>
#include <OgreCamera.h>
#include <OgreRenderSystem.h>
#include <OgreResourceGroupManager.h>

namespace Orkige
{
	//---------------------------------------------------------
	RenderSystem::FrameStats::FrameStats()
		: lastFPS(0.0f)
		, avgFPS(0.0f)
		, triangleCount(0)
		, batchCount(0)
	{
	}
	//---------------------------------------------------------
	RenderSystem::RenderSystem()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderSystem::~RenderSystem()
	{
		this->mImpl->windowCamera.reset();
		delete this->mImpl->world;
		// the workspace is owned by CompositorManager2 and dies with the
		// root (RenderBackend::destroyRenderSystem tears that down)
		delete this->mImpl;
	}
	//---------------------------------------------------------
	RenderSystem* RenderSystem::get()
	{
		return RenderBackend::system();
	}
	//---------------------------------------------------------
	bool RenderSystem::renderOneFrame()
	{
		// per-frame fps bookkeeping - Next has no per-target getStatistics
		const std::chrono::steady_clock::time_point now =
			std::chrono::steady_clock::now();
		if(this->mImpl->haveLastFrameTime)
		{
			const float deltaSeconds =
				std::chrono::duration<float>(now -
					this->mImpl->lastFrameTime).count();
			if(deltaSeconds > 0.0f)
			{
				this->mImpl->lastFPS = 1.0f / deltaSeconds;
				this->mImpl->avgFPS = this->mImpl->avgFPS == 0.0f
					? this->mImpl->lastFPS
					: this->mImpl->avgFPS * 0.95f +
						this->mImpl->lastFPS * 0.05f;
			}
		}
		this->mImpl->lastFrameTime = now;
		this->mImpl->haveLastFrameTime = true;
		return this->mImpl->root->renderOneFrame();
	}
	//---------------------------------------------------------
	void RenderSystem::showCameraOnWindow(optr<RenderCamera> const & camera)
	{
		oAssert(RenderBackend::ogreCamera(camera));
		this->mImpl->windowCamera = camera;
		RenderBackend::recreateWindowWorkspace();
	}
	//---------------------------------------------------------
	optr<RenderCamera> RenderSystem::getWindowCamera() const
	{
		return this->mImpl->windowCamera;
	}
	//---------------------------------------------------------
	void RenderSystem::setWindowBackgroundColour(Color const & colour)
	{
		this->mImpl->windowBackground = colour;
		if(this->mImpl->workspace)
		{
			// the clear colour is baked into the workspace definition's
			// clear pass - rebuild it (cheap, editor-frequency operation)
			RenderBackend::recreateWindowWorkspace();
		}
	}
	//---------------------------------------------------------
	void RenderSystem::getWindowSize(unsigned int & outWidth,
		unsigned int & outHeight) const
	{
		outWidth = this->mImpl->window->getWidth();
		outHeight = this->mImpl->window->getHeight();
	}
	//---------------------------------------------------------
	void RenderSystem::notifyWindowResized()
	{
		this->mImpl->window->windowMovedOrResized();
		Ogre::Camera* backendCamera =
			RenderBackend::ogreCamera(this->mImpl->windowCamera);
		if(backendCamera && this->mImpl->window->getHeight() > 0)
		{
			backendCamera->setAspectRatio(
				Ogre::Real(this->mImpl->window->getWidth()) /
				Ogre::Real(this->mImpl->window->getHeight()));
		}
	}
	//---------------------------------------------------------
	void RenderSystem::saveWindowContents(String const & fileName) const
	{
		// the upstream-documented Metal screenshot recipe (OgreWindow.h,
		// setManualSwapRelease): after a normal swap the drawable is gone,
		// so hold the swap of ONE extra frame, download, then release.
		// setWantsToDownload(true) already happened at boot.
		Ogre::Window* window = this->mImpl->window;
		window->setManualSwapRelease(true);
		this->mImpl->root->renderOneFrame();
		if(!window->canDownloadData())
		{
			window->performManualRelease();
			window->setManualSwapRelease(false);
			RenderBackend::notImplementedOnce(
				"RenderSystem::saveWindowContents (window backbuffer not downloadable)");
			return;
		}
		Ogre::Image2 image;
		Ogre::TextureGpu* texture = window->getTexture();
		image.convertFromTexture(texture, 0, texture->getNumMipmaps() - 1);
		window->performManualRelease();
		window->setManualSwapRelease(false);
		image.save(fileName, 0, image.getNumMipmaps());
	}
	//---------------------------------------------------------
	optr<RenderTexture> RenderSystem::createRenderTexture(String const & name,
		unsigned int width, unsigned int height)
	{
		(void)name; (void)width; (void)height;
		RenderBackend::notImplementedOnce("RenderSystem::createRenderTexture");
		return optr<RenderTexture>();
	}
	//---------------------------------------------------------
	RenderWorld* RenderSystem::getWorld() const
	{
		return this->mImpl->world;
	}
	//---------------------------------------------------------
	void RenderSystem::addResourceLocation(String const & path,
		LocationType type, String const & groupName, bool recursive)
	{
		if(type != LT_FILESYSTEM)
		{
			// Zip needs the zziplib-backed archive (port feature, B2);
			// BigZip needs engine_filesystem ported off the classic
			// umbrella (B2) - both wait for real content work
			RenderBackend::notImplementedOnce(type == LT_ZIP
				? "RenderSystem::addResourceLocation(LT_ZIP)"
				: "RenderSystem::addResourceLocation(LT_BIGZIP)");
			return;
		}
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			path, "FileSystem",
			groupName.empty()
				? Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME
				: groupName,
			recursive);
	}
	//---------------------------------------------------------
	void RenderSystem::initialiseResourceGroups()
	{
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups(
			false /*changeLocaleTemporarily*/);
	}
	//---------------------------------------------------------
	void RenderSystem::removeResourceLocation(String const & path,
		String const & groupName)
	{
		// idempotent by contract (same probing as the classic backend)
		Ogre::ResourceGroupManager & resourceGroups =
			Ogre::ResourceGroupManager::getSingleton();
		const String group = groupName.empty()
			? String(Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)
			: groupName;
		if(resourceGroups.resourceGroupExists(group) &&
			resourceGroups.resourceLocationExists(path, group))
		{
			resourceGroups.removeResourceLocation(path, group);
		}
	}
	//---------------------------------------------------------
	bool RenderSystem::resourceGroupExists(String const & groupName) const
	{
		return Ogre::ResourceGroupManager::getSingleton()
			.resourceGroupExists(groupName);
	}
	//---------------------------------------------------------
	void RenderSystem::destroyResourceGroup(String const & groupName)
	{
		Ogre::ResourceGroupManager & resourceGroups =
			Ogre::ResourceGroupManager::getSingleton();
		if(resourceGroups.resourceGroupExists(groupName))
		{
			resourceGroups.destroyResourceGroup(groupName);
		}
	}
	//---------------------------------------------------------
	bool RenderSystem::resourceExists(String const & resourceName,
		String const & groupName) const
	{
		return Ogre::ResourceGroupManager::getSingleton().resourceExists(
			groupName.empty()
				? Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME
				: groupName,
			resourceName);
	}
	//---------------------------------------------------------
	RenderSystem::FrameStats RenderSystem::getFrameStats() const
	{
		FrameStats stats;
		stats.lastFPS = this->mImpl->lastFPS;
		stats.avgFPS = this->mImpl->avgFPS;
		Ogre::RenderingMetrics const & metrics =
			this->mImpl->root->getRenderSystem()->getMetrics();
		stats.triangleCount = metrics.mFaceCount;
		stats.batchCount = metrics.mBatchCount;
		return stats;
	}
}
