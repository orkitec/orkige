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
// gui is flavor-neutral - the UI
// assertions below run on BOTH render flavors
#include <engine_gui/GuiManager.h>
#include <engine_runtime/AppHost.h>
#include <engine_filesystem/MiniZip.h>
#include "PlayerContext.h"
#include "PlayerSelfChecks.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <ctime>
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

#ifdef __ANDROID__
#include <jni.h>	// the APK path is a JNI call on the SDL activity (stored mode)
#endif

// the engine's shared-ownership alias, used throughout this TU
using Orkige::optr;
using Orkige::woptr;

namespace
{

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

#ifdef __ANDROID__
//! @brief the APK sub-trees whose bulk binary media the player MOUNTS in place
//! in `stored` mode (export.android.assets=stored) instead of extracting - the
//! game textures/audio/meshes it references by resource name, the bulk of the
//! bytes. The small fopen-consumed tree (manifest, scenes, scripts, config) and
//! the engine shader/font media (a directory tree the Hlms/RTSS loaders want)
//! stay extracted. Path is relative to the assets root (= extract destRoot).
bool isMountedMediaPath(std::string const& rel)
{
	static const char* const prefixes[] =
		{ "project/assets/", "assets/", "jumper_media/" };
	for (const char* prefix : prefixes)
	{
		if (rel.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

//! @brief the APK file's own path, via a JNI call on the SDL activity
//! (Context.getPackageCodePath) - the file the player mounts in `stored` mode.
//! "" when JNI/the activity is unavailable (the mount then falls back to
//! extraction, the always-safe path).
std::string androidApkPath()
{
	JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!env || !activity)
	{
		return std::string();
	}
	std::string result;
	jclass cls = env->GetObjectClass(activity);
	if (cls)
	{
		jmethodID method = env->GetMethodID(cls, "getPackageCodePath",
			"()Ljava/lang/String;");
		if (method)
		{
			jstring jpath = static_cast<jstring>(
				env->CallObjectMethod(activity, method));
			if (jpath)
			{
				const char* chars = env->GetStringUTFChars(jpath, nullptr);
				if (chars)
				{
					result = chars;
					env->ReleaseStringUTFChars(jpath, chars);
				}
				env->DeleteLocalRef(jpath);
			}
		}
		env->DeleteLocalRef(cls);
	}
	env->DeleteLocalRef(activity);
	return result;
}

//! @brief extract the APK's bundled media into destRoot. APK assets are not
//! files - OGRE's FileSystem archives, the scene loader (tinyxml2/fopen) and
//! the sound loader all want real paths, so everything is materialized once
//! under the app files dir. The package script (tools/player/android/
//! package_apk.sh) writes assets/orkige_assets.txt listing every bundled
//! file; SDL_LoadFile with a relative path reads from the APK assets. A file
//! that already exists with the same size is skipped (cheap re-launch).
//! @param mountMediaMode `stored` mode: skip the bulk binary media
//! (isMountedMediaPath) - the player mounts those in place - and extract only
//! the small fopen tree + shader/font media.
bool extractBundledAssets(std::string const& destRoot, bool mountMediaMode)
{
	size_t manifestSize = 0;
	char* manifestData = static_cast<char*>(
		SDL_LoadFile("orkige_assets.txt", &manifestSize));
	if (!manifestData)
	{
		SDL_Log("orkige_player: FAILED - no orkige_assets.txt in the APK "
			"assets: %s", SDL_GetError());
		return false;
	}
	std::istringstream manifest(std::string(manifestData, manifestSize));
	SDL_free(manifestData);
	std::string relativePath;
	unsigned extracted = 0;
	unsigned kept = 0;
	while (std::getline(manifest, relativePath))
	{
		if (!relativePath.empty() && relativePath.back() == '\r')
		{
			relativePath.pop_back();
		}
		if (relativePath.empty())
		{
			continue;
		}
		if (mountMediaMode && isMountedMediaPath(relativePath))
		{
			continue;	// mounted in place from the APK, not extracted
		}
		size_t dataSize = 0;
		void* data = SDL_LoadFile(relativePath.c_str(), &dataSize);
		if (!data)
		{
			SDL_Log("orkige_player: FAILED - manifest lists '%s' but the "
				"asset cannot be read: %s", relativePath.c_str(),
				SDL_GetError());
			return false;
		}
		const std::filesystem::path destPath =
			std::filesystem::path(destRoot) / relativePath;
		std::error_code ignored;
		if (std::filesystem::exists(destPath, ignored) &&
			std::filesystem::file_size(destPath, ignored) == dataSize)
		{
			SDL_free(data);
			++kept;
			continue;
		}
		std::filesystem::create_directories(destPath.parent_path(), ignored);
		const bool saved =
			SDL_SaveFile(destPath.string().c_str(), data, dataSize);
		SDL_free(data);
		if (!saved)
		{
			SDL_Log("orkige_player: FAILED - could not write '%s': %s",
				destPath.string().c_str(), SDL_GetError());
			return false;
		}
		++extracted;
	}
	SDL_Log("orkige_player: bundled assets ready under '%s' (%u extracted, "
		"%u up to date)", destRoot.c_str(), extracted, kept);
	return true;
}
#endif // __ANDROID__

} // namespace

// the re-entrant scene load: the SAME steps the initial load
// above runs, factored so the deferred-load pump at the frame boundary
// (in the player loop's tick order) can reuse them. SceneSerializer::
// loadScene tears the old world down via GameObjectManager::clear (the
// teardown hook - scripts get their shutdown, tweens are reaped,
// rigid bodies leave the sim); we then re-apply the unlit fix, bring
// physics up lazily if the new level introduces bodies (PhysicsWorld
// persists - inited once, never torn down), drop the debug-link
// selection so a stale id cannot dangle and let the hierarchy
// re-stream. A failed load is logged but keeps the run alive.
//---------------------------------------------------------
bool PlayerContext::reloadSceneFrom(std::string const & newScenePath)
{
	std::string& scenePath = this->scenePath;
	Orkige::BenchmarkRecorder& benchmarkRecorder = this->benchmarkRecorder;
	Orkige::GameObjectManager& gameObjectManager = *this->gameObjectManagerPtr;
	Orkige::PhysicsWorld& physicsWorld = *this->physicsWorld;
	Orkige::PlayerDebugLink& debugLink = *this->debugLink;
	bool& physicsNeeded = this->physicsNeeded;

	if (!Orkige::SceneSerializer::loadScene(newScenePath,
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
	debugLink.onSceneReloaded();
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
	Orkige::LevelManager& levelManager = *context.levelManager;
	Orkige::ScreenFade& screenFade = *context.screenFade;
	Orkige::ScreenShake& screenShake = *context.screenShake;
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
		// ============== PLAYER LOOP TICK ORDER (canonical) ==============
		// Ruled ONCE for every runtime feature (execution plan, 2026-07):
		//   input -> scripts/world -> tweens -> physics -> load pump.
		// Later packages FILL their labeled slot below instead of
		// appending elsewhere - a wrong position means silent
		// one-frame-lag bugs.
		//
		// TIME SCALE: the gameplay tick (scripts, tweens, physics) runs on
		// the SCALED delta (world.setTimeScale; 0 = hitstop, still renders);
		// input sampling, presentation overlays (fade/shake), audio, the
		// debug stream and rendering stay on the real delta.
		const float gameplayDelta = deltaTime * timeControl.getTimeScale();
		//
		// [1] INPUT - the raw SDL events of this frame were polled and
		//     injected at the top of the loop (inputManager.injectEvent).
		//     SLOT(input-actions): map raw input state to actions
		//     HERE, before the scripts that read them run. ONE edge
		//     snapshot per frame (pressed = down && !down-last-frame);
		//     scripts read the snapshot back, never recompute it.
		{
			OPROFILE("input");
			inputActions.update(deltaTime);
		}
		//
		// [2] SCRIPTS/WORLD - the component updates: ScriptComponent
		//     runs the game code, rigid bodies create lazily and sync
		//     their simulated pose into the transforms, sounds/sprites
		//     follow their transforms.
		{
			OPROFILE("scripts");
			gameObjectManager.update(gameplayDelta);
		}
		//
		// [2b] EVENT BUS - drain the ONE engine event bus
		//     (core_event/GlobalEventManager) in the SCRIPT PHASE: the
		//     script `events` table, the gui and the engine mirrors all
		//     queue onto it, and this tick fans each queued event out to
		//     its C++ and Lua listeners in subscription order. THIS is the
		//     missing wire that activates the heritage async queue. The
		//     manager's double-buffered queue makes an emit from inside a
		//     handler deliver NEXT frame (cascade-safe, no recursion). gui
		//     events queued during input dispatch (before [2]) and events
		//     emitted by the scripts just above are delivered HERE, this
		//     frame; events emitted LATER in the tick (the physics contact
		//     mirror in [4]) land next frame.
		if (Orkige::GlobalEventManager::getSingletonPtr())
		{
			OPROFILE("events");
			Orkige::GlobalEventManager::getSingleton().tick();
		}
		//
		// [3] TWEENS - after scripts (a tween started this frame takes
		//     its first step this frame), before physics (tweened poses
		//     are what the simulation sees). Dormant in the editor: only
		//     runtimes that tick this block create a TweenManager.
		{
			OPROFILE("tweens");
			tweenManager.update(gameplayDelta);
			// timers ride the SAME phase (a timer is a degenerate tween);
			// scheduled Lua callbacks fire here, after scripts, on the
			// scaled gameplay delta - NOT a new tick-order fence entry
			timerManager.update(gameplayDelta);
		}
		//
		// [4] PHYSICS - the fixed-timestep simulation, then the
		//     sim->scene pose sync: dynamic bodies publish the pose
		//     THIS frame's step produced (component updates ran before
		//     physics, so without this pass rendering and the debug
		//     stream would lag the simulation by one tick).
		if (physicsNeeded)
		{
			OPROFILE("physics");
			physicsWorld.update(gameplayDelta);
			Orkige::RigidBodyComponent::syncDynamicBodyPoses(
				gameObjectManager);
			// contacts + sensors/triggers: the worker-thread
			// contact queue was drained inside update() above; dispatch
			// the coalesced frame contacts to game code on THIS main
			// thread (C++ ContactBegan/EndedEvent + the Lua
			// onContactBegin/onContactEnd hooks). A script mutating the
			// world here defers through the GameObjectManager delete
			// queue, so it stays safe mid-dispatch.
			Orkige::RigidBodyComponent::dispatchContacts(
				gameObjectManager);
		}
		//
		// [5] SLOT(deferred-load pump): a script asked for a scene
		//     switch (world.loadScene / LevelManager:loadLevel set the
		//     pending request during [2]). Apply it HERE, at the frame
		//     boundary AFTER physics - never mid-update, where in-flight
		//     script/update pointers would dangle. reloadSceneFrom tears
		//     the old world down through the GameObjectManager::clear
		//     teardown hook and reloads; the new scene's scripts init on
		//     the NEXT frame. Keep this slot LAST.
		{
			OPROFILE("load");
			int pendingLevelIndex = -1;
			Orkige::String pendingScene;
			if (levelManager.consumePendingLoad(pendingLevelIndex,
				pendingScene))
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
				if (reloadSceneFrom(resolvedScene))
				{
					if (pendingLevelIndex >= 0)
					{
						levelManager.setCurrentIndex(pendingLevelIndex);
					}
				}
			}
		}
		// ================ end PLAYER LOOP TICK ORDER ====================

		// audio listener follows the (script-driven) camera rig
		{
			OPROFILE("audio");
			soundManager.update(deltaTime);
		}

		// the fade overlay is a PRESENTATION layer: ticked last, after
		// the deferred-load pump, so its alpha reflects the frame about to
		// render and a mid-fade scene switch is hidden under full opacity
		{
			OPROFILE("present");
			screenFade.update(deltaTime);
		}
		// screen shake is a PRESENTATION effect too, ticked after the fade
		// and the deferred-load pump: it reads the camera's base pose AFTER
		// the scripts/physics of this frame set it, applies the wobble for
		// the frame about to render, and restores it. On the real delta so
		// it still animates during a hitstop (timeScale 0).
		{
			OPROFILE("present");
			screenShake.update(deltaTime);
		}
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
	// the player's whole world lives on ONE heap context
	// (PlayerContext.h): main() fills it in boot order, playerIterate
	// reads it back per frame. Owned here for the desktop teardown; the
	// browser path hands it to the page's frame callback instead (which
	// deletes it, exactly once, when the run ends).
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
			"[scene.oscene] [--debug-port N]");
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

	// exported app, launched WITHOUT arguments (double-click): the
	// orkige_project.txt marker next to the executable's resources names the
	// bundled default project - macOS .app: Contents/Resources/, iOS .app:
	// the flat bundle root (Android reads its extracted assets root below,
	// after SDL is up). Dev runs carry no marker and are unaffected. See
	// PlayerBundle in engine_runtime/PlayerRuntime.h and Util/orkige_export.py.
	bool bundledProjectRun = false;
	if (projectPath.empty() && scenePath.empty())
	{
		projectPath = Orkige::PlayerBundle::findBundledProject();
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

#ifdef ORKIGE_IPHONE
	// iOS app bundle: everything ships inside the bundle - default to the
	// bundled example scene when launched without arguments (simctl launch
	// can still pass a different bundled scene path)
	if (scenePath.empty())
	{
		scenePath = Orkige::PlatformUtil::getResourceDirectory() +
			"example.oscene";
	}
#elif defined(__ANDROID__)
	// Android APK: the bundled media is extracted below (after SDL_Init) -
	// an empty scenePath defaults to the extracted example scene there (the
	// editor's play mode passes a scene path through the OrkigeActivity
	// intent extras instead)
#else
	if (scenePath.empty() && !pakSelfCheck)
	{
		SDL_Log("usage: orkige_player [--project <dir-or-.orkproj>] "
			"[scene.oscene] [--debug-port N]");
		return 1;
	}
#endif

	// Android back button: TRAP it so SDL delivers it as an
	// SDL_SCANCODE_AC_BACK key event (-> KC_WEBBACK, readable by scripts / an
	// input action) instead of letting the system finish the activity. The
	// engine default is DELIVER, don't exit - a game handles Back as "pause /
	// go up a menu"; a game that wants the old exit behavior can undo it. A
	// harmless no-op off Android.
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#ifdef __ANDROID__
	// the APK media must be extracted (and the bundled project resolved)
	// before the host boots, and extraction reads through SDL's asset IO -
	// initialise SDL video early; AppHost's own SDL_Init stacks on top
	// (per-subsystem refcount) and its teardown closes SDL for both.
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	// the app files dir is the writable root - the historical PlatformUtil
	// Android path model (setFilesPath feeds getDocumentsDirectory,
	// getResourceDirectory & co)
	Orkige::PlatformUtil::setFilesPath(
		std::string(SDL_GetAndroidInternalStoragePath()) + "/");
	// materialize the APK's bundled media (same set the iOS bundle carries)
	const std::string bundleRoot =
		Orkige::PlatformUtil::getResourceDirectory() + "bundle/";
	// stored mode (export.android.assets=stored, the default): the packager
	// left the APK assets UNCOMPRESSED and dropped an orkige_mount.txt marker,
	// so the player MOUNTS the bulk game media in place (no extraction of the
	// big textures/audio/meshes) and extracts only the small fopen tree +
	// shader/font media. A resolvable APK path is required; if it or the marker
	// is absent the run falls back to full extraction (the always-safe path).
	bool androidMountAssets = false;
	std::string androidApkForMount;
	{
		size_t markerSize = 0;
		void* marker = SDL_LoadFile("orkige_mount.txt", &markerSize);
		const bool storedMode = (marker != nullptr);
		if (marker)
		{
			SDL_free(marker);
		}
		if (storedMode)
		{
			androidApkForMount = androidApkPath();
			androidMountAssets = !androidApkForMount.empty();
			if (!androidMountAssets)
			{
				SDL_Log("orkige_player: stored APK but no resolvable APK path "
					"- falling back to full extraction");
			}
		}
	}
	if (!extractBundledAssets(bundleRoot, androidMountAssets))
	{
		return 1;
	}
	if (androidMountAssets)
	{
		SDL_Log("orkige_player: stored mode - mounting APK media in place "
			"from '%s'", androidApkForMount.c_str());
	}
	// exported APK: the marker rides in the extracted assets - the same
	// no-args default-project mechanism as the desktop/iOS bundles (SDL has
	// no base path on Android, so the extracted root is passed explicitly)
	if (!project.isLoaded() && scenePath.empty() && projectPath.empty())
	{
		const std::string bundledProject =
			Orkige::PlayerBundle::findBundledProject(bundleRoot);
		if (!bundledProject.empty())
		{
			bundledProjectRun = true;
			std::string projectError;
			if (!project.load(bundledProject, &projectError))
			{
				SDL_Log("orkige_player: FAILED - %s", projectError.c_str());
				return 1;
			}
			scenePath = project.getMainScenePath();
			SDL_Log("orkige_player: exported app - bundled project '%s'",
				project.getName().c_str());
		}
	}
	if (scenePath.empty())
	{
		scenePath = bundleRoot + "example.oscene";
	}
	else if (scenePath[0] != '/')
	{
		// the editor's play mode drops the temp scene into the app files dir
		// (adb push + run-as) and passes the path relative to it
		scenePath = Orkige::PlatformUtil::getDocumentsDirectory() + scenePath;
	}
#endif
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
	// mobile orientation: constrain the window / UIKit view-controller
	// orientations to the project's export.orientation BEFORE the window is
	// created. iOS otherwise picks the boot orientation from the allowed set by
	// the initial window aspect - and the mobile window is created desktop-wide
	// (w>h), so an unconstrained app boots LANDSCAPE. Pinning the hint makes the
	// render surface match the orientation the OS presents (the iOS 90°-rotation
	// guard) and keeps the safe-area insets deterministic. PORTRAIT is the default
	// (and where any unrecognised value lands); explicit "auto" leaves the hint
	// unset AND asks for a rotation-following window - both halves are needed:
	// with no hint the window system derives the allowed set from the window,
	// and only a RESIZABLE window may follow the device orientation (a fixed
	// one gets pinned to its boot aspect). A no-op on desktop.
	bool followDeviceRotation = false;
	{
		// an explicit --orientation (the editor's Android play sessions, where
		// the manifest does not travel to the device) wins over the manifest
		const Orkige::String orientation = !arguments.orientation.empty()
			? arguments.orientation
			: project.getSetting("export.orientation", "portrait");
		if (orientation == "landscape")
		{
			SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
		}
		else if (orientation == "auto")
		{
			followDeviceRotation = true;
		}
		else
		{
			SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
		}
	}
	// the shared boot spine (engine_runtime/AppHost.h): SDL window (mobile
	// fullscreen / desktop high-pixel-density), engine singletons, the
	// per-flavor Engine boot, the window-camera rig and the GameObject world
	Orkige::AppHost& host = context.host;
	{

#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
		// sandboxed app: the working directory is not writable - the engine
		// log goes into the app container (iOS: Documents, fetchable via
		// 'simctl get_app_container ... data'; Android: the files dir set
		// above, fetchable via 'adb shell run-as ... cat files/...')
		const std::string engineLogPath =
			Orkige::PlatformUtil::getDocumentsDirectory() + "orkige_player.log";
#else
		// exported .app: never write into the cwd (a double-clicked app runs
		// with cwd "/") - the log goes to ~/Library/Application Support/
		// "Orkige Player"/; dev runs keep the historical cwd log
		const std::string engineLogPath = bundledProjectRun
			? Orkige::PlatformUtil::getSupportDirectory("Orkige Player") +
				"orkige_player.log"
			: "orkige_player.log";
#endif
		// the breadcrumb file: the writable app dir (getSupportDirectory on
		// desktop, getDocumentsDirectory on mobile); ORKIGE_BREADCRUMB_DIR
		// overrides it (test isolation). rotate() at boot moves the last
		// run's file to breadcrumbs.prev.jsonl.
		{
			std::string breadcrumbDir;
			if (const char* dirEnv = std::getenv("ORKIGE_BREADCRUMB_DIR"))
			{
				breadcrumbDir = dirEnv;
			}
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
			else
			{
				breadcrumbDir = Orkige::PlatformUtil::getDocumentsDirectory();
			}
#else
			else
			{
				breadcrumbDir =
					Orkige::PlatformUtil::getSupportDirectory("Orkige Player");
			}
#endif
			if (!breadcrumbDir.empty() && breadcrumbDir.back() != '/')
			{
				breadcrumbDir += '/';
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
#ifdef ORKIGE_RENDER_NEXT
				meta.flavor = "next";
#else
				meta.flavor = "classic";
#endif
#if defined(ORKIGE_IPHONE)
				meta.platform = "ios";
#elif defined(__ANDROID__)
				meta.platform = "android";
#elif defined(__APPLE__)
				meta.platform = "macos";
#elif defined(_WIN32)
				meta.platform = "windows";
#else
				meta.platform = "linux";
#endif
				// render system: next boots Metal on Apple / Vulkan elsewhere;
				// classic honours ORKIGE_RENDERSYSTEM (GL3Plus default)
#ifdef ORKIGE_RENDER_NEXT
#if defined(__APPLE__)
				meta.renderSystem = "Metal";
#else
				meta.renderSystem = "Vulkan";
#endif
#else
				if (const char* rs = std::getenv("ORKIGE_RENDERSYSTEM"))
				{
					meta.renderSystem = rs;
				}
				else
				{
					meta.renderSystem = "GL3Plus";
				}
#endif
#ifdef NDEBUG
				meta.build = "Release";
#else
				meta.build = "Debug";
#endif
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
		const bool automatedRun = context.automatedRun;

#ifdef ORKIGE_IPHONE
		// iOS: everything was copied into the app bundle by the CMake
		// post-build step (see tools/player/CMakeLists.txt)
		const std::string bundleDir = Orkige::PlatformUtil::getResourceDirectory();
		// note: "assets", not "media" - macOS/iOS filesystems are case-
		// insensitive and a "media" dir would collide with "Media" above
		const std::string playerMediaDir = bundleDir + "Media";
		const std::string playerAssetDir = bundleDir + "assets";
		const std::string playerJumperAssetDir = bundleDir + "jumper_media";
#elif defined(__ANDROID__)
		// Android: same layout as the iOS bundle, extracted from the APK
		// assets into <files>/bundle/ above
		const std::string playerMediaDir = bundleRoot + "Media";
		const std::string playerAssetDir = bundleRoot + "assets";
		const std::string playerJumperAssetDir = bundleRoot + "jumper_media";
#else
		// desktop: build-tree defaults; an exported .app overrides the engine
		// media with the Media/ it carries in Contents/Resources (next to the
		// project marker) so the bundle is self-contained - no vcpkg or
		// source-tree path is touched at runtime
		const std::string playerMediaDir =
			Orkige::PlayerBundle::resolveMediaDirectory(ORKIGE_PLAYER_MEDIA_DIR);
		const std::string playerAssetDir = ORKIGE_PLAYER_ASSET_DIR;
		const std::string playerJumperAssetDir = ORKIGE_PLAYER_JUMPER_ASSET_DIR;
#endif
		// the host boot: mobile is a fullscreen native window, desktop bakes
		// the scene path into the title. The media dir feeds the classic RTSS
		// registration AND the next flavor's Hlms override: a device bundle
		// always overrides (the engine's baked default is a build-tree path,
		// invalid there), a desktop dev run keeps the baked Hlms default and
		// an exported .app resolves to the Media/ in its Resources instead.
		Orkige::AppHostConfig hostConfig;
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
		hostConfig.windowTitle = "Orkige Player";
		hostConfig.hlmsMediaDir = playerMediaDir;
		// export.orientation "auto": the fullscreen surface follows device
		// rotation (see the orientation-hint block above)
		hostConfig.resizableWindow = followDeviceRotation;
#else
		hostConfig.windowTitle = "Orkige Player - " + scenePath;
		if (playerMediaDir != std::string(ORKIGE_PLAYER_MEDIA_DIR))
		{
			hostConfig.hlmsMediaDir = playerMediaDir;
		}
		(void)followDeviceRotation;	// rotation policy is a mobile concern
#endif
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
#ifdef ORKIGE_PLAYER_BLOOM_DIR
				// the engine bloom compositor media (the bright/blur/combine
				// material + shaders engine:setBloom needs). Next flavor ONLY -
				// the define is set for the next backend, classic bloom is gated
				// off (@see RenderBackend::bloomSupported). Same bundled/dev-tree
				// pair as the media above.
				std::error_code bloomDirError;
				if (std::filesystem::is_directory(playerMediaDir + "/bloom/next",
					bloomDirError))
				{
					render->addResourceLocation(playerMediaDir + "/bloom/next");
				}
				if (std::filesystem::is_directory(ORKIGE_PLAYER_BLOOM_DIR,
					bloomDirError))
				{
					render->addResourceLocation(ORKIGE_PLAYER_BLOOM_DIR);
				}
#endif
				// sample assets (test_mesh.glb; scene meshes load lazily via
				// Codec_Assimp) and the jumper sample assets, so the editor's
				// play mode works on samples/* scenes too. Registered only when
				// present: an exported app ships nothing but its project's
				// assets, and the (baked-in) dev source-tree paths must not
				// abort the run elsewhere
				for (std::string const& sampleAssetDir :
					{ playerAssetDir, playerJumperAssetDir })
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
					SDL_Log("orkige_player: project '%s' (root '%s') rooted "
						"the resource locations", project.getName().c_str(),
						project.getRootDirectory().c_str());
				}
#ifdef __ANDROID__
				// stored mode: mount the APK's bulk game media in place. Each
				// media DIRECTORY becomes its own flat pak mount so files
				// resolve by BARE resource name, exactly like the loose-file
				// registration above (a single sub-tree mount would only
				// resolve by full sub-path). MiniZip enumerates the APK's own
				// directory table.
				if (androidMountAssets)
				{
					Orkige::MiniZip apk;
					if (apk.open(androidApkForMount))
					{
						std::set<std::string> mediaDirs;
						for (auto const& entry : apk.entries())
						{
							const std::string& full = entry.first;
							if (full.rfind("assets/", 0) != 0)
							{
								continue;
							}
							if (!isMountedMediaPath(full.substr(7)))
							{
								continue;
							}
							const std::size_t slash = full.find_last_of('/');
							if (slash != std::string::npos)
							{
								mediaDirs.insert(full.substr(0, slash + 1));
							}
						}
						for (std::string const& dir : mediaDirs)
						{
							render->mountPak(androidApkForMount, dir,
								Orkige::Project::RESOURCE_GROUP_NAME);
						}
						SDL_Log("orkige_player: mounted %zu APK media dirs in "
							"place", mediaDirs.size());
					}
					else
					{
						SDL_Log("orkige_player: WARNING - could not open APK "
							"'%s' to mount media in place",
							androidApkForMount.c_str());
					}
				}
#endif
				// ORKIGE_PAK_SELFCHECK: mount the pak's sub-tree so its scene,
				// textures and sounds resolve through the resource system like
				// loose files (the reborn BigZip acceptance path, both flavors)
				if (context.selfChecks.pakCheck)
				{
					render->mountPak(context.selfChecks.pakPath,
						context.selfChecks.pakMountPoint,
						Orkige::Project::RESOURCE_GROUP_NAME);
					SDL_Log("orkige_player: mounted pak '%s' (sub-tree '%s')",
						context.selfChecks.pakPath.c_str(),
						context.selfChecks.pakMountPoint.c_str());
				}
			}))
		{
			return 1;
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
			if (!debugLink.start(arguments.debugPort))
			{
				SDL_Log("orkige_player: FAILED - could not listen on "
					"127.0.0.1:%u",
					static_cast<unsigned>(arguments.debugPort));
				return 1;
			}
			SDL_Log("orkige_player: debug server listening on 127.0.0.1:%u",
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

		// the frame loop: one playerIterate per frame (the canonical tick
		// order lives in its fenced block above). The browser cannot loop
		// on main()'s stack - the page owns the frame cadence - so the
		// context moves to the page's frame callback and main() never
		// returns normally there; every other platform keeps the plain
		// loop and the straight-line teardown.
#ifdef __EMSCRIPTEN__
		// pacing follows the automated-run window policy: a HUMAN run uses
		// requestAnimationFrame (fps 0 - the page's vsync), an automated
		// (frame-capped/scripted) run uses timer pacing so a headless
		// session's virtual clock can fast-forward the frames. The final
		// `true` abandons main()'s frame right here, so the context
		// ownership moves to the callback BEFORE the call (nothing below
		// this line runs on the web)
		const int webFramesPerSecond = context.automatedRun ? 60 : 0;
		emscripten_set_main_loop_arg(
			[](void* rawContext)
			{
				PlayerContext* context = static_cast<PlayerContext*>(rawContext);
				if (playerIterate(*context))
				{
					return;
				}
				// the run ended: the same orderly shutdown the desktop path
				// runs, then the ONE owning pointer is deleted here, exactly
				// once - the loop is cancelled and the runtime exits with the
				// game's code; nothing touches the context afterwards
				context->shutdownWorld();
				const int finalExitCode = context->exitCode;
				delete context;
				emscripten_cancel_main_loop();
				emscripten_force_exit(finalExitCode);
			},
			contextOwner.release(), webFramesPerSecond, true);
#else
		while (context.running)
		{
			playerIterate(context);
		}
		context.shutdownWorld();
#endif
	}

	// AppHost's destructor mirrors the boot: world, engine, singletons,
	// then the SDL window; the breadcrumb trail outlives it all
	return exitCode;
}