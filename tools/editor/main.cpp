// orkige_editor - the in-engine editor shell (bootstrap).
//
// A Unity-like editor built as a regular Orkige app: SDL3 owns the window and
// event loop, Orkige::Engine renders into it (externalWindowHandle path, same
// boot sequence as samples/hello_orkige), and the UI is Dear ImGui drawn
// through the engine_render facade (ImGuiFacadeRenderer on DrawLayer2D).
//
// Renderer coupling (Docs/render-abstraction.md; decision #3 was REVISITED
// after the DrawLayer2D port): the editor builds and runs on BOTH render
// flavors. Everything scene-facing goes through the engine_render facade
// (RTT panel, picking, camera rig, grid, stats, resources, screenshots,
// ImGui itself); the one raw-Ogre corner left is the app-standard classic
// boot block (Engine ctor, ORKIGE_RENDERSYSTEM, RTSS internal media) with
// the EngineNext sibling behind #else, same as tools/player.
//
// Wiring choices:
// - ImGui renders as ONE facade 2D layer: ImGui draw data = textured
//   triangles + scissor rects = exactly the DrawLayer2D contract. The
//   editor owns the ImGui context; ImGuiFacadeRenderer uploads the font
//   atlas (RenderSystem::createTexture2D) and resubmits the draw data per
//   frame (see ImGuiFacadeRenderer.h).
// - Input goes to ImGui first (ImGuiSDL3Input); events ImGui wants captured
//   are NOT forwarded to the engine InputManager, everything else follows the
//   same injectEvent flow as the demo.
// - The UI is a full-window dockspace (docking-enabled imgui): the 3D scene
//   renders offscreen into a facade RenderTexture shown by the "Scene"
//   panel via ImGui::Image (the RTT handle binds through the DrawLayer2D
//   RenderTexture overload), the window itself is UI-only
//   (RenderSystem::showUIOnlyWindow - dark grey + the ImGui layer).
//   Picking and camera orbit/zoom happen through the Scene panel
//   (drawScenePanel).
#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/MeshInstance.h>
#include <engine_base/EngineLog.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_input/InputManager.h>
#include <engine_render/DrawLayer2D.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_project/Project.h>
#include <core_util/StringUtil.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>

#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName (the selfcheck's panel probes)
#include <ImGuizmo.h>

#include "EditorCamera.h"
#include "EditorCore.h"
#include "EditorTheme.h"
#include "FileDialog.h"
#include "ImGuiFacadeRenderer.h"
#include "ImGuiSDL3Input.h"
#include "MeshImport.h"
#ifdef __APPLE__
#include "MacMenu.h" // native menu bar (replaces the ImGui menu bar on mac)
#endif

#include "EditorApp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>	// the break-variant's filtered project copy
#include <mutex>
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
		// the Console line store exists before anything logs; the editor's
		// own SDL_Log lines route into it (and still reach the previous
		// output) from here on
		EditorConsole console;
		SdlLogHook sdlLogHook;
		sdlLogHook.console = &console;
		SDL_GetLogOutputFunction(&sdlLogHook.previous,
			&sdlLogHook.previousUserdata);
		SDL_SetLogOutputFunction(consoleSdlLogOutput, &sdlLogHook);

		// engine singletons normally created by Orkige::Application; the
		// editor boots the same set as the hello_orkige demo
		Orkige::Timer::initialise();
		Orkige::GlobalEventManager eventManager;
		// the scripting seam must exist before the module init functions run
		// so OrkigeMetaExport reaches the real backend state
		Orkige::ScriptRuntime scriptRuntime;
		init_module_orkige_core();

		// automated run? (any scripted-test/automation hook set) - decided
		// up front because it gates the vsync choice below (before
		// Engine::setup) and the reopen-last-project convenience: scripted
		// runs must render uncapped and start exactly where the script
		// expects (an empty untitled scene), never in yesterday's project
		const bool automatedRun =
			std::getenv("ORKIGE_DEMO_FRAMES") != nullptr ||
			std::getenv("ORKIGE_DEMO_SCREENSHOT") != nullptr ||
			std::getenv("ORKIGE_EDITOR_SELFCHECK") != nullptr ||
			std::getenv("ORKIGE_EDITOR_RESIZE_TEST") != nullptr ||
			std::getenv("ORKIGE_EDITOR_EDITTEST") != nullptr ||
			std::getenv("ORKIGE_EDITOR_OPEN_SCENE") != nullptr ||
			std::getenv("ORKIGE_EDITOR_PLAYTEST") != nullptr ||
			std::getenv("ORKIGE_EDITOR_EXPORT_EXAMPLE") != nullptr ||
			std::getenv("ORKIGE_EDITOR_PROJECT_TEST") != nullptr ||
			std::getenv("ORKIGE_EDITOR_NATIVE_PLAYTEST") != nullptr ||
			std::getenv("ORKIGE_EDITOR_SCRIPT_ERROR_PLAYTEST") != nullptr;

		// ORKIGE_SANCTIONED_OGRE_BEGIN(classic-boot) - lint gate, see Util/ogre_containment.json
		// --- per-flavor boot block (the WP-A1.3 app rule + B3 flavor split,
		// Docs/render-abstraction.md - same shape as tools/player): on
		// classic, Engine construction/config, the RTSS-internal media and
		// the ORKIGE_RENDERSYSTEM pick stay classic plumbing; on the next
		// flavor the Engine sibling (engine_graphic/EngineNext.h) carries
		// the same parameters into RenderBackend::createRenderSystem. After
		// Engine::setup the editor talks to the engine_render facade on
		// BOTH flavors.
#ifdef ORKIGE_RENDER_CLASSIC
		Orkige::Engine engine(Ogre::SMT_DEFAULT,
			Orkige::StringUtil::BLANK, Orkige::StringUtil::BLANK,
			Orkige::StringUtil::BLANK, "orkige_editor.log");
#else
		Orkige::Engine engine("orkige_editor.log");
#endif
		engine.setCustomWindowParam("width", "1280");
		engine.setCustomWindowParam("height", "720");
		if (!automatedRun)
		{
			// a HUMAN run gets vsync (same rule as the player and the jumper
			// sample): an uncapped editor renders thousands of UI frames per
			// second for no benefit - automated runs stay uncapped so the
			// frame-scripted tests finish as fast as the machine allows
			engine.setCustomWindowParam("vsync", "true");
		}

#ifdef ORKIGE_RENDER_CLASSIC
		// ORKIGE_RENDERSYSTEM: explicit render system choice ("Vulkan",
		// "Metal", "GL3Plus", "GL" - see Engine::matchRenderSystemName);
		// unset keeps the default (first available, i.e. GL3Plus). Vulkan
		// (MoltenVK on macOS) has full RTSS support; OGRE 14.5's Metal RS
		// does not (no MSL backend - built-in default shaders only).
		// (The next flavor boots Ogre-Next's Metal RS unconditionally -
		// the graphics-API pick is a classic-backend concern.)
		if (const char* renderSystemEnv = std::getenv("ORKIGE_RENDERSYSTEM"))
		{
			engine.setPreferredRenderSystem(renderSystemEnv);
		}
#endif

		// engine log -> Console: the engine_base log-capture service (shared
		// with PlayerDebugLink) queues every line from here on; the frame
		// loop drains it into the Console once per frame. The engine log
		// exists once the Engine ctor ran, so attach cannot fail here. Sized
		// to the Console cap - the OGRE boot easily exceeds the default
		// backlog and the Console wants those lines.
		Orkige::EngineLogCapture engineLogCapture(EditorConsole::MAX_LINES);
		if (!engineLogCapture.attach())
		{
			SDL_Log("orkige_editor: engine log capture failed to attach - "
				"the Console will miss the engine log");
		}

#ifdef ORKIGE_RENDER_CLASSIC
		// RTSS shader library + OgreUnifiedShader.h, same locations
		// OgreBites::ApplicationContext registers (see CMakeLists.txt) -
		// backend-internal media, must precede Engine::setup. (The next
		// flavor's Hlms media is a built-in default of its Engine sibling.)
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_EDITOR_MEDIA_DIR "/Main", "FileSystem", Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_EDITOR_MEDIA_DIR "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);
#endif

		if (!engine.setup("Orkige Editor", Orkige::Engine::SHOW_NEVER,
			Orkige::StringUtil::Converter::toString(
				reinterpret_cast<size_t>(orkige_native_window_handle(window)))))
		{
			SDL_Log("Engine::setup failed");
			return 1;
		}
		// ORKIGE_SANCTIONED_OGRE_END
		// --- end of the boot block: from here on the editor talks to the
		// renderer through the engine_render facade on BOTH flavors
		Orkige::RenderSystem* render = Orkige::RenderSystem::get();
		Orkige::RenderWorld* world = render->getWorld();

		// sample assets (test_mesh.glb from Util/make_test_mesh.py) in the
		// default group; meshes load lazily via Codec_Assimp on mesh load
		render->addResourceLocation(ORKIGE_EDITOR_ASSET_DIR);
		// jumper sample assets (textured .glb meshes from
		// Util/make_jumper_assets.py) so samples/jumper/level1.oscene opens
		render->addResourceLocation(ORKIGE_EDITOR_JUMPER_ASSET_DIR);

		// The scene no longer renders into the window (that was
		// Engine::createDefaultCameraAndViewport): the editor's scene camera
		// draws into the offscreen facade RenderTexture created below on a
		// facade camera rig (near/far defaults 1/100000 match the historical
		// editor camera).
		optr<Orkige::RenderCamera> sceneCamera =
			world->createCamera("EditorSceneCamera");
		optr<Orkige::RenderNode> sceneCameraNode =
			world->createNode("EditorSceneCameraNode");
		sceneCamera->attachTo(sceneCameraNode);

		// UI-only main window (facade): only the window background colour
		// and the ImGui 2D layer reach the screen - all scene content
		// renders offscreen into the Scene panel's RenderTexture. The
		// colour matches the theme's dockspace background (EditorTheme
		// DOCKSPACE_BG).
		render->setWindowBackgroundColour(
			Orkige::Color(0.102f, 0.102f, 0.102f));
		render->showUIOnlyWindow();
		render->initialiseResourceGroups();
		world->setAmbientLight(Orkige::Color(0.2f, 0.2f, 0.2f));

		// Dear ImGui through the engine_render facade: the editor owns the
		// context; ImGuiFacadeRenderer draws it as one DrawLayer2D (works
		// identically on both render flavors - see ImGuiFacadeRenderer.h).
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		// Theme + system font BEFORE ImGuiFacadeRenderer::initialise: it
		// builds + uploads the font atlas exactly once - fonts added later
		// would need an atlas re-upload nothing triggers. The San
		// Francisco font is loaded from the OS at runtime (never
		// redistributed); if it is unavailable the ImGui default font stays.
		// contentScale maps window points to render-target pixels (the space
		// ImGui works in here) so retina surfaces get a crisp atlas.
		float editorContentScale = 1.0f;
		{
			int sdlWindowWidth = 0;
			int sdlWindowHeight = 0;
			SDL_GetWindowSize(window, &sdlWindowWidth, &sdlWindowHeight);
			unsigned int drawableWidth = 0;
			unsigned int drawableHeight = 0;
			render->getWindowSize(drawableWidth, drawableHeight);
			if (sdlWindowWidth > 0 && drawableWidth > 0)
			{
				editorContentScale =
					static_cast<float>(drawableWidth) /
					static_cast<float>(sdlWindowWidth);
			}
		}
		Orkige::applyMacDarkTheme(ImGui::GetStyle(), editorContentScale);
		if (!Orkige::loadMacSystemFont(ImGui::GetIO(), 14.0f,
			editorContentScale))
		{
			SDL_Log("orkige_editor: system font unavailable - using the "
				"ImGui default font");
		}
		Orkige::ImGuiFacadeRenderer imguiRenderer;
		if (!imguiRenderer.initialise(300 /*layer zOrder*/))
		{
			SDL_Log("orkige_editor: ImGui facade renderer failed to initialise");
			return 1;
		}
		gImGuiRenderer = &imguiRenderer;

		// docking UI: full-window dockspace (drawDockspace). The panel layout
		// persists through imgui.ini stored NEXT TO THE EXECUTABLE
		// (SDL_GetBasePath), so it works no matter which cwd the editor is
		// launched from. Static so the path outlives the ImGui context - the
		// ini gets written during ImGui::DestroyContext (teardown below).
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		const char* sdlBasePath = SDL_GetBasePath();
		static const std::string imguiIniPath =
			std::string(sdlBasePath ? sdlBasePath : "") +
			"orkige_editor_imgui.ini";
		ImGui::GetIO().IniFilename = imguiIniPath.c_str();

		// viewport settings (grid/orientation gizmo/camera feel) persist in
		// a simple key=value file next to the imgui ini
		ViewSettings viewSettings;
		viewSettings.path = std::string(sdlBasePath ? sdlBasePath : "") +
			"orkige_editor_view.ini";
		viewSettings.load();
		// scene open/save feed File > Open Recent through this pointer
		gViewSettings = &viewSettings;
		// scripted runs must not rewrite the user's recents (see gRecordRecents)
		gRecordRecents = !automatedRun;
		sceneCamera->setFOVy(Orkige::Degree(viewSettings.fovDeg));

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

		// unlit vertex-colour material + the shared "EditorCube.mesh"
		// resource through the facade cube-mesh service (a real mesh, so
		// cubes go through ModelComponent and round-trip through scene
		// files - the player builds the identical resource); the default
		// name IS RenderWorld::CUBE_MESH_NAME
		world->createVertexColourCubeMesh();

		// ground-plane reference grid (editor-only, toggled via View menu,
		// invisible to picking, not part of the GameObject world) - a
		// facade line-list mesh since the editor-on-Next port, see
		// createEditorGrid
		optr<Orkige::RenderNode> gridNode = world->createNode("EditorGridNode");
		optr<Orkige::MeshInstance> gridMesh = createEditorGrid(world, gridNode);
		gridNode->setVisible(viewSettings.showGrid);

		// GameObject/component bridge (registers the component factories)
		init_module_orkige_engine();
		Orkige::GameObjectManager gameObjectManager;

		// the UI-independent editor logic (selection, tools, undo/redo,
		// object operations) - everything below drives THIS layer
		Orkige::EditorCore editorCore(gameObjectManager);
		quitOnEscape.editorCore = &editorCore;
		// persisted snap settings (toolbar toggle + editable step values);
		// scripted runs keep the factory defaults - the edittest asserts them
		if (!automatedRun)
		{
			editorCore.setSnapEnabled(viewSettings.snapEnabled);
			editorCore.setSnapValues(viewSettings.snapTranslate,
				viewSettings.snapRotateDegrees, viewSettings.snapScale);
		}

		// play mode session (idle until the Play button / playtest hook)
		PlaySession playSession;
		// project export job (idle until Build > Build for <platform>)
		ExportJob exportJob;
#ifdef __APPLE__
		// ORKIGE_EDITOR_PLAY_SIMULATOR: preselect an iOS simulator as the
		// play target (the toolbar picker sets the same fields; the scripted
		// play tests use this to exercise the simulator deployment path).
		// A UDID is taken as-is. "auto" prefers a booted simulator with
		// OrkigePlayer.app installed and otherwise falls back to a SHUTDOWN
		// one (Play then boots it and auto-installs the built app).
		// "auto-shutdown" ONLY accepts a shutdown simulator - the scripted
		// boot-path test uses it so the flow is really exercised end to end.
		// Both SKIP the run (exit 77 = the ctest SKIP_RETURN_CODE) when no
		// suitable simulator exists, so the tests stay green on unprepared
		// machines. The install fallbacks require the built app on disk
		// (get_app_container cannot answer for shutdown devices).
		if (const char* simulatorSelector =
			std::getenv("ORKIGE_EDITOR_PLAY_SIMULATOR"))
		{
			const std::string selector(simulatorSelector);
			if (selector == "auto" || selector == "auto-shutdown")
			{
				const std::vector<SimulatorDevice> simulators =
					listSimulators();
				if (selector == "auto")
				{
					// fast path: a booted simulator that already has the app
					for (SimulatorDevice const& device : simulators)
					{
						if (device.booted &&
							simulatorPlayerInstalled(device.udid))
						{
							playSession.simulatorUdid = device.udid;
							playSession.simulatorLabel = device.name;
							break;
						}
					}
				}
				if (playSession.simulatorUdid.empty())
				{
					// boot path: a shutdown simulator qualifies when the
					// built app exists (Play boots it and installs the app)
					std::error_code ignored;
					if (std::filesystem::exists(ORKIGE_EDITOR_IOS_PLAYER_APP,
						ignored))
					{
						for (SimulatorDevice const& device : simulators)
						{
							if (!device.booted)
							{
								playSession.simulatorUdid = device.udid;
								playSession.simulatorLabel = device.name;
								break;
							}
						}
					}
				}
				if (playSession.simulatorUdid.empty())
				{
					SDL_Log("orkige_editor: play simulator '%s' - no "
						"suitable simulator (booted with OrkigePlayer.app "
						"installed, or shutdown + the app built at %s), "
						"skipping", selector.c_str(),
						ORKIGE_EDITOR_IOS_PLAYER_APP);
					return 77;
				}
				SDL_Log("orkige_editor: play simulator '%s' -> '%s' (%s)",
					selector.c_str(), playSession.simulatorLabel.c_str(),
					playSession.simulatorUdid.c_str());
			}
			else
			{
				playSession.simulatorUdid = simulatorSelector;
				playSession.simulatorLabel = simulatorSelector;
			}
		}
#endif
		// ORKIGE_EDITOR_PLAY_ANDROID: preselect an Android device/emulator
		// as the play target (same contract as ORKIGE_EDITOR_PLAY_SIMULATOR:
		// a serial is taken as-is, "auto" resolves to the first adb device
		// with the player APK installed and SKIPS the run (exit 77) when
		// there is none, so the editor_play_android test stays green on
		// unprepared machines).
		if (const char* androidSelector =
			std::getenv("ORKIGE_EDITOR_PLAY_ANDROID"))
		{
			if (std::string(androidSelector) == "auto")
			{
				for (AndroidDevice const& device : listAdbDevices())
				{
					if (androidPlayerInstalled(device.serial))
					{
						playSession.androidSerial = device.serial;
						playSession.androidLabel = device.label;
						break;
					}
				}
				if (playSession.androidSerial.empty())
				{
					SDL_Log("orkige_editor: play android 'auto' - no adb "
						"device with the player APK installed, skipping");
					return 77;
				}
				SDL_Log("orkige_editor: play android 'auto' -> '%s'",
					playSession.androidLabel.c_str());
			}
			else
			{
				playSession.androidSerial = androidSelector;
				playSession.androidLabel = androidSelector;
			}
		}

		// A real editor opens EMPTY: no GameObjects, nothing selected, an
		// untitled scene, a clean undo history. The sample resources stay
		// registered (EditorCube.mesh + VertexColour material built above,
		// the asset dir with test_mesh.glb added as a resource location), so
		// GameObject > Create Cube / Create Test Mesh and File > Open Scene
		// work instantly on the blank scene. The scripted test runs create
		// their fixture objects themselves at frame 2 (see the frame loop).
		EditorState state;

		// initial orbit pose reproduces the old fixed camera at (0, 2.5, 9)
		applyOrbitCamera(state, sceneCameraNode);

#ifdef __APPLE__
		// native macOS menu bar: mirrors the editor menu structure as real
		// NSMenus and routes every selection into the SAME functions the
		// (ImGui) menus call. Installed once; enabled-states/labels refresh
		// per frame (macMenuUpdate in the loop). Menu actions fire while SDL
		// pumps AppKit events inside SDL_PollEvent - main thread, between
		// frames, exactly like the SDL event handlers.
		{
			Orkige::MacMenuActions menuActions;
			EditorState* statePtr = &state;
			Orkige::EditorCore* corePtr = &editorCore;
			ViewSettings* viewPtr = &viewSettings;
			SDL_Window* windowPtr = window;
			menuActions.newProject = [statePtr, windowPtr]()
				{ requestFileDialog(*statePtr, windowPtr,
					Orkige::FileDialogAction::NewProject); };
			menuActions.openProject = [statePtr, windowPtr]()
				{ requestFileDialog(*statePtr, windowPtr,
					Orkige::FileDialogAction::OpenProject); };
			menuActions.openRecentProject =
				[statePtr, corePtr](std::string const& path)
				{ openProjectFromPath(*statePtr, *corePtr, path); };
			menuActions.closeProject = [statePtr, corePtr]()
				{ closeProject(*statePtr, *corePtr); };
			menuActions.newScene = [statePtr, corePtr]()
				{ newScene(*statePtr, *corePtr); };
			menuActions.openScene = [statePtr, windowPtr]()
				{ requestFileDialog(*statePtr, windowPtr,
					Orkige::FileDialogAction::OpenScene); };
			menuActions.openRecentScene =
				[statePtr, corePtr](std::string const& path)
				{ openSceneFromPath(*statePtr, *corePtr, path); };
			menuActions.saveScene = [statePtr, corePtr, windowPtr]()
			{
				if (statePtr->currentScenePath.empty())
				{
					requestFileDialog(*statePtr, windowPtr,
						Orkige::FileDialogAction::SaveSceneAs);
				}
				else
				{
					saveSceneToPath(*statePtr, *corePtr,
						statePtr->currentScenePath);
				}
			};
			menuActions.saveSceneAs = [statePtr, windowPtr]()
				{ requestFileDialog(*statePtr, windowPtr,
					Orkige::FileDialogAction::SaveSceneAs); };
			menuActions.importMesh = [statePtr, windowPtr]()
				{ requestFileDialog(*statePtr, windowPtr,
					Orkige::FileDialogAction::ImportMesh); };
			menuActions.quit = [statePtr, corePtr]()
				{ requestQuit(*statePtr, *corePtr); };
			menuActions.undo = [corePtr]() { corePtr->undo(); };
			menuActions.redo = [corePtr]() { corePtr->redo(); };
			menuActions.duplicateSelected = [corePtr]()
				{ corePtr->duplicateSelected(); };
			menuActions.deleteSelected = [corePtr]()
				{ corePtr->deleteSelected(); };
			menuActions.createCube = [corePtr]() { corePtr->createCube(); };
			menuActions.createTestMesh = [corePtr]()
				{ corePtr->createTestMesh(); };
			// Build menu: the request is deferred to the frame loop (the
			// same flag pattern as the popup opens) so the native menu and
			// the ImGui menu share one start path
			menuActions.exportProject =
				[statePtr](std::string const& platform)
				{ statePtr->requestedExport = platform; };
			menuActions.setPanelVisible = [viewPtr](int panel, bool visible)
			{
				switch (panel)
				{
				case Orkige::PANEL_HIERARCHY:
					viewPtr->showHierarchyPanel = visible; break;
				case Orkige::PANEL_INSPECTOR:
					viewPtr->showInspectorPanel = visible; break;
				case Orkige::PANEL_CONSOLE:
					viewPtr->showConsolePanel = visible; break;
				case Orkige::PANEL_STATS:
					viewPtr->showStatsPanel = visible; break;
				case Orkige::PANEL_SCENE:
					viewPtr->showScenePanel = visible; break;
				default: break;
				}
				viewPtr->save();
			};
			menuActions.resetLayout = [statePtr]()
				{ statePtr->resetDockLayout = true; };
			menuActions.viewSettings = [statePtr]()
				{ statePtr->showViewSettingsWindow = true; };
			menuActions.about = [statePtr]()
				{ statePtr->openAboutPopup = true; };
			Orkige::macMenuInstall(menuActions);
			SDL_Log("orkige_editor: native menu bar installed (%d top-level "
				"items)", Orkige::macMenuItemCount());
		}
#endif

		// automation hooks (same env-hook style as the demo):
		// ORKIGE_DEMO_FRAMES=N exit 0 after N frames,
		// ORKIGE_DEMO_SCREENSHOT=path framebuffer dump at frame 60,
		// ORKIGE_EDITOR_SELFCHECK=1 empty-start assertions at frame 1 (the
		// production editor opens with an empty untitled scene - a tested
		// contract), fixture-state assertions at frame 30, Scene-panel-picking
		// checks at frames 45/65 and the scene round-trip check
		// (save/clear/reload/compare) at frame 90 (needs >= 90 frames;
		// ORKIGE_EDITOR_SELFCHECK_SCENE overrides the round-trip file path),
		// ORKIGE_EDITOR_EXPORT_EXAMPLE=path arrange the fixture objects into
		// the shipped example layout at frame 20 and save it through the
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

		// ORKIGE_EDITOR_OPEN_SCENE=path: open the scene at frame 10 through
		// the same function File > Open uses, then assert at frame 40 that
		// textured models arrived (>= 1 texture unit) and that an object is
		// selectable + movable through an undoable transform command - the
		// editor_open_jumper_level ctest test (editor compatibility of game
		// content: scene = data, app = behavior, the editor edits the data)
		const char* openSceneEnv = std::getenv("ORKIGE_EDITOR_OPEN_SCENE");

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

		// ORKIGE_EDITOR_EXPORT_EXAMPLE=path: fixture export (frame 20 below)
		const char* exportExampleEnv =
			std::getenv("ORKIGE_EDITOR_EXPORT_EXAMPLE");

		// ORKIGE_EDITOR_PROJECT_TEST=path: scripted project-system run (the
		// editor_project_play ctest test, combined with
		// ORKIGE_EDITOR_PLAYTEST=stop): open the project at frame 10 through
		// the same function File > Open Project uses, RE-open it at frame 20
		// (exercising the resource-group teardown/re-register on a project
		// switch), then assert at frame 30 that the project rooted
		// everything - window title, resource locations, main scene, scene
		// discovery, import destination, dialog defaults. The combined
		// playtest presses Play at frame 40; with a project open the player
		// receives --project, so the plumbing is covered end to end.
		const char* projectTestEnv = std::getenv("ORKIGE_EDITOR_PROJECT_TEST");

		// ORKIGE_EDITOR_NATIVE_PLAYTEST=path: scripted compile-on-Play run
		// against a project with a native module (the editor_project_native_
		// play ctest test on projects/jumper-native). Frame 10 opens the
		// project, frame 40 presses Play; the session must enter Building,
		// "[build]" lines must reach the Console, the build must succeed and
		// launch the PROJECT'S OWN executable, whose remote hierarchy must
		// contain the runtime-spawned "Player" object (proof the module's
		// compiled gameplay code is what is running - the level scene itself
		// has no Player). Then Stop must revert cleanly to edit mode.
		// ORKIGE_EDITOR_NATIVE_PLAYTEST_BREAK=1 flips to the failure
		// variant: the project is COPIED to a temp dir (the real one is
		// never touched), a syntax error is injected into its module source,
		// and the build failure must keep the editor in edit mode with
		// "[build]" error lines in the Console and nothing launched.
		const char* nativePlaytestEnv =
			std::getenv("ORKIGE_EDITOR_NATIVE_PLAYTEST");
		const bool nativePlaytestBreak =
			std::getenv("ORKIGE_EDITOR_NATIVE_PLAYTEST_BREAK") != nullptr;

		// ORKIGE_EDITOR_SCRIPT_ERROR_PLAYTEST=path: scripted "loud script
		// failure" run (the editor_play_script_error ctest test on
		// projects/jumper-lua). The project is COPIED to a temp dir (the
		// real one is never touched) and its scripts/player.lua is
		// overwritten with garbage; frame 10 opens the copy, frame 40
		// presses Play. The player's script_error protocol message must
		// surface WITHOUT selecting anything: the RED "[remote] SCRIPT
		// ERROR on 'Player'" Console line plus the session error set that
		// feeds the toolbar marker and the hierarchy tint. Stop must revert
		// cleanly with the error state cleared.
		const char* scriptErrorPlaytestEnv =
			std::getenv("ORKIGE_EDITOR_SCRIPT_ERROR_PLAYTEST");

		// Unity behavior: a plain interactive launch reopens the last project
		// (toggleable in View Settings; automation runs are exempt via
		// automatedRun so every scripted test keeps its empty-start
		// contract). A vanished project directory is skipped silently - the
		// stale entry stays in Open Recent Project for the user to see.
		if (!automatedRun && viewSettings.reopenLastProject &&
			!viewSettings.recentProjects.empty())
		{
			const std::string lastProjectRoot =
				viewSettings.recentProjects.front();
			std::error_code reopenError;
			if (std::filesystem::is_directory(lastProjectRoot, reopenError))
			{
				SDL_Log("orkige_editor: reopening last project '%s' "
					"(View Settings > Reopen Last Project)",
					lastProjectRoot.c_str());
				openProjectFromPath(state, editorCore, lastProjectRoot);
			}
		}

		// The scripted runs above were written against the historical boot
		// scene (Cube1-3 + TestMesh1). The production editor now starts
		// EMPTY, so those runs create the same four objects themselves at
		// frame 2 - after the frame-1 empty-start assertion, before every
		// scripted stage fires. The plain interactive editor (none of these
		// hooks set) never creates them.
		const bool needsSceneFixtures = selfCheck || editTest || playtest ||
			(exportExampleEnv != nullptr);
		// edittest state that spans frames
		Orkige::EditorTransform editTestCube1Before;
		Orkige::EditorTransform editTestCube1Moved;
		Orkige::EditorTransform editTestDeletedTransform;
		std::string editTestDeletedMesh;
		std::string editTestDuplicateId;

		// project-test state that spans frames (the throwaway project the
		// frame-10 New Project step creates in the temp directory)
		std::string projectTestTempRoot;

		// native-playtest state that spans frames
		enum class NativePlaytestPhase
		{
			Idle, WaitBuildOutcome, WaitRemote, WaitRevert, Done
		};
		NativePlaytestPhase nativePhase = NativePlaytestPhase::Idle;
		std::chrono::steady_clock::time_point nativeDeadline;
		std::chrono::steady_clock::time_point nativeBuildStart;
		std::string nativePlaytestTempRoot;	//!< the break variant's temp copy
		size_t nativeLocalObjects = 0;
		bool nativeSawBuilding = false;
		bool nativeSawLaunch = false;
		// does the Console hold a "[build]"-tagged line (optionally errors
		// only)? - the honest probe that compiler output reached the UI
		auto consoleHasBuildLine = [&console](bool errorsOnly)
		{
			std::lock_guard<std::mutex> lock(console.mutex);
			for (ConsoleLine const& line : console.lines)
			{
				if (line.text.rfind("[build]", 0) == 0 &&
					(!errorsOnly || line.level == ConsoleLevel::Error))
				{
					return true;
				}
			}
			return false;
		};

		// script-error playtest state that spans frames
		enum class ScriptErrorPlaytestPhase
		{
			Idle, WaitError, WaitRevert, Done
		};
		ScriptErrorPlaytestPhase scriptErrorPhase =
			ScriptErrorPlaytestPhase::Idle;
		std::chrono::steady_clock::time_point scriptErrorDeadline;
		std::string scriptErrorTempRoot;	//!< the broken temp project copy
		// does the Console hold the RED "[remote] SCRIPT ERROR" line? - the
		// honest probe that the failure reached the UI loudly
		auto consoleHasScriptErrorLine = [&console]()
		{
			std::lock_guard<std::mutex> lock(console.mutex);
			for (ConsoleLine const& line : console.lines)
			{
				if (line.text.rfind("[remote] SCRIPT ERROR on ", 0) == 0 &&
					line.level == ConsoleLevel::Error)
				{
					return true;
				}
			}
			return false;
		};

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
			// window title: a project ROOTS it ("Orkige Editor - <project> -
			// <scene>", the scene shown project-relative), loose-scene mode
			// keeps the historical "Orkige Editor - <scene path>"; the dirty
			// marker applies to both
			std::string sceneLabel = state.currentScenePath.empty()
				? std::string("untitled") : state.currentScenePath;
			std::string windowTitle;
			if (state.project.isLoaded())
			{
				if (!state.currentScenePath.empty())
				{
					const std::string relative = state.project
						.makeProjectRelative(state.currentScenePath);
					if (!relative.empty())
					{
						sceneLabel = relative;
					}
				}
				windowTitle = "Orkige Editor - " + state.project.getName() +
					" - " + sceneLabel;
			}
			else
			{
				windowTitle = "Orkige Editor - " + sceneLabel;
			}
			windowTitle += editorCore.isSceneDirty() ? " *" : "";
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
					// window close button / Cmd+Q (SDL's app-menu Quit posts
					// this event) - both honor the unsaved-changes confirm
					requestQuit(state, editorCore);
				}
				if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
				{
					// keep the render window in sync with the SDL window (the
					// ORKIGE_EDITOR_RESIZE_TEST hook below exercises this)
					render->notifyWindowResized();
				}
				if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
				{
					// a mesh file dropped onto the window imports through the
					// exact same path as File > Import Mesh...; anything else
					// is refused with a Console line. Drop events are not
					// mouse/keyboard input, so this does not compete with the
					// ImGui event routing below.
					importMeshFromPath(state, editorCore, event.drop.data);
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
			if (quitOnEscape.quitRequested)
			{
				// ESC-quit funnels through the same unsaved-changes confirm
				quitOnEscape.quitRequested = false;
				requestQuit(state, editorCore);
			}
			if (state.quitRequested)
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

			// native file dialog outcomes (deposited by the SDL dialog
			// callback, possibly from another thread) are acted on HERE,
			// on the main thread, before the UI draws - a fallback modal
			// raised for a failed dialog opens this same frame
			dispatchFileDialogResults(state, editorCore);

			// play mode: pump the debug link, watch the player process,
			// handle crash/stop transitions - before the UI reads the state
			updatePlaySession(playSession, console);

			// project export: act on a Build-menu request, then pump the
			// running exporter's output into the Console ([export] lines)
			if (!state.requestedExport.empty())
			{
				startExport(exportJob, state.project, state.requestedExport,
					console);
				state.requestedExport.clear();
			}
			updateExportJob(exportJob, console);

			// engine log lines captured since the last frame -> Console
			drainEngineLogIntoConsole(engineLogCapture, console);

			unsigned int drawableWidth = 0;
			unsigned int drawableHeight = 0;
			render->getWindowSize(drawableWidth, drawableHeight);
			imguiInput.newFrame(
				static_cast<float>(drawableWidth),
				static_cast<float>(drawableHeight));
			ImGui::NewFrame();
			ImGuizmo::BeginFrame();

			// snapshot the panel visibility: a close-button click (the x in
			// a docked tab) must persist exactly like a View-menu toggle
			const bool panelsBefore[5] = { viewSettings.showHierarchyPanel,
				viewSettings.showInspectorPanel, viewSettings.showConsolePanel,
				viewSettings.showStatsPanel, viewSettings.showScenePanel };
#ifdef __APPLE__
			// the native NSMenu bar replaces the ImGui menu bar on mac; the
			// ImGui bar only steps in when AppKit gave us no menu (headless)
			if (Orkige::macMenuItemCount() <= 1)
			{
				drawMainMenuBar(state, editorCore, viewSettings,
					sceneTarget.camera, window);
			}
#else
			drawMainMenuBar(state, editorCore, viewSettings,
				sceneTarget.camera, window);
#endif
			// modals + the floating View Settings window are drawn
			// independently of whichever menu bar is active
			drawEditorModals(state, editorCore);
			drawViewSettingsWindow(state, viewSettings, sceneTarget.camera);
			// the View menu may have toggled the grid
			gridNode->setVisible(viewSettings.showGrid);
			const float toolbarHeight =
				drawToolbar(state, playSession, editorCore);
			drawDockspace(state, toolbarHeight, viewSettings);
			if (viewSettings.showScenePanel)
			{
				drawScenePanel(state, editorCore, !playSession.isActive(),
					sceneTarget, sceneCameraNode, viewSettings,
					editorContentScale, imguiInput);
			}
			else
			{
				// hidden Scene panel: no hover/focus, in-flight camera
				// drags end (gates reset so the next hold swallows its
				// first frame again; a captured fly releases the mouse)
				state.scenePanelHovered = false;
				state.scenePanelFocused = false;
				state.flyActive = false;
				state.orbitActive = false;
				state.panActive = false;
				state.flyLookGate.update(false);
				state.orbitDragGate.update(false);
				state.panDragGate.update(false);
				imguiInput.setRelativeMode(false);
			}
			if (viewSettings.showHierarchyPanel)
			{
				drawHierarchyPanel(state, playSession, editorCore,
					sceneTarget.camera, &viewSettings.showHierarchyPanel);
			}
			else
			{
				state.hierarchyFocused = false;
			}
			if (viewSettings.showInspectorPanel)
			{
				drawInspectorPanel(state, playSession, editorCore,
					&viewSettings.showInspectorPanel);
			}
			if (viewSettings.showStatsPanel)
			{
				drawStatsPanel(&viewSettings.showStatsPanel);
			}
			if (viewSettings.showConsolePanel)
			{
				drawConsolePanel(state, console,
					&viewSettings.showConsolePanel);
			}
			if (panelsBefore[0] != viewSettings.showHierarchyPanel ||
				panelsBefore[1] != viewSettings.showInspectorPanel ||
				panelsBefore[2] != viewSettings.showConsolePanel ||
				panelsBefore[3] != viewSettings.showStatsPanel ||
				panelsBefore[4] != viewSettings.showScenePanel)
			{
				viewSettings.save();
			}
#ifdef __APPLE__
			// refresh the native menu's enabled-states/labels/checkmarks
			{
				Orkige::MacMenuStatus menuStatus;
				menuStatus.canUndo = editorCore.canUndo();
				menuStatus.canRedo = editorCore.canRedo();
				menuStatus.undoLabel = editorCore.canUndo()
					? "Undo " + editorCore.getUndoDescription()
					: std::string("Undo");
				menuStatus.redoLabel = editorCore.canRedo()
					? "Redo " + editorCore.getRedoDescription()
					: std::string("Redo");
				menuStatus.hasSelection = editorCore.hasSelection();
				menuStatus.projectOpen = state.project.isLoaded();
				menuStatus.canExport =
					state.project.isLoaded() && !exportJob.isActive();
				menuStatus.panelVisible[Orkige::PANEL_HIERARCHY] =
					viewSettings.showHierarchyPanel;
				menuStatus.panelVisible[Orkige::PANEL_INSPECTOR] =
					viewSettings.showInspectorPanel;
				menuStatus.panelVisible[Orkige::PANEL_CONSOLE] =
					viewSettings.showConsolePanel;
				menuStatus.panelVisible[Orkige::PANEL_STATS] =
					viewSettings.showStatsPanel;
				menuStatus.panelVisible[Orkige::PANEL_SCENE] =
					viewSettings.showScenePanel;
				menuStatus.recentScenes = viewSettings.recentScenes;
				menuStatus.recentProjects = viewSettings.recentProjects;
				Orkige::macMenuUpdate(menuStatus);
			}
#endif
			// keyboard shortcuts, gated by the hover/focus state the panels
			// just recorded (Q/W/E/R, undo/redo, duplicate, delete, ...)
			handleEditorShortcuts(state, editorCore, playSession,
				sceneTarget.camera, window);

			// finalize the ImGui frame and resubmit its draw data as the
			// facade 2D layer; renderOneFrame then composites it over the
			// UI-only window (both flavors - see ImGuiFacadeRenderer.h)
			ImGui::Render();
			imguiRenderer.render(ImGui::GetDrawData());

			if (!engine.renderOneFrame())
			{
				running = false;
			}
			++frameCount;

			if (frameCount == 1 && selfCheck)
			{
				// empty-start contract (asserted BEFORE the frame-2 fixtures
				// exist): a fresh editor opens like a real editor - no
				// GameObjects, no selection, an untitled window title without
				// a dirty marker, nothing to undo/redo
				const bool emptyStartOk =
					gameObjectManager.getGameObjects().empty() &&
					!editorCore.hasSelection() &&
					editorCore.getSelectionCount() == 0 &&
					state.currentScenePath.empty() &&
					!editorCore.isSceneDirty() &&
					!editorCore.canUndo() && !editorCore.canRedo() &&
					lastWindowTitle == "Orkige Editor - untitled";
				SDL_Log("orkige_editor: selfcheck frame 1 - empty start "
					"(objects=%zu, selection=%zu, title='%s'): %s",
					gameObjectManager.getGameObjects().size(),
					editorCore.getSelectionCount(), lastWindowTitle.c_str(),
					emptyStartOk ? "OK" : "FAILED");
				if (!emptyStartOk)
				{
					SDL_Log("orkige_editor: FAILED selfcheck (empty start)");
					exitCode = 2;
					running = false;
				}
			}
			if (frameCount == 2 && needsSceneFixtures)
			{
				// fixtures for the scripted stages below: the historical boot
				// scene (Cube1-3 + TestMesh1 - the glTF mesh also proves the
				// Codec_Assimp import path), created through the same
				// instantiate function the boot scene used - directly, not via
				// undoable commands, so the run still starts with an empty
				// undo history and a clean (non-dirty) scene
				const Orkige::Vec3 fixturePositions[3] = {
					{ -2.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
					{ 2.5f, 0.0f, 0.0f },
				};
				int fixtureCubeCounter = 0;
				bool fixturesOk = true;
				for (Orkige::Vec3 const& position : fixturePositions)
				{
					const std::string id =
						"Cube" + std::to_string(++fixtureCubeCounter);
					fixturesOk = fixturesOk &&
						editorCore.instantiateModelObject(id,
							Orkige::RenderWorld::CUBE_MESH_NAME, position);
				}
				fixturesOk = fixturesOk && editorCore.instantiateModelObject(
					"TestMesh1", "test_mesh.glb",
					Orkige::Vec3(0.0f, 2.2f, 0.0f));
				if (!fixturesOk)
				{
					SDL_Log("orkige_editor: FAILED - fixture creation for the "
						"scripted run (Cube1-3 + TestMesh1)");
					exitCode = 2;
					running = false;
				}
				else
				{
					// the historical boot selection - keeps the transform
					// gizmo visible on the screenshot runs
					editorCore.selectObject("Cube2");
					SDL_Log("orkige_editor: scripted-run fixtures created "
						"(Cube1-3 + TestMesh1, Cube2 selected)");
				}
			}

			if (frameCount == 10 && selfCheck &&
				Orkige::ScriptRuntime::available())
			{
				// self-check: run the console's default buffer through the
				// exact same path the Run button uses and expect a result
				// (skipped in no-scripting builds, where the tab is hidden)
				runLuaConsoleInput(state);
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
			if (frameCount == 30 && selfCheck)
			{
				// self-check: frame-2 fixture GameObjects present + ImGui
				// actually produced geometry this frame (catches a
				// non-rendering overlay: z-order, missing RenderQueueListener,
				// NewFrame order would all zero this out)
				const bool objectsOk =
					gameObjectManager.objectExists("Cube1") &&
					gameObjectManager.objectExists("Cube2") &&
					gameObjectManager.objectExists("Cube3") &&
					gameObjectManager.objectExists("TestMesh1");
				// ... and the glTF asset really became a loaded mesh
				// (Codec_Assimp decoded it during the fixture's facade mesh
				// load - the loaded MeshInstance with sub-meshes IS the proof)
				optr<Orkige::GameObject> testMeshObject =
					gameObjectManager.getGameObject("TestMesh1").lock();
				optr<Orkige::MeshInstance> testMesh =
					(testMeshObject &&
						testMeshObject->hasComponent<Orkige::ModelComponent>())
					? testMeshObject
						->getComponentPtr<Orkige::ModelComponent>()
						->getMeshInstance()
					: optr<Orkige::MeshInstance>();
				const bool meshResourceOk =
					testMesh && testMesh->getNumSubMeshes() > 0;
				const int imguiVertices = ImGui::GetIO().MetricsRenderVertices;
				// ... and the offscreen scene RTT exists and follows the
				// Scene panel size (the panel recorded its wish by now, so
				// after the hysteresis window the sizes must match)
				const bool rttOk = sceneTarget.texture &&
					sceneTarget.width > 0 && sceneTarget.height > 0 &&
					sceneTarget.width == std::max(state.scenePanelWidth, 32) &&
					sceneTarget.height == std::max(state.scenePanelHeight, 32);
#ifdef __APPLE__
				// ... and the native menu bar really exists: the app menu
				// SDL created plus the editor's File/Edit/GameObject/View/
				// Help must make it > 4 top-level items (bug: File/Edit/...
				// missing from the macOS menu bar)
				const int nativeMenuItems = Orkige::macMenuItemCount();
				const bool nativeMenuOk = nativeMenuItems > 4;
#else
				const int nativeMenuItems = -1;
				const bool nativeMenuOk = true;
#endif
				SDL_Log("orkige_editor: selfcheck frame 30 - gameobjects=%zu "
					"(fixture cubes + test mesh %s), test_mesh.glb resource %s, "
					"imgui vertices=%d, scene RTT %dx%d (panel wants %dx%d), "
					"native menu items=%d (%s)",
					gameObjectManager.getGameObjects().size(),
					objectsOk ? "present" : "MISSING",
					meshResourceOk ? "loaded" : "NOT LOADED", imguiVertices,
					sceneTarget.width, sceneTarget.height,
					state.scenePanelWidth, state.scenePanelHeight,
					nativeMenuItems, nativeMenuOk ? "ok" : "MISSING");
				if (!objectsOk || !meshResourceOk || imguiVertices <= 0 ||
					!rttOk || !nativeMenuOk)
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
					gameObjectManager, sceneTarget.camera,
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
					gameObjectManager, sceneTarget.camera,
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
			if (frameCount == 70 && selfCheck)
			{
				// panel visibility round-trip (bug: closed panels could not
				// be reopened): hide Stats through the SAME flag the View
				// menu checkable and the panel tab's close button flip
				viewSettings.showStatsPanel = false;
			}
			if (frameCount == 75 && selfCheck)
			{
				ImGuiWindow* statsWindow = ImGui::FindWindowByName("Stats");
				const bool statsHidden = !statsWindow || !statsWindow->Active;
				SDL_Log("orkige_editor: selfcheck frame 75 - Stats panel "
					"hidden: %s", statsHidden ? "yes" : "NO");
				if (!statsHidden)
				{
					SDL_Log("orkige_editor: FAILED selfcheck (panel hide)");
					exitCode = 2;
					running = false;
				}
				// reopen everything via View > Reset Layout (rebuilds the
				// dock layout AND re-opens every panel)
				state.resetDockLayout = true;
			}
			if (frameCount == 85 && selfCheck)
			{
				ImGuiWindow* statsWindow = ImGui::FindWindowByName("Stats");
				const bool statsReopened = viewSettings.showStatsPanel &&
					statsWindow && statsWindow->Active;
				SDL_Log("orkige_editor: selfcheck frame 85 - Stats panel "
					"reopened by Reset Layout: %s",
					statsReopened ? "yes" : "NO");
				if (!statsReopened)
				{
					SDL_Log("orkige_editor: FAILED selfcheck (panel reopen)");
					exitCode = 2;
					running = false;
				}
			}
			// --- scripted scene-open test (ORKIGE_EDITOR_OPEN_SCENE=path) ---
			if (openSceneEnv && frameCount == 10)
			{
				if (!openSceneFromPath(state, editorCore, openSceneEnv))
				{
					SDL_Log("orkige_editor: FAILED open-scene test (open)");
					exitCode = 6;
					running = false;
				}
			}
			if (openSceneEnv && frameCount == 40 && exitCode == 0)
			{
				// (a) the scene's models must have arrived TEXTURED (the
				// jumper meshes embed their PNGs in the .glb binary chunk)
				int modelCount = 0;
				int texturedCount = 0;
				for (auto const& [id, gameObject] :
					gameObjectManager.getGameObjects())
				{
					if (!gameObject->hasComponent<Orkige::ModelComponent>())
					{
						continue;
					}
					optr<Orkige::MeshInstance> mesh = gameObject
						->getComponentPtr<Orkige::ModelComponent>()
						->getMeshInstance();
					if (!mesh || mesh->getNumSubMeshes() == 0)
					{
						continue;
					}
					++modelCount;
					if (mesh->subMeshHasTexture(0))
					{
						++texturedCount;
					}
				}
				SDL_Log("orkige_editor: open-scene test - %d models, %d "
					"textured", modelCount, texturedCount);
				// (b) an object must be selectable and movable through the
				// undoable command path (what the gizmo/Inspector drive)
				bool openSceneOk = modelCount > 0 && texturedCount > 0;
				const std::string probeId = "Platform1";
				if (openSceneOk &&
					gameObjectManager.objectExists(probeId))
				{
					editorCore.selectObject(probeId);
					openSceneOk = editorCore.isSelected(probeId);
					Orkige::EditorTransform before;
					openSceneOk = openSceneOk &&
						editorCore.getObjectTransform(probeId, before);
					Orkige::EditorTransform after = before;
					after.position += Orkige::Vec3(0.0f, 0.5f, 0.0f);
					openSceneOk = openSceneOk &&
						editorCore.applyTransformChange(probeId, before, after);
					Orkige::EditorTransform now;
					openSceneOk = openSceneOk &&
						editorCore.getObjectTransform(probeId, now) &&
						now.position.positionEquals(after.position, 1e-3f);
					openSceneOk = openSceneOk && editorCore.undo();
					openSceneOk = openSceneOk &&
						editorCore.getObjectTransform(probeId, now) &&
						now.position.positionEquals(before.position, 1e-3f);
					if (openSceneOk)
					{
						SDL_Log("orkige_editor: open-scene test - '%s' "
							"selected, moved and undone via the command stack",
							probeId.c_str());
					}
				}
				else if (openSceneOk)
				{
					SDL_Log("orkige_editor: open-scene test - no '%s' in the "
						"scene, select/move check skipped", probeId.c_str());
				}
				if (!openSceneOk)
				{
					SDL_Log("orkige_editor: FAILED open-scene test "
						"(textured/selectable/movable)");
					exitCode = 6;
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
				auto positionsEqual = [](Orkige::Vec3 const& a,
					Orkige::Vec3 const& b)
				{
					return a.positionEquals(b, 1e-3f);
				};
				auto orientationsEqual = [](Orkige::Quat const& a,
					Orkige::Quat const& b)
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
						Orkige::Vec3(1.5f, 0.75f, -2.0f);
					editTestCube1Moved.orientation = Orkige::Quat(
						Orkige::Degree(45.0f), Orkige::Vec3::UNIT_Y);
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
						Orkige::RenderWorld::CUBE_MESH_NAME,
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
							->getMeshInstance() != nullptr,
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
						step.position += Orkige::Vec3(0.0f, 0.5f, 0.0f);
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
						"persistence OK");
				}
				if (frameCount == 95)
				{
					// Add Component through the exact function the Inspector
					// popup calls; dependencies + undo/redo behaviour
					editorCore.selectObject("Cube1");
					require(editorCore.getAddableComponentTypes().size() >= 6,
						"component registry");
					optr<Orkige::GameObject> cube1 =
						gameObjectManager.getGameObject("Cube1").lock();
					require(cube1 && !cube1
						->hasComponent<Orkige::RigidBodyComponent>(),
						"no rigidbody yet");
					require(editorCore.addComponentToObject("Cube1",
						"RigidBodyComponent"), "add component");
					require(cube1
						->hasComponent<Orkige::RigidBodyComponent>(),
						"component added");
					require(editorCore.getUndoDescription() ==
						"Add RigidBodyComponent to Cube1",
						"add description");
					require(!editorCore.addComponentToObject("Cube1",
						"RigidBodyComponent"), "double add refused");
					require(!editorCore.addComponentToObject("Cube1",
						"NoSuchComponent"), "unknown type refused");
					// undo-aware BodyDesc edit (the Inspector's drag path)
					Orkige::PhysicsWorld::BodyDesc descBefore;
					require(editorCore.getRigidBodyDesc("Cube1", descBefore),
						"body desc readable");
					Orkige::PhysicsWorld::BodyDesc descAfter = descBefore;
					descAfter.mass = 7.5f;
					descAfter.bodyType = Orkige::PhysicsWorld::BT_STATIC;
					require(editorCore.applyRigidBodyChange("Cube1",
						descBefore, descAfter), "rigidbody edit");
					Orkige::PhysicsWorld::BodyDesc descNow;
					require(editorCore.getRigidBodyDesc("Cube1", descNow) &&
						descNow.mass == 7.5f &&
						descNow.bodyType == Orkige::PhysicsWorld::BT_STATIC,
						"desc applied");
					require(editorCore.undo(), "undo rigidbody edit");
					require(editorCore.getRigidBodyDesc("Cube1", descNow) &&
						descNow.mass == descBefore.mass &&
						descNow.bodyType == descBefore.bodyType,
						"desc edit undone");
					require(editorCore.undo(), "undo add component");
					require(!cube1
						->hasComponent<Orkige::RigidBodyComponent>(),
						"add undone");
					require(editorCore.redo(), "redo add component");
					require(cube1
						->hasComponent<Orkige::RigidBodyComponent>(),
						"redo re-added");
					SDL_Log("orkige_editor: edittest frame 95 - add component "
						"OK");
				}
				if (frameCount == 100)
				{
					// remove-component dependency rule + state restore
					Orkige::String blockedBy;
					require(!editorCore.canRemoveComponent("Cube1",
						"TransformComponent", &blockedBy),
						"transform removal blocked");
					require(!blockedBy.empty(), "blocker reported");
					require(!editorCore.removeComponentFromObject("Cube1",
						"TransformComponent"), "transform remove refused");
					optr<Orkige::GameObject> cube1 =
						gameObjectManager.getGameObject("Cube1").lock();
					require(cube1 && cube1
						->hasComponent<Orkige::TransformComponent>(),
						"transform still attached");
					// give the RigidBody a recognizable state, remove it,
					// undo must restore that exact state
					Orkige::PhysicsWorld::BodyDesc descBefore;
					require(editorCore.getRigidBodyDesc("Cube1", descBefore),
						"rigidbody attached");
					Orkige::PhysicsWorld::BodyDesc descAfter = descBefore;
					descAfter.mass = 3.25f;
					require(editorCore.applyRigidBodyChange("Cube1",
						descBefore, descAfter), "prepare rigidbody state");
					require(editorCore.removeComponentFromObject("Cube1",
						"RigidBodyComponent"), "remove component");
					require(!cube1
						->hasComponent<Orkige::RigidBodyComponent>(),
						"component removed");
					require(editorCore.getUndoDescription() ==
						"Remove RigidBodyComponent from Cube1",
						"remove description");
					require(editorCore.undo(), "undo remove");
					Orkige::PhysicsWorld::BodyDesc descNow;
					require(cube1
						->hasComponent<Orkige::RigidBodyComponent>() &&
						editorCore.getRigidBodyDesc("Cube1", descNow) &&
						descNow.mass == 3.25f,
						"remove-undo restored the component state");
					SDL_Log("orkige_editor: edittest frame 100 - remove "
						"component + dependency rule OK");
				}
				if (frameCount == 105)
				{
					// undoable ModelComponent mesh swap (Inspector mesh field)
					optr<Orkige::GameObject> cube2 =
						gameObjectManager.getGameObject("Cube2").lock();
					require(cube2 && cube2
						->getComponentPtr<Orkige::ModelComponent>()
						->getCurrentModelFileName() ==
						Orkige::RenderWorld::CUBE_MESH_NAME, "Cube2 mesh");
					require(!editorCore.changeObjectMesh("Cube2",
						Orkige::RenderWorld::CUBE_MESH_NAME),
						"no-op mesh change refused");
					require(editorCore.changeObjectMesh("Cube2",
						"test_mesh.glb"), "mesh change");
					Orkige::ModelComponent* model = cube2
						->getComponentPtr<Orkige::ModelComponent>();
					require(model->getCurrentModelFileName() ==
						"test_mesh.glb" && model->getMeshInstance() != nullptr,
						"mesh swapped + entity loaded");
					require(editorCore.undo(), "undo mesh change");
					require(model->getCurrentModelFileName() ==
						Orkige::RenderWorld::CUBE_MESH_NAME &&
						model->getMeshInstance() != nullptr,
						"mesh change undone");
					SDL_Log("orkige_editor: edittest frame 105 - mesh change "
						"OK");
				}
				if (frameCount == 110)
				{
					// multi-select delete: ONE undo step restores the batch
					editorCore.selectObject("Cube1");
					editorCore.toggleSelection("Cube2");
					require(editorCore.getSelectionCount() == 2 &&
						editorCore.isSelected("Cube1") &&
						editorCore.isSelected("Cube2"), "multi selection");
					require(editorCore.getSelectedObjectId() == "Cube2",
						"newest selection is primary");
					const std::size_t depthBefore =
						editorCore.getUndoStackSize();
					require(editorCore.deleteSelected(), "multi delete");
					require(!gameObjectManager.objectExists("Cube1") &&
						!gameObjectManager.objectExists("Cube2"),
						"both deleted");
					require(editorCore.getUndoStackSize() == depthBefore + 1,
						"batch = one undo step");
					require(editorCore.undo(), "undo multi delete");
					require(gameObjectManager.objectExists("Cube1") &&
						gameObjectManager.objectExists("Cube2"),
						"both restored");
					optr<Orkige::GameObject> cube1 =
						gameObjectManager.getGameObject("Cube1").lock();
					require(cube1 && cube1
						->hasComponent<Orkige::RigidBodyComponent>(),
						"components survived the batch restore");
					require(editorCore.isSelected("Cube1") &&
						editorCore.isSelected("Cube2"),
						"selection restored");
					SDL_Log("orkige_editor: edittest frame 110 - multi-select "
						"delete OK");
				}
				if (frameCount == 115)
				{
					// multi-select duplicate: all copies, one undo step
					editorCore.selectObject("Cube1");
					editorCore.toggleSelection("Cube2");
					const std::string copy1 =
						editorCore.makeDuplicateId("Cube1");
					const std::string copy2 =
						editorCore.makeDuplicateId("Cube2");
					const std::size_t depthBefore =
						editorCore.getUndoStackSize();
					require(editorCore.duplicateSelected(),
						"multi duplicate");
					require(gameObjectManager.objectExists(copy1) &&
						gameObjectManager.objectExists(copy2),
						"both copies exist");
					require(editorCore.getSelectionCount() == 2 &&
						editorCore.isSelected(copy1) &&
						editorCore.isSelected(copy2), "copies selected");
					require(editorCore.getUndoStackSize() == depthBefore + 1,
						"duplicate batch = one undo step");
					require(editorCore.undo(), "undo multi duplicate");
					require(!gameObjectManager.objectExists(copy1) &&
						!gameObjectManager.objectExists(copy2),
						"copies removed");
					require(editorCore.isSelected("Cube1") &&
						editorCore.isSelected("Cube2"),
						"sources selected again");
					SDL_Log("orkige_editor: edittest frame 115 - multi-select "
						"duplicate OK");
				}
				if (frameCount == 120)
				{
					// mesh import through the EXACT function the Import modal
					// and SDL_EVENT_DROP_FILE call: the file is copied into
					// the scene's media dir (the scene was saved at frame 90,
					// so "<sceneDir>/media"), the object appears with a
					// loaded ModelComponent, undo removes it again
					const std::string importSource =
						ORKIGE_EDITOR_ASSET_DIR "/test_mesh.glb";
					require(importMeshFromPath(state, editorCore,
						importSource), "import mesh");
					const std::string importedId = "test_mesh";
					require(gameObjectManager.objectExists(importedId),
						"imported object exists");
					optr<Orkige::GameObject> imported =
						gameObjectManager.getGameObject(importedId).lock();
					require(imported && imported
						->hasComponent<Orkige::ModelComponent>() &&
						imported->getComponentPtr<Orkige::ModelComponent>()
							->getMeshInstance() != nullptr,
						"imported model entity loaded");
					require(editorCore.getSelectedObjectId() == importedId,
						"imported object selected");
					// the copy really landed in the scene's media folder
					require(std::filesystem::exists(std::filesystem::path(
						Orkige::meshImportDestinationDir(
							state.currentScenePath, "")) / "test_mesh.glb"),
						"file copied into the media dir");
					require(editorCore.undo(), "undo import");
					require(!gameObjectManager.objectExists(importedId),
						"import undone");
					// junk is refused cleanly (wrong extension, no crash,
					// no half-created object)
					const std::size_t objectsBefore =
						gameObjectManager.getGameObjects().size();
					require(!importMeshFromPath(state, editorCore,
						ORKIGE_EDITOR_SCENE_DIR "/example.oscene"),
						"non-mesh refused");
					require(gameObjectManager.getGameObjects().size() ==
						objectsBefore, "refused import created nothing");
					SDL_Log("orkige_editor: edittest frame 120 - mesh "
						"import OK");
				}
				if (frameCount == 125)
				{
					// fly-mode navigation: driven through flyCameraStep - the
					// same function the Scene panel's right-mouse fly path
					// runs every frame (injecting a held right button +
					// WASD through the real event pipeline is not doable
					// deterministically in this harness, so the step function
					// is called directly and the camera node checked)
					const Orkige::Vec3 positionBefore =
						Orkige::editorCameraPosition(state.camera);
					const float distanceBefore = state.camera.distance;
					float flySpeed = 6.0f;
					Orkige::FlyInput forward;
					forward.moveForward = true;
					Orkige::flyCameraStep(state.camera, forward, 0.5f, 0.4f,
						flySpeed);
					const Orkige::Vec3 positionAfter =
						Orkige::editorCameraPosition(state.camera);
					require((positionAfter - positionBefore).length() > 2.9f,
						"fly moved the camera");
					require(std::abs(state.camera.distance - distanceBefore) <
						1e-4f, "fly kept the orbit distance");
					applyOrbitCamera(state, sceneCameraNode);
					require(sceneCameraNode->getPosition().positionEquals(
						positionAfter, 1e-3f), "camera node follows fly");
					Orkige::FlyInput look;
					look.lookDeltaX = 120.0f;
					Orkige::flyCameraStep(state.camera, look, 0.016f, 0.4f,
						flySpeed);
					require(Orkige::editorCameraPosition(state.camera)
						.positionEquals(positionAfter, 1e-3f),
						"mouselook pivots around the camera position");
					// jiggle regression (owner report: "weird rotation when
					// right click and juggle the mouse"): rapid alternating
					// look deltas through the REAL per-frame path (fly step +
					// applyOrbitCamera) must accumulate ZERO roll - the old
					// lookAt-based orientation update rolled the horizon
					{
						Orkige::EditorCameraState jiggleStart = state.camera;
						for (int i = 0; i < 400; ++i)
						{
							Orkige::FlyInput jiggle;
							// alternating diagonal wiggles, net sum zero
							jiggle.lookDeltaX = (i % 2 == 0) ? 37.0f : -37.0f;
							jiggle.lookDeltaY = (i % 4 < 2) ? 23.0f : -23.0f;
							Orkige::flyCameraStep(state.camera, jiggle,
								0.016f, 0.4f, flySpeed);
							applyOrbitCamera(state, sceneCameraNode);
						}
						const Orkige::Radian roll =
							sceneCameraNode->getOrientation().getRoll();
						require(std::abs(roll.valueDegrees()) < 0.01f,
							"jiggle accumulates no camera roll");
						require(std::abs(state.camera.yawDeg -
							jiggleStart.yawDeg) < 1e-3f &&
							std::abs(state.camera.pitchDeg -
								jiggleStart.pitchDeg) < 1e-3f,
							"net-zero jiggle returns to the same yaw/pitch");
						state.camera = jiggleStart;
						applyOrbitCamera(state, sceneCameraNode);
					}
					// relative mouse capture: drive the EXACT entry function
					// the Scene panel's fly begin/end calls and require SDL's
					// per-window relative mode flag to engage/disengage on
					// the real window (this run is windowed, so the query is
					// meaningful; actual capture applies with focus)
					require(!SDL_GetWindowRelativeMouseMode(window),
						"relative mode off before fly capture");
					imguiInput.setRelativeMode(true);
					require(SDL_GetWindowRelativeMouseMode(window),
						"fly capture engages relative mouse mode");
					require(imguiInput.isRelativeMode(),
						"input shim tracks the capture");
					imguiInput.setRelativeMode(false);
					require(!SDL_GetWindowRelativeMouseMode(window),
						"fly release disengages relative mouse mode");
					require(!imguiInput.isRelativeMode(),
						"input shim tracks the release");
					SDL_Log("orkige_editor: edittest frame 125 - fly camera "
						"+ relative mouse capture OK");
				}
				if (frameCount == 130)
				{
					// Unity-style double-click focus: drive the EXACT function
					// the Hierarchy double-click (and, after its pick, the
					// Scene viewport double-click) runs - the selection must
					// change AND the orbit camera must retarget/refit to
					// frame the object; the inline rename stays reachable
					// through its own function (the F2/context-menu path)
					editorCore.selectObject("Cube2");
					state.camera.target = Orkige::Vec3(50.0f, 0.0f, 0.0f);
					state.camera.distance = 150.0f;
					applyOrbitCamera(state, sceneCameraNode);
					Orkige::EditorTransform cube1Now;
					require(editorCore.getObjectTransform("Cube1", cube1Now),
						"Cube1 transform readable");
					focusObjectFromDoubleClick(state, editorCore,
						sceneTarget.camera, "Cube1");
					require(editorCore.getSelectedObjectId() == "Cube1",
						"double-click changed the selection");
					require((state.camera.target - cube1Now.position)
						.length() < 2.0f,
						"double-click retargeted the camera onto the object");
					require(state.camera.distance < 20.0f,
						"double-click refit the orbit distance");
					// rename: no longer on double-click, but its function
					// still arms the inline edit field (F2 / context menu)
					editorCore.selectObject("Cube2");
					startRenameSelected(state, editorCore);
					require(state.renamingObjectId == "Cube2" &&
						state.renameFocusPending,
						"rename still reachable via F2/context menu");
					state.renamingObjectId.clear();
					state.renameFocusPending = false;
					SDL_Log("orkige_editor: edittest frame 130 - double-click "
						"focus OK -> edittest PASSED");
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
				// example scene export: arrange the frame-2 fixture objects
				// (plus two extra cubes) into an interesting layout and save
				// it through the serializer - the committed
				// samples/scenes/example.oscene is produced by exactly this
				// path
				if (const char* examplePath = exportExampleEnv)
				{
					auto setPose = [&](std::string const& id,
						Orkige::Vec3 const& position, float yawDegrees,
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
						transform->setOrientation(Orkige::Quat(
							Orkige::Radian(Orkige::Degree(yawDegrees)),
							Orkige::Vec3::UNIT_Y));
						transform->setScale(Orkige::Vec3(uniformScale));
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
					render->saveWindowContents(shotPath);
				}
			}

			// --- scripted project test (ORKIGE_EDITOR_PROJECT_TEST=path) ---
			if (projectTestEnv && frameCount == 10)
			{
				// File > New Project... on a temp folder through the same
				// function the folder dialog result runs: skeleton + manifest
				// + the initial empty main scene must materialize and the
				// fresh project must be open afterwards
				projectTestTempRoot = (std::filesystem::temp_directory_path() /
					("orkige_project_test_" + std::to_string(
						std::chrono::steady_clock::now()
							.time_since_epoch().count()))).string();
				bool newOk = newProjectAtPath(state, editorCore,
					projectTestTempRoot);
				std::error_code ignored;
				newOk = newOk && state.project.isLoaded();
				newOk = newOk && std::filesystem::exists(
					std::filesystem::path(state.project.getRootDirectory()) /
					Orkige::Project::MANIFEST_FILE_NAME, ignored);
				newOk = newOk && std::filesystem::exists(
					state.project.getMainScenePath(), ignored);
				newOk = newOk && state.currentScenePath ==
					state.project.getMainScenePath();
				newOk = newOk && gameObjectManager.getGameObjects().empty();
				SDL_Log("orkige_editor: project test frame 10 - New Project "
					"at '%s': %s", projectTestTempRoot.c_str(),
					newOk ? "OK" : "FAILED");
				if (!newOk)
				{
					SDL_Log("orkige_editor: FAILED project test (new project)");
					exitCode = 5;
					running = false;
				}
			}
			if (projectTestEnv && frameCount == 20 && exitCode == 0)
			{
				// File > Open Project... WHILE another project is open = a
				// project switch: the previous resource group must tear down
				// and the new project's roots come up cleanly
				if (!openProjectFromPath(state, editorCore, projectTestEnv))
				{
					SDL_Log("orkige_editor: FAILED project test (open)");
					exitCode = 5;
					running = false;
				}
				if (!projectTestTempRoot.empty())
				{
					std::error_code ignored;
					std::filesystem::remove_all(projectTestTempRoot, ignored);
				}
			}
			if (projectTestEnv && frameCount == 30 && exitCode == 0)
			{
				bool projectOk = true;
				std::string projectFailure;
				auto require = [&](bool condition, char const* what)
				{
					if (!condition && projectOk)
					{
						projectOk = false;
						projectFailure = what;
					}
				};
				require(state.project.isLoaded(), "project loaded");
				require(state.project.getName() == "Example", "project name");
				// the project roots the window title (scene shown relative)
				require(lastWindowTitle ==
					"Orkige Editor - Example - scenes/main.oscene",
					"window title");
				// the main scene opened as the current scene
				require(state.currentScenePath ==
					state.project.getMainScenePath(),
					"current scene = main scene");
				require(gameObjectManager.objectExists("Cube1") &&
					gameObjectManager.objectExists("TestMesh1"),
					"example objects present");
				// scene discovery sees the main scene
				const Orkige::StringVector scenes = state.project.listScenes();
				require(std::find(scenes.begin(), scenes.end(),
					std::string("scenes/main.oscene")) != scenes.end(),
					"scene list discovery");
				// resource roots: the dedicated group exists and serves the
				// project's assets/ and scenes/ (probed through the same
				// facade the registration used)
				require(render->resourceGroupExists(
					Orkige::Project::RESOURCE_GROUP_NAME),
					"project resource group");
				require(render->resourceExists("test_mesh.glb",
					Orkige::Project::RESOURCE_GROUP_NAME),
					"project asset indexed");
				require(render->resourceExists("main.oscene",
					Orkige::Project::RESOURCE_GROUP_NAME),
					"project scene indexed");
				// imports are rooted into the project's assets/
				require(meshImportDestination(state) ==
					state.project.getAssetsDirectory(),
					"import destination");
				// scene dialogs default into the project's scenes/
				require(defaultSceneDirectory(state) ==
					state.project.getScenesDirectory(),
					"scene dialog default");
				SDL_Log("orkige_editor: project test frame 30 - %s (title "
					"'%s', %zu scenes)", projectOk
						? "project roots verified" : projectFailure.c_str(),
					lastWindowTitle.c_str(), scenes.size());
				if (!projectOk)
				{
					SDL_Log("orkige_editor: FAILED project test (%s)",
						projectFailure.c_str());
					exitCode = 5;
					running = false;
				}
			}

			// --- scripted native compile-on-Play test -----------------------
			// (ORKIGE_EDITOR_NATIVE_PLAYTEST, see the env block above)
			if (nativePlaytestEnv)
			{
				const std::chrono::steady_clock::time_point nativeNow =
					std::chrono::steady_clock::now();
				bool nativeFailed = false;
				std::string nativeFailure;
				if (frameCount == 10 && nativePhase == NativePlaytestPhase::Idle)
				{
					std::string projectToOpen = nativePlaytestEnv;
					if (nativePlaytestBreak)
					{
						// the deliberate-breakage variant works on a temp COPY
						// - the real project is never touched. Build trees
						// (native/build*, the export outputs in builds/) are
						// SKIPPED during the copy: they are hundreds of MB the
						// broken copy must not inherit anyway (it has to
						// configure + fail-compile from scratch in its own
						// dir) and copying-then-deleting them cost seconds of
						// every desktop suite run.
						nativePlaytestTempRoot =
							(std::filesystem::temp_directory_path() /
							("orkige_native_break_" + std::to_string(
								std::chrono::steady_clock::now()
									.time_since_epoch().count()))).string();
						std::error_code copyError;
						std::function<void(std::filesystem::path const&,
							std::filesystem::path const&)> copyFiltered =
							[&copyFiltered, &copyError](
								std::filesystem::path const& from,
								std::filesystem::path const& to)
						{
							std::filesystem::create_directories(to, copyError);
							for (auto const& entry :
								std::filesystem::directory_iterator(from,
									copyError))
							{
								const std::string name =
									entry.path().filename().string();
								if (entry.is_directory() &&
									(name == "build" || name == "builds" ||
										name.rfind("build-", 0) == 0))
								{
									continue;
								}
								if (entry.is_directory())
								{
									copyFiltered(entry.path(), to / name);
								}
								else
								{
									std::filesystem::copy_file(entry.path(),
										to / name, copyError);
								}
								if (copyError)
								{
									return;
								}
							}
						};
						copyFiltered(nativePlaytestEnv, nativePlaytestTempRoot);
						// the error goes to the TOP of the module source: the
						// compiler fails immediately instead of first parsing
						// the fat OGRE-including TU - the tested contract
						// (build failure -> edit mode + Console errors +
						// nothing launched) is identical, just faster
						const std::filesystem::path breakSource =
							std::filesystem::path(nativePlaytestTempRoot) /
							"native" / "main.cpp";
						std::string originalSource;
						{
							std::ifstream sourceIn(breakSource,
								std::ios::binary);
							originalSource.assign(
								std::istreambuf_iterator<char>(sourceIn),
								std::istreambuf_iterator<char>());
						}
						std::ofstream breakFile(breakSource,
							std::ios::binary | std::ios::trunc);
						breakFile << "this is not valid C++ - injected by "
							"the native playtest break variant\n"
							<< originalSource;
						if (copyError || originalSource.empty() || !breakFile)
						{
							nativeFailed = true;
							nativeFailure = "could not prepare the broken "
								"temp copy at " + nativePlaytestTempRoot;
						}
						projectToOpen = nativePlaytestTempRoot;
					}
					if (!nativeFailed && !openProjectFromPath(state,
						editorCore, projectToOpen))
					{
						nativeFailed = true;
						nativeFailure = "could not open the native project '" +
							projectToOpen + "'";
					}
				}
				if (frameCount == 40 && !nativeFailed &&
					nativePhase == NativePlaytestPhase::Idle)
				{
					nativeLocalObjects =
						gameObjectManager.getGameObjects().size();
					nativeBuildStart = nativeNow;
					// the exact function the Play button calls
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						nativeFailed = true;
						nativeFailure = "startPlay failed";
					}
					else if (playSession.mode != PlaySession::Mode::Building)
					{
						nativeFailed = true;
						nativeFailure = "Play on a native project did not "
							"enter the Building state";
					}
					else
					{
						nativeSawBuilding = true;
						nativePhase = NativePlaytestPhase::WaitBuildOutcome;
						// generous: the first Play configures + compiles the
						// whole module (a fat OGRE-including TU + full link)
						nativeDeadline = nativeNow + std::chrono::seconds(240);
						SDL_Log("orkige_editor: native playtest - Play "
							"pressed, building '%s'",
							playSession.nativeTarget.c_str());
					}
				}
				else if (nativePhase == NativePlaytestPhase::WaitBuildOutcome)
				{
					const double buildSeconds = std::chrono::duration<double>(
						nativeNow - nativeBuildStart).count();
					if (playSession.mode == PlaySession::Mode::Launching ||
						playSession.mode == PlaySession::Mode::Playing)
					{
						// the build succeeded and the module was launched
						SDL_Log("orkige_editor: native playtest - build "
							"succeeded after %.1fs", buildSeconds);
						if (nativePlaytestBreak)
						{
							nativeFailed = true;
							nativeFailure = "the deliberately broken module "
								"built successfully";
						}
						else if (!consoleHasBuildLine(false))
						{
							nativeFailed = true;
							nativeFailure = "no [build] lines reached the "
								"Console during the build";
						}
						else
						{
							nativeSawLaunch = true;
							nativePhase = NativePlaytestPhase::WaitRemote;
							nativeDeadline =
								nativeNow + std::chrono::seconds(60);
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						// the build failed and the session reverted
						SDL_Log("orkige_editor: native playtest - build "
							"failed after %.1fs (mode back to edit)",
							buildSeconds);
						if (!nativePlaytestBreak)
						{
							nativeFailed = true;
							nativeFailure = "the native module build failed";
						}
						else if (!consoleHasBuildLine(true))
						{
							nativeFailed = true;
							nativeFailure = "the failed build left no "
								"[build] error lines in the Console";
						}
						else if (playSession.process != nullptr ||
							playSession.buildProcess != nullptr ||
							playSession.client.isConnected())
						{
							nativeFailed = true;
							nativeFailure = "session not fully torn down "
								"after the failed build";
						}
						else if (gameObjectManager.getGameObjects().size() !=
							nativeLocalObjects)
						{
							nativeFailed = true;
							nativeFailure = "the failed build modified the "
								"editor scene";
						}
						else
						{
							SDL_Log("orkige_editor: native playtest PASSED "
								"(break path): build failure kept edit mode, "
								"error lines in the Console, nothing "
								"launched (launch seen: %d)",
								static_cast<int>(nativeSawLaunch));
							std::error_code ignored;
							std::filesystem::remove_all(
								nativePlaytestTempRoot, ignored);
							nativePhase = NativePlaytestPhase::Done;
							running = false;
						}
					}
				}
				else if (nativePhase == NativePlaytestPhase::WaitRemote)
				{
					if (playSession.mode == PlaySession::Mode::Playing &&
						playSession.helloReceived &&
						playSession.hierarchyReceived)
					{
						// the remote hierarchy must contain the module-spawned
						// "Player" - the level scene has none, so this proves
						// the PROJECT'S compiled gameplay code is running
						if (std::find(playSession.remoteHierarchy.begin(),
							playSession.remoteHierarchy.end(),
							std::string("Player")) ==
							playSession.remoteHierarchy.end())
						{
							nativeFailed = true;
							nativeFailure = "remote hierarchy has no 'Player'"
								" - is the module's gameplay code running?";
						}
						else
						{
							SDL_Log("orkige_editor: native playtest - remote "
								"hierarchy arrived (%zu objects incl. the "
								"module-spawned Player), stopping",
								playSession.remoteHierarchy.size());
							requestStopPlay(playSession);
							nativePhase = NativePlaytestPhase::WaitRevert;
							nativeDeadline =
								nativeNow + std::chrono::seconds(30);
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						nativeFailed = true;
						nativeFailure = "play session ended before the "
							"remote hierarchy arrived";
					}
				}
				else if (nativePhase == NativePlaytestPhase::WaitRevert)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						if (gameObjectManager.getGameObjects().size() !=
							nativeLocalObjects)
						{
							nativeFailed = true;
							nativeFailure = "editor scene was modified by "
								"the native play session";
						}
						else if (playSession.process != nullptr ||
							playSession.client.isConnected())
						{
							nativeFailed = true;
							nativeFailure = "session not fully torn down "
								"after revert";
						}
						else
						{
							SDL_Log("orkige_editor: native playtest PASSED: "
								"build -> launch -> remote hierarchy -> "
								"clean revert (%zu objects intact, building "
								"state seen: %d)", nativeLocalObjects,
								static_cast<int>(nativeSawBuilding));
							nativePhase = NativePlaytestPhase::Done;
							running = false;
						}
					}
				}
				if (!nativeFailed &&
					nativePhase != NativePlaytestPhase::Idle &&
					nativePhase != NativePlaytestPhase::Done &&
					nativeNow >= nativeDeadline)
				{
					nativeFailed = true;
					nativeFailure = "deadline exceeded in phase " +
						std::to_string(static_cast<int>(nativePhase));
				}
				if (nativeFailed)
				{
					SDL_Log("orkige_editor: native playtest FAILED - %s",
						nativeFailure.c_str());
					if (!nativePlaytestTempRoot.empty())
					{
						std::error_code ignored;
						std::filesystem::remove_all(nativePlaytestTempRoot,
							ignored);
					}
					exitCode = 6;
					running = false;
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
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						playtestFailed = true;
						playtestFailure = "startPlay failed";
					}
					else
					{
						playtestPhase = PlaytestPhase::WaitRemote;
						// a simulator target may have to cold-boot the
						// device (+ install the app) before the player can
						// even start - give it real headroom
						playtestDeadline = playtestNow + std::chrono::seconds(
							playSession.simulatorUdid.empty() ? 60 : 240);
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
					// besides the object_state stream, at least one [remote]
					// log line (the player's forwarded Ogre log) must have
					// arrived over the debug protocol by now
					if (playSession.stateObjectId ==
							playSession.remoteSelectedId &&
						!playSession.remoteSelectedId.empty() &&
						playSession.stateProperties.count(
							"TransformComponent.position") != 0 &&
						playSession.remoteLogSeen)
					{
						SDL_Log("orkige_editor: playtest - object_state for "
							"'%s' streams (position %s), remote log lines "
							"received",
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
							render->saveWindowContents(shotPath);
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
#ifdef __APPLE__
							// leave the machine as found: a simulator this
							// scripted run booted is shut down again
							// (interactive runs keep it running on purpose)
							if (playSession.simulatorBootedByEditor &&
								!playSession.simulatorUdid.empty())
							{
								const char* shutdownArgs[] = {
									"/usr/bin/xcrun", "simctl", "shutdown",
									playSession.simulatorUdid.c_str(),
									nullptr };
								std::string shutdownOutput;
								int shutdownExit = 0;
								runProcessCaptured(shutdownArgs,
									shutdownOutput, shutdownExit);
								SDL_Log("orkige_editor: playtest - shut the "
									"simulator down again (booted by this "
									"run, simctl shutdown exit %d)",
									shutdownExit);
							}
#endif
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

			// --- scripted script-error playtest -----------------------------
			// (ORKIGE_EDITOR_SCRIPT_ERROR_PLAYTEST, see the env block above)
			if (scriptErrorPlaytestEnv)
			{
				const std::chrono::steady_clock::time_point scriptErrorNow =
					std::chrono::steady_clock::now();
				bool scriptErrorFailed = false;
				std::string scriptErrorFailure;
				if (frameCount == 10 &&
					scriptErrorPhase == ScriptErrorPlaytestPhase::Idle)
				{
					// work on a temp COPY - the real project is never touched
					scriptErrorTempRoot =
						(std::filesystem::temp_directory_path() /
						("orkige_script_error_" + std::to_string(
							std::chrono::steady_clock::now()
								.time_since_epoch().count()))).string();
					std::error_code copyError;
					std::filesystem::copy(scriptErrorPlaytestEnv,
						scriptErrorTempRoot,
						std::filesystem::copy_options::recursive, copyError);
					// the stale-player scenario: a script whose content the
					// runtime cannot load - the load failure must be LOUD in
					// the editor without selecting the object
					std::ofstream breakFile(
						std::filesystem::path(scriptErrorTempRoot) /
						"scripts" / "player.lua",
						std::ios::binary | std::ios::trunc);
					breakFile << "this is not valid lua ((\n";
					const bool breakOk = breakFile.good();
					breakFile.close();
					if (copyError || !breakOk)
					{
						scriptErrorFailed = true;
						scriptErrorFailure = "could not prepare the broken "
							"temp copy at " + scriptErrorTempRoot;
					}
					else if (!openProjectFromPath(state, editorCore,
						scriptErrorTempRoot))
					{
						scriptErrorFailed = true;
						scriptErrorFailure = "could not open the project "
							"copy '" + scriptErrorTempRoot + "'";
					}
				}
				if (frameCount == 40 && !scriptErrorFailed &&
					scriptErrorPhase == ScriptErrorPlaytestPhase::Idle)
				{
					// the exact function the Play button calls
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						scriptErrorFailed = true;
						scriptErrorFailure = "startPlay failed";
					}
					else
					{
						scriptErrorPhase = ScriptErrorPlaytestPhase::WaitError;
						scriptErrorDeadline =
							scriptErrorNow + std::chrono::seconds(60);
						SDL_Log("orkige_editor: script-error playtest - Play "
							"pressed on the broken copy");
					}
				}
				else if (scriptErrorPhase == ScriptErrorPlaytestPhase::WaitError)
				{
					// NOTHING is selected on purpose: the failure must arrive
					// through the pushed script_error message, not through the
					// selected object's state stream
					if (playSession.scriptErrorIds.count("Player") != 0)
					{
						// scriptErrorIds non-empty IS the toolbar marker's
						// draw condition - checking the id checks the marker
						if (!consoleHasScriptErrorLine())
						{
							scriptErrorFailed = true;
							scriptErrorFailure = "the script error never "
								"reached the Console as a [remote] SCRIPT "
								"ERROR error line";
						}
						else
						{
							SDL_Log("orkige_editor: script-error playtest - "
								"SCRIPT ERROR on 'Player' surfaced (Console "
								"line + %zu-entry toolbar marker state), "
								"stopping", playSession.scriptErrorIds.size());
							// the exact function the Stop button calls
							requestStopPlay(playSession);
							scriptErrorPhase =
								ScriptErrorPlaytestPhase::WaitRevert;
							scriptErrorDeadline =
								scriptErrorNow + std::chrono::seconds(30);
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						scriptErrorFailed = true;
						scriptErrorFailure = "play session ended before the "
							"script error arrived";
					}
				}
				else if (scriptErrorPhase == ScriptErrorPlaytestPhase::WaitRevert)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						if (!playSession.scriptErrorIds.empty())
						{
							scriptErrorFailed = true;
							scriptErrorFailure = "the script-error marker "
								"state survived Stop";
						}
						else if (playSession.process != nullptr ||
							playSession.client.isConnected())
						{
							scriptErrorFailed = true;
							scriptErrorFailure = "session not fully torn "
								"down after revert";
						}
						else
						{
							SDL_Log("orkige_editor: script-error playtest "
								"PASSED: broken script -> script_error message "
								"-> Console line + marker state -> clean Stop "
								"cleared it");
							std::error_code ignored;
							std::filesystem::remove_all(scriptErrorTempRoot,
								ignored);
							scriptErrorPhase = ScriptErrorPlaytestPhase::Done;
							running = false;
						}
					}
				}
				if (!scriptErrorFailed &&
					scriptErrorPhase != ScriptErrorPlaytestPhase::Idle &&
					scriptErrorPhase != ScriptErrorPlaytestPhase::Done &&
					scriptErrorNow >= scriptErrorDeadline)
				{
					scriptErrorFailed = true;
					scriptErrorFailure = "deadline exceeded in phase " +
						std::to_string(static_cast<int>(scriptErrorPhase));
				}
				if (scriptErrorFailed)
				{
					SDL_Log("orkige_editor: script-error playtest FAILED - %s",
						scriptErrorFailure.c_str());
					if (!scriptErrorTempRoot.empty())
					{
						std::error_code ignored;
						std::filesystem::remove_all(scriptErrorTempRoot,
							ignored);
					}
					exitCode = 7;
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
					Orkige::Vec3 position;
					Orkige::Quat orientation;
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
				// facade-graph child count of the world root: every
				// TransformComponent node lives there, so clear must shrink
				// it and reload must restore it exactly
				const size_t nodesBefore =
					world->getRootNode()->numChildren();
				bool roundTripOk = !before.empty() &&
					Orkige::SceneSerializer::saveScene(selfCheckScene,
						gameObjectManager);
				gameObjectManager.clear();
				const size_t nodesCleared =
					world->getRootNode()->numChildren();
				roundTripOk = roundTripOk &&
					gameObjectManager.getGameObjects().empty() &&
					nodesCleared < nodesBefore;
				roundTripOk = roundTripOk &&
					Orkige::SceneSerializer::loadScene(selfCheckScene,
						gameObjectManager);
				applyUnlitFixToLoadedModels(editorCore);
				const size_t nodesAfter =
					world->getRootNode()->numChildren();
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
					const Orkige::Vec3 position = transform->getPosition();
					const Orkige::Quat orientation =
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
				unsigned int resizedWidth = 0;
				unsigned int resizedHeight = 0;
				render->getWindowSize(resizedWidth, resizedHeight);
				SDL_Log("orkige_editor: resize test frame 100 - render window "
					"%ux%u, scene RTT %dx%d",
					resizedWidth, resizedHeight,
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

		// editor shutdown while an export is running: kill the exporter (a
		// half-written bundle in builds/<platform> is simply re-exported)
		if (exportJob.isActive())
		{
			SDL_KillProcess(exportJob.process, false);
			SDL_DestroyProcess(exportJob.process);
			exportJob.process = nullptr;
		}

		// ImGui teardown: destroying the context writes the ini; the facade
		// 2D layer + font texture die with the renderer/engine afterwards
		gImGuiRenderer = nullptr;
		ImGui::DestroyContext();
		// the console dies with this scope - detach the log hooks first
		// (the engine log capture detaches itself in its destructor)
		engineLogCapture.detach();
		SDL_SetLogOutputFunction(sdlLogHook.previous,
			sdlLogHook.previousUserdata);
	}

	SDL_DestroyWindow(window);
	SDL_Quit();
	return exitCode;
}
