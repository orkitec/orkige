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
// core_debugnet DebugServer on 127.0.0.1:N. Commands are processed once per
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
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_gocomponent/ScriptComponentRegistry.h>
#include <engine_gocomponent/ParticleComponent.h>
#include <engine_gocomponent/VectorShapeComponent.h>
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
// gui is flavor-neutral - the UI
// assertions below run on BOTH render flavors
#include <engine_gui/GuiManager.h>
#include <engine_runtime/AppHost.h>
#include <engine_runtime/PlayerRuntime.h>
#include <engine_util/FrameStatsUtil.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_game/LevelManager.h>
#include <core_game/LevelSequence.h>
#include <core_game/SaveStore.h>
#include <core_game/AppLifecycle.h>
#include <core_game/TimeControl.h>
#include <core_project/Project.h>
#include <core_debug/CVarManager.h>
#include <core_debug/Breadcrumbs.h>
#include <core_debug/MemoryManager.h>
#include <core_debug/Profile.h>
#include <core_debugnet/DebugServer.h>
#include <engine_base/EngineLog.h>
#include <core_util/PlatformUtil.h>
#include <core_util/StringUtil.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>
#include <core_tween/TweenManager.h>
#include <core_script/ScriptEventBus.h>
#include <core_tween/EaseLibrary.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

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
//! @brief extract the APK's bundled media into destRoot. APK assets are not
//! files - OGRE's FileSystem archives, the scene loader (tinyxml2/fopen) and
//! the sound loader all want real paths, so everything is materialized once
//! under the app files dir. The package script (tools/player/android/
//! package_apk.sh) writes assets/orkige_assets.txt listing every bundled
//! file; SDL_LoadFile with a relative path reads from the APK assets. A file
//! that already exists with the same size is skipped (cheap re-launch).
bool extractBundledAssets(std::string const& destRoot)
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

int main(int argc, char** argv)
{
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
	std::string scenePath = arguments.scenePath;
	std::string projectPath = arguments.projectPath;

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
	Orkige::Project project;
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
	if (scenePath.empty())
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
	if (!extractBundledAssets(bundleRoot))
	{
		return 1;
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
	int exitCode = 0;
	// crash breadcrumbs, declared before the host so the trail stays alive
	// through the whole engine teardown: an always-on, flush-per-entry trail
	// of engine events (scene loads, script errors, warnings, boot/shutdown)
	// that survives a hard crash (SIGSEGV/OOM/watchdog kill). Written to the
	// writable app dir (see the setFile block below) so the editor can read
	// the PREVIOUS session's trail after an abnormal exit.
	Orkige::Breadcrumbs breadcrumbs;
	// the shared boot spine (engine_runtime/AppHost.h): SDL window (mobile
	// fullscreen / desktop high-pixel-density), engine singletons, the
	// per-flavor Engine boot, the window-camera rig and the GameObject world
	Orkige::AppHost host;
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
				breadcrumbs.record("boot", scenePath);
			}
		}
		// automation hooks (read before engine setup - they gate the vsync
		// and frame-pacing decisions): ORKIGE_DEMO_FRAMES frame-limits the
		// run, ORKIGE_JUMPER_LUA_SELFCHECK runs the scripted verification
		unsigned long frameLimit = 0;
		if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
		{
			frameLimit = std::strtoul(demoFrames, nullptr, 10);
		}
		const bool jumperLuaCheck =
			(std::getenv("ORKIGE_JUMPER_LUA_SELFCHECK") != nullptr);
		// ORKIGE_ROLLER_SELFCHECK verifies the 2D tier end to end against
		// projects/roller; ORKIGE_ROLLER_SCREENSHOT_DIR (optional) additionally
		// dumps roller_play.png / roller_move_mode.png there
		const bool rollerCheck =
			(std::getenv("ORKIGE_ROLLER_SELFCHECK") != nullptr);
		const char* rollerShotDirEnv =
			std::getenv("ORKIGE_ROLLER_SCREENSHOT_DIR");
		const std::string rollerShotDir =
			rollerShotDirEnv ? rollerShotDirEnv : "";
		// ORKIGE_SOFTBODY_SELFCHECK verifies soft, deformable organic shapes end
		// to end against projects/vectorshapes (scenes/softbody.oscene): a blob
		// with a RigidBody falls, SQUASHES on landing and wobbles (the dynamic
		// deform upload), then returns to rest; a second blob is MORPHED via a Lua
		// self.shape:playMorph call. It also logs the measured per-frame deform
		// cost (the mobile-viability budget number).
		const bool softbodyCheck =
			(std::getenv("ORKIGE_SOFTBODY_SELFCHECK") != nullptr);
		// ORKIGE_ROLLER_PROGRESSION_SELFCHECK verifies the level sequence +
		// deferred scene switch + progression save end to end against
		// projects/roller: solve level 1 (the proven tile-slide + roll), assert
		// the runtime SWITCHED to level 2 (shared.roller.levelIndex incremented,
		// the progression file written), then solve the straight-shot level 2
		const bool rollerProgressionCheck =
			(std::getenv("ORKIGE_ROLLER_PROGRESSION_SELFCHECK") != nullptr);
		// ORKIGE_ASSETID_SELFCHECK=<expected texture> verifies asset-id
		// rename survival end to end (run with --project
		// tests/projects/asset_rename): the scene's "Sprite" object carries a
		// STALE texture name plus the sidecar asset id of the real file - the
		// sprite must come up under the CURRENT name, resolved via the
		// project's AssetDatabase (core_project/AssetDatabase.h)
		const char* assetIdCheckEnv =
			std::getenv("ORKIGE_ASSETID_SELFCHECK");
		const std::string assetIdCheckTexture =
			assetIdCheckEnv ? assetIdCheckEnv : "";
		// ORKIGE_TWEEN_SELFCHECK verifies the tween system end to end against
		// tests/projects/tween (run with --project tests/projects/tween)
		const bool tweenCheck =
			(std::getenv("ORKIGE_TWEEN_SELFCHECK") != nullptr);
		// ORKIGE_HOTRELOAD_SELFCHECK verifies Lua hot-reload end to
		// end against tests/projects/hotreload: overwrite the running script on
		// disk, drive ScriptComponent::hotReload() (the player-directed swap),
		// and assert (a) the behavior changed AND (b) an engine-side value
		// persisted; then a broken-file variant must keep the OLD instance
		// ticking with a non-fatal error. The committed script is restored at
		// the end (the selfcheck rewrites it in place).
		const bool hotreloadCheck =
			(std::getenv("ORKIGE_HOTRELOAD_SELFCHECK") != nullptr);
		// ORKIGE_SCRIPTPROP_SELFCHECK verifies Lua script EXPORTED properties
		// end to end against tests/projects/scriptprop: the
		// scene bakes the "Mover" object's exported moveSpeed at a non-default
		// value (5); the selfcheck asserts the running script SAW the injected
		// value (init published it) and BEHAVES with it (moves at that speed),
		// then flips it live over the debug-protocol setter (moveSpeed -> 0
		// stops the motion) and re-saves+reloads the scene to prove the value
		// round-trips per-instance.
		const bool scriptPropCheck =
			(std::getenv("ORKIGE_SCRIPTPROP_SELFCHECK") != nullptr);
		// ORKIGE_INTEGRATION_CONTACT_SELFCHECK verifies a CROSS-FEATURE chain
		// against tests/projects/integration (scenes/contact.oscene): a scripted
		// ball discovers the goal by TAG (world.findByTag), an injected named
		// INPUT ACTION ("jump") turns gravity on, the PHYSICS drop overlaps the
		// goal SENSOR and the CONTACT EVENT (onContactBegin) fires - tags +
		// input actions + physics + contact events cooperating in one run.
		const bool integrationContactCheck =
			(std::getenv("ORKIGE_INTEGRATION_CONTACT_SELFCHECK") != nullptr);
		// ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK verifies a DEFERRED level
		// switch (scenes/levelA -> levelB) fired WHILE a TWEEN and a live
		// PARTICLE emitter run: the switch must tear the running tween + emitter
		// down cleanly (GameObjectManager::clear), the new level must tick and
		// the shared table must survive - level system + tweens + particles +
		// script lifecycle + teardown in one run.
		const bool integrationLevelCheck =
			(std::getenv("ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK") != nullptr);
		// ORKIGE_BREADCRUMB_SELFCHECK verifies the crash breadcrumb trail against
		// tests/projects/breadcrumb: a ScriptComponent raises a Lua error at init;
		// the player must record it as a "script_error" line in breadcrumbs.jsonl
		// (flushed to disk) while the game keeps running. Isolate the trail with
		// ORKIGE_BREADCRUMB_DIR.
		const bool breadcrumbCheck =
			(std::getenv("ORKIGE_BREADCRUMB_SELFCHECK") != nullptr);
		// ORKIGE_FADE_SELFCHECK verifies the full-screen fade transition against
		// tests/projects/fade: drive a screen.loadScene-style fade (out -> switch
		// sceneA to sceneB while opaque -> in) and assert the overlay alpha climbs
		// to opacity, the deferred scene switch applies WHILE the screen is
		// covered, and the fade clears afterwards. Exercises the real render loop
		// on BOTH flavors (see the ORKIGE_FADE_SELFCHECK block in this file).
		const bool fadeCheck =
			(std::getenv("ORKIGE_FADE_SELFCHECK") != nullptr);
		// ORKIGE_LIFECYCLE_SELFCHECK verifies the mobile app-lifecycle contract
		// end to end against tests/projects/lifecycle, driving the REAL player
		// wiring with SYNTHETIC SDL lifecycle events (SDL_PushEvent, the same
		// path a device's background/foreground takes): background the app and
		// assert the sim gate engaged, the save store was FLUSHED (its dirty flag
		// cleared, the value the script wrote from onAppPause persisted) and a
		// "background" breadcrumb landed; then foreground it and assert the sim
		// resumed, onAppResume fired and a "foreground" breadcrumb landed. See
		// the ORKIGE_LIFECYCLE_SELFCHECK block near the loop below.
		const bool lifecycleCheck =
			(std::getenv("ORKIGE_LIFECYCLE_SELFCHECK") != nullptr);
		// ORKIGE_PERF_SELFCHECK=1: run a real project for ~70 frames and
		// assert the performance instruments produce a truthful readback -
		// the profiler snapshot carries the canonical tick phases at depth 0,
		// the frame boundary folded (frames sampled, frame time measured) and
		// the allocation counters ran. The MEASURED numbers are logged as the
		// deliverable; nothing gates on wall-clock (CI machines are slower).
		const bool perfCheck =
			(std::getenv("ORKIGE_PERF_SELFCHECK") != nullptr);
		// automated runs (ctest, the editor's play-mode tests - they inherit
		// ORKIGE_DEMO_FRAMES from the editor's environment) render as fast as
		// the machine allows; a HUMAN run gets vsync so games neither spin
		// uncapped nor tear
		const bool automatedRun = jumperLuaCheck || rollerCheck ||
			rollerProgressionCheck || tweenCheck ||
			hotreloadCheck || scriptPropCheck ||
			integrationContactCheck || integrationLevelCheck ||
			breadcrumbCheck || fadeCheck || lifecycleCheck || softbodyCheck ||
			perfCheck ||
			!assetIdCheckTexture.empty() || frameLimit != 0;

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
#else
		hostConfig.windowTitle = "Orkige Player - " + scenePath;
		if (playerMediaDir != std::string(ORKIGE_PLAYER_MEDIA_DIR))
		{
			hostConfig.hlmsMediaDir = playerMediaDir;
		}
#endif
		hostConfig.automatedRun = automatedRun;
		hostConfig.engineLogFile = engineLogPath;
		hostConfig.classicMediaDir = playerMediaDir;
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
			}))
		{
			return 1;
		}
		Orkige::RenderSystem* render = host.getRenderSystem();
		Orkige::RenderWorld* world = host.getRenderWorld();

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
		Orkige::EngineLogCapture breadcrumbLog;
		breadcrumbLog.attach();

		// the window-camera rig from the host boot (fixed yaw keeps per-frame
		// lookAt calls roll-free) - project scripts drive it through the Lua
		// bindings (engine:getCamera():getNode(), engine:setCameraOrthographic,
		// ...)
		optr<Orkige::RenderCamera> camera = host.getWindowCamera();
		optr<Orkige::RenderNode> cameraNode = host.getCameraNode();

		// input pipeline: the poll loop below feeds every SDL event into the
		// InputManager, which triggers Orkige input events globally
		Orkige::InputManager inputManager;
		// phone-body vibration for mobile games (Lua `haptics` table). A device
		// build drives the taptic engine / Vibrator; desktop is an honest no-op
		// (isAvailable() == false). Like the InputManager, the editor never makes
		// one, so `haptics.*` is a no-op in edit mode.
		Orkige::HapticManager hapticManager;
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
		// ORKIGE_TILT_CALIB_SELFCHECK: the calibration wiring end to end,
		// synchronous (no render loop needed) - set a simulated tilt pose,
		// calibrate so it reads as neutral (0,-1,0), steer further to confirm it
		// deflects again, then round-trip the auto-written save file. Exercises
		// what the pure-math unit test cannot: getTilt applying the offset and
		// setCalibrationSaveFile/save/load persistence. Runs against any scene.
		if (std::getenv("ORKIGE_TILT_CALIB_SELFCHECK") != nullptr)
		{
			bool ok = true;
			std::string detail;
			inputManager.clearTiltCalibration();
			inputManager.setTiltAngle(0.6f);
			const Orkige::Vec3 rawPose = inputManager.getTilt();
			if (std::abs(rawPose.x) < 0.3f)
			{
				ok = false;
				detail = "raw pose not tilted";
			}
			inputManager.calibrateTilt();
			const Orkige::Vec3 neutral = inputManager.getTilt();
			if (ok && (std::abs(neutral.x) > 0.02f || neutral.y > -0.98f))
			{
				ok = false;
				detail = "calibrated pose did not read as neutral";
			}
			inputManager.setTiltAngle(1.0f);
			const Orkige::Vec3 deflected = inputManager.getTilt();
			if (ok && std::abs(deflected.x) < 0.3f)
			{
				ok = false;
				detail = "steering past the neutral pose did not deflect";
			}
			// persistence: calibrateTilt above auto-wrote the file; drop the
			// in-memory value and reload it
			const float savedAngle = inputManager.getTiltCalibration();
			inputManager.setTiltCalibration(0.0f);
			if (ok && (!inputManager.loadCalibration() ||
				std::abs(inputManager.getTiltCalibration() - savedAngle) > 1.0e-4f))
			{
				ok = false;
				detail = "calibration save/load did not round-trip";
			}
			SDL_Log("orkige_player: TILT CALIB SELFCHECK %s%s%s",
				ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
			return ok ? 0 : 1;
		}
		// ORKIGE_HAPTIC_SELFCHECK: the honest desktop no-op + API shape (device
		// vibration itself cannot be asserted headlessly). Asserts isAvailable()
		// is false on desktop, the play/pattern calls do not throw and no-op
		// cleanly (incl. while muted), and the pure name->pattern mapping defaults
		// an unknown name. Synchronous - no render loop needed.
		if (std::getenv("ORKIGE_HAPTIC_SELFCHECK") != nullptr)
		{
			bool ok = true;
			std::string detail;
			if (hapticManager.isAvailable())
			{
				ok = false;
				detail = "isAvailable() reported true on desktop";
			}
			// these must all return cleanly (honest no-op on desktop)
			hapticManager.play(0.6f, 120);
			hapticManager.playPatternByName("success");
			hapticManager.playPatternByName("unknown-name-defaults-to-medium");
			hapticManager.setEnabled(false);
			hapticManager.play(1.0f, 50);
			hapticManager.setEnabled(true);
			if (ok && Orkige::HapticManager::patternFromName("nope") !=
				Orkige::HapticManager::Pattern::Medium)
			{
				ok = false;
				detail = "unknown pattern name did not default to Medium";
			}
			SDL_Log("orkige_player: HAPTIC SELFCHECK %s%s%s",
				ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
			return ok ? 0 : 1;
		}
		// action mapping layered on top: named, rebindable actions the scripts
		// query by intent (actions:pressed("jump")). Built-in defaults cover
		// the reference games; a project's input.oactions (manifest Settings
		// "input.actions") overrides. Ticked once per frame in the input slot.
		Orkige::InputActionMap inputActions;
		if (project.isLoaded())
		{
			inputActions.loadForProject(project);
		}
		// localisation: the Lua loc() accessor reads the active-language
		// strings from this table. A project's localisation file (manifest
		// Settings "localisation", config-asset convention) loads it; games
		// without one just see the keys echoed back.
		Orkige::StringTable stringTable;
		if (project.isLoaded())
		{
			const std::string localisationRef = project.getSetting(
				Orkige::StringTable::LOCALISATION_SETTING_KEY);
			if (!localisationRef.empty())
			{
				const std::string localisationPath =
					project.resolvePath(localisationRef);
				if (!stringTable.loadFile(localisationPath))
				{
					SDL_Log("orkige_player: localisation file '%s' not loaded",
						localisationPath.c_str());
				}
			}
		}
		Orkige::QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&Orkige::QuitOnEscape::onKeyPressed, &quitOnEscape);

		// audio: the mixer lives on the SoundManager (per-source gain x group
		// volume, master on the AL listener); the "ears" ride the window
		// camera's rig node. A failed OpenAL init is NOT fatal - the game
		// runs silent, every sound call no-ops honestly (headless CI safety)
		Orkige::SoundManager soundManager(cameraNode);
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

		// the GameObject world from the host boot (the component factories
		// registered there, before the manager existed)
		Orkige::GameObjectManager& gameObjectManager =
			host.getGameObjectManager();
		Orkige::PhysicsWorld physicsWorld; // inert until init()
		// tweens tick in the ordered block of the main loop below; scripts
		// start them through the Lua `tween` table (scene clears reap them
		// via the GameObjectManager::clear teardown hook)
		Orkige::TweenManager tweenManager;
		// the level director: the ordered level sequence, the DEFERRED
		// scene-load request that drives win->next-level and the progression
		// save. Created like the TweenManager - the editor never makes one, so
		// the Lua level/loadScene API is an honest no-op there. Only projects
		// carrying a levels.olevels (manifest Settings "levels") get a sequence
		// + persistence; scriptless games keep it inert.
		Orkige::LevelManager levelManager;
		// full-screen fade transitions (engine-owned overlay on a reserved high
		// draw layer, both flavors): scripts drive it through the Lua `screen`
		// table. Ticked LAST in the loop (a presentation overlay). Like the
		// TweenManager, the editor never makes one, so `screen.*` is a no-op there.
		Orkige::ScreenFade screenFade;
		// camera-space screen shake (engine-owned, both flavors): scripts drive
		// it through the Lua `screen.shake` table. Ticked LAST in the loop (a
		// presentation effect), like the fade. The editor never makes one.
		Orkige::ScreenShake screenShake;
		// the gameplay time scale the loop applies to the scripts/tweens/physics
		// delta (Lua `world.setTimeScale`); the editor never makes one, so
		// gameplay stays real-time in edit mode.
		Orkige::TimeControl timeControl;
		// general per-project persistence (Lua `save` table): a typed
		// key->value store written atomically to the writable app dir. Set up for
		// any loaded project below; the editor never makes one, so `save.*` is an
		// honest no-op in edit mode.
		Orkige::SaveStore saveStore;
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

		if (!Orkige::SceneSerializer::loadScene(scenePath, gameObjectManager))
		{
			SDL_Log("orkige_player: FAILED - could not load scene '%s'",
				scenePath.c_str());
			return 1;
		}
		Orkige::applyUnlitFixToLoadedModels(gameObjectManager);
		SDL_Log("orkige_player: scene '%s' loaded (%zu GameObjects)",
			scenePath.c_str(), gameObjectManager.getGameObjects().size());

		// --- ORKIGE_ASSETID_SELFCHECK: prove the rename survived - the
		// scene's stale texture name must have been replaced by the expected
		// current one (resolved through the sidecar id) and the sprite must
		// actually be showing it
		if (!assetIdCheckTexture.empty())
		{
			optr<Orkige::GameObject> spriteObject =
				gameObjectManager.getGameObject("Sprite").lock();
			Orkige::SpriteComponent* sprite = (spriteObject &&
				spriteObject->hasComponent<Orkige::SpriteComponent>())
				? spriteObject->getComponentPtr<Orkige::SpriteComponent>()
				: nullptr;
			if (!sprite || !sprite->hasSprite() ||
				sprite->getTextureName() != assetIdCheckTexture ||
				sprite->getTextureAssetId().empty())
			{
				SDL_Log("orkige_player: ASSETID SELFCHECK FAILED - "
					"texture='%s' assetId='%s' hasSprite=%d (expected the "
					"stale scene reference to resolve to '%s' via its "
					"sidecar id)",
					sprite ? sprite->getTextureName().c_str() : "<no sprite "
					"component>",
					sprite ? sprite->getTextureAssetId().c_str() : "",
					sprite ? (sprite->hasSprite() ? 1 : 0) : 0,
					assetIdCheckTexture.c_str());
				return 1;
			}
			SDL_Log("orkige_player: ASSETID SELFCHECK PASSED - stale "
				"reference resolved to '%s' (id %s)",
				sprite->getTextureName().c_str(),
				sprite->getTextureAssetId().c_str());
		}

		// remote debugging server (editor play mode): localhost only; the
		// editor keeps re-connecting until this point is reached, so the
		// engine boot time above does not matter
		Orkige::PlayerDebugLink debugLink;
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
		bool physicsNeeded = sceneHasRigidBodies(gameObjectManager);
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
		auto reloadSceneFrom = [&](std::string const & newScenePath) -> bool
		{
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
			SDL_Log("orkige_player: switched to scene '%s' (%zu GameObjects)",
				newScenePath.c_str(),
				gameObjectManager.getGameObjects().size());
			return true;
		};

		// default view: matches the editor's initial orbit camera pose so a
		// scene looks the same in the player as in a fresh editor viewport
		// (project scripts may re-place the rig from their init())
		cameraNode->setPosition(Orkige::Vec3(0.0f, 2.5f, 9.0f));
		cameraNode->lookAt(Orkige::Vec3::ZERO, Orkige::RenderNode::TS_WORLD);

		// ORKIGE_GAMESUPPORT_SELFCHECK: the game-support pack verified end to end,
		// synchronous (no render loop needed) against the live player wiring:
		//  (1) SAVE - set typed values through the Lua `save` table (proving the
		//      binding + the player's SaveStore file wiring), flush to disk, then
		//      re-load into a FRESH store (a "restart") and read them back;
		//  (2) SCREEN SHAKE - drive ScreenShake against the live window-camera rig
		//      node: it must deflect mid-shake and restore the node to EXACTLY its
		//      rest pose when the shake runs out;
		//  (3) TIME SCALE - feed the TweenManager the loop's delta*timeScale and
		//      assert timeScale 0.5 lands a tween at HALF the progress that
		//      timeScale 1.0 reaches over the same updates.
		// The ortho aspect-fit policy is proven headlessly by the CameraFit unit
		// tests (visible world rect across 4:3..21:9).
		if (std::getenv("ORKIGE_GAMESUPPORT_SELFCHECK") != nullptr)
		{
			bool ok = true;
			std::string detail;

			// (1) save round-trip through the Lua binding + a fresh-store reload
			std::string saveDir;
			if (const char* d = std::getenv("ORKIGE_PROGRESS_DIR"))
			{
				saveDir = d;
			}
			else
			{
				saveDir = std::filesystem::temp_directory_path().string();
			}
			if (!saveDir.empty() && saveDir.back() != '/')
			{
				saveDir += '/';
			}
			std::error_code saveDirErr;
			std::filesystem::create_directories(saveDir, saveDirErr);
			const std::string saveFile = saveDir + "gamesupport_save.osave";
			std::filesystem::remove(saveFile, saveDirErr);
			saveStore.setSaveFile(saveFile);
			Orkige::ScriptComponent::ensureScriptApi();
			Orkige::ScriptRuntime::Result saveResult =
				Orkige::ScriptRuntime::getSingleton().runString(
					"save.set('coins', 42)\n"
					"save.set('player.name', 'Ada')\n"
					"save.set('unlocked', true)\n"
					"return save.flush()");
			if (!saveResult.success)
			{
				ok = false;
				detail = "save script error: " + saveResult.error;
			}
			if (ok)
			{
				// model the next launch: wipe the in-memory store and re-load
				// from disk - the values must have reached the file via flush()
				saveStore.clear();
				if (!saveStore.load() ||
					saveStore.getNumber("coins", 0.0) != 42.0 ||
					saveStore.getString("player.name", "") != "Ada" ||
					saveStore.getBool("unlocked", false) != true)
				{
					ok = false;
					detail = "save did not round-trip through a reload from disk";
				}
			}

			// (2) screen shake deflects then restores the camera EXACTLY
			if (ok)
			{
				const Orkige::Vec3 rest = cameraNode->getPosition();
				screenShake.shake(1.0f, 0.2f, 30.0f);
				screenShake.update(0.05f);	// one frame into the shake
				const Orkige::Vec3 shaken = cameraNode->getPosition();
				if ((shaken - rest).length() < 1.0e-4f)
				{
					ok = false;
					detail = "screen shake did not deflect the camera";
				}
				// run well past the 0.2s duration, then it must be back at rest
				for (int i = 0; ok && i < 8; ++i)
				{
					screenShake.update(0.05f);
				}
				const Orkige::Vec3 after = cameraNode->getPosition();
				if (ok && ((after - rest).length() > 1.0e-5f ||
					screenShake.isShaking()))
				{
					ok = false;
					detail = "screen shake did not restore the camera to rest";
				}
			}

			// (3) time scale halves a tween's progress over the same updates
			if (ok)
			{
				auto runTween = [&](float scale) -> float
				{
					timeControl.setTimeScale(scale);
					float value = 0.0f;
					const float from = 0.0f;
					const float to = 1.0f;
					tweenManager.startTween(&from, &to, 1, 1.0f,
						&Orkige::Ease::linear,
						[&value](float const* values, int) -> bool
						{
							value = values[0];
							return true;
						},
						Orkige::TweenManager::CompleteFunction(), 0.0f,
						Orkige::StringUtil::BLANK);
					// ten updates of the loop's delta*timeScale (base dt 0.1)
					for (int i = 0; i < 10; ++i)
					{
						tweenManager.update(0.1f * timeControl.getTimeScale());
					}
					tweenManager.clear();	// reap before the next run
					return value;
				};
				const float fullProgress = runTween(1.0f);
				const float halfProgress = runTween(0.5f);
				timeControl.setTimeScale(1.0f);
				if (std::abs(fullProgress - 1.0f) > 1.0e-3f ||
					std::abs(halfProgress - 0.5f) > 1.0e-3f)
				{
					ok = false;
					detail = "time scale did not halve tween progress (full=" +
						std::to_string(fullProgress) + " half=" +
						std::to_string(halfProgress) + ")";
				}
			}

			SDL_Log("orkige_player: GAMESUPPORT SELFCHECK %s%s%s",
				ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
			return ok ? 0 : 1;
		}

		// frame-time statistics: the ORKIGE_DEMO_FPS_LOG measurement hook and
		// the one-time "this build is too slow to play" hint
		Orkige::FrameStatsUtil frameStats;

		// --- ORKIGE_JUMPER_LUA_SELFCHECK=1: the ScriptComponent milestone,
		// verified end to end against projects/jumper-lua (run with
		// --project projects/jumper-lua). Synthetic SDL key AND mouse events
		// take the real input path (poll loop -> injectEvent -> InputManager
		// events -> GuiManager/isKeyDown); the C++ side observes ONLY
		// what any outsider could: the Player object's components through
		// the world, the Lua-booted gui widgets through the
		// GuiManager singleton and the stats the scripts publish into
		// the Lua `shared` tables. Asserted, frame-scripted:
		//   frame  5  both scripts loaded (player.lua, game.lua), the UI is
		//             up (all widgets exist), the game is on the visible
		//             title screen and the HUD is hidden
		//   10..20    a synthetic mouse click on the START button (move,
		//             press, release) -> the game switches to "playing",
		//             title hidden, HUD visible
		//   30..65    hold RIGHT -> the player moved +x and the HUD
		//             progress bar advanced
		//   frame 80  the script reports grounded; press SPACE -> the player
		//             rises >0.8m and lands again within 120 frames
		//   then      teleport into the first gap -> the script's kill plane
		//             respawns the player at the start (shared respawns +1)
		//   then      teleport in front of the goal -> the script's win
		//             check fires (shared wins +1) -> the win screen shows
		//   then      press ENTER -> the game restarts into "playing" with
		//             the win screen hidden; the follow camera stayed
		//             roll-free over the whole session
		// Any missed deadline exits non-zero; measured values are logged.
		// (jumperLuaCheck itself is read above, before the engine window
		// exists.)
		enum class JumperCheckPhase
		{
			Script,			// fixed-frame part (boot+UI, click, move, jump)
			WaitLanding,	// jump flight until the script reports grounded
			WaitRespawn,	// falling in the gap until the script respawned
			WaitWin,		// in front of the goal until the win fired
			WaitWinUi,		// until the win screen is visible, then ENTER
			WaitRestart,	// until ENTER put the game back into "playing"
			Done
		};
		JumperCheckPhase jumperPhase = JumperCheckPhase::Script;
		float jumperStartX = 0.0f;
		float jumperMovedX = 0.0f;
		float jumperJumpStartY = 0.0f;
		float jumperMaxRise = 0.0f;
		unsigned long jumperJumpFrame = 0;
		unsigned long jumperEnterFrame = 0;
		unsigned long jumperPhaseDeadline = 0;
		double jumperBaseRespawns = 0.0;
		double jumperBaseWins = 0.0;
		size_t jumperBatchesWithUi = 0;
		bool jumperCheckFailed = false;
		// the script's published state (shared.jumper.<key>) - the honest
		// outside view of the Lua game; missing values read as the fallback
		auto jumperStat = [](const char* key, double fallback) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "jumper", key}, fallback);
		};
		auto jumperGrounded = []() -> bool
		{
			return Orkige::ScriptRuntime::getSingleton().getBool(
				{"shared", "jumper", "grounded"}, false);
		};
		// the game-flow state game.lua publishes (shared.game.<key>)
		auto jumperGameState = []() -> std::string
		{
			return Orkige::ScriptRuntime::getSingleton().getString(
				{"shared", "game", "state"}, "");
		};
		auto jumperGameStat = [](const char* key) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "game", key}, -1.0);
		};
		// the Lua-booted UI, seen through the GuiManager singleton: does
		// the widget exist, and is its screen (= its shared z layer) visible.
		// gui runs on BOTH render flavors
		// (engine:hasUISystem() is true everywhere), so the UI assertions
		// are no longer flavor-gated - uiChecksEnabled stays as the one
		// switch a future UI-less flavor would flip.
		constexpr bool uiChecksEnabled = true;
		auto jumperWidgetExists = [](const char* id) -> bool
		{
			Orkige::GuiManager* ui =
				Orkige::GuiManager::getSingletonPtr();
			return ui && ui->widgetExists(id);
		};
		auto jumperWidgetVisible = [&jumperWidgetExists](const char* id) -> bool
		{
			if (!jumperWidgetExists(id))
			{
				return false;
			}
			optr<Orkige::GuiWidget> widget =
				Orkige::GuiManager::getSingleton().getWidget(id).lock();
			return widget && widget->getLayer()->isVisible();
		};
		auto jumperHudProgress = [&jumperWidgetExists]() -> float
		{
			if (!jumperWidgetExists("hud.progress"))
			{
				return -1.0f;
			}
			optr<Orkige::GuiProgressBar> progressBar =
				Orkige::GuiManager::getSingleton()
					.getWidgetAs<Orkige::GuiProgressBar>("hud.progress")
					.lock();
			return progressBar ? progressBar->getProgress() : -1.0f;
		};
		// player object access through the world - what the script drives
		auto jumperPlayerTransform = [&gameObjectManager]()
			-> Orkige::TransformComponent*
		{
			optr<Orkige::GameObject> player =
				gameObjectManager.getGameObject("Player").lock();
			if (!player ||
				!player->hasComponent<Orkige::TransformComponent>())
			{
				return nullptr;
			}
			return player->getComponentPtr<Orkige::TransformComponent>();
		};
		// teleport the player body between check phases (the same pose reset
		// the jumper sample's selfcheck uses)
		auto jumperTeleport = [&gameObjectManager, &physicsWorld,
			&jumperPlayerTransform](Orkige::Vec3 const& position)
		{
			optr<Orkige::GameObject> player =
				gameObjectManager.getGameObject("Player").lock();
			if (!player || !player->hasComponent<Orkige::RigidBodyComponent>())
			{
				return;
			}
			Orkige::RigidBodyComponent* body =
				player->getComponentPtr<Orkige::RigidBodyComponent>();
			if (!body->hasBody())
			{
				return;
			}
			physicsWorld.setBodyTransform(body->getBodyId(), position,
				Orkige::Quat::IDENTITY);
			body->setLinearVelocity(Orkige::Vec3::ZERO);
			body->setAngularVelocity(Orkige::Vec3::ZERO);
			jumperPlayerTransform()->setPosition(position);
		};
		auto jumperFail = [&](std::string const& what)
		{
			Orkige::TransformComponent* transform = jumperPlayerTransform();
			const Orkige::Vec3 position = transform ?
				transform->getPosition() : Orkige::Vec3::ZERO;
			SDL_Log("orkige_player: JUMPER-LUA SELFCHECK FAILED - %s "
				"(pos %.2f/%.2f/%.2f grounded=%d respawns=%.0f wins=%.0f)",
				what.c_str(), position.x, position.y, position.z,
				static_cast<int>(jumperGrounded()),
				jumperStat("respawns", -1.0), jumperStat("wins", -1.0));
			jumperCheckFailed = true;
		};

		// --- ORKIGE_ROLLER_SELFCHECK=1: the 2D tier (SpriteComponent, ortho
		// camera, tilt input, physics pause + kinematic tile teleports),
		// verified end to end against projects/roller (run with
		// --project projects/roller). Same rules as the jumper selfcheck:
		// synthetic SDL key events take the real input path, the C++ side
		// observes only shared.roller, components through the world and the
		// gui widgets. Frame-scripted:
		//   frame  5  both scripts booted, HUD up, mode "play", sim running
		//   then      ACTIVE GATE: deactivate the TileC subtree + the Ball -
		//             triangle count drops, the tile's wall body leaves the
		//             simulation, ball.lua stops ticking; reactivate restores
		//   then      hold LEFT -> the simulated tilt turns gravity left and
		//             the ball rolls -x (position sampled)
		//             screenshot roller_play.png (when the env dir is set)
		//   then      TAB -> mode "move": physics PAUSED, cursor visible
		//             over the empty slot; collision probed at tile B's
		//             future wall location (must be free)
		//             DOWN -> tile B slides into the empty slot VIA ITS
		//             PARENT (one teleport of the "TileB" group object):
		//             frame sprite + GOAL moved -6 in WORLD y, their LOCAL
		//             transforms untouched, and a ray probe proves the wall
		//             BODY collides at the new spot (and no longer at the
		//             old one) - while still paused
		//             TAB -> back to "play", sim unpaused
		//   then      hold RIGHT -> gravity swings right, the ball rolls
		//             through tile A's right opening into the slid-down
		//             tile B onto the goal star -> shared.roller.wins fires
		//             -> the win banner shows
		// Any missed deadline exits non-zero; measured values are logged.
		// note: the tilt SIMULATION advances on wall-clock frame time (the
		// human-facing behavior) while headless automated runs floor the game
		// dt at 1/60 - so the tilt phases below are CONDITION-driven with fat
		// frame deadlines instead of fixed frame numbers
		enum class RollerCheckPhase
		{
			Boot,		// scripts/HUD/camera/mode checks at frame 5
			ActiveGate,	// deactivate a tile subtree + the ball: no render,
						// no collision, no script ticks; reactivate restores
			TiltRoll,	// hold LEFT until the tilt built up and the ball rolled
			MoveWorld,	// TAB, probe, DOWN-slide, probe, TAB (frame-scripted)
			WaitWin,	// rolling right until the script reports the win
			WaitWinUi,	// until the win banner layer is visible
			Done
		};
		RollerCheckPhase rollerPhase = RollerCheckPhase::Boot;
		unsigned long rollerStepFrame = 0;	// current phase's anchor frame
		float rollerStartX = 0.0f;
		float rollerMovedX = 0.0f;
		float rollerTileBStartY = 0.0f;
		float rollerGoalStartY = 0.0f;
		float rollerTileBFrameLocalY = 0.0f;	// child LOCAL y (must not change)
		std::size_t rollerTrianglesActive = 0;	// triangles with everything active
		double rollerBallUpdatesGated = 0.0;	// ball.lua ticks while deactivated
		unsigned long rollerRollFrame = 0;
		unsigned long rollerPhaseDeadline = 0;
		bool rollerCheckFailed = false;
		auto rollerStat = [](const char* key, double fallback) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "roller", key}, fallback);
		};
		auto rollerFlag = [](const char* key) -> bool
		{
			return Orkige::ScriptRuntime::getSingleton().getBool(
				{"shared", "roller", key}, false);
		};
		auto rollerMode = []() -> std::string
		{
			return Orkige::ScriptRuntime::getSingleton().getString(
				{"shared", "roller", "mode"}, "");
		};
		auto rollerTransform = [&gameObjectManager](const char* id)
			-> Orkige::TransformComponent*
		{
			optr<Orkige::GameObject> gameObject =
				gameObjectManager.getGameObject(id).lock();
			if (!gameObject ||
				!gameObject->hasComponent<Orkige::TransformComponent>())
			{
				return nullptr;
			}
			return gameObject->getComponentPtr<Orkige::TransformComponent>();
		};
		// flavor-neutral, like the jumper
		// lambdas above
		auto rollerWidgetExists = [](const char* id) -> bool
		{
			Orkige::GuiManager* ui =
				Orkige::GuiManager::getSingletonPtr();
			return ui && ui->widgetExists(id);
		};
		auto rollerWidgetVisible = [&rollerWidgetExists](const char* id) -> bool
		{
			if (!rollerWidgetExists(id))
			{
				return false;
			}
			optr<Orkige::GuiWidget> widget =
				Orkige::GuiManager::getSingleton().getWidget(id).lock();
			return widget && widget->getLayer()->isVisible();
		};
		//! is the cursor sprite (the move-mode highlight) currently showing
		auto rollerCursorVisible = [&gameObjectManager]() -> bool
		{
			optr<Orkige::GameObject> cursor =
				gameObjectManager.getGameObject("Cursor").lock();
			if (!cursor || !cursor->hasComponent<Orkige::SpriteComponent>())
			{
				return false;
			}
			return cursor->getComponentPtr<Orkige::SpriteComponent>()
				->isSpriteVisible();
		};
		//! probe the physics world for collision geometry at (x, y): a ray
		//! along +z through the tile plane - the honest "did the BODY really
		//! move" check for the tile slide
		auto rollerProbeHit = [&physicsWorld](float x, float y) -> bool
		{
			Orkige::Vec3 hitPosition;
			Orkige::PhysicsWorld::BodyId hitBody;
			return physicsWorld.castRay(Orkige::Vec3(x, y, -3.0f),
				Orkige::Vec3(0.0f, 0.0f, 1.0f), 6.0f, hitPosition, hitBody);
		};
		auto rollerScreenshot = [&render, &rollerShotDir](const char* name)
		{
			if (rollerShotDir.empty())
			{
				return;
			}
			const std::string path = rollerShotDir + "/" + name;
			render->saveWindowContents(path);
			SDL_Log("orkige_player: roller selfcheck - screenshot %s",
				path.c_str());
		};
		auto rollerFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: ROLLER SELFCHECK FAILED - %s "
				"(ball %.2f/%.2f mode '%s' slides=%.0f wins=%.0f "
				"respawns=%.0f)", what.c_str(),
				rollerStat("x", -999.0), rollerStat("y", -999.0),
				rollerMode().c_str(), rollerStat("slides", -1.0),
				rollerStat("wins", -1.0), rollerStat("respawns", -1.0));
			rollerCheckFailed = true;
		};

		// --- ORKIGE_SOFTBODY_SELFCHECK=1: soft, deformable organic shapes end to
		// end against projects/vectorshapes (scenes/softbody.oscene). Phased,
		// condition-driven (physics is wall-clock/fixed-step paced):
		//   fall     the "FallBlob" (VectorShape softBody + a dynamic RigidBody)
		//            drops; on landing a CONTACT squashes it (getSquash != 0) and
		//            the wobble springs move the mesh off rest (displacement > 0)
		//   settle   the wobble decays back to the rest pose (not deforming)
		//   morph    the "MorphBlob" is deformed by a Lua self.shape:playMorph
		//            call (proves the morph blend + the Lua drive path)
		// A missed deadline exits non-zero. On boot it logs the MEASURED per-frame
		// deform cost - the mobile-viability budget number the owner asked for.
		enum class SoftBodyPhase { Fall, Settle, Morph, Done };
		SoftBodyPhase softbodyPhase = SoftBodyPhase::Fall;
		bool softbodyCheckFailed = false;
		bool softbodySawSquash = false;
		bool softbodySawWobble = false;
		float softbodyPeakSquash = 0.0f;
		float softbodyPeakWobble = 0.0f;
		float softbodyMorphPeak = 0.0f;
		unsigned long softbodyPhaseDeadline = 0;
		bool softbodyMorphStarted = false;
		auto softbodyShape = [&gameObjectManager](const char* id)
			-> Orkige::VectorShapeComponent*
		{
			optr<Orkige::GameObject> gameObject =
				gameObjectManager.getGameObject(id).lock();
			if (!gameObject ||
				!gameObject->hasComponent<Orkige::VectorShapeComponent>())
			{
				return nullptr;
			}
			return gameObject->getComponentPtr<Orkige::VectorShapeComponent>();
		};
		auto softbodyFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: SOFTBODY SELFCHECK FAILED - %s "
				"(squash peak %.3f, wobble peak %.4f, morph peak %.4f)",
				what.c_str(), softbodyPeakSquash, softbodyPeakWobble,
				softbodyMorphPeak);
			softbodyCheckFailed = true;
		};
		if (softbodyCheck)
		{
			// MEASURED per-frame deform cost: build N representative blobs and
			// time a full deform tick (spring integrate + skin + write) each, so
			// the mobile budget is a real number. Pure core, allocation-free per
			// frame - no renderer involved.
			const int benchBlobs = 64;
			std::vector<Orkige::VectorTessellator::Region> benchRegions;
			{
				Orkige::VectorTessellator::Region region;
				region.fill = Orkige::VectorTessellator::Colour(1, 1, 1, 1);
				const int n = 24;
				for (int i = 0; i < n; ++i)
				{
					const float a = 2.0f * 3.14159265f * i / n;
					const float r = 1.0f + 0.12f * std::sin(a * 3.0f);
					region.outer.push_back(Orkige::VectorTessellator::Point(
						r * std::cos(a), r * std::sin(a)));
				}
				benchRegions.push_back(region);
			}
			Orkige::VectorTessellator::Mesh benchMesh;
			Orkige::VectorTessellator::build(benchRegions, 0.02f, benchMesh);
			std::vector<Orkige::SoftBodyDeform> benchDeformers(benchBlobs);
			for (int b = 0; b < benchBlobs; ++b)
			{
				benchDeformers[b].build(benchRegions, benchMesh.positions,
					Orkige::SoftBodyDeform::Params());
				benchDeformers[b].applyImpulse(0.3f, -1.0f, 2.0f);
				benchDeformers[b].setBodyVelocity(1.5f, -2.0f);
			}
			std::vector<Orkige::VectorTessellator::Point> benchScratch;
			const int benchFrames = 120;
			const auto benchStart = std::chrono::high_resolution_clock::now();
			for (int f = 0; f < benchFrames; ++f)
			{
				for (int b = 0; b < benchBlobs; ++b)
				{
					benchDeformers[b].update(1.0f / 60.0f);
					benchDeformers[b].writePositions(benchScratch);
				}
			}
			const auto benchEnd = std::chrono::high_resolution_clock::now();
			const double totalUs = std::chrono::duration_cast<
				std::chrono::nanoseconds>(benchEnd - benchStart).count() / 1000.0;
			const double perBlobFrameUs =
				totalUs / (benchFrames * benchBlobs);
			SDL_Log("orkige_player: SOFTBODY deform budget - %.2f us/blob/frame "
				"(%zu verts, %zu control points; %d blobs x %d frames = %.0f us "
				"total); a 60fps frame fits ~%.0f such blobs in 1 ms",
				perBlobFrameUs, benchMesh.positions.size(),
				benchDeformers[0].controlPointCount(), benchBlobs, benchFrames,
				totalUs, perBlobFrameUs > 0.0 ? 1000.0 / perBlobFrameUs : 0.0);
		}

		// --- ORKIGE_ROLLER_PROGRESSION_SELFCHECK=1: the level sequence + the
		// DEFERRED scene switch + the progression save, end to end
		// against projects/roller. Same discipline as the roller selfcheck
		// (synthetic keys through the real input path, C++ observes only
		// shared.roller, the components and the LevelManager). Phased:
		//   frame 5   scripts up, at level 1 (index 0), the sequence loaded
		//   solve L1  TAB->move, DOWN slides tile B into the empty slot, TAB->
		//             play, hold RIGHT until the ball rolls onto the goal (win)
		//   switch    game.lua completes the level, records/persists progress
		//             and, after the banner beat, the DEFERRED load switches to
		//             level 2: assert shared.roller.levelIndex became 1, the
		//             save file was written and resumeLevel persisted to 1
		//   solve L2  the straight-shot level: hold RIGHT until the win
		// note: the tilt phases are wall-clock paced, so they are condition-
		// driven with fat frame deadlines (like the roller selfcheck).
		enum class RollerProgPhase
		{
			Boot,			// scripts up + at level 0 at frame 5
			L1Slide,		// TAB, DOWN-slide, TAB (frame-scripted from the anchor)
			L1Roll,			// hold RIGHT until level 1's win
			WaitSwitch,		// until the deferred load reaches level 2
			L2Roll,			// hold RIGHT until level 2's win
			Done
		};
		RollerProgPhase rollerProgPhase = RollerProgPhase::Boot;
		unsigned long rollerProgStepFrame = 0;
		unsigned long rollerProgDeadline = 0;
		bool rollerProgCheckFailed = false;
		auto rollerProgFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: ROLLER PROGRESSION SELFCHECK FAILED - %s "
				"(levelIndex=%.0f mode '%s' slides=%.0f wins=%.0f saved=%d)",
				what.c_str(), rollerStat("levelIndex", -1.0),
				rollerMode().c_str(), rollerStat("slides", -1.0),
				rollerStat("wins", -1.0),
				static_cast<int>(Orkige::ScriptRuntime::getSingleton().getBool(
					{"shared", "roller", "saved"}, false)));
			rollerProgCheckFailed = true;
		};

		// --- ORKIGE_TWEEN_SELFCHECK=1: the tween system end to end against
		// tests/projects/tween (run with --project tests/projects/tween).
		// The scene's Tweener object runs scripts/tween_check.lua, which
		// starts the whole Lua tween surface (tween.to closure + typed
		// fade/volume helpers, cancel, delay, onComplete) and publishes its
		// observations into shared.tween; the C++ side verifies only what an
		// outsider can see: the script's verdict, the Tweener transform (the
		// tween.to closure drove x to 3), the sprite tint alpha (tween.fade),
		// the "music" group volume (tween.volume) and the manifest-loaded
		// mixer settings (audio.master=0.8 / audio.group.hud=0.4 from
		// project.orkproj). Condition-driven (durations live in simulated
		// time, deterministic under the floored automated dt) with a fat
		// frame deadline.
		bool tweenCheckDone = false;
		bool tweenCheckFailed = false;
		auto tweenStat = [](const char* key, double fallback) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "tween", key}, fallback);
		};
		auto tweenFlag = [](const char* key) -> bool
		{
			return Orkige::ScriptRuntime::getSingleton().getBool(
				{"shared", "tween", key}, false);
		};
		auto tweenFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: TWEEN SELFCHECK FAILED - %s "
				"(updates=%.0f completes=%.0f cancelUpdates=%.0f "
				"delayFirstAt=%.3f script='%s')", what.c_str(),
				tweenStat("updates", -1.0), tweenStat("completes", -1.0),
				tweenStat("cancelUpdates", -1.0),
				tweenStat("delayFirstAt", -1.0),
				Orkige::ScriptRuntime::getSingleton().getString(
					{"shared", "tween", "failed"}, "").c_str());
			tweenCheckFailed = true;
		};

		// --- ORKIGE_HOTRELOAD_SELFCHECK=1: Lua hot-reload, verified
		// end to end against tests/projects/hotreload (run with --project
		// tests/projects/hotreload). The Probe object runs scripts/
		// reload_probe.lua, whose init publishes shared.hotreload.value = 1.
		// Frame-scripted (hotReload is synchronous, so fixed frames are safe):
		//   frame  5  script loaded + started, value == 1; save the script's
		//             ORIGINAL bytes and MOVE the Probe transform to a
		//             distinctive engine-side pose (7,0,0)
		//   frame 10  overwrite the file to publish value = 2, drive
		//             hotReload() -> the running value flips to 2 (behavior
		//             changed) AND the (7,0,0) transform SURVIVED the swap
		//             (engine-side state persists), re-init ran, no error
		//   frame 15  record the tick count, overwrite the file with BROKEN
		//             Lua, drive hotReload() -> a non-fatal reload error
		//             surfaces (hasReloadError, mFailed still false) and the
		//             OLD instance (value == 2) stays intact
		//   frame 20  the surviving instance kept ticking (ticks advanced) ->
		//             done. The committed script is restored below on exit.
		enum class HotReloadPhase { Boot, AfterSwap, AfterBreak, Done };
		HotReloadPhase hotreloadPhase = HotReloadPhase::Boot;
		bool hotreloadCheckFailed = false;
		std::string hotreloadScriptPath;	// resolved reload_probe.lua path
		std::string hotreloadOriginal;		// its committed bytes (restored on exit)
		double hotreloadTicksAtBreak = -1.0;
		auto hotreloadNumber = [](const char* key, double fallback) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "hotreload", key}, fallback);
		};
		auto hotreloadProbeScript = [&gameObjectManager]()
			-> Orkige::ScriptComponent*
		{
			optr<Orkige::GameObject> probe =
				gameObjectManager.getGameObject("Probe").lock();
			if (!probe || !probe->hasComponent<Orkige::ScriptComponent>())
			{
				return nullptr;
			}
			return probe->getComponentPtr<Orkige::ScriptComponent>();
		};
		auto hotreloadProbeTransform = [&gameObjectManager]()
			-> Orkige::TransformComponent*
		{
			optr<Orkige::GameObject> probe =
				gameObjectManager.getGameObject("Probe").lock();
			if (!probe || !probe->hasComponent<Orkige::TransformComponent>())
			{
				return nullptr;
			}
			return probe->getComponentPtr<Orkige::TransformComponent>();
		};
		auto hotreloadWriteScript = [&hotreloadScriptPath](
			std::string const& source) -> bool
		{
			std::ofstream file(hotreloadScriptPath,
				std::ios::binary | std::ios::trunc);
			file << source;
			return file.good();
		};
		auto hotreloadFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: HOTRELOAD SELFCHECK FAILED - %s "
				"(value=%.0f inits=%.0f ticks=%.0f)", what.c_str(),
				hotreloadNumber("value", -1.0), hotreloadNumber("inits", -1.0),
				hotreloadNumber("ticks", -1.0));
			hotreloadCheckFailed = true;
		};

		// --- ORKIGE_SCRIPTPROP_SELFCHECK=1: Lua script exported properties
		// verified end to end against tests/projects/scriptprop.
		// The scene bakes the Mover's exported moveSpeed at 5 (the script's own
		// default is 1), so the running script seeing 5 at init proves the
		// PER-INSTANCE value round-tripped through serialization AND was injected
		// onto `self` before init. The script moves +x at moveSpeed; a live set
		// of moveSpeed -> 0 through the reflected setter (the debug-protocol /
		// MCP write path) must stop the motion. Frame-scripted:
		//   frame  5  script up, injectedSpeed == 5, x advanced from 0
		//   frame 15  x == 5 * elapsed (behaved with the injected value), then
		//             flip moveSpeed -> 0 through the reflected setter
		//   frame 25  x stopped (the live set reached self.moveSpeed) -> done
		enum class ScriptPropPhase { Boot, Moving, Frozen, Done };
		ScriptPropPhase scriptPropPhase = ScriptPropPhase::Boot;
		bool scriptPropCheckFailed = false;
		double scriptPropXAtFreeze = 0.0;
		auto scriptPropNumber = [](const char* key, double fallback) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "scriptprop", key}, fallback);
		};
		auto moverScript = [&gameObjectManager]() -> Orkige::ScriptComponent*
		{
			optr<Orkige::GameObject> mover =
				gameObjectManager.getGameObject("Mover").lock();
			if (!mover || !mover->hasComponent<Orkige::ScriptComponent>())
			{
				return nullptr;
			}
			return mover->getComponentPtr<Orkige::ScriptComponent>();
		};
		auto scriptPropFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: SCRIPTPROP SELFCHECK FAILED - %s "
				"(injected=%.3f x=%.3f elapsed=%.3f)", what.c_str(),
				scriptPropNumber("injectedSpeed", -1.0),
				scriptPropNumber("x", -999.0),
				scriptPropNumber("elapsed", -1.0));
			scriptPropCheckFailed = true;
		};

		// --- ORKIGE_INTEGRATION_CONTACT_SELFCHECK=1: tags + input action +
		// physics + contact events, end to end against
		// tests/projects/integration/scenes/contact.oscene. ball_probe.lua
		// discovers the goal BY TAG at init, then hangs still (gravity off)
		// until the injected "jump" action turns gravity on; the ensuing drop
		// overlaps the goal SENSOR and fires onContactBegin. Condition-driven:
		//   [VerifyTags]   the script published exactly one tag-found goal
		//                  ("Goal"), then inject a HELD SPACE (the "jump" edge)
		//   [DriveInput]   release SPACE a few frames later; the action must
		//                  have reached the script (input count advanced)
		//   [AwaitContact] the input-driven fall must fire onContactBegin with
		//                  the tagged goal as the other body
		enum class IntegContactPhase { VerifyTags, DriveInput, AwaitContact,
			Done };
		IntegContactPhase integContactPhase = IntegContactPhase::VerifyTags;
		bool integContactFailed = false;
		unsigned long integContactInputFrame = 0;
		auto integContactNum = [](const char* key, double fallback) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "integration", key}, fallback);
		};
		auto integContactStr = [](const char* key) -> std::string
		{
			return Orkige::ScriptRuntime::getSingleton().getString(
				{"shared", "integration", key}, "");
		};
		auto integContactFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: INTEGRATION CONTACT SELFCHECK FAILED - %s "
				"(found=%.0f foundGoal='%s' input=%.0f contact=%.0f "
				"contactOther='%s')", what.c_str(),
				integContactNum("found", -1.0),
				integContactStr("foundGoal").c_str(),
				integContactNum("input", -1.0),
				integContactNum("contact", -1.0),
				integContactStr("contactOther").c_str());
			integContactFailed = true;
		};

		// --- ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK=1: a deferred level
		// switch fired while a tween + a live particle emitter run, end to end
		// against tests/projects/integration/scenes/levelA.oscene ->
		// levelB.oscene. director.lua starts a 1.5s tween.move and a particle
		// burst, then requests world.loadScene mid-tween; survivor.lua on level
		// B proves the switched-to world ticks and that the shared table (a
		// carry marker) survived the GameObjectManager::clear teardown.
		// Condition-driven:
		//   [ObserveA]     on level A, confirm the tween is moving Mover AND the
		//                  Fx emitter has live particles (both mid-flight)
		//   [AwaitSwitch]  wait for level B to boot (the deferred switch applied)
		//   [VerifyB]      Mover is GONE (clean teardown), the carry survived,
		//                  and level B keeps ticking
		enum class IntegLevelPhase { ObserveA, AwaitSwitch, VerifyB, Done };
		IntegLevelPhase integLevelPhase = IntegLevelPhase::ObserveA;
		bool integLevelFailed = false;
		double integLevelTicksAtEntry = -1.0;
		unsigned long integLevelVerifyFrame = 0;
		auto integLevelNum = [](const char* key, double fallback) -> double
		{
			return Orkige::ScriptRuntime::getSingleton().getNumber(
				{"shared", "integ2", key}, fallback);
		};
		auto integFxLiveCount = [&gameObjectManager]() -> int
		{
			optr<Orkige::GameObject> fx =
				gameObjectManager.getGameObject("Fx").lock();
			if (!fx || !fx->hasComponent<Orkige::ParticleComponent>())
			{
				return -1;
			}
			return fx->getComponentPtr<Orkige::ParticleComponent>()
				->getLiveCount();
		};
		auto integLevelFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: INTEGRATION LEVELSWITCH SELFCHECK FAILED - "
				"%s (aliveA=%.0f moverX=%.3f switched=%.0f levelBBooted=%.0f "
				"carrySeen=%.0f levelBTicks=%.0f)", what.c_str(),
				integLevelNum("aliveA", -1.0), integLevelNum("moverX", -999.0),
				integLevelNum("switched", -1.0),
				integLevelNum("levelBBooted", -1.0),
				integLevelNum("carrySeen", -1.0),
				integLevelNum("levelBTicks", -1.0));
			integLevelFailed = true;
		};

		// mobile app lifecycle: the backgrounding contract as a pure state
		// machine (core_game/AppLifecycle.h). The poll loop below translates the
		// SDL_EVENT_* lifecycle events into it; applyLifecycle performs the
		// returned actions against the live subsystems, and the loop reads the
		// sim/render gates back (isSimPaused / isRenderingStopped). Desktop
		// windows minimizing is NOT a background - SDL only raises these events
		// on mobile - so desktop behavior is unchanged.
		Orkige::AppLifecycle lifecycle;
		auto applyLifecycle = [&](Orkige::AppLifecycle::Event event)
		{
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
		};
		// ORKIGE_LIFECYCLE_SELFCHECK phased state (the block lives at the loop
		// bottom): Init -> Backgrounded -> Foregrounded -> Done
		enum class LifecyclePhase { Init, Backgrounded, Foregrounded, Done };
		LifecyclePhase lifecyclePhase = LifecyclePhase::Init;
		bool lifecycleFailed = false;
		bool perfCheckFailed = false;
		if (perfCheck)
		{
			// the check must also hold on a Release tree, where the scope
			// machinery boots disarmed - arm it like MSG_PROFILE would
			Orkige::ProfileManager::setEnabled(true);
		}

		bool running = true;
		unsigned long frameCount = 0;
		// breadcrumbs: record each ScriptComponent failure once (a running game
		// may keep ticking with a failed script; the trail wants it, not a
		// per-frame repeat). Also drain the engine log's warnings/errors below.
		// fade selfcheck state (see the ORKIGE_FADE_SELFCHECK block below)
		bool fadeStarted = false;
		bool fadeSwitched = false;
		bool fadeSawClearAfterSwitch = false;
		float fadeMaxAlpha = 0.0f;
		float fadeAlphaAtSwitch = -1.0f;
		std::unordered_set<std::string> breadcrumbedScriptErrors;
		auto recordScriptErrorBreadcrumbs = [&]()
		{
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
		};
		std::chrono::steady_clock::time_point lastFrameTime =
			std::chrono::steady_clock::now();
		while (running)
		{
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

			// --- jumper-lua selfcheck script (see the block above the loop) --
			if (jumperLuaCheck && !jumperCheckFailed &&
				jumperPhase == JumperCheckPhase::Script)
			{
				if (frameCount == 5)
				{
					optr<Orkige::GameObject> player =
						gameObjectManager.getGameObject("Player").lock();
					Orkige::ScriptComponent* script = (player &&
						player->hasComponent<Orkige::ScriptComponent>()) ?
						player->getComponentPtr<Orkige::ScriptComponent>() :
						nullptr;
					if (!script)
					{
						jumperFail("no Player object with a ScriptComponent "
							"in the scene");
					}
					else if (script->hasScriptError())
					{
						jumperFail("script error: " + script->getScriptError());
					}
					else if (!script->isScriptStarted())
					{
						jumperFail("script never loaded/started");
					}
					else if (jumperStat("respawns", -1.0) < 0.0)
					{
						jumperFail("script did not publish shared.jumper");
					}
					// the UI game.lua booted from Lua: every widget of the
					// three screens exists, the game sits on the visible
					// title screen and the HUD layer is still hidden
					// (UI-gated: skipped on the next flavor - HUD-less)
					else if (uiChecksEnabled && (!jumperWidgetExists("title.name") ||
						!jumperWidgetExists("title.prompt") ||
						!jumperWidgetExists("title.start") ||
						!jumperWidgetExists("hud.progress") ||
						!jumperWidgetExists("hud.wins") ||
						!jumperWidgetExists("hud.hint") ||
						!jumperWidgetExists("win.banner") ||
						!jumperWidgetExists("win.again")))
					{
						jumperFail("the Lua-booted gui widgets are "
							"missing");
					}
					else if (jumperGameState() != "title")
					{
						jumperFail("game did not start on the title screen "
							"(state '" + jumperGameState() + "')");
					}
					else if (uiChecksEnabled && (!jumperWidgetVisible("title.name") ||
						jumperWidgetVisible("hud.progress") ||
						jumperWidgetVisible("win.banner")))
					{
						jumperFail("title-screen layer visibility is wrong");
					}
					else
					{
						SDL_Log("orkige_player: jumper-lua selfcheck - "
							"'%s' loaded, %s, title state reached",
							script->getScriptFile().c_str(),
							uiChecksEnabled ? "UI up + title screen showing"
								: "HUD-less flavor (UI checks skipped)");
					}
				}
				// UI batching property (the gui perf contract, the
				// UiRenderer design rule): the WHOLE HUD - every widget of
				// the title/hud/win groups - is ONE draw batch (one screen
				// = one atlas = one DrawLayer2D batch). Hiding all views
				// for a frame must drop the batch count by exactly the
				// SCREEN count (1 here), never by the widget count (8+).
				if (uiChecksEnabled && frameCount == 6)
				{
					jumperBatchesWithUi = render->getFrameStats().batchCount;
					Orkige::GuiManager::getSingleton().hideAllViews();
				}
				if (uiChecksEnabled && frameCount == 8)
				{
					const size_t batchesWithoutUi =
						render->getFrameStats().batchCount;
					Orkige::GuiManager::getSingleton().showAllViews();
					if (jumperBatchesWithUi != batchesWithoutUi + 1)
					{
						jumperFail("UI batch property broken: the whole HUD "
							"must cost exactly ONE draw batch (with UI " +
							std::to_string(jumperBatchesWithUi) +
							" batches, without " +
							std::to_string(batchesWithoutUi) + ")");
					}
					else
					{
						SDL_Log("orkige_player: jumper-lua selfcheck - the "
							"whole HUD costs one draw batch (%zu -> %zu "
							"with all views hidden)",
							jumperBatchesWithUi, batchesWithoutUi);
					}
				}
				// starting the game: with UI, a synthetic mouse click on the
				// START button (center published by game.lua): move -> hover,
				// press -> down, release -> hit. HUD-less (next flavor),
				// game.lua only listens for ENTER - push that instead (the
				// click path stays classic-verified).
				if (frameCount == 10)
				{
					if (uiChecksEnabled)
					{
						pushMouseMove(
							static_cast<float>(jumperGameStat("startButtonX")),
							static_cast<float>(jumperGameStat("startButtonY")));
					}
					else
					{
						pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, true);
					}
				}
				if (frameCount == 12)
				{
					if (uiChecksEnabled)
					{
						pushMouseButton(
							static_cast<float>(jumperGameStat("startButtonX")),
							static_cast<float>(jumperGameStat("startButtonY")),
							true);
					}
					else
					{
						pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
					}
				}
				if (frameCount == 14 && uiChecksEnabled)
				{
					pushMouseButton(
						static_cast<float>(jumperGameStat("startButtonX")),
						static_cast<float>(jumperGameStat("startButtonY")),
						false);
				}
				if (frameCount == 20)
				{
					if (jumperGameState() != "playing")
					{
						jumperFail("starting the game did not switch to "
							"playing (state '" + jumperGameState() + "')");
					}
					else if (uiChecksEnabled && (jumperWidgetVisible("title.name") ||
						!jumperWidgetVisible("hud.progress")))
					{
						jumperFail("start click did not swap title for HUD");
					}
					else
					{
						SDL_Log("orkige_player: jumper-lua selfcheck - game "
							"started (%s)", uiChecksEnabled
								? "START button clicked via synthetic mouse "
								"events, HUD up"
								: "ENTER on the HUD-less flavor");
					}
				}
				if (frameCount == 30)
				{
					jumperStartX = jumperPlayerTransform()->getPosition().x;
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
				}
				if (frameCount == 50)
				{
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
				}
				if (frameCount == 65)
				{
					jumperMovedX = jumperPlayerTransform()->getPosition().x -
						jumperStartX;
					SDL_Log("orkige_player: jumper-lua selfcheck - moved "
						"+x %.3f m after 20 frames of RIGHT (HUD progress "
						"%.0f%%)", jumperMovedX, jumperHudProgress());
					if (jumperMovedX < 0.6f)
					{
						jumperFail("player did not move right under "
							"scripted input");
					}
					else if (uiChecksEnabled && jumperHudProgress() <= 0.0f)
					{
						jumperFail("HUD progress bar did not advance with x");
					}
				}
				if (frameCount == 80)
				{
					if (!jumperGrounded())
					{
						jumperFail("script does not report grounded before "
							"the jump");
					}
					jumperJumpStartY = jumperPlayerTransform()
						->getPosition().y;
					jumperJumpFrame = frameCount;
					jumperMaxRise = 0.0f;
					pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, true);
					jumperPhase = JumperCheckPhase::WaitLanding;
					jumperPhaseDeadline = frameCount + 120;
				}
			}
			else if (jumperLuaCheck && !jumperCheckFailed &&
				jumperPhase == JumperCheckPhase::WaitLanding)
			{
				jumperMaxRise = std::max(jumperMaxRise,
					jumperPlayerTransform()->getPosition().y -
					jumperJumpStartY);
				if (frameCount == jumperJumpFrame + 5)
				{
					pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, false);
				}
				// give take-off a few frames before accepting "grounded"
				if (frameCount > jumperJumpFrame + 10 && jumperGrounded())
				{
					SDL_Log("orkige_player: jumper-lua selfcheck - jump rise "
						"%.3f m, landed %lu frames after take-off",
						jumperMaxRise, frameCount - jumperJumpFrame);
					if (jumperMaxRise < 0.8f)
					{
						jumperFail("scripted jump did not raise the player");
					}
					else
					{
						// drop into the first gap: the script's kill plane
						// must respawn the player at the start
						jumperBaseRespawns = jumperStat("respawns", 0.0);
						jumperTeleport(Orkige::Vec3(2.75f, 1.5f, 0.0f));
						jumperPhase = JumperCheckPhase::WaitRespawn;
						jumperPhaseDeadline = frameCount + 300;
					}
				}
				else if (frameCount >= jumperPhaseDeadline)
				{
					jumperFail("player never landed after the scripted jump");
				}
			}
			else if (jumperLuaCheck && !jumperCheckFailed &&
				jumperPhase == JumperCheckPhase::WaitRespawn)
			{
				if (jumperStat("respawns", 0.0) > jumperBaseRespawns)
				{
					const float spawnDistance = jumperPlayerTransform()
						->getPosition().distance(
							Orkige::Vec3(0.0f, 1.0f, 0.0f));
					SDL_Log("orkige_player: jumper-lua selfcheck - kill "
						"plane respawned the player %.3f m from the spawn",
						spawnDistance);
					if (spawnDistance > 1.0f)
					{
						jumperFail("respawn did not return to the start");
					}
					else
					{
						// walk-in test of the win path: drop just before the
						// buddy on the goal platform
						jumperBaseWins = jumperStat("wins", 0.0);
						jumperTeleport(Orkige::Vec3(36.0f, 3.0f, 0.0f));
						jumperPhase = JumperCheckPhase::WaitWin;
						jumperPhaseDeadline = frameCount + 300;
					}
				}
				else if (frameCount >= jumperPhaseDeadline)
				{
					jumperFail("falling off never triggered the script's "
						"respawn");
				}
			}
			else if (jumperLuaCheck && !jumperCheckFailed &&
				jumperPhase == JumperCheckPhase::WaitWin)
			{
				if (jumperStat("wins", 0.0) > jumperBaseWins)
				{
					SDL_Log("orkige_player: jumper-lua selfcheck - the goal "
						"fired the script's win (wins=%.0f) - waiting for "
						"the win screen", jumperStat("wins", -1.0));
					jumperPhase = JumperCheckPhase::WaitWinUi;
					jumperPhaseDeadline = frameCount + 120;
				}
				else if (frameCount >= jumperPhaseDeadline)
				{
					jumperFail("reaching the goal never triggered the "
						"script's win");
				}
			}
			else if (jumperLuaCheck && !jumperCheckFailed &&
				jumperPhase == JumperCheckPhase::WaitWinUi)
			{
				// the win overlay: game.lua switched to "win" and showed the
				// banner layer - then ENTER restarts the game (banner
				// visibility is UI-gated; the state machine runs HUD-less too)
				if (jumperGameState() == "win" &&
					(!uiChecksEnabled || jumperWidgetVisible("win.banner")))
				{
					SDL_Log("orkige_player: jumper-lua selfcheck - win "
						"screen up, pressing ENTER to restart");
					pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, true);
					jumperEnterFrame = frameCount;
					jumperPhase = JumperCheckPhase::WaitRestart;
					jumperPhaseDeadline = frameCount + 120;
				}
				else if (frameCount >= jumperPhaseDeadline)
				{
					jumperFail("the win never showed the win screen (state '" +
						jumperGameState() + "')");
				}
			}
			else if (jumperLuaCheck && !jumperCheckFailed &&
				jumperPhase == JumperCheckPhase::WaitRestart)
			{
				if (frameCount == jumperEnterFrame + 5)
				{
					pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
				}
				if (jumperGameState() == "playing" &&
					(!uiChecksEnabled || !jumperWidgetVisible("win.banner")))
				{
					// camera-roll regression: the Lua follow camera lookAt's
					// the camera rig node every frame of this session - the
					// rig's fixed yaw axis must have kept it roll-free (the
					// old shortest-arc lookAt slowly tilted the horizon)
					const float cameraRollDegrees = std::abs(
						cameraNode->getOrientation()
							.getRoll().valueDegrees());
					SDL_Log("orkige_player: jumper-lua selfcheck - camera "
						"roll %.4f deg after the play session",
						cameraRollDegrees);
					if (cameraRollDegrees > 0.1f)
					{
						jumperFail("scripted follow camera accumulated roll");
					}
					else
					{
						SDL_Log("orkige_player: jumper-lua selfcheck complete "
							"- %s, movement (+%.2f m)%s, jump (+%.2f m), "
							"kill-plane respawn, the goal, the win flow, "
							"the ENTER restart and the roll-free camera all "
							"verified (respawns=%.0f wins=%.0f)",
							uiChecksEnabled ? "script+UI boot, START click"
								: "script boot (HUD-less flavor)",
							jumperMovedX,
							uiChecksEnabled ? " with HUD progress" : "",
							jumperMaxRise,
							jumperStat("respawns", -1.0),
							jumperStat("wins", -1.0));
						jumperPhase = JumperCheckPhase::Done;
						running = false;
					}
				}
				else if (frameCount >= jumperPhaseDeadline)
				{
					jumperFail("ENTER on the win screen did not restart the "
						"game (state '" + jumperGameState() + "')");
				}
			}
			if (jumperLuaCheck && jumperCheckFailed)
			{
				exitCode = 1;
				running = false;
			}

			// --- roller selfcheck script (see the block above the loop) -----
			if (rollerCheck && !rollerCheckFailed &&
				rollerPhase == RollerCheckPhase::Boot)
			{
				if (frameCount == 5)
				{
					// boot: both scripts up, state published, HUD widgets
					// exist, mode "play" with a running simulation
					optr<Orkige::GameObject> ball =
						gameObjectManager.getGameObject("Ball").lock();
					// the Ball carries TWO script component kinds ("ball" +
					// "ball_spin"); reach them by name-agnostic collect -
					// getComponentPtr<ScriptComponent>() would find NEITHER (they
					// are keyed by script name, not the class)
					Orkige::ScriptComponent* ballScript = nullptr;
					Orkige::ScriptComponent* spinScript = nullptr;
					if (ball)
					{
						for (Orkige::ScriptComponent* s :
							Orkige::ScriptComponent::collectFrom(*ball))
						{
							if (s->getComponentName() == "ball")
							{
								ballScript = s;
							}
							else if (s->getComponentName() == "ball_spin")
							{
								spinScript = s;
							}
						}
					}
					if (!ballScript || !spinScript)
					{
						rollerFail("the Ball must carry BOTH the 'ball' and "
							"'ball_spin' script component kinds");
					}
					else if (ballScript->hasScriptError())
					{
						rollerFail("ball script error: " +
							ballScript->getScriptError());
					}
					else if (spinScript->hasScriptError())
					{
						rollerFail("ball_spin script error: " +
							spinScript->getScriptError());
					}
					else if (!rollerFlag("gameReady") ||
						!rollerFlag("ballReady"))
					{
						rollerFail("scripts did not publish shared.roller");
					}
					else if (!rollerFlag("spinReady"))
					{
						rollerFail("the second script kind (ball_spin) on the "
							"Ball did not start");
					}
					else if (rollerStat("blinkRate", -1.0) != 2.0)
					{
						rollerFail("the ball_spin blinkRate scene override (2.0) "
							"did not reach self.blinkRate (declared-property "
							"override on a script kind)");
					}
					else if (!ball->hasComponent<Orkige::SpriteComponent>() ||
						!ball->getComponentPtr<Orkige::SpriteComponent>()
							->hasSprite())
					{
						rollerFail("the Ball has no loaded SpriteComponent");
					}
					else if (render->getWindowCamera()->getProjectionType() !=
						Orkige::RenderCamera::PT_ORTHOGRAPHIC)
					{
						rollerFail("ball.lua did not switch the camera to "
							"orthographic projection");
					}
					// UI-gated: skipped on the HUD-less next flavor
					else if (uiChecksEnabled && (!rollerWidgetExists("hud.mode") ||
						!rollerWidgetExists("hud.wins") ||
						!rollerWidgetExists("hud.hint") ||
						!rollerWidgetExists("warn.label") ||
						!rollerWidgetExists("win.banner")))
					{
						rollerFail("the Lua-booted HUD widgets are missing");
					}
					else if (rollerMode() != "play" ||
						physicsWorld.isPaused() || rollerCursorVisible())
					{
						rollerFail("did not boot into running play mode");
					}
					else
					{
						SDL_Log("orkige_player: roller selfcheck - scripts "
							"up, ortho camera, sprites loaded%s",
							uiChecksEnabled ? ", HUD showing"
								: " (HUD-less flavor, UI checks skipped)");
						rollerPhase = RollerCheckPhase::ActiveGate;
						rollerStepFrame = 0;
					}
				}
			}
			// active-state gate: deactivating the WHOLE TileC subtree (one
			// setActive on the parent) must stop its rendering (triangle
			// count) and take its wall bodies out of the simulation;
			// deactivating the Ball must gate its script ticks -
			// reactivation restores everything (frame-scripted)
			else if (rollerCheck && !rollerCheckFailed &&
				rollerPhase == RollerCheckPhase::ActiveGate)
			{
				if (rollerStepFrame == 0)
				{
					rollerStepFrame = frameCount;
				}
				const unsigned long step = frameCount - rollerStepFrame;
				// TileC sits at slot 2 (-3, 3): its bottom wall's world
				// pose is x=-3, y=0.25 (same probe math as the slide)
				if (step == 5)
				{
					optr<Orkige::GameObject> tileC =
						gameObjectManager.getGameObject("TileC").lock();
					optr<Orkige::GameObject> ball =
						gameObjectManager.getGameObject("Ball").lock();
					if (!tileC || !ball)
					{
						rollerFail("no TileC/Ball objects in the scene");
					}
					else if (gameObjectManager.getChildren("TileC").empty())
					{
						rollerFail("TileC has no children - the scene "
							"lost its tile groups");
					}
					else if (!rollerProbeHit(-3.0f, 0.25f))
					{
						rollerFail("TileC's bottom wall body is missing "
							"before the active-state gate");
					}
					else
					{
						rollerTrianglesActive =
							render->getFrameStats().triangleCount;
						tileC->setActive(false);
						ball->setActive(false);
					}
				}
				if (step == 10)
				{
					// sampled AFTER the deactivation settled: any later
					// growth means the gated script still ticks
					rollerBallUpdatesGated =
						rollerStat("ballUpdates", -1.0);
				}
				if (step == 25)
				{
					optr<Orkige::GameObject> wall = gameObjectManager
						.getGameObject("TileC/WallBottom").lock();
					if (rollerTrianglesActive == 0 ||
						render->getFrameStats().triangleCount >=
							rollerTrianglesActive)
					{
						rollerFail("deactivating the TileC subtree + ball "
							"did not reduce the triangle count (" +
							std::to_string(render->getFrameStats()
								.triangleCount) + " of " +
							std::to_string(rollerTrianglesActive) + ")");
					}
					else if (rollerProbeHit(-3.0f, 0.25f))
					{
						rollerFail("the deactivated tile's wall body "
							"still collides");
					}
					else if (!wall || wall->isActiveInHierarchy() ||
						!wall->isActiveSelf())
					{
						rollerFail("the tile children did not inherit "
							"the parent's inactive state");
					}
					else if (rollerStat("ballUpdates", -1.0) !=
						rollerBallUpdatesGated)
					{
						rollerFail("the deactivated ball's script still "
							"ticks");
					}
					else
					{
						gameObjectManager.getGameObject("TileC").lock()
							->setActive(true);
						gameObjectManager.getGameObject("Ball").lock()
							->setActive(true);
					}
				}
				if (step == 40)
				{
					if (render->getFrameStats().triangleCount <
						rollerTrianglesActive)
					{
						rollerFail("reactivation did not restore the "
							"rendered triangles");
					}
					else if (!rollerProbeHit(-3.0f, 0.25f))
					{
						rollerFail("reactivation did not restore the "
							"tile's wall body");
					}
					else if (rollerStat("ballUpdates", -1.0) <=
						rollerBallUpdatesGated)
					{
						rollerFail("reactivation did not resume the "
							"ball's script ticks");
					}
					else
					{
						SDL_Log("orkige_player: roller selfcheck - "
							"active-state gate OK (subtree hidden, body "
							"out of the sim, script gated; all restored)");
						rollerPhase = RollerCheckPhase::TiltRoll;
						rollerStepFrame = 0;
					}
				}
			}
			// tilt roll: hold LEFT until the (wall-clock paced) simulated
			// tilt built up AND the ball visibly rolled -x
			else if (rollerCheck && !rollerCheckFailed &&
				rollerPhase == RollerCheckPhase::TiltRoll)
			{
				if (rollerStepFrame == 0)
				{
					rollerStepFrame = frameCount;
					rollerStartX = static_cast<float>(rollerStat("x", 0.0));
					pushKeyEvent(SDL_SCANCODE_LEFT, SDLK_LEFT, true);
				}
				rollerMovedX = static_cast<float>(rollerStat("x", 0.0)) -
					rollerStartX;
				const Orkige::Vec3 tilt = inputManager.getTilt();
				if (tilt.x <= -0.35f && rollerMovedX <= -0.4f)
				{
					pushKeyEvent(SDL_SCANCODE_LEFT, SDLK_LEFT, false);
					SDL_Log("orkige_player: roller selfcheck - tilt %.2f/%.2f "
						"rolled the ball %.3f m left (%lu frames of LEFT)",
						tilt.x, tilt.y, rollerMovedX,
						frameCount - rollerStepFrame);
					rollerScreenshot("roller_play.png");
					rollerPhase = RollerCheckPhase::MoveWorld;
					rollerStepFrame = 0;
				}
				else if (frameCount >= rollerStepFrame + 1800)
				{
					rollerFail("holding LEFT never tilted gravity/rolled the "
						"ball (tilt " + std::to_string(tilt.x) + ", moved " +
						std::to_string(rollerMovedX) + ")");
				}
			}
			// move-world mode: TAB pauses, probes verify the tile slide
			// moves sprite AND body, TAB resumes (frame-scripted relative
			// to the phase anchor - no tilt dependency in here)
			else if (rollerCheck && !rollerCheckFailed &&
				rollerPhase == RollerCheckPhase::MoveWorld)
			{
				if (rollerStepFrame == 0)
				{
					rollerStepFrame = frameCount;
				}
				const unsigned long step = frameCount - rollerStepFrame;
				// TAB: move-world mode - physics pauses, the cursor shows
				if (step == 5)
				{
					pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true);
				}
				if (step == 7)
				{
					pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false);
				}
				if (step == 15)
				{
					Orkige::TransformComponent* tileB =
						rollerTransform("TileB/Frame");
					if (rollerMode() != "move" || !physicsWorld.isPaused())
					{
						rollerFail("TAB did not pause into move-world mode");
					}
					else if (!rollerCursorVisible())
					{
						rollerFail("move mode did not show the slot cursor");
					}
					else if (!tileB)
					{
						rollerFail("no TileB/Frame object in the scene");
					}
					else if (rollerProbeHit(3.0f, -5.75f))
					{
						rollerFail("the empty slot already has collision "
							"geometry before the slide");
					}
					else if (!rollerProbeHit(3.0f, 0.25f))
					{
						rollerFail("tile B's bottom wall body is missing at "
							"its starting location");
					}
					else
					{
						// the frame sprite is a CHILD of the TileB group:
						// track its WORLD pose (what the player sees) and
						// its LOCAL pose (which must NOT change - the slide
						// moves the PARENT)
						rollerTileBStartY = tileB->getWorldPosition().y;
						rollerTileBFrameLocalY = tileB->getPosition().y;
						Orkige::TransformComponent* goal =
							rollerTransform("Goal");
						rollerGoalStartY = goal ?
							goal->getWorldPosition().y : 0.0f;
						SDL_Log("orkige_player: roller selfcheck - move "
							"mode up (paused, cursor on), tile B at y=%.2f",
							rollerTileBStartY);
						rollerScreenshot("roller_move_mode.png");
					}
				}
				// DOWN: slide tile B (above the empty slot) down into it
				if (step == 20)
				{
					pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, true);
				}
				if (step == 22)
				{
					pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, false);
				}
				if (step == 32)
				{
					Orkige::TransformComponent* tileBGroup =
						rollerTransform("TileB");
					Orkige::TransformComponent* tileB =
						rollerTransform("TileB/Frame");
					Orkige::TransformComponent* goal =
						rollerTransform("Goal");
					optr<Orkige::GameObject> frameObject =
						gameObjectManager.getGameObject("TileB/Frame").lock();
					const float tileBMovedY = tileB ?
						tileB->getWorldPosition().y - rollerTileBStartY : 0.0f;
					if (rollerStat("slides", 0.0) < 1.0)
					{
						rollerFail("DOWN in move mode did not slide a tile");
					}
					else if (std::abs(tileBMovedY + 6.0f) > 0.01f)
					{
						rollerFail("tile B's frame sprite did not move one "
							"slot down (world)");
					}
					// the slide must have happened VIA THE PARENT: the group
					// object moved one slot, the child's LOCAL pose is
					// untouched
					else if (!frameObject ||
						frameObject->getParentId() != "TileB")
					{
						rollerFail("TileB/Frame is not a child of the TileB "
							"group");
					}
					else if (!tileBGroup || std::abs(tileBGroup
						->getWorldPosition().y + 3.0f) > 0.01f)
					{
						rollerFail("the TileB group parent did not move to "
							"the empty slot");
					}
					else if (!tileB || std::abs(tileB->getPosition().y -
						rollerTileBFrameLocalY) > 0.001f)
					{
						rollerFail("the slide changed the child's LOCAL "
							"transform - it did not move via the parent");
					}
					else if (!goal || std::abs(goal->getWorldPosition().y -
						(rollerGoalStartY - 6.0f)) > 0.01f)
					{
						rollerFail("the goal star did not ride its tile");
					}
					else if (!rollerProbeHit(3.0f, -5.75f))
					{
						rollerFail("tile B's wall BODY does not collide at "
							"the new location (sprite moved, body did not)");
					}
					else if (rollerProbeHit(3.0f, 0.25f))
					{
						rollerFail("tile B's wall body still collides at "
							"the OLD location");
					}
					else
					{
						SDL_Log("orkige_player: roller selfcheck - tile B "
							"slid %.2f in y VIA ITS PARENT while paused; "
							"wall body probes old=free new=hit, goal rode "
							"along", tileBMovedY);
					}
				}
				// TAB: back to play; then roll right into the goal tile
				if (step == 35)
				{
					pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true);
				}
				if (step == 37)
				{
					pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false);
				}
				if (step == 45)
				{
					if (rollerMode() != "play" || physicsWorld.isPaused() ||
						rollerCursorVisible())
					{
						rollerFail("TAB did not resume play mode");
					}
				}
				if (step == 50)
				{
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
					rollerRollFrame = frameCount;
					rollerPhase = RollerCheckPhase::WaitWin;
					// fat deadline: the tilt swing is wall-clock paced while
					// headless sim frames run faster than real time
					rollerPhaseDeadline = frameCount + 3600;
				}
			}
			else if (rollerCheck && !rollerCheckFailed &&
				rollerPhase == RollerCheckPhase::WaitWin)
			{
				if (rollerStat("wins", 0.0) >= 1.0)
				{
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
					SDL_Log("orkige_player: roller selfcheck - rolled into "
						"the goal %lu frames after tilting right (wins=%.0f "
						"respawns=%.0f)", frameCount - rollerRollFrame,
						rollerStat("wins", -1.0), rollerStat("respawns", -1.0));
					rollerPhase = RollerCheckPhase::WaitWinUi;
					rollerPhaseDeadline = frameCount + 90;
				}
				else if (frameCount >= rollerPhaseDeadline)
				{
					rollerFail("rolling right never reached the goal in the "
						"slid tile");
				}
			}
			else if (rollerCheck && !rollerCheckFailed &&
				rollerPhase == RollerCheckPhase::WaitWinUi)
			{
				if (!uiChecksEnabled || rollerWidgetVisible("win.banner"))
				{
					SDL_Log("orkige_player: roller selfcheck complete - "
						"tilt roll (%.2f m), move mode (pause+cursor), tile "
						"slide (sprite+body+goal), resume and the win path "
						"all verified%s", rollerMovedX, uiChecksEnabled
							? "" : " (win banner check skipped - HUD-less)");
					rollerPhase = RollerCheckPhase::Done;
					running = false;
				}
				else if (frameCount >= rollerPhaseDeadline)
				{
					rollerFail("the win never showed the win banner");
				}
			}
			if (rollerCheck && rollerCheckFailed)
			{
				exitCode = 1;
				running = false;
			}

			// --- softbody selfcheck (see the block above the loop) ----------
			if (softbodyCheck && !softbodyCheckFailed)
			{
				Orkige::VectorShapeComponent* fall = softbodyShape("FallBlob");
				Orkige::VectorShapeComponent* morph = softbodyShape("MorphBlob");
				// the Lua-driven morph runs from boot; track its deformation
				// throughout so the Morph phase can confirm it
				if (morph)
				{
					softbodyMorphPeak = std::max(softbodyMorphPeak,
						morph->getDeformDisplacement());
				}
				if (softbodyPhase == SoftBodyPhase::Fall)
				{
					if (frameCount == 5)
					{
						if (!fall)
						{
							softbodyFail("no FallBlob with a "
								"VectorShapeComponent");
						}
						else if (!fall->isSoftBodyEnabled())
						{
							softbodyFail("FallBlob is not a soft body");
						}
						else if (fall->getControlPointCount() == 0)
						{
							softbodyFail("FallBlob deformer built no control "
								"points");
						}
						else
						{
							softbodyPhaseDeadline = frameCount + 1500;
						}
					}
					else if (frameCount > 5 && fall)
					{
						const float squash = std::fabs(fall->getSquash());
						const float wobble = fall->getDeformDisplacement();
						softbodyPeakSquash =
							std::max(softbodyPeakSquash, squash);
						softbodyPeakWobble =
							std::max(softbodyPeakWobble, wobble);
						if (squash > 0.02f)
						{
							softbodySawSquash = true;
						}
						if (wobble > 0.01f)
						{
							softbodySawWobble = true;
						}
						if (softbodySawSquash && softbodySawWobble)
						{
							SDL_Log("orkige_player: softbody selfcheck - "
								"FallBlob landed and deformed (squash %.3f, "
								"wobble %.4f)", softbodyPeakSquash,
								softbodyPeakWobble);
							softbodyPhase = SoftBodyPhase::Settle;
							softbodyPhaseDeadline = frameCount + 1500;
						}
						else if (frameCount >= softbodyPhaseDeadline)
						{
							softbodyFail("the falling blob never "
								"squashed+wobbled on landing");
						}
					}
				}
				else if (softbodyPhase == SoftBodyPhase::Settle)
				{
					if (fall && !fall->isDeforming() &&
						fall->getDeformDisplacement() < 1.0e-3f)
					{
						SDL_Log("orkige_player: softbody selfcheck - FallBlob "
							"wobble decayed back to the rest pose");
						softbodyPhase = SoftBodyPhase::Morph;
						softbodyPhaseDeadline = frameCount + 900;
					}
					else if (frameCount >= softbodyPhaseDeadline)
					{
						softbodyFail("the blob's wobble never decayed back to "
							"rest");
					}
				}
				else if (softbodyPhase == SoftBodyPhase::Morph)
				{
					if (morph && morph->getMorphTargetCount() > 0 &&
						softbodyMorphPeak > 0.01f)
					{
						SDL_Log("orkige_player: softbody selfcheck complete - "
							"physics squash+wobble, exact return to rest, and "
							"the Lua-driven morph (peak %.4f, %zu target(s)) all "
							"verified", softbodyMorphPeak,
							morph->getMorphTargetCount());
						softbodyPhase = SoftBodyPhase::Done;
						running = false;
					}
					else if (frameCount >= softbodyPhaseDeadline)
					{
						softbodyFail("the Lua-driven morph never deformed the "
							"MorphBlob");
					}
				}
			}
			if (softbodyCheck && softbodyCheckFailed)
			{
				exitCode = 1;
				running = false;
			}

			// --- roller PROGRESSION selfcheck (see the block above the loop) --
			if (rollerProgressionCheck && !rollerProgCheckFailed &&
				rollerProgPhase == RollerProgPhase::Boot)
			{
				if (frameCount == 5)
				{
					if (!rollerFlag("gameReady") || !rollerFlag("ballReady"))
					{
						rollerProgFail("scripts did not publish shared.roller");
					}
					else if (levelManager.count() < 2)
					{
						rollerProgFail("the level sequence did not load (need "
							">= 2 levels, got " +
							std::to_string(levelManager.count()) + ")");
					}
					else if (static_cast<int>(rollerStat("levelIndex", -1.0)) != 0)
					{
						rollerProgFail("did not boot at level 1 (index 0)");
					}
					else
					{
						SDL_Log("orkige_player: roller progression selfcheck - "
							"booted at level 1 of %d, solving...",
							levelManager.count());
						rollerProgPhase = RollerProgPhase::L1Slide;
						rollerProgStepFrame = 0;
					}
				}
			}
			// solve level 1: TAB into move mode, DOWN slides tile B into the
			// empty slot, TAB back to play (frame-scripted from the anchor)
			else if (rollerProgressionCheck && !rollerProgCheckFailed &&
				rollerProgPhase == RollerProgPhase::L1Slide)
			{
				if (rollerProgStepFrame == 0)
				{
					rollerProgStepFrame = frameCount;
				}
				const unsigned long step = frameCount - rollerProgStepFrame;
				if (step == 5) { pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true); }
				if (step == 7) { pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false); }
				if (step == 15)
				{
					if (rollerMode() != "move")
					{
						rollerProgFail("TAB did not enter move-world mode");
					}
					else
					{
						pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, true);
					}
				}
				if (step == 17) { pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, false); }
				if (step == 27)
				{
					if (rollerStat("slides", 0.0) < 1.0)
					{
						rollerProgFail("DOWN in move mode did not slide a tile");
					}
					else
					{
						pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true);
					}
				}
				if (step == 29) { pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false); }
				if (step == 35)
				{
					if (rollerMode() != "play")
					{
						rollerProgFail("TAB did not resume play mode");
					}
					else
					{
						pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
						rollerProgPhase = RollerProgPhase::L1Roll;
						rollerProgDeadline = frameCount + 3600;
					}
				}
			}
			// hold RIGHT until the ball rolls onto the goal (level 1's win)
			else if (rollerProgressionCheck && !rollerProgCheckFailed &&
				rollerProgPhase == RollerProgPhase::L1Roll)
			{
				if (rollerStat("wins", 0.0) >= 1.0)
				{
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
					SDL_Log("orkige_player: roller progression selfcheck - "
						"level 1 solved (slides=%.0f), waiting for the deferred "
						"switch to level 2", rollerStat("slides", -1.0));
					rollerProgPhase = RollerProgPhase::WaitSwitch;
					// the complete banner lingers ADVANCE_SECONDS before the
					// deferred load, then the level-2 scripts init - fat deadline
					rollerProgDeadline = frameCount + 1800;
				}
				else if (frameCount >= rollerProgDeadline)
				{
					rollerProgFail("holding RIGHT never reached the goal on "
						"level 1");
				}
			}
			// wait for the deferred scene switch to reach level 2
			else if (rollerProgressionCheck && !rollerProgCheckFailed &&
				rollerProgPhase == RollerProgPhase::WaitSwitch)
			{
				if (static_cast<int>(rollerStat("levelIndex", -1.0)) == 1)
				{
					std::error_code ignored;
					// progression rides the shared SaveStore now; saveProgress()
					// flushed it to that file on level complete
					const bool saveWritten = !saveStore.getSaveFile().empty() &&
						std::filesystem::exists(saveStore.getSaveFile(), ignored);
					if (!saveWritten)
					{
						rollerProgFail("the progression save file was not "
							"written on level complete ('" +
							saveStore.getSaveFile() + "')");
					}
					else if (levelManager.resumeLevel() != 1)
					{
						rollerProgFail("the resume level did not persist to 1 "
							"(got " + std::to_string(levelManager.resumeLevel()) +
							")");
					}
					else if (levelManager.bestMoves(0) < 0)
					{
						rollerProgFail("level 1's best moves were not recorded");
					}
					else if (!rollerFlag("gameReady") || !rollerFlag("ballReady"))
					{
						rollerProgFail("the level-2 scripts did not re-init "
							"after the switch");
					}
					else
					{
						SDL_Log("orkige_player: roller progression selfcheck - "
							"SWITCHED to level 2 (save written, resume=1, "
							"level 1 best=%d slides); solving level 2...",
							levelManager.bestMoves(0));
						pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
						rollerProgPhase = RollerProgPhase::L2Roll;
						rollerProgDeadline = frameCount + 3600;
					}
				}
				else if (frameCount >= rollerProgDeadline)
				{
					rollerProgFail("the deferred load never switched to level 2 "
						"(levelIndex still " +
						std::to_string(static_cast<int>(
							rollerStat("levelIndex", -1.0))) + ")");
				}
			}
			// level 2 is the straight shot: hold RIGHT until the win
			else if (rollerProgressionCheck && !rollerProgCheckFailed &&
				rollerProgPhase == RollerProgPhase::L2Roll)
			{
				if (rollerStat("wins", 0.0) >= 1.0)
				{
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
					SDL_Log("orkige_player: roller progression selfcheck "
						"complete - the level sequence, the deferred scene "
						"switch, the progression save AND the straight-shot "
						"level 2 win all verified");
					rollerProgPhase = RollerProgPhase::Done;
					running = false;
				}
				else if (frameCount >= rollerProgDeadline)
				{
					rollerProgFail("holding RIGHT never reached the goal on "
						"level 2 (the straight-shot level is unsolvable as "
						"scripted)");
				}
			}
			if (rollerProgressionCheck && rollerProgCheckFailed)
			{
				exitCode = 1;
				running = false;
			}

			// --- tween selfcheck (see the block above the loop) -------------
			if (tweenCheck && !tweenCheckFailed && !tweenCheckDone)
			{
				const std::string scriptVerdict =
					Orkige::ScriptRuntime::getSingleton().getString(
						{"shared", "tween", "failed"}, "");
				if (!scriptVerdict.empty())
				{
					tweenFail("the script reported: " + scriptVerdict);
				}
				else if (tweenFlag("done"))
				{
					// the script says every tween ran its course - now verify
					// the world from the OUTSIDE
					Orkige::TransformComponent* tweener =
						gameObjectManager.getGameObject("Tweener").lock()
						? gameObjectManager.getGameObject("Tweener").lock()
							->getComponentPtr<Orkige::TransformComponent>()
						: nullptr;
					Orkige::SpriteComponent* sprite =
						gameObjectManager.getGameObject("Tweener").lock()
						&& gameObjectManager.getGameObject("Tweener").lock()
							->hasComponent<Orkige::SpriteComponent>()
						? gameObjectManager.getGameObject("Tweener").lock()
							->getComponentPtr<Orkige::SpriteComponent>()
						: nullptr;
					if (tweenStat("updates", 0.0) < 1.0)
					{
						tweenFail("tween.to never called its onUpdate closure");
					}
					else if (tweenStat("completes", 0.0) != 1.0)
					{
						tweenFail("onComplete did not fire exactly once");
					}
					else if (!tweener ||
						std::abs(tweener->getPosition().x - 3.0f) > 0.001f)
					{
						tweenFail("the tween.to closure did not land the "
							"Tweener on x=3 exactly");
					}
					else if (!sprite ||
						std::abs(sprite->getTint().a - 0.25f) > 0.001f)
					{
						tweenFail("tween.fade did not land the sprite tint "
							"alpha on 0.25");
					}
					else if (std::abs(soundManager.getGroupVolume("music") -
						0.25f) > 0.001f)
					{
						tweenFail("tween.volume did not land the music group "
							"volume on 0.25 (the ducking recipe channel)");
					}
					else if (std::abs(soundManager.getMasterVolume() - 0.8f) >
						0.001f)
					{
						tweenFail("the manifest Setting audio.master=0.8 was "
							"not applied on project load");
					}
					else if (std::abs(soundManager.getGroupVolume("hud") -
						0.4f) > 0.001f)
					{
						tweenFail("the manifest Setting audio.group.hud=0.4 "
							"was not applied on project load");
					}
					else if (tweenFlag("soundChecked") &&
						(!soundManager.getSound("beep") ||
							std::abs(soundManager.getSound("beep")
								->getEffectiveGain() - 0.15f) > 0.001f))
					{
						tweenFail("the beep's effective gain is not 0.15 "
							"(base 0.6 x tweened music group 0.25)");
					}
					else
					{
						SDL_Log("orkige_player: tween selfcheck complete - "
							"closure tween.to (x=%.3f, %0.f updates, "
							"onComplete once), fade (alpha=%.3f), volume "
							"(music=%.3f), cancel, delay and the manifest "
							"mixer settings all verified",
							tweener->getPosition().x,
							tweenStat("updates", -1.0),
							sprite->getTint().a,
							soundManager.getGroupVolume("music"));
						tweenCheckDone = true;
						running = false;
					}
				}
				else if (frameCount >= 900)
				{
					// fat deadline: ~2.2s of simulated tween time must have
					// long finished after 900 floored-dt frames (15s sim)
					tweenFail("the script never reported done");
				}
			}
			if (tweenCheck && tweenCheckFailed)
			{
				exitCode = 1;
				running = false;
			}

			// --- hot-reload selfcheck (see the block above the loop) ---------
			if (hotreloadCheck && !hotreloadCheckFailed &&
				hotreloadPhase == HotReloadPhase::Boot && frameCount == 5)
			{
				Orkige::ScriptComponent* script = hotreloadProbeScript();
				Orkige::TransformComponent* transform =
					hotreloadProbeTransform();
				if (!script)
				{
					hotreloadFail("no Probe object with a ScriptComponent");
				}
				else if (script->hasScriptError())
				{
					hotreloadFail("script error: " + script->getScriptError());
				}
				else if (!script->isScriptStarted())
				{
					hotreloadFail("script never loaded/started");
				}
				else if (hotreloadNumber("value", -1.0) != 1.0)
				{
					hotreloadFail("variant A did not publish value == 1");
				}
				else if (!transform)
				{
					hotreloadFail("no Probe TransformComponent");
				}
				else
				{
					// save the committed bytes for restoration on exit
					hotreloadScriptPath = Orkige::ScriptRuntime::getSingleton()
						.resolveScriptPath(script->getScriptFile());
					std::ifstream original(hotreloadScriptPath,
						std::ios::binary);
					hotreloadOriginal.assign(
						std::istreambuf_iterator<char>(original),
						std::istreambuf_iterator<char>());
					if (hotreloadScriptPath.empty() ||
						hotreloadOriginal.empty())
					{
						hotreloadFail("could not read the fixture script from "
							"disk");
					}
					else
					{
						// engine-side state that the swap must NOT reset (the
						// new init does not touch the transform)
						transform->setPosition(Orkige::Vec3(7.0f, 0.0f, 0.0f));
						SDL_Log("orkige_player: hotreload selfcheck - variant A "
							"up (value=1), transform parked at x=7");
					}
				}
			}
			else if (hotreloadCheck && !hotreloadCheckFailed &&
				hotreloadPhase == HotReloadPhase::Boot && frameCount == 10)
			{
				// (1) overwrite with variant B (value = 2), (2) hot-swap
				if (!hotreloadWriteScript(
					"function init(self)\n"
					"\tshared.hotreload = shared.hotreload or {}\n"
					"\tshared.hotreload.value = 2\n"
					"\tshared.hotreload.inits = "
						"(shared.hotreload.inits or 0) + 1\n"
					"end\n"
					"function update(self, dt)\n"
					"\tshared.hotreload.ticks = "
						"(shared.hotreload.ticks or 0) + 1\n"
					"end\n"))
				{
					hotreloadFail("could not write variant B to disk");
				}
				else
				{
					Orkige::ScriptComponent* script = hotreloadProbeScript();
					Orkige::TransformComponent* transform =
						hotreloadProbeTransform();
					script->hotReload();
					if (script->hasReloadError())
					{
						hotreloadFail("a valid variant B failed to reload: " +
							script->getLastReloadError());
					}
					else if (script->hasScriptError())
					{
						hotreloadFail("the swap disabled the instance "
							"(mFailed set)");
					}
					else if (hotreloadNumber("value", -1.0) != 2.0)
					{
						hotreloadFail("the running behavior did not change to "
							"value == 2 after the swap");
					}
					else if (hotreloadNumber("inits", -1.0) != 2.0)
					{
						hotreloadFail("the new init did not run (inits != 2)");
					}
					else if (!transform || std::abs(
						transform->getPosition().x - 7.0f) > 0.001f)
					{
						hotreloadFail("the engine-side transform did NOT "
							"survive the swap (x != 7)");
					}
					else
					{
						SDL_Log("orkige_player: hotreload selfcheck - swap OK "
							"(value 1->2, re-init ran, transform x=7 persisted)");
						hotreloadPhase = HotReloadPhase::AfterSwap;
					}
				}
			}
			else if (hotreloadCheck && !hotreloadCheckFailed &&
				hotreloadPhase == HotReloadPhase::AfterSwap && frameCount == 15)
			{
				// NEGATIVE: overwrite with broken Lua and hot-swap - the old
				// (variant B) instance must keep running, error non-fatal
				hotreloadTicksAtBreak = hotreloadNumber("ticks", -1.0);
				if (!hotreloadWriteScript("this is not valid lua ((\n"))
				{
					hotreloadFail("could not write the broken variant to disk");
				}
				else
				{
					Orkige::ScriptComponent* script = hotreloadProbeScript();
					script->hotReload();
					if (!script->hasReloadError())
					{
						hotreloadFail("a broken script reloaded without an "
							"error");
					}
					else if (script->hasScriptError())
					{
						hotreloadFail("the failed reload wrongly disabled the "
							"instance (mFailed set - it must stay alive)");
					}
					else if (hotreloadNumber("value", -1.0) != 2.0)
					{
						hotreloadFail("the failed reload clobbered the running "
							"instance (value != 2)");
					}
					else
					{
						SDL_Log("orkige_player: hotreload selfcheck - broken "
							"edit contained (reload error surfaced, old instance "
							"kept, mFailed false)");
						hotreloadPhase = HotReloadPhase::AfterBreak;
					}
				}
			}
			else if (hotreloadCheck && !hotreloadCheckFailed &&
				hotreloadPhase == HotReloadPhase::AfterBreak && frameCount == 20)
			{
				// the surviving instance kept ticking across the failed reload
				if (hotreloadNumber("ticks", -1.0) <= hotreloadTicksAtBreak)
				{
					hotreloadFail("the surviving instance stopped updating "
						"after the failed reload");
				}
				else
				{
					SDL_Log("orkige_player: hotreload selfcheck complete - "
						"compile-before-swap (behavior change + engine-side "
						"state persistence) and broken-edit containment both "
						"verified");
					hotreloadPhase = HotReloadPhase::Done;
					running = false;
				}
			}
			if (hotreloadCheck && hotreloadCheckFailed)
			{
				exitCode = 1;
				running = false;
			}

			if (scriptPropCheck && !scriptPropCheckFailed &&
				scriptPropPhase == ScriptPropPhase::Boot && frameCount == 5)
			{
				Orkige::ScriptComponent* script = moverScript();
				if (!script)
				{
					scriptPropFail("no Mover object with a ScriptComponent");
				}
				else if (script->hasScriptError())
				{
					scriptPropFail("script error: " + script->getScriptError());
				}
				else if (!script->isScriptStarted())
				{
					scriptPropFail("the script never loaded/started");
				}
				else if (std::abs(
					scriptPropNumber("injectedSpeed", -1.0) - 5.0) > 0.001)
				{
					// the script's OWN default is 1; seeing 5 proves the
					// serialized per-instance value was loaded AND injected onto
					// self before init
					scriptPropFail("the serialized moveSpeed=5 was NOT injected "
						"before init");
				}
				else if (scriptPropNumber("x", 0.0) <=
					scriptPropNumber("startX", 0.0))
				{
					scriptPropFail("the script did not move the transform");
				}
				else
				{
					SDL_Log("orkige_player: scriptprop selfcheck - export "
						"injected before init (moveSpeed=5), transform moving");
					scriptPropPhase = ScriptPropPhase::Moving;
				}
			}
			else if (scriptPropCheck && !scriptPropCheckFailed &&
				scriptPropPhase == ScriptPropPhase::Moving && frameCount == 15)
			{
				const double x = scriptPropNumber("x", 0.0);
				const double elapsed = scriptPropNumber("elapsed", 0.0);
				Orkige::ScriptComponent* script = moverScript();
				if (elapsed <= 0.0)
				{
					scriptPropFail("no elapsed time accumulated");
				}
				else if (std::abs(x / elapsed - 5.0) > 0.25)
				{
					scriptPropFail("the script did not move at the injected "
						"speed (x/elapsed != 5)");
				}
				else if (!script)
				{
					scriptPropFail("lost the Mover ScriptComponent");
				}
				else
				{
					// flip moveSpeed live through the REFLECTED setter - the
					// exact path a debug-protocol set_property / MCP
					// set_component write takes to a dynamic export property
					const Orkige::PropertySchema schema =
						Orkige::getComponentSchema(*script);
					Orkige::PropertyDesc const* desc = schema.find("moveSpeed");
					if (!desc || desc->isReadOnly())
					{
						scriptPropFail("moveSpeed not writable through the "
							"reflected schema");
					}
					else
					{
						desc->set(script,
							Orkige::PropertyValue::makeFloat(0.0));
						scriptPropXAtFreeze = scriptPropNumber("x", 0.0);
						SDL_Log("orkige_player: scriptprop selfcheck - moved at "
							"speed 5, set moveSpeed->0 through the reflected "
							"setter");
						scriptPropPhase = ScriptPropPhase::Frozen;
					}
				}
			}
			else if (scriptPropCheck && !scriptPropCheckFailed &&
				scriptPropPhase == ScriptPropPhase::Frozen && frameCount == 25)
			{
				if (std::abs(scriptPropNumber("x", -999.0) -
					scriptPropXAtFreeze) > 0.05)
				{
					scriptPropFail("the live moveSpeed->0 did not reach the "
						"script (x kept moving)");
				}
				else
				{
					SDL_Log("orkige_player: scriptprop selfcheck complete - "
						"export injected-before-init, behaved, serialized "
						"per-instance, live reflected set reached self.moveSpeed");
					scriptPropPhase = ScriptPropPhase::Done;
					running = false;
				}
			}
			if (scriptPropCheck && scriptPropCheckFailed)
			{
				exitCode = 1;
				running = false;
			}

			// --- integration: contact + tags + input action (see the block
			// above the loop). Condition-driven with a fat frame ceiling: the
			// physics drop takes a variable number of frames, so every wait is a
			// poll-until, never a fixed frame.
			if (integrationContactCheck && !integContactFailed &&
				integContactPhase != IntegContactPhase::Done)
			{
				if (integContactPhase == IntegContactPhase::VerifyTags)
				{
					if (integContactNum("found", -1.0) >= 0.0 && frameCount >= 3)
					{
						if (integContactNum("found", -1.0) != 1.0)
						{
							integContactFail("world.findByTag(\"goal\") did not "
								"find exactly one goal");
						}
						else if (integContactStr("foundGoal") != "Goal")
						{
							integContactFail("the tag-discovered goal was not "
								"the 'Goal' object");
						}
						else
						{
							// fire the named "jump" action: hold SPACE so the
							// action layer samples a down edge (released below)
							pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, true);
							integContactInputFrame = frameCount;
							integContactPhase = IntegContactPhase::DriveInput;
							SDL_Log("orkige_player: integration contact "
								"selfcheck - goal found by tag; injecting "
								"the 'jump' action");
						}
					}
					else if (frameCount > 600)
					{
						integContactFail("the ball script never published its "
							"tag discovery");
					}
				}
				else if (integContactPhase == IntegContactPhase::DriveInput)
				{
					if (frameCount == integContactInputFrame + 5)
					{
						pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, false);
					}
					if (integContactNum("input", 0.0) >= 1.0)
					{
						integContactPhase = IntegContactPhase::AwaitContact;
					}
					else if (frameCount > integContactInputFrame + 300)
					{
						integContactFail("the injected 'jump' action never "
							"reached the ball script");
					}
				}
				else if (integContactPhase == IntegContactPhase::AwaitContact)
				{
					if (integContactNum("contact", 0.0) >= 1.0)
					{
						if (integContactStr("contactOther") != "Goal")
						{
							integContactFail("onContactBegin fired for a body "
								"other than the tagged goal");
						}
						// the bespoke onContactBegin fired THIS or an earlier
						// frame; the physics.contactBegin BUS mirror is emitted at
						// the contact drain (after the script phase), so it is
						// delivered one frame later. Wait for it, then assert the
						// mirror count matches the bespoke count (additive, never
						// a replacement).
						else if (integContactNum("busContact", 0.0) >= 1.0)
						{
							if (integContactNum("busContact", 0.0) !=
								integContactNum("contact", 0.0))
							{
								integContactFail("the physics.contactBegin bus "
									"mirror count did not match the onContactBegin "
									"hook count");
							}
							else
							{
								SDL_Log("orkige_player: integration contact "
									"selfcheck complete - tag lookup + 'jump' "
									"action + physics drop + goal-sensor "
									"onContactBegin AND the physics.contactBegin "
									"bus mirror all fired");
								integContactPhase = IntegContactPhase::Done;
								running = false;
							}
						}
						else if (frameCount > integContactInputFrame + 950)
						{
							integContactFail("the physics.contactBegin bus mirror "
								"was never delivered after onContactBegin");
						}
					}
					else if (frameCount > integContactInputFrame + 900)
					{
						integContactFail("the input-driven ball never contacted "
							"the goal sensor");
					}
				}
			}
			if (integrationContactCheck && integContactFailed)
			{
				exitCode = 1;
				running = false;
			}

			// --- integration: level switch with a live tween + particles (see
			// the block above the loop). Condition-driven throughout.
			if (integrationLevelCheck && !integLevelFailed &&
				integLevelPhase != IntegLevelPhase::Done)
			{
				if (integLevelPhase == IntegLevelPhase::ObserveA)
				{
					// director.lua's init published aliveA + moverStartX and
					// kicked off the tween + burst; confirm the tween is
					// advancing Mover AND the emitter has live particles, both
					// mid-flight (before the ~0.3s deferred switch)
					const double startX = integLevelNum("moverStartX", -999.0);
					const double moverX = integLevelNum("moverX", -999.0);
					if (integLevelNum("levelBBooted", 0.0) >= 1.0)
					{
						integLevelFail("the level switched before the live "
							"tween + particles could be observed on level A");
					}
					else if (integLevelNum("aliveA", 0.0) >= 1.0 &&
						startX > -900.0 && moverX > startX + 0.02 &&
						integFxLiveCount() > 0)
					{
						SDL_Log("orkige_player: integration levelswitch "
							"selfcheck - level A live (tween moved Mover to "
							"%.3f, %d particles alive)", moverX,
							integFxLiveCount());
						integLevelPhase = IntegLevelPhase::AwaitSwitch;
					}
					else if (frameCount > 600)
					{
						integLevelFail("never observed a live tween + particles "
							"on level A");
					}
				}
				else if (integLevelPhase == IntegLevelPhase::AwaitSwitch)
				{
					if (integLevelNum("levelBBooted", 0.0) >= 1.0)
					{
						integLevelTicksAtEntry =
							integLevelNum("levelBTicks", 0.0);
						integLevelVerifyFrame = frameCount;
						integLevelPhase = IntegLevelPhase::VerifyB;
					}
					else if (frameCount > 600)
					{
						integLevelFail("the deferred level switch never applied "
							"(level B never booted)");
					}
				}
				else if (integLevelPhase == IntegLevelPhase::VerifyB)
				{
					const bool moverGone =
						gameObjectManager.getGameObject("Mover").lock() ==
						nullptr;
					if (!moverGone)
					{
						integLevelFail("the old level's Mover survived the "
							"switch (scene teardown did not clear it)");
					}
					else if (integLevelNum("carrySeen", -1.0) != 4242.0)
					{
						integLevelFail("the shared table did not survive the "
							"switch (carry marker lost)");
					}
					else if (integLevelNum("levelBTicks", 0.0) >
						integLevelTicksAtEntry)
					{
						SDL_Log("orkige_player: integration levelswitch "
							"selfcheck complete - deferred switch tore down a "
							"live tween + emitter, shared state survived and "
							"level B ticks");
						integLevelPhase = IntegLevelPhase::Done;
						running = false;
					}
					else if (frameCount > integLevelVerifyFrame + 300)
					{
						integLevelFail("level B booted but never ticked");
					}
				}
			}
			if (integrationLevelCheck && integLevelFailed)
			{
				exitCode = 1;
				running = false;
			}

			if (frameCount == 60)
			{
				// ORKIGE_DEMO_SCREENSHOT: dump the framebuffer for automated
				// visual verification
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					render->saveWindowContents(shotPath);
				}
			}
			// ORKIGE_FADE_SELFCHECK: drive a fade-out -> scene switch -> fade-in
			// and observe the overlay alpha and the switch through the real loop
			if (fadeCheck)
			{
				if (frameCount == 5)
				{
					// the exact coordination the Lua screen.loadScene does: the
					// deferred switch is requested at full opacity
					screenFade.transition(0.2f, 0.2f, [&levelManager]()
					{
						levelManager.loadScenePath("scenes/sceneB.oscene");
					});
					fadeStarted = true;
				}
				if (fadeStarted)
				{
					fadeMaxAlpha = std::max(fadeMaxAlpha, screenFade.getAlpha());
					const bool markerBHere =
						gameObjectManager.getGameObject("MarkerB").lock() != nullptr;
					if (markerBHere && !fadeSwitched)
					{
						fadeSwitched = true;
						fadeAlphaAtSwitch = screenFade.getAlpha();
					}
					if (fadeSwitched && !screenFade.isFading() &&
						screenFade.getAlpha() < 0.02f)
					{
						fadeSawClearAfterSwitch = true;
					}
				}
				if (frameCount == 90)
				{
					bool ok = true;
					std::string detail;
					if (!fadeStarted || fadeMaxAlpha < 0.9f)
					{
						ok = false;
						detail = "overlay never reached opacity (max alpha " +
							std::to_string(fadeMaxAlpha) + ")";
					}
					else if (!fadeSwitched)
					{
						ok = false;
						detail = "scene never switched to sceneB";
					}
					else if (fadeAlphaAtSwitch < 0.5f)
					{
						ok = false;
						detail = "scene switched while the screen was not covered "
							"(alpha " + std::to_string(fadeAlphaAtSwitch) + ")";
					}
					else if (!fadeSawClearAfterSwitch)
					{
						ok = false;
						detail = "fade never cleared after the switch";
					}
					SDL_Log("orkige_player: FADE SELFCHECK %s%s%s",
						ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
					exitCode = ok ? 0 : 1;
					running = false;
				}
			}
			// ORKIGE_BREADCRUMB_SELFCHECK: by now (a dozen frames) the Crasher's
			// init has run, failed, and been recorded; read the trail off disk
			// and assert the script_error line is there with the object id
			if (breadcrumbCheck && frameCount == 15)
			{
				bool ok = true;
				std::string detail;
				const std::string file =
					Orkige::Breadcrumbs::getSingleton().getFile();
				Orkige::String trail;
				if (file.empty() || !Orkige::Breadcrumbs::loadFile(file, trail))
				{
					ok = false;
					detail = "breadcrumbs.jsonl not written";
				}
				else if (trail.find("\"script_error\"") == std::string::npos ||
					trail.find("Crasher") == std::string::npos)
				{
					ok = false;
					detail = "no script_error breadcrumb for the Crasher object";
				}
				else if (trail.find("\"boot\"") == std::string::npos)
				{
					ok = false;
					detail = "boot marker missing";
				}
				SDL_Log("orkige_player: BREADCRUMB SELFCHECK %s%s%s",
					ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
				exitCode = ok ? 0 : 1;
				running = false;
			}
			// ORKIGE_LIFECYCLE_SELFCHECK: drive the real player wiring with
			// synthetic SDL lifecycle events (SDL_PushEvent - processed at the
			// top of the NEXT iteration, exactly like a device's events) and
			// assert the backgrounding contract across a few frames.
			if (perfCheck && !perfCheckFailed && frameCount == 60)
			{
				auto perfFail = [&](std::string const& what)
				{
					SDL_Log("orkige_player: PERF SELFCHECK FAILED - %s "
						"(frame %lu)", what.c_str(), frameCount);
					perfCheckFailed = true;
					exitCode = 1;
					running = false;
				};
				// the frame boundary folded every frame so far
				if (Orkige::MemoryManager::framesSampled() < 50 ||
					Orkige::ProfileManager::framesSampled() < 50)
				{
					perfFail("frame boundaries never folded the instruments");
				}
				else if (Orkige::ProfileManager::lastFrameMilliseconds() <= 0.0)
				{
					perfFail("no frame duration was measured");
				}
				else
				{
					// the canonical tick phases all appear as depth-0 scopes
					std::vector<Orkige::ProfileManager::SnapshotNode> rows;
					Orkige::ProfileManager::snapshot(rows);
					const char* const requiredPhases[] = { "input", "scripts",
						"events", "tweens", "load", "audio", "present",
						"debug", "render" };
					for (const char* phase : requiredPhases)
					{
						bool found = false;
						for (Orkige::ProfileManager::SnapshotNode const& row :
							rows)
						{
							if (row.depth == 0 &&
								std::strcmp(row.name, phase) == 0)
							{
								found = true;
								break;
							}
						}
						if (!found)
						{
							perfFail(std::string("phase scope missing from "
								"the snapshot: ") + phase);
							break;
						}
					}
					if (!perfCheckFailed)
					{
						// the deliverable: the measured breakdown, logged
						SDL_Log("orkige_player: PERF SELFCHECK - frame %.3f ms, "
							"alloc/frame %zu (peak %zu), %zu profile rows",
							Orkige::ProfileManager::lastFrameMilliseconds(),
							Orkige::MemoryManager::lastFrameTotal(),
							Orkige::MemoryManager::peakFrameTotal(),
							rows.size());
						for (Orkige::ProfileManager::SnapshotNode const& row :
							rows)
						{
							if (row.depth == 0)
							{
								SDL_Log("orkige_player:   %s: %.3f ms (x%u)",
									row.name, row.milliseconds, row.calls);
							}
						}
						SDL_Log("orkige_player: PERF SELFCHECK COMPLETE");
						running = false;	// clean exit, code 0
					}
				}
			}

			if (lifecycleCheck && !lifecycleFailed)
			{
				auto lifecycleFail = [&](std::string const& what)
				{
					SDL_Log("orkige_player: LIFECYCLE SELFCHECK FAILED - %s "
						"(frame %lu)", what.c_str(), frameCount);
					lifecycleFailed = true;
					exitCode = 1;
					running = false;
				};
				auto crumbSeen = [](char const* kind) -> bool
				{
					const std::string needle = std::string("\"") + kind + "\"";
					return Orkige::Breadcrumbs::getSingleton().contents()
						.find(needle) != std::string::npos;
				};
				if (lifecyclePhase == LifecyclePhase::Init && frameCount == 5)
				{
					// the App script must be up - its onAppPause/onAppResume
					// hooks are what this selfcheck exercises
					optr<Orkige::GameObject> app =
						gameObjectManager.getGameObject("App").lock();
					Orkige::ScriptComponent* script = (app &&
						app->hasComponent<Orkige::ScriptComponent>()) ?
						app->getComponentPtr<Orkige::ScriptComponent>() : nullptr;
					if (!script || !script->isScriptStarted() ||
						script->hasScriptError())
					{
						lifecycleFail("App script never started cleanly");
					}
					else
					{
						SDL_Event evt{};
						evt.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
						SDL_PushEvent(&evt);
						evt.type = SDL_EVENT_DID_ENTER_BACKGROUND;
						SDL_PushEvent(&evt);
						lifecyclePhase = LifecyclePhase::Backgrounded;
					}
				}
				else if (lifecyclePhase == LifecyclePhase::Backgrounded &&
					frameCount == 8)
				{
					// the background contract: sim gated, rendering stopped, the
					// save FLUSHED (dirty cleared) with the value onAppPause wrote
					// persisted, a "background" breadcrumb recorded
					if (!lifecycle.isSimPaused())
					{
						lifecycleFail("sim not paused after background");
					}
					else if (!lifecycle.isRenderingStopped())
					{
						lifecycleFail("rendering not stopped after background");
					}
					else if (saveStore.getNumber("lifecycle.pauses", -1.0) < 1.0)
					{
						lifecycleFail("onAppPause did not run (no saved value)");
					}
					else if (saveStore.isDirty())
					{
						lifecycleFail("save store not flushed on background");
					}
					else if (!crumbSeen("background"))
					{
						lifecycleFail("no background breadcrumb");
					}
					else
					{
						SDL_Event evt{};
						evt.type = SDL_EVENT_WILL_ENTER_FOREGROUND;
						SDL_PushEvent(&evt);
						evt.type = SDL_EVENT_DID_ENTER_FOREGROUND;
						SDL_PushEvent(&evt);
						lifecyclePhase = LifecyclePhase::Foregrounded;
					}
				}
				else if (lifecyclePhase == LifecyclePhase::Foregrounded &&
					frameCount == 11)
				{
					// the foreground contract: sim resumed running, rendering
					// back, onAppResume fired, a "foreground" breadcrumb recorded
					if (lifecycle.isSimPaused())
					{
						lifecycleFail("sim still paused after foreground");
					}
					else if (lifecycle.isRenderingStopped())
					{
						lifecycleFail("rendering still stopped after foreground");
					}
					else if (saveStore.getNumber("lifecycle.resumes", -1.0) < 1.0)
					{
						lifecycleFail("onAppResume did not run (no saved value)");
					}
					else if (!crumbSeen("foreground"))
					{
						lifecycleFail("no foreground breadcrumb");
					}
					else
					{
						SDL_Log("orkige_player: LIFECYCLE SELFCHECK PASSED");
						lifecyclePhase = LifecyclePhase::Done;
						exitCode = 0;
						running = false;
					}
				}
			}
			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

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

		if (jumperLuaCheck && !jumperCheckFailed &&
			jumperPhase != JumperCheckPhase::Done)
		{
			SDL_Log("orkige_player: JUMPER-LUA SELFCHECK FAILED - run ended "
				"in phase %d", static_cast<int>(jumperPhase));
			exitCode = 1;
		}
		if (rollerCheck && !rollerCheckFailed &&
			rollerPhase != RollerCheckPhase::Done)
		{
			SDL_Log("orkige_player: ROLLER SELFCHECK FAILED - run ended in "
				"phase %d", static_cast<int>(rollerPhase));
			exitCode = 1;
		}
		if (rollerProgressionCheck && !rollerProgCheckFailed &&
			rollerProgPhase != RollerProgPhase::Done)
		{
			SDL_Log("orkige_player: ROLLER PROGRESSION SELFCHECK FAILED - run "
				"ended in phase %d", static_cast<int>(rollerProgPhase));
			exitCode = 1;
		}
		if (tweenCheck && !tweenCheckFailed && !tweenCheckDone)
		{
			SDL_Log("orkige_player: TWEEN SELFCHECK FAILED - run ended before "
				"the check completed");
			exitCode = 1;
		}
		if (lifecycleCheck && !lifecycleFailed &&
			lifecyclePhase != LifecyclePhase::Done)
		{
			SDL_Log("orkige_player: LIFECYCLE SELFCHECK FAILED - run ended in "
				"phase %d", static_cast<int>(lifecyclePhase));
			exitCode = 1;
		}
		if (integrationContactCheck && !integContactFailed &&
			integContactPhase != IntegContactPhase::Done)
		{
			SDL_Log("orkige_player: INTEGRATION CONTACT SELFCHECK FAILED - "
				"run ended in phase %d",
				static_cast<int>(integContactPhase));
			exitCode = 1;
		}
		if (integrationLevelCheck && !integLevelFailed &&
			integLevelPhase != IntegLevelPhase::Done)
		{
			SDL_Log("orkige_player: INTEGRATION LEVELSWITCH SELFCHECK FAILED "
				"- run ended in phase %d",
				static_cast<int>(integLevelPhase));
			exitCode = 1;
		}
		if (hotreloadCheck)
		{
			// restore the committed fixture script no matter how the run ended
			// (the selfcheck rewrites it in place) so the working tree stays
			// clean; the write is best-effort
			if (!hotreloadScriptPath.empty() && !hotreloadOriginal.empty())
			{
				std::ofstream restore(hotreloadScriptPath,
					std::ios::binary | std::ios::trunc);
				restore << hotreloadOriginal;
			}
			if (!hotreloadCheckFailed && hotreloadPhase != HotReloadPhase::Done)
			{
				SDL_Log("orkige_player: HOTRELOAD SELFCHECK FAILED - run ended "
					"in phase %d", static_cast<int>(hotreloadPhase));
				exitCode = 1;
			}
		}

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

		// orderly protocol shutdown: detach the log forwarder (the link dies
		// before the engine - declaration order), tell the editor we are
		// going down (the quit path already sent bye), flush the socket
		debugLink.shutdown();
	}

	// AppHost's destructor mirrors the boot: world, engine, singletons,
	// then the SDL window; the breadcrumb trail outlives it all
	return exitCode;
}
