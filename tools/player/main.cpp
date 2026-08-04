/********************************************************************
	created:	Tuesday 2026/07/07 at 12:00
	filename: 	main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// orkige_player - the standalone scene player.
//
// Boots the engine exactly like samples/hello_orkige (SDL3 owns the window
// and event loop, Orkige::Engine renders into it via the externalWindowHandle
// path), loads the .oscene file given as argv[1] through SceneSerializer and
// runs the game loop: the InputManager is wired (ESC quits through the engine
// event pipeline), GameObject components are updated every frame and the
// physics world is stepped when the scene contains RigidBodyComponents.
// This is the runtime the editor's play mode builds on.
//
// Remote debugging (the editor's play mode): --debug-port N starts a
// core_debugnet DebugServer on 127.0.0.1:N. On the web the direction
// reverses: a browser page cannot listen, so ORKIGE_DEBUG_CONNECT=host:port
// (forwarded by the shell page's ?env.* query mapping) makes the runtime
// DIAL the editor instead - the same protocol, WebSocket-framed by the
// platform's socket emulation. Commands are processed once per
// frame; pause gates physics + component updates but keeps rendering and the
// protocol alive, step advances exactly one fixed update while paused. The
// hierarchy (GameObject id list) is streamed on change (checked every
// HIERARCHY_CHECK_INTERVAL frames and on connect/request), the selected
// object's state at ~15Hz. set_property v1 covers TransformComponent
// position/orientation/scale and RigidBodyComponent
// linear_velocity/angular_velocity; anything else answers with an error
// message and never crashes.
//
// Automation hooks (same env-hook style as the demo/editor):
// ORKIGE_DEMO_FRAMES=N exit 0 after N frames,
// ORKIGE_DEMO_SCREENSHOT=path framebuffer dump at frame 60,
// ORKIGE_DEMO_FPS_LOG=1 log frame count / avg / p95 ms at exit.
#include <SDL3/SDL.h>
// SDL_main.h in the translation unit defining main(): a no-op on desktop,
// on iOS it wraps main() in SDL's UIKit application bootstrap
#include <SDL3/SDL_main.h>
#include <engine_graphic/Engine.h>
#include <engine_graphic/ScreenFade.h>
#include <engine_graphic/ScreenShake.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderSystemSelection.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/MeshInstance.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_gocomponent/ScriptComponentRegistry.h>
#include <engine_gocomponent/ParticleComponent.h>
#include <engine_gocomponent/VectorShapeComponent.h>
#include <engine_gocomponent/VectorAnimationComponent.h>
#include <core_util/SoftBodyDeform.h>
#include <core_util/VectorTessellator.h>
#include <core_util/VectorShapeAsset.h>
#include <engine_physic/PhysicsWorld.h>
#include <engine_input/InputManager.h>
#include <engine_input/HapticManager.h>
#include <engine_input/InputActionMap.h>
#include <engine_sound/SoundManager.h>
#include <engine_util/PlatformWindow.h>
#include <core_util/StringTable.h>
#include <core_util/LocaleMatch.h>
#include <core_util/PathJail.h>
// gui is flavor-neutral - the UI
// assertions below run on BOTH render flavors
#include <engine_gui/GuiManager.h>
#include <engine_runtime/AppHost.h>
#include <engine_runtime/GameHost.h>
#include <core_filesystem/ResourceReader.h>
#include <core_monetization/PlatformStore.h>
#include <core_monetization/ProductCatalogFile.h>
#include <core_monetization/SimulatedProvider.h>
#include "PlayerContext.h"
#include "PlayerSelfChecks.h"
#include "PlayerTestRun.h"
#include <engine_runtime/PlayerRuntime.h>
#include <engine_util/FrameStatsUtil.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_game/LevelManager.h>
#include <core_game/LevelSequence.h>
#include <core_game/SaveStore.h>
#include <core_game/AppLifecycle.h>
#include <core_game/TimeControl.h>
#include <core_game/GameState.h>
#include <core_project/Project.h>
#include <core_debug/CVarManager.h>
#include <core_debug/DebugMacros.h>
#include <core_debug/Breadcrumbs.h>
#include <core_debug/BenchmarkRecorder.h>
#include <core_debug/MemoryManager.h>
#include <core_debug/Profile.h>
#include <core_debugnet/DebugServer.h>
#include <core_debugnet/Json.h>
#include <engine_base/EngineLog.h>
#include <core_util/PlatformUtil.h>
#include <core_util/StringUtil.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>
#include <core_tween/TweenManager.h>
#include <core_tween/TimerManager.h>
#include <core_script/ScriptEventBus.h>
#include <core_tween/EaseLibrary.h>

#include <cctype>
#include <chrono>
#include <csignal>
#include <ctime>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <vector>

// the engine's shared-ownership alias, used throughout this TU
using Orkige::optr;
using Orkige::woptr;

namespace
{

// Read a project config file's text. The mounted content reader is tried FIRST
// by project-relative name, so the same file resolves out of a pak or an APK
// entry in place; a loose development tree falls through to the path read.
bool readProjectTextFile(std::string const& absolutePath,
	std::string const& relativeName, std::string& outText)
{
	if (Orkige::ResourceReader const* reader = Orkige::ResourceAccess::reader())
	{
		Orkige::String text;
		if (reader->readText(relativeName, text))
		{
			outText.assign(text.begin(), text.end());
			return true;
		}
	}
	std::ifstream file(absolutePath, std::ios::binary);
	if (!file)
	{
		return false;
	}
	std::ostringstream buffer;
	buffer << file.rdbuf();
	outText = buffer.str();
	return true;
}

// does any loaded GameObject carry a RigidBodyComponent?
bool sceneHasRigidBodies(Orkige::GameObjectManager& gameObjectManager)
{
	for (auto const& [id, gameObject] : gameObjectManager.getGameObjects())
	{
		if (gameObject->hasComponent<Orkige::RigidBodyComponent>())
		{
			return true;
		}
	}
	return false;
}

//--- remote debugging (editor play mode) ---------------------------------
// The whole player side of the protocol (pause/step/select/set_property,
// hierarchy + object_state streaming, Ogre-log forwarding) lives in the
// shared Orkige::PlayerDebugLink (engine_runtime/PlayerRuntime.h) so native
// game modules speak the identical protocol - this file only wires it into
// the frame loop. The synthetic-input pushers the selfchecks below script
// with (pushKeyEvent/pushMouseMove/pushMouseButton) are the shared
// engine_runtime/AppHost.h helpers.
using Orkige::pushKeyEvent;
using Orkige::pushMouseMove;
using Orkige::pushMouseButton;

} // namespace

// the re-entrant scene load: the SAME steps the initial load
// above runs, factored so the deferred-load pump at the frame boundary
// (in the player loop's tick order) can reuse them. SceneSerializer::
// loadScenePreservingPersistent tears the old world down via
// GameObjectManager::clearExceptPersistent (the teardown hook - non-persistent
// scripts get their shutdown, tweens are reaped, rigid bodies leave the sim;
// objects marked persistent survive whole); we then re-apply the unlit fix, bring
// physics up lazily if the new level introduces bodies (PhysicsWorld
// persists - inited once, never torn down), drop the debug-link
// selection so a stale id cannot dangle and let the hierarchy
// re-stream. A failed load is logged but keeps the run alive.
//---------------------------------------------------------
// THE TEST-TIER SCENE RESET. Deliberately NOT reloadSceneFrom above: that one
// preserves persistent objects (the level system's mid-play switch), and a
// test run is a boundary - a survivor carried from one test into the next
// would couple them, which is the whole reason a test tier isolates. So the
// world goes down WHOLE through GameObjectManager::clear (the ONE teardown
// hook: scripts get their shutdown, tweens/timers/tasks are reaped, rigid
// bodies leave the sim) and the scene loads fresh beside it.
bool PlayerContext::loadSceneForTest(std::string const & requestedScene)
{
	Orkige::GameObjectManager& gameObjectManager = *this->gameObjectManagerPtr;
	std::string resolvedScene = requestedScene;
	std::error_code ignored;
	if (this->project.isLoaded() &&
		!std::filesystem::exists(resolvedScene, ignored))
	{
		resolvedScene = this->project.resolvePath(requestedScene);
	}
	// the explicit whole-world teardown, before the load - so a scene that
	// FAILS to load leaves an empty world rather than the previous test's
	gameObjectManager.clear();
	if (!Orkige::SceneSerializer::loadScene(resolvedScene, gameObjectManager))
	{
		SDL_Log("orkige_player: test scene '%s' could not be loaded",
			resolvedScene.c_str());
		return false;
	}
	Orkige::applyUnlitFixToLoadedModels(gameObjectManager);
	this->physicsNeeded = sceneHasRigidBodies(gameObjectManager);
	if (this->physicsNeeded && this->physicsWorld &&
		!this->physicsWorld->isInitialized())
	{
		if (!this->physicsWorld->init())
		{
			SDL_Log("orkige_player: FAILED - PhysicsWorld::init failed for "
				"test scene '%s'", resolvedScene.c_str());
			return false;
		}
	}
	this->scenePath = resolvedScene;
	return true;
}
//---------------------------------------------------------
bool PlayerContext::reloadSceneFrom(std::string const & newScenePath)
{
	std::string& scenePath = this->scenePath;
	Orkige::BenchmarkRecorder& benchmarkRecorder = this->benchmarkRecorder;
	Orkige::GameObjectManager& gameObjectManager = *this->gameObjectManagerPtr;
	Orkige::PhysicsWorld& physicsWorld = *this->physicsWorld;
	Orkige::PlayerDebugLink& debugLink = *this->debugLink;
	bool& physicsNeeded = this->physicsNeeded;

	// crash-survivable trail: mark the scene-UNLOAD boundary BEFORE the old
	// world is torn down. The outgoing scene's teardown (below) destroys the
	// render items - including a mirrorlake water surface, which drops the
	// planar-reflection subsystem (@see destroyPlanarReflections) - so a hard
	// crash during that teardown leaves this crumb, not the previous scene's
	// "scene <path>" load crumb, as the last ordinary trail entry. Bounds the
	// unload phase; the "scene <new>" crumb (recorded after the new scene is
	// live) closes it.
	if (Orkige::Breadcrumbs::getSingletonPtr())
	{
		Orkige::Breadcrumbs::getSingleton().record("scene_unload", newScenePath);
	}

	// the level system's mid-play switch: objects marked persistent survive
	// the teardown with their whole live state (render node, physics body,
	// script sandbox) and the arriving scene loads in beside them; a duplicate
	// id the survivor already owns is skipped (SceneSerializer duplicate rule)
	if (!Orkige::SceneSerializer::loadScenePreservingPersistent(newScenePath,
		gameObjectManager))
	{
		SDL_Log("orkige_player: deferred load FAILED - could not load "
			"scene '%s' (keeping the previous world)",
			newScenePath.c_str());
		return false;
	}
	Orkige::applyUnlitFixToLoadedModels(gameObjectManager);
	physicsNeeded = sceneHasRigidBodies(gameObjectManager);
	if (physicsNeeded && !physicsWorld.isInitialized())
	{
		if (!physicsWorld.init())
		{
			SDL_Log("orkige_player: FAILED - PhysicsWorld::init failed "
				"on deferred level load");
			return false;
		}
	}
	// the editor learns the switch by its portable identity: project-relative
	// when this run plays a project (the editor resolves it against ITS copy
	// of the same files), the load path otherwise
	{
		std::string reportedScene = newScenePath;
		if (this->project.isLoaded())
		{
			const std::string relative =
				this->project.makeProjectRelative(newScenePath);
			if (!relative.empty())
			{
				reportedScene = relative;
			}
		}
		debugLink.onSceneReloaded(reportedScene);
	}
	// the old scene's subscriptions were cancelled as its ScriptInstances
	// were destroyed above; flush any events it left on the engine bus so
	// a stale emit (e.g. a last-frame physics contact) is never delivered
	// to the NEW scene's handlers (created next frame). Two ticks clear
	// both of GlobalEventManager's double-buffered queues, to nothing -
	// the outgoing listeners are already gone.
	if (Orkige::GlobalEventManager::getSingletonPtr())
	{
		Orkige::GlobalEventManager::getSingleton().tick();
		Orkige::GlobalEventManager::getSingleton().tick();
	}
	scenePath = newScenePath;
	if (Orkige::Breadcrumbs::getSingletonPtr())
	{
		Orkige::Breadcrumbs::getSingleton().record("scene", newScenePath);
	}
	// a level switch is a benchmark scene boundary: close the outgoing
	// scene's record and start a fresh aggregation (no-op when disarmed;
	// an explicit Lua benchmark.begin can later rename this aggregation)
	benchmarkRecorder.beginScene(newScenePath);
	SDL_Log("orkige_player: switched to scene '%s' (%zu GameObjects)",
		newScenePath.c_str(),
		gameObjectManager.getGameObjects().size());
	return true;
}

//---------------------------------------------------------
void PlayerContext::applyLifecycle(Orkige::AppLifecycle::Event event)
{
	std::string& scenePath = this->scenePath;
	Orkige::GameObjectManager& gameObjectManager = *this->gameObjectManagerPtr;
	Orkige::SoundManager& soundManager = *this->soundManager;
	Orkige::SaveStore& saveStore = *this->saveStore;
	Orkige::AppLifecycle& lifecycle = *this->lifecycle;

	const Orkige::AppLifecycle::Actions actions = lifecycle.handle(event);
	if (actions.breadcrumb && Orkige::Breadcrumbs::getSingletonPtr())
	{
		Orkige::Breadcrumbs::getSingleton().record(actions.breadcrumb,
			scenePath);
	}
	// pause path: let the game react (it may write its own save state)
	// BEFORE the engine flushes the store to disk
	if (actions.notifyPause)
	{
		Orkige::ScriptComponent::dispatchAppLifecycle(gameObjectManager,
			true);
	}
	if (actions.flushSave)
	{
		saveStore.flush();
	}
	if (actions.suspendAudio)
	{
		soundManager.onInterruptBegin();
	}
	// resume path: bring audio back before the game's onAppResume runs
	if (actions.resumeAudio)
	{
		soundManager.onInterruptEnd();
	}
	if (actions.notifyResume)
	{
		Orkige::ScriptComponent::dispatchAppLifecycle(gameObjectManager,
			false);
	}
}

// breadcrumbs: record each ScriptComponent failure once (a running game
// may keep ticking with a failed script; the trail wants it, not a
// per-frame repeat). Also drain the engine log's warnings/errors below.
//---------------------------------------------------------
void PlayerContext::recordScriptErrorBreadcrumbs()
{
	Orkige::GameObjectManager& gameObjectManager = *this->gameObjectManagerPtr;
	std::unordered_set<std::string>& breadcrumbedScriptErrors = this->breadcrumbedScriptErrors;

	if (!Orkige::Breadcrumbs::getSingletonPtr())
	{
		return;
	}
	for (auto const& [id, gameObject] :
		gameObjectManager.getGameObjects())
	{
		// every script on the object - an object may carry several
		for (Orkige::ScriptComponent* script :
			Orkige::ScriptComponent::collectFrom(*gameObject))
		{
			const std::string key = id + "\n" + script->getComponentName();
			if (script->hasScriptError() &&
				breadcrumbedScriptErrors.insert(key).second)
			{
				Orkige::Breadcrumbs::getSingleton().record("script_error",
					script->getScriptError(), { { "object", id },
					{ "component", script->getComponentName() } });
			}
		}
	}
}

//---------------------------------------------------------
void PlayerContext::shutdownWorld()
{
	std::string& scenePath = this->scenePath;
	int& exitCode = this->exitCode;
	Orkige::BenchmarkRecorder& benchmarkRecorder = this->benchmarkRecorder;
	Orkige::GameObjectManager& gameObjectManager = *this->gameObjectManagerPtr;
	Orkige::SaveStore& saveStore = *this->saveStore;
	Orkige::PlayerDebugLink& debugLink = *this->debugLink;
	Orkige::FrameStatsUtil& frameStats = *this->frameStats;
	unsigned long& frameLimit = this->frameLimit;

	// clean-shutdown autosave: persist any unflushed `save` changes now (a
	// hard crash skips this - the documented crash-loses-unflushed window).
	// A no-op when nothing changed or no save file is set.
	saveStore.flush();

	// breadcrumbs: an orderly shutdown marker distinguishes a clean exit
	// from a crash trail (whose last line is whatever happened before death)
	if (Orkige::Breadcrumbs::getSingletonPtr())
	{
		Orkige::Breadcrumbs::getSingleton().record("shutdown", scenePath);
	}
	// finalize the benchmark artifact: close the open scene and write the
	// summary line (a clean, non-aborted run). No-op when disarmed.
	benchmarkRecorder.finish(false);

	// the end-of-run selfcheck verdicts (a check that never reached
	// its Done phase fails the run) + the hot-reload script restore
	selfChecks.atLoopEnd(*this);

	// automated (frame-capped) runs fail honestly when a script instance
	// died: the export tests RUN the exported game, and a game whose
	// scripts errored out must not pass as "exited 0" (a human run keeps
	// going - the error is logged once and the instance is disabled)
	if (frameLimit != 0)
	{
		for (auto const& [id, gameObject] :
			gameObjectManager.getGameObjects())
		{
			if (!gameObject->hasComponent<Orkige::ScriptComponent>())
			{
				continue;
			}
			Orkige::ScriptComponent* script =
				gameObject->getComponentPtr<Orkige::ScriptComponent>();
			if (script->hasScriptError())
			{
				SDL_Log("orkige_player: FAILED - script error on '%s' "
					"('%s'): %s", id.c_str(),
					script->getScriptFile().c_str(),
					script->getScriptError().c_str());
				exitCode = 1;
			}
		}
	}

	frameStats.logAtExit("orkige_player");

	// tear the world down here, while physicsWorld and the Lua state are
	// still alive: the host owns the GameObjectManager (it outlives this
	// block), so its ScriptComponents (whose shutdown may call
	// physics:setPaused) and RigidBodyComponents (which remove bodies from
	// physicsWorld) must run the GameObjectManager::clear teardown hook
	// now, not in AppHost's later destructor once physicsWorld's stack
	// slot is gone. The debug link is still up so a teardown log reaches
	// the editor.
	gameObjectManager.clear();

	// orderly protocol shutdown: detach the log forwarder (the link dies
	// before the engine - declaration order), tell the editor we are
	// going down (the quit path already sent bye), flush the socket
	debugLink.shutdown();
}

//! @brief ONE frame of the player: poll/input, the canonical tick
//! order, presentation, streaming, render and the frame-boundary
//! folds - the body every platform's loop driver runs per frame.
//! Returns false when the run ended (quit, frame cap, a selfcheck
//! verdict) - the driver then runs PlayerContext::shutdownWorld().
static bool playerIterate(PlayerContext& context)
{
	Orkige::Project& project = context.project;
	std::string& scenePath = context.scenePath;
	Orkige::BenchmarkRecorder& benchmarkRecorder = context.benchmarkRecorder;
	Orkige::RenderSystem* const render = context.render;
	Orkige::GameObjectManager& gameObjectManager = *context.gameObjectManagerPtr;
	Orkige::EngineLogCapture& breadcrumbLog = *context.breadcrumbLog;
	Orkige::InputManager& inputManager = *context.inputManager;
	Orkige::InputActionMap& inputActions = *context.inputActions;
	Orkige::QuitOnEscape& quitOnEscape = *context.quitOnEscape;
	Orkige::SoundManager& soundManager = *context.soundManager;
	Orkige::PhysicsWorld& physicsWorld = *context.physicsWorld;
	Orkige::TweenManager& tweenManager = *context.tweenManager;
	Orkige::TimerManager& timerManager = *context.timerManager;
	Orkige::ScriptTaskManager& scriptTaskManager = *context.scriptTaskManager;
	Orkige::LevelManager& levelManager = *context.levelManager;
	Orkige::ScreenFade& screenFade = *context.screenFade;
	Orkige::ScreenShake& screenShake = *context.screenShake;
	Orkige::DebugDraw& debugDraw = *context.debugDraw;
	Orkige::TimeControl& timeControl = *context.timeControl;
	Orkige::PlayerDebugLink& debugLink = *context.debugLink;
	Orkige::FrameStatsUtil& frameStats = *context.frameStats;
	Orkige::AppLifecycle& lifecycle = *context.lifecycle;
	bool& physicsNeeded = context.physicsNeeded;
	unsigned long& frameLimit = context.frameLimit;
	bool& automatedRun = context.automatedRun;
	bool& running = context.running;
	unsigned long& frameCount = context.frameCount;
	std::chrono::steady_clock::time_point& lastFrameTime = context.lastFrameTime;
	auto reloadSceneFrom = [&context](std::string const & newScenePath) -> bool
	{
		return context.reloadSceneFrom(newScenePath);
	};
	auto applyLifecycle = [&context](Orkige::AppLifecycle::Event event)
	{
		context.applyLifecycle(event);
	};
	auto recordScriptErrorBreadcrumbs = [&context]()
	{
		context.recordScriptErrorBreadcrumbs();
	};

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_QUIT:
			running = false;
			break;
		// mobile app lifecycle (SDL raises these on iOS/Android only):
		// route them through the AppLifecycle contract. The back button
		// is NOT here - it arrives as a KC_WEBBACK key event through
		// injectEvent, delivered to the game, never quitting.
		case SDL_EVENT_WILL_ENTER_BACKGROUND:
			applyLifecycle(
				Orkige::AppLifecycle::Event::WillEnterBackground);
			break;
		case SDL_EVENT_DID_ENTER_BACKGROUND:
			applyLifecycle(
				Orkige::AppLifecycle::Event::DidEnterBackground);
			break;
		case SDL_EVENT_WILL_ENTER_FOREGROUND:
			applyLifecycle(
				Orkige::AppLifecycle::Event::WillEnterForeground);
			break;
		case SDL_EVENT_DID_ENTER_FOREGROUND:
			applyLifecycle(
				Orkige::AppLifecycle::Event::DidEnterForeground);
			break;
		case SDL_EVENT_LOW_MEMORY:
			applyLifecycle(Orkige::AppLifecycle::Event::LowMemory);
			break;
		// the drawable changed size (a desktop window resize, a device
		// ROTATION): resize the render target and re-derive the window
		// camera's aspect so the image never stretches. Same facade call
		// the editor makes on its own window; CameraComponent's fit modes
		// then see the new aspect on their next tick.
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			if (render != NULL)
			{
				render->notifyWindowResized();
				unsigned int resizedWidth = 0;
				unsigned int resizedHeight = 0;
				render->getWindowSize(resizedWidth, resizedHeight);
				oDebugMsg("player", 0, "window drawable resized to " <<
					resizedWidth << "x" << resizedHeight);
			}
			break;
		case SDL_EVENT_TERMINATING:
			// the OS is killing us: final flush + marker, then leave the
			// loop so the orderly shutdown path still runs if it can
			applyLifecycle(Orkige::AppLifecycle::Event::Terminating);
			running = false;
			break;
		default:
			break;
		}
		inputManager.injectEvent(event);
	}
	if (quitOnEscape.quitRequested)
	{
		running = false;
	}

	// remote debugging: pump the protocol and act on editor commands
	// BEFORE stepping, so pause/step/set_property apply to this frame
	debugLink.update(gameObjectManager, scenePath);
	if (debugLink.isQuitRequested())
	{
		running = false;
	}
	const bool stepOnce = debugLink.consumePendingStep();

	// measured frame dt through the shared clamp policy (simulated
	// time on automated runs, real dt for a human - see
	// AppHost::clampFrameDelta)
	const std::chrono::steady_clock::time_point frameTime =
		std::chrono::steady_clock::now();
	float deltaTime = std::chrono::duration<float>(
		frameTime - lastFrameTime).count();
	lastFrameTime = frameTime;
	frameStats.addFrame(deltaTime);
	frameStats.maybeWarnSlow("orkige_player");
	deltaTime = Orkige::AppHost::clampFrameDelta(deltaTime,
		automatedRun);
	// pause gates the stepping only - rendering and the debug
	// protocol stay alive; a step is exactly one fixed physics tick.
	// The lifecycle sim gate (isSimPaused) pauses gameplay while the app
	// is backgrounded, the same way the editor's pause does; a debug
	// step still forces exactly one tick for inspection.
	const bool advanceWorld =
		(!debugLink.isPaused() && !lifecycle.isSimPaused()) || stepOnce;
	if (stepOnce)
	{
		deltaTime = Orkige::PhysicsWorld::FIXED_TIMESTEP;
	}
	if (advanceWorld)
	{
		// the CANONICAL tick order (input -> scripts/world -> tweens ->
		// physics -> deferred-load pump, then the presentation layers) is
		// fenced and reasoned ONCE, in Orkige::advanceGameWorld
		// (engine_runtime/GameHost.h) - read it there before adding
		// anything to a frame. The player supplies its subsystems and the
		// deferred-load pump's scene loader; every other host that ticks a
		// world calls the same function.
		Orkige::GameTick tick;
		tick.inputActions = &inputActions;
		tick.httpClient = context.httpClient ? &*context.httpClient : nullptr;
		tick.monetization =
			context.monetization ? &*context.monetization : nullptr;
		tick.gameObjects = &gameObjectManager;
		tick.tweens = &tweenManager;
		tick.timers = &timerManager;
		tick.scriptTasks = &scriptTaskManager;
		tick.physics = &physicsWorld;
		tick.physicsNeeded = physicsNeeded;
		tick.levels = &levelManager;
		tick.sound = &soundManager;
		tick.screenFade = &screenFade;
		tick.screenShake = &screenShake;
		tick.debugDraw = &debugDraw;
		tick.timeScale = timeControl.getTimeScale();
		tick.loadScene = [&](Orkige::String const & pendingScene) -> bool
		{
			// resolve project-relative scene paths through the open
			// project (an already-existing path passes through)
			Orkige::String resolvedScene = pendingScene;
			std::error_code ignored;
			if (project.isLoaded() &&
				!std::filesystem::exists(resolvedScene, ignored))
			{
				resolvedScene = project.resolvePath(pendingScene);
			}
			return reloadSceneFrom(resolvedScene);
		};
		Orkige::advanceGameWorld(tick, deltaTime);
	}

	// streaming: hierarchy on change (checked every N frames),
	// selected object state at ~15Hz, queued log lines - also while
	// paused
	{
		OPROFILE("debug");
		debugLink.stream(gameObjectManager, frameCount);
	}

	// crash breadcrumbs: mirror engine warnings/errors and record any
	// newly-failed ScriptComponent (both once, flushed to disk)
	if (Orkige::Breadcrumbs::getSingletonPtr())
	{
		for (Orkige::EngineLogCapture::Line const& line :
			breadcrumbLog.drain())
		{
			if (line.level == "warning" || line.level == "error")
			{
				Orkige::Breadcrumbs::getSingleton().record("log",
					line.text, { { "level", line.level } });
			}
		}
	}
	recordScriptErrorBreadcrumbs();

	// backgrounded: mobile GPU work in the background is an OS kill (iOS
	// especially), so the loop must not draw until the app returns. Idle
	// the loop at a cheap poll pace so the foreground event is still
	// picked up promptly; automated runs skip the sleep so they stay
	// fast. Desktop never stops rendering (SDL raises no background
	// events there).
	// sprite-run batching resolves for the frame about to render: AFTER all
	// gameplay/debug mutations of this frame (also while paused, so a
	// debug-protocol edit still lands in its run), BEFORE renderOneFrame.
	// Dirty-tracked - a frame where nothing moved re-uploads nothing.
	if (context.spriteBatcher)
	{
		OPROFILE("present");
		context.spriteBatcher->update();
	}

	if (lifecycle.isRenderingStopped())
	{
		if (!automatedRun)
		{
			SDL_Delay(32);
		}
	}
	else
	{
		OPROFILE("render");
		if (!render->renderOneFrame())
		{
			running = false;
		}
	}
	// editor-requested screenshot of the RUNNING game (MSG_SCREENSHOT):
	// captured AFTER the frame renders so it shows what the player just
	// drew, then acknowledged over the debug link. The capture lives
	// here (not in PlayerDebugLink) to keep the protocol code free of
	// renderer types.
	{
		std::string screenshotPath;
		if (debugLink.consumePendingScreenshot(screenshotPath))
		{
			render->saveWindowContents(screenshotPath);
			// saveWindowContents is fire-and-forget; the file's presence
			// (non-empty) is the honest success signal reported back
			std::error_code shotError;
			const bool captured =
				std::filesystem::exists(screenshotPath, shotError) &&
				std::filesystem::file_size(screenshotPath, shotError) > 0;
			debugLink.notifyScreenshotSaved(screenshotPath, captured,
				captured ? std::string()
					: std::string("saveWindowContents wrote no file"));
			SDL_Log("orkige_player: debug screenshot %s -> '%s'",
				captured ? "written" : "FAILED", screenshotPath.c_str());
		}
	}
	// frame boundary for the perf instruments: fold the allocation
	// counters and the profiler's scope tree into their last-frame
	// snapshots. AFTER render (the frame is complete), BEFORE the trace
	// (a sampled frame carries ITS OWN phase times and alloc count).
	// Worker threads are quiescent here - physics jobs completed inside
	// the physics update.
	Orkige::MemoryManager::endFrame();
	Orkige::ProfileManager::endFrame();

	// per-scene benchmark sample: AFTER the frame boundary folded the
	// instruments (so lastFrameMilliseconds / alloc totals / phase means
	// are this frame's), pairing them with the render facade's FrameStats
	// (triangles/batches/texture memory, which the core layer can't see).
	// A no-op unless armed with a results file and a scene is open.
	if (benchmarkRecorder.isArmed() && benchmarkRecorder.sceneOpen())
	{
		const Orkige::RenderSystem::FrameStats stats =
			render->getFrameStats();
		benchmarkRecorder.sampleFrame(
			static_cast<unsigned int>(stats.triangleCount),
			static_cast<unsigned int>(stats.batchCount),
			static_cast<float>(stats.textureMemoryBytes) /
				(1024.0f * 1024.0f));
	}

	// editor-requested TRACE recording (MSG_RECORD_START): while active,
	// sample the world every Nth frame and interleave this frame's
	// physics contacts as events, until the time budget is spent (or
	// Stop arrives), then write the .jsonl trace and acknowledge. The
	// world sampling lives in the debug link; contacts are resolved here
	// where the player already owns the physics world.
	if (debugLink.isRecording())
	{
		debugLink.traceFrame(gameObjectManager, frameCount, deltaTime);
		// fold this frame's script/gui/engine bus events into the trace
		// event stream (drains the bus trace-capture buffer)
		debugLink.traceScriptEvents();
		// harvest THIS frame's contacts only when physics advanced (a
		// paused frame reuses the last drained list - do not re-emit)
		if (advanceWorld && physicsNeeded)
		{
			for (Orkige::PhysicsWorld::ContactEvent const & contact :
				physicsWorld.getFrameContacts())
			{
				Orkige::GameObject* objectA =
					Orkige::RigidBodyComponent::bodyOwner(
						physicsWorld, contact.bodyA);
				Orkige::GameObject* objectB =
					Orkige::RigidBodyComponent::bodyOwner(
						physicsWorld, contact.bodyB);
				if (objectA && objectB)
				{
					debugLink.traceContact(objectA->getObjectID(),
						objectB->getObjectID(), contact.began);
				}
			}
		}
		if (debugLink.recordingShouldFinish())
		{
			debugLink.finishRecording();
			SDL_Log("orkige_player: debug trace finished");
		}
	}
	++frameCount;

	// ORKIGE_CRASH_SELFCHECK: fire a deliberate SIGSEGV at the requested frame
	// so the crash-marker path runs end to end (the crash handler stamps the
	// "crash" crumb, then the OS report generates; the driver re-boots and
	// expects the "previous run crashed" warning). Only when the marker actually
	// armed - a sanitizer build leaves the fatal handlers to ASan and never
	// self-crashes (the driver skips then).
	if (context.crashSelfcheckFrame != 0 && context.crashMarkerArmed
		&& frameCount >= context.crashSelfcheckFrame)
	{
		std::raise(SIGSEGV);
	}

	// the per-frame selfcheck chain (every ORKIGE_*_SELFCHECK
	// script - see PlayerSelfChecks.cpp)
	context.selfChecks.perFrame(context);

	if (frameLimit != 0 && frameCount >= frameLimit)
	{
		running = false;
	}

	return running;
}

int main(int argc, char** argv)
{
	// the process-level abort diagnostics (engine_runtime/GameHost.h): the
	// Debug CRT's assertion reporting routed to stderr, and the terminate
	// trap that names an escaping exception plus the breadcrumb tail before
	// the process dies - so an abort with no assert dialog still says what
	// killed it in a headless log.
	Orkige::installAbortDiagnostics("orkige_player");
	// the player's whole world lives on ONE heap context
	// (PlayerContext.h): main() fills it in boot order, playerIterate
	// reads it back per frame. Owned here where the frame loop returns;
	// where it outlives main()'s frame the loop takes ownership instead and
	// destroys the context, exactly once, when the run ends
	// (@see Orkige::gameFrameLoopOwnsContext).
	std::unique_ptr<PlayerContext> contextOwner(new PlayerContext());
	PlayerContext& context = *contextOwner;
	// arguments: the player CLI contract (an optional positional scene file,
	// --project <dir-or-.orkproj> - play a whole project, its
	// assets/ and scenes/ become resource locations and its main scene is the
	// default scene - and --debug-port N for the editor's play mode), parsed
	// by the shared PlayerArguments so native game modules stay identical.
	// The bare-scene invocation (orkige_player <scene.oscene>) keeps working.
	const Orkige::PlayerArguments arguments =
		Orkige::PlayerArguments::parse(argc, argv);
	if (!arguments.valid)
	{
		SDL_Log("orkige_player: unknown argument '%s'",
			arguments.unknownArgument.c_str());
		SDL_Log("usage: orkige_player [--project <dir-or-.orkproj>] "
			"[scene.oscene] [--debug-port N] "
			"[--run-tests [--test-filter <substring>]]");
		return 1;
	}
	// --run-tests: a test suite belongs to a PROJECT (its tests/ directory and
	// its scripts/ libraries), never to a loose scene - say so before booting
	if (arguments.runTests && arguments.projectPath.empty())
	{
		SDL_Log("orkige_player: --run-tests needs --project <dir-or-.orkproj> "
			"(a test suite belongs to a project)");
		return 1;
	}
	// and a build with no scripting backend cannot answer the question at all -
	// say so HERE, before a window and an engine are stood up for nothing.
	// backendName() is the compiled-in fact (unlike available(), which also
	// needs the runtime to be booted), so this is honest pre-boot.
	if (arguments.runTests &&
		std::strcmp(Orkige::ScriptRuntime::backendName(), "none") == 0)
	{
		SDL_Log("orkige_player: --run-tests needs a scripting backend - this "
			"build has none (ORKIGE_SCRIPTING=OFF)");
		return 1;
	}
	std::string& scenePath = context.scenePath;
	scenePath = arguments.scenePath;
	std::string projectPath = arguments.projectPath;
	// ORKIGE_PAK_SELFCHECK mounts a zip and reads its whole scene/content
	// through the resource system - no scene path on the command line is needed
	// (the scene is read from the mounted pak below), so it exempts the
	// desktop "scene required" gate. Read here (before that gate); the full
	// selfcheck env read happens later, once the engine is up.
	const bool pakSelfCheck = std::getenv("ORKIGE_PAK_SELFCHECK") != nullptr;
	// ORKIGE_PAK_SCRIPT_SELFCHECK likewise needs no scene argument: it mounts a
	// script-only pak and builds its own GameObject in the synchronous check, so
	// it too exempts the desktop "scene required" gate below.
	const bool pakScriptSelfCheck =
		std::getenv("ORKIGE_PAK_SCRIPT_SELFCHECK") != nullptr;

	// the platform harness (engine_runtime/GameHost.h): materialize a
	// packaged app's content before anything tries to READ it - an APK's
	// assets are not files and a browser export's payload is one archive -
	// and resolve the media/content directories this platform reads from.
	// A desktop run has no prologue beyond letting an exported bundle's own
	// Media/ override the build-tree default.
	Orkige::GamePlatform& platform = context.platform;
	{
		Orkige::GamePlatformConfig platformConfig;
		platformConfig.appName = "Orkige Player";
		platformConfig.logTag = "orkige_player";
		platformConfig.desktopMediaDirectory = ORKIGE_PLAYER_MEDIA_DIR;
		// sample assets (test_mesh.glb; scene meshes load lazily) and the
		// jumper sample assets, so the editor's play mode works on samples/*
		// scenes too; a packaged app carries the same two sub-trees
		platformConfig.desktopContentDirectories = {
			ORKIGE_PLAYER_ASSET_DIR, ORKIGE_PLAYER_JUMPER_ASSET_DIR };
		platformConfig.bundleContentSubdirectories = {
			"assets", "jumper_media" };
		if (!platform.boot(platformConfig))
		{
			return 1;
		}
	}

	// exported app, launched WITHOUT arguments (double-click): the
	// orkige_project.txt marker next to the executable's resources names the
	// bundled default project - macOS .app: Contents/Resources/, iOS .app:
	// the flat bundle root, a browser export: the module filesystem root the
	// payload unpacked into, Android: the content root the APK extracted to.
	// Dev runs carry no marker and are unaffected. See PlayerBundle in
	// engine_runtime/PlayerRuntime.h.
	bool bundledProjectRun = false;
	if (projectPath.empty() && scenePath.empty())
	{
		projectPath =
			Orkige::PlayerBundle::findBundledProject(platform.getContentRoot());
		bundledProjectRun = !projectPath.empty();
		if (bundledProjectRun)
		{
			SDL_Log("orkige_player: exported app - bundled project '%s'",
				projectPath.c_str());
		}
	}

	// --project: load the manifest now (pure filesystem work, honest errors);
	// it roots the resource locations registered below and provides the
	// default scene when no scene argument was given
	Orkige::Project& project = context.project;
	if (!projectPath.empty())
	{
		std::string projectError;
		if (!project.load(projectPath, &projectError))
		{
			SDL_Log("orkige_player: FAILED - %s", projectError.c_str());
			return 1;
		}
		if (scenePath.empty())
		{
			scenePath = project.getMainScenePath();
			if (scenePath.empty())
			{
				SDL_Log("orkige_player: FAILED - project '%s' has no main "
					"scene and no scene argument was given",
					project.getName().c_str());
				return 1;
			}
		}
		else
		{
			// scene override: taken as-given when it exists (absolute or
			// cwd-relative), otherwise resolved against the project root
			std::error_code ignored;
			if (!std::filesystem::exists(scenePath, ignored))
			{
				scenePath = project.resolvePath(scenePath);
			}
		}
	}

	// the platform's scene rule: a packaged app defaults an empty path to the
	// scene it bundles and anchors a relative one in its writable root (the
	// shape a device-side play session pushes it in); a desktop path passes
	// through, so an empty one stays empty and the usage line below fires.
	scenePath = platform.resolveScenePath(scenePath);
	if (scenePath.empty() && !pakSelfCheck && !pakScriptSelfCheck)
	{
		SDL_Log("usage: orkige_player [--project <dir-or-.orkproj>] "
			"[scene.oscene] [--debug-port N]");
		return 1;
	}
	int& exitCode = context.exitCode;
	// crash breadcrumbs, declared before the host so the trail stays alive
	// through the whole engine teardown: an always-on, flush-per-entry trail
	// of engine events (scene loads, script errors, warnings, boot/shutdown)
	// that survives a hard crash (SIGSEGV/OOM/watchdog kill). Written to the
	// writable app dir (see the setFile block below) so the editor can read
	// the PREVIOUS session's trail after an abnormal exit.
	Orkige::Breadcrumbs& breadcrumbs = context.breadcrumbs;
	// per-scene performance capture (core_debug/BenchmarkRecorder): dormant
	// unless armed from ORKIGE_BENCHMARK below. Declared alongside breadcrumbs
	// so its results artifact is flushed through the whole teardown.
	Orkige::BenchmarkRecorder& benchmarkRecorder = context.benchmarkRecorder;
	// mobile orientation: constrain the window / view-controller orientations
	// to the project's export.orientation BEFORE the window is created (the
	// rule itself lives in GamePlatform::applyOrientationPolicy). An explicit
	// --orientation (a device play session, where the manifest does not travel
	// to the device) wins over the manifest.
	platform.applyOrientationPolicy(!arguments.orientation.empty()
		? arguments.orientation
		: project.getSetting("export.orientation", "portrait"));
	// the shared boot spine (engine_runtime/AppHost.h): SDL window (mobile
	// fullscreen / desktop high-pixel-density), engine singletons, the
	// per-flavor Engine boot, the window-camera rig and the GameObject world
	Orkige::AppHost& host = context.host;
	{

		// the writable paths, per platform (GamePlatform::resolveDirectories):
		// a sandboxed app writes into its container, an exported desktop app
		// into the app-support directory (a double-clicked app runs with an
		// unwritable cwd) and a dev run keeps the historical cwd log.
		platform.resolveDirectories("orkige_player.log", bundledProjectRun);
		const std::string engineLogPath = platform.getEngineLogPath();
		// the breadcrumb file: the platform's writable state dir;
		// ORKIGE_BREADCRUMB_DIR overrides it (test isolation). rotate() at
		// boot moves the last run's file to breadcrumbs.prev.jsonl.
		{
			std::string breadcrumbDir = platform.getStateDirectory();
			if (const char* dirEnv = std::getenv("ORKIGE_BREADCRUMB_DIR"))
			{
				breadcrumbDir = dirEnv;
				if (!breadcrumbDir.empty() && breadcrumbDir.back() != '/')
				{
					breadcrumbDir += '/';
				}
			}
			if (!breadcrumbDir.empty())
			{
				std::error_code ignored;
				std::filesystem::create_directories(breadcrumbDir, ignored);
				breadcrumbs.setFile(breadcrumbDir + "breadcrumbs.jsonl");
				breadcrumbs.rotate();
				// the previous session's trail was just rotated aside; if it
				// ends in a crash marker, say so ONCE - the machine-detectable
				// "the last run died" signal (a phone shows no crash dialog).
				{
					Orkige::String prevTrail;
					Orkige::String crashSignal;
					if (Orkige::Breadcrumbs::loadFile(
							breadcrumbs.getPreviousFile(), prevTrail)
						&& Orkige::Breadcrumbs::lastEntryIsCrash(
							prevTrail, crashSignal))
					{
						oDebugWarn("breadcrumbs", 0,
							"the previous run crashed ("
							<< (crashSignal.empty()
								? "unknown signal" : crashSignal.c_str())
							<< ") - trail in breadcrumbs.prev.jsonl");
					}
				}
				breadcrumbs.record("boot", scenePath);
				// arm the fatal-signal crash marker on the fresh live file: a
				// SIGSEGV/OOM-kill/watchdog death now stamps a final "crash"
				// crumb before the OS report generates. Returns false (marker
				// stands down) on a sanitizer build - ASan owns the handlers.
				context.crashMarkerArmed = breadcrumbs.installCrashHandler();
				// widen the abort coverage: chain a stderr last-gasp line IN
				// FRONT of the Breadcrumbs file marker (installed just above,
				// so it is the prior disposition the trap captures and calls)
				Orkige::installAbortSignalTrap("orkige_player");
				// ORKIGE_CRASH_SELFCHECK=<frame>: the deliberate crash-marker
				// test hook (see the playerIterate raise() below). The marker
				// line lets the driver decide arm-vs-skip from run 1's stdout.
				if (const char* crashEnv =
						std::getenv("ORKIGE_CRASH_SELFCHECK"))
				{
					context.crashSelfcheckFrame =
						std::strtoul(crashEnv, nullptr, 10);
					SDL_Log("orkige_player: crash marker %s",
						context.crashMarkerArmed
							? "armed" : "unavailable (sanitizer build)");
				}
			}
			// per-scene benchmark capture: OPT-IN, armed only when ORKIGE_BENCHMARK
			// is set. Writes a JSONL results artifact (benchmark-<utcstamp>.jsonl)
			// into the same writable app dir as the breadcrumbs (ORKIGE_BENCHMARK_DIR
			// overrides it for test isolation). The compiled-in identity - flavor,
			// render system, build config, platform, sha (from ORKIGE_BUILD_SHA;
			// there is no compiled-in sha define) - is gathered here.
			if (std::getenv("ORKIGE_BENCHMARK") != nullptr)
			{
				// ISO 8601 UTC start stamp + a filesystem-safe file stamp
				const std::time_t nowTime = std::time(nullptr);
				std::tm utcTm{};
#if defined(_WIN32)
				gmtime_s(&utcTm, &nowTime);
#else
				gmtime_r(&nowTime, &utcTm);
#endif
				char isoBuf[32] = { 0 };
				char stampBuf[32] = { 0 };
				std::strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
				std::strftime(stampBuf, sizeof(stampBuf), "%Y%m%dT%H%M%SZ", &utcTm);

				Orkige::BenchmarkMeta meta;
				meta.utc = isoBuf;
				if (const char* sha = std::getenv("ORKIGE_BUILD_SHA"))
				{
					meta.engineSha = sha;
				}
				// the compiled-in identity - flavor, platform, render system
				// and build configuration (Orkige::describeBuild)
				const Orkige::GameBuildIdentity identity =
					Orkige::describeBuild();
				meta.flavor = identity.flavor;
				meta.platform = identity.platform;
				meta.renderSystem = identity.renderSystem;
				meta.build = identity.build;
				if (const char* osName = SDL_GetPlatform())
				{
					meta.deviceOs = osName;
				}
				if (const char* mode = std::getenv("ORKIGE_BENCHMARK_MODE"))
				{
					meta.scenario = mode;
				}
				if (project.isLoaded())
				{
					meta.project = project.getName();
				}
				benchmarkRecorder.setMeta(meta);

				std::string benchmarkDir = breadcrumbDir;
				if (const char* dirEnv = std::getenv("ORKIGE_BENCHMARK_DIR"))
				{
					benchmarkDir = dirEnv;
					if (!benchmarkDir.empty() && benchmarkDir.back() != '/')
					{
						benchmarkDir += '/';
					}
				}
				if (!benchmarkDir.empty())
				{
					std::error_code benchErr;
					std::filesystem::create_directories(benchmarkDir, benchErr);
					benchmarkRecorder.setFile(benchmarkDir +
						"benchmark-" + stampBuf + ".jsonl");
				}
			}
		}
		// the selfcheck env hooks (they also derive the frame cap and
		// the automated-run pacing decision below)
		context.selfChecks.readEnvironment(context);
		// --run-tests IS an automated run: a machine drives the game to a
		// verdict with nobody at the window. It matters beyond pacing, because
		// a play-mode test's budget counts TICKS while its waits are measured
		// in SIMULATED SECONDS - and only an automated run makes the two agree.
		// With a real delta, wait(0.5) costs about 30 frames behind vsync and
		// several hundred on a headless host that free-runs, against one fixed
		// tick limit; the fixed AUTOMATED_FRAME_DELTA makes it exactly 30
		// everywhere, so a budget overrun means a wedged wait rather than a
		// fast machine.
		context.automatedRun = context.automatedRun || arguments.runTests;
		const bool automatedRun = context.automatedRun;

		// the engine media root the platform harness resolved: a packaged app
		// reads the media it carries, a desktop dev run the build tree and an
		// exported .app the Media/ in its own Resources (self-contained - no
		// dependency-closure or source-tree path is touched at runtime)
		const std::string playerMediaDir = platform.getMediaDirectory();
		// the host boot: mobile is a fullscreen native window, desktop bakes
		// the scene path into the title. The media dir feeds the classic RTSS
		// registration AND the next flavor's Hlms override: whenever it is not
		// the baked build-tree default, the backend's own default would name a
		// path that does not exist for this run.
		Orkige::AppHostConfig hostConfig;
		if (Orkige::GamePlatform::isMobile())
		{
			hostConfig.windowTitle = "Orkige Player";
			// export.orientation "auto": the fullscreen surface follows
			// device rotation (see the orientation policy above)
			hostConfig.resizableWindow = platform.followsDeviceRotation();
		}
		else
		{
			hostConfig.windowTitle = "Orkige Player - " + scenePath;
		}
		if (platform.overridesEngineMedia())
		{
			hostConfig.hlmsMediaDir = playerMediaDir;
		}
		hostConfig.automatedRun = automatedRun;
		hostConfig.engineLogFile = engineLogPath;
		hostConfig.classicMediaDir = playerMediaDir;
		// ORKIGE_FAKE_CONTENT_SCALE=N simulates a dense (2x-3x) display for
		// the UI scale path on any desktop - the same automation seam the
		// hello demos use, so a headless test can exercise the HUD layout at
		// a phone/retina density (glyph scale + scaled widget sizes).
		if (const char* fakeScaleEnv = std::getenv("ORKIGE_FAKE_CONTENT_SCALE"))
		{
			const float fakeScale = std::strtof(fakeScaleEnv, nullptr);
			if (fakeScale > 0.0f)
			{
				Orkige::PlatformWindow::setContentScaleOverride(fakeScale);
			}
		}
		// ORKIGE_WINDOW_SIZE=WxH overrides the desktop window size (e.g. a
		// portrait 540x960 to preview a phone aspect from the desktop). Ignored
		// on mobile, where the window is the device screen. A dev/CI affordance.
		if (const char* sizeEnv = std::getenv("ORKIGE_WINDOW_SIZE"))
		{
			int w = 0;
			int h = 0;
			if (std::sscanf(sizeEnv, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
			{
				hostConfig.windowWidth = w;
				hostConfig.windowHeight = h;
			}
		}
		if (!host.boot(hostConfig, [&]()
			{
				Orkige::RenderSystem* render = host.getRenderSystem();
				// the engine-default font (Nunito) directory as a resource
				// location so a project's .ogui can reference the font by name
				// (font-atlas baking resolves the ttf by resource name across
				// all groups). Register the bundled dir (present in an
				// exported/device bundle) and the dev-tree dir (build tree);
				// is_directory keeps a missing one a silent skip.
				std::error_code fontDirError;
				if (std::filesystem::is_directory(playerMediaDir + "/fonts",
					fontDirError))
				{
					render->addResourceLocation(playerMediaDir + "/fonts");
				}
				if (std::filesystem::is_directory(ORKIGE_PLAYER_FONT_DIR,
					fontDirError))
				{
					render->addResourceLocation(ORKIGE_PLAYER_FONT_DIR);
				}
				// the engine water media dir (the shared water plane mesh +
				// tiling water normal map WaterComponent references), the same
				// bundled/dev-tree pair as the fonts above
				std::error_code waterDirError;
				if (std::filesystem::is_directory(playerMediaDir + "/water",
					waterDirError))
				{
					render->addResourceLocation(playerMediaDir + "/water");
				}
				if (std::filesystem::is_directory(ORKIGE_PLAYER_WATER_DIR,
					waterDirError))
				{
					render->addResourceLocation(ORKIGE_PLAYER_WATER_DIR);
				}
				// the engine decal media dir (default mark + blob-shadow textures
				// DecalComponent references), the same bundled/dev-tree pair
				std::error_code decalDirError;
				if (std::filesystem::is_directory(playerMediaDir + "/decals",
					decalDirError))
				{
					render->addResourceLocation(playerMediaDir + "/decals");
				}
				if (std::filesystem::is_directory(ORKIGE_PLAYER_DECAL_DIR,
					decalDirError))
				{
					render->addResourceLocation(ORKIGE_PLAYER_DECAL_DIR);
				}
				// DEVICELESS: everything below this line is the low-level
				// shader tier - `.material`/`.program` SCRIPTS. A render
				// system with no device has no GPU program manager to parse
				// them into and could not run what they define, so a deviceless
				// run registers none of it (@see engine_render/
				// RenderSystemSelection.h). Textures, meshes and fonts above
				// stay registered - a scene still loads its content.
				const bool devicelessRun =
					Orkige::RenderSystemSelection::devicelessRequested();
#ifdef ORKIGE_PLAYER_BLOOM_DIR
				// the engine bloom compositor media (the bright/blur/combine
				// material + shaders engine:setBloom needs), per flavor
				// (bloom/next vs bloom/classic - the build sets both defines).
				// EXCLUSIVE bundled-first pair, unlike the texture media above:
				// this dir carries a material SCRIPT, and parsing it from both
				// the bundle and the baked dev tree (an exported app run on a
				// dev machine) would double-define its GpuPrograms and abort
				// resource-group initialisation.
				std::error_code bloomDirError;
				if (devicelessRun)
				{
					// no shader tier in a deviceless run (see above)
				}
				else if (std::filesystem::is_directory(
					playerMediaDir + "/" ORKIGE_BLOOM_MEDIA_SUBDIR,
					bloomDirError))
				{
					render->addResourceLocation(
						playerMediaDir + "/" ORKIGE_BLOOM_MEDIA_SUBDIR);
				}
				else if (std::filesystem::is_directory(ORKIGE_PLAYER_BLOOM_DIR,
					bloomDirError))
				{
					render->addResourceLocation(ORKIGE_PLAYER_BLOOM_DIR);
				}
#endif
#ifdef ORKIGE_PLAYER_GRADE_DIR
				// the engine output-grade compositor media (the grade material +
				// shaders engine:setGrade needs), per flavor (grade/next vs
				// grade/classic). EXCLUSIVE bundled-first pair like the bloom media
				// above (a material SCRIPT - double-parsing aborts resource-group
				// init).
				std::error_code gradeDirError;
				if (devicelessRun)
				{
					// no shader tier in a deviceless run (see above)
				}
				else if (std::filesystem::is_directory(
					playerMediaDir + "/" ORKIGE_GRADE_MEDIA_SUBDIR,
					gradeDirError))
				{
					render->addResourceLocation(
						playerMediaDir + "/" ORKIGE_GRADE_MEDIA_SUBDIR);
				}
				else if (std::filesystem::is_directory(ORKIGE_PLAYER_GRADE_DIR,
					gradeDirError))
				{
					render->addResourceLocation(ORKIGE_PLAYER_GRADE_DIR);
				}
#endif
				// sample assets (test_mesh.glb; scene meshes load lazily via
				// Codec_Assimp) and the jumper sample assets, so the editor's
				// play mode works on samples/* scenes too. Registered only when
				// present: an exported app ships nothing but its project's
				// assets, and the (baked-in) dev source-tree paths must not
				// abort the run elsewhere
				for (std::string const& sampleAssetDir :
					platform.getContentDirectories())
				{
					std::error_code sampleDirError;
					if (std::filesystem::is_directory(sampleAssetDir,
						sampleDirError))
					{
						render->addResourceLocation(sampleAssetDir);
					}
				}
				// --project: the project's assets/ and scenes/ become resource
				// locations in the dedicated project group (the same group the
				// editor uses); a missing directory is skipped with an honest
				// line
				if (project.isLoaded())
				{
					const std::string projectAssetsDir =
						project.getAssetsDirectory();
					std::error_code assetErr;
					if (std::filesystem::is_directory(projectAssetsDir,
						assetErr))
					{
						// assets/ AND each subfolder as their own FLAT
						// location, so a subfolder asset resolves by BARE name
						// (a single recursive location indexes subfolder files
						// by sub-path on the next backend, so bare-name loads
						// miss there); matches the editor
						const auto registerFlat =
							[&](std::string const& directory)
						{
							render->addResourceLocation(directory,
								Orkige::RenderSystem::LT_FILESYSTEM,
								Orkige::Project::RESOURCE_GROUP_NAME, false);
						};
						registerFlat(projectAssetsDir);
						for (std::filesystem::recursive_directory_iterator
							it(projectAssetsDir, assetErr), end;
							!assetErr && it != end; it.increment(assetErr))
						{
							if (it->is_directory(assetErr))
							{
								registerFlat(it->path().string());
							}
						}
					}
					else
					{
						SDL_Log("orkige_player: project directory '%s' does "
							"not exist - not registered",
							projectAssetsDir.c_str());
					}
					const std::string projectScenesDir =
						project.getScenesDirectory();
					if (std::filesystem::is_directory(projectScenesDir,
						assetErr))
					{
						render->addResourceLocation(projectScenesDir,
							Orkige::RenderSystem::LT_FILESYSTEM,
							Orkige::Project::RESOURCE_GROUP_NAME,
							false);	// scenes/ flat
					}
					else
					{
						SDL_Log("orkige_player: project directory '%s' does "
							"not exist - not registered",
							projectScenesDir.c_str());
					}
					// the project CONTENT ROOT as ONE location so EVERY content
					// folder - scripts/, scenes/, config + the manifest, and
					// anything added later - resolves by its project-relative
					// SUB-PATH (e.g. "scripts/player.lua", "scenes/level.oscene")
					// through the archive-aware resource read, WITHOUT a
					// hand-picked subfolder list that could drift (that drift is
					// exactly why scripts/ was never registered). This is what
					// lets the fopen-tree loaders route through the resource
					// system: a loose file and a mounted pak/APK entry resolve by
					// the SAME name, so scripts read in place from a pak, no fopen.
					// NON-recursive on purpose: Ogre resolves a sub-path name
					// against a location on demand (a filesystem probe), so the
					// content root does NOT have to be walked/indexed - it never
					// descends into derived dirs (builds/, native/ build trees,
					// .git), so no build junk is indexed and no exclusion list is
					// needed. (Bulk media - textures by BARE name - keeps its own
					// flat per-folder registration above.)
					render->addResourceLocation(project.getRootDirectory(),
						Orkige::RenderSystem::LT_FILESYSTEM,
						Orkige::Project::RESOURCE_GROUP_NAME,
						false);	// non-recursive: sub-paths resolve by on-demand probe
					SDL_Log("orkige_player: project '%s' (root '%s') rooted "
						"the resource locations", project.getName().c_str(),
						project.getRootDirectory().c_str());
				}
				// a packaged app's bulk game media never left its archive:
				// mount each media DIRECTORY as its own flat sub-tree so its
				// files resolve by BARE resource name, exactly like the
				// loose-file registration above (a single sub-tree mount
				// would only resolve by full sub-path). A platform that
				// extracted everything mounts nothing.
				platform.mountPackagedContent(*render,
					Orkige::Project::RESOURCE_GROUP_NAME);
				// ORKIGE_PAK_SELFCHECK: mount the pak's sub-tree so its scene,
				// textures and sounds resolve through the resource system like
				// loose files (the reborn BigZip acceptance path, both flavors).
				// ORKIGE_PAK_SAMPLER_SELFCHECK mounts the same way - only its
				// scene comes from disk instead of the archive.
				if (context.selfChecks.pakCheck ||
					context.selfChecks.pakSamplerCheck)
				{
					render->mountPak(context.selfChecks.pakPath,
						context.selfChecks.pakMountPoint,
						Orkige::Project::RESOURCE_GROUP_NAME);
					SDL_Log("orkige_player: mounted pak '%s' (sub-tree '%s')",
						context.selfChecks.pakPath.c_str(),
						context.selfChecks.pakMountPoint.c_str());
				}
				// ORKIGE_PAK_SCRIPT_SELFCHECK: mount the whole pak so its
				// "scripts/pak_script.lua" resolves by name through the resource
				// system - the archive-in-place script read (no fopen, no
				// --project, so there is provably no loose file on disk)
				if (context.selfChecks.pakScriptCheck)
				{
					render->mountPak(context.selfChecks.pakScriptPath, "",
						Orkige::Project::RESOURCE_GROUP_NAME);
					SDL_Log("orkige_player: mounted script pak '%s'",
						context.selfChecks.pakScriptPath.c_str());
				}
			}))
		{
			// a boot that failed because this process reaches no display is
			// not a broken player: a windowed run has nothing to do on a
			// session that owns no screen. Report it as ctest's SKIP so it
			// never hides a real failure underneath it.
			return host.hasNoDisplaySession()
				? Orkige::AppHost::NO_DISPLAY_EXIT_CODE : 1;
		}
		Orkige::RenderSystem* render = host.getRenderSystem();
		context.render = render;
		Orkige::RenderWorld* world = host.getRenderWorld();
		context.world = world;

		// project scripts: ScriptComponent paths like "scripts/player.lua"
		// resolve against the open project's root directory; the discovery
		// walk registers a factory alias per SCRIPT COMPONENT KIND
		// (*.component.lua) so a scene that attaches one loads, and a named
		// kind binds its own script file on attach
		if (project.isLoaded())
		{
			host.getScriptRuntime().setScriptSearchRoot(
				project.getRootDirectory());
			Orkige::ScriptComponentRegistry::getSingleton().scanProject(
				project.getScriptsDirectory(), project.getRootDirectory());
		}

		// mirror the engine log's warning/error lines into the breadcrumb trail
		// (a dedicated capture - the debug link owns its own). Drained once per
		// frame in the loop below; a no-op when no breadcrumb file is set.
		context.breadcrumbLog.emplace();
		Orkige::EngineLogCapture& breadcrumbLog = *context.breadcrumbLog;
		breadcrumbLog.attach();

		// the window-camera rig from the host boot (fixed yaw keeps per-frame
		// lookAt calls roll-free) - project scripts drive it through the Lua
		// bindings (engine:getCamera():getNode(), engine:setCameraOrthographic,
		// ...)
		optr<Orkige::RenderCamera>& camera = context.camera;
		camera = host.getWindowCamera();
		optr<Orkige::RenderNode>& cameraNode = context.cameraNode;
		cameraNode = host.getCameraNode();

		// input pipeline: the poll loop below feeds every SDL event into the
		// InputManager, which triggers Orkige input events globally
		context.inputManager.emplace();
		Orkige::InputManager& inputManager = *context.inputManager;
		// phone-body vibration for mobile games (Lua `haptics` table). A device
		// build drives the taptic engine / Vibrator; desktop is an honest no-op
		// (isAvailable() == false). Like the InputManager, the editor never makes
		// one, so `haptics.*` is a no-op in edit mode.
		context.hapticManager.emplace();
		Orkige::HapticManager& hapticManager = *context.hapticManager;
		// tilt calibration persists per-device next to the engine log (writable
		// on every target - see engineLogPath); a calibrated neutral pose sticks
		// across runs. input:calibrateTilt() auto-writes it. ORKIGE_TILT_CALIB_FILE
		// overrides the path for test isolation; the editor never sets one (so
		// calibration persistence is an honest no-op in edit mode).
		{
			std::string calibFile;
			if (const char* calibEnv = std::getenv("ORKIGE_TILT_CALIB_FILE"))
			{
				calibFile = calibEnv;
			}
			else
			{
				calibFile = std::filesystem::path(engineLogPath)
					.parent_path().string();
				if (!calibFile.empty() && calibFile.back() != '/')
				{
					calibFile += '/';
				}
				calibFile += "tilt_calibration.osave";
			}
			inputManager.setCalibrationSaveFile(calibFile);
			inputManager.loadCalibration();
		}
		// synchronous env-hook verifications that need no render loop
		// (tilt calibration, haptics - see PlayerSelfChecks.cpp)
		if (const std::optional<int> checkExit =
			context.selfChecks.earlySynchronousChecks(context))
		{
			return *checkExit;
		}
		// action mapping layered on top: named, rebindable actions the scripts
		// query by intent (actions:pressed("jump")). Built-in defaults cover
		// the reference games; a project's input.oactions (manifest Settings
		// "input.actions") overrides. Ticked once per frame in the input slot.
		context.inputActions.emplace();
		Orkige::InputActionMap& inputActions = *context.inputActions;
		if (project.isLoaded())
		{
			inputActions.loadForProject(project);
		}
		// localisation: the Lua loc() accessor reads the active-language
		// strings from this table. A project's localisation directory (manifest
		// Settings "localisation", config-asset convention) of XLIFF 1.2 .xlf
		// files loads it; games without one just see the keys echoed back.
		context.stringTable.emplace();
		Orkige::StringTable& stringTable = *context.stringTable;
		if (project.isLoaded())
		{
			const std::string localisationRef = project.getSetting(
				Orkige::StringTable::LOCALISATION_SETTING_KEY);
			if (!localisationRef.empty())
			{
				const std::string localisationPath =
					project.resolvePath(localisationRef);
				if (!stringTable.loadXliffDirectory(localisationPath))
				{
					SDL_Log("orkige_player: localisation directory '%s' not "
						"loaded", localisationPath.c_str());
				}
				else
				{
					// pick the initial language. A forced override wins (a test
					// or a game re-applying a saved preference at boot via
					// locale.set); otherwise a HUMAN run matches the device's
					// preferred locales against the loaded languages. Automated
					// runs stay on the source language loadXliffDirectory
					// defaulted to, so a selfcheck's readback never depends on
					// the CI machine's OS locale.
					if (const char* forcedLanguage =
						std::getenv("ORKIGE_LANGUAGE"))
					{
						stringTable.setLanguage(forcedLanguage);
					}
					else if (!automatedRun)
					{
						Orkige::StringVector preferred;
						int localeCount = 0;
						SDL_Locale** locales =
							SDL_GetPreferredLocales(&localeCount);
						if (locales != nullptr)
						{
							for (int index = 0; index < localeCount; ++index)
							{
								if (locales[index] == nullptr ||
									locales[index]->language == nullptr)
								{
									continue;
								}
								std::string tag = locales[index]->language;
								if (locales[index]->country != nullptr &&
									locales[index]->country[0] != '\0')
								{
									tag += "-";
									tag += locales[index]->country;
								}
								preferred.push_back(tag);
							}
							SDL_free(locales);
						}
						const Orkige::String picked = Orkige::pickBestLanguage(
							stringTable.getLanguages(), preferred,
							stringTable.getSourceLanguage());
						if (!picked.empty())
						{
							stringTable.setLanguage(picked);
						}
					}
				}
			}
		}
		context.quitOnEscape.emplace();
		Orkige::QuitOnEscape& quitOnEscape = *context.quitOnEscape;
		optr<Orkige::EventListener>& escapeListener = context.escapeListener;
		escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&Orkige::QuitOnEscape::onKeyPressed, &quitOnEscape);

		// audio: the mixer lives on the SoundManager (per-source gain x group
		// volume, master on the AL listener); the "ears" ride the window
		// camera's rig node. A failed OpenAL init is NOT fatal - the game
		// runs silent, every sound call no-ops honestly (headless CI safety)
		context.soundManager.emplace(cameraNode);
		Orkige::SoundManager& soundManager = *context.soundManager;
		if (!soundManager.init())
		{
			SDL_Log("orkige_player: sound disabled - OpenAL init failed");
		}
		if (project.isLoaded())
		{
			// the mixer persists per project: manifest Settings audio.master
			// and audio.group.<name> (engine_sound/SoundManager.h)
			soundManager.applySettings(project.getSettings());
			// console variables persist per project: manifest Settings
			// "cvar.<name>". Applied here BEFORE the scene's scripts
			// run - an override for a cvar a script has not registered yet is
			// held and re-applied on registerCVar, so the order does not matter
			// (core_debug/CVarManager.h).
			Orkige::CVarManager::getSingleton().applySettings(
				project.getSettings());
		}
		// automation cvar seed: ORKIGE_CVARS="name=value,name2=value2" applies
		// through the SAME held-override path as the manifest (so it lands
		// whether the cvar is compile-time or registered later by a script). A
		// general test/CI hook - e.g. shrinking an attract-mode scene duration
		// for a headless run - that opens no socket and touches no shipped file.
		if (const char* cvarsEnv = std::getenv("ORKIGE_CVARS"))
		{
			std::map<Orkige::String, Orkige::String> seed;
			Orkige::String spec(cvarsEnv);
			std::size_t pos = 0;
			while (pos < spec.size())
			{
				std::size_t comma = spec.find(',', pos);
				Orkige::String pair = spec.substr(pos,
					comma == Orkige::String::npos ? Orkige::String::npos
						: comma - pos);
				std::size_t eq = pair.find('=');
				if (eq != Orkige::String::npos)
				{
					seed[Orkige::CVarManager::SETTING_PREFIX +
						pair.substr(0, eq)] = pair.substr(eq + 1);
				}
				if (comma == Orkige::String::npos)
				{
					break;
				}
				pos = comma + 1;
			}
			if (!seed.empty())
			{
				Orkige::CVarManager::getSingleton().applySettings(seed);
			}
		}

		// the GameObject world from the host boot (the component factories
		// registered there, before the manager existed)
		Orkige::GameObjectManager& gameObjectManager =
			host.getGameObjectManager();
		context.gameObjectManagerPtr = &gameObjectManager;
		context.physicsWorld.emplace(); // inert until init()
		Orkige::PhysicsWorld& physicsWorld = *context.physicsWorld;
		// tweens tick in the ordered block of the main loop below; scripts
		// start them through the Lua `tween` table (scene clears reap them
		// via the GameObjectManager::clear teardown hook)
		context.tweenManager.emplace();
		Orkige::TweenManager& tweenManager = *context.tweenManager;
		// deferred callbacks (Lua `timer` table): scheduled functions tick in
		// the SAME tween phase of the loop below (a timer is a degenerate tween).
		// Created like the TweenManager - the editor never makes one, so
		// `timer.*` is an honest no-op there; scene clears reap timers via the
		// GameObjectManager::clear teardown hook.
		context.timerManager.emplace();
		Orkige::TimerManager& timerManager = *context.timerManager;
		// suspended script tasks (Lua `script.async` + wait/waitFrames/
		// waitUntil): resumed in the SCRIPT phase of the loop below, at the
		// ONE resume site. Created like the TweenManager - the editor never
		// makes one, so `script.async` is an honest no-op there; scene clears
		// reap tasks via the GameObjectManager::clear teardown hook.
		context.scriptTaskManager.emplace();
		Orkige::ScriptTaskManager& scriptTaskManager = *context.scriptTaskManager;
		(void)scriptTaskManager;
		// the game's single named state (Lua `game` table): every setState fires
		// `game.stateChanged` on the event bus. Created like the TweenManager -
		// the editor never makes one, so `game.setState` is a no-op there.
		context.gameState.emplace();
		Orkige::GameState& gameState = *context.gameState;
		// the level director: the ordered level sequence, the DEFERRED
		// scene-load request that drives win->next-level and the progression
		// save. Created like the TweenManager - the editor never makes one, so
		// the Lua level/loadScene API is an honest no-op there. Only projects
		// carrying a levels.olevels (manifest Settings "levels") get a sequence
		// + persistence; scriptless games keep it inert.
		context.levelManager.emplace();
		Orkige::LevelManager& levelManager = *context.levelManager;
		// full-screen fade transitions (engine-owned overlay on a reserved high
		// draw layer, both flavors): scripts drive it through the Lua `screen`
		// table. Ticked LAST in the loop (a presentation overlay). Like the
		// TweenManager, the editor never makes one, so `screen.*` is a no-op there.
		context.screenFade.emplace();
		Orkige::ScreenFade& screenFade = *context.screenFade;
		// camera-space screen shake (engine-owned, both flavors): scripts drive
		// it through the Lua `screen.shake` table. Ticked LAST in the loop (a
		// presentation effect), like the fade. The editor never makes one.
		context.screenShake.emplace();
		Orkige::ScreenShake& screenShake = *context.screenShake;
		// immediate-mode 3D debug drawing (engine-owned, both flavors): scripts
		// draw lines/boxes/spheres through the Lua `draw` table, flushed into one
		// dynamic line mesh per frame. Ticked LAST (a presentation effect), like
		// the fade/shake. The editor never makes one, so `draw.*` is a no-op there.
		context.debugDraw.emplace();
		Orkige::DebugDraw& debugDraw = *context.debugDraw;
		// sprite-run batching (contiguous same-material sprite runs merge
		// into one draw each): SpriteComponents register themselves against
		// the singleton on sprite load; the loop resolves runs right before
		// rendering. The editor never makes one, so edit mode keeps the
		// plain per-quad path (merged pixels equal per-quad pixels anyway -
		// the render-toggle test proves it).
		context.spriteBatcher.emplace();
		// the gameplay time scale the loop applies to the scripts/tweens/physics
		// delta (Lua `world.setTimeScale`); the editor never makes one, so
		// gameplay stays real-time in edit mode.
		context.timeControl.emplace();
		Orkige::TimeControl& timeControl = *context.timeControl;
		// general per-project persistence (Lua `save` table): a typed
		// key->value store written atomically to the writable app dir. Set up for
		// any loaded project below; the editor never makes one, so `save.*` is an
		// honest no-op in edit mode.
		context.saveStore.emplace();
		Orkige::SaveStore& saveStore = *context.saveStore;
		// the HTTP(S) client the Lua `http` table talks to (leaderboards, remote
		// config, asset downloads). Costs nothing until a request is made - the
		// transport comes up on the first submit - and its completions are
		// delivered in the tick order's [1b] slot below.
		context.httpClient.emplace();
		// the store and ad seam the Lua `store` table talks to. Its answers are
		// delivered in the same tick-order slot the HTTP client's are, and for
		// the same reason: a platform payment sheet answers on the platform's
		// own queue. The editor never makes one, so `store.*` is an honest
		// no-op in edit mode.
		context.monetization.emplace();
		if (project.isLoaded())
		{
			Orkige::MonetizationService& money = *context.monetization;
			// WHAT THE GAME SELLS comes from the project's catalog file, a
			// config asset of public identifiers (@see ProductCatalogFile) -
			// never from code, because the identifier a product is sold under
			// is decided in each store's console and changes there.
			const std::string catalogRef = project.getSetting(
				Orkige::ProductCatalogFile::CATALOG_SETTING_KEY);
			if (!catalogRef.empty())
			{
				const std::string catalogPath = project.resolvePath(catalogRef);
				std::string catalogText;
				std::string catalogError;
				if (!readProjectTextFile(catalogPath, catalogRef, catalogText))
				{
					SDL_Log("orkige_player: product catalog '%s' not read",
						catalogRef.c_str());
				}
				else if (!Orkige::ProductCatalogFile::parse(catalogText,
					money.catalog(), &catalogError))
				{
					// a broken catalog leaves NOTHING sellable rather than a
					// half-read one that sells some products and not others
					SDL_Log("orkige_player: product catalog '%s' - %s",
						catalogRef.c_str(), catalogError.c_str());
				}
				else
				{
					SDL_Log("orkige_player: product catalog '%s' (%u products)",
						catalogRef.c_str(),
						static_cast<unsigned>(money.catalog().count()));
				}
			}
			// WHICH STORE stands behind the seam. The default is the platform's
			// own (or the honest absence where there is none); the simulator is
			// never a fallback a missing platform store decays into, because a
			// shipped game running against it would hand every product out for
			// free and look correct doing it.
			Orkige::StoreProviderChoice storeChoice = Orkige::SPC_PLATFORM;
			const std::string providerRef =
				project.getSetting(Orkige::STORE_PROVIDER_SETTING_KEY);
			if (!Orkige::storeProviderChoiceFromName(providerRef, storeChoice))
			{
				SDL_Log("orkige_player: '%s' is not a store provider "
					"(platform, simulated or none) - using the platform's",
					providerRef.c_str());
				storeChoice = Orkige::SPC_PLATFORM;
			}
			if (storeChoice == Orkige::SPC_SIMULATED)
			{
				money.setStoreProvider(
					std::make_unique<Orkige::SimulatedStoreProvider>(
						Orkige::SF_SIMULATED));
				SDL_Log("orkige_player: the SIMULATED store is installed - "
					"purchases are not real");
			}
			else if (storeChoice == Orkige::SPC_PLATFORM)
			{
				if (Orkige::StoreProvider* platformStore =
					Orkige::createPlatformStoreProvider())
				{
					money.setStoreProvider(
						std::unique_ptr<Orkige::StoreProvider>(platformStore));
				}
			}
			if (money.storeProvider() && !money.initializeStore())
			{
				SDL_Log("orkige_player: the store did not come up - purchases "
					"will be refused");
			}
			else if (!money.storeProvider())
			{
				// named, not silent: the game's own purchase callbacks will say
				// the same thing, but a developer reads this first
				const std::string reason =
					Orkige::platformStoreUnavailableReason();
				SDL_Log("orkige_player: no store provider - %s",
					reason.empty() ? "none was asked for" : reason.c_str());
			}
		}
		if (project.isLoaded())
		{
			const std::string levelsRef =
				project.getSetting(Orkige::LevelSequence::LEVELS_SETTING_KEY);
			if (!levelsRef.empty())
			{
				const std::string levelsPath = project.resolvePath(levelsRef);
				if (levelManager.sequence().load(levelsPath))
				{
					SDL_Log("orkige_player: level sequence '%s' (%d levels)",
						levelsRef.c_str(), levelManager.count());
				}
				else
				{
					SDL_Log("orkige_player: level sequence '%s' could not be "
						"loaded - single-scene run", levelsRef.c_str());
				}
				// progression (resume index + best moves) now rides the shared
				// SaveStore under "level.*" keys (set up just below, per project),
				// so there is no separate LevelManager save file to open here.
			}

			// general persistence (Lua `save` table AND the LevelManager
			// progression, which rides "level.*" keys in this ONE store): the
			// writable directory the engine log uses, under a PER-PROJECT file
			// name. Loaded at boot; flushed on a clean shutdown (below) and on any
			// explicit save.flush() / levels:saveProgress(). ORKIGE_PROGRESS_DIR /
			// ORKIGE_PROGRESS_RESET isolate/reset it for selfchecks.
			{
				std::string saveDir;
				if (const char* saveDirEnv = std::getenv("ORKIGE_PROGRESS_DIR"))
				{
					saveDir = saveDirEnv;
				}
				else
				{
					saveDir = std::filesystem::path(engineLogPath)
						.parent_path().string();
				}
				if (!saveDir.empty() && saveDir.back() != '/')
				{
					saveDir += '/';
				}
				if (!saveDir.empty())
				{
					std::error_code ignored;
					std::filesystem::create_directories(saveDir, ignored);
				}
				// slug the project name into a safe file stem (spaces / path
				// separators -> '_'); an unnamed project falls back to "orkige"
				std::string slug = project.getName();
				for (char& character : slug)
				{
					if (!std::isalnum(static_cast<unsigned char>(character)))
					{
						character = '_';
					}
				}
				if (slug.empty())
				{
					slug = "orkige";
				}
				const std::string saveFile = saveDir + slug + "_save.osave";
				if (std::getenv("ORKIGE_PROGRESS_RESET") != nullptr)
				{
					std::error_code ignored;
					std::filesystem::remove(saveFile, ignored);
				}
				saveStore.setSaveFile(saveFile);
				saveStore.load();
			}
		}

		bool sceneLoaded = false;
		if (context.selfChecks.pakCheck)
		{
			// the scene comes FROM the mounted pak: read its bytes THROUGH the
			// resource system (a zip entry cannot be fopen'd) and parse in
			// memory - the pak-mount scene-load proof
			Orkige::String sceneXml;
			if (!render->readResourceText("pak.oscene", sceneXml))
			{
				SDL_Log("orkige_player: FAILED - pak scene 'pak.oscene' not "
					"found in the mounted pak");
				return 1;
			}
			scenePath = "pak:pak.oscene";
			sceneLoaded = Orkige::SceneSerializer::loadSceneFromString(
				sceneXml, gameObjectManager, scenePath);
		}
		else if (context.selfChecks.pakScriptCheck)
		{
			// no scene: the script-in-pak check builds its own GameObject +
			// path-bound ScriptComponent in gameplaySynchronousChecks and
			// drives it, so an empty world here is the correct starting point
			scenePath = "pak:script";
			sceneLoaded = true;
		}
		else
		{
			sceneLoaded = Orkige::SceneSerializer::loadScene(
				scenePath, gameObjectManager);
		}
		if (!sceneLoaded)
		{
			SDL_Log("orkige_player: FAILED - could not load scene '%s'",
				scenePath.c_str());
			return 1;
		}
		Orkige::applyUnlitFixToLoadedModels(gameObjectManager);
		SDL_Log("orkige_player: scene '%s' loaded (%zu GameObjects)",
			scenePath.c_str(), gameObjectManager.getGameObjects().size());
		// open the first benchmark scene boundary (no-op when disarmed); a level
		// switch or an explicit Lua benchmark.begin re-opens it thereafter
		benchmarkRecorder.beginScene(scenePath);

		// the asset-id rename verification, right after the scene load
		if (const std::optional<int> checkExit =
			context.selfChecks.afterSceneLoad(context))
		{
			return *checkExit;
		}

		// remote debugging server (editor play mode): localhost only; the
		// editor keeps re-connecting until this point is reached, so the
		// engine boot time above does not matter
		context.debugLink.emplace();
		Orkige::PlayerDebugLink& debugLink = *context.debugLink;
		if (arguments.debugRequested)
		{
			if (!debugLink.start(arguments.debugPort,
				arguments.debugExposeNonLoopback))
			{
				SDL_Log("orkige_player: FAILED - could not listen on "
					"debug port %u",
					static_cast<unsigned>(arguments.debugPort));
				return 1;
			}
			SDL_Log("orkige_player: debug server listening on %s:%u",
				arguments.debugExposeNonLoopback ? "0.0.0.0 (ALL interfaces)"
					: "127.0.0.1",
				static_cast<unsigned>(debugLink.getPort()));
		}
#ifdef __EMSCRIPTEN__
		// Play in Browser, live session: a page cannot listen, so the
		// direction reverses - the editor appends
		// ?env.ORKIGE_DEBUG_CONNECT=127.0.0.1:<port> to the URL it opens
		// (the shell maps ?env.* onto the module environment) and the
		// runtime DIALS that endpoint; the socket emulation carries the
		// byte stream over a WebSocket the editor's serve port answers. A
		// dial nobody answers (a hand-opened page, an ended session) gives
		// up after its bounded budget and the game runs standalone.
		else if (const char* debugConnect =
			std::getenv("ORKIGE_DEBUG_CONNECT"))
		{
			if (debugLink.startConnect(debugConnect))
			{
				SDL_Log("orkige_player: dialing the editor debug endpoint "
					"%s", debugConnect);
			}
			else
			{
				SDL_Log("orkige_player: debug endpoint '%s' is malformed - "
					"running standalone", debugConnect);
			}
		}
#endif

		// collision layers must be configured BEFORE PhysicsWorld::init (the
		// Jolt filters are built from them at init time). A project's
		// physics.olayers (manifest Settings "physics.layers") overrides the
		// built-in default (a single "Default" layer colliding with everything).
		if (project.isLoaded())
		{
			Orkige::PhysicsWorld::LayerConfig layerConfig;
			layerConfig.loadForProject(project);
			physicsWorld.setLayerConfig(layerConfig);
		}

		// physics only when the scene needs it: RigidBodyComponents create
		// their bodies lazily on the first component update, which requires
		// an initialized PhysicsWorld. Not const: a deferred level load
		// re-evaluates it for the new scene.
		bool& physicsNeeded = context.physicsNeeded;
		physicsNeeded = sceneHasRigidBodies(gameObjectManager);
		if (physicsNeeded)
		{
			if (!physicsWorld.init())
			{
				SDL_Log("orkige_player: FAILED - PhysicsWorld::init failed");
				return 1;
			}
			SDL_Log("orkige_player: physics world up (scene contains "
				"rigid bodies)");
		}


		// default view: matches the editor's initial orbit camera pose so a
		// scene looks the same in the player as in a fresh editor viewport
		// (project scripts may re-place the rig from their init())
		cameraNode->setPosition(Orkige::Vec3(0.0f, 2.5f, 9.0f));
		cameraNode->lookAt(Orkige::Vec3::ZERO, Orkige::RenderNode::TS_WORLD);

		// synchronous game-support/gameplay verifications against the
		// live wiring (see PlayerSelfChecks.cpp)
		if (const std::optional<int> checkExit =
			context.selfChecks.gameplaySynchronousChecks(context))
		{
			return *checkExit;
		}
		// frame-time statistics: the ORKIGE_DEMO_FPS_LOG measurement hook and
		// the one-time "this build is too slow to play" hint
		context.frameStats.emplace();
		Orkige::FrameStatsUtil& frameStats = *context.frameStats;
		// mobile app lifecycle: the backgrounding contract as a pure state
		// machine (core_game/AppLifecycle.h). The poll loop below translates the
		// SDL_EVENT_* lifecycle events into it; applyLifecycle performs the
		// returned actions against the live subsystems, and the loop reads the
		// sim/render gates back (isSimPaused / isRenderingStopped). Desktop
		// windows minimizing is NOT a background - SDL only raises these events
		// on mobile - so desktop behavior is unchanged.
		context.lifecycle.emplace();
		Orkige::AppLifecycle& lifecycle = *context.lifecycle;

		// pre-loop selfcheck work: the measured deform/animation budgets
		// and the profiler arm (see PlayerSelfChecks.cpp)
		context.selfChecks.beforeLoop(context);

		context.lastFrameTime = std::chrono::steady_clock::now();

		// --run-tests: the project's OWN Lua test suite, run here - where the
		// runtime is fully up, so a test sees exactly the engine a game sees
		// (live resource mounts, so script.require reaches a library inside a
		// pak or an APK; the project's script search root; the hardened
		// sandbox). The exit code IS the verdict, the same contract every
		// player selfcheck ctest uses. Always compiled in: a released player -
		// the one inside a distributed editor and the one on a device payload -
		// must be able to run a project's tests with no repository, no build
		// tree and no interpreter beyond the engine's own.
		//
		// ONE PLAYER BOOT for the whole run: the suite runs INSTEAD of the
		// frame loop, driving frames itself for the play-mode tests through
		// the same playerIterate the loop below would call. Everything the
		// loop needs is up by this point.
		if (arguments.runTests)
		{
			PlayerTestHooks testHooks;
			testHooks.pumpFrame = [&context]() -> bool
			{
				return playerIterate(context);
			};
			testHooks.loadScene =
				[&context](Orkige::String const & requestedScene) -> bool
			{
				return context.loadSceneForTest(requestedScene);
			};
			return runProjectLuaTests(project, arguments.testFilter,
				std::filesystem::path(engineLogPath).parent_path().string(),
				testHooks);
		}

		// the frame loop, driven by the platform (engine_runtime/GameHost.h):
		// one playerIterate per frame, then the orderly shutdown. Some
		// platforms own the frame cadence and the runtime must RETURN to them
		// between frames, so the loop is expressed as callbacks and takes
		// ownership of the run state where it outlives main()'s frame; where
		// it does not, it loops here and returns with the context still ours.
		Orkige::GameFrameLoop loop;
		loop.context = Orkige::gameFrameLoopOwnsContext()
			? contextOwner.release() : contextOwner.get();
		loop.frame = [](void* rawContext)
		{
			return playerIterate(*static_cast<PlayerContext*>(rawContext));
		};
		loop.finish = [](void* rawContext)
		{
			static_cast<PlayerContext*>(rawContext)->shutdownWorld();
		};
		loop.exitCode = [](void* rawContext)
		{
			return static_cast<PlayerContext*>(rawContext)->exitCode;
		};
		loop.dispose = [](void* rawContext)
		{
			delete static_cast<PlayerContext*>(rawContext);
		};
		loop.automatedRun = context.automatedRun;
		Orkige::runGameFrameLoop(loop);
	}

	// AppHost's destructor mirrors the boot: world, engine, singletons,
	// then the SDL window; the breadcrumb trail outlives it all
	return exitCode;
}