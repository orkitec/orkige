// jumper-native - the Jumper Native project's game module: the
// proof that a .orkproj project can carry COMPILED C++ game code which the
// editor builds on Play (cmake/OrkigeGameModule.cmake) and runs as the play
// process.
//
// This executable implements the player CLI contract the editor expects:
//
//     jumper_native [scene.oscene] [--project <dir-or-.orkproj>] [--debug-port N]
//
// parsed by the shared Orkige::PlayerArguments; the editor's play-mode debug
// protocol (pause/step, remote hierarchy/inspector, [remote] log lines) is
// served by the shared Orkige::PlayerDebugLink - both from
// engine_runtime/PlayerRuntime.h, so a game module and tools/player behave
// identically on the wire.
//
// Gameplay: the jumper jump-and-run. The pure math (ground probe, approach,
// kill plane, goal check) is INCLUDED from samples/jumper/JumperLogic.h (one
// source of truth, unit-tested in tests/jumper) and the gui HUD (title
// splash, controls hint, win banner, progress bar) from the equally shared
// samples/jumper/JumperHud.h - the atlas rides in the PROJECT
// (assets/gui_default.{ogui,png}, loaded from the "OrkigeProject"
// resource group, exactly like jumper-lua's game.lua does it). The
// JumperInput/JumperGame glue below is duplicated from
// samples/jumper/main.cpp on purpose - trimmed to the project shape (no
// --write-level, scene/assets come from the PROJECT via --project instead of
// baked sample paths). The scene holds only the level data (scenes/
// main.oscene = platforms/crates/goal); the player object is spawned by THIS
// code at runtime - scene = data, module = behavior.
//
// Automation hooks (the standard set): ORKIGE_DEMO_FRAMES=N exits 0 after N
// frames, ORKIGE_RENDERSYSTEM picks the render system, ORKIGE_DEMO_FPS_LOG=1
// logs frame count / avg / p95 ms at exit, ORKIGE_DEMO_SCREENSHOT=path dumps
// the framebuffer at frame 55 (HUD title splash still up), and
// ORKIGE_JUMPER_NATIVE_SELFCHECK=1 asserts at frame 5 that the gui HUD
// booted from the project atlas (widgets exist, title splash showing, win
// banner hidden) - exits non-zero on failure; the editor_project_native_play
// ctest sets it, so the spawned play process proves the HUD along the way.
#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/MeshInstance.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_physic/PhysicsWorld.h>
#include <engine_input/InputManager.h>
#include <engine_runtime/AppHost.h>
#include <engine_runtime/PlayerRuntime.h>
#include <engine_util/FrameStatsUtil.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_debug/MemoryManager.h>
#include <core_debug/ProfileManager.h>
#include <core_game/SceneSerializer.h>
#include <core_project/Project.h>
#include <core_util/PlatformUtil.h>
#include <core_util/StringUtil.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>

#include <JumperHud.h>   // samples/jumper - the shared gui HUD
#include <JumperLogic.h> // samples/jumper - the shared pure gameplay math

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

using Orkige::optr;
using Orkige::woptr;

namespace
{

//! keyboard state fed by the engine input pipeline (SDL event ->
//! InputManager::injectEvent -> KeyPressed/KeyReleasedEvent -> here); same
//! as samples/jumper - gameplay code never touches raw SDL. ESC is handled
//! by the shared Orkige::QuitOnEscape listener (engine_runtime/AppHost.h).
struct JumperInput
{
	bool left = false;
	bool right = false;
	bool forward = false;	//!< into the screen (-z)
	bool back = false;		//!< toward the camera (+z)
	float jumpBuffer = 0.0f;	//!< seconds a queued jump press stays valid

	//! how long a SPACE press waits for the ground (jump buffering)
	static constexpr float JUMP_BUFFER_SECONDS = 0.12f;

	bool onKeyPressed(Orkige::Event const& event)
	{
		switch (event.getDataPtr<Orkige::KeyEventData>()->key)
		{
		case Orkige::KeyEventData::KC_A:
		case Orkige::KeyEventData::KC_LEFT:		left = true; break;
		case Orkige::KeyEventData::KC_D:
		case Orkige::KeyEventData::KC_RIGHT:	right = true; break;
		case Orkige::KeyEventData::KC_W:
		case Orkige::KeyEventData::KC_UP:		forward = true; break;
		case Orkige::KeyEventData::KC_S:
		case Orkige::KeyEventData::KC_DOWN:		back = true; break;
		case Orkige::KeyEventData::KC_SPACE:
			jumpBuffer = JUMP_BUFFER_SECONDS;
			break;
		default: break;
		}
		return false;
	}
	bool onKeyReleased(Orkige::Event const& event)
	{
		switch (event.getDataPtr<Orkige::KeyEventData>()->key)
		{
		case Orkige::KeyEventData::KC_A:
		case Orkige::KeyEventData::KC_LEFT:		left = false; break;
		case Orkige::KeyEventData::KC_D:
		case Orkige::KeyEventData::KC_RIGHT:	right = false; break;
		case Orkige::KeyEventData::KC_W:
		case Orkige::KeyEventData::KC_UP:		forward = false; break;
		case Orkige::KeyEventData::KC_S:
		case Orkige::KeyEventData::KC_DOWN:		back = false; break;
		default: break;
		}
		return false;
	}
};

//! the jump-and-run gameplay (duplicated from samples/jumper/main.cpp):
//! owns the player object and the per-frame rules - movement, jump,
//! respawn, win, camera follow. Reads scene data (the goal marker) from
//! the loaded level but never stores behavior in it.
class JumperGame
{
public:
	//--- tuning (the "feel" numbers, same as the sample) -------------------
	static constexpr float MOVE_SPEED = 4.5f;		//!< target run speed m/s
	static constexpr float ACCEL_RATE = 12.0f;		//!< velocity approach rate 1/s
	static constexpr float JUMP_SPEED = 8.0f;		//!< take-off velocity m/s
	static constexpr float GRAVITY_Y = -20.0f;		//!< snappy platformer gravity
	static constexpr float KILL_PLANE_Y = -10.0f;	//!< fall-out respawn line
	static constexpr float GOAL_RADIUS = 1.5f;		//!< win distance to the marker
	static constexpr float CAMERA_RATE = 5.0f;		//!< camera follow approach rate
	//--- player capsule (matches jumper_player.glb) -------------------------
	static constexpr float CAPSULE_HALF_HEIGHT = 0.25f;	//!< cylinder part
	static constexpr float CAPSULE_RADIUS = 0.35f;

	JumperGame(Orkige::GameObjectManager& gameObjectManager,
		Orkige::PhysicsWorld& physicsWorld,
		optr<Orkige::RenderNode> const& cameraNode)
		: mGameObjectManager(gameObjectManager), mPhysicsWorld(physicsWorld),
		mCameraNode(cameraNode)
	{
	}

	//! create the player GameObject (module-owned - NOT part of the scene)
	bool spawnPlayer()
	{
		optr<Orkige::GameObject> player =
			mGameObjectManager.createGameObject("Player").lock();
		if (!player ||
			!player->addComponent<Orkige::TransformComponent>() ||
			!player->addComponent<Orkige::ModelComponent>() ||
			!player->addComponent<Orkige::RigidBodyComponent>())
		{
			return false;
		}
		mPlayerTransform = player->getComponentPtr<Orkige::TransformComponent>();
		mPlayerBody = player->getComponentPtr<Orkige::RigidBodyComponent>();
		Orkige::ModelComponent* model =
			player->getComponentPtr<Orkige::ModelComponent>();
		model->loadModel("jumper_player.glb");
		if (!model->getMeshInstance())
		{
			return false;
		}
		model->getMeshInstance()->setVertexColourUnlit();
		mPlayerTransform->setPosition(mSpawnPosition);
		mPlayerBody->setBodyType(Orkige::PhysicsWorld::BT_DYNAMIC);
		mPlayerBody->setCapsuleShape(CAPSULE_HALF_HEIGHT, CAPSULE_RADIUS);
		mPlayerBody->setMass(1.0f);
		// low friction: the player is velocity-controlled, high friction
		// would glue the capsule to walls and platform edges
		mPlayerBody->setFriction(0.2f);
		mPlayerBody->setRestitution(0.0f);
		return true;
	}

	//! read the goal marker's position from the loaded scene (scene = data)
	void findGoal()
	{
		optr<Orkige::GameObject> goal =
			mGameObjectManager.getGameObject("Goal").lock();
		if (goal && goal->hasComponent<Orkige::TransformComponent>())
		{
			mGoalPosition = goal->getComponentPtr<
				Orkige::TransformComponent>()->getPosition();
			mHasGoal = true;
		}
		else
		{
			SDL_Log("jumper_native: no 'Goal' object in the scene - win "
				"check disabled");
		}
	}

	//! run one gameplay frame; call AFTER PhysicsWorld/GameObjectManager update
	void update(float deltaTime, JumperInput& input)
	{
		if (!mPlayerBody || !mPlayerBody->hasBody())
		{
			return;	// body is created lazily on the first component update
		}
		const Orkige::Vec3 position = mPlayerTransform->getPosition();

		// grounded: short ray just below the capsule (see JumperLogic for
		// why it must start OUTSIDE the capsule); never accept a self-hit
		const Orkige::JumperLogic::GroundProbe probe =
			Orkige::JumperLogic::makeGroundProbe(position,
				CAPSULE_HALF_HEIGHT, CAPSULE_RADIUS);
		Orkige::Vec3 hitPosition;
		Orkige::PhysicsWorld::BodyId hitBodyId =
			Orkige::PhysicsWorld::INVALID_BODY_ID;
		mGrounded = mPhysicsWorld.castRay(probe.origin, probe.direction,
			probe.maxDistance, hitPosition, hitBodyId) &&
			hitBodyId != mPlayerBody->getBodyId();

		// velocity control: exponential approach to the input direction, the
		// same in the air (full air control - tight, forgiving feel)
		const float inputX = (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);
		const float inputZ = (input.back ? 1.0f : 0.0f) - (input.forward ? 1.0f : 0.0f);
		Orkige::Vec3 velocity = mPlayerBody->getLinearVelocity();
		velocity.x = Orkige::JumperLogic::approach(velocity.x,
			inputX * MOVE_SPEED, ACCEL_RATE, deltaTime);
		velocity.z = Orkige::JumperLogic::approach(velocity.z,
			inputZ * MOVE_SPEED, ACCEL_RATE, deltaTime);

		// buffered jump: a SPACE press up to 0.12s before landing still jumps
		input.jumpBuffer = std::max(0.0f, input.jumpBuffer - deltaTime);
		if (mGrounded && input.jumpBuffer > 0.0f)
		{
			velocity.y = JUMP_SPEED;
			input.jumpBuffer = 0.0f;
			mGrounded = false;
		}
		mPlayerBody->setLinearVelocity(velocity);

		// keep the capsule upright (no rotation-lock DOFs exposed yet)
		mPlayerBody->setAngularVelocity(Orkige::Vec3::ZERO);
		const Orkige::Quat orientation = mPlayerTransform->getOrientation();
		if (std::abs(orientation.w) < 0.9995f)
		{
			mPhysicsWorld.setBodyTransform(mPlayerBody->getBodyId(), position,
				Orkige::Quat::IDENTITY);
		}

		// fell out of the level?
		if (Orkige::JumperLogic::isBelowKillPlane(position.y, KILL_PLANE_Y))
		{
			SDL_Log("jumper_native: fell out of the level - respawning");
			respawn();
			return;
		}
		// reached the buddy at the end?
		if (mHasGoal && Orkige::JumperLogic::reachedGoal(position,
			mGoalPosition, GOAL_RADIUS))
		{
			++mWinCount;
			SDL_Log("jumper_native: WIN! you reached your buddy (win #%d) - "
				"respawning for another round", mWinCount);
			respawn();
			return;
		}

		// smooth camera follow, looking slightly ahead of the player
		const Orkige::Vec3 cameraTarget = position + CAMERA_OFFSET;
		Orkige::Vec3 cameraPosition = mCameraNode->getPosition();
		cameraPosition.x = Orkige::JumperLogic::approach(cameraPosition.x,
			cameraTarget.x, CAMERA_RATE, deltaTime);
		cameraPosition.y = Orkige::JumperLogic::approach(cameraPosition.y,
			cameraTarget.y, CAMERA_RATE, deltaTime);
		cameraPosition.z = Orkige::JumperLogic::approach(cameraPosition.z,
			cameraTarget.z, CAMERA_RATE, deltaTime);
		mCameraNode->setPosition(cameraPosition);
		mCameraNode->lookAt(position + CAMERA_LOOK_AHEAD,
			Orkige::RenderNode::TS_WORLD);
	}

	//! put the player back at the start (teleport + kill all momentum)
	void respawn()
	{
		++mRespawnCount;
		if (!mPlayerBody || !mPlayerBody->hasBody())
		{
			return;
		}
		mPhysicsWorld.setBodyTransform(mPlayerBody->getBodyId(),
			mSpawnPosition, Orkige::Quat::IDENTITY);
		mPlayerBody->setLinearVelocity(Orkige::Vec3::ZERO);
		mPlayerBody->setAngularVelocity(Orkige::Vec3::ZERO);
		mPlayerTransform->setPosition(mSpawnPosition);
	}

	Orkige::Vec3 const& getSpawnPosition() const { return mSpawnPosition; }
	Orkige::Vec3 getPlayerPosition() const
	{
		return mPlayerTransform ? mPlayerTransform->getPosition()
			: Orkige::Vec3::ZERO;
	}
	int getWinCount() const { return mWinCount; }
	//! progress toward the goal along the level axis (0 = spawn, 1 = goal) -
	//! feeds the HUD progress bar, same math as the sample
	float getGoalProgress() const
	{
		if (!mHasGoal || mGoalPosition.x <= mSpawnPosition.x)
		{
			return 0.0f;
		}
		return (getPlayerPosition().x - mSpawnPosition.x) /
			(mGoalPosition.x - mSpawnPosition.x);
	}

private:
	static inline const Orkige::Vec3 CAMERA_OFFSET =
		Orkige::Vec3(0.0f, 2.2f, 8.0f);
	static inline const Orkige::Vec3 CAMERA_LOOK_AHEAD =
		Orkige::Vec3(1.5f, 0.8f, 0.0f);

	Orkige::GameObjectManager&		mGameObjectManager;
	Orkige::PhysicsWorld&			mPhysicsWorld;
	optr<Orkige::RenderNode>		mCameraNode;
	Orkige::TransformComponent*		mPlayerTransform = nullptr;
	Orkige::RigidBodyComponent*		mPlayerBody = nullptr;
	Orkige::Vec3					mSpawnPosition = Orkige::Vec3(0.0f, 1.0f, 0.0f);
	Orkige::Vec3					mGoalPosition = Orkige::Vec3::ZERO;
	bool							mHasGoal = false;
	bool							mGrounded = false;
	int								mRespawnCount = 0;
	int								mWinCount = 0;
};

} // namespace

int main(int argc, char** argv)
{
	// the player CLI contract, via the shared parser
	const Orkige::PlayerArguments arguments =
		Orkige::PlayerArguments::parse(argc, argv);
	if (!arguments.valid)
	{
		SDL_Log("jumper_native: unknown argument '%s'",
			arguments.unknownArgument.c_str());
		SDL_Log("usage: jumper_native [scene.oscene] "
			"[--project <dir-or-.orkproj>] [--debug-port N]");
		return 1;
	}
	std::string scenePath = arguments.scenePath;
	std::string projectPath = arguments.projectPath;

	// exported app, launched WITHOUT arguments: the orkige_project.txt marker
	// in the .app's Resources names the bundled project (the shared
	// PlayerBundle mechanism - see engine_runtime/PlayerRuntime.h and
	// Util/orkige_export.py); dev runs carry no marker and are unaffected
	bool bundledProjectRun = false;
	if (projectPath.empty() && scenePath.empty())
	{
		projectPath = Orkige::PlayerBundle::findBundledProject();
		bundledProjectRun = !projectPath.empty();
		if (bundledProjectRun)
		{
			SDL_Log("jumper_native: exported app - bundled project '%s'",
				projectPath.c_str());
		}
	}

	// --project roots the resource locations and provides the default scene
	Orkige::Project project;
	if (!projectPath.empty())
	{
		std::string projectError;
		if (!project.load(projectPath, &projectError))
		{
			SDL_Log("jumper_native: FAILED - %s", projectError.c_str());
			return 1;
		}
		if (scenePath.empty())
		{
			scenePath = project.getMainScenePath();
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
	if (scenePath.empty())
	{
		SDL_Log("usage: jumper_native [scene.oscene] "
			"[--project <dir-or-.orkproj>] [--debug-port N]");
		return 1;
	}

	// automation hook (read before boot - it gates the vsync and frame-pacing
	// decisions): ORKIGE_DEMO_FRAMES frame-limits the run; automated runs
	// (ctest, the editor's play-mode tests - they inherit the variable from
	// the editor's environment) render as fast as the machine allows, a HUMAN
	// run gets vsync so the game neither spins uncapped nor tears
	unsigned long frameLimit = 0;
	if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
	{
		frameLimit = std::strtoul(demoFrames, nullptr, 10);
	}
	// ORKIGE_JUMPER_NATIVE_SELFCHECK=1: assert at frame 5 that the gui
	// HUD booted from the project atlas; exits non-zero on failure (the
	// editor_project_native_play ctest sets it - a failed HUD boot drops
	// the play process and fails the editor-side hierarchy wait)
	const bool hudSelfCheck =
		(std::getenv("ORKIGE_JUMPER_NATIVE_SELFCHECK") != nullptr);
	const bool automatedRun = frameLimit != 0 || hudSelfCheck;

	// the shared boot spine (engine_runtime/AppHost.h): SDL window, engine
	// singletons, the classic Engine boot, the fixed-yaw window-camera rig
	// and the GameObject world. The RTSS media comes from the engine build's
	// vcpkg OGRE media (ORKIGE_MODULE_MEDIA_DIR, baked in by
	// OrkigeGameModule.cmake) for dev runs; an exported .app overrides it
	// with the Media/ bundled in its Resources so the bundle is
	// self-contained.
	Orkige::AppHostConfig hostConfig;
	hostConfig.windowTitle = "Orkige Jumper (native module)";
	hostConfig.automatedRun = automatedRun;
	// exported .app: never write into the cwd (a double-clicked app runs
	// with cwd "/") - the log goes to Application Support instead
	hostConfig.engineLogFile = bundledProjectRun
		? Orkige::PlatformUtil::getSupportDirectory("Orkige Player") +
			"jumper_native.log"
		: "jumper_native.log";
	hostConfig.classicMediaDir =
		Orkige::PlayerBundle::resolveMediaDirectory(ORKIGE_MODULE_MEDIA_DIR);

	int exitCode = 0;
	Orkige::AppHost host;
	if (!host.boot(hostConfig, [&]()
		{
			// the project's assets/ and scenes/ (meshes referenced by the
			// scene)
			if (project.isLoaded())
			{
				for (std::string const& projectDir : {
					project.getAssetsDirectory(),
					project.getScenesDirectory() })
				{
					std::error_code ignored;
					if (std::filesystem::is_directory(projectDir, ignored))
					{
						host.getRenderSystem()->addResourceLocation(projectDir,
							Orkige::RenderSystem::LT_FILESYSTEM,
							Orkige::Project::RESOURCE_GROUP_NAME);
					}
				}
			}
		}))
	{
		return 1;
	}
	{
		Orkige::RenderSystem* render = host.getRenderSystem();
		optr<Orkige::RenderNode> cameraNode = host.getCameraNode();
		// project scripts resolve against the open project's root directory
		if (project.isLoaded())
		{
			host.getScriptRuntime().setScriptSearchRoot(
				project.getRootDirectory());
		}

		// a friendly sky instead of the void
		render->setWindowBackgroundColour(Orkige::Color(0.53f, 0.75f, 0.94f));

		// input pipeline: the poll loop feeds every SDL event into the
		// InputManager - gameplay code never sees raw SDL; ESC quits through
		// the shared listener
		Orkige::InputManager inputManager;
		JumperInput input;
		Orkige::QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> keyPressedListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&JumperInput::onKeyPressed, &input);
		optr<Orkige::EventListener> keyReleasedListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyReleasedEvent,
				&JumperInput::onKeyReleased, &input);
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&Orkige::QuitOnEscape::onKeyPressed, &quitOnEscape);

		Orkige::GameObjectManager& gameObjectManager =
			host.getGameObjectManager();
		Orkige::PhysicsWorld physicsWorld;

		if (!Orkige::SceneSerializer::loadScene(scenePath, gameObjectManager))
		{
			SDL_Log("jumper_native: FAILED - could not load scene '%s'",
				scenePath.c_str());
			return 1;
		}
		Orkige::applyUnlitFixToLoadedModels(gameObjectManager);
		SDL_Log("jumper_native: level '%s' loaded (%zu GameObjects)",
			scenePath.c_str(), gameObjectManager.getGameObjects().size());

		if (!physicsWorld.init())
		{
			SDL_Log("jumper_native: FAILED - PhysicsWorld::init failed");
			return 1;
		}
		physicsWorld.setGravity(
			Orkige::Vec3(0.0f, JumperGame::GRAVITY_Y, 0.0f));

		JumperGame game(gameObjectManager, physicsWorld, cameraNode);
		if (!game.spawnPlayer())
		{
			SDL_Log("jumper_native: FAILED - player creation");
			return 1;
		}
		game.findGoal();
		cameraNode->setPosition(game.getSpawnPosition() +
			Orkige::Vec3(0.0f, 2.2f, 8.0f));
		cameraNode->lookAt(game.getSpawnPosition(),
			Orkige::RenderNode::TS_WORLD);

		// the HUD (the shared samples/jumper/JumperHud.h): title splash,
		// controls hint, win banner, distance-to-goal progress bar. The atlas
		// lives in the PROJECT (assets/gui_default.{ogui,png}) and loads
		// from the "OrkigeProject" resource group registered above - the same
		// arrangement as jumper-lua's game.lua. Without a project there is no
		// atlas to load, so bare-scene dev runs stay HUD-less (honestly
		// logged); every real path (editor Play, exported .app) has one.
		optr<Orkige::JumperHud> hud;
		if (project.isLoaded())
		{
			unsigned int hudWidth = 0;
			unsigned int hudHeight = 0;
			render->getWindowSize(hudWidth, hudHeight);
			hud = onew(new Orkige::JumperHud(static_cast<int>(hudWidth),
				static_cast<int>(hudHeight),
				"gui_default", Orkige::Project::RESOURCE_GROUP_NAME));
		}
		else
		{
			SDL_Log("jumper_native: no --project - the HUD atlas is a project "
				"asset, running without the HUD");
		}
		int hudLastWinCount = game.getWinCount();

		// remote debugging server (editor play mode) - the shared link
		Orkige::PlayerDebugLink debugLink;
		if (arguments.debugRequested)
		{
			if (!debugLink.start(arguments.debugPort))
			{
				SDL_Log("jumper_native: FAILED - could not listen on "
					"127.0.0.1:%u",
					static_cast<unsigned>(arguments.debugPort));
				return 1;
			}
			SDL_Log("jumper_native: debug server listening on 127.0.0.1:%u",
				static_cast<unsigned>(debugLink.getPort()));
		}

		// frame-time statistics: the ORKIGE_DEMO_FPS_LOG measurement hook and
		// the one-time "this build is too slow to play" hint
		Orkige::FrameStatsUtil frameStats;

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

			// debug protocol BEFORE stepping, so pause/step/set_property
			// apply to this frame (same wiring as tools/player)
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
			frameStats.maybeWarnSlow("jumper_native");
			deltaTime = Orkige::AppHost::clampFrameDelta(deltaTime,
				automatedRun);
			const bool advanceWorld = !debugLink.isPaused() || stepOnce;
			if (stepOnce)
			{
				deltaTime = Orkige::PhysicsWorld::FIXED_TIMESTEP;
			}
			if (advanceWorld)
			{
				physicsWorld.update(deltaTime);
				gameObjectManager.update(deltaTime); // body creation + pose sync
				game.update(deltaTime, input);		// the gameplay rules

				// HUD: win banner on a new win, then timers + progress bar
				// (frozen with the world while the editor has us paused)
				if (hud)
				{
					if (game.getWinCount() > hudLastWinCount)
					{
						hudLastWinCount = game.getWinCount();
						hud->showWinBanner();
					}
					hud->update(deltaTime, game.getGoalProgress());
				}
			}

			// streaming AFTER stepping - also while paused
			debugLink.stream(gameObjectManager, frameCount);

			if (!render->renderOneFrame())
			{
				running = false;
			}
			// perf-instrument frame boundary (same contract as the player):
			// fold the allocation counters and the profiler scope tree so the
			// stats/profile streams above carry per-frame numbers
			Orkige::MemoryManager::endFrame();
			Orkige::ProfileManager::endFrame();
			++frameCount;

			if (frameCount == 55)
			{
				// ORKIGE_DEMO_SCREENSHOT: the player stands on the textured
				// start platform with the HUD up (title splash, hint,
				// progress bar) - same hook as the sample
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					render->saveWindowContents(shotPath);
				}
			}

			// --- ORKIGE_JUMPER_NATIVE_SELFCHECK=1: the HUD boot assert -----
			// mirrors the frame-5 HUD block of the jumper sample's selfcheck:
			// GuiManager loaded the PROJECT'S atlas and all four widgets
			// exist, the title splash is showing, the win banner is not
			if (hudSelfCheck && frameCount == 5)
			{
				const bool widgets = hud && hud->widgetsExist();
				SDL_Log("jumper_native: hud selfcheck - created=%d widgets=%d "
					"title=%d banner=%d progress=%.0f%%",
					static_cast<int>(hud != nullptr),
					static_cast<int>(widgets),
					static_cast<int>(widgets && hud->isTitleVisible()),
					static_cast<int>(widgets && hud->isWinBannerVisible()),
					widgets ? hud->getProgress() : -1.0f);
				if (!widgets || !hud->isTitleVisible() ||
					hud->isWinBannerVisible())
				{
					SDL_Log("jumper_native: HUD SELFCHECK FAILED - the "
						"gui HUD did not boot from the project atlas");
					exitCode = 1;
					running = false;
				}
				else
				{
					SDL_Log("jumper_native: hud selfcheck passed - gui "
						"HUD up from the project's gui_default atlas");
				}
			}

			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

		frameStats.logAtExit("jumper_native");

		// orderly protocol shutdown (bye + flush, log forwarder detached)
		debugLink.shutdown();
	}

	// AppHost's destructor mirrors the boot: world, engine, singletons,
	// then the SDL window
	return exitCode;
}
