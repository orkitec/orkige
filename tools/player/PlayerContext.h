/**************************************************************
	created:	2026/07/16 at 06:00
	filename: 	PlayerContext.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlayerContext_h__16_7_2026__06_00_00__
#define __PlayerContext_h__16_7_2026__06_00_00__

#include <core_debug/Breadcrumbs.h>
#include <core_debug/BenchmarkRecorder.h>
#include <core_game/AppLifecycle.h>
#include <core_game/GameState.h>
#include <core_game/LevelManager.h>
#include <core_game/SaveStore.h>
#include <core_game/TimeControl.h>
#include <core_project/Project.h>
#include <core_tween/TweenManager.h>
#include <core_tween/TimerManager.h>
#include <core_util/optr.h>
#include <engine_base/EngineLog.h>
#include <engine_gocomponent/SpriteBatcher.h>
#include <engine_graphic/ScreenFade.h>
#include <engine_graphic/ScreenShake.h>
#include <engine_input/InputManager.h>
#include <engine_input/HapticManager.h>
#include <engine_input/InputActionMap.h>
#include <engine_physic/PhysicsWorld.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderNode.h>
#include <engine_runtime/AppHost.h>
#include <engine_runtime/PlayerRuntime.h>
#include <engine_sound/SoundManager.h>
#include <engine_util/FrameStatsUtil.h>
#include <core_util/StringTable.h>

#include "PlayerSelfChecks.h"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_set>

//! @brief everything the player's frame loop touches, on the heap: the boot
//! inputs, the engine host, every runtime manager and the loop state. main()
//! fills it in the exact sequence the historical stack boot used, the loop
//! body (playerIterate in main.cpp) reads it back per frame, and the browser
//! build hands ONE raw pointer to the page's frame callback (the loop cannot
//! live on main()'s stack there - the runtime returns to the page between
//! frames).
//!
//! THE ORDER CONTRACT: member declaration order IS the boot order, and its
//! reverse IS the teardown order - this struct's destructor must mirror the
//! historical stack teardown exactly (managers, then the debug link's
//! dependents, then the host, then the benchmark artifact, then the
//! breadcrumb trail last, so the trail outlives the whole engine teardown).
//! Members that historically constructed only after the host booted (their
//! constructors need the live engine) are std::optional and are emplaced at
//! the same sequence points; an optional still destroys in declaration-order
//! reverse, so the contract holds for them too. REORDERING A MEMBER IS A
//! LIFECYCLE CHANGE - review it as one.
struct PlayerContext
{
	//--- boot inputs (pure data, before any engine object) ------------------
	Orkige::Project project;
	std::string scenePath;
	int exitCode = 0;

	//--- the outermost engine objects (the historical outer stack frame) ----
	//! crash breadcrumbs: alive through the WHOLE engine teardown below
	Orkige::Breadcrumbs breadcrumbs;
	//! per-scene performance capture: its artifact flushes through teardown
	Orkige::BenchmarkRecorder benchmarkRecorder;
	//! every ORKIGE_* env-hook verification (state + logic; pure data here)
	PlayerSelfChecks selfChecks;
	//! the shared boot/teardown spine (window, engine, world)
	Orkige::AppHost host;

	//--- the world the host boot produced (the historical inner scope) ------
	//! render facade handles (owned by the host; plain handles here)
	Orkige::RenderSystem* render = nullptr;
	Orkige::RenderWorld* world = nullptr;
	Orkige::GameObjectManager* gameObjectManagerPtr = nullptr;
	//! engine-log -> breadcrumb mirror (attached after the log exists)
	std::optional<Orkige::EngineLogCapture> breadcrumbLog;
	//! the window camera on its fixed-yaw rig (from the host boot)
	Orkige::optr<Orkige::RenderCamera> camera;
	Orkige::optr<Orkige::RenderNode> cameraNode;
	std::optional<Orkige::InputManager> inputManager;
	std::optional<Orkige::HapticManager> hapticManager;
	std::optional<Orkige::InputActionMap> inputActions;
	std::optional<Orkige::StringTable> stringTable;
	std::optional<Orkige::QuitOnEscape> quitOnEscape;
	Orkige::optr<Orkige::EventListener> escapeListener;
	std::optional<Orkige::SoundManager> soundManager;
	std::optional<Orkige::PhysicsWorld> physicsWorld;
	std::optional<Orkige::TweenManager> tweenManager;
	std::optional<Orkige::TimerManager> timerManager;
	std::optional<Orkige::GameState> gameState;
	std::optional<Orkige::LevelManager> levelManager;
	std::optional<Orkige::ScreenFade> screenFade;
	std::optional<Orkige::ScreenShake> screenShake;
	std::optional<Orkige::SpriteBatcher> spriteBatcher;
	std::optional<Orkige::TimeControl> timeControl;
	std::optional<Orkige::SaveStore> saveStore;
	std::optional<Orkige::PlayerDebugLink> debugLink;
	std::optional<Orkige::FrameStatsUtil> frameStats;
	std::optional<Orkige::AppLifecycle> lifecycle;

	//--- loop state ----------------------------------------------------------
	bool physicsNeeded = false;
	unsigned long frameLimit = 0;
	bool automatedRun = false;
	bool running = true;
	unsigned long frameCount = 0;
	//! script failures already recorded to the breadcrumb trail (once each)
	std::unordered_set<std::string> breadcrumbedScriptErrors;
	std::chrono::steady_clock::time_point lastFrameTime;

	//--- structural helpers (defined in main.cpp, next to the loop) ---------
	//! the re-entrant scene load the deferred-load pump reuses
	bool reloadSceneFrom(std::string const & newScenePath);
	//! translate one mobile app-lifecycle event into subsystem actions
	void applyLifecycle(Orkige::AppLifecycle::Event event);
	//! record newly-failed ScriptComponents to the breadcrumb trail (once each)
	void recordScriptErrorBreadcrumbs();
	//! the orderly post-loop sequence: autosave flush, shutdown breadcrumb,
	//! benchmark finish, the selfcheck verdicts, the frame-capped script-error
	//! scan, frame stats, world teardown (GameObjectManager::clear) and the
	//! debug-protocol shutdown - run once when the loop ends, on every platform
	void shutdownWorld();
};

#endif //__PlayerContext_h__16_7_2026__06_00_00__
