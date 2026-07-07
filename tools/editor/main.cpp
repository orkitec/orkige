// orkige_editor - the in-engine editor shell (bootstrap).
//
// A Unity-like editor built as a regular Orkige app: SDL3 owns the window and
// event loop, Orkige::Engine renders into it (externalWindowHandle path, same
// boot sequence as samples/hello_orkige), and the UI is Dear ImGui drawn by
// OGRE's own Overlay integration (Ogre::ImGuiOverlay).
//
// Wiring choices:
// - Ogre::OverlaySystem is owned editor-side: constructed after the Engine
//   (Ogre::Root exists) but before Engine::setup() (Root not yet initialised,
//   as OgreOverlaySystem.h requires), then registered as RenderQueueListener
//   on the Engine SceneManager. The Engine class stays untouched; if a game
//   ever needs overlays this moves into Engine behind an option.
// - Input goes to ImGui first (ImGuiSDL3Input); events ImGui wants captured
//   are NOT forwarded to the engine InputManager, everything else follows the
//   same injectEvent flow as the demo.
// - The UI is a full-window dockspace (docking-enabled imgui): the 3D scene
//   renders offscreen into a RENDER_TARGET texture shown by the "Scene"
//   panel via ImGui::Image, the window itself only carries a UI viewport
//   (dark grey, visibility mask 0). Picking and camera orbit/zoom happen
//   through the Scene panel (drawScenePanel).
#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_input/InputManager.h>
#include <engine_util/NodeUtil.h>
#include <engine_util/PrimitiveUtil.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_debugnet/DebugClient.h>
#include <core_debugnet/DebugServer.h>
#include <core_util/StringUtil.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#ifdef ORKIGE_LUA
#include <core_script/ScriptManager.h>
#endif

#include <OgreOverlaySystem.h>
#include <OgreOverlayManager.h>
#include <OgreImGuiOverlay.h>
#include <OgreEntity.h>
#include <OgreMeshManager.h>
#include <OgreTextureManager.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreRenderTexture.h>
#include <OgreRoot.h>
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder API (programmatic first-run layout)
#include <ImGuizmo.h>

#include "EditorCore.h"
#include "ImGuiSDL3Input.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <signal.h> // ORKIGE_EDITOR_PLAYTEST=crash kills the player with SIGKILL
#endif

extern "C" void* orkige_native_window_handle(SDL_Window* window);

namespace
{

// ESC through the engine input pipeline (SDL event -> InputManager ->
// GlobalEventManager -> listener) - also proves that non-ImGui input still
// reaches the engine. Unity-style: a first ESC clears the selection, ESC
// with nothing selected quits the editor.
struct QuitOnEscape
{
	bool quitRequested = false;
	Orkige::EditorCore* editorCore = nullptr;
	bool onKeyPressed(Orkige::Event const& event)
	{
		if (event.getDataPtr<Orkige::KeyEventData>()->key ==
			Orkige::KeyEventData::KC_ESCAPE)
		{
			if (editorCore && editorCore->hasSelection())
			{
				editorCore->clearSelection();
			}
			else
			{
				quitRequested = true;
			}
		}
		return false;
	}
};

// Editor UI state that lives across frames. Everything UI-independent
// (selection, dirty flag, tools, undo/redo) lives in EditorCore instead.
struct EditorState
{
	bool quitRequested = false;
	//! current scene file (empty = unsaved "untitled" scene); the dirty
	//! marker lives in EditorCore, both are reflected in the window title
	std::string currentScenePath;
	//! "Scene Path" modal (native file dialogs are out of scope for now):
	//! which action the path input confirms
	enum class ScenePathAction { None, SaveAs, Open };
	ScenePathAction scenePathAction = ScenePathAction::None;
	bool openScenePathPopup = false;
	char scenePathInput[1024] = "";
	char luaInput[4096] =
		"-- Lua console (sol2). Example:\n"
		"return Engine.getSingleton():getTopLevelWindowHandle()";
	std::vector<std::string> luaHistory;
	bool luaScrollToBottom = false;
	//! first-frame guard for the DockBuilder default layout
	bool dockLayoutChecked = false;
	//! content size the Scene panel wants for the RTT (recorded while drawing)
	int scenePanelWidth = 0;
	int scenePanelHeight = 0;
	//! RTT resize hysteresis bookkeeping (see the frame loop)
	int pendingRttWidth = 0;
	int pendingRttHeight = 0;
	int pendingRttFrames = 0;
	//! orbit camera: spherical coordinates around orbitTarget; defaults
	//! reproduce the old fixed camera at (0, 2.5, 9) looking at the origin.
	//! F (frame selected) retargets, middle-mouse drag pans the target.
	float orbitYawDeg = 0.0f;
	float orbitPitchDeg = 15.524f;
	float orbitDistance = 9.3408f;
	bool orbitActive = false;
	bool panActive = false;
	Ogre::Vector3 orbitTarget = Ogre::Vector3::ZERO;
	//! Scene panel interaction state recorded while drawing (gates the tool
	//! shortcuts to "Scene panel hovered/focused")
	bool scenePanelHovered = false;
	bool scenePanelFocused = false;
	bool hierarchyFocused = false;
	//! gizmo drag bracketing: the whole drag merges into ONE undo command
	bool gizmoWasUsing = false;
	unsigned int gizmoMergeSession = 0;
	//! inspector drag bracketing (only one drag widget is active at a time)
	unsigned int inspectorMergeSession = 0;
	//! inline rename in the Hierarchy (F2 / double-click / context menu)
	std::string renamingObjectId;
	char renameBuffer[256] = "";
	bool renameFocusPending = false;
};

//--- play mode (remote debugging) -----------------------------------------

namespace Protocol = Orkige::DebugProtocol;

// panel titles: the text before ### changes with the mode ("(Remote)" while
// playing), the id after ### keeps the window/docking identity stable
const char* const HIERARCHY_WINDOW_EDIT = "Scene Hierarchy###SceneHierarchy";
const char* const HIERARCHY_WINDOW_REMOTE =
	"Scene Hierarchy (Remote)###SceneHierarchy";
const char* const INSPECTOR_WINDOW_EDIT = "Inspector###Inspector";
const char* const INSPECTOR_WINDOW_REMOTE = "Inspector (Remote)###Inspector";

// The editor's play mode, Godot-style: Play saves the CURRENT scene to a
// temp file (never the user's file), spawns ./orkige_player <tempScene>
// --debug-port <freeport> (SDL_CreateProcess - the editor already lives on
// SDL3, so no extra platform code) and connects a core_debugnet DebugClient.
// While the session is active the Hierarchy/Inspector panels show the REMOTE
// scene streamed by the player; Stop sends quit and kills the process after
// a grace timeout; a crashed/vanished player is detected via the connection
// drop / process exit and reverts the editor to edit mode. The editor scene
// is never touched by any of this.
struct PlaySession
{
	enum class Mode { Edit, Launching, Playing, Paused, Stopping };
	Mode mode = Mode::Edit;
	SDL_Process* process = nullptr;
	Orkige::DebugClient client;
	unsigned short port = 0;
	std::string tempScenePath;
	//! remote state streamed by the player
	std::string remoteScenePath;
	bool helloReceived = false;
	bool hierarchyReceived = false;
	Orkige::StringVector remoteHierarchy;
	std::string remoteSelectedId;
	std::string stateObjectId;					//!< object of the latest object_state
	Orkige::StringVector stateComponents;		//!< its component type names
	std::map<std::string, std::string> stateProperties;	//!< "<Comp>.<prop>" -> value
	//! timing
	std::chrono::steady_clock::time_point launchStart;
	std::chrono::steady_clock::time_point lastConnectAttempt;
	std::chrono::steady_clock::time_point stopRequestTime;

	bool isActive() const { return this->mode != Mode::Edit; }
};

//! seconds the editor keeps re-connecting while the player engine boots
const int PLAY_CONNECT_TIMEOUT_SECONDS = 30;
//! milliseconds between connect attempts while launching
const int PLAY_CONNECT_RETRY_MS = 250;
//! milliseconds a stopped player gets before it is killed
const int PLAY_STOP_GRACE_MS = 3000;

//! parse exactly count whitespace-separated floats; false on any junk
bool parsePlayFloats(std::string const& text, float* out, int count)
{
	std::istringstream stream(text);
	for (int i = 0; i < count; ++i)
	{
		if (!(stream >> out[i]))
		{
			return false;
		}
	}
	return true;
}

//! format floats with round-trip precision (the wire format for values)
std::string formatPlayFloats(const float* values, int count)
{
	std::string out;
	for (int i = 0; i < count; ++i)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.9g", values[i]);
		if (i > 0)
		{
			out += ' ';
		}
		out += buffer;
	}
	return out;
}

//! forget everything streamed by the (previous) player
void clearRemoteState(PlaySession& session)
{
	session.remoteScenePath.clear();
	session.helloReceived = false;
	session.hierarchyReceived = false;
	session.remoteHierarchy.clear();
	session.remoteSelectedId.clear();
	session.stateObjectId.clear();
	session.stateComponents.clear();
	session.stateProperties.clear();
}

//! @brief tear the session down (reap/kill the player, drop the link,
//! delete the temp scene) and revert to edit mode - the single exit path
//! for Stop, crash detection and editor shutdown
void endPlaySession(PlaySession& session, std::string const& reason)
{
	if (session.process)
	{
		int exitCode = 0;
		if (!SDL_WaitProcess(session.process, false, &exitCode))
		{
			SDL_KillProcess(session.process, true);
			SDL_WaitProcess(session.process, true, &exitCode);
		}
		SDL_DestroyProcess(session.process);
		session.process = nullptr;
	}
	session.client.disconnect();
	if (!session.tempScenePath.empty())
	{
		std::error_code ignored;
		std::filesystem::remove(session.tempScenePath, ignored);
		session.tempScenePath.clear();
	}
	clearRemoteState(session);
	session.mode = PlaySession::Mode::Edit;
	SDL_Log("orkige_editor: play mode ended (%s)", reason.c_str());
}

//! @brief Play: save the current scene to a temp play file, spawn the
//! player with --debug-port on a probed free port and start connecting.
//! The user's scene file and the editor world stay untouched.
bool startPlay(PlaySession& session,
	Orkige::GameObjectManager& gameObjectManager)
{
	if (session.isActive())
	{
		return false;
	}
	// temp play file (never the user's file - saveScene is called directly,
	// EditorState::currentScenePath/sceneDirty are not involved)
	const std::string tempName = "orkige_play_" + std::to_string(
		std::chrono::steady_clock::now().time_since_epoch().count()) +
		".oscene";
	session.tempScenePath =
		(std::filesystem::temp_directory_path() / tempName).string();
	if (!Orkige::SceneSerializer::saveScene(session.tempScenePath,
		gameObjectManager))
	{
		SDL_Log("orkige_editor: play failed - could not save temp scene '%s'",
			session.tempScenePath.c_str());
		session.tempScenePath.clear();
		return false;
	}
	// free localhost port: bind an ephemeral DebugServer, read the port
	// back, close it again (tiny race until the player re-binds it -
	// acceptable for a local dev loop)
	{
		Orkige::DebugServer portProbe;
		if (!portProbe.start(0))
		{
			SDL_Log("orkige_editor: play failed - no free debug port");
			endPlaySession(session, "port probe failed");
			return false;
		}
		session.port = portProbe.getPort();
	}
	// spawn the player next to this build (ORKIGE_EDITOR_PLAYER_PATH is
	// baked in by CMake as $<TARGET_FILE:orkige_player>). SDL's process API
	// keeps the editor free of platform spawn code; stdio stays inherited so
	// the player log shows up in the editor console. The automation env
	// hooks aimed at THIS editor process (frame caps, screenshot paths) must
	// not leak into the player - it honours the same variables and would
	// e.g. exit after N frames or overwrite the requested screenshot.
	const std::string portString = std::to_string(session.port);
	const char* args[] = { ORKIGE_EDITOR_PLAYER_PATH,
		session.tempScenePath.c_str(), "--debug-port", portString.c_str(),
		nullptr };
	SDL_Environment* playerEnvironment = SDL_CreateEnvironment(true);
	SDL_UnsetEnvironmentVariable(playerEnvironment, "ORKIGE_DEMO_FRAMES");
	SDL_UnsetEnvironmentVariable(playerEnvironment, "ORKIGE_DEMO_SCREENSHOT");
	SDL_PropertiesID spawnProperties = SDL_CreateProperties();
	SDL_SetPointerProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
		const_cast<char**>(args));
	SDL_SetPointerProperty(spawnProperties,
		SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, playerEnvironment);
	session.process = SDL_CreateProcessWithProperties(spawnProperties);
	SDL_DestroyProperties(spawnProperties);
	SDL_DestroyEnvironment(playerEnvironment);
	if (!session.process)
	{
		SDL_Log("orkige_editor: play failed - SDL_CreateProcess '%s': %s",
			ORKIGE_EDITOR_PLAYER_PATH, SDL_GetError());
		endPlaySession(session, "spawn failed");
		return false;
	}
	clearRemoteState(session);
	session.mode = PlaySession::Mode::Launching;
	session.launchStart = std::chrono::steady_clock::now();
	session.lastConnectAttempt = std::chrono::steady_clock::time_point();
	SDL_Log("orkige_editor: play - spawned player (scene '%s', port %u)",
		session.tempScenePath.c_str(), static_cast<unsigned>(session.port));
	return true;
}

//! Stop: ask the player to quit; updatePlaySession reaps it (or kills it
//! after the grace timeout)
void requestStopPlay(PlaySession& session)
{
	if (!session.isActive() || session.mode == PlaySession::Mode::Stopping)
	{
		return;
	}
	if (session.client.isConnected())
	{
		session.client.send(Orkige::DebugMessage(Protocol::MSG_QUIT));
	}
	session.mode = PlaySession::Mode::Stopping;
	session.stopRequestTime = std::chrono::steady_clock::now();
	SDL_Log("orkige_editor: play - stop requested");
}

//! remote selection: remember it and tell the player what to stream
void selectRemoteObject(PlaySession& session, std::string const& id)
{
	session.remoteSelectedId = id;
	Orkige::DebugMessage select(Protocol::MSG_SELECT);
	select.set(Protocol::FIELD_ID, id);
	session.client.send(select);
}

//! per-frame play session pump: complete the connect, drain streamed
//! messages, watch the process and the link, enforce the stop grace timeout
void updatePlaySession(PlaySession& session)
{
	if (!session.isActive())
	{
		return;
	}
	session.client.update();
	Orkige::DebugMessage message;
	while (session.client.receive(message))
	{
		if (message.type == Protocol::MSG_HELLO)
		{
			session.helloReceived = true;
			session.remoteScenePath = message.get(Protocol::FIELD_SCENE);
			if (message.version != Protocol::VERSION)
			{
				SDL_Log("orkige_editor: play - protocol version mismatch "
					"(player %d, editor %d)", message.version,
					Protocol::VERSION);
			}
		}
		else if (message.type == Protocol::MSG_HIERARCHY)
		{
			session.remoteHierarchy = message.getList(Protocol::LIST_IDS);
			session.hierarchyReceived = true;
		}
		else if (message.type == Protocol::MSG_OBJECT_STATE)
		{
			session.stateObjectId = message.get(Protocol::FIELD_ID);
			session.stateComponents = message.getList(Protocol::LIST_COMPONENTS);
			session.stateProperties.clear();
			for (auto const& [key, value] : message.fields)
			{
				if (key != Protocol::FIELD_ID)
				{
					session.stateProperties[key] = value;
				}
			}
		}
		else if (message.type == Protocol::MSG_LOG ||
			message.type == Protocol::MSG_ERROR)
		{
			SDL_Log("orkige_editor: play - remote %s: %s",
				message.type.c_str(),
				message.get(Protocol::FIELD_MESSAGE).c_str());
		}
		else if (message.type == Protocol::MSG_BYE)
		{
			SDL_Log("orkige_editor: play - player said bye");
		}
	}
	int exitCode = 0;
	const bool processExited = session.process &&
		SDL_WaitProcess(session.process, false, &exitCode);
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	switch (session.mode)
	{
	case PlaySession::Mode::Launching:
		if (processExited)
		{
			endPlaySession(session, "player exited during launch (code " +
				std::to_string(exitCode) + ")");
			return;
		}
		if (session.client.isConnected())
		{
			session.mode = PlaySession::Mode::Playing;
			SDL_Log("orkige_editor: play - connected to the player");
			return;
		}
		// the player needs a few seconds to boot before it listens: keep
		// re-connecting (a refused attempt ends in Failed, not Connecting)
		if (!session.client.isConnecting() &&
			now - session.lastConnectAttempt >
				std::chrono::milliseconds(PLAY_CONNECT_RETRY_MS))
		{
			session.client.connect("127.0.0.1", session.port);
			session.lastConnectAttempt = now;
		}
		if (now - session.launchStart >
			std::chrono::seconds(PLAY_CONNECT_TIMEOUT_SECONDS))
		{
			endPlaySession(session, "could not connect to the player");
		}
		return;
	case PlaySession::Mode::Playing:
	case PlaySession::Mode::Paused:
		// crash resilience: a vanished process or a dropped link reverts
		// the editor to edit mode cleanly
		if (processExited)
		{
			endPlaySession(session, "player process exited unexpectedly "
				"(code " + std::to_string(exitCode) + ")");
			return;
		}
		if (!session.client.isConnected())
		{
			endPlaySession(session, "debug link dropped unexpectedly");
		}
		return;
	case PlaySession::Mode::Stopping:
		if (processExited)
		{
			endPlaySession(session, "stopped");
			return;
		}
		if (now - session.stopRequestTime >
			std::chrono::milliseconds(PLAY_STOP_GRACE_MS))
		{
			SDL_Log("orkige_editor: play - player ignored quit, killing it");
			endPlaySession(session, "stopped (killed after grace timeout)");
		}
		return;
	case PlaySession::Mode::Edit:
		return;
	}
}

// The offscreen scene render target: the editor's scene camera renders into a
// manual RENDER_TARGET texture whose viewport replaces the old whole-window
// scene viewport, and the Scene panel displays that texture via ImGui::Image.
// OGRE's ImGuiOverlay resolves any nonzero ImTextureID through
// TextureManager::getByHandle (OgreImGuiOverlay.cpp, ImGUIRenderable::
// preRender), so the texture's resource handle doubles as the ImGui texture
// id - the same pattern as the font atlas SetTexID in main().
struct SceneRenderTarget
{
	Ogre::TexturePtr texture;
	Ogre::Camera* camera = nullptr;
	int width = 0;
	int height = 0;
};

// (re)create the scene RTT at the given size; the old texture (if any) is
// destroyed first - the ImGui overlay resolves texture ids per draw call, so
// a vanished handle degrades gracefully for the frame it could still be seen
void createSceneRenderTexture(SceneRenderTarget& target, int width, int height)
{
	if (target.texture)
	{
		target.texture->getBuffer()->getRenderTarget()->removeAllViewports();
		Ogre::TextureManager::getSingleton().remove(target.texture);
		target.texture.reset();
	}
	target.texture = Ogre::TextureManager::getSingleton().createManual(
		"EditorSceneRT", Ogre::RGN_INTERNAL, Ogre::TEX_TYPE_2D,
		static_cast<Ogre::uint>(width), static_cast<Ogre::uint>(height), 0,
		Ogre::PF_BYTE_RGB, Ogre::TU_RENDERTARGET);
	Ogre::RenderTarget* renderTarget =
		target.texture->getBuffer()->getRenderTarget();
	Ogre::Viewport* viewport = renderTarget->addViewport(target.camera);
	viewport->setBackgroundColour(Ogre::ColourValue::Blue);
	viewport->setShadowsEnabled(true);
	// the ImGui overlay must only ever render into the window, not the RTT
	// (OverlaySystem also skips RTT passes on its own, this is belt+braces)
	viewport->setOverlaysEnabled(false);
#ifdef USE_RTSHADER_SYSTEM
	// same RTSS wiring Engine::createDefaultCameraAndViewport applied to the
	// old window viewport - without it nothing renders on GL3+ (no FFP)
	if (!Ogre::Root::getSingleton().getRenderSystem()->getCapabilities()
		->hasCapability(Ogre::RSC_FIXED_FUNCTION))
	{
		viewport->setMaterialScheme(
			Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
	}
#endif
	target.camera->setAspectRatio(
		static_cast<Ogre::Real>(width) / static_cast<Ogre::Real>(height));
	target.width = width;
	target.height = height;
}

// place the scene camera on its orbit sphere around the orbit target
void applyOrbitCamera(EditorState const& state, Ogre::SceneNode* cameraNode)
{
	const Ogre::Radian yaw(Ogre::Degree(state.orbitYawDeg));
	const Ogre::Radian pitch(Ogre::Degree(state.orbitPitchDeg));
	const Ogre::Vector3 offset(
		Ogre::Math::Cos(pitch) * Ogre::Math::Sin(yaw),
		Ogre::Math::Sin(pitch),
		Ogre::Math::Cos(pitch) * Ogre::Math::Cos(yaw));
	cameraNode->setPosition(state.orbitTarget + offset * state.orbitDistance);
	cameraNode->lookAt(state.orbitTarget, Ogre::Node::TS_WORLD);
}

// F: frame the selected object - retarget the orbit to the object's world
// bounds centre and fit the orbit distance to its bounding radius
void frameSelectedObject(EditorState& state, Orkige::EditorCore& core,
	Ogre::Camera* camera)
{
	if (!core.hasSelection())
	{
		return;
	}
	optr<Orkige::GameObject> gameObject = core.getGameObjectManager()
		.getGameObject(core.getSelectedObjectId()).lock();
	if (!gameObject ||
		!gameObject->hasComponent<Orkige::TransformComponent>())
	{
		return;
	}
	Orkige::TransformComponent* transform =
		gameObject->getComponentPtr<Orkige::TransformComponent>();
	Ogre::Vector3 center = transform->getWorldPosition();
	float radius = 1.0f;
	const Ogre::AxisAlignedBox& box = transform->getWorldAABB();
	if (box.isFinite() && !box.isNull())
	{
		center = box.getCenter();
		radius = std::max(box.getHalfSize().length(), 0.25f);
	}
	state.orbitTarget = center;
	const float halfFov = std::min(
		camera->getFOVy().valueRadians() * 0.5f, 1.2f);
	state.orbitDistance = std::clamp(
		radius / std::sin(halfFov) * 1.25f, 2.0f, 200.0f);
}

// ModelComponent does not serialize material tweaks (yet), so re-apply the
// unlit vertex-colour render state to every model after a scene load
void applyUnlitFixToLoadedModels(Orkige::EditorCore& core)
{
	for (auto const& [id, gameObject] :
		core.getGameObjectManager().getGameObjects())
	{
		core.applyModelFixups(id);
	}
}

// viewport click-picking: cast a camera ray through the click point and
// select the nearest hit that belongs to a GameObject (a TransformComponent
// tags its scene nodes, NodeUtil walks a hit back to the owner). AABB-level
// picking is right for the editor bootstrap; polygon-accurate picking via
// CollisionTools comes when entities with real meshes arrive.
bool pickObjectAtCursor(Orkige::EditorCore& core, Ogre::Camera* camera,
	Ogre::SceneManager* sceneManager, float normalizedX, float normalizedY)
{
	const Ogre::Ray ray =
		camera->getCameraToViewportRay(normalizedX, normalizedY);
	Ogre::RaySceneQuery* query = sceneManager->createRayQuery(ray);
	query->setSortByDistance(true);
	bool picked = false;
	for (Ogre::RaySceneQueryResultEntry const& hit : query->execute())
	{
		if (!hit.movable || !hit.movable->getParentSceneNode())
		{
			continue;
		}
		Orkige::GameObject* gameObject = Orkige::NodeUtil::getGameObjectFromNode(
			hit.movable->getParentSceneNode());
		if (gameObject)
		{
			core.selectObject(gameObject->getObjectID());
			picked = true;
			break;
		}
	}
	sceneManager->destroyQuery(query);
	if (!picked)
	{
		// clicking empty space deselects, like Unity
		core.clearSelection();
	}
	return picked;
}

// project a world position to viewport-normalized coordinates (0..1,
// top-left origin - what getCameraToViewportRay expects); returns false if
// the position is behind the camera
bool worldToViewportNormalized(Ogre::Camera* camera,
	Ogre::Vector3 const& worldPos, float& outX, float& outY)
{
	const Ogre::Vector4 clip = camera->getProjectionMatrix() *
		(camera->getViewMatrix() * Ogre::Vector4(worldPos.x, worldPos.y,
			worldPos.z, 1.0f));
	if (clip.w <= 0.0f)
	{
		return false;
	}
	outX = clip.x / clip.w * 0.5f + 0.5f;
	outY = 1.0f - (clip.y / clip.w * 0.5f + 0.5f);
	return true;
}

// File > New Scene: clear all GameObjects - removing the components tears
// down their scene nodes (TransformComponent::onRemove wipes via NodeUtil)
void newScene(EditorState& state, Orkige::EditorCore& core)
{
	core.getGameObjectManager().clear();
	core.resetForScene();
	state.currentScenePath.clear();
}

bool saveSceneToPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	if (!Orkige::SceneSerializer::saveScene(path, gameObjectManager))
	{
		SDL_Log("orkige_editor: saving scene '%s' failed", path.c_str());
		return false;
	}
	SDL_Log("orkige_editor: scene saved to '%s' (%zu GameObjects)",
		path.c_str(), gameObjectManager.getGameObjects().size());
	state.currentScenePath = path;
	core.clearSceneDirty();
	return true;
}

bool openSceneFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	// loadScene replaces the current world (clears the manager first); the
	// undo history refers to the old world, so it goes too
	core.resetForScene();
	if (!Orkige::SceneSerializer::loadScene(path, gameObjectManager))
	{
		SDL_Log("orkige_editor: opening scene '%s' failed", path.c_str());
		return false;
	}
	applyUnlitFixToLoadedModels(core);
	SDL_Log("orkige_editor: scene opened from '%s' (%zu GameObjects)",
		path.c_str(), gameObjectManager.getGameObjects().size());
	state.currentScenePath = path;
	return true;
}

// selfcheck helper: compute the viewport-normalized Scene-panel position of
// a GameObject from the RTT camera and run it through pickObjectAtCursor -
// the same function the Scene panel's mouse path calls (the panel image
// fills the panel content region, so panel-relative and viewport-normalized
// coordinates coincide). Returns false if the object is missing or behind
// the camera.
bool pickGameObjectThroughScenePanel(Orkige::EditorCore& core,
	Orkige::GameObjectManager& gameObjectManager, Ogre::Camera* camera,
	Ogre::SceneManager* sceneManager, std::string const& id)
{
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.getGameObject(id).lock();
	float normalizedX = 0.0f;
	float normalizedY = 0.0f;
	if (!gameObject || !worldToViewportNormalized(camera,
		gameObject->getComponentPtr<Orkige::TransformComponent>()
			->getPosition(),
		normalizedX, normalizedY))
	{
		return false;
	}
	pickObjectAtCursor(core, camera, sceneManager, normalizedX, normalizedY);
	return true;
}

#ifdef ORKIGE_LUA
// run the console buffer through the ScriptManager, capture returns/errors
void runLuaConsoleInput(EditorState& state, Orkige::ScriptManager& scriptManager)
{
	std::string code(state.luaInput);
	state.luaHistory.push_back("lua> " + code);
	sol::state& lua = scriptManager.state();
	sol::protected_function_result result =
		lua.safe_script(code, sol::script_pass_on_error);
	if (!result.valid())
	{
		const sol::error error = result;
		state.luaHistory.push_back(std::string("error: ") + error.what());
	}
	else if (result.return_count() == 0)
	{
		state.luaHistory.push_back("ok");
	}
	else
	{
		std::string out;
		for (int i = 0; i < result.return_count(); ++i)
		{
			if (i > 0)
			{
				out += "\t";
			}
			const std::string value =
				lua["tostring"](result.get<sol::object>(i));
			out += value;
		}
		state.luaHistory.push_back(out);
	}
	state.luaScrollToBottom = true;
}
#endif

// open the "Scene Path" modal preloaded with a sensible path (the current
// scene, or a default inside samples/scenes/)
void requestScenePathPopup(EditorState& state,
	EditorState::ScenePathAction action)
{
	state.scenePathAction = action;
	state.openScenePathPopup = true;
	const std::string defaultPath = state.currentScenePath.empty()
		? std::string(ORKIGE_EDITOR_SCENE_DIR "/scene.oscene")
		: state.currentScenePath;
	SDL_strlcpy(state.scenePathInput, defaultPath.c_str(),
		sizeof(state.scenePathInput));
}

// modifier label for the menu shortcut column (the handler accepts both
// Super and Ctrl either way)
#ifdef __APPLE__
#define ORKIGE_EDITOR_MOD_LABEL "Cmd"
#else
#define ORKIGE_EDITOR_MOD_LABEL "Ctrl"
#endif

void startRenameSelected(EditorState& state, Orkige::EditorCore& core)
{
	if (!core.hasSelection())
	{
		return;
	}
	state.renamingObjectId = core.getSelectedObjectId();
	SDL_strlcpy(state.renameBuffer, state.renamingObjectId.c_str(),
		sizeof(state.renameBuffer));
	state.renameFocusPending = true;
}

void drawMenuBar(EditorState& state, Orkige::EditorCore& core)
{
	bool openAbout = false;
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
			{
				newScene(state, core);
			}
			if (ImGui::MenuItem("Open Scene..."))
			{
				requestScenePathPopup(state,
					EditorState::ScenePathAction::Open);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Save Scene"))
			{
				if (state.currentScenePath.empty())
				{
					requestScenePathPopup(state,
						EditorState::ScenePathAction::SaveAs);
				}
				else
				{
					saveSceneToPath(state, core, state.currentScenePath);
				}
			}
			if (ImGui::MenuItem("Save Scene As..."))
			{
				requestScenePathPopup(state,
					EditorState::ScenePathAction::SaveAs);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Quit", "Esc"))
			{
				state.quitRequested = true;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			// undo/redo labels carry the description of the command they
			// would apply ("Undo Delete Cube1")
			const std::string undoLabel = core.canUndo()
				? "Undo " + core.getUndoDescription() : std::string("Undo");
			if (ImGui::MenuItem(undoLabel.c_str(),
				ORKIGE_EDITOR_MOD_LABEL "+Z", false, core.canUndo()))
			{
				core.undo();
			}
			const std::string redoLabel = core.canRedo()
				? "Redo " + core.getRedoDescription() : std::string("Redo");
			if (ImGui::MenuItem(redoLabel.c_str(),
				"Shift+" ORKIGE_EDITOR_MOD_LABEL "+Z", false, core.canRedo()))
			{
				core.redo();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate", ORKIGE_EDITOR_MOD_LABEL "+D",
				false, core.hasSelection()))
			{
				core.duplicateSelected();
			}
			if (ImGui::MenuItem("Rename", "F2", false, core.hasSelection()))
			{
				startRenameSelected(state, core);
			}
			if (ImGui::MenuItem("Delete", "Del", false, core.hasSelection()))
			{
				core.deleteSelected();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Create Cube"))
			{
				core.createCube();
			}
			if (ImGui::MenuItem("Create Test Mesh"))
			{
				core.createTestMesh();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About"))
			{
				openAbout = true;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
	if (openAbout)
	{
		ImGui::OpenPopup("About Orkige Editor");
	}
	if (ImGui::BeginPopupModal("About Orkige Editor", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Orkige Editor");
		ImGui::Separator();
		ImGui::Text("orkige - the orkitec game engine, version %s",
			ORKIGE_EDITOR_VERSION);
		ImGui::Text("OGRE %d.%d.%d", OGRE_VERSION_MAJOR, OGRE_VERSION_MINOR,
			OGRE_VERSION_PATCH);
		ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
		ImGui::Spacing();
		if (ImGui::Button("Close"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	// "Scene Path" modal: a plain path input standing in for a native file
	// dialog (out of scope for now); confirms the pending SaveAs/Open action
	if (state.openScenePathPopup)
	{
		ImGui::OpenPopup("Scene Path");
		state.openScenePathPopup = false;
	}
	if (ImGui::BeginPopupModal("Scene Path", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		const bool saving =
			(state.scenePathAction == EditorState::ScenePathAction::SaveAs);
		ImGui::TextUnformatted(saving ? "Save scene as (.oscene):"
			: "Open scene (.oscene):");
		ImGui::SetNextItemWidth(620.0f);
		ImGui::InputText("##scenepath", state.scenePathInput,
			sizeof(state.scenePathInput));
		if (ImGui::Button(saving ? "Save" : "Open"))
		{
			const std::string path(state.scenePathInput);
			if (!path.empty())
			{
				if (saving)
				{
					saveSceneToPath(state, core, path);
				}
				else
				{
					openSceneFromPath(state, core, path);
				}
			}
			state.scenePathAction = EditorState::ScenePathAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			state.scenePathAction = EditorState::ScenePathAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// The play toolbar strip: a fixed window at the top of the work area (under
// the main menu bar, above the dockspace) carrying Play/Pause(Resume)/Step/
// Stop with state-appropriate enabling plus a session status line. Returns
// the height the dockspace below must leave free.
float drawToolbar(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core)
{
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	const float toolbarHeight = ImGui::GetFrameHeight() +
		ImGui::GetStyle().WindowPadding.y * 2.0f;
	ImGui::SetNextWindowPos(mainViewport->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, toolbarHeight));
	if (ImGui::Begin("##PlayToolbar", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus))
	{
		const PlaySession::Mode mode = session.mode;
		ImGui::BeginDisabled(mode != PlaySession::Mode::Edit);
		if (ImGui::Button("Play"))
		{
			startPlay(session, gameObjectManager);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (mode == PlaySession::Mode::Paused)
		{
			if (ImGui::Button("Resume"))
			{
				session.client.send(
					Orkige::DebugMessage(Protocol::MSG_RESUME));
				session.mode = PlaySession::Mode::Playing;
			}
		}
		else
		{
			ImGui::BeginDisabled(mode != PlaySession::Mode::Playing);
			if (ImGui::Button("Pause"))
			{
				session.client.send(Orkige::DebugMessage(Protocol::MSG_PAUSE));
				session.mode = PlaySession::Mode::Paused;
			}
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(mode != PlaySession::Mode::Paused);
		if (ImGui::Button("Step"))
		{
			session.client.send(Orkige::DebugMessage(Protocol::MSG_STEP));
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!session.isActive() ||
			mode == PlaySession::Mode::Stopping);
		if (ImGui::Button("Stop"))
		{
			requestStopPlay(session);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();
		// the tool strip: Q/W/E/R, world/local space, snap toggle - the
		// buttons call the exact functions the keyboard shortcuts invoke
		ImGui::BeginDisabled(session.isActive());
		auto toolButton = [&core](char const* label, Orkige::EditorTool tool)
		{
			const bool active = (core.getActiveTool() == tool);
			if (active)
			{
				ImGui::PushStyleColor(ImGuiCol_Button,
					ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			if (ImGui::Button(label))
			{
				core.setActiveTool(tool);
			}
			if (active)
			{
				ImGui::PopStyleColor();
			}
			ImGui::SameLine();
		};
		toolButton("Q", Orkige::EditorTool::Select);
		toolButton("W", Orkige::EditorTool::Translate);
		toolButton("E", Orkige::EditorTool::Rotate);
		toolButton("R", Orkige::EditorTool::Scale);
		if (ImGui::Button(core.getTransformSpace() ==
			Orkige::EditorTransformSpace::World ? "World" : "Local"))
		{
			core.toggleTransformSpace();
		}
		ImGui::SameLine();
		bool snapEnabled = core.isSnapEnabled();
		if (ImGui::Checkbox("Snap", &snapEnabled))
		{
			core.setSnapEnabled(snapEnabled);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%.1f / %.0f\xC2\xB0 / %.1f)",
			Orkige::EditorCore::SNAP_TRANSLATE,
			Orkige::EditorCore::SNAP_ROTATE_DEGREES,
			Orkige::EditorCore::SNAP_SCALE);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();
		switch (mode)
		{
		case PlaySession::Mode::Edit:
			ImGui::TextDisabled("editing");
			break;
		case PlaySession::Mode::Launching:
			ImGui::TextUnformatted("launching player...");
			break;
		case PlaySession::Mode::Playing:
			ImGui::Text("PLAYING (remote: %zu objects)",
				session.remoteHierarchy.size());
			break;
		case PlaySession::Mode::Paused:
			ImGui::Text("PAUSED (remote: %zu objects)",
				session.remoteHierarchy.size());
			break;
		case PlaySession::Mode::Stopping:
			ImGui::TextUnformatted("stopping...");
			break;
		}
	}
	ImGui::End();
	(void)state;
	return toolbarHeight;
}

// Dockspace filling the work area below the toolbar strip. The first run
// builds the default layout programmatically with the DockBuilder:
// Hierarchy left, Inspector right, Lua Console + Stats tabbed at the
// bottom, Scene panel filling the centre. Afterwards the layout persists
// through imgui.ini (stored next to the executable, see main()) and the
// builder stays out of the way.
void drawDockspace(EditorState& state, float toolbarHeight)
{
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x,
		mainViewport->WorkPos.y + toolbarHeight));
	const ImVec2 hostSize(mainViewport->WorkSize.x,
		mainViewport->WorkSize.y - toolbarHeight);
	ImGui::SetNextWindowSize(hostSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##EditorDockHost", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground);
	ImGui::PopStyleVar(3);
	const ImGuiID dockspaceId = ImGui::GetID("OrkigeEditorDockspace");
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	ImGui::End();
	if (state.dockLayoutChecked ||
		hostSize.x <= 0.0f || hostSize.y <= 0.0f)
	{
		// the very first frame has no display size yet (ImGuiOverlay derives
		// it from the not-yet-rendered overlay viewport) - try again next
		// frame, DockBuilderSetNodeSize needs a real size
		return;
	}
	state.dockLayoutChecked = true;
	ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId);
	if (rootNode && rootNode->IsSplitNode())
	{
		return; // imgui.ini restored a layout - keep it
	}
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, hostSize);
	ImGuiID centerId = dockspaceId;
	ImGuiID leftId = 0;
	ImGuiID rightId = 0;
	ImGuiID bottomId = 0;
	ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.20f,
		&leftId, &centerId);
	ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.30f,
		&rightId, &centerId);
	ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.30f,
		&bottomId, &centerId);
	ImGui::DockBuilderDockWindow(HIERARCHY_WINDOW_EDIT, leftId);
	ImGui::DockBuilderDockWindow(INSPECTOR_WINDOW_EDIT, rightId);
	ImGui::DockBuilderDockWindow("Lua Console", bottomId);
	ImGui::DockBuilderDockWindow("Stats", bottomId);
	ImGui::DockBuilderDockWindow("Scene", centerId);
	ImGui::DockBuilderFinish(dockspaceId);
}

// OGRE Matrix4 stores row-major (m[row][col]); ImGuizmo expects the usual
// OpenGL-style column-major float16 - copying transposed converts between
// the two (both directions).
void ogreToImGuizmo(Ogre::Matrix4 const& matrix, float* out16)
{
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			out16[col * 4 + row] = matrix[row][col];
		}
	}
}

Ogre::Matrix4 imGuizmoToOgre(const float* in16)
{
	Ogre::Matrix4 matrix;
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			matrix[row][col] = in16[col * 4 + row];
		}
	}
	return matrix;
}

// The transform gizmo over the Scene panel image: ImGuizmo draws into the
// panel's drawlist (SetDrawlist/SetRect on the image screen rect) with the
// RTT camera's view/projection. A whole drag collapses into ONE undo command
// (merge session opened on drag start, closed on release). Returns true if
// the gizmo owns the mouse (hovered or dragging) - the click-to-pick path
// must stand down then.
bool drawSceneGizmo(EditorState& state, Orkige::EditorCore& core,
	Ogre::Camera* camera, ImVec2 const& rectMin, ImVec2 const& rectSize)
{
	const Orkige::EditorTool tool = core.getActiveTool();
	Orkige::EditorTransform current;
	if (tool == Orkige::EditorTool::Select || !core.hasSelection() ||
		!core.getObjectTransform(core.getSelectedObjectId(), current))
	{
		state.gizmoWasUsing = false;
		return false;
	}
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(rectMin.x, rectMin.y, rectSize.x, rectSize.y);

	float view[16];
	float projection[16];
	float model[16];
	ogreToImGuizmo(camera->getViewMatrix(), view);
	ogreToImGuizmo(camera->getProjectionMatrix(), projection);
	Ogre::Matrix4 modelMatrix;
	modelMatrix.makeTransform(current.position, current.scale,
		current.orientation);
	ogreToImGuizmo(modelMatrix, model);

	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	float snapValues[3] = { Orkige::EditorCore::SNAP_TRANSLATE,
		Orkige::EditorCore::SNAP_TRANSLATE,
		Orkige::EditorCore::SNAP_TRANSLATE };
	if (tool == Orkige::EditorTool::Rotate)
	{
		operation = ImGuizmo::ROTATE;
		snapValues[0] = snapValues[1] = snapValues[2] =
			Orkige::EditorCore::SNAP_ROTATE_DEGREES;
	}
	else if (tool == Orkige::EditorTool::Scale)
	{
		operation = ImGuizmo::SCALE;
		snapValues[0] = snapValues[1] = snapValues[2] =
			Orkige::EditorCore::SNAP_SCALE;
	}
	// scale is always object-local; translate/rotate follow the X toggle
	const ImGuizmo::MODE mode = (operation != ImGuizmo::SCALE &&
		core.getTransformSpace() == Orkige::EditorTransformSpace::World)
		? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	// snap: toolbar toggle, or held Cmd/Ctrl while dragging
	ImGuiIO& io = ImGui::GetIO();
	const bool snapActive = core.isSnapEnabled() || io.KeySuper || io.KeyCtrl;

	const bool changed = ImGuizmo::Manipulate(view, projection, operation,
		mode, model, nullptr, snapActive ? snapValues : nullptr);
	if (ImGuizmo::IsUsing())
	{
		if (!state.gizmoWasUsing)
		{
			// drag start: everything until release merges into one command
			state.gizmoMergeSession = core.beginMergeSession();
			state.gizmoWasUsing = true;
		}
		if (changed)
		{
			Orkige::EditorTransform after;
			// gizmo output is affine (no shear) - decompose back to
			// position/scale/orientation (Affine3 extracts the 3x4 part)
			Ogre::Affine3(imGuizmoToOgre(model)).decomposition(
				after.position, after.scale, after.orientation);
			core.applyTransformChange(core.getSelectedObjectId(), current,
				after, state.gizmoMergeSession);
		}
	}
	else if (state.gizmoWasUsing)
	{
		state.gizmoWasUsing = false; // drag ended - next drag = new undo step
	}
	return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

// The Scene panel: displays the offscreen scene texture, records the size
// the RTT should have (applied with hysteresis in the frame loop) and hosts
// the in-panel interactions - the transform gizmo (input priority), left
// click picks (panel-relative mouse coords map 1:1 to viewport-normalized
// coords because the image always fills the content region), right-drag
// orbits, middle-drag pans, scroll zooms.
void drawScenePanel(EditorState& state, Orkige::EditorCore& core,
	bool editMode, SceneRenderTarget& sceneTarget,
	Ogre::SceneManager* sceneManager, Ogre::SceneNode* cameraNode)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin("Scene");
	ImGui::PopStyleVar();
	state.scenePanelHovered = false;
	state.scenePanelFocused = open && ImGui::IsWindowFocused();
	if (open)
	{
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		state.scenePanelWidth = static_cast<int>(avail.x);
		state.scenePanelHeight = static_cast<int>(avail.y);
		if (sceneTarget.texture && avail.x >= 1.0f && avail.y >= 1.0f)
		{
			// the texture handle is the ImGui texture id (resolved back via
			// TextureManager::getByHandle inside Ogre::ImGuiOverlay)
			ImGui::Image(
				static_cast<ImTextureID>(sceneTarget.texture->getHandle()),
				avail);
			const ImVec2 rectMin = ImGui::GetItemRectMin();
			state.scenePanelHovered = ImGui::IsItemHovered();
			// gizmo first: while it is hovered/dragged the click-pick and
			// the camera drags stand down (input priority). Editing the
			// local scene is pointless while the panels show the remote one.
			const bool gizmoOwnsMouse = editMode &&
				drawSceneGizmo(state, core, sceneTarget.camera, rectMin, avail);
			ImGuiIO& io = ImGui::GetIO();
			if (state.scenePanelHovered && !gizmoOwnsMouse)
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					pickObjectAtCursor(core, sceneTarget.camera, sceneManager,
						(io.MousePos.x - rectMin.x) / avail.x,
						(io.MousePos.y - rectMin.y) / avail.y);
				}
				if (io.MouseWheel != 0.0f)
				{
					// scroll up zooms in
					state.orbitDistance = std::clamp(state.orbitDistance *
						std::pow(0.9f, io.MouseWheel), 2.0f, 200.0f);
				}
				if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					state.orbitActive = true;
				}
				if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				{
					state.panActive = true;
				}
			}
			// orbit/pan drags keep going while the button is held, even when
			// the cursor leaves the panel mid-drag
			if (state.orbitActive)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					state.orbitActive = false;
				}
				else
				{
					state.orbitYawDeg -= io.MouseDelta.x * 0.4f;
					state.orbitPitchDeg = std::clamp(
						state.orbitPitchDeg + io.MouseDelta.y * 0.4f,
						-85.0f, 85.0f);
				}
			}
			if (state.panActive)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				{
					state.panActive = false;
				}
				else
				{
					// slide the orbit target along the camera plane; the
					// factor scales with distance so a screen-pixel drag
					// moves the scene about the same visual amount
					const float panScale = state.orbitDistance * 0.0015f;
					state.orbitTarget += cameraNode->getOrientation() *
						Ogre::Vector3(-io.MouseDelta.x * panScale,
							io.MouseDelta.y * panScale, 0.0f);
				}
			}
			applyOrbitCamera(state, cameraNode);
		}
	}
	ImGui::End();
}

// commit/cancel handling for the inline rename field in the Hierarchy
void drawHierarchyRenameField(EditorState& state, Orkige::EditorCore& core,
	std::string const& id)
{
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (state.renameFocusPending)
	{
		ImGui::SetKeyboardFocusHere();
		state.renameFocusPending = false;
	}
	const bool commit = ImGui::InputText("##rename", state.renameBuffer,
		sizeof(state.renameBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_AutoSelectAll);
	if (commit)
	{
		const Orkige::EditorCore::NameValidation validation =
			core.validateRename(id, state.renameBuffer);
		if (validation == Orkige::EditorCore::NameValidation::Ok)
		{
			core.renameObject(id, state.renameBuffer);
			state.renamingObjectId.clear();
		}
		else if (validation ==
			Orkige::EditorCore::NameValidation::Unchanged)
		{
			state.renamingObjectId.clear(); // no-op rename = cancel
		}
		else
		{
			// empty/duplicate rejected: stay in edit so the user can fix it
			state.renameFocusPending = true;
		}
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
		ImGui::IsItemDeactivated())
	{
		state.renamingObjectId.clear(); // focus lost / ESC cancels
	}
}

// Hierarchy panel: the local scene while editing; during play it switches to
// the REMOTE hierarchy streamed by the player ("(Remote)" in the title) and
// clicking an entry sends select so the player streams that object's state.
// Edit mode extras: double-click (or F2/context menu) renames inline,
// right-click opens Duplicate/Rename/Delete (per object) or Create Cube/
// Create Test Mesh (empty space), up/down arrows move the selection.
void drawHierarchyPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core)
{
	const bool remote = session.isActive();
	const bool open =
		ImGui::Begin(remote ? HIERARCHY_WINDOW_REMOTE : HIERARCHY_WINDOW_EDIT);
	state.hierarchyFocused = open && !remote && ImGui::IsWindowFocused();
	if (open)
	{
		if (remote)
		{
			if (!session.hierarchyReceived)
			{
				ImGui::TextDisabled("waiting for the player...");
			}
			else
			{
				ImGui::TextDisabled("remote: %s",
					session.remoteScenePath.c_str());
				ImGui::Separator();
				for (std::string const& id : session.remoteHierarchy)
				{
					const bool selected = (session.remoteSelectedId == id);
					if (ImGui::Selectable(id.c_str(), selected) && !selected)
					{
						selectRemoteObject(session, id);
					}
				}
			}
		}
		else
		{
			std::vector<std::string> orderedIds;
			for (auto const& [id, gameObject] :
				core.getGameObjectManager().getGameObjects())
			{
				orderedIds.push_back(id);
				ImGui::PushID(id.c_str());
				if (state.renamingObjectId == id)
				{
					drawHierarchyRenameField(state, core, id);
					ImGui::PopID();
					continue;
				}
				if (ImGui::Selectable(id.c_str(), core.isSelected(id),
					ImGuiSelectableFlags_AllowDoubleClick))
				{
					core.selectObject(id);
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						startRenameSelected(state, core);
					}
				}
				// right-click selects, then offers the object operations
				if (ImGui::BeginPopupContextItem("##objectmenu"))
				{
					core.selectObject(id);
					if (ImGui::MenuItem("Duplicate"))
					{
						core.duplicateSelected();
					}
					if (ImGui::MenuItem("Rename"))
					{
						startRenameSelected(state, core);
					}
					if (ImGui::MenuItem("Delete"))
					{
						core.deleteSelected();
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			// right-click on empty space: creation menu
			if (ImGui::BeginPopupContextWindow("##createmenu",
				ImGuiPopupFlags_MouseButtonRight |
				ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Create Cube"))
				{
					core.createCube();
				}
				if (ImGui::MenuItem("Create Test Mesh"))
				{
					core.createTestMesh();
				}
				ImGui::EndPopup();
			}
			// keyboard: up/down moves the selection through the (sorted) list
			if (state.hierarchyFocused && !ImGui::GetIO().WantTextInput &&
				state.renamingObjectId.empty() && !orderedIds.empty())
			{
				int step = 0;
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				{
					step = -1;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				{
					step = 1;
				}
				if (step != 0)
				{
					int index = -1;
					for (std::size_t i = 0; i < orderedIds.size(); ++i)
					{
						if (core.isSelected(orderedIds[i]))
						{
							index = static_cast<int>(i);
							break;
						}
					}
					index = (index < 0) ? (step > 0 ? 0 : static_cast<int>(
						orderedIds.size()) - 1)
						: std::clamp(index + step, 0,
							static_cast<int>(orderedIds.size()) - 1);
					core.selectObject(orderedIds[index]);
				}
			}
		}
	}
	ImGui::End();
}

// Inspector transform editors: every edit goes through the command stack
// (undoable); while a drag widget stays active the per-frame edits merge
// into ONE undo step, exactly like a gizmo drag.
void drawTransformComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::EditorTransform before;
	if (!core.getObjectTransform(objectId, before))
	{
		return;
	}
	Orkige::EditorTransform after = before;
	bool edited = false;

	float position[3] = { before.position.x, before.position.y,
		before.position.z };
	bool changed = ImGui::DragFloat3("Position", position, 0.05f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.position = Ogre::Vector3(position[0], position[1], position[2]);
		edited = true;
	}

	float yawPitchRoll[3] = {
		before.orientation.getYaw().valueDegrees(),
		before.orientation.getPitch().valueDegrees(),
		before.orientation.getRoll().valueDegrees(),
	};
	changed = ImGui::DragFloat3("Yaw/Pitch/Roll", yawPitchRoll, 0.5f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		Ogre::Matrix3 rotation;
		rotation.FromEulerAnglesYXZ(
			Ogre::Degree(yawPitchRoll[0]),
			Ogre::Degree(yawPitchRoll[1]),
			Ogre::Degree(yawPitchRoll[2]));
		after.orientation = Ogre::Quaternion(rotation);
		edited = true;
	}

	float scale[3] = { before.scale.x, before.scale.y, before.scale.z };
	changed = ImGui::DragFloat3("Scale", scale, 0.02f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.scale = Ogre::Vector3(scale[0], scale[1], scale[2]);
		edited = true;
	}

	if (edited)
	{
		core.applyTransformChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

// remote inspector helpers: a Drag editor bound to a streamed property that
// sends set_property on change (only used for the set_property-backed
// properties; everything else renders read-only)
void drawRemoteDragProperty(PlaySession& session, char const* label,
	std::string const& component, std::string const& property, int floatCount)
{
	const std::string key = component + "." + property;
	std::map<std::string, std::string>::const_iterator it =
		session.stateProperties.find(key);
	if (it == session.stateProperties.end())
	{
		return;
	}
	float values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (!parsePlayFloats(it->second, values, floatCount))
	{
		ImGui::TextDisabled("%s: %s", label, it->second.c_str());
		return;
	}
	const bool edited = (floatCount == 4)
		? ImGui::DragFloat4(label, values, 0.05f)
		: ImGui::DragFloat3(label, values, 0.05f);
	if (edited)
	{
		Orkige::DebugMessage set(Protocol::MSG_SET_PROPERTY);
		set.set(Protocol::FIELD_ID, session.stateObjectId);
		set.set(Protocol::FIELD_COMPONENT, component);
		set.set(Protocol::FIELD_PROPERTY, property);
		set.set(Protocol::FIELD_VALUE, formatPlayFloats(values, floatCount));
		session.client.send(set);
	}
}

// Inspector content during play: the streamed object_state of the selected
// remote object. The set_property-backed properties (TransformComponent
// position/orientation/scale, RigidBodyComponent linear/angular velocity)
// are editable drags, everything else is read-only.
void drawRemoteInspector(PlaySession& session)
{
	if (session.remoteSelectedId.empty())
	{
		ImGui::TextDisabled("nothing selected (remote)");
		return;
	}
	if (session.stateObjectId != session.remoteSelectedId)
	{
		ImGui::TextDisabled("waiting for '%s' state...",
			session.remoteSelectedId.c_str());
		return;
	}
	ImGui::Text("%s", session.stateObjectId.c_str());
	ImGui::TextDisabled("remote object (live)");
	ImGui::Separator();
	for (std::string const& component : session.stateComponents)
	{
		if (!ImGui::CollapsingHeader(component.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen))
		{
			continue;
		}
		if (component == "TransformComponent")
		{
			drawRemoteDragProperty(session, "Position", component,
				"position", 3);
			drawRemoteDragProperty(session, "Orientation (wxyz)", component,
				"orientation", 4);
			drawRemoteDragProperty(session, "Scale", component, "scale", 3);
		}
		else if (component == "RigidBodyComponent")
		{
			ImGui::TextDisabled("body: %s%s",
				session.stateProperties["RigidBodyComponent.body_type"].c_str(),
				session.stateProperties["RigidBodyComponent.has_body"] == "1"
					? "" : " (not created yet)");
			drawRemoteDragProperty(session, "Linear velocity", component,
				"linear_velocity", 3);
			drawRemoteDragProperty(session, "Angular velocity", component,
				"angular_velocity", 3);
		}
		else
		{
			// generic read-only dump of whatever the player streamed
			bool any = false;
			const std::string prefix = component + ".";
			for (auto const& [key, value] : session.stateProperties)
			{
				if (key.rfind(prefix, 0) == 0)
				{
					ImGui::Text("%s: %s", key.c_str() + prefix.size(),
						value.c_str());
					any = true;
				}
			}
			if (!any)
			{
				ImGui::TextDisabled("(no properties streamed)");
			}
		}
	}
}

void drawInspectorPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core)
{
	const bool remote = session.isActive();
	if (ImGui::Begin(remote ? INSPECTOR_WINDOW_REMOTE : INSPECTOR_WINDOW_EDIT))
	{
		if (remote)
		{
			drawRemoteInspector(session);
			ImGui::End();
			return;
		}
		optr<Orkige::GameObject> gameObject;
		if (core.hasSelection())
		{
			gameObject = core.getGameObjectManager()
				.getGameObject(core.getSelectedObjectId()).lock();
		}
		if (!gameObject)
		{
			ImGui::TextDisabled("nothing selected");
		}
		else
		{
			ImGui::Text("%s", gameObject->getObjectID().c_str());
			ImGui::TextDisabled("type: %s",
				gameObject->getTypeInfo().getName().c_str());
			ImGui::Separator();
			for (auto const& [componentType, component] :
				gameObject->getComponents())
			{
				if (!ImGui::CollapsingHeader(componentType.getName().c_str(),
					ImGuiTreeNodeFlags_DefaultOpen))
				{
					continue;
				}
				if (dynamic_cast<Orkige::TransformComponent*>(component.get()))
				{
					drawTransformComponentUI(state, core,
						gameObject->getObjectID());
				}
				else if (auto* model =
					dynamic_cast<Orkige::ModelComponent*>(component.get()))
				{
					ImGui::Text("mesh: %s",
						model->getCurrentModelFileName().c_str());
				}
				else
				{
					ImGui::TextDisabled("(no editable properties yet)");
				}
			}
		}
	}
	ImGui::End();
}

// The keyboard shortcut map (checked once per frame, after the panels have
// recorded their hover/focus state; inactive while a text field is being
// edited or a play session runs):
//   global: Cmd/Ctrl+Z undo, Shift+Cmd/Ctrl+Z redo
//   Scene panel hovered/focused: Q select, W translate, E rotate, R scale,
//   X world/local, F frame selected, F2 rename, Delete/Backspace delete,
//   Cmd/Ctrl+D duplicate
void handleEditorShortcuts(EditorState& state, Orkige::EditorCore& core,
	PlaySession& session, Ogre::Camera* sceneCamera)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput || session.isActive())
	{
		return;
	}
	const bool commandDown = io.KeySuper || io.KeyCtrl;
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_Z, false))
	{
		if (io.KeyShift)
		{
			core.redo();
		}
		else
		{
			core.undo();
		}
		return;
	}
	if (!state.scenePanelHovered && !state.scenePanelFocused)
	{
		return;
	}
	if (commandDown)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_D, false))
		{
			core.duplicateSelected();
		}
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
	{
		core.setActiveTool(Orkige::EditorTool::Select);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_W, false))
	{
		core.setActiveTool(Orkige::EditorTool::Translate);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_E, false))
	{
		core.setActiveTool(Orkige::EditorTool::Rotate);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_R, false))
	{
		core.setActiveTool(Orkige::EditorTool::Scale);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_X, false))
	{
		core.toggleTransformSpace();
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		frameSelectedObject(state, core, sceneCamera);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		startRenameSelected(state, core);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
		ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
	{
		core.deleteSelected();
	}
}

void drawStatsPanel(Ogre::RenderWindow* renderWindow)
{
	if (ImGui::Begin("Stats"))
	{
		Ogre::RenderTarget::FrameStats const& stats =
			renderWindow->getStatistics();
		ImGui::Text("FPS: %.1f (avg %.1f)", stats.lastFPS, stats.avgFPS);
		ImGui::Text("Triangles: %zu", stats.triangleCount);
		ImGui::Text("Batches: %zu", stats.batchCount);
	}
	ImGui::End();
}

#ifdef ORKIGE_LUA
void drawLuaConsolePanel(EditorState& state,
	Orkige::ScriptManager& scriptManager)
{
	if (ImGui::Begin("Lua Console"))
	{
		const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 4.0f;
		if (ImGui::BeginChild("history", ImVec2(0, -footerHeight), true))
		{
			for (std::string const& line : state.luaHistory)
			{
				ImGui::TextWrapped("%s", line.c_str());
			}
			if (state.luaScrollToBottom)
			{
				ImGui::SetScrollHereY(1.0f);
				state.luaScrollToBottom = false;
			}
		}
		ImGui::EndChild();
		ImGui::InputTextMultiline("##luainput", state.luaInput,
			sizeof(state.luaInput),
			ImVec2(-1.0f, ImGui::GetFrameHeightWithSpacing() * 3.0f));
		if (ImGui::Button("Run"))
		{
			runLuaConsoleInput(state, scriptManager);
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			state.luaHistory.clear();
		}
	}
	ImGui::End();
}
#endif

} // namespace

int main(int, char**)
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_Window* window =
		SDL_CreateWindow("Orkige Editor", 1280, 720, SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		return 1;
	}

	int exitCode = 0;
	{
		// engine singletons normally created by Orkige::Application; the
		// editor boots the same set as the hello_orkige demo
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
			Orkige::StringUtil::BLANK, "orkige_editor.log");
		engine.setCustomWindowParam("width", "1280");
		engine.setCustomWindowParam("height", "720");

		// ORKIGE_RENDERSYSTEM: explicit render system choice ("Metal",
		// "GL3Plus", "GL" - see Engine::matchRenderSystemName); unset keeps
		// the default (first available, i.e. GL3Plus). OGRE 14.5's Metal RS
		// has no RTSS/MSL backend - the scene renders through OGRE's built-in
		// default shaders (no vertex colours, no lighting) on Metal.
		if (const char* renderSystemEnv = std::getenv("ORKIGE_RENDERSYSTEM"))
		{
			engine.setPreferredRenderSystem(renderSystemEnv);
		}

		// OverlaySystem: Root exists (Engine ctor), Root::initialise has not
		// run yet (that happens inside Engine::setup) - exactly the window
		// OgreOverlaySystem.h documents. Declared after `engine` so it is
		// destroyed before the Root goes down.
		Ogre::OverlaySystem overlaySystem;

		// RTSS shader library + OgreUnifiedShader.h, same locations
		// OgreBites::ApplicationContext registers (see CMakeLists.txt)
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_EDITOR_MEDIA_DIR "/Main", "FileSystem", Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_EDITOR_MEDIA_DIR "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);
		// sample assets (test_mesh.glb from Util/make_test_mesh.py) in the
		// default group; meshes load lazily via Codec_Assimp on createEntity
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_EDITOR_ASSET_DIR, "FileSystem");

		if (!engine.setup("Orkige Editor", Orkige::Engine::SHOW_NEVER,
			Orkige::StringUtil::Converter::toString(
				reinterpret_cast<size_t>(orkige_native_window_handle(window)))))
		{
			SDL_Log("Engine::setup failed");
			return 1;
		}
		Ogre::SceneManager* sceneManager = engine.getSceneManager();

		// The scene no longer renders into the window (that was
		// Engine::createDefaultCameraAndViewport): the editor's scene camera
		// draws into the offscreen RTT created below and the window keeps a
		// single UI-only viewport for the ImGui overlay - visibility mask 0
		// hides all scene content, leaving a dark grey backdrop around the
		// docked panels.
		Ogre::Camera* sceneCamera =
			sceneManager->createCamera("EditorSceneCamera");
		sceneCamera->setNearClipDistance(1.0f);
		sceneCamera->setFarClipDistance(100000.0f);
		Ogre::SceneNode* sceneCameraNode = sceneManager->getRootSceneNode()
			->createChildSceneNode("EditorSceneCameraNode");
		sceneCameraNode->attachObject(sceneCamera);

		Ogre::Camera* uiCamera = sceneManager->createCamera("EditorUICamera");
		sceneManager->getRootSceneNode()
			->createChildSceneNode("EditorUICameraNode")
			->attachObject(uiCamera);
		Ogre::Viewport* uiViewport =
			engine.getRenderWindow()->addViewport(uiCamera);
		uiViewport->setBackgroundColour(
			Ogre::ColourValue(0.16f, 0.16f, 0.16f));
		uiViewport->setVisibilityMask(0); // overlay only - no scene objects
		uiViewport->setShadowsEnabled(false);
#ifdef USE_RTSHADER_SYSTEM
		// the ImGui overlay material renders through the RTSS scheme, same
		// as it did on the old scene viewport (GL3+ has no fixed function)
		if (!Ogre::Root::getSingleton().getRenderSystem()->getCapabilities()
			->hasCapability(Ogre::RSC_FIXED_FUNCTION))
		{
			uiViewport->setMaterialScheme(
				Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
		}
#endif
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
		// overlays render as a RenderQueueListener on the SceneManager
		sceneManager->addRenderQueueListener(&overlaySystem);
		sceneManager->setAmbientLight(Ogre::ColourValue(0.2f, 0.2f, 0.2f));

		// Dear ImGui overlay: OGRE's own integration; the overlay renders
		// through the Overlay render queue hook above. Ownership goes to the
		// OverlayManager (destroyed with the OverlaySystem).
		Ogre::ImGuiOverlay* imguiOverlay = new Ogre::ImGuiOverlay();
		imguiOverlay->setZOrder(300);
		imguiOverlay->show(); // initialises the renderable: font atlas + "ImGui/FontTex"
		Ogre::OverlayManager::getSingleton().addOverlay(imguiOverlay);
		// OGRE 14.5 predates imgui 1.92's texture update protocol and never
		// calls SetTexID after uploading the font atlas, which trips
		// ImDrawCmd::GetTexID's "texture wasn't uploaded" assert. Register the
		// uploaded Ogre texture through the legacy path; OGRE's renderable
		// resolves the id via TextureManager::getByHandle.
		Ogre::TexturePtr imguiFontTex = Ogre::TextureManager::getSingleton()
			.getByName("ImGui/FontTex", Ogre::RGN_INTERNAL);
		OgreAssert(imguiFontTex, "ImGui font texture missing after overlay init");
		ImGui::GetIO().Fonts->SetTexID(
			static_cast<ImTextureID>(imguiFontTex->getHandle()));

		// docking UI: full-window dockspace (drawDockspace). The panel layout
		// persists through imgui.ini stored NEXT TO THE EXECUTABLE
		// (SDL_GetBasePath), so it works no matter which cwd the editor is
		// launched from. Static so the path outlives the ImGui context - the
		// ini gets written during ImGui::DestroyContext (~ImGuiOverlay).
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		const char* sdlBasePath = SDL_GetBasePath();
		static const std::string imguiIniPath =
			std::string(sdlBasePath ? sdlBasePath : "") +
			"orkige_editor_imgui.ini";
		ImGui::GetIO().IniFilename = imguiIniPath.c_str();

		// offscreen scene viewport: initial size is a placeholder, the Scene
		// panel drives resizes from its content region (with hysteresis)
		SceneRenderTarget sceneTarget;
		sceneTarget.camera = sceneCamera;
		createSceneRenderTexture(sceneTarget, 960, 540);

		// input: ImGui first, engine InputManager for whatever is left
		Orkige::InputManager inputManager;
		Orkige::ImGuiSDL3Input imguiInput(window);
		QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&QuitOnEscape::onKeyPressed, &quitOnEscape);

		// unlit vertex-colour material + the shared "EditorCube.mesh" resource
		// (a real mesh, so cubes go through ModelComponent and round-trip
		// through scene files - the player builds the identical resource)
		Orkige::PrimitiveUtil::createVertexColourMaterial();
		Orkige::PrimitiveUtil::createVertexColourCubeMesh(sceneManager);

		// GameObject/component bridge (registers the component factories)
		init_module_orkige_engine();
		Orkige::GameObjectManager gameObjectManager;

		// the UI-independent editor logic (selection, tools, undo/redo,
		// object operations) - everything below drives THIS layer
		Orkige::EditorCore editorCore(gameObjectManager);
		quitOnEscape.editorCore = &editorCore;

		// play mode session (idle until the Play button / playtest hook)
		PlaySession playSession;

		// boot scene: three sample cubes so the panels have content (created
		// directly, not through undoable commands - a fresh editor starts
		// with an empty undo history and a clean scene)
		EditorState state;
		const Ogre::Vector3 bootPositions[3] = {
			{ -2.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 2.5f, 0.0f, 0.0f },
		};
		int bootCubeCounter = 0;
		for (Ogre::Vector3 const& position : bootPositions)
		{
			const std::string id = "Cube" + std::to_string(++bootCubeCounter);
			if (!editorCore.instantiateModelObject(id,
				Orkige::PrimitiveUtil::CUBE_MESH_NAME, position))
			{
				SDL_Log("orkige_editor: FAILED - boot GameObject '%s' "
					"creation failed", id.c_str());
				return 1;
			}
		}
		// ... plus one glTF test mesh above the cubes: proves the Codec_Assimp
		// import path (registered in Engine.cpp's static-plugin block) at boot
		if (!editorCore.instantiateModelObject("TestMesh1", "test_mesh.glb",
			Ogre::Vector3(0.0f, 2.2f, 0.0f)))
		{
			SDL_Log("orkige_editor: FAILED - boot test mesh GameObject "
				"creation failed");
			return 1;
		}
		editorCore.selectObject("Cube2");

		// initial orbit pose reproduces the old fixed camera at (0, 2.5, 9)
		applyOrbitCamera(state, sceneCameraNode);

		// automation hooks (same env-hook style as the demo):
		// ORKIGE_DEMO_FRAMES=N exit 0 after N frames,
		// ORKIGE_DEMO_SCREENSHOT=path framebuffer dump at frame 60,
		// ORKIGE_EDITOR_SELFCHECK=1 boot-state assertions at frame 30,
		// Scene-panel-picking checks at frames 45/65 and the scene round-trip
		// check (save/clear/reload/compare) at frame 90 (needs >= 90 frames;
		// ORKIGE_EDITOR_SELFCHECK_SCENE overrides the round-trip file path),
		// ORKIGE_EDITOR_EXPORT_EXAMPLE=path arrange the boot objects into the
		// shipped example layout at frame 20 and save it through the
		// serializer (produces samples/scenes/example.oscene),
		// ORKIGE_EDITOR_RESIZE_TEST=1 programmatic SDL_SetWindowSize at
		// frame 80 (resize robustness; needs >= 90 frames)
		unsigned long frameLimit = 0;
		if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
		{
			frameLimit = std::strtoul(demoFrames, nullptr, 10);
		}
		const bool selfCheck = (std::getenv("ORKIGE_EDITOR_SELFCHECK") != nullptr);
		const bool resizeTest =
			(std::getenv("ORKIGE_EDITOR_RESIZE_TEST") != nullptr);
		// ORKIGE_EDITOR_EDITTEST=1: scripted editing run (tools, command
		// stack, duplicate, delete+undo, rename, merge, save/reload) - the
		// editor_edittest ctest test; asserts fire at fixed frames below
		const bool editTest = (std::getenv("ORKIGE_EDITOR_EDITTEST") != nullptr);

		// ORKIGE_EDITOR_PLAYTEST=stop|crash: scripted play-mode run (used by
		// the editor_play_stop / editor_play_crash ctest tests) - press Play
		// at frame 40, require the remote hierarchy (count must match the
		// local scene) and a streamed object_state, then Stop (or SIGKILL
		// the player) and require a clean revert to edit mode with the
		// editor scene untouched. Exits non-zero on any missed deadline.
		const char* playtestEnv = std::getenv("ORKIGE_EDITOR_PLAYTEST");
		const bool playtest = (playtestEnv != nullptr);
		const bool playtestCrash =
			playtest && (std::strcmp(playtestEnv, "crash") == 0);
		// edittest state that spans frames
		Orkige::EditorTransform editTestCube1Before;
		Orkige::EditorTransform editTestCube1Moved;
		Orkige::EditorTransform editTestDeletedTransform;
		std::string editTestDeletedMesh;
		std::string editTestDuplicateId;

		enum class PlaytestPhase
		{
			Idle, WaitRemote, WaitState, Interfere, WaitRevert, Done
		};
		PlaytestPhase playtestPhase = PlaytestPhase::Idle;
		std::chrono::steady_clock::time_point playtestDeadline;
		size_t playtestLocalObjects = 0;
		unsigned long playtestScreenshotFrame = 0;
		unsigned long playtestInterfereFrame = 0;

		bool running = true;
		unsigned long frameCount = 0;
		std::string lastWindowTitle;
		while (running)
		{
			// window title reflects the scene path + dirty marker
			const std::string windowTitle = "Orkige Editor - " +
				(state.currentScenePath.empty() ? std::string("untitled")
					: state.currentScenePath) +
				(editorCore.isSceneDirty() ? " *" : "");
			if (windowTitle != lastWindowTitle)
			{
				SDL_SetWindowTitle(window, windowTitle.c_str());
				lastWindowTitle = windowTitle;
			}

			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				if (event.type == SDL_EVENT_QUIT)
				{
					running = false;
				}
				if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
				{
					// keep the OGRE window in sync with the SDL window (the
					// ORKIGE_EDITOR_RESIZE_TEST hook below exercises this)
					engine.getRenderWindow()->windowMovedOrResized();
				}
				// ImGui gets every event first; only forward into the engine
				// input pipeline what ImGui does not capture. The dockspace
				// covers the whole window, so ImGui now captures all mouse
				// input - scene picking happens inside the Scene panel
				// (drawScenePanel), not here.
				if (!imguiInput.processEvent(event))
				{
					inputManager.injectEvent(event);
				}
			}
			if (quitOnEscape.quitRequested || state.quitRequested)
			{
				running = false;
			}

			// apply Scene-panel-driven RTT resizes with hysteresis: only
			// recreate the texture once the requested size held still for a
			// few frames (avoids recreation thrash while a dock splitter or
			// the window is dragged - the image stretches in the meantime)
			{
				const int desiredW = std::max(state.scenePanelWidth, 32);
				const int desiredH = std::max(state.scenePanelHeight, 32);
				if (state.scenePanelWidth > 0 && state.scenePanelHeight > 0 &&
					(desiredW != sceneTarget.width ||
						desiredH != sceneTarget.height))
				{
					if (desiredW == state.pendingRttWidth &&
						desiredH == state.pendingRttHeight)
					{
						++state.pendingRttFrames;
					}
					else
					{
						state.pendingRttWidth = desiredW;
						state.pendingRttHeight = desiredH;
						state.pendingRttFrames = 1;
					}
					if (state.pendingRttFrames >= 4)
					{
						createSceneRenderTexture(sceneTarget,
							desiredW, desiredH);
						state.pendingRttFrames = 0;
					}
				}
				else
				{
					state.pendingRttFrames = 0;
				}
			}

			// play mode: pump the debug link, watch the player process,
			// handle crash/stop transitions - before the UI reads the state
			updatePlaySession(playSession);

			imguiInput.newFrame(
				static_cast<float>(engine.getRenderWindow()->getWidth()),
				static_cast<float>(engine.getRenderWindow()->getHeight()));
			Ogre::ImGuiOverlay::NewFrame();
			ImGuizmo::BeginFrame();

			drawMenuBar(state, editorCore);
			const float toolbarHeight =
				drawToolbar(state, playSession, editorCore);
			drawDockspace(state, toolbarHeight);
			drawScenePanel(state, editorCore, !playSession.isActive(),
				sceneTarget, sceneManager, sceneCameraNode);
			drawHierarchyPanel(state, playSession, editorCore);
			drawInspectorPanel(state, playSession, editorCore);
			drawStatsPanel(engine.getRenderWindow());
#ifdef ORKIGE_LUA
			drawLuaConsolePanel(state, scriptManager);
#endif
			// keyboard shortcuts, gated by the hover/focus state the panels
			// just recorded (Q/W/E/R, undo/redo, duplicate, delete, ...)
			handleEditorShortcuts(state, editorCore, playSession,
				sceneTarget.camera);

			if (!engine.renderOneFrame())
			{
				running = false;
			}
			++frameCount;

#ifdef ORKIGE_LUA
			if (frameCount == 10 && selfCheck)
			{
				// self-check: run the console's default buffer through the
				// exact same path the Run button uses and expect a result
				runLuaConsoleInput(state, scriptManager);
				const std::string result =
					state.luaHistory.empty() ? "" : state.luaHistory.back();
				SDL_Log("orkige_editor: selfcheck frame 10 - lua console "
					"result: '%s'", result.c_str());
				if (result.empty() || result.rfind("error:", 0) == 0)
				{
					SDL_Log("orkige_editor: FAILED selfcheck (lua console)");
					exitCode = 2;
					running = false;
				}
			}
#endif
			if (frameCount == 30 && selfCheck)
			{
				// self-check: boot GameObjects present + ImGui actually
				// produced geometry this frame (catches a non-rendering
				// overlay: z-order, missing RenderQueueListener, NewFrame
				// order would all zero this out)
				const bool objectsOk =
					gameObjectManager.objectExists("Cube1") &&
					gameObjectManager.objectExists("Cube2") &&
					gameObjectManager.objectExists("Cube3") &&
					gameObjectManager.objectExists("TestMesh1");
				// ... and the glTF asset really became an Ogre mesh resource
				// (Codec_Assimp decoded it during boot createEntity)
				const Ogre::MeshPtr testMesh =
					Ogre::MeshManager::getSingleton().getByName(
						"test_mesh.glb",
						Ogre::ResourceGroupManager::
							AUTODETECT_RESOURCE_GROUP_NAME);
				const bool meshResourceOk = testMesh && testMesh->isLoaded();
				const int imguiVertices = ImGui::GetIO().MetricsRenderVertices;
				// ... and the offscreen scene RTT exists and follows the
				// Scene panel size (the panel recorded its wish by now, so
				// after the hysteresis window the sizes must match)
				const bool rttOk = sceneTarget.texture &&
					sceneTarget.width > 0 && sceneTarget.height > 0 &&
					sceneTarget.width == std::max(state.scenePanelWidth, 32) &&
					sceneTarget.height == std::max(state.scenePanelHeight, 32);
				SDL_Log("orkige_editor: selfcheck frame 30 - gameobjects=%zu "
					"(boot cubes + test mesh %s), test_mesh.glb resource %s, "
					"imgui vertices=%d, scene RTT %dx%d (panel wants %dx%d)",
					gameObjectManager.getGameObjects().size(),
					objectsOk ? "present" : "MISSING",
					meshResourceOk ? "loaded" : "NOT LOADED", imguiVertices,
					sceneTarget.width, sceneTarget.height,
					state.scenePanelWidth, state.scenePanelHeight);
				if (!objectsOk || !meshResourceOk || imguiVertices <= 0 ||
					!rttOk)
				{
					SDL_Log("orkige_editor: FAILED selfcheck");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 45 && selfCheck)
			{
				// self-check: Scene panel picking - project Cube1 through the
				// RTT camera into viewport-normalized panel coordinates and
				// run the exact pick function the panel's mouse path uses
				editorCore.clearSelection();
				if (!pickGameObjectThroughScenePanel(editorCore,
					gameObjectManager, sceneTarget.camera, sceneManager,
					"Cube1"))
				{
					SDL_Log("orkige_editor: FAILED selfcheck (pick projection)");
					exitCode = 2;
					running = false;
				}
				SDL_Log("orkige_editor: selfcheck frame 45 - picked '%s' via "
					"scene panel pick",
					editorCore.getSelectedObjectId().c_str());
				if (editorCore.getSelectedObjectId() != "Cube1")
				{
					SDL_Log("orkige_editor: FAILED selfcheck (scene panel pick)");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 65 && selfCheck)
			{
				// self-check: same panel-picking path, this time on the glTF
				// test mesh (its Entity goes through the identical
				// TransformComponent scene-node tagging as the cubes)
				editorCore.clearSelection();
				if (!pickGameObjectThroughScenePanel(editorCore,
					gameObjectManager, sceneTarget.camera, sceneManager,
					"TestMesh1"))
				{
					SDL_Log("orkige_editor: FAILED selfcheck (test mesh pick "
						"projection)");
					exitCode = 2;
					running = false;
				}
				SDL_Log("orkige_editor: selfcheck frame 65 - picked '%s' via "
					"scene panel pick",
					editorCore.getSelectedObjectId().c_str());
				if (editorCore.getSelectedObjectId() != "TestMesh1")
				{
					SDL_Log("orkige_editor: FAILED selfcheck (test mesh "
						"scene panel pick)");
					exitCode = 2;
					running = false;
				}
			}
			// --- scripted editing test (ORKIGE_EDITOR_EDITTEST=1) ---
			// drives the SAME EditorCore functions the shortcuts/menus/gizmo
			// invoke and asserts the outcomes at fixed frames
			if (editTest)
			{
				bool editOk = true;
				std::string editFailure;
				auto require = [&](bool condition, char const* what)
				{
					if (!condition && editOk)
					{
						editOk = false;
						editFailure = what;
					}
				};
				auto positionsEqual = [](Ogre::Vector3 const& a,
					Ogre::Vector3 const& b)
				{
					return a.positionEquals(b, 1e-3f);
				};
				auto orientationsEqual = [](Ogre::Quaternion const& a,
					Ogre::Quaternion const& b)
				{
					return std::abs(a.Dot(b)) > 0.9999f;
				};
				if (frameCount == 30)
				{
					// tool switching through the functions the Q/W/E/R/X
					// shortcuts call
					require(editorCore.getActiveTool() ==
						Orkige::EditorTool::Translate, "default tool");
					editorCore.setActiveTool(Orkige::EditorTool::Select);
					require(editorCore.getActiveTool() ==
						Orkige::EditorTool::Select, "Q tool");
					editorCore.setActiveTool(Orkige::EditorTool::Rotate);
					require(editorCore.getActiveTool() ==
						Orkige::EditorTool::Rotate, "E tool");
					editorCore.setActiveTool(Orkige::EditorTool::Scale);
					require(editorCore.getActiveTool() ==
						Orkige::EditorTool::Scale, "R tool");
					editorCore.setActiveTool(Orkige::EditorTool::Translate);
					require(editorCore.getTransformSpace() ==
						Orkige::EditorTransformSpace::World, "default space");
					editorCore.toggleTransformSpace();
					require(editorCore.getTransformSpace() ==
						Orkige::EditorTransformSpace::Local, "X toggle");
					editorCore.toggleTransformSpace();
					require(!editorCore.isSnapEnabled(), "default snap");
					editorCore.setSnapEnabled(true);
					require(editorCore.isSnapEnabled(), "snap toggle");
					editorCore.setSnapEnabled(false);
					require(!editorCore.isSceneDirty(),
						"tool changes must not dirty the scene");
					SDL_Log("orkige_editor: edittest frame 30 - tool "
						"switching OK");
				}
				if (frameCount == 35)
				{
					// scripted TransformChange through the command stack
					require(editorCore.getObjectTransform("Cube1",
						editTestCube1Before), "Cube1 transform");
					editTestCube1Moved = editTestCube1Before;
					editTestCube1Moved.position =
						Ogre::Vector3(1.5f, 0.75f, -2.0f);
					editTestCube1Moved.orientation = Ogre::Quaternion(
						Ogre::Degree(45.0f), Ogre::Vector3::UNIT_Y);
					require(editorCore.applyTransformChange("Cube1",
						editTestCube1Before, editTestCube1Moved),
						"applyTransformChange");
					Orkige::EditorTransform now;
					require(editorCore.getObjectTransform("Cube1", now) &&
						positionsEqual(now.position,
							editTestCube1Moved.position) &&
						orientationsEqual(now.orientation,
							editTestCube1Moved.orientation),
						"object moved");
					require(editorCore.canUndo() && editorCore.isSceneDirty(),
						"undoable + dirty");
					require(editorCore.getUndoDescription() ==
						"Transform Cube1", "undo description");
					SDL_Log("orkige_editor: edittest frame 35 - transform "
						"command OK");
				}
				if (frameCount == 40)
				{
					require(editorCore.undo(), "undo");
					Orkige::EditorTransform now;
					require(editorCore.getObjectTransform("Cube1", now) &&
						positionsEqual(now.position,
							editTestCube1Before.position) &&
						orientationsEqual(now.orientation,
							editTestCube1Before.orientation),
						"undo restored the transform");
					require(editorCore.canRedo(), "redoable");
					SDL_Log("orkige_editor: edittest frame 40 - undo OK");
				}
				if (frameCount == 45)
				{
					require(editorCore.redo(), "redo");
					Orkige::EditorTransform now;
					require(editorCore.getObjectTransform("Cube1", now) &&
						positionsEqual(now.position,
							editTestCube1Moved.position),
						"redo re-applied the transform");
					SDL_Log("orkige_editor: edittest frame 45 - redo OK");
				}
				if (frameCount == 50)
				{
					// duplicate: clone via serialize/deserialize, offset,
					// select the copy
					editorCore.selectObject("Cube1");
					editTestDuplicateId = editorCore.makeDuplicateId("Cube1");
					require(editTestDuplicateId == "Cube1 Copy",
						"duplicate id");
					require(editorCore.duplicateSelected(), "duplicate");
					require(gameObjectManager.objectExists(
						editTestDuplicateId), "copy exists");
					require(editorCore.getSelectedObjectId() ==
						editTestDuplicateId, "copy selected");
					Orkige::EditorTransform source;
					Orkige::EditorTransform copy;
					require(editorCore.getObjectTransform("Cube1", source) &&
						editorCore.getObjectTransform(editTestDuplicateId,
							copy) &&
						positionsEqual(copy.position, source.position +
							Orkige::EditorCore::DUPLICATE_OFFSET),
						"copy offset");
					optr<Orkige::GameObject> copyObject = gameObjectManager
						.getGameObject(editTestDuplicateId).lock();
					require(copyObject && copyObject
						->hasComponent<Orkige::ModelComponent>() &&
						copyObject->getComponentPtr<Orkige::ModelComponent>()
							->getCurrentModelFileName() ==
						Orkige::PrimitiveUtil::CUBE_MESH_NAME,
						"copy mesh");
					SDL_Log("orkige_editor: edittest frame 50 - duplicate OK "
						"('%s')", editTestDuplicateId.c_str());
				}
				if (frameCount == 55)
				{
					// delete stores the full serialized state for undo
					require(editorCore.getObjectTransform("Cube2",
						editTestDeletedTransform), "Cube2 transform");
					optr<Orkige::GameObject> cube2 =
						gameObjectManager.getGameObject("Cube2").lock();
					require(cube2 && cube2
						->hasComponent<Orkige::ModelComponent>(),
						"Cube2 model");
					if (cube2)
					{
						editTestDeletedMesh = cube2
							->getComponentPtr<Orkige::ModelComponent>()
							->getCurrentModelFileName();
					}
					editorCore.selectObject("Cube2");
					require(editorCore.deleteSelected(), "delete");
					require(!gameObjectManager.objectExists("Cube2"),
						"Cube2 gone");
					require(!editorCore.hasSelection(), "selection cleared");
					require(editorCore.getUndoDescription() == "Delete Cube2",
						"delete description");
					SDL_Log("orkige_editor: edittest frame 55 - delete OK");
				}
				if (frameCount == 60)
				{
					require(editorCore.undo(), "undo delete");
					require(gameObjectManager.objectExists("Cube2"),
						"Cube2 restored");
					Orkige::EditorTransform now;
					require(editorCore.getObjectTransform("Cube2", now) &&
						positionsEqual(now.position,
							editTestDeletedTransform.position) &&
						orientationsEqual(now.orientation,
							editTestDeletedTransform.orientation) &&
						positionsEqual(now.scale,
							editTestDeletedTransform.scale),
						"restored transform");
					optr<Orkige::GameObject> cube2 =
						gameObjectManager.getGameObject("Cube2").lock();
					require(cube2 && cube2
						->hasComponent<Orkige::ModelComponent>() &&
						cube2->getComponentPtr<Orkige::ModelComponent>()
							->getCurrentModelFileName() ==
							editTestDeletedMesh &&
						cube2->getComponentPtr<Orkige::ModelComponent>()
							->getModel() != nullptr,
						"restored mesh");
					require(editorCore.getSelectedObjectId() == "Cube2",
						"restored selection");
					SDL_Log("orkige_editor: edittest frame 60 - delete+undo "
						"restored the full object state");
				}
				if (frameCount == 70)
				{
					// rename + validation rules
					require(editorCore.renameObject("Cube3", "Tower"),
						"rename");
					require(gameObjectManager.objectExists("Tower") &&
						!gameObjectManager.objectExists("Cube3"),
						"renamed");
					require(editorCore.validateRename("Tower", "") ==
						Orkige::EditorCore::NameValidation::Empty,
						"empty rejected");
					require(editorCore.validateRename("Tower", "Cube1") ==
						Orkige::EditorCore::NameValidation::Exists,
						"duplicate rejected");
					require(!editorCore.renameObject("Tower", "Cube1"),
						"invalid rename refused");
					require(editorCore.undo(), "undo rename");
					require(gameObjectManager.objectExists("Cube3") &&
						!gameObjectManager.objectExists("Tower"),
						"rename undone");
					SDL_Log("orkige_editor: edittest frame 70 - rename OK");
				}
				if (frameCount == 80)
				{
					// merge session: a simulated 3-step drag = ONE undo step
					const std::size_t depthBefore =
						editorCore.getUndoStackSize();
					Orkige::EditorTransform dragStart;
					require(editorCore.getObjectTransform("Cube1", dragStart),
						"drag start transform");
					const unsigned int session =
						editorCore.beginMergeSession();
					Orkige::EditorTransform step = dragStart;
					for (int i = 0; i < 3; ++i)
					{
						Orkige::EditorTransform stepBefore = step;
						step.position += Ogre::Vector3(0.0f, 0.5f, 0.0f);
						require(editorCore.applyTransformChange("Cube1",
							stepBefore, step, session), "drag step");
					}
					require(editorCore.getUndoStackSize() == depthBefore + 1,
						"drag merged into one undo step");
					require(editorCore.undo(), "undo drag");
					Orkige::EditorTransform now;
					require(editorCore.getObjectTransform("Cube1", now) &&
						positionsEqual(now.position, dragStart.position),
						"single undo reverts the whole drag");
					SDL_Log("orkige_editor: edittest frame 80 - merge OK");
				}
				if (frameCount == 90)
				{
					// persistence: save, reload, edits survive
					const char* editScene =
						std::getenv("ORKIGE_EDITOR_EDITTEST_SCENE");
					const std::string scenePath =
						editScene ? editScene : "edittest.oscene";
					const std::size_t objectCount =
						gameObjectManager.getGameObjects().size();
					Orkige::EditorTransform cube1Saved;
					require(editorCore.getObjectTransform("Cube1",
						cube1Saved), "Cube1 before save");
					require(saveSceneToPath(state, editorCore, scenePath),
						"save");
					require(!editorCore.isSceneDirty(), "clean after save");
					require(openSceneFromPath(state, editorCore, scenePath),
						"reload");
					require(gameObjectManager.getGameObjects().size() ==
						objectCount, "object count after reload");
					require(gameObjectManager.objectExists(
						editTestDuplicateId), "copy persisted");
					Orkige::EditorTransform reloaded;
					require(editorCore.getObjectTransform("Cube1",
						reloaded) &&
						positionsEqual(reloaded.position,
							cube1Saved.position) &&
						orientationsEqual(reloaded.orientation,
							cube1Saved.orientation),
						"Cube1 edits persisted");
					SDL_Log("orkige_editor: edittest frame 90 - save/reload "
						"persistence OK -> edittest PASSED");
				}
				if (!editOk)
				{
					SDL_Log("orkige_editor: edittest FAILED at frame %lu - %s",
						frameCount, editFailure.c_str());
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 20)
			{
				// example scene export: arrange the boot objects (plus two
				// extra cubes) into an interesting layout and save it through
				// the serializer - the committed samples/scenes/example.oscene
				// is produced by exactly this path
				if (const char* examplePath =
					std::getenv("ORKIGE_EDITOR_EXPORT_EXAMPLE"))
				{
					auto setPose = [&](std::string const& id,
						Ogre::Vector3 const& position, float yawDegrees,
						float uniformScale) -> bool
					{
						optr<Orkige::GameObject> gameObject =
							gameObjectManager.getGameObject(id).lock();
						if (!gameObject)
						{
							return false;
						}
						Orkige::TransformComponent* transform = gameObject
							->getComponentPtr<Orkige::TransformComponent>();
						transform->setPosition(position);
						transform->setOrientation(Ogre::Quaternion(
							Ogre::Radian(Ogre::Degree(yawDegrees)),
							Ogre::Vector3::UNIT_Y));
						transform->setScale(Ogre::Vector3(uniformScale));
						return true;
					};
					editorCore.createCube(); // Cube4
					editorCore.createCube(); // Cube5
					const bool arranged =
						setPose("Cube1", { -3.0f, 0.0f, -1.5f }, 30.0f, 1.0f) &&
						setPose("Cube2", { 0.0f, -0.4f, 0.0f }, 0.0f, 1.4f) &&
						setPose("Cube3", { 3.0f, 0.2f, -1.0f }, -25.0f, 1.0f) &&
						setPose("Cube4", { -1.6f, 1.5f, 1.2f }, 45.0f, 0.6f) &&
						setPose("Cube5", { 1.8f, 1.7f, 1.4f }, -60.0f, 0.5f) &&
						setPose("TestMesh1", { 0.0f, 2.8f, 0.0f }, 15.0f, 1.2f);
					const bool exported = arranged && saveSceneToPath(state,
						editorCore, examplePath);
					SDL_Log("orkige_editor: example scene export to '%s' %s",
						examplePath, exported ? "succeeded" : "FAILED");
					if (!exported)
					{
						exitCode = 2;
						running = false;
					}
				}
			}
			if (frameCount == 60 && !playtest)
			{
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					engine.getRenderWindow()->writeContentsToFile(shotPath);
				}
			}

			// --- scripted play-mode test (ORKIGE_EDITOR_PLAYTEST) ---
			if (playtest)
			{
				const std::chrono::steady_clock::time_point playtestNow =
					std::chrono::steady_clock::now();
				bool playtestFailed = false;
				std::string playtestFailure;
				if (playtestPhase == PlaytestPhase::Idle && frameCount == 40)
				{
					playtestLocalObjects =
						gameObjectManager.getGameObjects().size();
					// the exact function the Play button calls
					if (!startPlay(playSession, gameObjectManager))
					{
						playtestFailed = true;
						playtestFailure = "startPlay failed";
					}
					else
					{
						playtestPhase = PlaytestPhase::WaitRemote;
						playtestDeadline = playtestNow +
							std::chrono::seconds(60);
					}
				}
				else if (playtestPhase == PlaytestPhase::WaitRemote)
				{
					if (playSession.mode == PlaySession::Mode::Playing &&
						playSession.helloReceived &&
						playSession.hierarchyReceived)
					{
						if (playSession.remoteHierarchy.size() !=
							playtestLocalObjects)
						{
							playtestFailed = true;
							playtestFailure = "remote hierarchy has " +
								std::to_string(
									playSession.remoteHierarchy.size()) +
								" objects, local scene has " +
								std::to_string(playtestLocalObjects);
						}
						else
						{
							// select the first remote object like a click in
							// the remote hierarchy panel would
							selectRemoteObject(playSession,
								playSession.remoteHierarchy.front());
							SDL_Log("orkige_editor: playtest - remote "
								"hierarchy verified (%zu objects), selected "
								"'%s'", playSession.remoteHierarchy.size(),
								playSession.remoteSelectedId.c_str());
							playtestPhase = PlaytestPhase::WaitState;
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						playtestFailed = true;
						playtestFailure =
							"play session ended before the remote hierarchy "
							"arrived";
					}
				}
				else if (playtestPhase == PlaytestPhase::WaitState)
				{
					if (playSession.stateObjectId ==
							playSession.remoteSelectedId &&
						!playSession.remoteSelectedId.empty() &&
						playSession.stateProperties.count(
							"TransformComponent.position") != 0)
					{
						SDL_Log("orkige_editor: playtest - object_state for "
							"'%s' streams (position %s)",
							playSession.stateObjectId.c_str(),
							playSession.stateProperties
								["TransformComponent.position"].c_str());
						// give the UI a few frames to draw the remote panels
						// before the screenshot / the interference step
						playtestScreenshotFrame = frameCount + 5;
						playtestInterfereFrame = frameCount + 20;
						playtestPhase = PlaytestPhase::Interfere;
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						playtestFailed = true;
						playtestFailure =
							"play session ended before object_state arrived";
					}
				}
				else if (playtestPhase == PlaytestPhase::Interfere)
				{
					if (frameCount == playtestScreenshotFrame)
					{
						if (const char* shotPath =
							std::getenv("ORKIGE_DEMO_SCREENSHOT"))
						{
							engine.getRenderWindow()->writeContentsToFile(
								shotPath);
							SDL_Log("orkige_editor: playtest - screenshot "
								"with active remote session -> %s", shotPath);
						}
					}
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						playtestFailed = true;
						playtestFailure = "play session ended before the "
							"stop/crash step";
					}
					else if (frameCount >= playtestInterfereFrame)
					{
						if (playtestCrash)
						{
#ifndef _WIN32
							// simulate a player crash: SIGKILL, not Stop -
							// the editor must recover via the link drop
							const Sint64 playerPid = SDL_GetNumberProperty(
								SDL_GetProcessProperties(playSession.process),
								SDL_PROP_PROCESS_PID_NUMBER, 0);
							if (playerPid <= 0)
							{
								playtestFailed = true;
								playtestFailure = "could not get player pid";
							}
							else
							{
								::kill(static_cast<pid_t>(playerPid), SIGKILL);
								SDL_Log("orkige_editor: playtest - SIGKILLed "
									"player pid %lld",
									static_cast<long long>(playerPid));
							}
#else
							playtestFailed = true;
							playtestFailure =
								"crash playtest not supported on this platform";
#endif
						}
						else
						{
							// the exact function the Stop button calls
							requestStopPlay(playSession);
						}
						if (!playtestFailed)
						{
							playtestPhase = PlaytestPhase::WaitRevert;
							playtestDeadline = playtestNow +
								std::chrono::seconds(30);
						}
					}
				}
				else if (playtestPhase == PlaytestPhase::WaitRevert)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						// clean revert: session gone, panels back on the edit
						// scene, editor world untouched
						if (gameObjectManager.getGameObjects().size() !=
							playtestLocalObjects)
						{
							playtestFailed = true;
							playtestFailure = "editor scene was modified by "
								"the play session";
						}
						else if (playSession.process != nullptr ||
							playSession.client.isConnected())
						{
							playtestFailed = true;
							playtestFailure =
								"session not fully torn down after revert";
						}
						else
						{
							SDL_Log("orkige_editor: playtest PASSED (%s "
								"path): clean revert to edit mode, %zu "
								"objects intact", playtestCrash ? "crash"
								: "stop", playtestLocalObjects);
							playtestPhase = PlaytestPhase::Done;
							running = false;
						}
					}
				}
				if (!playtestFailed &&
					playtestPhase != PlaytestPhase::Idle &&
					playtestPhase != PlaytestPhase::Done &&
					playtestNow >= playtestDeadline)
				{
					playtestFailed = true;
					playtestFailure = "deadline exceeded in phase " +
						std::to_string(static_cast<int>(playtestPhase));
				}
				if (playtestFailed)
				{
					SDL_Log("orkige_editor: playtest FAILED - %s",
						playtestFailure.c_str());
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 90 && selfCheck)
			{
				// self-check: full scene round-trip through the serializer -
				// snapshot the world, save it, clear it (scene nodes go with
				// the components), reload it and require identical GameObjects
				// and transforms plus a sane scene node count
				const char* selfCheckSceneEnv =
					std::getenv("ORKIGE_EDITOR_SELFCHECK_SCENE");
				const std::string selfCheckScene = selfCheckSceneEnv
					? selfCheckSceneEnv : "selfcheck.oscene";
				struct ObjectSnapshot
				{
					std::string id;
					Ogre::Vector3 position;
					Ogre::Quaternion orientation;
				};
				std::vector<ObjectSnapshot> before;
				for (auto const& [id, gameObject] :
					gameObjectManager.getGameObjects())
				{
					if (gameObject->hasComponent<Orkige::TransformComponent>())
					{
						Orkige::TransformComponent* transform = gameObject
							->getComponentPtr<Orkige::TransformComponent>();
						before.push_back({ id, transform->getPosition(),
							transform->getOrientation() });
					}
				}
				const unsigned short nodesBefore =
					sceneManager->getRootSceneNode()->numChildren();
				bool roundTripOk = !before.empty() &&
					Orkige::SceneSerializer::saveScene(selfCheckScene,
						gameObjectManager);
				gameObjectManager.clear();
				const unsigned short nodesCleared =
					sceneManager->getRootSceneNode()->numChildren();
				roundTripOk = roundTripOk &&
					gameObjectManager.getGameObjects().empty() &&
					nodesCleared < nodesBefore;
				roundTripOk = roundTripOk &&
					Orkige::SceneSerializer::loadScene(selfCheckScene,
						gameObjectManager);
				applyUnlitFixToLoadedModels(editorCore);
				const unsigned short nodesAfter =
					sceneManager->getRootSceneNode()->numChildren();
				roundTripOk = roundTripOk &&
					gameObjectManager.getGameObjects().size() == before.size() &&
					nodesAfter == nodesBefore;
				for (ObjectSnapshot const& snapshot : before)
				{
					optr<Orkige::GameObject> gameObject =
						gameObjectManager.getGameObject(snapshot.id).lock();
					if (!gameObject || !gameObject
						->hasComponent<Orkige::TransformComponent>())
					{
						SDL_Log("orkige_editor: selfcheck frame 90 - '%s' "
							"missing after reload", snapshot.id.c_str());
						roundTripOk = false;
						continue;
					}
					Orkige::TransformComponent* transform = gameObject
						->getComponentPtr<Orkige::TransformComponent>();
					const Ogre::Vector3 position = transform->getPosition();
					const Ogre::Quaternion orientation =
						transform->getOrientation();
					SDL_Log("orkige_editor: selfcheck frame 90 - '%s' pos "
						"before (%.3f, %.3f, %.3f) after (%.3f, %.3f, %.3f)",
						snapshot.id.c_str(), snapshot.position.x,
						snapshot.position.y, snapshot.position.z,
						position.x, position.y, position.z);
					const bool positionOk =
						position.positionEquals(snapshot.position, 1e-4f);
					const bool orientationOk = std::abs(
						orientation.Dot(snapshot.orientation)) > 0.9999f;
					roundTripOk = roundTripOk && positionOk && orientationOk;
				}
				SDL_Log("orkige_editor: selfcheck frame 90 - scene round-trip "
					"via '%s': %zu objects, root nodes %u -> %u -> %u: %s",
					selfCheckScene.c_str(), before.size(),
					static_cast<unsigned>(nodesBefore),
					static_cast<unsigned>(nodesCleared),
					static_cast<unsigned>(nodesAfter),
					roundTripOk ? "OK" : "FAILED");
				if (!roundTripOk)
				{
					SDL_Log("orkige_editor: FAILED selfcheck (scene round-trip)");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 80 && resizeTest)
			{
				// resize robustness: shrink the window mid-run; the SDL
				// resize events keep the OGRE window and (via the Scene
				// panel + hysteresis) the RTT in sync - must not crash
				SDL_SetWindowSize(window, 1000, 640);
				SDL_Log("orkige_editor: resize test - SDL_SetWindowSize"
					"(1000, 640) issued at frame 80");
			}
			if (frameCount == 100 && resizeTest)
			{
				SDL_Log("orkige_editor: resize test frame 100 - render window "
					"%ux%u, scene RTT %dx%d",
					engine.getRenderWindow()->getWidth(),
					engine.getRenderWindow()->getHeight(),
					sceneTarget.width, sceneTarget.height);
			}
			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

		// editor shutdown while a play session is live: ask the player to
		// quit, give it a short moment, then endPlaySession reaps/kills it
		if (playSession.isActive())
		{
			if (playSession.client.isConnected())
			{
				playSession.client.send(
					Orkige::DebugMessage(Protocol::MSG_QUIT));
				const std::chrono::steady_clock::time_point quitDeadline =
					std::chrono::steady_clock::now() +
					std::chrono::milliseconds(PLAY_STOP_GRACE_MS);
				int playerExitCode = 0;
				while (std::chrono::steady_clock::now() < quitDeadline &&
					!SDL_WaitProcess(playSession.process, false,
						&playerExitCode))
				{
					playSession.client.update();
					SDL_Delay(10);
				}
			}
			endPlaySession(playSession, "editor shutdown");
		}

		sceneManager->removeRenderQueueListener(&overlaySystem);
	}

	SDL_DestroyWindow(window);
	SDL_Quit();
	return exitCode;
}
