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
	RenderSystem::FrameStats RenderSystem::getFrameStats() const
	{
		Ogre::RenderTarget::FrameStats const & backendStats =
			this->mImpl->engine->getRenderWindow(0)->getStatistics();
		FrameStats stats;
		stats.lastFPS = backendStats.lastFPS;
		stats.avgFPS = backendStats.avgFPS;
		stats.triangleCount = backendStats.triangleCount;
		stats.batchCount = backendStats.batchCount;
		return stats;
	}
}
