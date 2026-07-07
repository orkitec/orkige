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
// ORKIGE_DEMO_SCREENSHOT=path framebuffer dump at frame 60.
#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_physic/PhysicsWorld.h>
#include <engine_input/InputManager.h>
#include <engine_util/PrimitiveUtil.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_debugnet/DebugServer.h>
#include <core_util/StringUtil.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#ifdef ORKIGE_LUA
#include <core_script/ScriptManager.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
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

namespace Protocol = Orkige::DebugProtocol;

//! frames between hierarchy change checks (~4 checks/second at 60 fps)
const unsigned long HIERARCHY_CHECK_INTERVAL = 15;
//! minimum milliseconds between object_state messages (~15 Hz)
const int OBJECT_STATE_INTERVAL_MS = 66;

//! everything the per-frame debug link handling needs
struct PlayerDebugLink
{
	Orkige::DebugServer server;
	bool active = false;			//!< --debug-port was given and start() worked
	bool paused = false;			//!< update/physics stepping gated
	int pendingSteps = 0;			//!< queued single-steps (only served while paused)
	bool quitRequested = false;		//!< editor sent quit
	std::string selectedObjectId;	//!< object whose state is streamed
	Orkige::StringVector lastSentHierarchy;
	bool hierarchySent = false;		//!< has any hierarchy gone out yet
	//! default-constructed = clock epoch, so the first send never waits
	//! (time_point::min() would overflow the duration subtraction)
	std::chrono::steady_clock::time_point lastStateSend =
		std::chrono::steady_clock::time_point();
};

//! print a float with round-trip precision (matches DebugMessage::setFloat)
std::string formatFloat(float value)
{
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%.9g", value);
	return buffer;
}

std::string formatVector3(Ogre::Vector3 const& v)
{
	return formatFloat(v.x) + " " + formatFloat(v.y) + " " + formatFloat(v.z);
}

std::string formatQuaternion(Ogre::Quaternion const& q)
{
	return formatFloat(q.w) + " " + formatFloat(q.x) + " " +
		formatFloat(q.y) + " " + formatFloat(q.z);
}

//! parse exactly count whitespace-separated floats; false on any junk
bool parseFloats(std::string const& text, float* out, int count)
{
	std::istringstream stream(text);
	for (int i = 0; i < count; ++i)
	{
		if (!(stream >> out[i]))
		{
			return false;
		}
	}
	std::string trailing;
	return !(stream >> trailing); // no trailing tokens allowed
}

void sendDebugError(PlayerDebugLink& link, std::string const& text)
{
	Orkige::DebugMessage error(Protocol::MSG_ERROR);
	error.set(Protocol::FIELD_MESSAGE, text);
	link.server.send(error);
	SDL_Log("orkige_player: debug command error: %s", text.c_str());
}

//! all GameObject ids (the manager map is sorted, so the order is stable)
Orkige::StringVector collectHierarchy(
	Orkige::GameObjectManager& gameObjectManager)
{
	Orkige::StringVector ids;
	ids.reserve(gameObjectManager.getGameObjects().size());
	for (auto const& [id, gameObject] : gameObjectManager.getGameObjects())
	{
		ids.push_back(id);
	}
	return ids;
}

//! send the id list when it differs from the last sent one (or when forced)
void sendHierarchyIfChanged(PlayerDebugLink& link,
	Orkige::GameObjectManager& gameObjectManager, bool force)
{
	if (!link.server.hasClient())
	{
		return;
	}
	Orkige::StringVector ids = collectHierarchy(gameObjectManager);
	if (!force && link.hierarchySent && ids == link.lastSentHierarchy)
	{
		return;
	}
	Orkige::DebugMessage hierarchy(Protocol::MSG_HIERARCHY);
	hierarchy.setList(Protocol::LIST_IDS, ids);
	link.server.send(hierarchy);
	link.lastSentHierarchy = std::move(ids);
	link.hierarchySent = true;
}

//! @brief object_state v1: per-component property snapshot via the known
//! component getters. Exposed: TransformComponent position/orientation/scale,
//! ModelComponent mesh, RigidBodyComponent body_type/has_body/
//! linear_velocity/angular_velocity; other components appear in the
//! "components" list without properties.
Orkige::DebugMessage buildObjectState(optr<Orkige::GameObject> const& gameObject)
{
	Orkige::DebugMessage state(Protocol::MSG_OBJECT_STATE);
	state.set(Protocol::FIELD_ID, gameObject->getObjectID());
	Orkige::StringVector componentNames;
	for (auto const& [componentType, component] : gameObject->getComponents())
	{
		componentNames.push_back(componentType.getName());
	}
	state.setList(Protocol::LIST_COMPONENTS, componentNames);
	if (gameObject->hasComponent<Orkige::TransformComponent>())
	{
		Orkige::TransformComponent* transform =
			gameObject->getComponentPtr<Orkige::TransformComponent>();
		state.set("TransformComponent.position",
			formatVector3(transform->getPosition()));
		state.set("TransformComponent.orientation",
			formatQuaternion(transform->getOrientation()));
		state.set("TransformComponent.scale",
			formatVector3(transform->getScale()));
	}
	if (gameObject->hasComponent<Orkige::ModelComponent>())
	{
		state.set("ModelComponent.mesh",
			gameObject->getComponentPtr<Orkige::ModelComponent>()
				->getCurrentModelFileName());
	}
	if (gameObject->hasComponent<Orkige::RigidBodyComponent>())
	{
		Orkige::RigidBodyComponent* rigidBody =
			gameObject->getComponentPtr<Orkige::RigidBodyComponent>();
		const char* bodyTypeNames[] = { "static", "kinematic", "dynamic" };
		const int bodyType = static_cast<int>(rigidBody->getBodyType());
		state.set("RigidBodyComponent.body_type",
			(bodyType >= 0 && bodyType <= 2) ? bodyTypeNames[bodyType] : "?");
		state.set("RigidBodyComponent.has_body",
			rigidBody->hasBody() ? "1" : "0");
		state.set("RigidBodyComponent.linear_velocity",
			formatVector3(rigidBody->getLinearVelocity()));
		state.set("RigidBodyComponent.angular_velocity",
			formatVector3(rigidBody->getAngularVelocity()));
	}
	return state;
}

//! @brief set_property v1: TransformComponent position ("x y z"),
//! orientation ("w x y z", normalized here), scale ("x y z");
//! RigidBodyComponent linear_velocity/angular_velocity ("x y z", needs the
//! created body). Unknown objects/components/properties and malformed values
//! answer with an error message - never crash.
void handleSetProperty(PlayerDebugLink& link,
	Orkige::GameObjectManager& gameObjectManager,
	Orkige::DebugMessage const& message)
{
	const std::string id = message.get(Protocol::FIELD_ID);
	const std::string component = message.get(Protocol::FIELD_COMPONENT);
	const std::string property = message.get(Protocol::FIELD_PROPERTY);
	const std::string value = message.get(Protocol::FIELD_VALUE);
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.getGameObject(id).lock();
	if (!gameObject)
	{
		sendDebugError(link, "set_property: no GameObject '" + id + "'");
		return;
	}
	if (component == "TransformComponent" &&
		gameObject->hasComponent<Orkige::TransformComponent>())
	{
		Orkige::TransformComponent* transform =
			gameObject->getComponentPtr<Orkige::TransformComponent>();
		float floats[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (property == "position" && parseFloats(value, floats, 3))
		{
			transform->setPosition(
				Ogre::Vector3(floats[0], floats[1], floats[2]));
			return;
		}
		if (property == "orientation" && parseFloats(value, floats, 4))
		{
			Ogre::Quaternion orientation(floats[0], floats[1], floats[2],
				floats[3]);
			orientation.normalise();
			transform->setOrientation(orientation);
			return;
		}
		if (property == "scale" && parseFloats(value, floats, 3))
		{
			transform->setScale(
				Ogre::Vector3(floats[0], floats[1], floats[2]));
			return;
		}
	}
	else if (component == "RigidBodyComponent" &&
		gameObject->hasComponent<Orkige::RigidBodyComponent>())
	{
		Orkige::RigidBodyComponent* rigidBody =
			gameObject->getComponentPtr<Orkige::RigidBodyComponent>();
		float floats[3] = { 0.0f, 0.0f, 0.0f };
		if ((property == "linear_velocity" ||
			property == "angular_velocity") && parseFloats(value, floats, 3))
		{
			if (!rigidBody->hasBody())
			{
				sendDebugError(link, "set_property: '" + id +
					"' has no created rigid body yet");
				return;
			}
			const Ogre::Vector3 velocity(floats[0], floats[1], floats[2]);
			if (property == "linear_velocity")
			{
				rigidBody->setLinearVelocity(velocity);
			}
			else
			{
				rigidBody->setAngularVelocity(velocity);
			}
			return;
		}
	}
	sendDebugError(link, "set_property: unsupported or malformed " +
		component + "." + property + " = '" + value + "' on '" + id + "'");
}

//! drain and act on every queued editor command
void processDebugMessages(PlayerDebugLink& link,
	Orkige::GameObjectManager& gameObjectManager)
{
	Orkige::DebugMessage message;
	while (link.server.receive(message))
	{
		if (message.type == Protocol::MSG_PAUSE)
		{
			link.paused = true;
			link.pendingSteps = 0;
		}
		else if (message.type == Protocol::MSG_RESUME)
		{
			link.paused = false;
			link.pendingSteps = 0;
		}
		else if (message.type == Protocol::MSG_STEP)
		{
			if (link.paused)
			{
				++link.pendingSteps;
			}
			else
			{
				sendDebugError(link, "step: not paused");
			}
		}
		else if (message.type == Protocol::MSG_QUIT)
		{
			link.quitRequested = true;
			link.server.send(Orkige::DebugMessage(Protocol::MSG_BYE));
		}
		else if (message.type == Protocol::MSG_SELECT)
		{
			const std::string id = message.get(Protocol::FIELD_ID);
			if (id.empty() || gameObjectManager.objectExists(id))
			{
				link.selectedObjectId = id;
				// stream the first state message right away
				link.lastStateSend = std::chrono::steady_clock::time_point();
			}
			else
			{
				sendDebugError(link, "select: no GameObject '" + id + "'");
			}
		}
		else if (message.type == Protocol::MSG_SET_PROPERTY)
		{
			handleSetProperty(link, gameObjectManager, message);
		}
		else if (message.type == Protocol::MSG_REQUEST_HIERARCHY)
		{
			sendHierarchyIfChanged(link, gameObjectManager, true);
		}
		else
		{
			sendDebugError(link, "unknown command '" + message.type + "'");
		}
	}
}

//! stream the selected object's state at ~15Hz
void streamObjectState(PlayerDebugLink& link,
	Orkige::GameObjectManager& gameObjectManager)
{
	if (!link.server.hasClient() || link.selectedObjectId.empty())
	{
		return;
	}
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	if (now - link.lastStateSend <
		std::chrono::milliseconds(OBJECT_STATE_INTERVAL_MS))
	{
		return;
	}
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.getGameObject(link.selectedObjectId).lock();
	if (!gameObject)
	{
		sendDebugError(link, "selected GameObject '" + link.selectedObjectId +
			"' no longer exists");
		link.selectedObjectId.clear();
		return;
	}
	link.server.send(buildObjectState(gameObject));
	link.lastStateSend = now;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		SDL_Log("usage: orkige_player <scene.oscene> [--debug-port N]");
		return 1;
	}
	const std::string scenePath = argv[1];
	bool debugRequested = false;
	unsigned short debugPort = 0;
	for (int argIndex = 2; argIndex < argc; ++argIndex)
	{
		if (std::strcmp(argv[argIndex], "--debug-port") == 0 &&
			argIndex + 1 < argc)
		{
			debugRequested = true;
			debugPort = static_cast<unsigned short>(
				std::strtoul(argv[++argIndex], nullptr, 10));
		}
		else
		{
			SDL_Log("orkige_player: unknown argument '%s'", argv[argIndex]);
			return 1;
		}
	}

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow(
		("Orkige Player - " + scenePath).c_str(), 1280, 720, 0);
	if (!window)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		return 1;
	}

	int exitCode = 0;
	{
		// engine singletons normally created by Orkige::Application; the
		// player boots the same set as the hello_orkige demo
		Orkige::Timer::initialise();
		Orkige::GlobalEventManager eventManager;
#ifdef ORKIGE_LUA
		// ScriptManager must exist before the module init functions run so
		// OrkigeMetaExport reaches the real Lua state
		Orkige::ScriptManager scriptManager;
#endif
		init_module_orkige_core();

		Orkige::Engine engine(Ogre::SMT_DEFAULT,
			Orkige::StringUtil::BLANK, Orkige::StringUtil::BLANK,
			Orkige::StringUtil::BLANK, "orkige_player.log");
		engine.setCustomWindowParam("width", "1280");
		engine.setCustomWindowParam("height", "720");

		// RTSS shader library + OgreUnifiedShader.h, same locations
		// OgreBites::ApplicationContext registers (see CMakeLists.txt)
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_PLAYER_MEDIA_DIR "/Main", "FileSystem", Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_PLAYER_MEDIA_DIR "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);
		// sample assets (test_mesh.glb); scene meshes load lazily via
		// Codec_Assimp when ModelComponent::load calls loadModel
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_PLAYER_ASSET_DIR, "FileSystem");

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
		PlayerDebugLink debugLink;
		if (debugRequested)
		{
			if (!debugLink.server.start(debugPort))
			{
				SDL_Log("orkige_player: FAILED - could not listen on "
					"127.0.0.1:%u", static_cast<unsigned>(debugPort));
				return 1;
			}
			debugLink.active = true;
			SDL_Log("orkige_player: debug server listening on 127.0.0.1:%u",
				static_cast<unsigned>(debugLink.server.getPort()));
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

		// ORKIGE_DEMO_FRAMES: frame-limit escape for automated runs
		// (0/unset = run until the window is closed or ESC is pressed)
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

			// remote debugging: pump the protocol and act on editor commands
			// BEFORE stepping, so pause/step/set_property apply to this frame
			bool stepOnce = false;
			if (debugLink.active)
			{
				debugLink.server.update();
				if (debugLink.server.consumeClientConnected())
				{
					Orkige::DebugMessage hello(Protocol::MSG_HELLO);
					hello.set(Protocol::FIELD_SCENE, scenePath);
					debugLink.server.send(hello);
					sendHierarchyIfChanged(debugLink, gameObjectManager, true);
					SDL_Log("orkige_player: debug client connected");
				}
				if (debugLink.server.consumeClientDisconnected())
				{
					// a vanished editor must not leave the game frozen
					SDL_Log("orkige_player: debug client disconnected");
					debugLink.paused = false;
					debugLink.pendingSteps = 0;
					debugLink.selectedObjectId.clear();
					debugLink.hierarchySent = false;
				}
				processDebugMessages(debugLink, gameObjectManager);
				if (debugLink.quitRequested)
				{
					running = false;
				}
				if (debugLink.paused && debugLink.pendingSteps > 0)
				{
					stepOnce = true;
					--debugLink.pendingSteps;
				}
			}

			// measured frame dt, clamped to [1/60, 0.1] like the demo: the
			// floor keeps headless runs accumulating simulated time, the cap
			// avoids the catch-up spiral after a stall
			const std::chrono::steady_clock::time_point frameTime =
				std::chrono::steady_clock::now();
			float deltaTime = std::chrono::duration<float>(
				frameTime - lastFrameTime).count();
			lastFrameTime = frameTime;
			deltaTime = std::clamp(deltaTime, 1.0f / 60.0f, 0.1f);
			// pause gates the stepping only - rendering and the debug
			// protocol stay alive; a step is exactly one fixed physics tick
			const bool advanceWorld =
				!debugLink.active || !debugLink.paused || stepOnce;
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
			// selected object state at ~15Hz - also while paused
			if (debugLink.active && debugLink.server.hasClient())
			{
				if (frameCount % HIERARCHY_CHECK_INTERVAL == 0)
				{
					sendHierarchyIfChanged(debugLink, gameObjectManager, false);
				}
				streamObjectState(debugLink, gameObjectManager);
			}

			if (!engine.renderOneFrame())
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
					engine.getRenderWindow()->writeContentsToFile(shotPath);
				}
			}
			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

		// orderly protocol shutdown: tell the editor we are going down (the
		// quit path already sent bye) and give the socket a moment to flush
		if (debugLink.active && debugLink.server.hasClient())
		{
			if (!debugLink.quitRequested)
			{
				debugLink.server.send(Orkige::DebugMessage(Protocol::MSG_BYE));
			}
			for (int flush = 0; flush < 10; ++flush)
			{
				debugLink.server.update();
				SDL_Delay(5);
			}
		}
	}

	SDL_DestroyWindow(window);
	SDL_Quit();
	return exitCode;
}
