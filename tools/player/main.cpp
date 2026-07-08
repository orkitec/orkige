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
#include <engine_physic/PhysicsWorld.h>
#include <engine_input/InputManager.h>
#ifdef ORKIGE_RENDER_CLASSIC
// fastgui is classic-only (decision #2, Docs/render-abstraction.md): the
// game selfchecks below only assert the Lua-booted UI on that flavor - on
// next the games run HUD-less (scripts probe engine:hasUISystem())
#include <engine_fastgui/FastGuiManager.h>
#endif
#include <engine_runtime/PlayerRuntime.h>
#include <engine_util/FrameStatsUtil.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_project/Project.h>
#include <core_debugnet/DebugServer.h>
#include <core_util/PlatformUtil.h>
#include <core_util/StringUtil.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

extern "C" void* orkige_native_window_handle(SDL_Window* window);

namespace
{

// quit-on-ESC through the engine input pipeline (SDL event -> InputManager ->
// GlobalEventManager -> listener), same flow as the demo and the editor
struct QuitOnEscape
{
	bool quitRequested = false;
	bool onKeyPressed(Orkige::Event const& event)
	{
		if (event.getDataPtr<Orkige::KeyEventData>()->key ==
			Orkige::KeyEventData::KC_ESCAPE)
		{
			quitRequested = true;
		}
		return false;
	}
};

// ModelComponent does not serialize material tweaks (yet), so re-apply the
// unlit vertex-colour render state to every model after the scene load -
// the same treatment the editor gives freshly created objects
void applyUnlitFixToLoadedModels(Orkige::GameObjectManager& gameObjectManager)
{
	for (auto const& [id, gameObject] : gameObjectManager.getGameObjects())
	{
		if (!gameObject->hasComponent<Orkige::ModelComponent>())
		{
			continue;
		}
		optr<Orkige::MeshInstance> mesh =
			gameObject->getComponentPtr<Orkige::ModelComponent>()
				->getMeshInstance();
		if (mesh)
		{
			mesh->setVertexColourUnlit();
		}
	}
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
// the frame loop.

//! push a synthetic key event through the SDL queue (the jumper selfcheck
//! pattern): the loop's SDL_PollEvent feeds it into InputManager::injectEvent,
//! so scripted input takes the REAL input path - including isKeyDown
void pushKeyEvent(SDL_Scancode scancode, SDL_Keycode key, bool down)
{
	SDL_Event event{};
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.scancode = scancode;
	event.key.key = key;
	event.key.down = down;
	SDL_PushEvent(&event);
}

//! push a synthetic mouse move through the SDL queue - same real input path
//! as pushKeyEvent (InputManager -> MouseMovedEvent -> FastGuiManager)
void pushMouseMove(float x, float y)
{
	SDL_Event event{};
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.x = x;
	event.motion.y = y;
	SDL_PushEvent(&event);
}

//! push a synthetic left mouse button press/release at the given position
void pushMouseButton(float x, float y, bool down)
{
	SDL_Event event{};
	event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.down = down;
	event.button.x = x;
	event.button.y = y;
	SDL_PushEvent(&event);
}

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
	// --project <dir-or-.orkproj> - Unity-style: play a whole project, its
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

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
#ifdef __ANDROID__
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
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
	// mobile: fullscreen native window; SDL sizes it to the screen/surface
	// regardless of the requested size
	SDL_Window* window = SDL_CreateWindow("Orkige Player", 1280, 720,
		SDL_WINDOW_FULLSCREEN);
#else
	SDL_Window* window = SDL_CreateWindow(
		("Orkige Player - " + scenePath).c_str(), 1280, 720, 0);
#endif
	if (!window)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		return 1;
	}
	// the actual window size (iOS: the screen size in points - the unit
	// OGRE's EAGL2 window expects; desktop: the requested 1280x720)
	int windowWidth = 1280;
	int windowHeight = 720;
	SDL_GetWindowSize(window, &windowWidth, &windowHeight);

	int exitCode = 0;
	{
		// engine singletons normally created by Orkige::Application; the
		// player boots the same set as the hello_orkige demo
		Orkige::Timer::initialise();
		Orkige::GlobalEventManager eventManager;
		// the scripting seam must exist before the module init functions run
		// so OrkigeMetaExport reaches the real backend state
		Orkige::ScriptRuntime scriptRuntime;
		// project scripts: ScriptComponent paths like "scripts/player.lua"
		// resolve against the open project's root directory
		if (project.isLoaded())
		{
			scriptRuntime.setScriptSearchRoot(project.getRootDirectory());
		}
		init_module_orkige_core();

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
		// automated runs (ctest, the editor's play-mode tests - they inherit
		// ORKIGE_DEMO_FRAMES from the editor's environment) render as fast as
		// the machine allows; a HUMAN run gets vsync so games neither spin
		// uncapped nor tear
		const bool automatedRun = jumperLuaCheck || rollerCheck ||
			frameLimit != 0;

		// ORKIGE_SANCTIONED_OGRE_BEGIN(classic-boot) - lint gate, see Util/ogre_containment.json
		// --- per-flavor boot block (B3, Docs/render-abstraction.md "App
		// boot"): on classic, Engine construction/config and the
		// RTSS-internal media registration stay classic plumbing; on the
		// next flavor the Engine sibling (engine_graphic/EngineNext.h)
		// carries the same parameters into RenderBackend::createRenderSystem.
		// After Engine::setup the player talks to the engine_render facade
		// on BOTH flavors.
#ifdef ORKIGE_RENDER_CLASSIC
		Orkige::Engine engine(Ogre::SMT_DEFAULT,
			Orkige::StringUtil::BLANK, Orkige::StringUtil::BLANK,
			Orkige::StringUtil::BLANK, engineLogPath);
#else
		Orkige::Engine engine(engineLogPath);
#endif
		engine.setCustomWindowParam("width",
			Orkige::StringUtil::Converter::toString(windowWidth));
		engine.setCustomWindowParam("height",
			Orkige::StringUtil::Converter::toString(windowHeight));
		if (!automatedRun)
		{
			engine.setCustomWindowParam("vsync", "true");
		}

#ifdef ORKIGE_RENDER_CLASSIC
		// ORKIGE_RENDERSYSTEM: explicit render system choice ("Vulkan",
		// "Metal", "GL3Plus", "GL" - see Engine::matchRenderSystemName);
		// unset keeps the default (first available, i.e. GL3Plus). Vulkan
		// (MoltenVK on macOS) has full RTSS support; OGRE 14.5's Metal RS
		// does not (no MSL backend - built-in default shaders only).
		// (The next flavor boots Ogre-Next's Metal RS unconditionally.)
		if (const char* renderSystemEnv = std::getenv("ORKIGE_RENDERSYSTEM"))
		{
			engine.setPreferredRenderSystem(renderSystemEnv);
		}
#endif

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
#ifdef ORKIGE_RENDER_CLASSIC
		// RTSS shader library + OgreUnifiedShader.h, same locations
		// OgreBites::ApplicationContext registers (see CMakeLists.txt) -
		// backend-internal media, needed before setup: classic bootstrap
		// business, not a facade call. (The next flavor's Hlms media is a
		// built-in default of its Engine sibling.)
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			playerMediaDir + "/Main", "FileSystem", Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			playerMediaDir + "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);
#else
		// keep the exported-app media resolution alive on this flavor too
		// (unused until next-flavor export exists)
		(void)playerMediaDir;
#endif

		if (!engine.setup("Orkige Player", Orkige::Engine::SHOW_NEVER,
			Orkige::StringUtil::Converter::toString(
				reinterpret_cast<size_t>(orkige_native_window_handle(window)))))
		{
			SDL_Log("Engine::setup failed");
			return 1;
		}
		// ORKIGE_SANCTIONED_OGRE_END
		// --- end of the boot block: from here on the player talks to the
		// engine_render facade exclusively (both flavors)
		Orkige::RenderSystem* render = Orkige::RenderSystem::get();
		Orkige::RenderWorld* world = render->getWorld();

		// the window camera on a facade rig (createDefaultCameraAndViewport
		// successor, same pattern as the samples). The fixed yaw axis keeps
		// per-frame lookAt calls roll-free - project scripts drive this rig
		// through the Lua bindings (engine:getCamera():getNode(),
		// engine:setCameraOrthographic, ...) since WP-A1.5.
		optr<Orkige::RenderCamera> camera = world->createCamera("player.camera");
		optr<Orkige::RenderNode> cameraNode =
			world->createNode("player.cameraNode");
		cameraNode->setFixedYawAxis(true);
		camera->attachTo(cameraNode);
		render->showCameraOnWindow(camera);

		// sample assets (test_mesh.glb; scene meshes load lazily via
		// Codec_Assimp) and the jumper sample assets, so the editor's play
		// mode works on samples/* scenes too. Registered only when present:
		// an exported app ships nothing but its project's assets, and the
		// (baked-in) dev source-tree paths must not abort the run elsewhere
		for (std::string const& sampleAssetDir :
			{ playerAssetDir, playerJumperAssetDir })
		{
			std::error_code sampleDirError;
			if (std::filesystem::is_directory(sampleAssetDir, sampleDirError))
			{
				render->addResourceLocation(sampleAssetDir);
			}
		}
		// --project: the project's assets/ and scenes/ become resource
		// locations in the dedicated project group (the same group the
		// editor uses); a missing directory is skipped with an honest line
		if (project.isLoaded())
		{
			for (std::string const& projectDir : {
				project.getAssetsDirectory(), project.getScenesDirectory() })
			{
				std::error_code ignored;
				if (std::filesystem::is_directory(projectDir, ignored))
				{
					render->addResourceLocation(projectDir,
						Orkige::RenderSystem::LT_FILESYSTEM,
						Orkige::Project::RESOURCE_GROUP_NAME);
				}
				else
				{
					SDL_Log("orkige_player: project directory '%s' does not "
						"exist - not registered", projectDir.c_str());
				}
			}
			SDL_Log("orkige_player: project '%s' (root '%s') rooted the "
				"resource locations", project.getName().c_str(),
				project.getRootDirectory().c_str());
		}
		render->initialiseResourceGroups();

		world->setAmbientLight(Orkige::Color(0.2f, 0.2f, 0.2f));

		// the same shared resources the editor sets up before creating
		// objects: the in-memory "EditorCube.mesh" (plus its unlit
		// "VertexColour" material) that saved scenes reference by name
		world->createVertexColourCubeMesh();

		// input pipeline: the poll loop below feeds every SDL event into the
		// InputManager, which triggers Orkige input events globally
		Orkige::InputManager inputManager;
		QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&QuitOnEscape::onKeyPressed, &quitOnEscape);

		// GameObject/component bridge (registers the component factories)
		init_module_orkige_engine();
		Orkige::GameObjectManager gameObjectManager;
		Orkige::PhysicsWorld physicsWorld; // inert until init()

		if (!Orkige::SceneSerializer::loadScene(scenePath, gameObjectManager))
		{
			SDL_Log("orkige_player: FAILED - could not load scene '%s'",
				scenePath.c_str());
			return 1;
		}
		applyUnlitFixToLoadedModels(gameObjectManager);
		SDL_Log("orkige_player: scene '%s' loaded (%zu GameObjects)",
			scenePath.c_str(), gameObjectManager.getGameObjects().size());

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

		// physics only when the scene needs it: RigidBodyComponents create
		// their bodies lazily on the first component update, which requires
		// an initialized PhysicsWorld
		const bool physicsNeeded = sceneHasRigidBodies(gameObjectManager);
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

		// frame-time statistics: the ORKIGE_DEMO_FPS_LOG measurement hook and
		// the one-time "this build is too slow to play" hint
		Orkige::FrameStatsUtil frameStats;

		// --- ORKIGE_JUMPER_LUA_SELFCHECK=1: the ScriptComponent milestone,
		// verified end to end against projects/jumper-lua (run with
		// --project projects/jumper-lua). Synthetic SDL key AND mouse events
		// take the real input path (poll loop -> injectEvent -> InputManager
		// events -> FastGuiManager/isKeyDown); the C++ side observes ONLY
		// what any outsider could: the Player object's components through
		// the world, the Lua-booted fastgui widgets through the
		// FastGuiManager singleton and the stats the scripts publish into
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
		// the Lua-booted UI, seen through the FastGuiManager singleton: does
		// the widget exist, and is its screen (= its shared z layer) visible.
		// UI-GATED (B3): fastgui is classic-only (decision #2) - on the next
		// flavor the games run HUD-less (game.lua probes engine:hasUISystem())
		// and every UI assertion below is skipped via uiChecksEnabled; the
		// GAMEPLAY assertions (script state, movement, physics, win flow)
		// stay identical on both flavors.
#ifdef ORKIGE_RENDER_CLASSIC
		constexpr bool uiChecksEnabled = true;
		auto jumperWidgetExists = [](const char* id) -> bool
		{
			Orkige::FastGuiManager* ui =
				Orkige::FastGuiManager::getSingletonPtr();
			return ui && ui->widgetExists(id);
		};
		auto jumperWidgetVisible = [&jumperWidgetExists](const char* id) -> bool
		{
			if (!jumperWidgetExists(id))
			{
				return false;
			}
			optr<Orkige::FastGuiWidget> widget =
				Orkige::FastGuiManager::getSingleton().getWidget(id).lock();
			return widget && widget->getLayer()->isVisible();
		};
		auto jumperHudProgress = [&jumperWidgetExists]() -> float
		{
			if (!jumperWidgetExists("hud.progress"))
			{
				return -1.0f;
			}
			optr<Orkige::FastGuiProgressBar> progressBar =
				Orkige::FastGuiManager::getSingleton()
					.getWidgetAs<Orkige::FastGuiProgressBar>("hud.progress")
					.lock();
			return progressBar ? progressBar->getProgress() : -1.0f;
		};
#else
		constexpr bool uiChecksEnabled = false;
		auto jumperWidgetExists = [](const char*) -> bool { return false; };
		auto jumperWidgetVisible = [](const char*) -> bool { return false; };
		auto jumperHudProgress = []() -> float { return -1.0f; };
#endif
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
		// fastgui widgets. Frame-scripted:
		//   frame  5  both scripts booted, HUD up, mode "play", sim running
		//   10..70    hold LEFT -> the simulated tilt turns gravity left and
		//             the ball rolls -x (position sampled)
		//   ~80       screenshot roller_play.png (when the env dir is set)
		//   85..95    TAB -> mode "move": physics PAUSED, cursor visible
		//             over the empty slot; collision probed at tile B's
		//             future wall location (must be free)
		//   100..112  DOWN -> tile B slides into the empty slot: its frame
		//             SPRITE moved -6 in y, the GOAL rode along, and a ray
		//             probe proves the wall BODY collides at the new spot
		//             (and no longer at the old one) - while still paused
		//   115..125  TAB -> back to "play", sim unpaused
		//   130..     hold RIGHT -> gravity swings right, the ball rolls
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
		// UI-GATED (B3): same rule as the jumper lambdas above
#ifdef ORKIGE_RENDER_CLASSIC
		auto rollerWidgetExists = [](const char* id) -> bool
		{
			Orkige::FastGuiManager* ui =
				Orkige::FastGuiManager::getSingletonPtr();
			return ui && ui->widgetExists(id);
		};
		auto rollerWidgetVisible = [&rollerWidgetExists](const char* id) -> bool
		{
			if (!rollerWidgetExists(id))
			{
				return false;
			}
			optr<Orkige::FastGuiWidget> widget =
				Orkige::FastGuiManager::getSingleton().getWidget(id).lock();
			return widget && widget->getLayer()->isVisible();
		};
#else
		auto rollerWidgetExists = [](const char*) -> bool { return false; };
		auto rollerWidgetVisible = [](const char*) -> bool { return false; };
#endif
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

		bool running = true;
		unsigned long frameCount = 0;
		std::chrono::steady_clock::time_point lastFrameTime =
			std::chrono::steady_clock::now();
		while (running)
		{
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				if (event.type == SDL_EVENT_QUIT)
				{
					running = false;
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

			// measured frame dt. Automated (frame-scripted) runs keep the
			// historical 1/60 floor so headless frames accumulate simulated
			// time; a HUMAN run uses the real dt - flooring it at 1/60 made
			// gameplay run FASTER than real time whenever rendering beat 60
			// fps (physics stepped its fixed 1/60 tick once per rendered
			// frame). The 0.1 cap stays: it avoids the catch-up spiral after
			// a stall, at the honest price of slow motion below 10 fps.
			const std::chrono::steady_clock::time_point frameTime =
				std::chrono::steady_clock::now();
			float deltaTime = std::chrono::duration<float>(
				frameTime - lastFrameTime).count();
			lastFrameTime = frameTime;
			frameStats.addFrame(deltaTime);
			frameStats.maybeWarnSlow("orkige_player");
			deltaTime = std::clamp(deltaTime,
				automatedRun ? 1.0f / 60.0f : 0.0001f, 0.1f);
			// pause gates the stepping only - rendering and the debug
			// protocol stay alive; a step is exactly one fixed physics tick
			const bool advanceWorld = !debugLink.isPaused() || stepOnce;
			if (stepOnce)
			{
				deltaTime = Orkige::PhysicsWorld::FIXED_TIMESTEP;
			}
			if (advanceWorld)
			{
				if (physicsNeeded)
				{
					physicsWorld.update(deltaTime);
				}
				// component updates (rigid body creation/pose sync, ...)
				gameObjectManager.update(deltaTime);
			}

			// streaming: hierarchy on change (checked every N frames),
			// selected object state at ~15Hz, queued log lines - also while
			// paused
			debugLink.stream(gameObjectManager, frameCount);

			if (!render->renderOneFrame())
			{
				running = false;
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
						jumperFail("the Lua-booted fastgui widgets are "
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
					Orkige::ScriptComponent* ballScript = (ball &&
						ball->hasComponent<Orkige::ScriptComponent>()) ?
						ball->getComponentPtr<Orkige::ScriptComponent>() :
						nullptr;
					if (!ballScript)
					{
						rollerFail("no Ball object with a ScriptComponent");
					}
					else if (ballScript->hasScriptError())
					{
						rollerFail("ball script error: " +
							ballScript->getScriptError());
					}
					else if (!rollerFlag("gameReady") ||
						!rollerFlag("ballReady"))
					{
						rollerFail("scripts did not publish shared.roller");
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
						rollerTransform("TileB_Frame");
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
						rollerFail("no TileB_Frame object in the scene");
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
						rollerTileBStartY = tileB->getPosition().y;
						Orkige::TransformComponent* goal =
							rollerTransform("Goal");
						rollerGoalStartY = goal ?
							goal->getPosition().y : 0.0f;
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
					Orkige::TransformComponent* tileB =
						rollerTransform("TileB_Frame");
					Orkige::TransformComponent* goal =
						rollerTransform("Goal");
					const float tileBMovedY = tileB ?
						tileB->getPosition().y - rollerTileBStartY : 0.0f;
					if (rollerStat("slides", 0.0) < 1.0)
					{
						rollerFail("DOWN in move mode did not slide a tile");
					}
					else if (std::abs(tileBMovedY + 6.0f) > 0.01f)
					{
						rollerFail("tile B's frame sprite did not move one "
							"slot down");
					}
					else if (!goal || std::abs(goal->getPosition().y -
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
							"slid %.2f in y while paused; wall body probes "
							"old=free new=hit, goal rode along", tileBMovedY);
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

			if (frameCount == 60)
			{
				// ORKIGE_DEMO_SCREENSHOT: dump the framebuffer for automated
				// visual verification
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					render->saveWindowContents(shotPath);
				}
			}
			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
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

	SDL_DestroyWindow(window);
	SDL_Quit();
	return exitCode;
}
