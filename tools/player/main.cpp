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
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_physic/PhysicsWorld.h>
#include <engine_input/InputManager.h>
#include <engine_runtime/PlayerRuntime.h>
#include <engine_util/FrameStatsUtil.h>
#include <engine_util/PrimitiveUtil.h>
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

#include <OgreLogManager.h>

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
		Ogre::Entity* model =
			gameObject->getComponentPtr<Orkige::ModelComponent>()->getModel();
		if (model)
		{
			Orkige::PrimitiveUtil::makeEntityVertexColourUnlit(model);
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
		// automated runs (ctest, the editor's play-mode tests - they inherit
		// ORKIGE_DEMO_FRAMES from the editor's environment) render as fast as
		// the machine allows; a HUMAN run gets vsync so games neither spin
		// uncapped nor tear
		const bool automatedRun = jumperLuaCheck || frameLimit != 0;

		Orkige::Engine engine(Ogre::SMT_DEFAULT,
			Orkige::StringUtil::BLANK, Orkige::StringUtil::BLANK,
			Orkige::StringUtil::BLANK, engineLogPath);
		engine.setCustomWindowParam("width",
			Orkige::StringUtil::Converter::toString(windowWidth));
		engine.setCustomWindowParam("height",
			Orkige::StringUtil::Converter::toString(windowHeight));
		if (!automatedRun)
		{
			engine.setCustomWindowParam("vsync", "true");
		}

		// ORKIGE_RENDERSYSTEM: explicit render system choice ("Vulkan",
		// "Metal", "GL3Plus", "GL" - see Engine::matchRenderSystemName);
		// unset keeps the default (first available, i.e. GL3Plus). Vulkan
		// (MoltenVK on macOS) has full RTSS support; OGRE 14.5's Metal RS
		// does not (no MSL backend - built-in default shaders only).
		if (const char* renderSystemEnv = std::getenv("ORKIGE_RENDERSYSTEM"))
		{
			engine.setPreferredRenderSystem(renderSystemEnv);
		}

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
		// RTSS shader library + OgreUnifiedShader.h, same locations
		// OgreBites::ApplicationContext registers (see CMakeLists.txt)
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			playerMediaDir + "/Main", "FileSystem", Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			playerMediaDir + "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);
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
				Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
					sampleAssetDir, "FileSystem");
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
					Ogre::ResourceGroupManager::getSingleton()
						.addResourceLocation(projectDir, "FileSystem",
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

		if (!engine.setup("Orkige Player", Orkige::Engine::SHOW_NEVER,
			Orkige::StringUtil::Converter::toString(
				reinterpret_cast<size_t>(orkige_native_window_handle(window)))))
		{
			SDL_Log("Engine::setup failed");
			return 1;
		}
		engine.createDefaultCameraAndViewport();
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

		Ogre::SceneManager* sceneManager = engine.getSceneManager();
		sceneManager->setAmbientLight(Ogre::ColourValue(0.2f, 0.2f, 0.2f));

		// the same shared resources the editor sets up before creating
		// objects: the unlit "VertexColour" material and the in-memory
		// "EditorCube.mesh" that saved scenes reference by name
		Orkige::PrimitiveUtil::createVertexColourMaterial();
		Orkige::PrimitiveUtil::createVertexColourCubeMesh(sceneManager);

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
		engine.getCamera()->getParentSceneNode()->setPosition(0.0f, 2.5f, 9.0f);
		engine.getCamera()->getParentSceneNode()->lookAt(
			Ogre::Vector3::ZERO, Ogre::Node::TS_WORLD);

		// frame-time statistics: the ORKIGE_DEMO_FPS_LOG measurement hook and
		// the one-time "this build is too slow to play" hint
		Orkige::FrameStatsUtil frameStats;

		// --- ORKIGE_JUMPER_LUA_SELFCHECK=1: the ScriptComponent milestone,
		// verified end to end against projects/jumper-lua (run with
		// --project projects/jumper-lua). Synthetic SDL key events take the
		// real input path (poll loop -> injectEvent -> isKeyDown); the C++
		// side observes ONLY what any outsider could: the Player object's
		// components through the world and the stats the script publishes
		// into the Lua `shared.jumper` table. Asserted, frame-scripted:
		//   frame  5  the ScriptComponent loaded scripts/player.lua, ran
		//             init without error and published shared.jumper
		//   10..30    hold RIGHT -> frame 45: the player moved +x
		//   frame 60  the script reports grounded; press SPACE -> the player
		//             rises >0.8m and lands again within 120 frames
		//   then      teleport into the first gap -> the script's kill plane
		//             respawns the player at the start (shared respawns +1)
		//   then      teleport in front of the goal -> the script's win
		//             check fires (shared wins +1)
		// Any missed deadline exits non-zero; measured values are logged.
		// (jumperLuaCheck itself is read above, before the engine window
		// exists.)
		enum class JumperCheckPhase
		{
			Script,			// fixed-frame part (boot, move, jump press)
			WaitLanding,	// jump flight until the script reports grounded
			WaitRespawn,	// falling in the gap until the script respawned
			WaitWin,		// in front of the goal until the win fired
			Done
		};
		JumperCheckPhase jumperPhase = JumperCheckPhase::Script;
		float jumperStartX = 0.0f;
		float jumperMovedX = 0.0f;
		float jumperJumpStartY = 0.0f;
		float jumperMaxRise = 0.0f;
		unsigned long jumperJumpFrame = 0;
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
			&jumperPlayerTransform](Ogre::Vector3 const& position)
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
				Ogre::Quaternion::IDENTITY);
			body->setLinearVelocity(Ogre::Vector3::ZERO);
			body->setAngularVelocity(Ogre::Vector3::ZERO);
			jumperPlayerTransform()->setPosition(position);
		};
		auto jumperFail = [&](std::string const& what)
		{
			Orkige::TransformComponent* transform = jumperPlayerTransform();
			const Ogre::Vector3 position = transform ?
				transform->getPosition() : Ogre::Vector3::ZERO;
			SDL_Log("orkige_player: JUMPER-LUA SELFCHECK FAILED - %s "
				"(pos %.2f/%.2f/%.2f grounded=%d respawns=%.0f wins=%.0f)",
				what.c_str(), position.x, position.y, position.z,
				static_cast<int>(jumperGrounded()),
				jumperStat("respawns", -1.0), jumperStat("wins", -1.0));
			jumperCheckFailed = true;
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

			if (!engine.renderOneFrame())
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
					else
					{
						SDL_Log("orkige_player: jumper-lua selfcheck - "
							"'%s' loaded and initialized",
							script->getScriptFile().c_str());
					}
				}
				if (frameCount == 10)
				{
					jumperStartX = jumperPlayerTransform()->getPosition().x;
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
				}
				if (frameCount == 30)
				{
					pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
				}
				if (frameCount == 45)
				{
					jumperMovedX = jumperPlayerTransform()->getPosition().x -
						jumperStartX;
					SDL_Log("orkige_player: jumper-lua selfcheck - moved "
						"+x %.3f m after 20 frames of RIGHT", jumperMovedX);
					if (jumperMovedX < 0.6f)
					{
						jumperFail("player did not move right under "
							"scripted input");
					}
				}
				if (frameCount == 60)
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
						jumperTeleport(Ogre::Vector3(2.75f, 1.5f, 0.0f));
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
							Ogre::Vector3(0.0f, 1.0f, 0.0f));
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
						jumperTeleport(Ogre::Vector3(36.0f, 3.0f, 0.0f));
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
					// camera-roll regression: the Lua follow camera lookAt's
					// the engine camera node every frame of this session -
					// the engine's fixed yaw axis must have kept it roll-free
					// (the old shortest-arc lookAt slowly tilted the horizon)
					const float cameraRollDegrees = std::abs(
						engine.getCamera()->getParentSceneNode()
							->getOrientation().getRoll().valueDegrees());
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
							"- script boot, movement (+%.2f m), jump "
							"(+%.2f m), kill-plane respawn, the goal and the "
							"roll-free camera all verified "
							"(respawns=%.0f wins=%.0f)", jumperMovedX,
							jumperMaxRise, jumperStat("respawns", -1.0),
							jumperStat("wins", -1.0));
						jumperPhase = JumperCheckPhase::Done;
						running = false;
					}
				}
				else if (frameCount >= jumperPhaseDeadline)
				{
					jumperFail("reaching the goal never triggered the "
						"script's win");
				}
			}
			if (jumperLuaCheck && jumperCheckFailed)
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
					engine.getRenderWindow()->writeContentsToFile(shotPath);
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
