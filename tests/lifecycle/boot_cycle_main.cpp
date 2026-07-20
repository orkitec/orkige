/********************************************************************
	created:	2026/07/20 at 12:00
	filename: 	boot_cycle_main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file boot_cycle_main.cpp
//! @brief boot_cycle_selfcheck - the real-stack scene-teardown cycler
//! @remarks A single-boot selfcheck cannot catch a teardown-ORDER or lifetime
//! fault: those surface only when a populated scene is torn down under a live
//! render system, and when the whole stack finally comes down. This app boots
//! the FULL runtime spine ONCE (AppHost: SDL window + the per-flavor Engine + a
//! live render system + the GameObjectManager) - the way the engine actually
//! runs, one render-system boot per process - then reaches every risky teardown
//! edge:
//!  - a live SKYBOX cubemap (whose sky quad SceneManager::clearScene re-attaches
//!    at Root::shutdown) and image-based lighting sourced from it stay armed the
//!    whole run and through final teardown;
//!  - GameObjects whose components hang render nodes off the world graph AND ask
//!    for per-frame updates are populated and then unloaded through
//!    GameObjectManager::clear (the scene teardown hook) MANY times, the way a
//!    real mid-session scene switch does it - under the live render system and
//!    with the skybox up;
//!  - a populated scene is left standing for the FINAL teardown, so the AppHost
//!    destructor runs Engine::~Engine -> RenderBackend::destroyRenderSystem with
//!    objects AND the skybox + IBL still live.
//!
//! It cycles the SCENE, not the render system: the engine never re-creates its
//! render system within one process (the editor spawns a fresh player per Play;
//! LevelManager switches scenes over one live renderer), so re-booting the
//! backend would test a path the engine neither exercises nor supports.
//!
//! It exists because two shipped teardown bugs would each have failed here: a
//! skybox re-attach on clearScene that dereferenced freed memory at shutdown
//! (fixed in destroyRenderSystem - the skybox is detached BEFORE the root tears
//! the scene manager down) and a heap-use-after-free in the GameObjectManager
//! update-list teardown. Both are memory-safety faults, so this app carries its
//! weight under the CI AddressSanitizer gate (it is desktop-labelled, not
//! device, so the instrumented Linux suite runs it) - a plain build often masks
//! a UAF a sanitizer build reports.

#include <SDL3/SDL.h>

#include <engine_runtime/AppHost.h>
#include <engine_graphic/Engine.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderCamera.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_util/optr.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using Orkige::optr;

namespace
{
	//! fail loudly - the non-zero exit code IS the ctest contract. `cycle` is
	//! the current scene-cycle index (-1 during the one-time boot/setup).
	#define CYCLE_CHECK(condition, description) \
		do \
		{ \
			if(!(condition)) \
			{ \
				std::fprintf(stderr, "boot_cycle_selfcheck: FAILED - %s " \
					"(cycle %d, %s:%d)\n", description, cycle, __FILE__, \
					__LINE__); \
				return false; \
			} \
		} while(false)

	//! render a few frames, pumping the SDL event queue between them so the
	//! host window stays responsive (the real host loop's SDL_PollEvent step)
	bool pumpAndRender(Orkige::RenderSystem* renderSystem,
		Orkige::GameObjectManager& gameObjectManager, int frames)
	{
		for(int each = 0; each < frames; ++each)
		{
			SDL_PumpEvents();
			SDL_Event event;
			while(SDL_PollEvent(&event))
			{
				// drained, not acted on - this app scripts no input
			}
			// tick the game world so update-wanting components land on (and
			// stay on) the manager's update list, the teardown-order precondition
			gameObjectManager.update(1.0f / 60.0f);
			if(!renderSystem->renderOneFrame())
			{
				return false;
			}
		}
		return true;
	}

	//! populate a small scene: GameObjects whose ModelComponents attach mesh
	//! instances to the world graph AND whose transform siblings ride the update
	//! list, so a later clear() unwinds live render + update state
	bool populateScene(Orkige::GameObjectManager& gameObjectManager, int cycle)
	{
		const int objectCount = 6;
		for(int index = 0; index < objectCount; ++index)
		{
			optr<Orkige::GameObject> object = gameObjectManager.createGameObject(
				"cycle_object_" + std::to_string(index)).lock();
			CYCLE_CHECK(object != NULL, "a scene GameObject was created");
			CYCLE_CHECK(object->addComponent<Orkige::ModelComponent>(),
				"the object took a ModelComponent (+ its TransformComponent)");
			Orkige::ModelComponent* model =
				object->getComponentPtr<Orkige::ModelComponent>();
			Orkige::TransformComponent* transform =
				object->getComponentPtr<Orkige::TransformComponent>();
			CYCLE_CHECK(model && transform,
				"the model and its transform sibling resolve");
			model->loadModel(Orkige::RenderWorld::CUBE_MESH_NAME);
			transform->setPosition(Orkige::Vec3(
				static_cast<float>(index) - 2.5f, 0, 0));
		}
		CYCLE_CHECK(gameObjectManager.getGameObjects().size() ==
			static_cast<std::size_t>(objectCount),
			"every scene object registered with the manager");
		return true;
	}

	//! ONE render-system boot; the SCENE is cycled populate -> clear N times,
	//! then a populated scene is left standing for the on-scope-exit teardown
	bool runSceneCycles(int sceneCycles)
	{
		int cycle = -1;	// -1 = the one-time boot/setup (for CYCLE_CHECK)

		Orkige::AppHost host;

		Orkige::AppHostConfig config;
		config.windowTitle = "orkige boot-cycle";
		config.windowWidth = 320;
		config.windowHeight = 240;
		// vsync-free (the automated-run pacing) - this is a frame-capped probe,
		// never a human session
		config.automatedRun = true;
		config.engineLogFile = "orkige_boot_cycle.log";
#ifdef ORKIGE_CYCLE_OGRE_MEDIA_DIR
		// classic RTSS media must be registered before Engine::setup; ignored
		// on the next flavor (its Hlms templates are a baked Engine default)
		config.classicMediaDir = ORKIGE_CYCLE_OGRE_MEDIA_DIR;
#endif
		const bool booted = host.boot(config, []()
			{
				// the committed sky cubemap (sky_day.dds) + cube mesh live here;
				// registered between Engine::setup and initialiseResourceGroups
				Orkige::RenderSystem::get()->addResourceLocation(
					ORKIGE_CYCLE_MEDIA_DIR);
			});
		CYCLE_CHECK(booted, "AppHost::boot brought the full stack up");

		Orkige::RenderSystem* renderSystem = host.getRenderSystem();
		Orkige::RenderWorld* world = host.getRenderWorld();
		Orkige::Engine& engine = host.getEngine();
		Orkige::GameObjectManager& gameObjectManager =
			host.getGameObjectManager();
		CYCLE_CHECK(renderSystem && world, "the render system + world are live");

		// frame the content
		optr<Orkige::RenderNode> cameraNode = host.getCameraNode();
		CYCLE_CHECK(cameraNode != NULL, "the window camera rig exists");
		cameraNode->setPosition(Orkige::Vec3(0, 2, 8));
		cameraNode->lookAt(Orkige::Vec3::ZERO, Orkige::RenderNode::TS_WORLD);

		// A LIVE SKYBOX, armed once and held for the whole run + final teardown.
		// This is the exact edge the fixed teardown bug lived on - a still-
		// attached sky quad that clearScene (run from Root::shutdown at AppHost
		// teardown) would re-attach after freeing it. Image lighting sources
		// from the same cubemap, so it exercises the IBL bookkeeping reset in
		// destroyRenderSystem too. Under a software rasterizer the cubemap may
		// decline to load (the backend logs one honest line and renders the flat
		// sky); the run still tears down cleanly, and on a real GPU the skybox
		// path is fully live.
		engine.setAtmosphere(true, 0.5f, 0.6f, 0.85f, 1.0f, 0.0f);
		engine.setAtmosphereSky("skybox", "sky_day.dds");
		engine.setImageLighting(true, 1.0f);

		// cycle the SCENE: populate -> render -> clear -> render empty, many
		// times, under the one live render system and the armed skybox. Each
		// clear() runs the GameObjectManager teardown hook the mid-session scene
		// switch uses; the skybox interaction is re-entered every cycle.
		for(cycle = 0; cycle < sceneCycles; ++cycle)
		{
			CYCLE_CHECK(populateScene(gameObjectManager, cycle),
				"the scene populated");
			CYCLE_CHECK(pumpAndRender(renderSystem, gameObjectManager, 3),
				"frames render with the populated scene + skybox");

			// UNLOAD through the scene teardown hook: clear() destroys every
			// GameObject (and through them their render nodes / mesh instances)
			// while the render system is still live
			gameObjectManager.clear();
			CYCLE_CHECK(gameObjectManager.getGameObjects().empty(),
				"GameObjectManager::clear emptied the world");
			// the empty scene, skybox still up, still renders
			CYCLE_CHECK(pumpAndRender(renderSystem, gameObjectManager, 2),
				"frames render after the scene was cleared");
		}

		// leave a POPULATED scene standing so the final teardown unwinds live
		// objects AND the skybox + IBL together (destroyRenderSystem with the
		// fullest possible live state)
		cycle = sceneCycles;
		CYCLE_CHECK(populateScene(gameObjectManager, cycle),
			"the final scene populated");
		CYCLE_CHECK(pumpAndRender(renderSystem, gameObjectManager, 2),
			"frames render with the final standing scene");

		std::printf("boot_cycle_selfcheck: ok - booted, cycled %d scene loads "
			"and left one standing\n", sceneCycles);
		std::fflush(stdout);
		return true;
		// host goes out of scope here: the AppHost destructor tears the whole
		// stack down (world -> script/render/engine singletons -> SDL window),
		// running destroyRenderSystem with the objects + skybox + IBL still armed
	}
}

int main(int, char**)
{
	// N meaningful but runtime-sane; overridable for a longer local soak
	int sceneCycles = 6;
	if(const char* override = std::getenv("ORKIGE_BOOT_CYCLES"))
	{
		const int parsed = std::atoi(override);
		if(parsed > 0)
		{
			sceneCycles = parsed;
		}
	}

	std::printf("boot_cycle_selfcheck: one boot, cycling scene load -> clear "
		"%d times, then teardown\n", sceneCycles);
	std::fflush(stdout);

	if(!runSceneCycles(sceneCycles))
	{
		return 1;
	}

	std::printf("boot_cycle_selfcheck: PASS - scene-teardown cycles + final "
		"teardown clean\n");
	return 0;
}
