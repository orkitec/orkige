/********************************************************************
	created:	2026/07/20 at 12:00
	filename: 	boot_cycle_main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file boot_cycle_main.cpp
//! @brief boot_cycle_selfcheck - the real-stack boot/teardown cycler
//! @remarks A single-boot selfcheck cannot catch a teardown-ORDER or
//! lifetime fault: those surface only when the whole stack comes up and goes
//! down. This app boots the FULL runtime spine (AppHost: SDL window + the
//! per-flavor Engine + a live render system + the GameObjectManager), fills a
//! small scene that reaches every risky teardown edge - a live SKYBOX cubemap
//! (whose sky quad SceneManager::clearScene re-attaches at Root::shutdown),
//! image-based lighting sourced from it, and GameObjects whose components hang
//! render nodes off the world graph AND ask for per-frame updates - unloads it
//! through GameObjectManager::clear (the scene teardown hook), then tears the
//! whole stack DOWN (the AppHost destructor runs Engine::~Engine ->
//! RenderBackend::destroyRenderSystem). It does this N times in one process, so
//! a cycle re-enters every boot/shutdown path a real session touches once.
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
	//! fail loudly - the non-zero exit code IS the ctest contract
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

	//! one full boot -> populate -> clear -> shutdown cycle on a fresh stack
	bool runOneCycle(int cycle)
	{
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

		// A LIVE SKYBOX: switch the atmosphere onto the committed cubemap. This
		// is the exact edge the fixed teardown bug lived on - a still-attached
		// sky quad that clearScene (run from Root::shutdown at AppHost teardown)
		// would re-attach after freeing it. Image lighting sources from the same
		// cubemap, so it exercises the IBL bookkeeping reset in destroyRenderSystem
		// too. Under a software rasterizer the cubemap may decline to load (the
		// backend logs one honest line and renders the flat sky); the cycle still
		// tears down cleanly, and on a real GPU the skybox path is fully live.
		engine.setAtmosphere(true, 0.5f, 0.6f, 0.85f, 1.0f, 0.0f);
		engine.setAtmosphereSky("skybox", "sky_day.dds");
		engine.setImageLighting(true, 1.0f);

		// a small scene: GameObjects whose ModelComponents attach mesh instances
		// to the world graph AND whose siblings ride the update list, so clear()
		// and the teardown destructor both unwind live render + update state
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

		// render the populated scene
		CYCLE_CHECK(pumpAndRender(renderSystem, gameObjectManager, 4),
			"frames render with the populated scene + skybox");

		// UNLOAD through the scene teardown hook: clear() destroys every
		// GameObject (and through them their render nodes / mesh instances)
		// while the render system is still live - the mid-session scene switch
		gameObjectManager.clear();
		CYCLE_CHECK(gameObjectManager.getGameObjects().empty(),
			"GameObjectManager::clear emptied the world");
		// the empty scene, skybox still up, still renders
		CYCLE_CHECK(pumpAndRender(renderSystem, gameObjectManager, 2),
			"frames render after the scene was cleared");

		// re-populate once, to prove clear() left the world re-usable (a fresh
		// object graph over the same live render system)
		{
			optr<Orkige::GameObject> object = gameObjectManager.createGameObject(
				"cycle_object_reload").lock();
			CYCLE_CHECK(object != NULL, "a GameObject creates after clear()");
			CYCLE_CHECK(object->addComponent<Orkige::ModelComponent>(),
				"the reloaded object took a ModelComponent");
			object->getComponentPtr<Orkige::ModelComponent>()->loadModel(
				Orkige::RenderWorld::CUBE_MESH_NAME);
		}
		CYCLE_CHECK(pumpAndRender(renderSystem, gameObjectManager, 2),
			"frames render after the scene was re-populated");

		std::printf("boot_cycle_selfcheck: ok - cycle %d booted, populated, "
			"cleared and rebuilt cleanly\n", cycle);
		std::fflush(stdout);
		return true;
		// host goes out of scope here: the AppHost destructor tears the whole
		// stack down (world -> script/render/engine singletons -> SDL window),
		// running destroyRenderSystem with the skybox + IBL still armed
	}
}

int main(int, char**)
{
	// N meaningful but runtime-sane (each cycle is a full window + render-system
	// boot); overridable for a longer local soak
	int cycles = 6;
	if(const char* override = std::getenv("ORKIGE_BOOT_CYCLES"))
	{
		const int parsed = std::atoi(override);
		if(parsed > 0)
		{
			cycles = parsed;
		}
	}

	std::printf("boot_cycle_selfcheck: cycling boot -> scene -> clear -> "
		"shutdown %d times\n", cycles);
	std::fflush(stdout);

	for(int cycle = 0; cycle < cycles; ++cycle)
	{
		if(!runOneCycle(cycle))
		{
			return 1;
		}
	}

	std::printf("boot_cycle_selfcheck: PASS - %d boot/teardown cycles clean\n",
		cycles);
	return 0;
}
