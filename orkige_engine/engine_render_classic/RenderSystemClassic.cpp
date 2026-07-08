/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	RenderSystemClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderSystemClassic.cpp
//! @brief classic-OGRE implementation of the RenderSystem facade
//! @remarks RenderSystem WRAPS the existing Engine (the classic
//! bootstrapper keeps owning root/window/config plumbing - kept working
//! because everything still calls it until WP-A1.3+); the facade adds
//! the services apps used to reach raw Ogre for (Docs/render-abstraction.md
//! bucket B). Engine::setup creates this via RenderBackend::createRenderSystem.

#include "engine_render_classic/ClassicBackend.h"
#include "engine_graphic/Engine.h"
#include <core_debug/DebugMacros.h>

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
		return this->mImpl->engine->renderOneFrame();
	}
	//---------------------------------------------------------
	void RenderSystem::showCameraOnWindow(optr<RenderCamera> const & camera)
	{
		Ogre::Camera* backendCamera = RenderBackend::ogreCamera(camera);
		oAssert(backendCamera);
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		// "replaces the previous window camera": one full-window viewport
		// (single-window/single-viewport is frozen by design decision #7).
		// NOTE for the migration window: this drops a viewport created via
		// Engine::createDefaultCameraAndViewport - an app uses the Engine
		// path OR the facade path for the window camera, never both.
		window->removeAllViewports();
		Ogre::Viewport* viewport = window->addViewport(backendCamera);
		viewport->setBackgroundColour(this->mImpl->windowBackground);
		viewport->setShadowsEnabled(true);
		RenderBackend::applyRTSSScheme(viewport);
		backendCamera->setAspectRatio(
			Ogre::Real(viewport->getActualWidth()) /
			Ogre::Real(viewport->getActualHeight()));
		this->mImpl->windowCamera = camera;
		this->mImpl->uiOnlyWindow = false;
	}
	//---------------------------------------------------------
	void RenderSystem::showUIOnlyWindow()
	{
		// the editor-shell mode: the window carries one full-window viewport
		// whose visibility mask hides ALL scene content - only the clear
		// colour and the DrawLayer2D composition (a RenderQueueListener,
		// mask-independent) reach the screen. A viewport needs a camera, so
		// an internal one feeds it; it never sees content and is never
		// handed out (getWindowCamera answers NULL in this mode).
		Ogre::SceneManager* sceneManager =
			this->mImpl->engine->getSceneManager();
		if(!this->mImpl->uiOnlyCamera)
		{
			this->mImpl->uiOnlyCamera = sceneManager->createCamera(
				RenderBackend::generateName("Orkige/UIOnlyCamera"));
			sceneManager->getRootSceneNode()->createChildSceneNode()
				->attachObject(this->mImpl->uiOnlyCamera);
		}
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		window->removeAllViewports();
		Ogre::Viewport* viewport =
			window->addViewport(this->mImpl->uiOnlyCamera);
		viewport->setBackgroundColour(this->mImpl->windowBackground);
		viewport->setVisibilityMask(0);	// 2D layers only - no scene objects
		viewport->setShadowsEnabled(false);
		RenderBackend::applyRTSSScheme(viewport);
		this->mImpl->windowCamera.reset();
		this->mImpl->engineWindowCamera.reset();
		this->mImpl->uiOnlyWindow = true;
	}
	//---------------------------------------------------------
	optr<RenderCamera> RenderSystem::getWindowCamera() const
	{
		if(this->mImpl->uiOnlyWindow)
		{
			// UI-only mode shows no scene camera by contract (the internal
			// viewport camera is plumbing, not scene surface)
			return optr<RenderCamera>();
		}
		if(this->mImpl->windowCamera)
		{
			return this->mImpl->windowCamera;	// the facade path
		}
		// the Engine path (every app until WP-A1.3): wrap the camera the
		// window viewport shows - non-owning, Engine keeps destroying it
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		if(window->getNumViewports() == 0)
		{
			return optr<RenderCamera>();	// no camera shown yet
		}
		Ogre::Camera* backendCamera = window->getViewport(0)->getCamera();
		if(!backendCamera)
		{
			return optr<RenderCamera>();
		}
		if(RenderBackend::ogreCamera(this->mImpl->engineWindowCamera)
			!= backendCamera)
		{
			this->mImpl->engineWindowCamera =
				RenderBackend::wrapCamera(backendCamera, false /*owned*/);
		}
		return this->mImpl->engineWindowCamera;
	}
	//---------------------------------------------------------
	void RenderSystem::setWindowBackgroundColour(Color const & colour)
	{
		this->mImpl->windowBackground = colour;
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		if(window->getNumViewports() > 0)
		{
			window->getViewport(0)->setBackgroundColour(colour);
		}
	}
	//---------------------------------------------------------
	void RenderSystem::getWindowSize(unsigned int & outWidth,
		unsigned int & outHeight) const
	{
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		if(window->getNumViewports() > 0)
		{
			outWidth = static_cast<unsigned int>(
				window->getViewport(0)->getActualWidth());
			outHeight = static_cast<unsigned int>(
				window->getViewport(0)->getActualHeight());
			return;
		}
		outWidth = window->getWidth();
		outHeight = window->getHeight();
	}
	//---------------------------------------------------------
	void RenderSystem::notifyWindowResized()
	{
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		window->windowMovedOrResized();
		// keep the shown camera's projection in step with the new drawable
		Ogre::Camera* backendCamera =
			RenderBackend::ogreCamera(this->mImpl->windowCamera);
		if(backendCamera && window->getNumViewports() > 0)
		{
			Ogre::Viewport* viewport = window->getViewport(0);
			backendCamera->setAspectRatio(
				Ogre::Real(viewport->getActualWidth()) /
				Ogre::Real(viewport->getActualHeight()));
		}
	}
	//---------------------------------------------------------
	void RenderSystem::saveWindowContents(String const & fileName) const
	{
		this->mImpl->engine->getRenderWindow(0)->writeContentsToFile(fileName);
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
		// resolve through EVERY resource group, same rule as sprite/2D
		// batch textures: engine media and project assets both work by
		// plain file name
		Ogre::TexturePtr texture;
		try
		{
			texture = Ogre::TextureManager::getSingleton().load(textureName,
				Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
		}
		catch(Ogre::Exception const & e)
		{
			oDebugError("engine", 0, "RenderSystem: texture '"
				<< textureName << "' failed to load: " << e.getDescription());
			return false;
		}
		if(!texture)
		{
			oDebugError("engine", 0, "RenderSystem: texture '"
				<< textureName << "' not found");
			return false;
		}
		outWidth = static_cast<unsigned int>(texture->getWidth());
		outHeight = static_cast<unsigned int>(texture->getHeight());
		return true;
	}
	//---------------------------------------------------------
	bool RenderSystem::createTexture2D(String const & name,
		unsigned char const * rgbaPixels,
		unsigned int width, unsigned int height)
	{
		if(name.empty() || !rgbaPixels || width == 0 || height == 0)
		{
			oDebugError("engine", 0, "RenderSystem::createTexture2D('" << name
				<< "'): refused (empty name/pixels/size)");
			return false;
		}
		Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton()
			.getByName(name,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(texture && (texture->getWidth() != width ||
			texture->getHeight() != height))
		{
			// replace-by-recreate: a size change cannot blit in place; the
			// cached 2D-layer material would keep the dead incarnation
			Ogre::TextureManager::getSingleton().remove(texture);
			texture.reset();
			RenderBackend::invalidateDrawLayer2DTexture(name);
		}
		if(!texture)
		{
			// no mipmaps: 2D-layer batches sample point-filtered at 1:1
			texture = Ogre::TextureManager::getSingleton().createManual(
				name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
				Ogre::TEX_TYPE_2D, width, height, 0 /*numMipmaps*/,
				Ogre::PF_BYTE_RGBA, Ogre::TU_DEFAULT);
		}
		const Ogre::PixelBox source(width, height, 1, Ogre::PF_BYTE_RGBA,
			const_cast<unsigned char*>(rgbaPixels));
		texture->getBuffer()->blitFromMemory(source);
		return true;
	}
	//---------------------------------------------------------
	void RenderSystem::destroyTexture2D(String const & name)
	{
		Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton()
			.getByName(name,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(!texture)
		{
			return;	// idempotent
		}
		// the cached 2D-layer material would keep the texture (and its GPU
		// memory) alive past the render system - drop it first
		RenderBackend::invalidateDrawLayer2DTexture(name);
		Ogre::TextureManager::getSingleton().remove(texture);
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
		// LT_BIGZIP requires the BigZip archive factory, which Engine
		// registers when constructed with a zip file (unchanged plumbing)
		static const char* const locationTypeNames[] =
			{ "FileSystem", "Zip", "BigZip" };
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			path, locationTypeNames[type],
			groupName.empty()
				? Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME
				: groupName,
			recursive);
	}
	//---------------------------------------------------------
	void RenderSystem::initialiseResourceGroups()
	{
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
	}
	//---------------------------------------------------------
	void RenderSystem::removeResourceLocation(String const & path,
		String const & groupName)
	{
		// idempotent by contract: probing first keeps the facade free of the
		// backend's ItemIdentityException on unknown locations
		Ogre::ResourceGroupManager& resourceGroups =
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
		Ogre::ResourceGroupManager& resourceGroups =
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
		Ogre::RenderTarget::FrameStats const & backendStats =
			this->mImpl->engine->getRenderWindow(0)->getStatistics();
		FrameStats stats;
		stats.lastFPS = backendStats.lastFPS;
		stats.avgFPS = backendStats.avgFPS;
		stats.bestFPS = backendStats.bestFPS;
		stats.worstFPS = backendStats.worstFPS;
		stats.triangleCount = backendStats.triangleCount;
		stats.batchCount = backendStats.batchCount;
		stats.textureMemoryBytes =
			Ogre::TextureManager::getSingleton().getMemoryUsage();
		return stats;
	}
	//---------------------------------------------------------
	void RenderSystem::resetFrameStats()
	{
		this->mImpl->engine->getRenderWindow(0)->resetStatistics();
	}
}
