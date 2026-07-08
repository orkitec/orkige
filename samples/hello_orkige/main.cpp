// hello_orkige - Phase 1 milestone demo.
// SDL3 owns the window and event loop; Orkige::Engine boots the renderer into
// it via the externalWindowHandle path, everything AFTER boot goes through the
// engine_render facade (A1, Docs/render-abstraction.md). Scene: a spinning
// vertex-colored cube (the shared procedural cube mesh), which exercises the
// whole RTSS shader pipeline without needing any asset files.
#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/MeshInstance.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_physic/PhysicsWorld.h>
#include <engine_fastgui/FastGuiManager.h>
#include <engine_input/InputManager.h>
#include <engine_sound/SoundManager.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_util/StringUtil.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>

#include <exception>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

extern "C" void* orkige_native_window_handle(SDL_Window* window);

// C++-side receiver for the event the Lua smoke-test script triggers -
// proves the Lua -> GlobalEventManager -> C++ listener path end-to-end
struct LuaEventProbe
{
	bool received = false;
	bool onLuaEvent(Orkige::Event const& event)
	{
		received = event.getData() &&
			event.getData()->getObjectID() == "lua_payload";
		return false;
	}
};

// quit-on-ESC through the engine input pipeline (SDL event -> InputManager ->
// GlobalEventManager -> listener) instead of a raw SDL keycode check
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

int main(int, char**)
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow("hello orkige", 1280, 720, 0);
	if (!window)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		return 1;
	}

	{
		// engine singletons normally created by Orkige::Application; the demo
		// boots just the ones Engine::setup depends on
		Orkige::Timer::initialise();
		Orkige::GlobalEventManager eventManager;
		// the scripting seam must exist before the module init functions run
		// so OrkigeMetaExport reaches the real backend state (otherwise it
		// targets the throwaway fallback state)
		Orkige::ScriptRuntime scriptRuntime;
		init_module_orkige_core();

		// ORKIGE_SANCTIONED_OGRE_BEGIN(classic-boot) - lint gate, see Util/ogre_containment.json
		// --- classic boot block (sanctioned raw-Ogre corner, see
		// Docs/render-abstraction.md "App boot"): Engine is the classic
		// backend's bootstrapper - constructing/configuring it and feeding
		// the RTSS its internal media stays classic plumbing; everything
		// after Engine::setup talks to the engine_render facade.
		// No resources.cfg / plugins.cfg / ogre.cfg: the demo wires its
		// resource locations manually and lets Engine::configure pick defaults.
		Orkige::Engine engine(Ogre::SMT_DEFAULT,
			Orkige::StringUtil::BLANK, Orkige::StringUtil::BLANK,
			Orkige::StringUtil::BLANK, "hello_orkige.log");
		engine.setCustomWindowParam("width", "1280");
		engine.setCustomWindowParam("height", "720");

		// ORKIGE_RENDERSYSTEM: explicit render system choice for this run
		// ("Vulkan", "Metal", "GL3Plus", "GL" - see
		// Engine::matchRenderSystemName); unset keeps the default (first
		// available, i.e. GL3Plus). Vulkan (MoltenVK on macOS) has full RTSS
		// support. OGRE 14.5's Metal RS has no RTSS/MSL backend: it renders
		// through OGRE's built-in default shaders (no vertex colours, no
		// lighting), so this demo's cubes come out untinted on Metal.
		if (const char* renderSystemEnv = std::getenv("ORKIGE_RENDERSYSTEM"))
		{
			engine.setPreferredRenderSystem(renderSystemEnv);
		}

		// The RTSS needs its shader library (and OgreUnifiedShader.h from
		// Media/Main) in the OgreInternal group before Engine::setup runs -
		// same locations OgreBites::ApplicationContext registers. Backend-
		// internal media = classic bootstrap business, not a facade call
		// (the same rule tests/render_facade/bootstrap_classic.cpp follows).
		// ORKIGE_DEMO_MEDIA_DIR is a demo-only compile definition pointing into
		// the vcpkg-installed OGRE media (see CMakeLists.txt).
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_DEMO_MEDIA_DIR "/Main", "FileSystem",
			Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_DEMO_MEDIA_DIR "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);

		if (!engine.setup("hello orkige", Orkige::Engine::SHOW_NEVER,
			Orkige::StringUtil::Converter::toString(
				reinterpret_cast<size_t>(orkige_native_window_handle(window)))))
		{
			SDL_Log("Engine::setup failed");
			return 1;
		}
		// ORKIGE_SANCTIONED_OGRE_END
		// --- end of the classic boot block: from here on the demo talks to
		// the renderer through the engine_render facade exclusively
		Orkige::RenderSystem* render = Orkige::RenderSystem::get();
		Orkige::RenderWorld* world = render->getWorld();

		// ORKIGE_DEMO_MESH=1: also load the generated glTF test asset
		// (samples/hello_orkige/media/test_mesh.glb, built by
		// Util/make_test_mesh.py) through the statically linked Codec_Assimp
		// plugin. Unconditional runs stay asset-free.
		const bool demoMesh = (std::getenv("ORKIGE_DEMO_MESH") != nullptr);
		if (demoMesh)
		{
			render->addResourceLocation(ORKIGE_DEMO_ASSET_DIR);
		}
		render->initialiseResourceGroups();

		// the window camera on a facade rig (the createDefaultCameraAndViewport
		// successor): perspective defaults match the old Engine camera, the
		// fixed yaw axis keeps per-frame lookAt calls roll-free
		optr<Orkige::RenderCamera> camera = world->createCamera("hello.camera");
		optr<Orkige::RenderNode> cameraNode = world->createNode("hello.cameraNode");
		cameraNode->setFixedYawAxis(true);
		camera->attachTo(cameraNode);
		render->showCameraOnWindow(camera);
		// the historical Engine default viewport colour
		render->setWindowBackgroundColour(Orkige::Color(0.0f, 0.0f, 1.0f));

		// input pipeline: the poll loop below feeds every SDL event into the
		// InputManager, which triggers Orkige input events globally
		Orkige::InputManager inputManager;
		QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&QuitOnEscape::onKeyPressed, &quitOnEscape);

		// ORKIGE_DEMO_SOUND=1: play a synthesized beep through the engine_sound
		// OpenAL Soft path at demo start; normal runs stay silent (the manager
		// is only initialized when the env var is set).
		Orkige::SoundManager soundManager;
		if (std::getenv("ORKIGE_DEMO_SOUND"))
		{
			if (!soundManager.init())
			{
				SDL_Log("hello_orkige: FAILED - SoundManager::init "
					"(OpenAL device/context) failed");
				return 1;
			}
			// 0.2s of 440Hz 16-bit mono PCM synthesized in code - the raw-PCM
			// path (SoundManager::createSoundFromPCM) needs no asset file
			const int sampleRate = 44100;
			const int sampleCount = sampleRate / 5;
			std::vector<int16_t> samples(sampleCount);
			for (int i = 0; i < sampleCount; ++i)
			{
				const float t = static_cast<float>(i) / sampleRate;
				const float fadeOut =
					1.0f - static_cast<float>(i) / sampleCount;
				samples[i] = static_cast<int16_t>(30000.0f * fadeOut *
					std::sin(2.0f * 3.14159265f * 440.0f * t));
			}
			soundManager.createSoundFromPCM("beep", samples.data(),
				static_cast<int>(samples.size() * sizeof(int16_t)),
				1, 16, sampleRate);
			if (soundManager.playSound("beep"))
			{
				SDL_Log("hello_orkige: beep playing (440Hz via OpenAL Soft)");
			}
			else
			{
				SDL_Log("hello_orkige: FAILED - beep did not start");
				return 1;
			}
		}

		world->setAmbientLight(Orkige::Color(0.2f, 0.2f, 0.2f));

		// the demo geometry: the shared procedural vertex-coloured cube mesh
		// (facade service; also creates the unlit "VertexColour" material the
		// RTSS reads vertex colours through). The 12-triangle count per cube
		// feeds the frame-stats self-check below.
		world->createVertexColourCubeMesh("HelloCube.mesh", 1.0f);
		optr<Orkige::RenderNode> cubeNode = world->createNode("cubeNode");
		optr<Orkige::MeshInstance> cube =
			world->createMeshInstance("HelloCube.mesh");
		cube->attachTo(cubeNode);

		// --- GameObject component bridge: a second, smaller cube that is not
		// placed through raw Ogre scene calls but through a GameObject with a
		// TransformComponent (engine_gocomponent), orbiting the main cube.
		// OEXPORT in engine_module/module.cpp registers the component
		// factories; GameObjectManager is the singleton owning the objects.
		init_module_orkige_engine();
		Orkige::GameObjectManager gameObjectManager;
		optr<Orkige::GameObject> orbiter =
			gameObjectManager.createGameObject("orbiter").lock();
		if (!orbiter || !orbiter->addComponent<Orkige::TransformComponent>())
		{
			SDL_Log("hello_orkige: FAILED - GameObject/TransformComponent "
				"creation failed");
			return 1;
		}
		Orkige::TransformComponent* orbiterTransform =
			orbiter->getComponentPtr<Orkige::TransformComponent>();

		// a smaller cube instance of the same mesh, attached to the
		// TransformComponent's facade node through a scaled child (WP-A1.3:
		// the demo content sits on facade types - no more reaching the
		// backend node by its deterministic name)
		optr<Orkige::RenderNode> smallCubeNode =
			orbiterTransform->createChildNode("orbiterVisual");
		smallCubeNode->setScale(Orkige::Vec3(0.35f, 0.35f, 0.35f));
		optr<Orkige::MeshInstance> smallCube =
			world->createMeshInstance("HelloCube.mesh");
		smallCube->attachTo(smallCubeNode);

		// ORKIGE_DEMO_MESH=1: a real mesh asset next to the procedural cubes -
		// createMeshInstance("test_mesh.glb") pulls the .glb through
		// Codec_Assimp (registered in Engine.cpp's static-plugin block). The
		// codec sets vertex-colour tracking on the synthesized material because
		// the mesh carries COLOR_0 vertex colours, but it also generates
		// normals (aiProcess_GenNormals) so lighting stays on; under this
		// scene's ambient-only light the colours would drown. Render it unlit,
		// the same treatment the cubes get.
		optr<Orkige::RenderNode> testMeshNode;
		optr<Orkige::MeshInstance> testMesh;
		if (demoMesh)
		{
			testMesh = world->createMeshInstance("test_mesh.glb");
			testMesh->setVertexColourUnlit();
			testMeshNode = world->createNode("testMeshNode");
			testMeshNode->setPosition(Orkige::Vec3(0.0f, 2.5f, 0.0f));
			testMesh->attachTo(testMeshNode);
			SDL_Log("hello_orkige: test_mesh.glb loaded via Codec_Assimp "
				"(%zu sub-meshes)", testMesh->getNumSubMeshes());
		}

		cameraNode->setPosition(Orkige::Vec3(0.0f, 2.0f, 6.0f));
		cameraNode->lookAt(Orkige::Vec3::ZERO, Orkige::RenderNode::TS_WORLD);

		// --- ORKIGE_DEMO_PHYSICS=1: Jolt dynamics through the engine_physic /
		// engine_gocomponent bridge. A static floor body, a pile of dynamic
		// cubes dropped from height, and two plane-locked cubes
		// (setPlanarMode: translation locked to X/Y, rotation to Z) shoved
		// sideways with an impulse. The world is stepped from this app loop
		// with the measured frame dt (engine-loop integration via
		// FrameStartedEvent is a Phase 2 TODO); self-checks run at frame 120.
		const bool demoPhysics = (std::getenv("ORKIGE_DEMO_PHYSICS") != nullptr);
		Orkige::PhysicsWorld physicsWorld; // inert until init()
		const float floorTopY = -2.0f;
		const float cubeHalf = 0.5f;
		std::vector<Orkige::TransformComponent*> dropTransforms;
		std::vector<Orkige::RigidBodyComponent*> dropBodies;
		std::vector<float> dropStartY;
		std::vector<Orkige::TransformComponent*> planarTransforms;
		std::vector<Orkige::RigidBodyComponent*> planarBodies;
		std::vector<float> planarStartX;
		// the physics boxes' visuals: cube-mesh instances on scaled child
		// nodes of the components' facade nodes - the app holds the handles
		// (RAII; released before the GameObjectManager by declaration order)
		std::vector<optr<Orkige::RenderNode>> physicsVisualNodes;
		std::vector<optr<Orkige::MeshInstance>> physicsVisuals;
		const float planarStartZ = 0.0f;
		if (demoPhysics)
		{
			physicsWorld.init();

			// GameObject with TransformComponent + RigidBodyComponent and a
			// cube-mesh visual attached (the unit cube scaled per axis to the
			// box's half extents); the rigid body is created at the
			// transform's pose on the first component update
			auto makePhysicsBox = [&](std::string const& name,
				Orkige::Vec3 const& pos, Orkige::Vec3 const& halfExtents,
				Orkige::PhysicsWorld::BodyType bodyType, bool planar)
				-> std::pair<Orkige::TransformComponent*,
					Orkige::RigidBodyComponent*>
			{
				optr<Orkige::GameObject> gameObject =
					gameObjectManager.createGameObject(name).lock();
				if (!gameObject ||
					!gameObject->addComponent<Orkige::TransformComponent>() ||
					!gameObject->addComponent<Orkige::RigidBodyComponent>())
				{
					return {nullptr, nullptr};
				}
				Orkige::TransformComponent* transform =
					gameObject->getComponentPtr<Orkige::TransformComponent>();
				Orkige::RigidBodyComponent* rigidBody =
					gameObject->getComponentPtr<Orkige::RigidBodyComponent>();
				transform->setPosition(pos);
				optr<Orkige::RenderNode> visualNode =
					transform->createChildNode(name + "Visual");
				visualNode->setScale(halfExtents);
				optr<Orkige::MeshInstance> visual =
					world->createMeshInstance("HelloCube.mesh");
				visual->attachTo(visualNode);
				physicsVisualNodes.push_back(visualNode);
				physicsVisuals.push_back(visual);
				rigidBody->setBodyType(bodyType);
				rigidBody->setBoxShape(halfExtents);
				rigidBody->setMass(1.0f);
				rigidBody->setPlanarMode(planar);
				return {transform, rigidBody};
			};

			// static floor, top surface at floorTopY
			if (!makePhysicsBox("physicsFloor",
				Orkige::Vec3(0.0f, floorTopY - 0.5f, 0.0f),
				Orkige::Vec3(12.0f, 0.5f, 12.0f),
				Orkige::PhysicsWorld::BT_STATIC, false).first)
			{
				SDL_Log("hello_orkige: FAILED - physics floor creation");
				return 1;
			}

			// dynamic cubes dropped from height (spread so each lands on the
			// floor instead of on a sibling)
			const Orkige::Vec3 dropPositions[4] = {
				{ 1.2f, 3.0f, -0.6f }, { 2.4f, 4.5f, -0.6f },
				{ 1.2f, 6.0f,  0.6f }, { 2.4f, 7.5f,  0.6f },
			};
			for (int i = 0; i < 4; ++i)
			{
				auto [transform, rigidBody] = makePhysicsBox(
					"physicsCube" + std::to_string(i), dropPositions[i],
					Orkige::Vec3(cubeHalf),
					Orkige::PhysicsWorld::BT_DYNAMIC, false);
				if (!transform)
				{
					SDL_Log("hello_orkige: FAILED - physics cube creation");
					return 1;
				}
				dropTransforms.push_back(transform);
				dropBodies.push_back(rigidBody);
				dropStartY.push_back(dropPositions[i].y);
			}

			// two plane-locked cubes (the 2D mode)
			const Orkige::Vec3 planarPositions[2] = {
				{ -3.5f, 2.0f, planarStartZ }, { -5.0f, 3.0f, planarStartZ },
			};
			for (int i = 0; i < 2; ++i)
			{
				auto [transform, rigidBody] = makePhysicsBox(
					"planarCube" + std::to_string(i), planarPositions[i],
					Orkige::Vec3(cubeHalf),
					Orkige::PhysicsWorld::BT_DYNAMIC, true);
				if (!transform)
				{
					SDL_Log("hello_orkige: FAILED - planar cube creation");
					return 1;
				}
				planarTransforms.push_back(transform);
				planarBodies.push_back(rigidBody);
				planarStartX.push_back(planarPositions[i].x);
			}

			// a zero-dt component update creates all bodies at their initial
			// poses (no simulation step runs) so the impulse has a body to
			// push on: 1 kg * 1.5 m/s sideways along +x
			gameObjectManager.update(0.0f);
			for (Orkige::RigidBodyComponent* rigidBody : planarBodies)
			{
				if (!rigidBody->hasBody())
				{
					SDL_Log("hello_orkige: FAILED - rigid body not created");
					return 1;
				}
				rigidBody->applyImpulse(Orkige::Vec3(1.5f, 0.0f, 0.0f));
			}
			SDL_Log("hello_orkige: physics world up - gravity (%.2f, %.2f, "
				"%.2f), %zu dynamic + %zu planar cubes",
				physicsWorld.getGravity().x, physicsWorld.getGravity().y,
				physicsWorld.getGravity().z, dropTransforms.size(),
				planarTransforms.size());

			// pull the camera back so floor and falling cubes stay in view
			cameraNode->setPosition(Orkige::Vec3(0.0f, 3.0f, 20.0f));
			cameraNode->lookAt(Orkige::Vec3(0.0f, -1.0f, 0.0f),
				Orkige::RenderNode::TS_WORLD);
		}

		// --- Lua scripting smoke test (Phase 2, sol2 meta backend): an inline
		// script pulls the Engine singleton, calls registered methods on it,
		// walks into the exposed engine_render facade types (WP-A1.5:
		// RenderSystem/RenderWorld/RenderCamera/RenderNode - the classic Ogre
		// usertypes are gone), constructs core objects through their
		// registered factories, triggers an event a C++ listener receives,
		// and sets a global the C++ side reads back. Runs through the
		// neutral ScriptRuntime seam; skipped in no-scripting builds.
		if (Orkige::ScriptRuntime::available())
		{
			LuaEventProbe luaProbe;
			optr<Orkige::EventListener> luaListener =
				Orkige::GlobalEventManager::getSingleton().bind(
					Orkige::EventType("lua_event"),
					&LuaEventProbe::onLuaEvent, &luaProbe);
			const Orkige::ScriptRuntime::Result luaResult =
				scriptRuntime.runString(R"lua(
				local engine = Engine.getSingleton()
				assert(engine ~= nil, 'Engine.getSingleton() returned nil')

				-- call registered Engine methods
				demo_window_handle = engine:getTopLevelWindowHandle()

				-- the render facade surface (RenderSystem/RenderWorld/
				-- RenderNode/RenderCamera usertypes): reach the scene graph
				-- and the window camera rig the demo built in C++
				local render = engine:getRenderSystem()
				assert(render ~= nil, 'engine:getRenderSystem() returned nil')
				local renderWorld = render:getWorld()
				assert(renderWorld ~= nil, 'render:getWorld() returned nil')
				assert(renderWorld:getRootNode():numChildren() > 0,
					'render world root has no facade child nodes')
				local camera = engine:getCamera()
				assert(camera ~= nil, 'engine:getCamera() returned nil')
				local cameraNode = camera:getNode()
				assert(cameraNode ~= nil, 'camera rig node not reachable from Lua')
				local cameraPos = cameraNode:getPosition()
				assert(cameraPos.z > 1.0, 'camera rig node answers a wrong position')
				assert(engine:getWindowWidth() > 0 and engine:getWindowHeight() > 0,
					'window size not reachable from Lua')

				-- construct registered core objects: Object through its factory,
				-- Event through the Lua call syntax (first registered constructor)
				local payload = Object.new1('lua_payload')
				assert(payload:getObjectID() == 'lua_payload')
				local ev = Event('lua_event')
				ev:setData(payload)
				assert(ev:getData():getObjectID() == 'lua_payload')

				-- fire it into the engine event system; a C++ listener verifies it
				GlobalEventManager.getSingleton():trigger(ev)

				demo_lua_ok = 42
			)lua");
			if (!luaResult.success)
			{
				SDL_Log("hello_orkige: FAILED - Lua script error: %s",
					luaResult.error.c_str());
				return 1;
			}
			const int luaOk = static_cast<int>(
				scriptRuntime.getNumber({"demo_lua_ok"}, 0.0));
			const std::string luaHandle =
				scriptRuntime.getString({"demo_window_handle"}, "");
			if (luaOk != 42 || !luaProbe.received)
			{
				SDL_Log("hello_orkige: FAILED - Lua verification (demo_lua_ok=%d, "
					"event received=%d)", luaOk,
					static_cast<int>(luaProbe.received));
				return 1;
			}
			SDL_Log("hello_orkige: Lua scripting OK - demo_lua_ok=%d, C++ listener "
				"got the Lua-triggered event, window handle via Lua='%s'",
				luaOk, luaHandle.c_str());
		}
		else
		{
			SDL_Log("hello_orkige: scripting disabled (backend '%s') - Lua "
				"smoke test skipped", Orkige::ScriptRuntime::backendName());
		}

		// ORKIGE_DEMO_GUI=1: engine_fastgui runtime smoke test. No .ogui texture
		// atlas exists anywhere in the repo or its git history (the 2012 game
		// assets were never checked in), so FastGui has nothing it could render
		// yet and fabricating an atlas by hand is off the table. What this
		// verifies instead: the module links, the Gorilla render backend
		// (Silverback) starts up, survives a rendered frame and tears down
		// cleanly, and FastGuiManager - which loads its default atlas in the
		// constructor by design - fails that construction with the documented
		// Ogre resource error instead of crashing.
		if (std::getenv("ORKIGE_DEMO_GUI"))
		{
			{
				// Gorilla backend alone: frame listener registration, an empty
				// rendered frame, and teardown all work without an atlas.
				Gorilla::Silverback silverback;
				if (!render->renderOneFrame())
				{
					SDL_Log("hello_orkige: FAILED - frame with Gorilla "
						"Silverback alive did not render");
					return 1;
				}
			}
			bool guiRefusedCleanly = false;
			try
			{
				Orkige::FastGuiManager guiManager(
					Orkige::onew(new Orkige::FastGuiFactory()));
			}
			catch (std::exception const& e)
			{
				// the classic backend surfaces the missing atlas as a resource
				// exception (derives from std::exception - no renderer types
				// needed to catch it)
				guiRefusedCleanly = true;
				SDL_Log("hello_orkige: FastGuiManager without atlas failed "
					"cleanly as designed: %s", e.what());
			}
			if (!guiRefusedCleanly)
			{
				SDL_Log("hello_orkige: FAILED - FastGuiManager constructed "
					"without its default atlas; expected a resource error");
				return 1;
			}
			SDL_Log("hello_orkige: FastGui smoke test passed (Silverback "
				"lifecycle + clean no-atlas constructor failure)");
		}

		// ORKIGE_DEMO_FRAMES: frame-limit escape for automated runs
		// (0/unset = run until the window is closed).
		unsigned long frameLimit = 0;
		if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
		{
			frameLimit = std::strtoul(demoFrames, nullptr, 10);
		}

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
			if (demoPhysics)
			{
				// measured frame dt for the physics step, clamped to
				// [1/60, 0.1]: the floor keeps headless runs (which render
				// far faster than 60 fps) accumulating enough simulated time
				// for the frame-based self-checks below, the cap avoids the
				// catch-up spiral after a stall
				const std::chrono::steady_clock::time_point frameTime =
					std::chrono::steady_clock::now();
				float deltaTime = std::chrono::duration<float>(
					frameTime - lastFrameTime).count();
				lastFrameTime = frameTime;
				deltaTime = std::clamp(deltaTime, 1.0f / 60.0f, 0.1f);
				physicsWorld.update(deltaTime);
				// component updates create the bodies and sync poses
				// (simulation -> TransformComponent for dynamic bodies)
				gameObjectManager.update(deltaTime);
			}
			cubeNode->yaw(Orkige::Degree(0.4f));
			cubeNode->pitch(Orkige::Degree(0.13f));
			// orbit the small cube around the main cube purely through the
			// TransformComponent API - proves the component bridge end-to-end
			const float orbitAngle = static_cast<float>(frameCount) * 0.02f;
			orbiterTransform->setPosition(Orkige::Vec3(
				3.0f * std::cos(orbitAngle), 0.8f, 3.0f * std::sin(orbitAngle)));
			orbiterTransform->setOrientation(Orkige::Quat(
				Orkige::Radian(-orbitAngle), Orkige::Vec3::UNIT_Y));
			if (!render->renderOneFrame())
			{
				running = false;
			}
			++frameCount;
			if (frameCount == 60)
			{
				// ORKIGE_DEMO_SCREENSHOT: dump the framebuffer for automated
				// visual verification
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					render->saveWindowContents(shotPath);
				}
			}
			// ORKIGE_DEMO_SYNTH_ESC: push a synthetic ESC key press through the
			// SDL event queue after 60 frames to prove the quit path (SDL event
			// -> InputManager::injectEvent -> KeyPressedEvent -> listener) in
			// automated runs; OS-level synthetic key events would need macOS
			// accessibility permissions, this stays inside SDL instead.
			if (frameCount == 60 && std::getenv("ORKIGE_DEMO_SYNTH_ESC"))
			{
				SDL_Event escEvent{};
				escEvent.type = SDL_EVENT_KEY_DOWN;
				escEvent.key.scancode = SDL_SCANCODE_ESCAPE;
				escEvent.key.key = SDLK_ESCAPE;
				escEvent.key.down = true;
				SDL_PushEvent(&escEvent);
			}
			if (frameCount == 10)
			{
				// verification that both cubes actually got drawn (12 triangles
				// each), not just a black window; with ORKIGE_DEMO_MESH the
				// glTF octahedron adds 8 more - read through the facade stats
				const size_t expectedTriangles = demoMesh ? 32 : 24;
				const size_t triangleCount =
					render->getFrameStats().triangleCount;
				SDL_Log("hello_orkige: triangle count after 10 frames: %zu",
					triangleCount);
				if (triangleCount < expectedTriangles)
				{
					SDL_Log("hello_orkige: FAILED - expected %s "
						"(>= %zu triangles)", demoMesh ?
						"both cubes and the test mesh" : "both cubes",
						expectedTriangles);
					return 1;
				}
			}
			if (demoPhysics && frameCount == 110)
			{
				// physics screenshot (cubes resting on the floor) through the
				// existing ORKIGE_DEMO_SCREENSHOT hook, later than the
				// frame-60 shot so the pile has settled
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					render->saveWindowContents(shotPath);
				}
			}
			if (demoPhysics && frameCount == 120)
			{
				// physics self-checks: (a) the dropped cubes fell and rest on
				// the floor (not below it), (b) the plane-locked cubes moved
				// in x but kept their z (DOF locks work)
				bool physicsOk = true;
				const float restY = floorTopY + cubeHalf;
				for (size_t i = 0; i < dropTransforms.size(); ++i)
				{
					const Orkige::Vec3 pos = dropTransforms[i]->getPosition();
					const float speed = dropBodies[i]->getLinearVelocity().length();
					const bool fell = (dropStartY[i] - pos.y) > 2.0f;
					const bool atRest = std::abs(pos.y - restY) < 0.3f &&
						pos.y > floorTopY && speed < 0.5f;
					SDL_Log("hello_orkige: physics cube %zu y=%.3f (start "
						"%.1f, rest %.1f) |v|=%.3f fell=%d atRest=%d",
						i, pos.y, dropStartY[i], restY, speed,
						static_cast<int>(fell), static_cast<int>(atRest));
					physicsOk = physicsOk && fell && atRest;
				}
				for (size_t i = 0; i < planarTransforms.size(); ++i)
				{
					const Orkige::Vec3 pos = planarTransforms[i]->getPosition();
					const bool zLocked =
						std::abs(pos.z - planarStartZ) < 1e-3f;
					const bool xMoved =
						std::abs(pos.x - planarStartX[i]) > 0.5f;
					SDL_Log("hello_orkige: planar cube %zu x=%.3f (start "
						"%.1f) z=%.6f (start %.1f) zLocked=%d xMoved=%d",
						i, pos.x, planarStartX[i], pos.z, planarStartZ,
						static_cast<int>(zLocked), static_cast<int>(xMoved));
					physicsOk = physicsOk && zLocked && xMoved;
				}
				if (!physicsOk)
				{
					SDL_Log("hello_orkige: FAILED - physics self-checks");
					return 1;
				}
				SDL_Log("hello_orkige: physics self-checks passed (fall + "
					"rest on floor, planar DOF locks)");
			}
			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

		soundManager.deinit();
	}

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
