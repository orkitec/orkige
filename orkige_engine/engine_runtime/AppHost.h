/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	AppHost.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __AppHost_h__12_7_2026__12_00_00__
#define __AppHost_h__12_7_2026__12_00_00__

//! @file AppHost.h
//! @brief the shared boot/teardown spine of every Orkige app host
//! @remarks AppHost owns exactly what the hosts (samples, player, editor,
//! native game modules) used to hand-wire identically: the SDL window
//! creation policy, the engine singleton bring-up order, the per-flavor
//! Engine boot, the automated-run window pacing decision, the window-camera
//! rig, the shared cube-mesh primitive, the engine module init +
//! GameObjectManager and the mirrored teardown. Each host KEEPS its own
//! frame loop - AppHost ends where the loop begins.

#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderPrerequisites.h"
#include "core_util/String.h"
#include "core_util/optr.h"

#include <SDL3/SDL.h>

#include <functional>

namespace Orkige
{
	class Engine;
	class Event;
	class GlobalEventManager;
	class ScriptRuntime;
	class GameObjectManager;

	//! @brief how AppHost boots the host window + engine; every field has a
	//! working default, hosts override only what makes them special
	struct AppHostConfig
	{
		String	windowTitle = "Orkige";
		int		windowWidth = 1280;
		int		windowHeight = 720;
		//! desktop: a user-resizable window frame. Mobile: the window is
		//! always a fullscreen native surface, and this flag opts it into
		//! DEVICE ROTATION instead (the window system only rotates an app
		//! whose window declares itself resizable; a fixed window stays
		//! pinned to its boot orientation)
		bool	resizableWindow = false;
		//! scripted/frame-capped runs render vsync-free (as fast as the
		//! machine allows); a HUMAN run gets vsync so games neither spin
		//! uncapped nor tear
		bool	automatedRun = false;
		String	engineLogFile = "orkige.log";
		//! classic flavor: the RTSS media root (containing Main/ and
		//! RTShaderLib/), registered as backend-internal media before
		//! Engine::setup; empty skips the registration. Ignored on next.
		String	classicMediaDir;
		//! next flavor: Hlms shader-template directory override; empty keeps
		//! the engine's baked default. Ignored on classic.
		String	hlmsMediaDir;
		//! the window camera on a fixed-yaw facade rig, shown on the window;
		//! the editor renders scenes offscreen (RTT) and turns this off
		bool	createWindowCamera = true;
		//! the shared vertex-colour cube mesh + material scenes reference
		bool	createCubeMesh = true;
	};

	//! @brief the boot-only app scaffold: construction, engine bring-up and
	//! mirrored teardown - never the frame loop
	//! @remarks two boot phases so a host can reach the freshly constructed
	//! Engine before setup() runs (the editor attaches its log capture there
	//! to catch the render backend's boot lines); boot() runs both for hosts
	//! that need no such seam. The destructor tears everything down in the
	//! exact reverse of the bring-up order and closes the SDL window last.
	class ORKIGE_ENGINE_DLL AppHost
	{
		//--- Variables ---------------------------------------
	private:
		AppHostConfig				mConfig;
		SDL_Window*					mWindow = nullptr;
		bool						mSdlInitialised = false;
		uptr<GlobalEventManager>	mEventManager;
		uptr<ScriptRuntime>			mScriptRuntime;
		uptr<Engine>				mEngine;
		RenderSystem*				mRenderSystem = nullptr;
		RenderWorld*				mRenderWorld = nullptr;
		optr<RenderCamera>			mWindowCamera;
		optr<RenderNode>			mCameraNode;
		uptr<GameObjectManager>		mGameObjectManager;
		//--- Methods -----------------------------------------
		//! @brief the setupEngine body, run inside an exception guard so a
		//! failure between window creation and the first frame (engine setup,
		//! resource-group init, a contended driver call) returns false - a clean
		//! non-zero app exit - instead of escaping as an uncaught throw. @see setupEngine
		bool setupEngineBody(std::function<void()> const & registerResources);
	public:
		AppHost();
		//! mirrored teardown: world before engine, engine before the script/
		//! event singletons, the SDL window after everything
		~AppHost();
		AppHost(AppHost const &) = delete;
		AppHost& operator=(AppHost const &) = delete;

		//! @brief phase 1: SDL video + the host window (fullscreen native
		//! surface on mobile; high-pixel-density - optionally resizable - on
		//! desktop, so both render flavors derive the same drawable from the
		//! same request), PlatformWindow registration, the engine singletons
		//! in the order Engine::setup depends on (Timer, GlobalEventManager,
		//! ScriptRuntime - the scripting seam must exist before the module
		//! init functions run - then init_module_orkige_core) and the
		//! per-flavor Engine construction/config (window params, the
		//! automated-run vsync decision, the classic render-system pick +
		//! RTSS media / the next Hlms override)
		bool initialise(AppHostConfig const & config);
		//! @brief phase 2: Engine::setup into the host window, then the
		//! facade bring-up every host shares - registerResources (the host's
		//! own resource locations) runs between setup and
		//! initialiseResourceGroups, then the default ambient light, the
		//! window-camera rig (fixed yaw keeps per-frame lookAt calls
		//! roll-free), the shared cube mesh, init_module_orkige_engine (the
		//! component factories must register before a GameObjectManager
		//! exists) and the GameObjectManager
		bool setupEngine(std::function<void()> const & registerResources = {});
		//! both phases in one call (hosts without a between-phase need)
		bool boot(AppHostConfig const & config,
			std::function<void()> const & registerResources = {});

		//! @brief the shared frame-delta policy: automated (frame-scripted)
		//! runs keep the historical 1/60 floor so headless frames accumulate
		//! simulated time; a HUMAN run uses the real dt - flooring it at 1/60
		//! made gameplay run FASTER than real time whenever rendering beat
		//! 60 fps. The 0.1 cap avoids the catch-up spiral after a stall, at
		//! the honest price of slow motion below 10 fps.
		static float clampFrameDelta(float measuredDelta, bool automatedRun);

		SDL_Window* getWindow() const { return this->mWindow; }
		Engine& getEngine() { return *this->mEngine; }
		ScriptRuntime& getScriptRuntime() { return *this->mScriptRuntime; }
		RenderSystem* getRenderSystem() const { return this->mRenderSystem; }
		RenderWorld* getRenderWorld() const { return this->mRenderWorld; }
		optr<RenderCamera> const & getWindowCamera() const { return this->mWindowCamera; }
		optr<RenderNode> const & getCameraNode() const { return this->mCameraNode; }
		GameObjectManager& getGameObjectManager() { return *this->mGameObjectManager; }
		bool isAutomatedRun() const { return this->mConfig.automatedRun; }
	};

	//! @brief quit-on-ESC through the engine input pipeline (SDL event ->
	//! InputManager -> GlobalEventManager -> listener) - bind onKeyPressed to
	//! InputManager::KeyPressedEvent. An optional intercept runs first and
	//! consumes the press by returning true (the editor clears its selection
	//! before a second ESC quits).
	class ORKIGE_ENGINE_DLL QuitOnEscape
	{
	public:
		bool					quitRequested = false;
		std::function<bool()>	intercept;
		bool onKeyPressed(Event const & event);
	};

	//! @brief push a synthetic key event through the SDL queue: the host
	//! loop's SDL_PollEvent feeds it into InputManager::injectEvent, so
	//! scripted input takes the REAL input path - including isKeyDown
	void ORKIGE_ENGINE_DLL pushKeyEvent(SDL_Scancode scancode, SDL_Keycode key,
		bool down);
	//! push a synthetic mouse move through the SDL queue - same real input
	//! path as pushKeyEvent (InputManager -> MouseMovedEvent -> GuiManager)
	void ORKIGE_ENGINE_DLL pushMouseMove(float x, float y);
	//! push a synthetic left mouse button press/release at the given position
	void ORKIGE_ENGINE_DLL pushMouseButton(float x, float y, bool down);

	//! @brief ModelComponent does not serialize material tweaks (yet) -
	//! re-apply the shared unlit vertex-colour render state to every model
	//! after a scene load. Meshes carrying white COLOR_0 keep their textures
	//! intact (white x texture = texture).
	void ORKIGE_ENGINE_DLL applyUnlitFixToLoadedModels(
		GameObjectManager& gameObjectManager);
}

#endif //__AppHost_h__12_7_2026__12_00_00__
