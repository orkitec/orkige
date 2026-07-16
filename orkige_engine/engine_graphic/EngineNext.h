/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	EngineNext.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EngineNext_h__8_7_2026__12_00_00__
#define __EngineNext_h__8_7_2026__12_00_00__

//! @file EngineNext.h
//! @brief the Ogre-Next flavor's Orkige::Engine (Docs/render-abstraction.md)
//! @remarks Engine stays the app/Lua singleton on every render flavor. On
//! classic it IS the backend bootstrapper (engine_graphic/Engine.h); on the
//! Next flavor the RenderSystem facade is the boot
//! (RenderBackend::createRenderSystem), so THIS Engine is a thin wrapper:
//! it carries the boot parameters into that call, keeps the same
//! script/app-facing surface (getCamera/getRenderSystem/window size/
//! projection switches - all facade-routed one-liners) and fires the same
//! frame events (FrameStartedEvent & friends) so InputManager and game code
//! stay flavor-blind. Include it as "engine_graphic/Engine.h" - that
//! header dispatches per flavor; only EngineNext.cpp names backend types.

#ifndef ORKIGE_RENDER_NEXT
#	error "EngineNext.h is the Ogre-Next flavor's Engine - classic builds use the Engine in engine_graphic/Engine.h"
#endif

#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/FrameEventData.h"
#include "engine_render/RenderPrerequisites.h"
#include <core_event/Event.h>
#include <core_util/StringUtil.h>
#include <core_util/SafeArea.h>

#include <map>
#include <memory>

namespace Orkige
{
	//! Engine core of the Ogre-Next flavor: boot-parameter carrier around
	//! RenderBackend::createRenderSystem plus the flavor-neutral app/Lua
	//! surface (@see engine_graphic/Engine.h for the classic counterpart)
	class ORKIGE_ENGINE_DLL Engine : public Singleton<Engine>, public Interface
	{
		OOBJECT(Engine,Interface)
		DECL_OSINGLETON(Engine)
		//--- Types -------------------------------------------------
	public:
		/** \addtogroup EngineEvents
		*  @{ */
		//! Called when a frame is about to begin rendering.
		DECL_EVENTTYPE(FrameStartedEvent);
		//! @brief fired between frame start and frame end (the classic
		//! rendering-queued slot; on Next it fires right before frame end)
		DECL_EVENTTYPE(FrameRenderingQueuedEvent);
		//! Called just after a frame has been rendered.
		DECL_EVENTTYPE(FrameEndedEvent);
		/** @} End of "addtogroup EngineEvents"*/
		//! kept for source compatibility with the classic Engine (Next has
		//! no config dialog/stored render config - the value is ignored)
		enum ShowConfigBehavior
		{
			SHOW_IFERROR = 0,
			SHOW_ALWAYS,
			SHOW_NEVER,
		};
		//--- Methods -----------------------------------------------
	public:
		//! construct Engine and set basic parameters (window size & co come
		//! in via setCustomWindowParam, exactly like the classic Engine)
		Engine(String const & engineLogFileName = "orkige.log");
		//! destructor - tears the render system down again when setup() booted it
		virtual ~Engine();

		//! set custom window properties before setup(); understood keys:
		//! "width"/"height" (pixels). "vsync" is accepted and ignored (the
		//! Metal swapchain paces presentation itself).
		void setCustomWindowParam(Orkige::String paramName, Orkige::String paramValue, unsigned int windowNumber = 0);
		//! @brief override the Hlms shader-template directory (Hlms/{Common,
		//! Pbs,Unlit}) registered at boot; defaults to the vcpkg-installed
		//! ogre-next media (ORKIGE_NEXT_HLMS_MEDIA_DIR, a dev-tree default -
		//! exported apps override this once next-flavor export exists)
		void setHlmsMediaDir(String const & directory);

		//! @brief boot the Ogre-Next render system into the host window
		//! @param showConfigBehavior ignored (source compatibility)
		//! @param externalHandle stringified native window handle (NSWindow*
		//! via engine_util/SDLNativeWindow.mm) - empty lets the backend
		//! create its own window
		bool setup(String const & windowTitle = StringUtil::BLANK, ShowConfigBehavior showConfigBehavior = Engine::SHOW_IFERROR, String const & externalHandle = StringUtil::BLANK, String const & topLevelHandle = StringUtil::BLANK);

		//! render one frame (facade-routed; fires the frame events)
		bool renderOneFrame();

		//--- the flavor-neutral app/Lua surface ---------------------------
		//! the facade camera currently shown on the main window
		//! (Lua: engine:getCamera() - scripts place it via getNode())
		optr<RenderCamera> getWindowCamera();
		//! the render facade entry point (Lua: engine:getRenderSystem())
		RenderSystem* getRenderSystem();
		//! main-window drawable width in pixels (UI layout)
		unsigned int getWindowWidth();
		//! main-window drawable height in pixels (UI layout)
		unsigned int getWindowHeight();
		//! @copydoc Engine::getSafeAreaInsets (classic)
		SafeAreaInsets getSafeAreaInsets();
		//! @copydoc Engine::getContentScale (classic)
		float getContentScale();
		//! @copydoc Engine::setCameraOrthographic (classic)
		void setCameraOrthographic(float verticalHalfExtent);
		//! @copydoc Engine::setCameraOrthographicFit (classic)
		void setCameraOrthographicFit(int fitMode, float designWidth,
			float designHeight);
		//! switch the window camera back to PERSPECTIVE projection
		void setCameraPerspective();
		//! window clear colour (games pick their sky/void)
		void setWindowBackgroundColour(float red, float green, float blue);
		//! @copydoc Engine::setAtmosphere (classic)
		void setAtmosphere(bool enabled, float skyRed, float skyGreen,
			float skyBlue, float density, float fogDensity);
		//! @copydoc Engine::setAtmosphereBlend (classic)
		void setAtmosphereBlend(String const & fromSky, String const & toSky,
			float t);
		//! @brief does this build carry the gui UI system?
		//! @remarks true on BOTH flavors
		//! (gui renders through the engine_render facade); the probe
		//! stays registered so scripts written against older builds keep
		//! working - and so a future UI-less flavor can answer honestly
		bool hasUISystem() const { return true; }
		//! @brief does the active render backend support a named capability?
		//! (the script-facing face of RenderSystem::supports; @p name is a
		//! RenderCaps name e.g. "skyDome"/"dynamicShadows" - an unknown name
		//! returns false). Lets a script degrade its look honestly per flavor.
		//! @see RenderCaps (the X-macro vocabulary), Docs/render-abstraction.md
		bool supports(String const & name) const;
		//! get external window handle if Engine is embedded
		inline String const & getExternalWindowHandle() { return this->externalWindowHandle; }
		//! get top level window handle if Engine is embedded into multi window app
		inline String const & getTopLevelWindowHandle() { return this->topLevelWindowHandle; }
		/** \addtogroup Debug
		*  @{ */
		//! Wireframe rendering mode (GLOBAL on Next - see RenderCamera.h)
		void enableWireframeMode();
		//! Solid rendering mode
		void disableWireframeMode();
		/** @} End of "addtogroup Debug"*/
	protected:
	private:
		//--- Variables ---------------------------------------------
		//! the backend frame-listener bridge firing the events above
		//! (defined in EngineNext.cpp - the one place naming backend types)
		struct FrameBridge;
		uptr<FrameBridge>	frameBridge;
		std::map<String, String>		windowParams;
		String							logFileName;
		String							hlmsMediaDir;
		String							externalWindowHandle;
		String							topLevelWindowHandle;
		optr<FrameEventData>			data;
		//! the constructor pre-created the Ogre::LogManager (Root adopts an
		//! existing singleton) - the destructor must release it
		bool							ownsLogManager;
		Event							frameStartedEvent;
		Event							frameRenderingQueuedEvent;
		Event							frameEndedEvent;
		//! true when setup() booted the render system (the dtor then owns
		//! the teardown; false keeps pre-booted test scenarios untouched)
		bool							ownsRenderSystem;
	};
}

#endif //__EngineNext_h__8_7_2026__12_00_00__
