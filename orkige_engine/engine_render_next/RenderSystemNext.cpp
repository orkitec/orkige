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
//! @remarks frame loop, window camera/background (through the
//! compositor workspace - Next renders nothing without one), window
//! size/resize, screenshots, RTT creation (RenderTextureNext.cpp),
//! resource locations, stats (fps facade-side, triangles/batches from
//! the RenderingMetrics enabled at boot). Honest gap, logged once:
//! Zip/BigZip resource locations (waits for real content work).

#include "engine_render_next/NextBackend.h"

#include <OgreRoot.h>
#include <OgreWindow.h>
#include <OgreTextureGpu.h>
#include <OgreImage2.h>
#include <OgreCamera.h>
#include <OgreRenderSystem.h>
#include <OgreResourceGroupManager.h>
#include <OgreDataStream.h>

namespace Orkige
{
	//---------------------------------------------------------
	RenderSystem::FrameStats::FrameStats()
		: lastFPS(0.0f)
		, avgFPS(0.0f)
		, bestFPS(0.0f)
		, worstFPS(0.0f)
		, triangleCount(0)
		, batchCount(0)
		, textureMemoryBytes(0)
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
		if(this->mImpl->lastFPS > 0.0f)
		{
			this->mImpl->bestFPS =
				std::max(this->mImpl->bestFPS, this->mImpl->lastFPS);
			this->mImpl->worstFPS =
				std::min(this->mImpl->worstFPS, this->mImpl->lastFPS);
		}
		// 2D layer upkeep: UI camera follows window resizes, painter
		// depths reassign after layer/batch changes (DrawLayer2DNext.cpp)
		RenderBackend::updateDrawLayer2DFrame();
		// the Metal RS never passes through _beginFrameOnce's per-frame
		// metrics reset, so RenderingMetrics accumulate forever - reset
		// here so getFrameStats reports the LAST frame (classic semantics)
		this->mImpl->root->getRenderSystem()->_resetMetrics();
		return this->mImpl->root->renderOneFrame();
	}
	//---------------------------------------------------------
	void RenderSystem::showCameraOnWindow(optr<RenderCamera> const & camera)
	{
		oAssert(RenderBackend::ogreCamera(camera));
		this->mImpl->windowCamera = camera;
		this->mImpl->uiOnlyWindow = false;
		RenderBackend::recreateWindowWorkspace();
	}
	//---------------------------------------------------------
	void RenderSystem::showUIOnlyWindow()
	{
		// the editor-shell mode: the window workspace becomes one clear +
		// 2D-layer pass (see recreateWindowWorkspace) - no scene camera,
		// getWindowCamera answers NULL by contract
		this->mImpl->windowCamera.reset();
		this->mImpl->uiOnlyWindow = true;
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
		this->mImpl->root->getRenderSystem()->_resetMetrics();	// see renderOneFrame
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
		RenderBackend::makeImageAlphaOpaque(image);
		image.save(fileName, 0, image.getNumMipmaps());
	}
	//---------------------------------------------------------
	optr<RenderTexture> RenderSystem::createRenderTexture(String const & name,
		unsigned int width, unsigned int height)
	{
		return RenderBackend::createRenderTexture(name, width, height);
	}
	//---------------------------------------------------------
	optr<DrawLayer2D> RenderSystem::createDrawLayer2D(int zOrder)
	{
		return RenderBackend::createDrawLayer2D(zOrder);
	}
	//---------------------------------------------------------
	bool RenderSystem::getTextureSize(String const & textureName,
		unsigned int & outWidth, unsigned int & outHeight) const
	{
		outWidth = outHeight = 0;
		Ogre::TextureGpu* texture = RenderBackend::loadTexture2D(textureName);
		if(!texture)
		{
			return false;	// failure already logged by loadTexture2D
		}
		outWidth = texture->getWidth();
		outHeight = texture->getHeight();
		return true;
	}
	//---------------------------------------------------------
	bool RenderSystem::createTexture2D(String const & name,
		unsigned char const * rgbaPixels,
		unsigned int width, unsigned int height)
	{
		return RenderBackend::createTexture2DFromPixels(name, rgbaPixels,
			width, height) != NULL;
	}
	//---------------------------------------------------------
	void RenderSystem::destroyTexture2D(String const & name)
	{
		RenderBackend::destroyTexture2DByName(name);
	}
	//---------------------------------------------------------
	bool RenderSystem::createMaterial(String const & name,
		RenderMaterialDesc const & desc)
	{
		oAssert(!name.empty());
		// every field of the description is native on this backend (metallic
		// workflow); a missing texture is skipped + logged (-> false)
		bool complete = true;
		return RenderBackend::createOrUpdatePbsDatablock(name, desc,
			complete) != NULL && complete;
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
			// Zip needs the zziplib-backed archive (port feature);
			// BigZip needs engine_filesystem ported off the classic
			// umbrella - both wait for real content work
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
	bool RenderSystem::readResourceText(String const & resourceName,
		String & outText) const
	{
		try
		{
			Ogre::DataStreamPtr stream =
				Ogre::ResourceGroupManager::getSingleton().openResource(
					resourceName,
					Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
			if(!stream)
			{
				return false;
			}
			outText = stream->getAsString();
			return true;
		}
		catch(Ogre::Exception const &)
		{
			return false;	// not found in any group - honest miss
		}
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
		// v2 draws count into mDrawCount (mBatchCount is the v1 path) -
		// draw calls ARE the classic "batches" notion
		stats.batchCount = metrics.mBatchCount + metrics.mDrawCount;
		stats.bestFPS = this->mImpl->bestFPS;
		stats.worstFPS =
			this->mImpl->worstFPS >= 999999.0f ? 0.0f : this->mImpl->worstFPS;
		// no cheap texture-memory total on this backend yet - honest 0
		// (the field exists for the classic stats HUD parity)
		stats.textureMemoryBytes = 0;
		return stats;
	}
	//---------------------------------------------------------
	void RenderSystem::resetFrameStats()
	{
		this->mImpl->bestFPS = 0.0f;
		this->mImpl->worstFPS = 999999.0f;
	}
}
