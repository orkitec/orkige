/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	EngineNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file EngineNext.cpp
//! @brief the Ogre-Next flavor's Engine (see EngineNext.h)
//! @remarks This TU is the next flavor's Engine.cpp counterpart and - like
//! the classic Engine.cpp - the app-layer boot bridge into its backend: it
//! includes engine_render_next/NextBackend.h (sanctioned there) to run
//! RenderBackend::createRenderSystem and to hook the frame-event bridge
//! into the backend's frame loop. Nothing else above the backend does.

#include "engine_graphic/EngineNext.h"
#include "engine_render_next/NextBackend.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderCamera.h"
#include "engine_util/PlatformWindow.h"
#include <core_util/CameraFit.h>
#include <core_event/GlobalEventManager.h>

#include <OgreRoot.h>
#include <OgreFrameListener.h>

#include <cstdlib>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(Engine, FrameStartedEvent);
	IMPL_OWNED_EVENTTYPE(Engine, FrameRenderingQueuedEvent);
	IMPL_OWNED_EVENTTYPE(Engine, FrameEndedEvent);
	IMPL_OSINGLETON(Engine);
	//---------------------------------------------------------
	//! the backend frame-listener bridge: Ogre-Next's Root fires the same
	//! frameStarted/frameRenderingQueued/frameEnded trio as classic, so the
	//! engine frame events keep their classic timing semantics
	struct Engine::FrameBridge : public Ogre::FrameListener
	{
		Engine* owner = NULL;

		virtual bool frameStarted(const Ogre::FrameEvent& evt) override
		{
			return this->fire(evt, owner->frameStartedEvent);
		}
		virtual bool frameRenderingQueued(const Ogre::FrameEvent& evt) override
		{
			return this->fire(evt, owner->frameRenderingQueuedEvent);
		}
		virtual bool frameEnded(const Ogre::FrameEvent& evt) override
		{
			return this->fire(evt, owner->frameEndedEvent);
		}
		bool fire(const Ogre::FrameEvent& evt, Event & engineEvent)
		{
			owner->data->timeSinceLastEvent = evt.timeSinceLastEvent;
			owner->data->timeSinceLastFrame = evt.timeSinceLastFrame;
			if(GlobalEventManager::getSingletonPtr())
			{
				GlobalEventManager::getSingleton().trigger(engineEvent);
			}
			return true;
		}
	};
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Engine::Engine(String const & engineLogFileName)
		: frameStartedEvent(Engine::FrameStartedEvent),
		frameRenderingQueuedEvent(Engine::FrameRenderingQueuedEvent),
		frameEndedEvent(Engine::FrameEndedEvent),
		logFileName(engineLogFileName),
		ownsRenderSystem(false)
	{
#ifdef ORKIGE_NEXT_HLMS_MEDIA_DIR
		// dev-tree default: the Hlms shader templates the ogre-next vcpkg
		// overlay port ships (setHlmsMediaDir overrides for bundled apps)
		this->hlmsMediaDir = ORKIGE_NEXT_HLMS_MEDIA_DIR;
#endif
		this->data = onew(new FrameEventData());
		this->frameStartedEvent.setData(this->data);
		this->frameRenderingQueuedEvent.setData(this->data);
		this->frameEndedEvent.setData(this->data);
	}
	//---------------------------------------------------------
	Engine::~Engine()
	{
		if(this->frameBridge && RenderBackend::ogreRoot())
		{
			RenderBackend::ogreRoot()->removeFrameListener(this->frameBridge.get());
		}
		this->frameBridge.reset();
		if(this->ownsRenderSystem)
		{
			RenderBackend::destroyRenderSystem();
		}
	}
	//---------------------------------------------------------
	void Engine::setCustomWindowParam(Orkige::String paramName, Orkige::String paramValue, unsigned int windowNumber)
	{
		// single main window on this flavor (by design)
		oAssert(windowNumber == 0);
		this->windowParams[paramName] = paramValue;
	}
	//---------------------------------------------------------
	void Engine::setHlmsMediaDir(String const & directory)
	{
		this->hlmsMediaDir = directory;
	}
	//---------------------------------------------------------
	bool Engine::setup(String const & windowTitle, ShowConfigBehavior /*showConfigBehavior*/, String const & externalHandle, String const & topLevelHandle)
	{
		this->externalWindowHandle = externalHandle;
		// same fallback as the classic Engine: an embedded single-window app
		// reports its host window as the top-level one
		this->topLevelWindowHandle =
			(topLevelHandle.empty() && !externalHandle.empty())
			? externalHandle : topLevelHandle;

		RenderBackend::NextBootOptions options;
		options.windowTitle = windowTitle;
		options.nativeWindowHandle = externalHandle;
		options.logFileName = this->logFileName;
		options.hlmsMediaDir = this->hlmsMediaDir;
		if(this->windowParams.count("width"))
		{
			options.width = static_cast<unsigned int>(
				std::strtoul(this->windowParams["width"].c_str(), NULL, 10));
		}
		if(this->windowParams.count("height"))
		{
			options.height = static_cast<unsigned int>(
				std::strtoul(this->windowParams["height"].c_str(), NULL, 10));
		}
		// "vsync" is accepted and ignored (Metal swapchain pacing)

		const bool preBooted = (RenderBackend::system() != NULL);
		if(!RenderBackend::createRenderSystem(options))
		{
			return false;
		}
		this->ownsRenderSystem = !preBooted;

		this->frameBridge = std::make_unique<FrameBridge>();
		this->frameBridge->owner = this;
		RenderBackend::ogreRoot()->addFrameListener(this->frameBridge.get());
		return true;
	}
	//---------------------------------------------------------
	bool Engine::renderOneFrame()
	{
		return this->getRenderSystem()->renderOneFrame();
	}
	//---------------------------------------------------------
	optr<RenderCamera> Engine::getWindowCamera()
	{
		RenderSystem* renderSystem = RenderSystem::get();
		oAssert(renderSystem);
		return renderSystem->getWindowCamera();
	}
	//---------------------------------------------------------
	RenderSystem* Engine::getRenderSystem()
	{
		RenderSystem* renderSystem = RenderSystem::get();
		oAssert(renderSystem);
		return renderSystem;
	}
	//---------------------------------------------------------
	unsigned int Engine::getWindowWidth()
	{
		unsigned int width = 0;
		unsigned int height = 0;
		this->getRenderSystem()->getWindowSize(width, height);
		return width;
	}
	//---------------------------------------------------------
	unsigned int Engine::getWindowHeight()
	{
		unsigned int width = 0;
		unsigned int height = 0;
		this->getRenderSystem()->getWindowSize(width, height);
		return height;
	}
	//---------------------------------------------------------
	SafeAreaInsets Engine::getSafeAreaInsets()
	{
		unsigned int width = 0;
		unsigned int height = 0;
		this->getRenderSystem()->getWindowSize(width, height);
		return PlatformWindow::getSafeAreaInsets(width, height);
	}
	//---------------------------------------------------------
	float Engine::getContentScale()
	{
		return PlatformWindow::getContentScale();
	}
	//---------------------------------------------------------
	void Engine::setCameraOrthographic(float verticalHalfExtent)
	{
		optr<RenderCamera> windowCamera = this->getWindowCamera();
		oAssert(windowCamera);
		// height only - the width follows the camera's aspect ratio; the
		// facade call wants the clips, preserving the current ones keeps the
		// historical "projection switch only" behavior (same as classic)
		windowCamera->setOrthographic(verticalHalfExtent,
			windowCamera->getNearClip(), windowCamera->getFarClip());
	}
	//---------------------------------------------------------
	void Engine::setCameraOrthographicFit(int fitMode, float designWidth,
		float designHeight)
	{
		optr<RenderCamera> windowCamera = this->getWindowCamera();
		oAssert(windowCamera);
		unsigned int width = 0;
		unsigned int height = 0;
		this->getRenderSystem()->getWindowSize(width, height);
		const float aspect = (width > 0 && height > 0)
			? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
		const float halfExtent = CameraFit::orthoHalfHeight(
			static_cast<CameraFit::FitMode>(fitMode), designWidth, designHeight,
			aspect);
		windowCamera->setOrthographic(halfExtent,
			windowCamera->getNearClip(), windowCamera->getFarClip());
	}
	//---------------------------------------------------------
	void Engine::setCameraPerspective()
	{
		optr<RenderCamera> windowCamera = this->getWindowCamera();
		oAssert(windowCamera);
		windowCamera->setPerspective(windowCamera->getFOVy(),
			windowCamera->getNearClip(), windowCamera->getFarClip());
	}
	//---------------------------------------------------------
	void Engine::setWindowBackgroundColour(float red, float green, float blue)
	{
		this->getRenderSystem()->setWindowBackgroundColour(
			Color(red, green, blue));
	}
	//---------------------------------------------------------
	void Engine::enableWireframeMode()
	{
		optr<RenderCamera> windowCamera = this->getWindowCamera();
		if(windowCamera)
		{
			windowCamera->setWireframe(true);
		}
	}
	//---------------------------------------------------------
	void Engine::disableWireframeMode()
	{
		optr<RenderCamera> windowCamera = this->getWindowCamera();
		if(windowCamera)
		{
			windowCamera->setWireframe(false);
		}
	}
	//---------------------------------------------------------
	// same Lua-facing registration as the classic Engine.cpp - scripts see
	// ONE Engine surface on every flavor
	OOBJECT_IMPL(Engine)
		OCONSTRUCTOR0()
		OSINGLETON()
		OFUNC(setup)
		OFUNC(getTopLevelWindowHandle)
		OFUNC(renderOneFrame)
		OFUNC(enableWireframeMode)
		OFUNC(disableWireframeMode)
		// the window camera; scripts place it via its rig node:
		// Engine.getSingleton():getCamera():getNode()
		OFUNC_REN(getWindowCamera,getCamera)
		// render services (RenderSystem/RenderWorld usertypes in module.cpp)
		OFUNC(getRenderSystem)
		// window size in pixels for UI layout
		OFUNC(getWindowWidth)
		OFUNC(getWindowHeight)
		// safe-area insets (notch/home indicator) + display density: scripts
		// anchor HUD/menus inside engine:getSafeAreaInsets()
		OFUNC(getSafeAreaInsets)
		OFUNC(getContentScale)
		// 2D projection switches: engine:setCameraOrthographic(orthoSize)
		OFUNC(setCameraOrthographic)
		OFUNC(setCameraOrthographicFit)
		OFUNC(setCameraPerspective)
		OFUNC(setWindowBackgroundColour)
		// UI capability probe: true on BOTH flavors since the DrawLayer2D
		// port (fastgui renders through the engine_render facade); the probe
		// stays so scripts can still gate honestly for a future UI-less flavor
		OFUNC(hasUISystem)
	OOBJECT_END
}
