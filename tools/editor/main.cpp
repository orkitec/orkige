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
#include <imgui.h>

#include "ImGuiSDL3Input.h"

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
};

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

// project a world position to SDL window coordinates (for the picking
// self-check); returns false if the position is behind the camera
bool worldToWindowPoint(Ogre::Camera* camera, Ogre::Vector3 const& worldPos,
	float windowWidth, float windowHeight, float& outX, float& outY)
{
	const Ogre::Vector4 clip = camera->getProjectionMatrix() *
		(camera->getViewMatrix() * Ogre::Vector4(worldPos.x, worldPos.y,
			worldPos.z, 1.0f));
	if (clip.w <= 0.0f)
	{
		return false;
	}
	outX = (clip.x / clip.w * 0.5f + 0.5f) * windowWidth;
	outY = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * windowHeight;
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

// push a real SDL left-click at a GameObject's projected screen position;
// the click travels the exact user path (SDL queue -> ImGui capture test ->
// pickObjectAtCursor). Selfcheck helper; returns false if the object is
// missing or behind the camera.
bool pushClickOnGameObject(Orkige::GameObjectManager& gameObjectManager,
	Ogre::Camera* camera, SDL_Window* window, std::string const& id)
{
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.getGameObject(id).lock();
	int windowW = 0;
	int windowH = 0;
	SDL_GetWindowSize(window, &windowW, &windowH);
	float clickX = 0.0f;
	float clickY = 0.0f;
	if (!gameObject || !worldToWindowPoint(camera,
		gameObject->getComponentPtr<Orkige::TransformComponent>()
			->getPosition(),
		static_cast<float>(windowW), static_cast<float>(windowH),
		clickX, clickY))
	{
		return false;
	}
	SDL_Event clickEvent{};
	clickEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	clickEvent.button.button = SDL_BUTTON_LEFT;
	clickEvent.button.x = clickX;
	clickEvent.button.y = clickY;
	SDL_PushEvent(&clickEvent);
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

void drawHierarchyPanel(EditorState& state,
	Orkige::GameObjectManager& gameObjectManager)
{
	ImGui::SetNextWindowPos(ImVec2(10, 35), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(250, 330), ImGuiCond_FirstUseEver);
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
	ImGui::SetNextWindowPos(ImVec2(950, 35), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
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
	ImGui::SetNextWindowPos(ImVec2(10, 375), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(250, 130), ImGuiCond_FirstUseEver);
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
	ImGui::SetNextWindowPos(ImVec2(270, 460), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(670, 250), ImGuiCond_FirstUseEver);
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
		engine.createDefaultCameraAndViewport();
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

		Ogre::SceneManager* sceneManager = engine.getSceneManager();
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

		engine.getCamera()->getParentSceneNode()->setPosition(0.0f, 2.5f, 9.0f);
		engine.getCamera()->getParentSceneNode()->lookAt(
			Ogre::Vector3::ZERO, Ogre::Node::TS_WORLD);

		// automation hooks (same env-hook style as the demo):
		// ORKIGE_DEMO_FRAMES=N exit 0 after N frames,
		// ORKIGE_DEMO_SCREENSHOT=path framebuffer dump at frame 60,
		// ORKIGE_EDITOR_SELFCHECK=1 boot-state assertions at frame 30 and
		// viewport-picking checks at frames 45-70 (needs >= 70 frames)
		unsigned long frameLimit = 0;
		if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
		{
			frameLimit = std::strtoul(demoFrames, nullptr, 10);
		}
		const bool selfCheck = (std::getenv("ORKIGE_EDITOR_SELFCHECK") != nullptr);

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
				// ImGui gets every event first; only forward into the engine
				// input pipeline what ImGui does not capture
				if (!imguiInput.processEvent(event))
				{
					// left click in the 3D viewport selects the GameObject
					// under the cursor (or deselects on empty space)
					if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
						event.button.button == SDL_BUTTON_LEFT)
					{
						int windowW = 0;
						int windowH = 0;
						SDL_GetWindowSize(window, &windowW, &windowH);
						if (windowW > 0 && windowH > 0)
						{
							pickObjectAtCursor(state, engine.getCamera(),
								sceneManager,
								event.button.x / static_cast<float>(windowW),
								event.button.y / static_cast<float>(windowH));
						}
					}
					inputManager.injectEvent(event);
				}
			}
			if (quitOnEscape.quitRequested || state.quitRequested)
			{
				running = false;
			}

			imguiInput.newFrame(
				static_cast<float>(engine.getRenderWindow()->getWidth()),
				static_cast<float>(engine.getRenderWindow()->getHeight()));
			Ogre::ImGuiOverlay::NewFrame();

			drawMenuBar(state, gameObjectManager, sceneManager);
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
				SDL_Log("orkige_editor: selfcheck frame 30 - gameobjects=%zu "
					"(boot cubes + test mesh %s), test_mesh.glb resource %s, "
					"imgui vertices=%d",
					gameObjectManager.getGameObjects().size(),
					objectsOk ? "present" : "MISSING",
					meshResourceOk ? "loaded" : "NOT LOADED", imguiVertices);
				if (!objectsOk || !meshResourceOk || imguiVertices <= 0)
				{
					SDL_Log("orkige_editor: FAILED selfcheck");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 45 && selfCheck)
			{
				// self-check: viewport picking - clear the selection, then
				// push a real SDL click at Cube1's projected screen position
				state.selectedObjectId.clear();
				if (!pushClickOnGameObject(gameObjectManager,
					engine.getCamera(), window, "Cube1"))
				{
					SDL_Log("orkige_editor: FAILED selfcheck (pick projection)");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 50 && selfCheck)
			{
				SDL_Log("orkige_editor: selfcheck frame 50 - picked '%s' via "
					"viewport click", state.selectedObjectId.c_str());
				if (state.selectedObjectId != "Cube1")
				{
					SDL_Log("orkige_editor: FAILED selfcheck (viewport pick)");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 65 && selfCheck)
			{
				// self-check: same viewport-picking path, this time on the
				// glTF test mesh (its Entity goes through the identical
				// TransformComponent scene-node tagging as the cubes)
				state.selectedObjectId.clear();
				if (!pushClickOnGameObject(gameObjectManager,
					engine.getCamera(), window, "TestMesh1"))
				{
					SDL_Log("orkige_editor: FAILED selfcheck (test mesh pick "
						"projection)");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 70 && selfCheck)
			{
				SDL_Log("orkige_editor: selfcheck frame 70 - picked '%s' via "
					"viewport click", state.selectedObjectId.c_str());
				if (state.selectedObjectId != "TestMesh1")
				{
					SDL_Log("orkige_editor: FAILED selfcheck (test mesh "
						"viewport pick)");
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
