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
#include <engine_input/InputManager.h>
#include <engine_util/NodeUtil.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
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

#include "ImGuiSDL3Input.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" void* orkige_native_window_handle(SDL_Window* window);

namespace
{

// quit-on-ESC through the engine input pipeline (SDL event -> InputManager ->
// GlobalEventManager -> listener) - also proves that non-ImGui input still
// reaches the engine
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

// Editor UI state that lives across frames.
struct EditorState
{
	bool quitRequested = false;
	std::string selectedObjectId;
	int cubeCounter = 0;
	int meshCounter = 0;
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
	//! orbit camera: spherical coordinates around the scene origin; defaults
	//! reproduce the old fixed camera at (0, 2.5, 9) looking at the origin
	float orbitYawDeg = 0.0f;
	float orbitPitchDeg = 15.524f;
	float orbitDistance = 9.3408f;
	bool orbitActive = false;
};

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

// place the scene camera on its orbit sphere around the scene origin
void applyOrbitCamera(EditorState const& state, Ogre::SceneNode* cameraNode)
{
	const Ogre::Radian yaw(Ogre::Degree(state.orbitYawDeg));
	const Ogre::Radian pitch(Ogre::Degree(state.orbitPitchDeg));
	const Ogre::Vector3 offset(
		Ogre::Math::Cos(pitch) * Ogre::Math::Sin(yaw),
		Ogre::Math::Sin(pitch),
		Ogre::Math::Cos(pitch) * Ogre::Math::Cos(yaw));
	cameraNode->setPosition(offset * state.orbitDistance);
	cameraNode->lookAt(Ogre::Vector3::ZERO, Ogre::Node::TS_WORLD);
}

// vertex-coloured unit cube as a ManualObject (same technique as the
// hello_orkige demo: exercises the RTSS pipeline without any asset files)
Ogre::ManualObject* buildColoredCube(Ogre::SceneManager* sceneManager,
	std::string const& meshName, float halfExtent)
{
	Ogre::ManualObject* cube = sceneManager->createManualObject(meshName);
	cube->begin("VertexColour", Ogre::RenderOperation::OT_TRIANGLE_LIST);
	const float s = halfExtent;
	const Ogre::Vector3 corners[8] = {
		{-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s},
		{-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s},
	};
	const Ogre::ColourValue colors[8] = {
		Ogre::ColourValue(1, 0, 0), Ogre::ColourValue(0, 1, 0),
		Ogre::ColourValue(0, 0, 1), Ogre::ColourValue(1, 1, 0),
		Ogre::ColourValue(1, 0, 1), Ogre::ColourValue(0, 1, 1),
		Ogre::ColourValue(1, 1, 1), Ogre::ColourValue(0.5f, 0.5f, 0.5f),
	};
	for (int i = 0; i < 8; ++i)
	{
		cube->position(corners[i]);
		cube->colour(colors[i]);
	}
	const int quads[6][4] = {
		{0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0},
	};
	for (const int* q : quads)
	{
		cube->quad(q[0], q[1], q[2], q[3]);
	}
	cube->end();
	return cube;
}

// create a GameObject with a TransformComponent carrying a coloured cube
bool createCubeGameObject(Orkige::GameObjectManager& gameObjectManager,
	Ogre::SceneManager* sceneManager, std::string const& id,
	Ogre::Vector3 const& position)
{
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.createGameObject(id).lock();
	if (!gameObject || !gameObject->addComponent<Orkige::TransformComponent>())
	{
		return false;
	}
	Orkige::TransformComponent* transform =
		gameObject->getComponentPtr<Orkige::TransformComponent>();
	transform->attachObject(buildColoredCube(sceneManager, id + "_mesh", 0.8f));
	transform->setPosition(position);
	return true;
}

// create an Ogre::Entity from the generated glTF test asset (loaded through
// the statically linked Codec_Assimp plugin; the .glb comes from
// Util/make_test_mesh.py). Codec_Assimp already sets TVC_DIFFUSE on the
// synthesized material because the mesh carries COLOR_0 vertex colours, but
// it also generates normals (aiProcess_GenNormals), which keeps lighting
// enabled - under the editor's ambient-only light the colours would drown.
// Render it unlit, exactly like the manual cubes.
Ogre::Entity* createTestMeshEntity(Ogre::SceneManager* sceneManager,
	std::string const& entityName)
{
	Ogre::Entity* entity =
		sceneManager->createEntity(entityName, "test_mesh.glb");
	for (unsigned int i = 0; i < entity->getNumSubEntities(); ++i)
	{
		Ogre::Pass* pass = entity->getSubEntity(i)->getMaterial()
			->getTechnique(0)->getPass(0);
		pass->setLightingEnabled(false);
		pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
	}
	return entity;
}

// create a GameObject with a TransformComponent carrying the glTF test mesh
bool createTestMeshGameObject(Orkige::GameObjectManager& gameObjectManager,
	Ogre::SceneManager* sceneManager, std::string const& id,
	Ogre::Vector3 const& position)
{
	Ogre::Entity* entity = nullptr;
	try
	{
		entity = createTestMeshEntity(sceneManager, id + "_entity");
	}
	catch (Ogre::Exception const& e)
	{
		SDL_Log("orkige_editor: test mesh load failed: %s",
			e.getDescription().c_str());
		return false;
	}
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.createGameObject(id).lock();
	if (!gameObject || !gameObject->addComponent<Orkige::TransformComponent>())
	{
		return false;
	}
	Orkige::TransformComponent* transform =
		gameObject->getComponentPtr<Orkige::TransformComponent>();
	transform->attachObject(entity);
	transform->setPosition(position);
	return true;
}

// viewport click-picking: cast a camera ray through the click point and
// select the nearest hit that belongs to a GameObject (a TransformComponent
// tags its scene nodes, NodeUtil walks a hit back to the owner). AABB-level
// picking is right for the editor bootstrap; polygon-accurate picking via
// CollisionTools comes when entities with real meshes arrive.
bool pickObjectAtCursor(EditorState& state, Ogre::Camera* camera,
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
			state.selectedObjectId = gameObject->getObjectID();
			picked = true;
			break;
		}
	}
	sceneManager->destroyQuery(query);
	if (!picked)
	{
		// clicking empty space deselects, like Unity
		state.selectedObjectId.clear();
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

// GameObject > Create Cube: auto-named, at origin
void createCubeFromMenu(EditorState& state,
	Orkige::GameObjectManager& gameObjectManager,
	Ogre::SceneManager* sceneManager)
{
	std::string id;
	do
	{
		++state.cubeCounter;
		id = "Cube" + std::to_string(state.cubeCounter);
	} while (gameObjectManager.objectExists(id));
	if (createCubeGameObject(gameObjectManager, sceneManager, id,
		Ogre::Vector3::ZERO))
	{
		state.selectedObjectId = id;
	}
}

// GameObject > Create Test Mesh: auto-named, at origin
void createTestMeshFromMenu(EditorState& state,
	Orkige::GameObjectManager& gameObjectManager,
	Ogre::SceneManager* sceneManager)
{
	std::string id;
	do
	{
		++state.meshCounter;
		id = "TestMesh" + std::to_string(state.meshCounter);
	} while (gameObjectManager.objectExists(id));
	if (createTestMeshGameObject(gameObjectManager, sceneManager, id,
		Ogre::Vector3::ZERO))
	{
		state.selectedObjectId = id;
	}
}

// selfcheck helper: compute the viewport-normalized Scene-panel position of
// a GameObject from the RTT camera and run it through pickObjectAtCursor -
// the same function the Scene panel's mouse path calls (the panel image
// fills the panel content region, so panel-relative and viewport-normalized
// coordinates coincide). Returns false if the object is missing or behind
// the camera.
bool pickGameObjectThroughScenePanel(EditorState& state,
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
	pickObjectAtCursor(state, camera, sceneManager, normalizedX, normalizedY);
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

void drawMenuBar(EditorState& state,
	Orkige::GameObjectManager& gameObjectManager,
	Ogre::SceneManager* sceneManager)
{
	bool openAbout = false;
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Quit", "Esc"))
			{
				state.quitRequested = true;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Create Cube"))
			{
				createCubeFromMenu(state, gameObjectManager, sceneManager);
			}
			if (ImGui::MenuItem("Create Test Mesh"))
			{
				createTestMeshFromMenu(state, gameObjectManager, sceneManager);
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
}

// Fullscreen dockspace over the main viewport (below the main menu bar,
// which claims its part of the work area first). The first run builds the
// default layout programmatically with the DockBuilder: Hierarchy left,
// Inspector right, Lua Console + Stats tabbed at the bottom, Scene panel
// filling the centre. Afterwards the layout persists through imgui.ini
// (stored next to the executable, see main()) and the builder stays out of
// the way.
void drawDockspace(EditorState& state)
{
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	const ImGuiID dockspaceId =
		ImGui::DockSpaceOverViewport(0, mainViewport);
	if (state.dockLayoutChecked ||
		mainViewport->WorkSize.x <= 0.0f || mainViewport->WorkSize.y <= 0.0f)
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
	ImGui::DockBuilderSetNodeSize(dockspaceId,
		ImGui::GetMainViewport()->WorkSize);
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
	ImGui::DockBuilderDockWindow("Scene Hierarchy", leftId);
	ImGui::DockBuilderDockWindow("Inspector", rightId);
	ImGui::DockBuilderDockWindow("Lua Console", bottomId);
	ImGui::DockBuilderDockWindow("Stats", bottomId);
	ImGui::DockBuilderDockWindow("Scene", centerId);
	ImGui::DockBuilderFinish(dockspaceId);
}

// The Scene panel: displays the offscreen scene texture, records the size
// the RTT should have (applied with hysteresis in the frame loop) and hosts
// the in-panel interactions - left click picks (panel-relative mouse coords
// map 1:1 to viewport-normalized coords because the image always fills the
// content region), right-drag orbits, scroll zooms.
void drawScenePanel(EditorState& state, SceneRenderTarget& sceneTarget,
	Ogre::SceneManager* sceneManager, Ogre::SceneNode* cameraNode)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin("Scene");
	ImGui::PopStyleVar();
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
			ImGuiIO& io = ImGui::GetIO();
			if (ImGui::IsItemHovered())
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					const ImVec2 rectMin = ImGui::GetItemRectMin();
					pickObjectAtCursor(state, sceneTarget.camera, sceneManager,
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
			}
			// an orbit drag keeps going while the button is held, even when
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
			applyOrbitCamera(state, cameraNode);
		}
	}
	ImGui::End();
}

void drawHierarchyPanel(EditorState& state,
	Orkige::GameObjectManager& gameObjectManager)
{
	if (ImGui::Begin("Scene Hierarchy"))
	{
		for (auto const& [id, gameObject] : gameObjectManager.getGameObjects())
		{
			const bool selected = (state.selectedObjectId == id);
			if (ImGui::Selectable(id.c_str(), selected))
			{
				state.selectedObjectId = id;
			}
		}
	}
	ImGui::End();
}

void drawTransformComponentUI(Orkige::TransformComponent* transform)
{
	const Ogre::Vector3 p = transform->getPosition();
	float position[3] = { p.x, p.y, p.z };
	if (ImGui::DragFloat3("Position", position, 0.05f))
	{
		transform->setPosition(
			Ogre::Vector3(position[0], position[1], position[2]));
	}
	const Ogre::Quaternion q = transform->getOrientation();
	float yawPitchRoll[3] = {
		q.getYaw().valueDegrees(),
		q.getPitch().valueDegrees(),
		q.getRoll().valueDegrees(),
	};
	if (ImGui::DragFloat3("Yaw/Pitch/Roll", yawPitchRoll, 0.5f))
	{
		Ogre::Matrix3 rotation;
		rotation.FromEulerAnglesYXZ(
			Ogre::Degree(yawPitchRoll[0]),
			Ogre::Degree(yawPitchRoll[1]),
			Ogre::Degree(yawPitchRoll[2]));
		transform->setOrientation(Ogre::Quaternion(rotation));
	}
}

void drawInspectorPanel(EditorState& state,
	Orkige::GameObjectManager& gameObjectManager)
{
	if (ImGui::Begin("Inspector"))
	{
		optr<Orkige::GameObject> gameObject;
		if (!state.selectedObjectId.empty() &&
			gameObjectManager.objectExists(state.selectedObjectId))
		{
			gameObject =
				gameObjectManager.getGameObject(state.selectedObjectId).lock();
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
				if (auto* transform =
					dynamic_cast<Orkige::TransformComponent*>(component.get()))
				{
					drawTransformComponentUI(transform);
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

		// unlit vertex-colour material shared by all editor cubes
		Ogre::MaterialPtr cubeMaterial =
			Ogre::MaterialManager::getSingleton().create("VertexColour",
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		Ogre::Pass* cubePass = cubeMaterial->getTechnique(0)->getPass(0);
		cubePass->setLightingEnabled(false);
		cubePass->setVertexColourTracking(Ogre::TVC_DIFFUSE);

		// GameObject/component bridge (registers the component factories)
		init_module_orkige_engine();
		Orkige::GameObjectManager gameObjectManager;

		// boot scene: three sample cubes so the panels have content
		EditorState state;
		const Ogre::Vector3 bootPositions[3] = {
			{ -2.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 2.5f, 0.0f, 0.0f },
		};
		for (Ogre::Vector3 const& position : bootPositions)
		{
			++state.cubeCounter;
			const std::string id = "Cube" + std::to_string(state.cubeCounter);
			if (!createCubeGameObject(gameObjectManager, sceneManager, id,
				position))
			{
				SDL_Log("orkige_editor: FAILED - boot GameObject '%s' "
					"creation failed", id.c_str());
				return 1;
			}
		}
		// ... plus one glTF test mesh above the cubes: proves the Codec_Assimp
		// import path (registered in Engine.cpp's static-plugin block) at boot
		++state.meshCounter;
		if (!createTestMeshGameObject(gameObjectManager, sceneManager,
			"TestMesh" + std::to_string(state.meshCounter),
			Ogre::Vector3(0.0f, 2.2f, 0.0f)))
		{
			SDL_Log("orkige_editor: FAILED - boot test mesh GameObject "
				"creation failed");
			return 1;
		}
		state.selectedObjectId = "Cube2";

		// initial orbit pose reproduces the old fixed camera at (0, 2.5, 9)
		applyOrbitCamera(state, sceneCameraNode);

		// automation hooks (same env-hook style as the demo):
		// ORKIGE_DEMO_FRAMES=N exit 0 after N frames,
		// ORKIGE_DEMO_SCREENSHOT=path framebuffer dump at frame 60,
		// ORKIGE_EDITOR_SELFCHECK=1 boot-state assertions at frame 30 and
		// Scene-panel-picking checks at frames 45/65 (needs >= 65 frames),
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

		bool running = true;
		unsigned long frameCount = 0;
		while (running)
		{
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

			imguiInput.newFrame(
				static_cast<float>(engine.getRenderWindow()->getWidth()),
				static_cast<float>(engine.getRenderWindow()->getHeight()));
			Ogre::ImGuiOverlay::NewFrame();

			drawMenuBar(state, gameObjectManager, sceneManager);
			drawDockspace(state);
			drawScenePanel(state, sceneTarget, sceneManager, sceneCameraNode);
			drawHierarchyPanel(state, gameObjectManager);
			drawInspectorPanel(state, gameObjectManager);
			drawStatsPanel(engine.getRenderWindow());
#ifdef ORKIGE_LUA
			drawLuaConsolePanel(state, scriptManager);
#endif

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
				state.selectedObjectId.clear();
				if (!pickGameObjectThroughScenePanel(state, gameObjectManager,
					sceneTarget.camera, sceneManager, "Cube1"))
				{
					SDL_Log("orkige_editor: FAILED selfcheck (pick projection)");
					exitCode = 2;
					running = false;
				}
				SDL_Log("orkige_editor: selfcheck frame 45 - picked '%s' via "
					"scene panel pick", state.selectedObjectId.c_str());
				if (state.selectedObjectId != "Cube1")
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
				state.selectedObjectId.clear();
				if (!pickGameObjectThroughScenePanel(state, gameObjectManager,
					sceneTarget.camera, sceneManager, "TestMesh1"))
				{
					SDL_Log("orkige_editor: FAILED selfcheck (test mesh pick "
						"projection)");
					exitCode = 2;
					running = false;
				}
				SDL_Log("orkige_editor: selfcheck frame 65 - picked '%s' via "
					"scene panel pick", state.selectedObjectId.c_str());
				if (state.selectedObjectId != "TestMesh1")
				{
					SDL_Log("orkige_editor: FAILED selfcheck (test mesh "
						"scene panel pick)");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 60)
			{
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					engine.getRenderWindow()->writeContentsToFile(shotPath);
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

		sceneManager->removeRenderQueueListener(&overlaySystem);
	}

	SDL_DestroyWindow(window);
	SDL_Quit();
	return exitCode;
}
