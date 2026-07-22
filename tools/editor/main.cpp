// orkige_editor - the in-engine editor shell (bootstrap).
//
// An editor built as a regular Orkige app: SDL3 owns the window and
// event loop, Orkige::Engine renders into it (externalWindowHandle path, same
// boot sequence as samples/hello_orkige), and the UI is Dear ImGui drawn
// through the engine_render facade (ImGuiFacadeRenderer on DrawLayer2D).
//
// Renderer coupling (Docs/render-abstraction.md): the editor builds and runs on BOTH render
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
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/VectorShapeComponent.h>
#include <core_base/PropertySchema.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <core_project/AssetDatabase.h>
#include <engine_input/InputManager.h>
#include <engine_render/DrawLayer2D.h>
#include <engine_util/StringUtil.h>
#include <engine_util/PlatformWindow.h>
#include <core_game/GameObjectManager.h>
#include <core_game/LevelComponent.h>
#include <core_game/LevelSequence.h>
#include <core_game/PrefabSerializer.h>
#include <core_game/SceneSerializer.h>
#include <core_game/TileComponent.h>
#include <core_project/Project.h>
#include <core_util/StringUtil.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>
#include <engine_runtime/AppHost.h>

#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName (the selfcheck's panel probes)
#include <ImGuizmo.h>
#include <core_debug/DebugMacros.h> // oDebug* + the Console log sink
#include <core_debug/CVarManager.h> // the r.staticScene edit-mode gate

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
#include "EditorAutosave.h"
#include "EditorControlServer.h"
#include "EditorScriptHost.h"
#include "AnimationPreviewStage.h"
#include "EditorImageDecode.h"
#include "GuiPreviewStage.h"
#include "MeshPreviewStage.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>	// the break-variant's filtered project copy
#include <limits>		// the hot-reload playtest's NaN sentinel
#include <mutex>
#include <string>
#include <vector>

#ifndef _WIN32
using Orkige::optr;
using Orkige::woptr;
#endif

int main(int argc, char** argv)
{
	// --mcp-port N / --mcp-token-file PATH (aliases --control-port /
	// --control-token-file): opt-in in-editor MCP endpoint over Streamable HTTP
	// (previously a line-JSON control port). Off unless asked for; the
	// env vars ORKIGE_MCP_PORT / ORKIGE_MCP_TOKEN_FILE (and the historical
	// ORKIGE_CONTROL_PORT / ORKIGE_CONTROL_TOKEN_FILE) are the equivalents.
	// A remote MCP client then connects to http://127.0.0.1:<port>/mcp.
	int controlPort = -1;			// < 0 = the MCP endpoint stays off
	std::string controlTokenFile;
	std::string controlBindValue;	// --mcp-bind / ORKIGE_MCP_BIND ("" = default)
	for (int argIndex = 1; argIndex < argc; ++argIndex)
	{
		if ((std::strcmp(argv[argIndex], "--mcp-port") == 0 ||
			std::strcmp(argv[argIndex], "--control-port") == 0) &&
			argIndex + 1 < argc)
		{
			controlPort = std::atoi(argv[++argIndex]);
		}
		else if ((std::strcmp(argv[argIndex], "--mcp-token-file") == 0 ||
			std::strcmp(argv[argIndex], "--control-token-file") == 0) &&
			argIndex + 1 < argc)
		{
			controlTokenFile = argv[++argIndex];
		}
		else if ((std::strcmp(argv[argIndex], "--mcp-bind") == 0 ||
			std::strcmp(argv[argIndex], "--control-bind") == 0) &&
			argIndex + 1 < argc)
		{
			controlBindValue = argv[++argIndex];
		}
	}
	if (const char* portEnv = std::getenv("ORKIGE_MCP_PORT"))
	{
		controlPort = std::atoi(portEnv);
	}
	else if (const char* portEnv = std::getenv("ORKIGE_CONTROL_PORT"))
	{
		controlPort = std::atoi(portEnv);
	}
	if (const char* tokenEnv = std::getenv("ORKIGE_MCP_TOKEN_FILE"))
	{
		controlTokenFile = tokenEnv;
	}
	else if (const char* tokenEnv = std::getenv("ORKIGE_CONTROL_TOKEN_FILE"))
	{
		controlTokenFile = tokenEnv;
	}
	if (const char* bindEnv = std::getenv("ORKIGE_MCP_BIND"))
	{
		controlBindValue = bindEnv;
	}
	else if (const char* bindEnv = std::getenv("ORKIGE_CONTROL_BIND"))
	{
		controlBindValue = bindEnv;
	}
	// interpret the bind value: loopback (the safe default) unless the caller
	// explicitly asks for every interface. Anything unrecognized stays
	// loopback so a typo can never silently expose the control surface. Binding
	// non-loopback puts FULL editor control on the network - only do it behind
	// a trusted boundary.
	bool controlExposeNonLoopback = false;
	if (!controlBindValue.empty())
	{
		std::string lowered = controlBindValue;
		for (char& character : lowered)
		{
			character = static_cast<char>(std::tolower(
				static_cast<unsigned char>(character)));
		}
		if (lowered == "0.0.0.0" || lowered == "any" || lowered == "all" ||
			lowered == "*")
		{
			controlExposeNonLoopback = true;
		}
		else if (lowered != "127.0.0.1" && lowered != "localhost" &&
			lowered != "loopback")
		{
			SDL_Log("orkige_editor: unrecognized MCP bind '%s' - staying "
				"loopback-only (use 0.0.0.0/any to expose to the network)",
				controlBindValue.c_str());
		}
	}

	// automated run? (any scripted-test/automation hook set) - decided up
	// front because it gates the vsync choice (before Engine::setup: an
	// uncapped editor renders thousands of UI frames per second for no
	// benefit, automated runs stay uncapped so the frame-scripted tests
	// finish as fast as the machine allows) and the reopen-last-project
	// convenience: scripted runs must start exactly where the script
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
		std::getenv("ORKIGE_EDITOR_SCRIPT_ERROR_PLAYTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_HOTRELOAD_PLAYTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_UI_HOTRELOAD_PLAYTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_ANIM_HOTRELOAD_PLAYTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_CONTROL_TEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_CONTROL_PLAYTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_HELPTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_CONTROL_BROWSERTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_CONTROL_BROWSERSESSION") != nullptr ||
		std::getenv("ORKIGE_EDITOR_LEVELPAINT") != nullptr ||
		std::getenv("ORKIGE_EDITOR_PREFABEDIT") != nullptr ||
		std::getenv("ORKIGE_EDITOR_SCRIPTTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_ASSETTEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_AUTOSAVETEST") != nullptr ||
		std::getenv("ORKIGE_EDITOR_THEME_SWITCH") != nullptr ||
		std::getenv("ORKIGE_EDITOR_MIGRATE_TEST") != nullptr;

	int exitCode = 0;

	// migration selfcheck (ORKIGE_EDITOR_MIGRATE_TEST): synthesize an ini from
	// before the panel-defaults rework and verify the one-time migration closes
	// the Tile Palette + GUI Preview, flags the palette re-dock, stamps the
	// version, persists it, and is idempotent on a second load. Pure settings
	// IO - it needs no window, so it runs and exits before the render boot.
	if (std::getenv("ORKIGE_EDITOR_MIGRATE_TEST") != nullptr)
	{
		int migrateExit = 0;
		const std::string testIni =
			(std::filesystem::temp_directory_path() /
				"orkige_migrate_test_view.ini").string();
		{
			// a pre-rework ini: both panels OPEN, no layout_version stamp
			std::ofstream oldIni(testIni, std::ios::trunc);
			oldIni << "panel_tilepalette=1\n"
				<< "panel_gui_preview=1\n"
				<< "mode_2d=0\n";
		}
		auto check = [&](bool cond, const char* what)
		{
			SDL_Log("orkige_editor: migrate-test %s: %s", what,
				cond ? "ok" : "FAILED");
			if (!cond)
			{
				migrateExit = 2;
			}
		};
		ViewSettings migrated;
		migrated.path = testIni;
		migrated.load();
		check(migrated.showTilePalettePanel && migrated.showGuiPreviewPanel,
			"old ini loaded with both panels open");
		check(migrated.layoutVersion == 0, "old ini has no version stamp");
		const bool dockPending = migrated.migrateLayoutDefaults();
		migrated.save();
		check(!migrated.showTilePalettePanel, "tile palette closed");
		check(!migrated.showGuiPreviewPanel, "gui preview closed");
		check(dockPending, "palette re-dock flagged");
		check(migrated.layoutVersion == ViewSettings::CURRENT_LAYOUT_VERSION,
			"version stamped");
		// reload the saved ini: the stamp persisted and a re-migrate is a no-op
		ViewSettings reloaded;
		reloaded.path = testIni;
		reloaded.load();
		check(reloaded.layoutVersion == ViewSettings::CURRENT_LAYOUT_VERSION,
			"version persisted");
		check(!reloaded.migrateLayoutDefaults(), "re-migrate is a no-op");
		std::error_code removeError;
		std::filesystem::remove(testIni, removeError);
		SDL_Log("orkige_editor: migrate-test %s",
			migrateExit == 0 ? "PASSED" : "FAILED");
		return migrateExit;
	}
	// the shared boot spine (engine_runtime/AppHost.h). The editor is the
	// bespoke host: a resizable window, no window-camera rig (the scene
	// renders offscreen into the Scene panel's RenderTexture), and the two
	// boot phases run separately so the console/log plumbing can hook in
	// before anything logs and before the render backend boots.
	Orkige::AppHost host;
	{
		Orkige::AppHostConfig hostConfig;
		hostConfig.windowTitle = "Orkige Editor";
		hostConfig.resizableWindow = true;
		hostConfig.automatedRun = automatedRun;
		hostConfig.engineLogFile = "orkige_editor.log";
		hostConfig.classicMediaDir = ORKIGE_EDITOR_MEDIA_DIR;
		hostConfig.createWindowCamera = false;

		// the Console line store exists before anything logs; the editor's
		// own SDL_Log lines route into it (and still reach the previous
		// output) from here on
		EditorConsole console;
		SdlLogHook sdlLogHook;
		sdlLogHook.console = &console;
		SDL_GetLogOutputFunction(&sdlLogHook.previous,
			&sdlLogHook.previousUserdata);
		SDL_SetLogOutputFunction(consoleSdlLogOutput, &sdlLogHook);
		// mirror the engine's tagged diagnostic stream (oDebug*) into the
		// Console, so operational messages moved off SDL_Log stay visible here.
		// The sink only references the console; it is cleared before the console
		// dies (at scope teardown below), honouring the lifetime contract.
		Orkige::logSetSink([&console](int level, const char* tag,
			const char* message)
		{
			const ConsoleLevel consoleLevel =
				level == Orkige::LL_ERROR ? ConsoleLevel::Error
				: level == Orkige::LL_WARN ? ConsoleLevel::Warning
				: ConsoleLevel::Info;
			console.addLine(consoleLevel, (tag && tag[0])
				? (std::string("[") + tag + "] " + message) : message);
		});

		if (!host.initialise(hostConfig))
		{
			return 1;
		}
		SDL_Window* const window = host.getWindow();
		// testing/multi-display hook: ORKIGE_EDITOR_WINDOW_DISPLAY=<index>
		// centers the window on the given display BEFORE the render context is
		// built, so the drawable adopts that display's backing scale (the
		// content-scale probe below then reads e.g. 2.0 on a Retina panel).
		// Lets the visual selfcheck capture the HiDPI chrome on machines whose
		// primary display is 1x.
		if (const char* displayEnv = std::getenv("ORKIGE_EDITOR_WINDOW_DISPLAY"))
		{
			int displayCount = 0;
			SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
			const int wanted = std::atoi(displayEnv);
			if (displays && wanted >= 0 && wanted < displayCount)
			{
				SDL_SetWindowPosition(window,
					SDL_WINDOWPOS_CENTERED_DISPLAY(displays[wanted]),
					SDL_WINDOWPOS_CENTERED_DISPLAY(displays[wanted]));
			}
			SDL_free(displays);
		}

		// engine log -> Console: the engine_base log-capture service (shared
		// with PlayerDebugLink) queues every line from here on; the frame
		// loop drains it into the Console once per frame. Sized to the
		// Console cap - the OGRE boot easily exceeds the default backlog and
		// the Console wants those lines. The attach point is per-flavor: on
		// classic the log exists from Engine CONSTRUCTION (Ogre::Root is
		// built in the constructor), so this early attach catches the render
		// backend's boot lines; on the next flavor the log only exists once
		// setup created Root - pre-creating the manager is not an option
		// (Root adopting a foreign LogManager leaves its own log member null,
		// which the Vulkan boot path dereferences), so the attach is retried
		// after setupEngine and the Console starts at post-boot lines there.
		Orkige::EngineLogCapture engineLogCapture(EditorConsole::MAX_LINES);
		engineLogCapture.attach();

		if (!host.setupEngine([&host]()
			{
				Orkige::RenderSystem* render = host.getRenderSystem();
				// sample assets (test_mesh.glb from Util/make_test_mesh.py) in
				// the default group; meshes load lazily via Codec_Assimp
				render->addResourceLocation(ORKIGE_EDITOR_ASSET_DIR);
				// jumper sample assets (textured .glb meshes from
				// Util/make_jumper_assets.py) so samples/jumper/level1.oscene
				// opens
				render->addResourceLocation(ORKIGE_EDITOR_JUMPER_ASSET_DIR);
				// the engine-default font (Nunito) directory so a project's
				// .ogui can reference the font by name (font-atlas baking
				// resolves the ttf by resource name across all groups);
				// is_directory keeps it a silent skip
				std::error_code fontDirError;
				if (std::filesystem::is_directory(ORKIGE_EDITOR_FONT_DIR,
					fontDirError))
				{
					render->addResourceLocation(ORKIGE_EDITOR_FONT_DIR);
				}
				// the engine water media dir (the shared water plane mesh +
				// tiling water normal map) so a scene's WaterComponent shows
				// its static preview in the editor scene panel
				std::error_code waterDirError;
				if (std::filesystem::is_directory(ORKIGE_EDITOR_WATER_DIR,
					waterDirError))
				{
					render->addResourceLocation(ORKIGE_EDITOR_WATER_DIR);
				}
				// the engine decal media dir (default mark + blob-shadow
				// textures) so a scene's DecalComponent shows its static
				// preview in the editor scene panel
				std::error_code decalDirError;
				if (std::filesystem::is_directory(ORKIGE_EDITOR_DECAL_DIR,
					decalDirError))
				{
					render->addResourceLocation(ORKIGE_EDITOR_DECAL_DIR);
				}
#ifdef ORKIGE_EDITOR_BLOOM_DIR
				// the engine bloom compositor media (bright/blur/combine
				// material + shaders) so a play session with engine:setBloom
				// resolves its materials - per flavor (bloom/next vs
				// bloom/classic, the build bakes the matching dir)
				std::error_code bloomDirError;
				if (std::filesystem::is_directory(ORKIGE_EDITOR_BLOOM_DIR,
					bloomDirError))
				{
					render->addResourceLocation(ORKIGE_EDITOR_BLOOM_DIR);
				}
#endif
#ifdef ORKIGE_EDITOR_GRADE_DIR
				// the engine output-grade compositor media (the grade material +
				// shaders) so a play session with engine:setGrade resolves its
				// materials - per flavor (grade/next vs grade/classic)
				std::error_code gradeDirError;
				if (std::filesystem::is_directory(ORKIGE_EDITOR_GRADE_DIR,
					gradeDirError))
				{
					render->addResourceLocation(ORKIGE_EDITOR_GRADE_DIR);
				}
#endif
			}))
		{
			return 1;
		}
		// the per-flavor second chance (see the attach comment above): the
		// engine log exists now on every flavor
		if (!engineLogCapture.attach())
		{
			SDL_Log("orkige_editor: engine log capture failed to attach - "
				"the Console will miss the engine log");
		}
		Orkige::RenderSystem* render = host.getRenderSystem();
		Orkige::RenderWorld* world = host.getRenderWorld();

		// edit mode never applies the static mobility flag to the renderer:
		// gizmo moves and inspector edits keep working on the default dynamic
		// path with no mobility-contract warnings, and the flag still
		// round-trips through the inspector/serialization (played scenes get
		// the fast path in the player, which boots with the gate ON)
		Orkige::CVarManager::getSingleton().setString("r.staticScene", "0");

		// The scene does not render into the window: the editor's scene
		// camera draws into the offscreen facade RenderTexture created below
		// on a facade camera rig (near/far defaults 1/100000 match the
		// historical editor camera) - hence no host window-camera rig.
		optr<Orkige::RenderCamera> sceneCamera =
			world->createCamera("EditorSceneCamera");
		optr<Orkige::RenderNode> sceneCameraNode =
			world->createNode("EditorSceneCameraNode");
		sceneCamera->attachTo(sceneCameraNode);

		// UI-only main window (facade): only the window background colour
		// and the ImGui 2D layer reach the screen - all scene content
		// renders offscreen into the Scene panel's RenderTexture. This is a
		// provisional dark clear; applyEditorThemeNow (below) re-sets it to the
		// resolved theme's dockspace colour so the gaps between panels match.
		render->setWindowBackgroundColour(
			Orkige::Color(0.102f, 0.102f, 0.102f));
		render->showUIOnlyWindow();

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
		// the theme colours are applied AFTER the view settings load (below)
		// so the persisted System/Dark/Light choice is honoured; only the font
		// atlas has to be built here (before the renderer initialises).
		if (!Orkige::loadMacSystemFont(ImGui::GetIO(), 14.0f,
			editorContentScale))
		{
			oDebugWarn("editor.boot", 0, "system font unavailable - using the "
				"ImGui default font");
		}
		// merge the icon font (Font Awesome 6 solid) for the asset browser's
		// kind icons. It ships next to the executable in the .app bundle
		// (SDL_GetBasePath = Resources), and out of the build tree it comes from
		// the source media dir (ORKIGE_EDITOR_ICON_FONT_DIR). Missing file -> the
		// browser keeps drawing its glyph icons, so this is never fatal.
		{
			const char* fontBase = SDL_GetBasePath();
			std::string bundledFont =
				std::string(fontBase ? fontBase : "") + "fa-solid-900.ttf";
			std::error_code fontEc;
			const std::string iconFontPath =
				std::filesystem::exists(bundledFont, fontEc)
					? bundledFont
					: std::string(ORKIGE_EDITOR_ICON_FONT_DIR "/fa-solid-900.ttf");
			Orkige::loadEditorIconFont(ImGui::GetIO(), iconFontPath.c_str(),
				14.0f, editorContentScale);
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
		// automated runs neither read nor write the persisted layout: they build
		// the fresh ratio-based default every time, so the suite's captures are
		// deterministic and correctly proportioned at whatever density the run
		// has (a layout persisted at another density would load mis-scaled).
		ImGui::GetIO().IniFilename = automatedRun ? nullptr : imguiIniPath.c_str();

		// viewport settings (grid/orientation gizmo/camera feel) persist in
		// a simple key=value file next to the imgui ini. Automated runs use
		// defaults and an empty path: one test must neither inherit nor rewrite
		// another test's/user's panel visibility, camera, theme or snap settings.
		ViewSettings viewSettings;
		// deferred to a frame with a live Assets node: re-dock the Tile Palette
		// after an older-ini layout migration (set just below)
		bool layoutMigrationDockPending = false;
		if (!automatedRun)
		{
			viewSettings.path = std::string(sdlBasePath ? sdlBasePath : "") +
				"orkige_editor_view.ini";
			viewSettings.load();
			// an ini saved before the panel-defaults rework never got the new
			// closed-by-default Tile Palette / GUI Preview or the palette's
			// bottom-node home - reconcile it once, then the stamp persists
			if (viewSettings.layoutVersion <
				ViewSettings::CURRENT_LAYOUT_VERSION)
			{
				layoutMigrationDockPending = viewSettings.migrateLayoutDefaults();
				viewSettings.save();
			}
		}
		// scene open/save feed File > Open Recent through this pointer
		gViewSettings = &viewSettings;
		// scripted runs must not rewrite the user's recents (see gRecordRecents)
		gRecordRecents = !automatedRun;
		// ... and must never autosave or block on a recovery modal (gAutomatedRun)
		gAutomatedRun = automatedRun;
		sceneCamera->setFOVy(Orkige::Degree(viewSettings.fovDeg));

		// resolve + apply the editor theme now that the persisted preference is
		// loaded. A lambda so the settings toggle and the live OS-appearance
		// event can re-apply it (the content scale and the window clear colour
		// both live in this scope).
		const auto applyEditorThemeNow = [&](Orkige::EditorThemeMode mode)
		{
			const Orkige::EditorThemeVariant variant =
				Orkige::resolveEditorTheme(mode);
			Orkige::applyEditorTheme(ImGui::GetStyle(), variant,
				editorContentScale);
			// the UI-only editor window clears to the theme's dockspace colour
			// so the gaps between docked panels match the chrome
			const ImVec4 dockBg = Orkige::editorDockspaceBackground();
			render->setWindowBackgroundColour(
				Orkige::Color(dockBg.x, dockBg.y, dockBg.z));
		};
		// the visual selfcheck forces a variant with ORKIGE_EDITOR_THEME=dark|
		// light|system (capture-only - it never overwrites the saved preference)
		Orkige::EditorThemeMode bootThemeMode = viewSettings.themeMode;
		if (const char* themeEnv = std::getenv("ORKIGE_EDITOR_THEME"))
		{
			const std::string requested = themeEnv;
			bootThemeMode = (requested == "light")
				? Orkige::EditorThemeMode::Light
				: (requested == "dark") ? Orkige::EditorThemeMode::Dark
				: Orkige::EditorThemeMode::System;
		}
		applyEditorThemeNow(bootThemeMode);
		// the ImGuizmo overlay draws its gizmos with pixel line thicknesses;
		// scale them with the content scale so they keep a constant physical
		// weight on retina (matching the themed chrome and the view-cube inset)
		{
			ImGuizmo::Style& gizmoStyle = ImGuizmo::GetStyle();
			gizmoStyle.TranslationLineThickness *= editorContentScale;
			gizmoStyle.TranslationLineArrowSize *= editorContentScale;
			gizmoStyle.RotationLineThickness *= editorContentScale;
			gizmoStyle.RotationOuterLineThickness *= editorContentScale;
			gizmoStyle.ScaleLineThickness *= editorContentScale;
			gizmoStyle.ScaleLineCircleSize *= editorContentScale;
			gizmoStyle.HatchedAxisLineThickness *= editorContentScale;
			gizmoStyle.CenterCircleSize *= editorContentScale;
		}

		// offscreen scene viewport: initial size is a placeholder, the Scene
		// panel drives resizes from its content region (with hysteresis)
		SceneRenderTarget sceneTarget;
		sceneTarget.camera = sceneCamera;
		createSceneRenderTexture(sceneTarget, 960, 540);

		// input: ImGui first, engine InputManager for whatever is left.
		// ESC through the shared listener - also proves that non-ImGui input
		// still reaches the engine; the intercept (set below, once the
		// EditorCore exists) makes ESC step out of the current mode without
		// ever quitting the editor from the scene view.
		Orkige::InputManager inputManager;
		Orkige::ImGuiSDL3Input imguiInput(window);
		Orkige::QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&Orkige::QuitOnEscape::onKeyPressed, &quitOnEscape);

		// ground-plane reference grid (editor-only, toggled via View menu,
		// invisible to picking, not part of the GameObject world) - a
		// facade line-list mesh since the editor-on-Next port, see
		// createEditorGrid
		optr<Orkige::RenderNode> gridNode = world->createNode("EditorGridNode");
		optr<Orkige::MeshInstance> gridMesh = createEditorGrid(world, gridNode);
		gridNode->setVisible(viewSettings.showGrid);

		// the GameObject world from the host boot (which also built the
		// shared "EditorCube.mesh" + its unlit "VertexColour" material - a
		// real mesh, so cubes go through ModelComponent and round-trip
		// through scene files; the player builds the identical resource)
		Orkige::GameObjectManager& gameObjectManager =
			host.getGameObjectManager();

		// the UI-independent editor logic (selection, tools, undo/redo,
		// object operations) - everything below drives THIS layer
		Orkige::EditorCore editorCore(gameObjectManager);
		// quitOnEscape.intercept is set once EditorState exists (it disarms the
		// paint tile, which lives in the editor UI state) - see below.
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
		// Play-in-Browser static server: one loopback HttpServer instance for
		// the editor's lifetime, doc root swapped per browser play (see
		// EditorBrowserServe.cpp; the MCP endpoint keeps its own instance)
		BrowserServe browserServe;
		// Help > Orkige Help: the URL the frame loop opened last (the
		// published documentation site; "" until the first Help click) -
		// what the ORKIGE_EDITOR_HELPTEST hook asserts
		std::string helpOpenedUrl;
		const char* helpTestEnv = std::getenv("ORKIGE_EDITOR_HELPTEST");
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
		// suitable simulator exists, so local tests stay green on unprepared
		// machines. CI sets ORKIGE_REQUIRE_DEVICE_TEST_TARGET, which turns that
		// missing-device skip into a hard failure. The install fallbacks require
		// the built app on disk
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
					// built app exists (Play boots it and installs the app).
					// ORKIGE_CI_SIMULATOR_SHUTDOWN names a PRE-WARMED shutdown
					// device (CI boots and shuts it down during prep) and wins
					// over the arbitrary first pick: a never-booted device's
					// cold first boot takes many minutes on a loaded runner,
					// a warm re-boot seconds - the flow exercised is identical.
					std::error_code ignored;
					if (std::filesystem::exists(ORKIGE_EDITOR_IOS_PLAYER_APP,
						ignored))
					{
						const char* warmShutdown =
							std::getenv("ORKIGE_CI_SIMULATOR_SHUTDOWN");
						for (SimulatorDevice const& device : simulators)
						{
							if (device.booted)
							{
								continue;
							}
							if (playSession.simulatorUdid.empty() ||
								(warmShutdown && device.udid == warmShutdown))
							{
								playSession.simulatorUdid = device.udid;
								playSession.simulatorLabel = device.name;
							}
						}
					}
				}
				if (playSession.simulatorUdid.empty())
				{
					oDebugWarn("editor.play", 0, "play simulator '" << selector <<
						"' - no suitable simulator (booted with OrkigePlayer.app "
						"installed, or shutdown + the app built at " <<
						ORKIGE_EDITOR_IOS_PLAYER_APP << "), skipping");
					return std::getenv(
						"ORKIGE_REQUIRE_DEVICE_TEST_TARGET") ? 2 : 77;
				}
				oDebugMsg("editor.play", 0, "play simulator '" << selector <<
					"' -> '" << playSession.simulatorLabel << "' (" <<
					playSession.simulatorUdid << ")");
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
		// unprepared machines. CI sets ORKIGE_REQUIRE_DEVICE_TEST_TARGET so a
		// missing emulator fails instead of skipping.)
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
					oDebugWarn("editor.play", 0, "play android 'auto' - no adb "
						"device with the player APK installed, skipping");
					return std::getenv(
						"ORKIGE_REQUIRE_DEVICE_TEST_TARGET") ? 2 : 77;
				}
				oDebugMsg("editor.play", 0, "play android 'auto' -> '" <<
					playSession.androidLabel << "'");
			}
			else
			{
				playSession.androidSerial = androidSelector;
				playSession.androidLabel = androidSelector;
			}
		}

#ifdef __APPLE__
		// ORKIGE_EDITOR_PLAY_IOS_DEVICE: the physical-iPhone deploy GATE probe.
		// Play on a device is an export-and-deploy (build + sign + install +
		// launch via devicectl), not a live play session - a headless test
		// cannot meaningfully drive a signed device install (no certificate on
		// CI, minutes-long signed export), so this asserts the GATE instead:
		// "auto" resolves only when iOS signing is fully configured
		// (isIosSigningConfigured), a device is connected AND the arm64-ios
		// device player app is built, and then reports the open gate and exits
		// 0; otherwise it SKIPS the run (exit 77 = the ctest SKIP_RETURN_CODE),
		// so the test stays green on unprepared machines (no cert -> the target
		// is disabled, exactly the honest gate). Full deploy is owner-validated
		// on real hardware - see Docs/ios-signing.md.
		if (const char* iosDeviceSelector =
			std::getenv("ORKIGE_EDITOR_PLAY_IOS_DEVICE"))
		{
			if (std::string(iosDeviceSelector) == "auto")
			{
				std::error_code ignored;
				const bool deviceTreeBuilt = std::filesystem::exists(
					std::string(ORKIGE_EDITOR_ENGINE_ROOT) + "/build/" +
					ORKIGE_EDITOR_IOS_DEVICE_TREE +
					"/tools/player/OrkigePlayer.app", ignored);
				std::vector<IosHardwareDevice> devices;
				if (isIosSigningConfigured())
				{
					devices = listIosHardwareDevices();
				}
				if (devices.empty() || !deviceTreeBuilt)
				{
					oDebugWarn("editor.play", 0, "play ios-device 'auto' - gate "
						"closed (signing configured: " <<
						(isIosSigningConfigured() ? "yes" : "no") <<
						", connected devices: " << devices.size() <<
						", device player built: " <<
						(deviceTreeBuilt ? "yes" : "no") << "), skipping");
					return 77;
				}
				oDebugMsg("editor.play", 0, "play ios-device 'auto' - gate "
					"OPEN, target '" << devices.front().name << "' (" <<
					devices.front().udid << "); interactive deploy is "
					"owner-validated (see Docs/ios-signing.md)");
				return 0;
			}
		}
#endif

		// A real editor opens EMPTY: no GameObjects, nothing selected, an
		// untitled scene, a clean undo history. The sample resources stay
		// registered (EditorCube.mesh + VertexColour material built above,
		// the asset dir with test_mesh.glb added as a resource location), so
		// GameObject > Create Cube / Create Test Mesh and File > Open Scene
		// work instantly on the blank scene. The scripted test runs create
		// their fixture objects themselves at frame 2 (see the frame loop).
		EditorState state;
		// reachable from the shared menu widgets (e.g. the Theme selector raises
		// state.themeReapplyRequested for the loop below)
		gEditorState = &state;
		// a pending older-ini layout migration re-docks the palette once the UI
		// is up (deferred to a frame where the Assets node exists)
		state.migratePaletteDock = layoutMigrationDockPending;
		// ESC steps out of the current mode; it NEVER quits the editor from the
		// scene view (Cmd+Q / the window close button own quitting, with the
		// unsaved-changes confirm). Priority: disarm the paint tile first, so
		// leaving paint is a single clean action; otherwise clear a selection.
		// The disarm lives here (not only in EditorShortcuts, which is gated on
		// the scene panel being focused) so ESC leaves paint from anywhere.
		// Always consumed, so QuitOnEscape's quit path is never reached from ESC.
		quitOnEscape.intercept = [&editorCore, &state]()
		{
			if (editorCore.getActiveTool() == Orkige::EditorTool::Paint ||
				!state.tilePalette.armedAssetPath.empty())
			{
				disarmPaintTileOnIntent(state, editorCore);
			}
			else if (editorCore.hasSelection())
			{
				editorCore.clearSelection();
			}
			return true;
		};
		// the Asset browser opens at the persisted thumbnail-size zoom
		state.assetBrowser.thumbnailSize = viewSettings.assetThumbnailSize;

		// MCP endpoint: an MCP server hosted IN THIS (editor) process
		// over Streamable HTTP (POST /mcp, JSON-RPC 2.0), exposing editor
		// operations to a remote MCP client (Claude Code/Desktop) - the
		// former Python sidecar is retired. Off unless --mcp-port / the control
		// self-test asked for it. The context bundles the objects the handler
		// bridges to (all owned here). Pumped once per frame below.
		// the shared GUI Preview stage: one gui-into-offscreen-target stack
		// driven by BOTH the GUI Preview tab and the preview_ui MCP verb
		OrkigeEditor::GuiPreviewStage guiPreviewStage;
		// the shared vector-animation preview stage: one .oanim-on-CPU-raster
		// stack driven by the Inspector's animation section and the
		// preview_animation MCP verb
		OrkigeEditor::AnimationPreviewStage animPreviewStage;
		// the shared 3D mesh/material preview stage: the Inspector renders a
		// selected .glb (or a .omat on the shared preview mesh) into an
		// offscreen RTT through this one stage
		OrkigeEditor::MeshPreviewStage meshPreviewStage;
		// a second mesh preview stage (instance slot 1: its own far origin +
		// target name) the asset browser bakes .glb/.omat thumbnails through,
		// one per frame, without disturbing the Inspector's stage
		OrkigeEditor::MeshPreviewStage thumbnailBaker(1);
		Orkige::EditorControlServer controlServer;
		Orkige::EditorControlContext controlContext;
		controlContext.state = &state;
		controlContext.core = &editorCore;
		controlContext.play = &playSession;
		controlContext.console = &console;
		controlContext.sceneTarget = &sceneTarget;
		controlContext.gameObjectManager = &gameObjectManager;
		controlContext.previewStage = &guiPreviewStage;
		controlContext.animPreviewStage = &animPreviewStage;
		// editor-tool host: discovers *.editor.lua tools in the open project and
		// runs one on demand (Tools menu / run_editor_script) in a sandbox whose
		// editor.* table rides the SAME verb handler - so it reuses controlServer
		// purely as a synchronous verb dispatcher (whether or not it is listening)
		Orkige::EditorScriptHost editorScripts;
		editorScripts.setDispatcher(&controlServer);
		state.editorScripts = &editorScripts;
		// the control self-test drives an ephemeral in-process port; a real
		// host passes an explicit --control-port. Two self-test flavors: the
		// edit-world conversation (ORKIGE_EDITOR_CONTROL_TEST) and the runtime-
		// debug conversation (ORKIGE_EDITOR_CONTROL_PLAYTEST) which boots Play
		// over MCP and drives the running game. Both take a screenshot path env.
		const char* controlTestEnv = std::getenv("ORKIGE_EDITOR_CONTROL_TEST");
		const char* controlPlaytestEnv =
			std::getenv("ORKIGE_EDITOR_CONTROL_PLAYTEST");
		// the browser-play flavor (editor_play_browser ctest): the browser
		// target + export-serve-open flow over MCP; exits 77 (SKIP) when the
		// wasm player was never built. The browser-SESSION flavor
		// (editor_play_browser_session ctest) additionally spawns a headless
		// browser at the served URL and drives the LIVE debug session the
		// page dials in; it also skips without a headless browser.
		const char* controlBrowserTestEnv =
			std::getenv("ORKIGE_EDITOR_CONTROL_BROWSERTEST");
		const char* controlBrowserSessionEnv =
			std::getenv("ORKIGE_EDITOR_CONTROL_BROWSERSESSION");
		const char* controlSelfTestEnv = controlTestEnv ? controlTestEnv
			: (controlPlaytestEnv ? controlPlaytestEnv
				: (controlBrowserTestEnv ? controlBrowserTestEnv
					: controlBrowserSessionEnv));
		Orkige::EditorControlSelfTest controlSelfTest;
		if (controlSelfTestEnv != nullptr && controlPort < 0)
		{
			controlPort = 0;	// ephemeral - the in-process test reads it back
			// publish a token so the self-test really exercises the auth path
			// (a token file makes start() mint + enforce a secret)
			if (controlTokenFile.empty())
			{
				controlTokenFile = std::string(controlSelfTestEnv) + ".token";
			}
		}
		if (controlPort >= 0)
		{
			if (!controlServer.start(
				static_cast<unsigned short>(controlPort), controlTokenFile,
				controlExposeNonLoopback))
			{
				SDL_Log("orkige_editor: control port failed to start on "
					"port %d", controlPort);
				if (controlSelfTestEnv != nullptr)
				{
					exitCode = 2;
				}
			}
			else
			{
				oDebugMsg("editor.mcp", 0, "MCP endpoint listening at "
					"http://127.0.0.1:" << controlServer.getPort() << "/mcp" <<
					(controlTokenFile.empty() ? " (no token file - auth off)"
						: ""));
				if (controlExposeNonLoopback)
				{
					// a network-exposed control surface is a deliberate, loud
					// choice - the full editor is reachable off the machine
					oDebugWarn("editor.mcp", 0, "MCP endpoint bound to ALL "
						"interfaces (--mcp-bind) - the full editor-control "
						"surface is reachable over the network; only do this "
						"behind a trusted boundary");
				}
			}
		}

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
			// Cmd+S is the native menu's key equivalent on mac, so the prefab-
			// mode routing (Save Prefab while a stage is open) lives here
			menuActions.saveScene = [statePtr, corePtr, windowPtr]()
				{ saveCurrentDocument(*statePtr, *corePtr, windowPtr); };
			menuActions.saveSceneAs = [statePtr, windowPtr]()
				{ requestFileDialog(*statePtr, windowPtr,
					Orkige::FileDialogAction::SaveSceneAs); };
			menuActions.closePrefab = [statePtr, corePtr]()
				{ requestClosePrefabEdit(*statePtr, *corePtr); };
			menuActions.addSceneToLevels = [statePtr, corePtr]()
				{ addCurrentSceneToLevels(*statePtr, *corePtr); };
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
			menuActions.groupSelected = [corePtr]()
				{ corePtr->groupSelected(); };
			menuActions.createCube = [corePtr]() { corePtr->createCube(); };
			menuActions.createTestMesh = [corePtr]()
				{ corePtr->createTestMesh(); };
			menuActions.createPrefab = [statePtr, corePtr]()
				{ createPrefabFromSelection(*statePtr, *corePtr); };
			// Build menu: the request is deferred to the frame loop (the
			// same flag pattern as the popup opens) so the native menu and
			// the ImGui menu share one start path
			menuActions.exportProject =
				[statePtr](std::string const& platform)
				{ statePtr->requestedExport = platform; };
			menuActions.runEditorScript =
				[statePtr](std::string const& toolName)
				{ statePtr->requestedEditorScript = toolName; };
			menuActions.setPanelVisible = [viewPtr](int panel, bool visible)
			{
				switch (panel)
				{
#define ORKIGE_SET_PANEL_CASE(id, label, defaultVisible, member) \
				case Orkige::id: viewPtr->member = visible; break;
				ORKIGE_EDITOR_PANEL_LIST(ORKIGE_SET_PANEL_CASE)
#undef ORKIGE_SET_PANEL_CASE
				default: break;
				}
				viewPtr->save();
			};
			menuActions.resetLayout = [statePtr]()
				{ statePtr->resetDockLayout = true; };
			menuActions.viewSettings = [statePtr]()
				{ statePtr->showViewSettingsWindow = true; };
			menuActions.projectSettings = [statePtr]()
				{ statePtr->showProjectSettingsWindow = true; };
			menuActions.helpPortal = [statePtr]()
				{ statePtr->requestedHelpPortal = true; };
			menuActions.about = [statePtr]()
				{ statePtr->openAboutPopup = true; };
			Orkige::macMenuInstall(menuActions);
			oDebugMsg("editor.boot", 0, "native menu bar installed (" <<
				Orkige::macMenuItemCount() << " top-level items)");
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
		// ORKIGE_EDITOR_THEME_SWITCH=1: drive the live theme re-apply path
		// (View > Theme / OS-appearance change) at fixed frames and assert the
		// active variant follows - the editor_theme_switch ctest
		const bool themeSwitchTest =
			(std::getenv("ORKIGE_EDITOR_THEME_SWITCH") != nullptr);
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
		// ORKIGE_EDITOR_PREVIEW_SELFCHECK=<project>: open the project and prove
		// both human preview tabs load real assets, dock beside Scene and submit
		// their live texture IDs. Used by editor_previews_next.
		const char* previewSelfcheckEnv =
			std::getenv("ORKIGE_EDITOR_PREVIEW_SELFCHECK");

		// ORKIGE_EDITOR_CUBEMAP_SELFCHECK=<project>: open a project carrying a
		// cubemap .dds and prove the asset-browser thumbnailer SURVIVES it. A
		// cubemap cannot back a 2D thumbnail, so the tile must fall back to the
		// kind glyph (thumbnail id 0) with no crash - before the fix, feeding a
		// cubemap to the 2D+AutomaticBatching loader left a staging texture
		// mid-map and SIGABRTed the editor. Regression guard on BOTH flavors
		// (editor_cubemap_thumbnail{,_next}).
		const char* cubemapSelfcheckEnv =
			std::getenv("ORKIGE_EDITOR_CUBEMAP_SELFCHECK");

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

		// ORKIGE_EDITOR_HOTRELOAD_PLAYTEST=path: scripted Lua hot-reload run
		// (the editor_play_hotreload ctest on tests/projects/hotreload). The
		// project is COPIED to a temp dir (never the real one); its
		// scripts/reload_probe.lua is rewritten to move the Probe transform in
		// init (an editor-observable behavior via the object_state stream).
		// After Play + selecting the Probe, the hook EDITS the script on disk
		// and asserts the editor's scripts/ watcher fired ([reload] Console
		// line) and the player recompiled-and-swapped (the streamed transform
		// took the new value); a broken edit must raise a RED [remote] SCRIPT
		// ERROR while play continues, and a good edit must heal it. Stop must
		// revert cleanly.
		const char* hotreloadPlaytestEnv =
			std::getenv("ORKIGE_EDITOR_HOTRELOAD_PLAYTEST");

		// ORKIGE_EDITOR_UI_HOTRELOAD_PLAYTEST=path: scripted .oui hot-reload run
		// (the editor_ui_hotreload ctest on tests/projects/uihotreload). The
		// project is COPIED to a temp dir (never the real one); its
		// assets/hud.oui carries a single positioned label whose script loads it.
		// After Play, once the label's rect streams over MSG_UI_LAYOUT, the hook
		// OVERWRITES the .oui to move the label and asserts the editor's .oui
		// watcher fired ([reload] Console line) and the player rebuilt the screen
		// (the streamed rect moved); a broken (unparseable) .oui must surface a
		// [remote] error while the OLD (moved) screen stays up. Stop must revert
		// cleanly.
		const char* uiHotreloadPlaytestEnv =
			std::getenv("ORKIGE_EDITOR_UI_HOTRELOAD_PLAYTEST");

		// ORKIGE_EDITOR_ANIM_HOTRELOAD_PLAYTEST=path: scripted vector-animation
		// hot-reload run (the editor_anim_hotreload ctest on
		// tests/projects/animhotreload). The project is COPIED to a temp dir;
		// OPENING it must run the cooked-pair drift scan (the fixture commits a
		// deliberately drifted cook record on assets/probe.json, so the open
		// re-cooks it; the record-less legacy pair must stay byte-untouched).
		// After Play, the hook EDITS probe.json (a renamed clip marker) and
		// asserts the watcher re-cooked it ([import] summary carries the new
		// clip) and the player hot-reloaded the rig ([remote] runtime line),
		// then edits the source-less orphan.oanim directly (direct reload),
		// then breaks BOTH paths: an invalid .json must fail the cook honestly
		// (error line, NO reload, play continues) and an unparseable .oanim
		// must be refused by the player's parse-before-swap ([remote] error,
		// play continues). Stop must revert cleanly.
		const char* animHotreloadPlaytestEnv =
			std::getenv("ORKIGE_EDITOR_ANIM_HOTRELOAD_PLAYTEST");

		// ORKIGE_EDITOR_ASSETTEST=path: scripted Asset browser run (the
		// editor_asset_browser ctest). At frame 10 it COPIES the
		// project to a temp dir (the real one is never touched), opens the
		// copy and exercises the browser headlessly end to end: enumerate the
		// assets (the known project asset carries an id, a raw sidecar-less
		// file dropped into assets/ shows dimmed), run the generic
		// importAssetFile on a temp file (copied into assets/ + sidecar minted
		// + now enumerable with an id), instantiate a sprite via
		// CreateSpriteObjectCommand (GameObject + SpriteComponent exist, undo
		// removes it) and instantiate a prefab through the drag path
		// (instantiateAssetIntoScene). Exits non-zero on any failed assertion.
		const char* assetTestEnv = std::getenv("ORKIGE_EDITOR_ASSETTEST");

		// ORKIGE_EDITOR_SAFEAREA_CHECK=<project dir>: the safe-area DEVICE run
		// (the player_safearea_device ctest). Combined with
		// ORKIGE_EDITOR_PLAY_SIMULATOR=auto it opens the project, plays it on a
		// booted iOS simulator (iPhone 16 - a Dynamic Island device, so a
		// non-zero top inset) and, once the player streams its window size +
		// safe-area insets (MSG_STATS) and its gui widget rects
		// (MSG_UI_LAYOUT), asserts the reported top inset is > 0 and every
		// visible HUD widget lies inside the safe box. Then Stop. The simulator
		// selection above already SKIPs (exit 77) when no sim is prepared, so
		// this run only executes on a real notched-device profile.
		const char* safeAreaCheckEnv =
			std::getenv("ORKIGE_EDITOR_SAFEAREA_CHECK");

		// ORKIGE_EDITOR_ROTATION_CHECK=<project dir>: the device-rotation
		// run (the player_rotation_android / player_rotation_ios ctests).
		// Combined with ORKIGE_EDITOR_PLAY_ANDROID=auto or
		// ORKIGE_EDITOR_PLAY_SIMULATOR=auto it opens the project (whose
		// manifest carries export.orientation=auto - the rotation opt-in),
		// plays it on the device, waits for the MSG_STATS window stream,
		// then ROTATES the device (Android: the system user_rotation
		// setting over adb; iOS: Simulator.app's Device menu over
		// osascript) and asserts over the debug protocol that the drawable
		// dimensions swapped orientation while the player kept rendering
		// (a stats message carrying the flipped size), that on a notched
		// simulator the safe-area inset moved to a side edge, and that
		// rotating back restores the original orientation. An osascript
		// refusal (no Automation/Accessibility permission) SKIPS the run
		// (exit 77) - the iOS leg is a local-only affordance.
		const char* rotationCheckEnv =
			std::getenv("ORKIGE_EDITOR_ROTATION_CHECK");

		// a plain interactive launch reopens the last project
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
				oDebugMsg("editor.project", 0, "reopening last project '" <<
					lastProjectRoot << "' (View Settings > Reopen Last Project)");
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
			(exportExampleEnv != nullptr) || (controlSelfTestEnv != nullptr);
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

		// hot-reload playtest state that spans frames
		enum class HotReloadPlaytestPhase
		{
			Idle, WaitBaseline, WaitGood, WaitBroken, WaitHeal,
			WaitRevert, Done
		};
		HotReloadPlaytestPhase hotreloadPhase =
			HotReloadPlaytestPhase::Idle;
		std::chrono::steady_clock::time_point hotreloadDeadline;
		std::string hotreloadTempRoot;	//!< the temp project copy
		// does the Console hold a "[reload]" line (the watcher fired)?
		auto consoleHasReloadLine = [&console]()
		{
			std::lock_guard<std::mutex> lock(console.mutex);
			for (ConsoleLine const& line : console.lines)
			{
				if (line.text.rfind("[reload] ", 0) == 0)
				{
					return true;
				}
			}
			return false;
		};
		// the Probe's streamed transform x (a NaN sentinel when the
		// object_state stream has not carried a position yet)
		auto hotreloadRemoteX = [&playSession]() -> float
		{
			if (playSession.stateObjectId != "Probe")
			{
				return std::numeric_limits<float>::quiet_NaN();
			}
			auto it = playSession.stateProperties.find(
				"TransformComponent.position");
			if (it == playSession.stateProperties.end())
			{
				return std::numeric_limits<float>::quiet_NaN();
			}
			float xyz[3] = { 0.0f, 0.0f, 0.0f };
			if (!parsePlayFloats(it->second, xyz, 3))
			{
				return std::numeric_limits<float>::quiet_NaN();
			}
			return xyz[0];
		};
		// (over)write the temp copy's reload_probe.lua; false on I/O error
		auto hotreloadWriteScript = [&hotreloadTempRoot](
			std::string const& source) -> bool
		{
			const std::filesystem::path scriptPath =
				std::filesystem::path(hotreloadTempRoot) /
				"scripts" / "reload_probe.lua";
			{
				std::ofstream file(scriptPath,
					std::ios::binary | std::ios::trunc);
				file << source;
				if (!file.good())
				{
					return false;
				}
			}
			// the reload watcher compares mtimes: on filesystems with coarse
			// timestamp granularity a rewrite within the same granule would
			// be invisible, so stamp a strictly newer mtime explicitly
			std::error_code stampError;
			const std::filesystem::file_time_type written =
				std::filesystem::last_write_time(scriptPath, stampError);
			if (!stampError)
			{
				std::filesystem::last_write_time(scriptPath,
					written + std::chrono::seconds(2), stampError);
			}
			return true;
		};
		// init moves the Probe transform to (x,0,0) - the editor observes
		// the change through the object_state stream (no shared visibility)
		auto hotreloadMoveScript = [](int x) -> std::string
		{
			return "function init(self)\n\tself.transform:setPosition("
				"Vector3(" + std::to_string(x) + ", 0, 0))\nend\n";
		};

		// .oui hot-reload playtest state that spans frames
		enum class UiHotReloadPhase
		{
			Idle, WaitBaseline, WaitMoved, WaitBroken, WaitRevert, Done
		};
		UiHotReloadPhase uiHotreloadPhase = UiHotReloadPhase::Idle;
		std::chrono::steady_clock::time_point uiHotreloadDeadline;
		std::string uiHotreloadTempRoot;	//!< the temp project copy
		float uiBaselineLeft = std::numeric_limits<float>::quiet_NaN();
		float uiMovedLeft = std::numeric_limits<float>::quiet_NaN();
		// the "probe" label's streamed on-screen left (a NaN sentinel until the
		// MSG_UI_LAYOUT stream has carried it)
		auto uiProbeLeft = [&playSession]() -> float
		{
			for (PlaySession::RemoteWidgetRect const& w :
				playSession.remoteUiLayout)
			{
				if (w.id == "probe")
				{
					return static_cast<float>(w.left);
				}
			}
			return std::numeric_limits<float>::quiet_NaN();
		};
		// (over)write the temp copy's assets/hud.oui; false on I/O error
		auto uiWriteOui = [&uiHotreloadTempRoot](
			std::string const& source) -> bool
		{
			const std::filesystem::path ouiPath =
				std::filesystem::path(uiHotreloadTempRoot) /
				"assets" / "hud.oui";
			{
				std::ofstream file(ouiPath,
					std::ios::binary | std::ios::trunc);
				file << source;
				if (!file.good())
				{
					return false;
				}
			}
			// stamp a strictly newer mtime so a rewrite within a coarse
			// filesystem timestamp granule stays visible to the watcher
			std::error_code stampError;
			const std::filesystem::file_time_type written =
				std::filesystem::last_write_time(ouiPath, stampError);
			if (!stampError)
			{
				std::filesystem::last_write_time(ouiPath,
					written + std::chrono::seconds(2), stampError);
			}
			return true;
		};
		// a hud.oui whose label sits at (x, 100)
		auto uiMovedOui = [](int x) -> std::string
		{
			return "[Layout]\natlas = gui_default\n\n[Label probe]\nfont = 9\n"
				"text = HUD\nposition = " + std::to_string(x) + " 100\n";
		};
		// does the Console hold a "[remote]" error line about reload_ui?
		auto consoleHasReloadUiErrorLine = [&console]()
		{
			std::lock_guard<std::mutex> lock(console.mutex);
			for (ConsoleLine const& line : console.lines)
			{
				if (line.level == ConsoleLevel::Error &&
					line.text.rfind("[remote]", 0) == 0 &&
					line.text.find("reload_ui") != std::string::npos)
				{
					return true;
				}
			}
			return false;
		};

		// vector-animation hot-reload playtest state that spans frames
		enum class AnimHotReloadPhase
		{
			Idle, WaitBaseline, WaitRecook, WaitOrphan, WaitBrokenCook,
			WaitBrokenReload, WaitRevert, Done
		};
		AnimHotReloadPhase animHotreloadPhase = AnimHotReloadPhase::Idle;
		std::chrono::steady_clock::time_point animHotreloadDeadline;
		std::string animHotreloadTempRoot;	//!< the temp project copy
		// how many console lines contain the substring (the "did a second
		// reload fire?" probe needs a count, not a boolean)
		auto consoleCountLines = [&console](std::string const& needle) -> int
		{
			std::lock_guard<std::mutex> lock(console.mutex);
			int count = 0;
			for (ConsoleLine const& line : console.lines)
			{
				if (line.text.find(needle) != std::string::npos)
				{
					++count;
				}
			}
			return count;
		};
		// does the Console hold an ERROR line containing the substring?
		auto consoleHasErrorLine = [&console](std::string const& needle)
		{
			std::lock_guard<std::mutex> lock(console.mutex);
			for (ConsoleLine const& line : console.lines)
			{
				if (line.level == ConsoleLevel::Error &&
					line.text.find(needle) != std::string::npos)
				{
					return true;
				}
			}
			return false;
		};
		// (over)write a temp-copy asset with a strictly newer mtime (the
		// uiWriteOui stamping rule - a rewrite within one filesystem timestamp
		// granule must stay visible to the watcher)
		auto animWriteAsset = [&animHotreloadTempRoot](
			std::string const& name, std::string const& content) -> bool
		{
			const std::filesystem::path assetPath =
				std::filesystem::path(animHotreloadTempRoot) / "assets" / name;
			{
				std::ofstream file(assetPath,
					std::ios::binary | std::ios::trunc);
				file << content;
				if (!file.good())
				{
					return false;
				}
			}
			std::error_code stampError;
			const std::filesystem::file_time_type written =
				std::filesystem::last_write_time(assetPath, stampError);
			if (!stampError)
			{
				std::filesystem::last_write_time(assetPath,
					written + std::chrono::seconds(2), stampError);
			}
			return true;
		};
		// read a temp-copy asset ("" on error)
		auto animReadAsset = [&animHotreloadTempRoot](
			std::string const& name) -> std::string
		{
			std::ifstream file(std::filesystem::path(animHotreloadTempRoot) /
				"assets" / name, std::ios::binary);
			std::stringstream text;
			text << file.rdbuf();
			return file ? text.str() : std::string();
		};
		// the probe-rig reload count observed when the broken-cook leg started
		// (a failed cook must never grow it)
		int animProbeReloadsAtBreak = 0;

		enum class PlaytestPhase
		{
			Idle, WaitRemote, WaitState, Interfere, WaitRevert, Done
		};
		PlaytestPhase playtestPhase = PlaytestPhase::Idle;
		std::chrono::steady_clock::time_point playtestDeadline;
		size_t playtestLocalObjects = 0;
		unsigned long playtestScreenshotFrame = 0;
		unsigned long playtestInterfereFrame = 0;

		// safe-area device run state (ORKIGE_EDITOR_SAFEAREA_CHECK)
		enum class SafeAreaPhase { Idle, WaitStream, WaitStop, Done };
		SafeAreaPhase safeAreaPhase = SafeAreaPhase::Idle;
		std::chrono::steady_clock::time_point safeAreaDeadline;
		bool safeAreaFailed = false;
		std::string safeAreaFailure;
		// the first MSG_STATS can carry a still-settling top inset of 0 before
		// UIKit lays the view out; hold a bounded window for a real notch to
		// arrive (well under the WaitStream deadline) and, if it never does,
		// report the specific "not > 0" cause rather than a generic timeout
		bool safeAreaSawStream = false;
		std::chrono::steady_clock::time_point safeAreaStreamAt;
		const std::chrono::seconds safeAreaInsetSettle{ 20 };

		// device-rotation run state (ORKIGE_EDITOR_ROTATION_CHECK)
		enum class RotationPhase
		{
			Idle, WaitStream, WaitRotated, WaitRestored, WaitStop, Done
		};
		RotationPhase rotationPhase = RotationPhase::Idle;
		std::chrono::steady_clock::time_point rotationDeadline;
		bool rotationFailed = false;
		bool rotationSkip = false;		//!< end the run as SKIPPED (exit 77)
		std::string rotationFailure;
		long long rotationBaselineW = -1;
		long long rotationBaselineH = -1;
		// the device's rotation settings before the run (Android), restored
		// on every exit path so the emulator is left as it was found
		std::string rotationSavedAccel;
		std::string rotationSavedUser;
		bool rotationSettingsSaved = false;

		// ORKIGE_EDITOR_LEVELPAINT=<roller project dir>: the level-paint state
		// seam test (editor_level_paint ctest). Frame 10 copies the project to a
		// temp dir, paints/erases prefab instances through the EditorCore paint
		// seams (grid from a LevelComponent), round-trips the scene and appends
		// it to the level sequence; then it PLAYS the painted scene and requires
		// the painted roots + their non-suppressed wall children to arrive over
		// the debug protocol. Both flavors.
		const char* levelPaintEnv = std::getenv("ORKIGE_EDITOR_LEVELPAINT");
		enum class LevelPaintPhase
		{
			Idle, WaitRemote, WaitRevert, EscapeCheck, Done
		};
		LevelPaintPhase levelPaintPhase = LevelPaintPhase::Idle;
		int levelPaintEscapeStep = 0;
		std::chrono::steady_clock::time_point levelPaintDeadline;
		std::string levelPaintTempRoot;
		Orkige::StringVector levelPaintExpectedRoots;
		//! bare-asset tile roots painted into the same scene (a sprite tile) -
		//! the play acceptance requires them in the remote hierarchy too, but
		//! they carry no prefab-provided children
		Orkige::StringVector levelPaintExpectedBareRoots;

		// ORKIGE_EDITOR_MARQUEE=<roller project dir>: the 2D editing-ergonomics
		// test (editor_marquee ctest). Frame 10 copies the project, paints three
		// tiles in 2D mode (checking the ghost-preview handle and the undoable
		// drop-cell paint on the way), then drives the REAL scene-panel input with
		// synthetic SDL mouse events: a rubber-band drag over the tiles selects all
		// three, a plain click selects one (no marquee), a Cmd-drag EXTENDS. Both
		// flavors.
		const char* marqueeEnv = std::getenv("ORKIGE_EDITOR_MARQUEE");
		enum class MarqueePhase { Idle, Run, Done };
		MarqueePhase marqueePhase = MarqueePhase::Idle;
		int mqStep = 0;
		int mqSettleHold = 0;	// frames an assert step has waited to settle
		std::string mqTempRoot;
		Orkige::StringVector mqTileIds;		// the three painted tile roots
		// screen points (render-target pixels), computed once the 2D layout is
		// live: the all-tiles marquee box, the mid-tile click point, and the
		// single-tile boxes over the left / right tiles (both press in the empty
		// top strip so they begin a marquee, not a pick)
		float mqStartX = 0.0f, mqStartY = 0.0f, mqEndX = 0.0f, mqEndY = 0.0f;
		float mqMidX = 0.0f, mqMidY = 0.0f;
		float mqLbSX = 0.0f, mqLbSY = 0.0f, mqLbEX = 0.0f, mqLbEY = 0.0f;
		float mqRbSX = 0.0f, mqRbSY = 0.0f, mqRbEX = 0.0f, mqRbEY = 0.0f;

		// ORKIGE_EDITOR_SCRIPTTEST=<roller project dir>: the editor-scripts
		// selfcheck (editor_scripts ctest). Frame 10 copies the project, writes
		// fixture *.editor.lua tools, and runs them through the EditorScriptHost -
		// asserting the discovery, the editor.* verb surface, the ONE-undo-step
		// contract, the error-path rollback (a failing tool reports file:line and
		// leaves no partial edits) and the shipped sample tool. Both flavors.
		const char* scriptTestEnv = std::getenv("ORKIGE_EDITOR_SCRIPTTEST");
		std::string scriptTestTempRoot;

		// ORKIGE_EDITOR_AUTOSAVETEST=1: the autosave + backup + component
		// copy/paste selfcheck (editor_autosave ctest). Frame 10 exercises the
		// wired paths on a temp loose scene end to end.
		const char* autosaveTestEnv = std::getenv("ORKIGE_EDITOR_AUTOSAVETEST");
		std::string autosaveTestDir;

		// ORKIGE_EDITOR_PREFABEDIT=<roller project dir>: the prefab edit-mode
		// selfcheck (editor_prefab_edit ctest). Frame 10 copies the project to
		// a temp dir, authors a tile instance with a per-instance override,
		// saves the scene, then drives the whole stage loop through the same
		// free functions the UI calls: open (world = the prefab subtree,
		// fresh undo scope, lifecycle guards refuse), edit a child + add an
		// object under the root, the save-guard refusals (stray root, deleted
		// root), save, a dirty close with the explicit Discard policy, and
		// the restored scene whose instance shows the prefab edit while the
		// pre-existing override survived. Single-shot and condition-driven -
		// every step's outcome is asserted immediately.
		const char* prefabEditEnv = std::getenv("ORKIGE_EDITOR_PREFABEDIT");
		std::string prefabEditTempRoot;

		bool running = true;
		unsigned long frameCount = 0;
		std::string lastWindowTitle;
		// the Tile Palette auto-opens when the Scene ENTERS 2D editor mode (and
		// never auto-closes); track the prior state to fire only on the edge
		bool prevEditor2D = viewSettings.editor2D;
		// timed autosave bookkeeping: the wall-clock time the last autosave (or
		// the loop start) ran, and the interval between them. Automated runs
		// never autosave (gAutomatedRun gates shouldAutosave), so the selfcheck
		// drives writeSceneAutosave directly.
		std::chrono::steady_clock::time_point lastAutosaveTime =
			std::chrono::steady_clock::now();
		const double autosaveIntervalSeconds =
			Orkige::EditorAutosave::defaultIntervalSeconds();
		while (running)
		{
			// window title: a project ROOTS it ("Orkige Editor - <project> -
			// <scene>", the scene shown project-relative), loose-scene mode
			// keeps the historical "Orkige Editor - <scene path>"; the dirty
			// marker applies to both. A prefab edit stage shows the prefab
			// instead of the scene (the dirty marker then means prefab-dirty).
			std::string sceneLabel = state.currentScenePath.empty()
				? std::string("untitled") : state.currentScenePath;
			if (isPrefabEditActive(state))
			{
				PrefabEditContext const& prefabContext =
					state.prefabEditStack.back();
				sceneLabel = (prefabContext.prefabRef.empty()
					? prefabContext.prefabPath : prefabContext.prefabRef) +
					" (prefab)";
			}
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
				if (event.type == SDL_EVENT_SYSTEM_THEME_CHANGED &&
					viewSettings.themeMode == Orkige::EditorThemeMode::System)
				{
					// the OS appearance flipped and we follow it: re-apply live
					state.themeReapplyRequested = true;
				}
				if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
				{
					// Finder drop: a mesh file imports AND instantiates (as
					// before, File > Import Mesh...); any other file (png/lua/
					// oscene/oprefab/...) rides the generic asset import -
					// copied into the project assets/ + sidecar minted, no
					// scene object (browse/instantiate it from the Assets
					// panel). Drop events are not mouse/keyboard input, so this
					// does not compete with the ImGui event routing below.
					const std::string dropped(event.drop.data);
					if (Orkige::isSupportedMeshFile(dropped))
					{
						importMeshFromPath(state, editorCore, dropped);
					}
					else if (!importAssetFile(state, dropped).empty())
					{
						oDebugMsg("editor.assets", 0, "imported '" << dropped <<
							"' into the project assets");
					}
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

			// View > Theme change or a live OS-appearance flip: re-apply the
			// style + window clear colour (viewSettings.themeMode already holds
			// the new selection; System re-resolves the OS appearance)
			if (state.themeReapplyRequested)
			{
				state.themeReapplyRequested = false;
				applyEditorThemeNow(viewSettings.themeMode);
			}

			// one-shot framing request (opening a prefab stage selects its
			// root and asks for the F-key framing; the camera lives here)
			if (state.frameSelectedRequested)
			{
				state.frameSelectedRequested = false;
				if (sceneTarget.camera)
				{
					frameSelectedObject(state, editorCore, sceneTarget.camera);
				}
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
			updatePlaySession(state, playSession, console);

			// timed autosave: a crash-recovery copy of a dirty scene to its
			// ".autosave" sibling (never the real file). Stands down during an
			// automated run, a play session and a prefab edit stage (whose world
			// is the prefab subtree, not the scene) - see shouldAutosave.
			{
				const double secondsSinceLastAutosave =
					std::chrono::duration_cast<std::chrono::duration<double>>(
						std::chrono::steady_clock::now() - lastAutosaveTime)
						.count();
				if (Orkige::EditorAutosave::shouldAutosave(
					editorCore.isSceneDirty(), gAutomatedRun,
					playSession.isActive(), isPrefabEditActive(state),
					secondsSinceLastAutosave, autosaveIntervalSeconds) &&
					!state.currentScenePath.empty())
				{
					if (writeSceneAutosave(state, editorCore))
					{
						oDebugMsg("editor.scene", 0, "autosaved '" <<
							Orkige::EditorAutosave::autosavePath(
								state.currentScenePath) << "'");
					}
					lastAutosaveTime = std::chrono::steady_clock::now();
				}
			}

			// MCP endpoint: accept/read/dispatch HTTP JSON-RPC tool calls
			// Pumped after the play session so play-control verbs act
			// on current state; no-op when the endpoint never started.
			controlServer.update(controlContext);

			// project export: act on a Build-menu request, then pump the
			// running exporter's output into the Console ([export] lines)
			if (!state.requestedExport.empty())
			{
				startExport(exportJob, state.project, state.requestedExport,
					console);
				state.requestedExport.clear();
			}
			// Tools menu: run the requested editor script tool once (its edits
			// fold into one undo step; a script error rolls back + logs). Run at
			// this clean point in the loop, not inside the menu callback.
			if (!state.requestedEditorScript.empty())
			{
				editorScripts.runToolByName(state.requestedEditorScript,
					controlContext);
				state.requestedEditorScript.clear();
			}
			// Play-on-iPhone: an "ios" export whose success installs + launches
			// on the picked device (the deploy fields ride on ExportJob so the
			// existing async export pump carries the whole flow). Needs a loaded
			// project (the device runs the signed bundle, there is no temp-scene
			// handoff over USB) - reported honestly to the Console otherwise.
			if (!state.requestedIosDeviceDeployUdid.empty())
			{
				if (!state.project.isLoaded())
				{
					console.addLine(ConsoleLevel::Warning, "[deploy] Play on "
						"iOS hardware needs an open project - a signed device "
						"install ships the whole project bundle (no loose-scene "
						"handoff over USB)");
				}
				else if (startExport(exportJob, state.project, "ios", console))
				{
					exportJob.deployDeviceUdid =
						state.requestedIosDeviceDeployUdid;
					exportJob.deployDeviceLabel =
						state.requestedIosDeviceDeployLabel;
				}
				state.requestedIosDeviceDeployUdid.clear();
				state.requestedIosDeviceDeployLabel.clear();
			}
			// Play in Browser: a "web" export whose success serves the artifact
			// directory + opens the default browser (the deployBrowser fields on
			// ExportJob carry the continuation through the existing async export
			// pump). Needs a loaded project - the page boots the bundled project
			// payload, there is no loose-scene handoff into a browser tab.
			if (state.requestedBrowserPlay)
			{
				if (!state.project.isLoaded())
				{
					console.addLine(ConsoleLevel::Warning, "[deploy] Play in "
						"Browser needs an open project - the page boots the "
						"exported project bundle (no loose-scene handoff)");
				}
				else if (startExport(exportJob, state.project, "web", console))
				{
					exportJob.deployBrowser = true;
					state.browserPlayStatus = "exporting";
					state.browserPlayUrl.clear();
				}
				state.requestedBrowserPlay = false;
			}
			updateExportJob(exportJob, console);
			// browser-play continuation: serve the fresh artifact and open the
			// default browser at it. A re-play re-points the ONE server's doc
			// root; a previous tab's fetches answer 404 from then on (those
			// artifacts no longer exist - the honest outcome).
			if (exportJob.browserArtifactReady)
			{
				exportJob.browserArtifactReady = false;
				exportJob.deployBrowser = false;
				std::string url;
				std::string serveError;
				if (playSession.isActive())
				{
					// another session is already live (a desktop play
					// started while the export ran): serving follows the
					// browser session, so this attempt is abandoned rather
					// than hijacking that session
					console.addLine(ConsoleLevel::Warning, "[deploy] the "
						"web build is exported, but a play session is "
						"already active - stop it and press Play in "
						"Browser again");
					state.browserPlayStatus = "failed";
				}
				else if (!browserServeStart(browserServe,
					exportJob.artifactPath, url, serveError))
				{
					console.addLine(ConsoleLevel::Error,
						"[deploy] Play in Browser: " + serveError);
					state.browserPlayStatus = "failed";
				}
				else
				{
					// the page dials the debug link back into THIS serve
					// port (the shell maps ?env.* onto the module
					// environment; the wasm runtime dials the endpoint and
					// the port's WebSocket upgrade lands in the session's
					// DebugClient - a live session like desktop play)
					url += "?env.ORKIGE_DEBUG_CONNECT=127.0.0.1:" +
						std::to_string(browserServe.server.getPort());
					state.browserPlayUrl = url;
					state.browserPlayStatus = "serving";
					beginBrowserPlaySession(playSession,
						state.project.isLoaded()
							? state.project.getRootDirectory()
							: std::string());
					console.addLine(ConsoleLevel::Info, "[deploy] "
						"serving the web build at " + url + " - waiting "
						"for the page to connect the debug link");
					// automated runs never touch the user's default browser
					// (the scripted tests fetch the served files themselves,
					// or drive their own headless browser at the URL)
					if (!automatedRun && !SDL_OpenURL(url.c_str()))
					{
						// the toolbar's web status keeps the URL clickable,
						// so the recovery is one click away
						console.addLine(ConsoleLevel::Error,
							"[deploy] could not open the default browser: " +
							std::string(SDL_GetError()) + " - open " + url +
							" yourself (or click the web status in the "
							"toolbar)");
					}
				}
			}
			else if (!exportJob.isActive() && exportJob.deployBrowser)
			{
				// the web export failed (updateExportJob reported the lines);
				// the browser-play attempt is over
				exportJob.deployBrowser = false;
				state.browserPlayStatus = "failed";
			}
			// Stop ends the serve with the session (EditorBrowserServe
			// answers the honest 404 once no browser play is live) - reset
			// the toolbar/get_state status so nothing offers a URL the
			// server would refuse
			if (!playSession.onBrowser &&
				(state.browserPlayStatus == "serving" ||
					state.browserPlayStatus == "connected"))
			{
				state.browserPlayStatus.clear();
				state.browserPlayUrl.clear();
				browserServe.docRoot.clear();
			}
			// pump the static server (accept/read/respond; a no-op while idle)
			browserServeUpdate(browserServe, playSession);

			// Help > Orkige Help: open the published documentation site
			// (the docs corpus + engine API reference, redeployed by CI on
			// every main push - see HELP_PORTAL_URL). Automated runs never
			// touch the user's default browser.
			if (state.requestedHelpPortal)
			{
				state.requestedHelpPortal = false;
				helpOpenedUrl = HELP_PORTAL_URL;
				console.addLine(ConsoleLevel::Info,
					std::string("[help] ") + HELP_PORTAL_URL);
				if (!gAutomatedRun && !SDL_OpenURL(HELP_PORTAL_URL))
				{
					console.addLine(ConsoleLevel::Error,
						std::string("[help] could not open the default "
						"browser: ") + SDL_GetError() + " - open " +
						HELP_PORTAL_URL + " yourself");
				}
			}

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
			// Exercise the real preview-panel docking path in the editor selfcheck:
			// the GUI Preview panel must join Scene's dock node, not appear as a
			// loose window.
			if (selfCheck && frameCount == 5)
			{
				viewSettings.showGuiPreviewPanel = true;
			}

			// snapshot the panel visibility: a close-button click (the x in
			// a docked tab) must persist exactly like a View-menu toggle
			const bool panelsBefore[Orkige::PANEL_COUNT] = {
#define ORKIGE_PANEL_VISIBILITY(id, label, visible, member) \
				viewSettings.member,
				ORKIGE_EDITOR_PANEL_LIST(ORKIGE_PANEL_VISIBILITY)
#undef ORKIGE_PANEL_VISIBILITY
			};
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
			drawProjectSettingsWindow(state);
			// the View menu may have toggled the grid; 2D mode hides the XZ
			// ground grid (it lies edge-on to the top-down view; an
			// XY grid redraw is deferred)
			gridNode->setVisible(viewSettings.showGrid &&
				!viewSettings.editor2D);
			const float toolbarHeight =
				drawToolbar(state, playSession, editorCore);
			drawDockspace(state, toolbarHeight, viewSettings,
				editorContentScale);
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
					animPreviewStage, meshPreviewStage,
					&viewSettings.showInspectorPanel);
			}
			if (viewSettings.showStatsPanel)
			{
				drawStatsPanel(playSession, &viewSettings.showStatsPanel);
			}
			if (viewSettings.showConsolePanel)
			{
				drawConsolePanel(state, playSession, console,
					&viewSettings.showConsolePanel);
			}
			if (viewSettings.showAssetBrowserPanel)
			{
				drawAssetBrowserPanel(state, editorCore,
					&viewSettings.showAssetBrowserPanel);
			}
			// one-time layout migration: with the Assets (bottom) node live this
			// frame, move the Tile Palette there so its 2D auto-open lands beside
			// the browser instead of the pre-rework slot the restored ini kept.
			// The palette is closed here (migration closed it), so this only
			// updates its docked home for the next time it opens.
			if (state.migratePaletteDock)
			{
				ImGuiWindow* assetsWindow =
					ImGui::FindWindowByName("Assets###Assets");
				if (assetsWindow && assetsWindow->DockId != 0)
				{
					ImGui::DockBuilderDockWindow("Tile Palette###TilePalette",
						assetsWindow->DockId);
					state.migratePaletteDock = false;
				}
			}
			// entering 2D editor mode auto-opens the Tile Palette (it docks
			// beside the Asset Browser); leaving 2D never auto-closes it, and a
			// saved layout that had it open/closed is left untouched
			if (viewSettings.editor2D && !prevEditor2D)
			{
				viewSettings.showTilePalettePanel = true;
			}
			prevEditor2D = viewSettings.editor2D;
			if (viewSettings.showTilePalettePanel)
			{
				drawTilePalettePanel(state, editorCore,
					&viewSettings.showTilePalettePanel);
			}
			else
			{
				state.tilePalette.focused = false;
			}
			if (viewSettings.showGuiPreviewPanel)
			{
				drawGuiPreviewPanel(state, guiPreviewStage, editorCore,
					viewSettings);
			}
			bool panelVisibilityChanged = false;
#define ORKIGE_CHECK_PANEL_VISIBILITY(id, label, visible, member) \
			panelVisibilityChanged |= \
				panelsBefore[Orkige::id] != viewSettings.member;
			ORKIGE_EDITOR_PANEL_LIST(ORKIGE_CHECK_PANEL_VISIBILITY)
#undef ORKIGE_CHECK_PANEL_VISIBILITY
			if (panelVisibilityChanged)
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
				menuStatus.sceneInProject =
					state.project.isLoaded() && !state.currentScenePath.empty();
				menuStatus.canExport =
					state.project.isLoaded() && !exportJob.isActive();
#define ORKIGE_SYNC_PANEL_STATUS(id, label, visible, member) \
				menuStatus.panelVisible[Orkige::id] = viewSettings.member;
				ORKIGE_EDITOR_PANEL_LIST(ORKIGE_SYNC_PANEL_STATUS)
#undef ORKIGE_SYNC_PANEL_STATUS
				menuStatus.recentScenes = viewSettings.recentScenes;
				menuStatus.recentProjects = viewSettings.recentProjects;
				menuStatus.prefabEditActive = isPrefabEditActive(state);
				menuStatus.saveLabel = menuStatus.prefabEditActive
					? "Save Prefab" : "Save Scene";
				menuStatus.scriptingAvailable =
					Orkige::EditorScriptHost::scriptingAvailable();
				for (Orkige::EditorScriptTool const& tool :
					editorScripts.tools())
				{
					menuStatus.editorTools.push_back({ tool.name, tool.label });
				}
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

			if (!host.getEngine().renderOneFrame())
			{
				running = false;
			}
			++frameCount;

			// bake at most one queued .glb/.omat asset-browser thumbnail now
			// that the frame (and the baker's offscreen target) has rendered -
			// OUTSIDE the ImGui frame, so nothing re-enters renderOneFrame
			serviceThumbnailBakes(state, thumbnailBaker, frameCount);

			// bake the Inspector's .oui thumbnail post-render: a .oui is a GPU
			// render (GuiPreviewStage), so the Inspector cannot bake it mid-frame
			// - it requests here. Borrow the shared stage while the full panel is
			// CLOSED (the panel drives the stage when open), render one frame,
			// read the target back, and upload it as a cached named texture.
			if (!state.assetBrowser.ouiPreviewRequest.empty() &&
				!viewSettings.showGuiPreviewPanel && state.project.isLoaded())
			{
				const std::string ouiAbs = state.assetBrowser.ouiPreviewRequest;
				state.assetBrowser.ouiPreviewRequest.clear();
				std::error_code ouiEc;
				const long long ouiMtime = static_cast<long long>(
					std::filesystem::last_write_time(ouiAbs, ouiEc)
						.time_since_epoch().count());
				const std::string ouiKey = ouiAbs + "|" + std::to_string(ouiMtime);
				const std::string ouiRel =
					state.project.makeProjectRelative(ouiAbs);
				std::string ouiErr;
				if (guiPreviewStage.show(state.project.getRootDirectory(),
					ouiRel, ouiErr))
				{
					const std::filesystem::path ouiTmp =
						std::filesystem::temp_directory_path(ouiEc) /
						("orkige_ouithumb_" + std::to_string(
							std::hash<std::string>{}(ouiAbs)) + ".png");
					if (guiPreviewStage.renderAndCapture(ouiTmp.string(), ouiErr))
					{
						std::vector<unsigned char> ouiRgba;
						int ouiW = 0;
						int ouiH = 0;
						if (OrkigeEditor::decodeImageRgba(ouiTmp.string(),
							ouiRgba, ouiW, ouiH) && ouiW > 0 && ouiH > 0 && render &&
							render->createTexture2D("__ouiinspector", ouiRgba.data(),
								static_cast<unsigned int>(ouiW),
								static_cast<unsigned int>(ouiH)))
						{
							state.assetBrowser.ouiPreviewUpload = "__ouiinspector";
							state.assetBrowser.ouiPreviewKey = ouiKey;
						}
						std::filesystem::remove(ouiTmp, ouiEc);
					}
				}
				guiPreviewStage.clear();
			}

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
					// the Tile Palette is closed on a cold start (it auto-opens
					// only when the Scene enters 2D mode)
					!viewSettings.showTilePalettePanel &&
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
				runLuaConsoleInput(state, playSession);
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
				ImGuiWindow* sceneWindow = ImGui::FindWindowByName("Scene");
				ImGuiWindow* guiPreviewWindow =
					ImGui::FindWindowByName("GuiPreview");
				const bool previewDockOk = sceneWindow &&
					sceneWindow->DockId != 0 && guiPreviewWindow &&
					guiPreviewWindow->DockId == sceneWindow->DockId;
				SDL_Log("orkige_editor: selfcheck frame 30 - gameobjects=%zu "
					"(fixture cubes + test mesh %s), test_mesh.glb resource %s, "
					"imgui vertices=%d, scene RTT %dx%d (panel wants %dx%d), "
					"native menu items=%d (%s), previews docked=%s",
					gameObjectManager.getGameObjects().size(),
					objectsOk ? "present" : "MISSING",
					meshResourceOk ? "loaded" : "NOT LOADED", imguiVertices,
					sceneTarget.width, sceneTarget.height,
					state.scenePanelWidth, state.scenePanelHeight,
					nativeMenuItems, nativeMenuOk ? "ok" : "MISSING",
					previewDockOk ? "yes" : "NO");
				if (!objectsOk || !meshResourceOk || imguiVertices <= 0 ||
					!rttOk || !nativeMenuOk || !previewDockOk)
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
			if (frameCount == 90 && selfCheck)
			{
				// entering 2D editor mode auto-opens the Tile Palette
				viewSettings.editor2D = true;
			}
			if (frameCount == 100 && selfCheck)
			{
				// the palette opened AND docked into the bottom node beside the
				// Asset Browser (same dock node as Assets); also confirms Reset
				// Layout at frame 75 did NOT reopen it (it is default-closed)
				ImGuiWindow* paletteWindow =
					ImGui::FindWindowByName("Tile Palette###TilePalette");
				ImGuiWindow* assetsWindow =
					ImGui::FindWindowByName("Assets###Assets");
				const bool paletteAutoOpened =
					viewSettings.showTilePalettePanel && paletteWindow != nullptr;
				const bool paletteDockedWithAssets =
					paletteWindow && assetsWindow && assetsWindow->DockId != 0 &&
					paletteWindow->DockId == assetsWindow->DockId;
				SDL_Log("orkige_editor: selfcheck frame 100 - 2D toggle opened "
					"Tile Palette: %s, tabbed with Assets: %s",
					paletteAutoOpened ? "yes" : "NO",
					paletteDockedWithAssets ? "yes" : "NO");
				if (!paletteAutoOpened || !paletteDockedWithAssets)
				{
					SDL_Log("orkige_editor: FAILED selfcheck (tile palette 2D "
						"auto-open/dock)");
					exitCode = 2;
					running = false;
				}
			}
			// --- scripted Asset browser test (ORKIGE_EDITOR_ASSETTEST) ------
			if (assetTestEnv && frameCount == 10)
			{
				bool assetOk = true;
				std::string assetFail;
				std::error_code assetErr;
				// the project-relative path of the real png imported in
				// step (8), carried into the rename/move steps below
				std::string realImportRel;
				// the .oshape cooked from an SVG in step (3c),
				// thumbnailed in step (8)
				std::string cookedShapePath;
				// work on a temp COPY - the real project is never touched
				const std::string assetTempRoot =
					(std::filesystem::temp_directory_path() /
					("orkige_assettest_" + std::to_string(
						std::chrono::steady_clock::now()
							.time_since_epoch().count()))).string();
				std::filesystem::copy(assetTestEnv, assetTempRoot,
					std::filesystem::copy_options::recursive, assetErr);
				if (assetErr ||
					!openProjectFromPath(state, editorCore, assetTempRoot))
				{
					assetOk = false;
					assetFail = "could not prepare/open the temp copy at " +
						assetTempRoot;
				}
				// (1) enumerate: a known asset carries an id; a raw sidecar-less
				// file dropped into assets/ AFTER open shows dimmed (the DB never
				// indexed it)
				if (assetOk)
				{
					const std::string sidecarless =
						state.project.getAssetsDirectory() +
						"/loose_sidecarless.png";
					{
						std::ofstream f(sidecarless,
							std::ios::binary | std::ios::trunc);
						f << "not a real png";
					}
					bool sawKnownWithId = false;
					bool sawDimmed = false;
					for (AssetBrowserItem const& item :
						enumerateProjectAssets(state.project))
					{
						if (item.kind == AssetKind::Mesh && item.hasId)
						{
							sawKnownWithId = true;
						}
						if (item.relativePath.find("loose_sidecarless.png") !=
							std::string::npos)
						{
							sawDimmed = item.dimmed && !item.hasId;
						}
					}
					if (!sawKnownWithId)
					{
						assetOk = false;
						assetFail = "no id-carrying asset enumerated";
					}
					else if (!sawDimmed)
					{
						assetOk = false;
						assetFail = "sidecar-less asset not dimmed";
					}
				}
				// (2) generic import: a temp file lands in assets/ + gets a
				// sidecar + becomes enumerable with an id
				if (assetOk)
				{
					const std::string srcPng = (std::filesystem::path(
						assetTempRoot) / "import_src.png").string();
					{
						std::ofstream f(srcPng,
							std::ios::binary | std::ios::trunc);
						f << "fake png bytes";
					}
					const std::string dest = importAssetFile(state, srcPng);
					const bool sidecarMinted = !dest.empty() &&
						std::filesystem::exists(dest +
							Orkige::AssetDatabase::META_FILE_EXTENSION, assetErr);
					if (dest.empty() || !sidecarMinted)
					{
						assetOk = false;
						assetFail = "importAssetFile did not copy + mint a sidecar";
					}
					else
					{
						const std::string importedRel =
							state.project.makeProjectRelative(dest);
						bool enumerable = false;
						for (AssetBrowserItem const& item :
							enumerateProjectAssets(state.project))
						{
							if (item.relativePath == importedRel && item.hasId)
							{
								enumerable = true;
							}
						}
						if (!enumerable)
						{
							assetOk = false;
							assetFail = "imported file not enumerable with an id";
						}
					}
				}
				// (3) sprite via CreateSpriteObjectCommand: object +
				// SpriteComponent exist, undo removes it
				if (assetOk)
				{
					instantiateAssetIntoScene(state, editorCore,
						AssetKind::Texture,
						state.project.getAssetsDirectory() + "/import_src.png");
					const std::string spriteId = editorCore.getSelectedObjectId();
					optr<Orkige::GameObject> spriteObj =
						gameObjectManager.getGameObject(spriteId).lock();
					if (!spriteObj ||
						!spriteObj->hasComponent<Orkige::SpriteComponent>())
					{
						assetOk = false;
						assetFail = "sprite object/SpriteComponent missing";
					}
					else if (!editorCore.undo() ||
						gameObjectManager.objectExists(spriteId))
					{
						assetOk = false;
						assetFail = "undo did not remove the sprite object";
					}
				}
				// (3b) vector shape: an agent-authorable .oshape written straight
				// into assets/ (the write_project_file story), instantiated
				// through the drag path (CreateVectorShapeObjectCommand), then
				// its reflected properties exercised the way MCP/the Inspector do
				// (applyPropertyChange + getComponentPropertySchema) and undone.
				if (assetOk)
				{
					const std::string shapePath =
						state.project.getAssetsDirectory() + "/test_blob.oshape";
					{
						std::ofstream f(shapePath,
							std::ios::binary | std::ios::trunc);
						f << "version 1\n"
							<< "fill 0.9 0.4 0.4 1\n"
							<< "contour 4\n"
							<< "v -1 -1\nv 1 -1\nv 1 1\nv -1 1\n";
					}
					instantiateAssetIntoScene(state, editorCore,
						AssetKind::VectorShape, shapePath);
					const std::string shapeId = editorCore.getSelectedObjectId();
					optr<Orkige::GameObject> shapeObj =
						gameObjectManager.getGameObject(shapeId).lock();
					Orkige::VectorShapeComponent* shapeComp = shapeObj
						? shapeObj->getComponentPtr<Orkige::VectorShapeComponent>()
						: nullptr;
					if (!shapeComp)
					{
						assetOk = false;
						assetFail = "shape object/VectorShapeComponent missing";
					}
					else if (shapeComp->getTriangleCount() == 0)
					{
						assetOk = false;
						assetFail = "instantiated shape did not tessellate";
					}
					else
					{
						// the reflected surface MCP get/set_component relies on:
						// the schema lists the shape's props, and a reflected
						// write drives the component
						const Orkige::PropertySchema schema =
							editorCore.getComponentPropertySchema(shapeId,
								"VectorShapeComponent");
						const bool reflected = schema.find("shape") &&
							schema.find("tint") && schema.find("zOrder");
						editorCore.applyPropertyChange(shapeId,
							"VectorShapeComponent", "zOrder", "0", "7");
						if (!reflected)
						{
							assetOk = false;
							assetFail = "shape reflected schema missing properties";
						}
						else if (shapeComp->getZOrder() != 7)
						{
							assetOk = false;
							assetFail = "reflected zOrder write did not apply";
						}
						// undo the property change, then the create: proves both
						// the reflected write and the create are undoable steps
						else if (!editorCore.undo() || shapeComp->getZOrder() != 0)
						{
							assetOk = false;
							assetFail = "undo did not revert the reflected zOrder";
						}
						else if (!editorCore.undo() ||
							gameObjectManager.objectExists(shapeId))
						{
							assetOk = false;
							assetFail = "undo did not remove the shape object";
						}
					}
				}
				// (3c) SVG import cooks to .oshape: importAssetFile on an .svg runs
				// cook_shapes.py (subprocess) and lands the native .oshape (NOT the
				// .svg) in assets/ with a minted sidecar; it then instantiates +
				// tessellates like any shape
				if (assetOk)
				{
					const std::string svgSource = (std::filesystem::path(
						assetTempRoot) / "import_shape.svg").string();
					{
						std::ofstream f(svgSource,
							std::ios::binary | std::ios::trunc);
						f << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
							<< "viewBox=\"0 0 100 100\">"
							<< "<rect x=\"20\" y=\"20\" width=\"60\" "
							<< "height=\"60\" fill=\"#4488cc\"/></svg>";
					}
					std::string svgError;
					const std::string cooked =
						importAssetFile(state, svgSource, &svgError);
					const bool isOshape = !cooked.empty() &&
						std::filesystem::path(cooked).extension() == ".oshape";
					const bool sidecar = isOshape && std::filesystem::exists(
						cooked + Orkige::AssetDatabase::META_FILE_EXTENSION,
						assetErr);
					// the source .svg is NOT copied into assets/ (the .oshape IS
					// the asset, the .svg only the on-ramp)
					const bool svgKept = std::filesystem::exists(
						state.project.getAssetsDirectory() + "/import_shape.svg",
						assetErr);
					if (!isOshape || !sidecar)
					{
						assetOk = false;
						assetFail = "SVG import did not cook to a sidecar'd "
							".oshape: " + svgError;
					}
					else if (svgKept)
					{
						assetOk = false;
						assetFail = "SVG import kept the source .svg in assets/";
					}
					else
					{
						instantiateAssetIntoScene(state, editorCore,
							AssetKind::VectorShape, cooked);
						const std::string cookedId =
							editorCore.getSelectedObjectId();
						optr<Orkige::GameObject> cookedObj =
							gameObjectManager.getGameObject(cookedId).lock();
						Orkige::VectorShapeComponent* cookedComp = cookedObj
							? cookedObj->getComponentPtr<
								Orkige::VectorShapeComponent>()
							: nullptr;
						if (!cookedComp || cookedComp->getTriangleCount() == 0)
						{
							assetOk = false;
							assetFail = "cooked .oshape did not tessellate";
						}
						else
						{
							cookedShapePath = cooked;
							editorCore.undo(); // remove the instantiated object
						}
					}
				}
				// (4) prefab via the drag path: make a prefab from an object,
				// then instantiate it through instantiateAssetIntoScene (the
				// exact code path a Scene/Hierarchy drop runs)
				if (assetOk)
				{
					editorCore.instantiateSpriteObject("PrefabSource",
						"import_src.png", Orkige::Vec3::ZERO);
					editorCore.selectObject("PrefabSource");
					if (!createPrefabFromSelection(state, editorCore))
					{
						assetOk = false;
						assetFail = "createPrefabFromSelection failed";
					}
					else
					{
						const std::string prefabPath =
							state.project.getAssetsDirectory() +
							"/PrefabSource.oprefab";
						instantiateAssetIntoScene(state, editorCore,
							AssetKind::Prefab, prefabPath);
						const std::string instId =
							editorCore.getSelectedObjectId();
						optr<Orkige::GameObject> inst =
							gameObjectManager.getGameObject(instId).lock();
						if (!inst || inst->getPrefabRef().empty())
						{
							assetOk = false;
							assetFail = "prefab instance not created via drag path";
						}
						else if (!editorCore.undo() ||
							gameObjectManager.objectExists(instId))
						{
							assetOk = false;
							assetFail = "undo did not remove the prefab instance";
						}
					}
				}
				// (5) folder browsing (v2): the project root enumerates its
				// standard subfolders; descending into assets/ lists the
				// imported file; ".." from assets/ lands back on the root
				if (assetOk)
				{
					const std::string projectRoot =
						state.project.getRootDirectory();
					bool sawAssetsDir = false;
					for (std::string const& sub : enumerateAssetFolder(
						state.project, projectRoot).subfolders)
					{
						if (std::filesystem::path(sub).filename() == "assets")
						{
							sawAssetsDir = true;
						}
					}
					bool sawImportedInAssets = false;
					for (AssetBrowserItem const& fileItem : enumerateAssetFolder(
						state.project,
						state.project.getAssetsDirectory()).files)
					{
						if (fileItem.relativePath.find("import_src.png") !=
							std::string::npos)
						{
							sawImportedInAssets = true;
						}
					}
					const std::string upFromAssets = std::filesystem::path(
						state.project.getAssetsDirectory())
							.parent_path().string();
					const bool upIsRoot = std::filesystem::equivalent(
						upFromAssets, projectRoot, assetErr);
					if (!sawAssetsDir)
					{
						assetOk = false;
						assetFail = "assets/ not enumerated as a subfolder";
					}
					else if (!sawImportedInAssets)
					{
						assetOk = false;
						assetFail = "imported file not in the assets/ folder listing";
					}
					else if (!upIsRoot)
					{
						assetOk = false;
						assetFail = "'..' from assets/ does not reach the root";
					}
				}
				// (6) New Folder: created + enumerates under assets/
				if (assetOk)
				{
					const std::string newDir = createFolderInDir(
						state.project.getAssetsDirectory(), "NewGroup");
					bool enumerated = false;
					for (std::string const& sub : enumerateAssetFolder(
						state.project,
						state.project.getAssetsDirectory()).subfolders)
					{
						if (std::filesystem::path(sub).filename() == "NewGroup")
						{
							enumerated = true;
						}
					}
					if (newDir.empty() || !enumerated)
					{
						assetOk = false;
						assetFail = "New Folder not created/enumerated";
					}
				}
				// (7) New Script: file written + sidecar minted + enumerates
				// under scripts/ with a stable id
				if (assetOk)
				{
					const std::string scriptPath = createScriptInDir(state,
						state.project.getScriptsDirectory(), "probe");
					const bool sidecarMinted = !scriptPath.empty() &&
						std::filesystem::exists(scriptPath +
							Orkige::AssetDatabase::META_FILE_EXTENSION, assetErr);
					bool enumeratedWithId = false;
					for (AssetBrowserItem const& fileItem : enumerateAssetFolder(
						state.project,
						state.project.getScriptsDirectory()).files)
					{
						if (fileItem.relativePath.find("probe.lua") !=
							std::string::npos && fileItem.hasId)
						{
							enumeratedWithId = true;
						}
					}
					if (scriptPath.empty() || !sidecarMinted || !enumeratedWithId)
					{
						assetOk = false;
						assetFail = "New Script not written/sidecar/enumerated";
					}
				}
				// (8) thumbnails: a REAL image loads into a valid ImGui texture
				// handle; a non-image fails gracefully (0 handle, no crash).
				// The real png comes from the repo (a neutral source outside
				// the temp copy, resolved from the ASSETTEST project path).
				if (assetOk)
				{
					const std::string sourceRoot = std::filesystem::path(
						assetTestEnv).parent_path().parent_path().string();
					const std::string realPngSource =
						(std::filesystem::path(sourceRoot) / "projects" /
							"roller" / "assets" / "ball.png").string();
					std::string thumbError;
					const std::string realPngDest =
						importAssetFile(state, realPngSource, &thumbError);
					const ImTextureID realThumb = realPngDest.empty() ? 0 :
						assetThumbnailFor(state, realPngDest);
					// the fake png from step (2) must NOT crash and yields no id
					const ImTextureID fakeThumb = assetThumbnailFor(state,
						state.project.getAssetsDirectory() + "/import_src.png");
					(void)fakeThumb;
					if (realPngDest.empty())
					{
						assetOk = false;
						assetFail = "could not import the real png: " + thumbError;
					}
					else if (realThumb == 0)
					{
						assetOk = false;
						assetFail = "no thumbnail handle for a real png";
					}
					else
					{
						realImportRel =
							state.project.makeProjectRelative(realPngDest);
						// a .oshape gets a REAL rasterized thumbnail too (not a
						// glyph): CPU tessellate + raster + upload -> a bindable
						// handle
						const ImTextureID shapeThumb = cookedShapePath.empty()
							? 0 : assetThumbnailFor(state, cookedShapePath);
						if (shapeThumb == 0)
						{
							assetOk = false;
							assetFail = "no thumbnail handle for a .oshape";
						}
					}
				}
				// (9) openWithDefaultApp resolver: the composed file:// URL is
				// correct (asserted WITHOUT spawning a GUI app)
				if (assetOk)
				{
					const std::string url =
						fileUrlForPath("/tmp/orkige asset.lua");
					if (url != "file:///tmp/orkige%20asset.lua")
					{
						assetOk = false;
						assetFail = "fileUrlForPath composed '" + url + "'";
					}
				}
				// (10) searchAssets: a recursive search from the root finds a
				// file created in assets/NewGroup/; a non-recursive search from
				// the root does not; the kind mask filters it in/out; the result
				// row's relativePath parent is the containing folder
				if (assetOk)
				{
					const std::string rootDir = state.project.getRootDirectory();
					const std::string groupDir = (std::filesystem::path(
						state.project.getAssetsDirectory()) / "NewGroup").string();
					std::filesystem::create_directories(groupDir, assetErr);
					const std::string nested =
						(std::filesystem::path(groupDir) / "search_probe.png")
							.string();
					{
						std::ofstream f(nested,
							std::ios::binary | std::ios::trunc);
						f << "probe";
					}
					const auto containsProbe = [](
						std::vector<AssetBrowserItem> const& hits,
						std::string* containing)
					{
						for (AssetBrowserItem const& hit : hits)
						{
							if (hit.relativePath.find("search_probe.png") !=
								std::string::npos)
							{
								if (containing)
								{
									*containing = std::filesystem::path(
										hit.relativePath).parent_path().string();
								}
								return true;
							}
						}
						return false;
					};
					std::string containing;
					const bool foundRecursive = containsProbe(searchAssets(
						state.project, rootDir, "search_probe", true, 0),
						&containing);
					const bool foundFlat = containsProbe(searchAssets(
						state.project, rootDir, "search_probe", false, 0),
						nullptr);
					const unsigned int texBit =
						1u << static_cast<unsigned int>(AssetKind::Texture);
					const unsigned int meshBit =
						1u << static_cast<unsigned int>(AssetKind::Mesh);
					const bool inTexMask = containsProbe(searchAssets(
						state.project, rootDir, "search_probe", true, texBit),
						nullptr);
					const bool inMeshMask = containsProbe(searchAssets(
						state.project, rootDir, "search_probe", true, meshBit),
						nullptr);
					const bool folderOk =
						containing.find("NewGroup") != std::string::npos;
					if (!foundRecursive || foundFlat || !inTexMask ||
						inMeshMask || !folderOk)
					{
						assetOk = false;
						assetFail = "searchAssets recursion/mask/containing-folder"
							" wrong";
					}
				}
				// (11) navigateTo + back/forward: root -> assets -> NewGroup,
				// back twice lands on root, forward once lands on assets, a
				// fresh navigateTo clears the forward history
				if (assetOk)
				{
					AssetBrowserState nav;
					const std::string rootDir = state.project.getRootDirectory();
					const std::string assetsDir =
						state.project.getAssetsDirectory();
					const std::string groupDir = (std::filesystem::path(
						assetsDir) / "NewGroup").string();
					nav.currentDir = rootDir;
					navigateTo(nav, assetsDir);
					navigateTo(nav, groupDir);
					navigateBack(nav);
					const bool atAssets = std::filesystem::equivalent(
						nav.currentDir, assetsDir, assetErr);
					navigateBack(nav);
					const bool atRoot = std::filesystem::equivalent(
						nav.currentDir, rootDir, assetErr);
					navigateForward(nav);
					const bool forwardAssets = std::filesystem::equivalent(
						nav.currentDir, assetsDir, assetErr);
					navigateTo(nav, groupDir);
					const bool forwardCleared = nav.forwardHistory.empty();
					if (!atAssets || !atRoot || !forwardAssets ||
						!forwardCleared)
					{
						assetOk = false;
						assetFail = "navigateTo/back/forward history wrong";
					}
				}
				// (12) rename: an id-tracked asset keeps its id across a rename
				if (assetOk && !realImportRel.empty())
				{
					Orkige::AssetDatabase* db =
						state.project.getAssetDatabase().get();
					const Orkige::String preId = db->idForPath(realImportRel);
					AssetBrowserItem toRename;
					bool found = false;
					for (AssetBrowserItem const& fileItem : enumerateAssetFolder(
						state.project,
						state.project.getAssetsDirectory()).files)
					{
						if (fileItem.relativePath == realImportRel)
						{
							toRename = fileItem;
							found = true;
						}
					}
					const std::string oldAbs = toRename.absolutePath;
					const bool renamed = found &&
						renameAssetEntry(state, toRename, "renamed_ball");
					const std::string renamedMeta =
						(std::filesystem::path(state.project.getAssetsDirectory()) /
							"renamed_ball.png").string() +
						Orkige::AssetDatabase::META_FILE_EXTENSION;
					const std::string newRel = state.project.makeProjectRelative(
						(std::filesystem::path(state.project.getAssetsDirectory()) /
							"renamed_ball.png").string());
					const Orkige::String postId = db->idForPath(newRel);
					if (!renamed || preId.empty() || postId != preId)
					{
						assetOk = false;
						assetFail = "renameAssetEntry did not preserve the id";
					}
					else if (std::filesystem::exists(oldAbs, assetErr) ||
						!std::filesystem::exists(renamedMeta, assetErr))
					{
						assetOk = false;
						assetFail = "rename left the old file or lost the sidecar";
					}
					else
					{
						realImportRel = newRel;	// carry into the move step
					}
				}
				// (13) move into a subfolder: the id survives and the texture's
				// bare name still resolves to it through the database, so a scene
				// reference (bare name + assetId) keeps pointing at the moved asset
				// across a save + reload round-trip
				if (assetOk)
				{
					const std::string newGroup =
						(std::filesystem::path(state.project.getAssetsDirectory()) /
							"NewGroup").string();
					std::filesystem::create_directories(newGroup, assetErr);
					Orkige::AssetDatabase* db =
						state.project.getAssetDatabase().get();
					const std::string bareName =
						std::filesystem::path(realImportRel).filename().string();
					const Orkige::String preId = db->idForPath(realImportRel);
					const std::string moveAbs =
						(std::filesystem::path(state.project.getRootDirectory()) /
							realImportRel).string();
					const int moved =
						moveAssetsIntoFolder(state, { moveAbs }, newGroup);
					const std::string movedRel = state.project.makeProjectRelative(
						(std::filesystem::path(newGroup) / bareName).string());
					const Orkige::String postId = db->idForPath(movedRel);
					// the bare name still maps to the same id - this is the scene
					// reference resolution guarantee (a Sprite serializes the bare
					// name + assetId; the id heals the reference on load)
					const Orkige::String byName = db->idForFileName(bareName);
					// the rendering-level proof: loadSprite keeps the texture
					// name ONLY when the quad actually LOADED (a failed load
					// returns early, leaving the name empty). An equal name
					// means the moved subfolder file resolved by BARE name -
					// exactly what a recursive registration fails to give on
					// the next backend (which indexes subfolder files by
					// sub-path). Checked right after the move AND after a
					// scene round-trip.
					const auto spriteTextureName = [&](std::string const& id)
					{
						optr<Orkige::GameObject> obj =
							gameObjectManager.getGameObject(id).lock();
						if (!obj ||
							!obj->hasComponent<Orkige::SpriteComponent>())
						{
							return std::string();
						}
						return std::string(obj
							->getComponentPtr<Orkige::SpriteComponent>()
							->getTextureName());
					};
					editorCore.instantiateSpriteObject("MovedRefSprite", bareName,
						Orkige::Vec3::ZERO);
					const bool loadedFresh =
						spriteTextureName("MovedRefSprite") == bareName;
					const std::string roundtripScene =
						(std::filesystem::path(state.project.getScenesDirectory()) /
							"moved_ref.oscene").string();
					const bool saved =
						saveSceneToPath(state, editorCore, roundtripScene);
					const bool reopened = saved &&
						openSceneFromPath(state, editorCore, roundtripScene);
					const bool loadedReloaded = reopened &&
						spriteTextureName("MovedRefSprite") == bareName;
					if (moved != 1 || preId.empty() || postId != preId ||
						byName != preId)
					{
						assetOk = false;
						assetFail = "moveAssetsIntoFolder did not keep the id / "
							"bare-name resolution";
					}
					else if (!loadedFresh)
					{
						assetOk = false;
						assetFail = "moved-subfolder texture did not load by "
							"bare name after the move";
					}
					else if (!loadedReloaded)
					{
						assetOk = false;
						assetFail = "moved-subfolder texture did not load by "
							"bare name after the scene round-trip";
					}
					else
					{
						realImportRel = movedRel;
					}
				}
				// (14) duplicate: an id-tracked asset yields a new enumerable
				// id-carrying copy
				if (assetOk)
				{
					const std::string srcAbs =
						state.project.getAssetsDirectory() + "/import_src.png";
					const int duplicated =
						duplicateAssetEntries(state, { srcAbs });
					bool sawCopyWithId = false;
					for (AssetBrowserItem const& fileItem : enumerateAssetFolder(
						state.project,
						state.project.getAssetsDirectory()).files)
					{
						if (fileItem.relativePath.find("import_src Copy") !=
							std::string::npos && fileItem.hasId)
						{
							sawCopyWithId = true;
						}
					}
					if (duplicated != 1 || !sawCopyWithId)
					{
						assetOk = false;
						assetFail = "duplicateAssetEntries did not create an "
							"id-carrying copy";
					}
				}
				// (15) delete: the file + sidecar go and the id map is pruned
				if (assetOk)
				{
					const std::string delAbs =
						state.project.getAssetsDirectory() + "/import_src.png";
					const std::string delRel =
						state.project.makeProjectRelative(delAbs);
					const int deleted = deleteAssetEntries(state, { delAbs });
					const bool fileGone =
						!std::filesystem::exists(delAbs, assetErr);
					const bool sidecarGone = !std::filesystem::exists(delAbs +
						Orkige::AssetDatabase::META_FILE_EXTENSION, assetErr);
					const bool idPruned = state.project.getAssetDatabase()
						->idForPath(delRel).empty();
					if (deleted < 1 || !fileGone || !sidecarGone || !idPruned)
					{
						assetOk = false;
						assetFail = "deleteAssetEntries left the file/sidecar/id";
					}
				}
				// (16) selection survival: two relativePaths survive a re-
				// enumerate + prune; deleting one on disk drops exactly it
				if (assetOk)
				{
					const AssetFolderListing before = enumerateAssetFolder(
						state.project, state.project.getAssetsDirectory());
					if (before.files.size() < 2)
					{
						assetOk = false;
						assetFail = "need two files for the selection test";
					}
					else
					{
						const std::string relA = before.files[0].relativePath;
						const std::string relB = before.files[1].relativePath;
						const std::string absA = before.files[0].absolutePath;
						state.assetBrowser.selection.clear();
						state.assetBrowser.selection.insert(relA);
						state.assetBrowser.selection.insert(relB);
						std::set<std::string> present;
						for (AssetBrowserItem const& f : before.files)
						{
							present.insert(f.relativePath);
						}
						pruneAssetSelection(state.assetBrowser, present);
						const bool bothSurvive =
							state.assetBrowser.selection.count(relA) > 0 &&
							state.assetBrowser.selection.count(relB) > 0;
						// delete one on disk, re-enumerate, prune again
						std::filesystem::remove(absA, assetErr);
						std::filesystem::remove(absA +
							Orkige::AssetDatabase::META_FILE_EXTENSION, assetErr);
						const AssetFolderListing after = enumerateAssetFolder(
							state.project, state.project.getAssetsDirectory());
						std::set<std::string> presentAfter;
						for (AssetBrowserItem const& f : after.files)
						{
							presentAfter.insert(f.relativePath);
						}
						pruneAssetSelection(state.assetBrowser, presentAfter);
						const bool droppedA =
							state.assetBrowser.selection.count(relA) == 0;
						const bool keptB =
							state.assetBrowser.selection.count(relB) > 0;
						state.assetBrowser.selection.clear();
						if (!bothSurvive || !droppedA || !keptB)
						{
							assetOk = false;
							assetFail = "selection prune survival wrong";
						}
					}
				}
				// (17) texture import settings round-trip through the inspector
				// seam: select a single id-tracked texture, edit its settings and
				// Apply, then read the sidecar back and confirm every field landed
				// (the base block AND a per-platform override)
				if (assetOk)
				{
					const std::string probePng =
						state.project.getAssetsDirectory() + "/settings_probe.png";
					{
						std::ofstream f(probePng,
							std::ios::binary | std::ios::trunc);
						f << "px";
					}
					Orkige::AssetDatabase* db =
						state.project.getAssetDatabase().get();
					const Orkige::String probeId = db->importAsset(probePng);
					const std::string probeRel =
						state.project.makeProjectRelative(probePng);
					state.assetBrowser.selection.clear();
					state.assetBrowser.selection.insert(probeRel);
					state.assetBrowser.editImportPath.clear();
					Orkige::TextureImport edit;
					edit.base.filter = "point";
					edit.base.wrap = "wrap";
					edit.base.maxSize = 512;
					edit.base.premultiply = true;
					edit.hasAndroid = true;
					edit.android = edit.base;
					edit.android.maxSize = 256;
					const bool applied =
						applyTextureImportEdit(state, edit);
					const std::string metaPath = db->metaFilePathForId(probeId);
					Orkige::TextureImport readBack;
					const bool readOk = Orkige::AssetDatabase::readImportSettings(
						metaPath, readBack);
					// the id must survive the sidecar rewrite (still resolves)
					const bool idKept =
						db->idForPath(probeRel) == probeId && !probeId.empty();
					if (!applied || !readOk || !idKept ||
						readBack.base.filter != "point" ||
						readBack.base.wrap != "wrap" ||
						readBack.base.maxSize != 512 ||
						!readBack.base.premultiply ||
						!readBack.hasAndroid ||
						readBack.android.maxSize != 256)
					{
						assetOk = false;
						assetFail = "texture import settings did not round-trip "
							"through applyTextureImportEdit";
					}
					state.assetBrowser.selection.clear();
					state.assetBrowser.editImportPath.clear();
				}
				// (18) multi-drop as ONE undo step: instantiating a batch of
				// assets creates every object, and a single undo removes them all
				// (the CompositeCommand the ORKIGE_ASSET_MULTI drop path builds)
				if (assetOk)
				{
					const std::string multiA =
						state.project.getAssetsDirectory() + "/multi_a.png";
					const std::string multiB =
						state.project.getAssetsDirectory() + "/multi_b.png";
					for (std::string const& p : { multiA, multiB })
					{
						std::ofstream f(p, std::ios::binary | std::ios::trunc);
						f << "px";
					}
					instantiateAssetsIntoScene(state, editorCore,
						{ multiA, multiB });
					const bool bothCreated =
						gameObjectManager.objectExists("multi_a") &&
						gameObjectManager.objectExists("multi_b");
					const bool undoneOnce = editorCore.undo();
					const bool bothGone =
						!gameObjectManager.objectExists("multi_a") &&
						!gameObjectManager.objectExists("multi_b");
					if (!bothCreated || !undoneOnce || !bothGone)
					{
						assetOk = false;
						assetFail = "multi-drop did not create/undo as one step";
					}
				}
				// (19) searchAssets returns matching FOLDERS, not only files -
				// the interactive symptom was "in the tree we only show files
				// not folders": any search term switched the content pane into a
				// files-only listing. A search that names a folder must surface
				// that folder (isFolder set), and a type filter must NOT gate it
				// out (folders carry no asset kind - they stay navigable).
				if (assetOk)
				{
					const std::string zebra = (std::filesystem::path(
						state.project.getAssetsDirectory()) / "ZebraDir")
						.string();
					std::filesystem::create_directories(zebra, assetErr);
					const auto hasFolderHit = [](
						std::vector<AssetBrowserItem> const& hits)
					{
						for (AssetBrowserItem const& hit : hits)
						{
							if (hit.isFolder && std::filesystem::path(
								hit.absolutePath).filename() == "ZebraDir")
							{
								return true;
							}
						}
						return false;
					};
					const std::string rootDir =
						state.project.getRootDirectory();
					const bool folderInSearch = hasFolderHit(searchAssets(
						state.project, rootDir, "ZebraDir", true, 0));
					const unsigned int texBit = 1u <<
						static_cast<unsigned int>(AssetKind::Texture);
					const bool folderSurvivesFilter = hasFolderHit(searchAssets(
						state.project, rootDir, "ZebraDir", true, texBit));
					if (!folderInSearch || !folderSurvivesFilter)
					{
						assetOk = false;
						assetFail = "searchAssets hid matching folders";
					}
				}
				// (20) createFolderAndReveal reveals in place + clears any active
				// search/filter - the interactive symptom was "creating folders
				// does not seem to work": New Folder navigated INTO the new empty
				// folder (blank pane), and a stale search/filter would hide the
				// fresh item. The fix keeps the current folder, drops the filter,
				// selects the new item and opens its inline rename.
				if (assetOk)
				{
					AssetBrowserState& browser = state.assetBrowser;
					const std::string assetsDir =
						state.project.getAssetsDirectory();
					browser.currentDir = assetsDir;
					browser.backHistory.clear();
					browser.forwardHistory.clear();
					SDL_strlcpy(browser.searchText, "zzz",
						sizeof(browser.searchText));	// a stale, non-matching search
					browser.kindFilterMask = 1u <<
						static_cast<unsigned int>(AssetKind::Texture);
					browser.selection.clear();
					browser.renamingPath.clear();
					const std::string created = createFolderAndReveal(state);
					const std::string createdRel =
						state.project.makeProjectRelative(created);
					const bool stayedPut = std::filesystem::equivalent(
						browser.currentDir, assetsDir, assetErr);
					const bool filterCleared = browser.searchText[0] == '\0' &&
						browser.kindFilterMask == 0;
					const bool revealed =
						browser.selection.count(createdRel) == 1 &&
						browser.renamingPath == createdRel;
					if (created.empty() || !stayedPut || !filterCleared ||
						!revealed ||
						!std::filesystem::is_directory(created, assetErr))
					{
						assetOk = false;
						assetFail = "createFolderAndReveal did not reveal in place";
					}
				}
				// (12) .glb + .omat get a GPU-rendered preview thumbnail baked
				// through the deferred baker (staged + read back across frames).
				// Drive the queue synchronously here (this block runs after the
				// frame's renderOneFrame, so extra renders are safe) and assert
				// both land a REAL owned upload - a bindable handle plus an owned
				// texture name, i.e. not the glyph placeholder (id 0, no upload).
				if (assetOk)
				{
					const std::string glbAbs =
						state.project.getAssetsDirectory() + "/test_mesh.glb";
					const std::string omatAbs =
						state.project.getAssetsDirectory() + "/thumb_probe.omat";
					{
						std::ofstream f(omatAbs,
							std::ios::binary | std::ios::trunc);
						f << "version 1\nalbedo 0.85 0.30 0.25 1.0\n"
							"metalness 0.10\nroughness 0.50\n"
							"emissive 0.0 0.0 0.0\n";
					}
					// request both (routes them into the bake queue, returns 0)
					assetThumbnailFor(state, glbAbs);
					assetThumbnailFor(state, omatAbs);
					// drain the queue: each asset stages, settles a couple of
					// frames, then is captured (one per pass)
					for (int pump = 0; pump < 60 &&
						(!state.assetBrowser.thumbBakeQueue.empty() ||
							!state.assetBrowser.thumbBakeInFlight.empty()); ++pump)
					{
						host.getEngine().renderOneFrame();
						serviceThumbnailBakes(state, thumbnailBaker,
							frameCount + pump + 1);
					}
					const auto bakedRealThumbnail =
						[&](std::string const& abs) -> bool
					{
						auto it = state.assetBrowser.thumbnails.find(abs);
						return it != state.assetBrowser.thumbnails.end() &&
							it->second.textureId != 0 &&
							!it->second.uploadName.empty();
					};
					if (!bakedRealThumbnail(glbAbs))
					{
						assetOk = false;
						assetFail = "no baked thumbnail for a .glb model";
					}
					else if (!bakedRealThumbnail(omatAbs))
					{
						assetOk = false;
						assetFail = "no baked thumbnail for a .omat material";
					}
				}
				std::filesystem::remove_all(assetTempRoot, assetErr);
				SDL_Log("orkige_editor: asset browser test - %s%s%s",
					assetOk ? "OK" : "FAILED", assetOk ? "" : ": ",
					assetOk ? "" : assetFail.c_str());
				if (!assetOk)
				{
					exitCode = 9;
				}
				running = false;
			}
			// --- level-paint state-seam test (ORKIGE_EDITOR_LEVELPAINT) ---
			if (levelPaintEnv)
			{
				const std::chrono::steady_clock::time_point lpNow =
					std::chrono::steady_clock::now();
				bool lpFailed = false;
				std::string lpFail;
				const auto cellRoot = [&](float x, float y)
				{
					return editorCore.findTileAtCell(x, y, 6.0f);
				};
				if (levelPaintPhase == LevelPaintPhase::Idle && frameCount == 10)
				{
					std::error_code lpErr;
					levelPaintTempRoot = (std::filesystem::temp_directory_path() /
						("orkige_levelpaint_" + std::to_string(
							std::chrono::steady_clock::now()
								.time_since_epoch().count()))).string();
					std::filesystem::copy(levelPaintEnv, levelPaintTempRoot,
						std::filesystem::copy_options::recursive, lpErr);
					if (lpErr ||
						!openProjectFromPath(state, editorCore, levelPaintTempRoot))
					{
						lpFailed = true;
						lpFail = "could not prepare/open the temp copy";
					}
					// a fresh scene carrying a Level object = the paint grid source
					if (!lpFailed)
					{
						newScene(state, editorCore);
						gameObjectManager.createGameObject("Level");
						editorCore.addComponentToObject("Level", "LevelComponent");
						editorCore.setObjectProperty("Level", "LevelComponent",
							"cols", "3");
						editorCore.setObjectProperty("Level", "LevelComponent",
							"rows", "2");
						editorCore.setObjectProperty("Level", "LevelComponent",
							"tileSize", "6");
						editorCore.setObjectProperty("Level", "LevelComponent",
							"originX", "-3");
						editorCore.setObjectProperty("Level", "LevelComponent",
							"originY", "-3");
						editorCore.clearHistory();
						const Orkige::EditorPaintGrid grid =
							editorCore.resolvePaintGrid();
						if (std::fabs(grid.originX + 3.0f) > 1e-3f ||
							std::fabs(grid.originY + 3.0f) > 1e-3f ||
							std::fabs(grid.cellSize - 6.0f) > 1e-3f)
						{
							lpFailed = true;
							lpFail = "resolvePaintGrid did not come from the Level";
						}
					}
					// GATE: with nothing armed the palette must not have armed the
					// Paint tool, and no cell may be painted - painting only
					// happens once a palette tile is selected (mode gating)
					if (!lpFailed)
					{
						if (!state.tilePalette.armedAssetPath.empty() ||
							editorCore.getActiveTool() ==
								Orkige::EditorTool::Paint)
						{
							lpFailed = true;
							lpFail = "palette should start disarmed with the Paint "
								"tool inactive";
						}
						else if (!cellRoot(-3.0f, -3.0f).empty())
						{
							lpFailed = true;
							lpFail = "a cell was painted with nothing selected";
						}
					}
					// arm the tile prefab and probe it
					if (!lpFailed)
					{
						const std::string tilePrefab =
							(std::filesystem::path(levelPaintTempRoot) / "assets" /
								"tile.oprefab").string();
						if (!paletteArmAsset(state, editorCore, tilePrefab))
						{
							lpFailed = true;
							lpFail = "paletteArmAsset failed";
						}
						else if (editorCore.getActiveTool() !=
								Orkige::EditorTool::Paint ||
							!state.tilePalette.hasEdgeWalls ||
							state.tilePalette.rootHasTileComponent)
						{
							lpFailed = true;
							lpFail = "prefab probe wrong (tool/edgeWalls/rootTile)";
						}
					}
					// palette THUMBNAILS: a prefab (its probed primary visual), a
					// texture and a .oshape each resolve a REAL cached thumbnail (not
					// the generic-tile 0). Drop a known-good shape in so all three
					// paintable kinds exist in the temp project.
					if (!lpFailed)
					{
						const std::string shapeSrc =
							(std::filesystem::path(levelPaintEnv).parent_path() /
								"benchmark" / "assets" / "blob.oshape").string();
						const std::string shapeDst =
							state.project.getAssetsDirectory() + "/probe.oshape";
						std::error_code shpErr;
						std::filesystem::copy_file(shapeSrc, shapeDst,
							std::filesystem::copy_options::overwrite_existing, shpErr);
						const unsigned int pMask =
							(1u << static_cast<unsigned>(AssetKind::Prefab)) |
							(1u << static_cast<unsigned>(AssetKind::Texture)) |
							(1u << static_cast<unsigned>(AssetKind::VectorShape));
						ImTextureID prefabThumb = 0;
						ImTextureID texThumb = 0;
						ImTextureID shapeThumb = 0;
						for (AssetBrowserItem const& pit : searchAssets(
							state.project, state.project.getAssetsDirectory(), "",
							true, pMask))
						{
							if (pit.kind == AssetKind::Prefab && !prefabThumb)
								prefabThumb = paletteTileThumbnail(state, pit);
							else if (pit.kind == AssetKind::Texture && !texThumb)
								texThumb = paletteTileThumbnail(state, pit);
							else if (pit.kind == AssetKind::VectorShape && !shapeThumb)
								shapeThumb = paletteTileThumbnail(state, pit);
						}
						SDL_Log("orkige_editor: level-paint - palette thumbs "
							"prefab=%s texture=%s shape=%s",
							prefabThumb ? "ok" : "MISSING", texThumb ? "ok" : "MISSING",
							shapeThumb ? "ok" : "MISSING");
						if (!prefabThumb || !texThumb || !shapeThumb)
						{
							lpFailed = true;
							lpFail = "palette thumbnail missing for a prefab / texture / "
								"shape";
						}
					}
					// paint two cells in ONE stroke (right edge open, tagged)
					if (!lpFailed)
					{
						state.tilePalette.edgeOpen[3] = true;	// right
						SDL_strlcpy(state.tilePalette.paintTags, "tile",
							sizeof(state.tilePalette.paintTags));
						const std::size_t undoBefore =
							editorCore.getUndoStackSize();
						const unsigned int stroke = editorCore.beginMergeSession();
						editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							-3.0f, -3.0f, 6.0f, stroke);
						editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							3.0f, -3.0f, 6.0f, stroke);
						const std::string r0 = cellRoot(-3.0f, -3.0f);
						const std::string r1 = cellRoot(3.0f, -3.0f);
						if (r0.empty() || r1.empty() || r0 == r1)
						{
							lpFailed = true;
							lpFail = "two painted cells not found";
						}
						else if (editorCore.getUndoStackSize() != undoBefore + 1)
						{
							lpFailed = true;
							lpFail = "stroke did not collapse into one undo step";
						}
						else
						{
							optr<Orkige::GameObject> root =
								gameObjectManager.getGameObject(r0).lock();
							Orkige::StringVector supp = root
								? root->getSuppressedPrefabChildren()
								: Orkige::StringVector();
							std::string edges;
							editorCore.getObjectProperty(r0, "TileComponent",
								"openEdges", edges);
							Orkige::StringVector tags;
							editorCore.getObjectTags(r0, tags);
							const bool ok = root &&
								root->getPrefabRef() == "assets/tile.oprefab" &&
								!gameObjectManager.objectExists(r0 + "/WallRight") &&
								gameObjectManager.objectExists(r0 + "/WallTop") &&
								supp.size() == 1 && supp[0] == "WallRight" &&
								edges == "8" &&
								std::find(tags.begin(), tags.end(), "tile") !=
									tags.end();
							if (!ok)
							{
								lpFailed = true;
								lpFail = "painted root structural asserts wrong";
							}
						}
					}
					// undo/redo the whole stroke as one step
					if (!lpFailed)
					{
						editorCore.undo();
						const bool bothGone = cellRoot(-3.0f, -3.0f).empty() &&
							cellRoot(3.0f, -3.0f).empty();
						editorCore.redo();
						const bool bothBack = !cellRoot(-3.0f, -3.0f).empty() &&
							!cellRoot(3.0f, -3.0f).empty();
						if (!bothGone || !bothBack)
						{
							lpFailed = true;
							lpFail = "stroke undo/redo did not restore both cells";
						}
					}
					// replace a cell (left open now) + identical repaint is a no-op
					if (!lpFailed)
					{
						state.tilePalette.edgeOpen[3] = false;	// right
						state.tilePalette.edgeOpen[2] = true;	// left
						editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							-3.0f, -3.0f, 6.0f, 0);
						const std::string rNew = cellRoot(-3.0f, -3.0f);
						optr<Orkige::GameObject> root =
							gameObjectManager.getGameObject(rNew).lock();
						Orkige::StringVector supp = root
							? root->getSuppressedPrefabChildren()
							: Orkige::StringVector();
						const bool replaced =
							supp.size() == 1 && supp[0] == "WallLeft";
						const bool noop = !editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							-3.0f, -3.0f, 6.0f, 0);
						if (!replaced || !noop)
						{
							lpFailed = true;
							lpFail = "replace / no-op-repaint wrong";
						}
					}
					// erase a cell + undo restores it with its paint data
					if (!lpFailed)
					{
						const bool erased =
							editorCore.eraseTileAtCell(3.0f, -3.0f, 6.0f, 0);
						const bool gone = cellRoot(3.0f, -3.0f).empty();
						editorCore.undo();
						const std::string back = cellRoot(3.0f, -3.0f);
						std::string edges;
						editorCore.getObjectProperty(back, "TileComponent",
							"openEdges", edges);
						if (!erased || !gone || back.empty() || edges != "8")
						{
							lpFailed = true;
							lpFail = "erase / undo did not round-trip the tile";
						}
					}
					// BARE-ASSET tiles: paint a texture straight into a free cell -
					// a grid-cell sprite object + a TileComponent stamping the
					// source id, NO prefab file. wall.png ships in roller and is
					// id-tracked; its own thumbnail is the ghost preview.
					if (!lpFailed)
					{
						const std::string wallTex =
							(std::filesystem::path(levelPaintTempRoot) / "assets" /
								"wall.png").string();
						if (!paletteArmAsset(state, editorCore, wallTex))
						{
							lpFailed = true;
							lpFail = "arming a bare texture failed";
						}
						else if (state.tilePalette.armedKind !=
								AssetKind::Texture ||
							editorCore.getActiveTool() !=
								Orkige::EditorTool::Paint ||
							state.tilePalette.hasEdgeWalls ||
							state.tilePalette.previewImagePath.empty() ||
							assetThumbnailFor(state,
								state.tilePalette.previewImagePath) == 0)
						{
							lpFailed = true;
							lpFail = "bare-texture arm / ghost preview wrong";
						}
					}
					if (!lpFailed)
					{
						// paint a bare sprite tile into free cell (9,-3), tagged
						SDL_strlcpy(state.tilePalette.paintTags, "tile",
							sizeof(state.tilePalette.paintTags));
						editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							9.0f, -3.0f, 6.0f, 0);
						const std::string sr = cellRoot(9.0f, -3.0f);
						optr<Orkige::GameObject> root =
							gameObjectManager.getGameObject(sr).lock();
						std::string srcId;
						std::string width;
						const bool hasSprite = editorCore.getObjectProperty(sr,
							"SpriteComponent", "width", width);
						editorCore.getObjectProperty(sr, "TileComponent",
							"sourceAssetId", srcId);
						Orkige::StringVector tags;
						editorCore.getObjectTags(sr, tags);
						const bool ok = root && !sr.empty() &&
							root->getPrefabRef().empty() && hasSprite &&
							!srcId.empty() &&
							std::fabs(std::stof(width) - 6.0f) < 1e-3f &&
							std::find(tags.begin(), tags.end(), "tile") !=
								tags.end();
						// dragging back over an identical bare tile is a no-op
						const bool noop = !editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							9.0f, -3.0f, 6.0f, 0);
						if (!ok || !noop)
						{
							lpFailed = true;
							lpFail = "bare sprite tile structure / no-op wrong";
						}
					}
					// MIXED grid: a bare sprite tile replaces a prefab tile in the
					// same cell and vice versa (one seam, across kinds)
					if (!lpFailed)
					{
						const std::string tilePrefab =
							(std::filesystem::path(levelPaintTempRoot) / "assets" /
								"tile.oprefab").string();
						paletteArmAsset(state, editorCore, tilePrefab);
						editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							9.0f, 3.0f, 6.0f, 0);
						const std::string asPrefab = cellRoot(9.0f, 3.0f);
						optr<Orkige::GameObject> prefabRoot =
							gameObjectManager.getGameObject(asPrefab).lock();
						const bool wasPrefab = prefabRoot &&
							!prefabRoot->getPrefabRef().empty();
						// paint the bare texture over the prefab tile -> replaced
						const std::string wallTex =
							(std::filesystem::path(levelPaintTempRoot) / "assets" /
								"wall.png").string();
						paletteArmAsset(state, editorCore, wallTex);
						SDL_strlcpy(state.tilePalette.paintTags, "",
							sizeof(state.tilePalette.paintTags));
						editorCore.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							9.0f, 3.0f, 6.0f, 0);
						const std::string asSprite = cellRoot(9.0f, 3.0f);
						optr<Orkige::GameObject> spriteRoot =
							gameObjectManager.getGameObject(asSprite).lock();
						std::string spriteWidth;
						const bool nowSprite = spriteRoot &&
							spriteRoot->getPrefabRef().empty() &&
							editorCore.getObjectProperty(asSprite,
								"SpriteComponent", "width", spriteWidth) &&
							!gameObjectManager.objectExists(asPrefab);
						// erase the scratch cell (bare tile erases like any tile)
						const bool erased =
							editorCore.eraseTileAtCell(9.0f, 3.0f, 6.0f, 0);
						if (!wasPrefab || !nowSprite || !erased ||
							!cellRoot(9.0f, 3.0f).empty())
						{
							lpFailed = true;
							lpFail = "mixed-grid replace across kinds wrong";
						}
					}
					// a bare SHAPE tile (VectorShapeComponent + TileComponent):
					// copy a shape asset in, arm it, paint/erase in a free cell
					if (!lpFailed)
					{
						// the shape asset lives in a sibling sample project of the
						// source roller dir (levelPaintEnv), not the temp copy
						const std::string shapeSrc =
							(std::filesystem::path(levelPaintEnv)
								.parent_path() / "vectorshapes" / "assets" /
								"blob.oshape").string();
						const std::string shapeDst =
							(std::filesystem::path(levelPaintTempRoot) / "assets" /
								"blob.oshape").string();
						std::error_code shErr;
						std::filesystem::copy_file(shapeSrc, shapeDst,
							std::filesystem::copy_options::overwrite_existing,
							shErr);
						SDL_strlcpy(state.tilePalette.paintTags, "",
							sizeof(state.tilePalette.paintTags));
						const bool armed = !shErr &&
							paletteArmAsset(state, editorCore, shapeDst) &&
							state.tilePalette.armedKind == AssetKind::VectorShape;
						if (armed)
						{
							editorCore.paintTileAtCell(
								paletteMakePaintDesc(state.tilePalette),
								-3.0f, 3.0f, 6.0f, 0);
						}
						const std::string shapeRoot = cellRoot(-3.0f, 3.0f);
						optr<Orkige::GameObject> root =
							gameObjectManager.getGameObject(shapeRoot).lock();
						std::string shapeName;
						const bool ok = armed && root && !shapeRoot.empty() &&
							root->getPrefabRef().empty() &&
							editorCore.getObjectProperty(shapeRoot,
								"VectorShapeComponent", "shape", shapeName) &&
							editorCore.getObjectProperty(shapeRoot,
								"TileComponent", "sourceAssetId", shapeName);
						const bool erased =
							editorCore.eraseTileAtCell(-3.0f, 3.0f, 6.0f, 0);
						if (!ok || !erased || !cellRoot(-3.0f, 3.0f).empty())
						{
							lpFailed = true;
							lpFail = "bare shape tile paint / erase wrong";
						}
					}
					// a NON-prefab object at a cell is never painted over/erased
					if (!lpFailed)
					{
						gameObjectManager.createGameObject("Decoration");
						editorCore.addComponentToObject("Decoration",
							"TransformComponent");
						editorCore.setObjectProperty("Decoration",
							"TransformComponent", "position", "3 -3 0");
						const std::string at = cellRoot(3.0f, -3.0f);
						editorCore.eraseTileAtCell(3.0f, -3.0f, 6.0f, 0);
						const bool decorationSurvives =
							gameObjectManager.objectExists("Decoration");
						editorCore.undo();	// bring the erased tile back
						if (at == "Decoration" || !decorationSurvives)
						{
							lpFailed = true;
							lpFail = "a non-prefab object was painted over/erased";
						}
					}
					// scene round-trip preserves the painted structure
					if (!lpFailed)
					{
						const std::string scenePath =
							(std::filesystem::path(
								state.project.getScenesDirectory()) /
								"painted.oscene").string();
						const bool saved =
							saveSceneToPath(state, editorCore, scenePath);
						newScene(state, editorCore);
						const bool reopened = saved &&
							openSceneFromPath(state, editorCore, scenePath);
						const std::string r0 = cellRoot(-3.0f, -3.0f);
						const std::string r1 = cellRoot(3.0f, -3.0f);
						// the bare sprite tile at (9,-3) must survive the round-trip
						// as a plain SpriteComponent object carrying its Tile
						// component + source id (no prefab file was ever generated)
						const std::string sr = cellRoot(9.0f, -3.0f);
						std::string spriteWidth;
						std::string spriteSrcId;
						const bool bareOk = !sr.empty() &&
							editorCore.getObjectProperty(sr, "SpriteComponent",
								"width", spriteWidth) &&
							editorCore.getObjectProperty(sr, "TileComponent",
								"sourceAssetId", spriteSrcId) &&
							!spriteSrcId.empty();
						bool ok = reopened && !r0.empty() && !r1.empty() && bareOk;
						if (ok)
						{
							// r1 (the right-open tile): WallRight suppressed,
							// openEdges 8, tag "tile", provided children back
							optr<Orkige::GameObject> root =
								gameObjectManager.getGameObject(r1).lock();
							Orkige::StringVector supp = root
								? root->getSuppressedPrefabChildren()
								: Orkige::StringVector();
							std::string edges;
							editorCore.getObjectProperty(r1, "TileComponent",
								"openEdges", edges);
							Orkige::StringVector tags;
							editorCore.getObjectTags(r1, tags);
							ok = supp.size() == 1 && supp[0] == "WallRight" &&
								edges == "8" &&
								std::find(tags.begin(), tags.end(), "tile") !=
									tags.end() &&
								!gameObjectManager.objectExists(r1 + "/WallRight") &&
								gameObjectManager.objectExists(r1 + "/WallTop");
						}
						if (!ok)
						{
							lpFailed = true;
							lpFail = "scene round-trip lost the paint data";
						}
						else
						{
							levelPaintExpectedRoots.clear();
							levelPaintExpectedRoots.push_back(r0);
							levelPaintExpectedRoots.push_back(r1);
							levelPaintExpectedBareRoots.clear();
							levelPaintExpectedBareRoots.push_back(sr);
						}
					}
					// append the scene to the level sequence; a duplicate refuses
					if (!lpFailed)
					{
						const bool added =
							addCurrentSceneToLevels(state, editorCore);
						Orkige::LevelSequence seq;
						const bool loaded = seq.load(
							state.project.resolvePath("levels.olevels"));
						bool hasOurs = false;
						for (Orkige::LevelSequence::Entry const& entry :
							seq.getEntries())
						{
							if (entry.scenePath == "scenes/painted.oscene")
							{
								hasOurs = true;
							}
						}
						const bool dupRefused =
							!addCurrentSceneToLevels(state, editorCore);
						if (!added || !loaded || !hasOurs || !dupRefused)
						{
							lpFailed = true;
							lpFail = "add-to-levels / duplicate refusal wrong";
						}
					}
					// ACCEPTANCE: the painted scene must PLAY in the player
					if (!lpFailed)
					{
						if (!startPlay(playSession, gameObjectManager,
							state.project))
						{
							lpFailed = true;
							lpFail = "startPlay on the painted scene failed";
						}
						else
						{
							levelPaintPhase = LevelPaintPhase::WaitRemote;
							levelPaintDeadline =
								lpNow + std::chrono::seconds(60);
						}
					}
				}
				else if (levelPaintPhase == LevelPaintPhase::WaitRemote)
				{
					if (playSession.mode == PlaySession::Mode::Playing &&
						playSession.helloReceived &&
						playSession.hierarchyReceived)
					{
						const auto inRemote = [&](std::string const& id)
						{
							return std::find(playSession.remoteHierarchy.begin(),
								playSession.remoteHierarchy.end(), id) !=
								playSession.remoteHierarchy.end();
						};
						bool ok = !levelPaintExpectedRoots.empty();
						for (std::string const& root : levelPaintExpectedRoots)
						{
							// the root AND its non-suppressed provided children
							// (Frame, WallTop) must have reached the player
							if (!inRemote(root) || !inRemote(root + "/Frame") ||
								!inRemote(root + "/WallTop"))
							{
								ok = false;
							}
						}
						// the bare sprite tile (no prefab children) must arrive too
						for (std::string const& root : levelPaintExpectedBareRoots)
						{
							if (!inRemote(root))
							{
								ok = false;
							}
						}
						if (!ok)
						{
							lpFailed = true;
							lpFail = "painted tiles missing in the remote hierarchy";
						}
						else
						{
							SDL_Log("orkige_editor: level-paint - painted scene "
								"plays (%zu remote objects)",
								playSession.remoteHierarchy.size());
							requestStopPlay(playSession);
							levelPaintPhase = LevelPaintPhase::WaitRevert;
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						lpFailed = true;
						lpFail = "play ended before the remote hierarchy arrived";
					}
				}
				else if (levelPaintPhase == LevelPaintPhase::WaitRevert)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						levelPaintPhase = LevelPaintPhase::EscapeCheck;
						levelPaintEscapeStep = 0;
					}
				}
				else if (levelPaintPhase == LevelPaintPhase::EscapeCheck)
				{
					// paint-mode exits: Escape disarms + restores Translate,
					// and a project switch never carries the armed prefab over
					const std::string escTile =
						(std::filesystem::path(levelPaintTempRoot) / "assets" /
							"tile.oprefab").string();
					if (levelPaintEscapeStep == 0)
					{
						if (!paletteArmAsset(state, editorCore, escTile) ||
							editorCore.getActiveTool() !=
								Orkige::EditorTool::Paint)
						{
							lpFailed = true;
							lpFail = "re-arm for the escape check failed";
						}
						// the real key path: a synthetic SDL key event flows
						// through the same translation live input takes
						SDL_Event escDown = {};
						escDown.type = SDL_EVENT_KEY_DOWN;
						escDown.key.key = SDLK_ESCAPE;
						escDown.key.scancode = SDL_SCANCODE_ESCAPE;
						escDown.key.windowID = SDL_GetWindowID(window);
						SDL_PushEvent(&escDown);
					}
					else if (levelPaintEscapeStep == 1)
					{
						SDL_Event escUp = {};
						escUp.type = SDL_EVENT_KEY_UP;
						escUp.key.key = SDLK_ESCAPE;
						escUp.key.scancode = SDL_SCANCODE_ESCAPE;
						escUp.key.windowID = SDL_GetWindowID(window);
						SDL_PushEvent(&escUp);
					}
					else if (levelPaintEscapeStep == 2)
					{
						if (editorCore.getActiveTool() !=
								Orkige::EditorTool::Translate ||
							!state.tilePalette.armedAssetPath.empty())
						{
							lpFailed = true;
							lpFail = "Escape did not leave paint mode";
						}
						// Escape must DISARM, never quit: reaching this phase proves
						// the loop still runs, and quitRequested must be untouched
						// (an ESC that reached the quit path would have set it)
						if (!lpFailed && state.quitRequested)
						{
							lpFailed = true;
							lpFail = "Escape triggered a quit instead of disarming";
						}
						// disarm-by-intent: the shared "did something that isn't
						// painting" exit (a scene/hierarchy select, an empty-space
						// click, a browser asset select all route through it)
						if (!lpFailed)
						{
							if (!paletteArmAsset(state, editorCore, escTile) ||
								editorCore.getActiveTool() !=
									Orkige::EditorTool::Paint)
							{
								lpFailed = true;
								lpFail = "re-arm for the intent-disarm check failed";
							}
							else
							{
								disarmPaintTileOnIntent(state, editorCore);
								if (editorCore.getActiveTool() !=
										Orkige::EditorTool::Translate ||
									!state.tilePalette.armedAssetPath.empty())
								{
									lpFailed = true;
									lpFail = "disarm-by-intent stayed in paint mode";
								}
							}
						}
						// arm again, then close the project: the armed prefab
						// belongs to it and must not survive
						if (!lpFailed &&
							(!paletteArmAsset(state, editorCore, escTile) ||
								editorCore.getActiveTool() !=
									Orkige::EditorTool::Paint))
						{
							lpFailed = true;
							lpFail = "re-arm for the close check failed";
						}
						if (!lpFailed)
						{
							closeProject(state, editorCore);
							if (!state.tilePalette.armedAssetPath.empty() ||
								editorCore.getActiveTool() ==
									Orkige::EditorTool::Paint)
							{
								lpFailed = true;
								lpFail =
									"closing the project kept the armed prefab";
							}
						}
						if (!lpFailed)
						{
							SDL_Log("orkige_editor: level-paint test - OK");
							levelPaintPhase = LevelPaintPhase::Done;
							running = false;
						}
					}
					++levelPaintEscapeStep;
				}
				// deadline backstop for the play phases
				if (!lpFailed &&
					(levelPaintPhase == LevelPaintPhase::WaitRemote ||
						levelPaintPhase == LevelPaintPhase::WaitRevert) &&
					lpNow > levelPaintDeadline)
				{
					lpFailed = true;
					lpFail = "play phase timed out";
				}
				if (lpFailed)
				{
					SDL_Log("orkige_editor: level-paint test - FAILED: %s",
						lpFail.c_str());
					exitCode = 11;
					std::error_code cleanupErr;
					std::filesystem::remove_all(levelPaintTempRoot, cleanupErr);
					running = false;
				}
			}
			// --- prefab edit-mode selfcheck (ORKIGE_EDITOR_PREFABEDIT) ---
			if (prefabEditEnv && frameCount == 10)
			{
				bool peFailed = false;
				std::string peFail;
				std::string peScenePath;
				std::string pePrefabPath;
				std::string peSnapshotPath;
				// two Vec3 property probes: read a transform position as three
				// floats (canonical float formatting may differ from the input
				// string, so assertions compare parsed values)
				auto positionOf = [&](std::string const& id, float* xyz)
				{
					std::string text;
					return editorCore.getObjectProperty(id,
						"TransformComponent", "position", text) &&
						parsePlayFloats(text, xyz, 3);
				};
				auto positionYIs = [&](std::string const& id, float expected)
				{
					float xyz[3] = { 0.0f, 0.0f, 0.0f };
					return positionOf(id, xyz) &&
						std::fabs(xyz[1] - expected) < 1e-3f;
				};
				// 1. temp project copy + fixture scene: a tile instance with a
				// per-instance override on a provided child, saved to disk
				{
					std::error_code peErr;
					prefabEditTempRoot =
						(std::filesystem::temp_directory_path() /
						("orkige_prefabedit_" + std::to_string(
							std::chrono::steady_clock::now()
								.time_since_epoch().count()))).string();
					std::filesystem::copy(prefabEditEnv, prefabEditTempRoot,
						std::filesystem::copy_options::recursive, peErr);
					if (peErr || !openProjectFromPath(state, editorCore,
						prefabEditTempRoot))
					{
						peFailed = true;
						peFail = "could not prepare/open the temp copy";
					}
				}
				if (!peFailed)
				{
					newScene(state, editorCore);
					pePrefabPath = (std::filesystem::path(prefabEditTempRoot) /
						"assets" / "tile.oprefab").string();
					std::string assetId;
					if (optr<Orkige::AssetDatabase> const& database =
						state.project.getAssetDatabase())
					{
						assetId = database->idForPath("assets/tile.oprefab");
					}
					if (!editorCore.executeCommand(Orkige::onew(
						new Orkige::CreatePrefabInstanceCommand("Tile1",
							pePrefabPath, "assets/tile.oprefab", assetId,
							Orkige::Vec3::ZERO))))
					{
						peFailed = true;
						peFail = "could not instantiate the fixture instance";
					}
				}
				if (!peFailed)
				{
					// the per-instance override: WallTop moves - it must
					// survive the prefab edit (merge rule: overridden wins)
					std::string before;
					const bool overridden = editorCore.getObjectProperty(
						"Tile1/WallTop", "TransformComponent", "position",
						before) &&
						editorCore.applyPropertyChange("Tile1/WallTop",
							"TransformComponent", "position", before,
							"0 9 0", 0);
					peScenePath = (std::filesystem::path(
						state.project.getScenesDirectory()) /
						"prefab_edit.oscene").string();
					if (!overridden ||
						!saveSceneToPath(state, editorCore, peScenePath) ||
						editorCore.getUndoStackSize() == 0)
					{
						peFailed = true;
						peFail = "fixture override/save wrong";
					}
				}
				// 2. open the prefab stage: the world swaps to the prefab
				// subtree, undo scope resets, the scene session is stashed
				if (!peFailed)
				{
					Orkige::StringVector locals;
					Orkige::StringVector rootComponents;
					Orkige::PrefabSerializer::listPrefabInfo(pePrefabPath,
						locals, rootComponents);
					if (!openPrefabForEdit(state, editorCore, pePrefabPath))
					{
						peFailed = true;
						peFail = "openPrefabForEdit failed";
					}
					else
					{
						peSnapshotPath =
							state.prefabEditStack.back().snapshotPath;
						std::error_code peErr;
						const bool staged = isPrefabEditActive(state) &&
							gameObjectManager.objectExists("tile") &&
							gameObjectManager.objectExists("tile/WallTop") &&
							!gameObjectManager.objectExists("Tile1") &&
							gameObjectManager.getGameObjects().size() ==
								locals.size() &&
							editorCore.getUndoStackSize() == 0 &&
							state.currentScenePath.empty() &&
							!editorCore.isSceneDirty() &&
							editorCore.getSelectedObjectId() == "tile" &&
							std::filesystem::is_regular_file(peSnapshotPath,
								peErr);
						if (!staged)
						{
							peFailed = true;
							peFail = "stage state after open wrong";
						}
					}
				}
				// 3. the guards: re-open and every scene/project lifecycle op
				// refuse while the stage is open (the world stays untouched)
				if (!peFailed)
				{
					const bool reopenRefused =
						!openPrefabForEdit(state, editorCore, pePrefabPath);
					const bool saveSceneRefused =
						!saveSceneToPath(state, editorCore, peScenePath);
					newScene(state, editorCore);	// must refuse
					const bool newSceneRefused =
						gameObjectManager.objectExists("tile");
					editorCore.selectObject("tile");
					const bool createPrefabRefused =
						!createPrefabFromSelection(state, editorCore);
					const bool armRefused =
						!paletteArmAsset(state, editorCore, pePrefabPath);
					const bool levelsRefused =
						!addCurrentSceneToLevels(state, editorCore);
					if (!reopenRefused || !saveSceneRefused ||
						!newSceneRefused || !createPrefabRefused ||
						!armRefused || !levelsRefused)
					{
						peFailed = true;
						peFail = "a prefab-mode guard did not refuse";
					}
				}
				// 4. edit the stage: a child property (NOT overridden by the
				// instance) and a new object under the root
				if (!peFailed)
				{
					std::string before;
					bool edited = editorCore.getObjectProperty(
						"tile/WallBottom", "TransformComponent", "position",
						before) &&
						editorCore.applyPropertyChange("tile/WallBottom",
							"TransformComponent", "position", before,
							"0 -9 0", 0);
					edited = edited &&
						!gameObjectManager.createGameObject("Gem").expired() &&
						editorCore.addComponentToObject("Gem",
							"TransformComponent") &&
						editorCore.reparentObject("Gem", "tile");
					if (!edited || editorCore.getUndoStackSize() == 0 ||
						!editorCore.isSceneDirty())
					{
						peFailed = true;
						peFail = "stage edits wrong";
					}
				}
				// 5. the save guards: a stray root refuses (named), a deleted/
				// renamed-away root refuses; then the real save succeeds
				if (!peFailed)
				{
					gameObjectManager.createGameObject("Stray");
					const bool strayRefused =
						!savePrefabEdit(state, editorCore);
					gameObjectManager.delGameObject("Stray");
					const bool renamedAway =
						editorCore.renameObject("tile", "tileMoved");
					const bool rootRefused =
						!savePrefabEdit(state, editorCore);
					const bool renamedBack =
						editorCore.renameObject("tileMoved", "tile");
					const bool saved = savePrefabEdit(state, editorCore);
					if (!strayRefused || !renamedAway || !rootRefused ||
						!renamedBack || !saved || editorCore.isSceneDirty())
					{
						peFailed = true;
						peFail = "save guards / save wrong";
					}
				}
				// 6. dirty close with the explicit Discard policy (the modal
				// is UI-only; automated runs pass the policy): the post-save
				// edit must NOT reach the file
				if (!peFailed)
				{
					std::string before;
					const bool dirtied = editorCore.getObjectProperty(
						"tile/WallBottom", "TransformComponent", "position",
						before) &&
						editorCore.applyPropertyChange("tile/WallBottom",
							"TransformComponent", "position", before,
							"0 -20 0", 0) &&
						editorCore.isSceneDirty();
					if (!dirtied ||
						!closePrefabEdit(state, editorCore,
							PrefabClosePolicy::Discard))
					{
						peFailed = true;
						peFail = "dirty close (Discard) failed";
					}
				}
				// 7. the restored scene: path/dirty/selection back, the
				// instance refreshed with the prefab edit (WallBottom at the
				// SAVED value, not the discarded one; the added Gem child
				// arrived) while the per-instance override SURVIVED, the undo
				// scope fresh, the temp snapshot gone
				if (!peFailed)
				{
					std::error_code peErr;
					const bool restored = !isPrefabEditActive(state) &&
						state.currentScenePath == peScenePath &&
						!editorCore.isSceneDirty() &&
						editorCore.getSelectedObjectId() == "Tile1" &&
						gameObjectManager.objectExists("Tile1") &&
						gameObjectManager.objectExists("Tile1/Gem") &&
						positionYIs("Tile1/WallBottom", -9.0f) &&
						positionYIs("Tile1/WallTop", 9.0f) &&
						editorCore.getUndoStackSize() == 0 &&
						!std::filesystem::exists(peSnapshotPath, peErr);
					if (!restored)
					{
						peFailed = true;
						peFail = "restored scene / instance refresh wrong";
					}
				}
				std::error_code peCleanupErr;
				std::filesystem::remove_all(prefabEditTempRoot, peCleanupErr);
				SDL_Log("orkige_editor: prefab edit test - %s%s%s",
					peFailed ? "FAILED" : "OK", peFailed ? ": " : "",
					peFail.c_str());
				if (peFailed)
				{
					exitCode = 13;
				}
				running = false;
			}
			// --- 2D editing-ergonomics test (ORKIGE_EDITOR_MARQUEE) ---
			// marquee box select + ghost preview + drop-cell paint, driven
			// through the REAL scene-panel mouse path with synthetic SDL events
			if (marqueeEnv)
			{
				// convert a Scene-image render-target pixel to a window-point
				// SDL event coordinate (the inverse of ImGuiSDL3Input's mouse
				// scale), so a pushed event lands at the intended io.MousePos
				auto toWindow = [&](float px, float py, float& wx, float& wy)
				{
					int winW = 0, winH = 0;
					SDL_GetWindowSize(window, &winW, &winH);
					unsigned int drawW = 0, drawH = 0;
					render->getWindowSize(drawW, drawH);
					const float sx = (winW > 0 && drawW > 0)
						? static_cast<float>(drawW) / winW : 1.0f;
					const float sy = (winH > 0 && drawH > 0)
						? static_cast<float>(drawH) / winH : 1.0f;
					wx = px / sx;
					wy = py / sy;
				};
				auto pushMove = [&](float px, float py)
				{
					float wx = 0.0f, wy = 0.0f;
					toWindow(px, py, wx, wy);
					SDL_Event e = {};
					e.type = SDL_EVENT_MOUSE_MOTION;
					e.motion.windowID = SDL_GetWindowID(window);
					e.motion.x = wx;
					e.motion.y = wy;
					SDL_PushEvent(&e);
				};
				auto pushButton = [&](bool down, float px, float py)
				{
					float wx = 0.0f, wy = 0.0f;
					toWindow(px, py, wx, wy);
					SDL_Event e = {};
					e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
						: SDL_EVENT_MOUSE_BUTTON_UP;
					e.button.windowID = SDL_GetWindowID(window);
					e.button.button = SDL_BUTTON_LEFT;
					e.button.down = down;
					e.button.clicks = 1;
					e.button.x = wx;
					e.button.y = wy;
					SDL_PushEvent(&e);
				};
				auto pushSuper = [&](bool down)
				{
					SDL_Event e = {};
					e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
					e.key.windowID = SDL_GetWindowID(window);
					e.key.key = SDLK_LGUI;
					e.key.scancode = SDL_SCANCODE_LGUI;
					e.key.mod = down ? SDL_KMOD_LGUI : SDL_KMOD_NONE;
					e.key.down = down;
					SDL_PushEvent(&e);
				};
				auto projectTile = [&](float wx, float wy, float& px,
					float& py) -> bool
				{
					Orkige::Real nx = 0.0f, ny = 0.0f;
					if (!sceneTarget.camera->projectPoint(
						Orkige::Vec3(wx, wy, 0.0f), nx, ny))
					{
						return false;
					}
					px = state.sceneImageMin.x + nx * state.sceneImageSize.x;
					py = state.sceneImageMin.y + ny * state.sceneImageSize.y;
					return true;
				};
				auto mqAbort = [&](std::string const& why)
				{
					SDL_Log("orkige_editor: marquee test - FAILED: %s",
						why.c_str());
					exitCode = 12;
					std::error_code cleanupErr;
					std::filesystem::remove_all(mqTempRoot, cleanupErr);
					running = false;
				};
				// A verification step's state is CONDITION-driven, not
				// frame-driven: a pushed SDL event travels event-pump -> ImGui
				// io -> scene panel over a variable number of frames, so a fixed
				// step gap can check the marquee/selection before it settled
				// (a CI runner-image timing shift is enough to flake it). Hold
				// the step and re-check each frame until the state settles;
				// only fail (with the real reason) after a generous budget the
				// deadline backstop also guards. Returns true when settled.
				const int mqSettleBudget = 30;
				auto mqSettled = [&](int stepId, bool condition,
					char const* why) -> bool
				{
					if (condition)
					{
						if (mqSettleHold > 0)
						{
							SDL_Log("orkige_editor: marquee step %d settled "
								"after %d extra frame(s)", stepId, mqSettleHold);
						}
						mqSettleHold = 0;
						return true;
					}
					if (++mqSettleHold <= mqSettleBudget)
					{
						mqStep = stepId - 1;	// re-run this case next frame
						return false;
					}
					mqSettleHold = 0;
					mqAbort(why);
					return false;
				};

				if (marqueePhase == MarqueePhase::Idle && frameCount == 10)
				{
					std::error_code mqErr;
					mqTempRoot = (std::filesystem::temp_directory_path() /
						("orkige_marquee_" + std::to_string(
							std::chrono::steady_clock::now()
								.time_since_epoch().count()))).string();
					std::filesystem::copy(marqueeEnv, mqTempRoot,
						std::filesystem::copy_options::recursive, mqErr);
					if (mqErr || !openProjectFromPath(state, editorCore,
						mqTempRoot))
					{
						mqAbort("could not prepare/open the temp copy");
					}
					else
					{
						newScene(state, editorCore);
						// 2D top-down view centred on the origin, framed so the
						// three tiles at x = -16/0/+16 (y = 0) sit in the middle
						// with a clear screen gap between them (the single-tile
						// marquees need to bracket one tile without touching its
						// neighbour)
						viewSettings.editor2D = true;
						state.camera.target = Orkige::Vec3(0.0f, 0.0f, 0.0f);
						state.camera.distance = 24.0f;
						apply2DCamera(state, sceneTarget.camera, sceneCameraNode);
						const std::string tilePrefab =
							(std::filesystem::path(mqTempRoot) / "assets" /
								"tile.oprefab").string();
						if (!paletteArmAsset(state, editorCore, tilePrefab))
						{
							mqAbort("paletteArmAsset failed");
						}
						// GHOST PREVIEW: arming a sprite-bearing prefab yields a
						// non-null preview handle (the tile's texture thumbnail)
						else if (state.tilePalette.previewImageRef.empty() ||
							state.tilePalette.previewImagePath.empty() ||
							assetThumbnailFor(state,
								state.tilePalette.previewImagePath) == 0)
						{
							mqAbort("ghost preview handle is null while armed");
						}
						else
						{
							const Orkige::EditorPaintDesc desc =
								paletteMakePaintDesc(state.tilePalette);
							editorCore.paintTileAtCell(desc, -16.0f, 0.0f,
								6.0f, 0);
							editorCore.paintTileAtCell(desc, 0.0f, 0.0f,
								6.0f, 0);
							editorCore.paintTileAtCell(desc, 16.0f, 0.0f,
								6.0f, 0);
							// DROP-CELL PAINT is undoable: the same seam a Tile
							// Palette drag-drop uses (paint a cell, one undo
							// step that removes it)
							editorCore.paintTileAtCell(desc, 32.0f, 0.0f,
								6.0f, 0);
							const bool dropped = !editorCore
								.findTileAtCell(32.0f, 0.0f, 6.0f)
								.empty();
							editorCore.undo();
							const bool undone = editorCore
								.findTileAtCell(32.0f, 0.0f, 6.0f)
								.empty();
							mqTileIds.clear();
							mqTileIds.push_back(editorCore
								.findTileAtCell(-16.0f, 0.0f, 6.0f));
							mqTileIds.push_back(editorCore
								.findTileAtCell(0.0f, 0.0f, 6.0f));
							mqTileIds.push_back(editorCore
								.findTileAtCell(16.0f, 0.0f, 6.0f));
							if (!dropped || !undone || mqTileIds[0].empty() ||
								mqTileIds[1].empty() || mqTileIds[2].empty())
							{
								mqAbort("tile setup / drop-cell undo wrong");
							}
							else
							{
								editorCore.clearHistory();
								editorCore.clearSelection();
								editorCore.setActiveTool(
									Orkige::EditorTool::Select);
								marqueePhase = MarqueePhase::Run;
								mqStep = 0;
							}
						}
					}
				}
				else if (marqueePhase == MarqueePhase::Run)
				{
					// project the tile screen points once the 2D layout is live
					// (the panel has recorded its image rect)
					const bool layoutReady = state.sceneImageSize.x > 1.0f &&
						state.sceneImageSize.y > 1.0f;
					switch (mqStep)
					{
					case 4:
						if (!layoutReady)
						{
							mqStep = 3;	// hold until the panel has drawn
							break;
						}
						// Derive the all-tiles box from the live camera projection.
						// Fixed image fractions became stale when the Scene dock's
						// aspect ratio changed (for example after adding preview tabs).
						// A 45%-of-centre-gap pad encloses each 6-unit tile while the
						// press corner remains in empty space between tile bounds.
						{
							float lx = 0.0f, ly = 0.0f, mx = 0.0f, my = 0.0f;
							float rx = 0.0f, ry = 0.0f;
							if (!projectTile(-16.0f, 0.0f, lx, ly) ||
								!projectTile(0.0f, 0.0f, mx, my) ||
								!projectTile(16.0f, 0.0f, rx, ry))
							{
								mqAbort("tile projection failed before marquee");
								break;
							}
							const float gap = std::min(std::abs(mx - lx),
								std::abs(rx - mx));
							const float pad = gap * 0.45f;
							const float minX = state.sceneImageMin.x + 2.0f;
							const float minY = state.sceneImageMin.y + 2.0f;
							const float maxX = state.sceneImageMin.x +
								state.sceneImageSize.x - 2.0f;
							const float maxY = state.sceneImageMin.y +
								state.sceneImageSize.y - 2.0f;
							mqStartX = std::max(minX, std::min({lx, mx, rx}) - pad);
							mqStartY = std::max(minY, std::min({ly, my, ry}) - pad);
							mqEndX = std::min(maxX, std::max({lx, mx, rx}) + pad);
							mqEndY = std::min(maxY, std::max({ly, my, ry}) + pad);
						}
						pushMove(mqStartX, mqStartY);
						break;
					case 7:
						pushButton(true, mqStartX, mqStartY);	// press empty
						break;
					case 10:
						pushMove(mqEndX, mqEndY);				// drag
						break;
					case 13:
						pushButton(false, mqEndX, mqEndY);		// release
						break;
					case 16:
					{
						// EVERY tile ROOT is now selected, and the marquee closed
						const Orkige::StringVector& sel =
							editorCore.getSelection();
						bool all = sel.size() == mqTileIds.size() &&
							!state.marqueePending && !state.marqueeActive;
						for (std::string const& id : mqTileIds)
						{
							all = all && editorCore.isSelected(id);
						}
						if (!mqSettled(16, all,
							"marquee did not select all tiles"))
						{
							break;
						}
						{
							// project the three tiles to place: the mid-tile click
							// point, and the single-tile boxes over left / right
							// (each box presses in the empty top strip so it starts
							// a marquee, and stops at the mid-gap so it catches only
							// its own tile - marquee band-selects ROOTS, so this is
							// unambiguous)
							float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
							if (!projectTile(0.0f, 0.0f, mqMidX, mqMidY) ||
								!projectTile(-16.0f, 0.0f, lx, ly) ||
								!projectTile(16.0f, 0.0f, rx, ry))
							{
								mqAbort("tile projection failed");
							}
							else
							{
								const float topY = state.sceneImageMin.y +
									0.12f * state.sceneImageSize.y;
								const float botY = state.sceneImageMin.y +
									0.85f * state.sceneImageSize.y;
								// bracket each side tile by 40% of the screen gap
								// to its neighbour - wide enough to cover the tile,
								// tight enough to miss the mid tile
								const float gap = (mqMidX - lx) * 0.4f;
								// left box (press in the empty strip above the left
								// tile, drag down over it)
								mqLbSX = lx - gap;
								mqLbSY = topY;
								mqLbEX = lx + gap;
								mqLbEY = botY;
								// right box (same, over the right tile)
								mqRbSX = rx - gap;
								mqRbSY = topY;
								mqRbEX = rx + gap;
								mqRbEY = botY;
							}
						}
						break;
					}
					case 19:
						pushMove(mqMidX, mqMidY);				// over mid tile
						break;
					case 22:
						pushButton(true, mqMidX, mqMidY);		// click (down)
						break;
					case 25:
						pushButton(false, mqMidX, mqMidY);		// click (up)
						break;
					case 28:
					{
						// a plain click that STARTS ON a tile picks a single
						// object (no rubber-band) - which exact object (root vs a
						// prefab child sprite) is the pre-existing 2D-pick rule, so
						// assert only "one object, and no marquee ran"
						const Orkige::StringVector& sel =
							editorCore.getSelection();
						const bool one = sel.size() == 1 &&
							!state.marqueeActive && !state.marqueePending;
						if (!mqSettled(28, one,
							"click did not select exactly one object"))
						{
							break;
						}
						break;
					}
					// left tile via a plain marquee (REPLACE): selection -> {left}
					case 31:
						pushMove(mqLbSX, mqLbSY);
						break;
					case 34:
						pushButton(true, mqLbSX, mqLbSY);		// press empty
						break;
					case 37:
						pushMove(mqLbEX, mqLbEY);				// drag over left
						break;
					case 40:
						pushButton(false, mqLbEX, mqLbEY);		// release
						break;
					case 43:
					{
						const Orkige::StringVector& sel =
							editorCore.getSelection();
						const bool leftOnly = sel.size() == 1 &&
							editorCore.isSelected(mqTileIds[0]);
						if (!mqSettled(43, leftOnly,
							"marquee over the left tile did not replace-select it"))
						{
							break;
						}
						break;
					}
					// right tile via a Cmd-marquee (EXTEND): -> {left, right}
					case 46:
						pushSuper(true);						// hold Cmd
						break;
					case 49:
						pushMove(mqRbSX, mqRbSY);
						break;
					case 52:
						pushButton(true, mqRbSX, mqRbSY);		// Cmd-press empty
						break;
					case 55:
						pushMove(mqRbEX, mqRbEY);				// drag over right
						break;
					case 58:
						pushButton(false, mqRbEX, mqRbEY);		// release
						break;
					case 61:
						pushSuper(false);						// release Cmd
						break;
					case 64:
					{
						// EXTEND kept the left tile AND added the right (never
						// cleared it)
						const Orkige::StringVector& sel =
							editorCore.getSelection();
						const bool both = sel.size() == 2 &&
							editorCore.isSelected(mqTileIds[0]) &&
							editorCore.isSelected(mqTileIds[2]);
						if (!both && mqSettleHold == mqSettleBudget)
						{
							// the last hold frame before we give up: diagnose
							SDL_Log("orkige_editor: marquee extend diag - "
								"selCount=%zu left=%d right=%d",
								sel.size(),
								editorCore.isSelected(mqTileIds[0]) ? 1 : 0,
								editorCore.isSelected(mqTileIds[2]) ? 1 : 0);
						}
						if (!mqSettled(64, both,
							"Cmd-drag did not extend the selection"))
						{
							break;
						}
						{
							SDL_Log("orkige_editor: marquee test - OK "
								"(select-all, click, replace, Cmd-extend, "
								"ghost, drop)");
							marqueePhase = MarqueePhase::Done;
							std::error_code cleanupErr;
							std::filesystem::remove_all(mqTempRoot, cleanupErr);
							running = false;
						}
						break;
					}
					default:
						break;
					}
					++mqStep;
				}
				// deadline backstop: never let the demo-frame cap turn a stuck
				// run into a false pass
				if (marqueePhase != MarqueePhase::Done && exitCode == 0 &&
					frameCount >= 140)
				{
					mqAbort("did not complete before the deadline");
				}
			}
			// --- editor-scripts selfcheck (ORKIGE_EDITOR_SCRIPTTEST=roller) ---
			if (scriptTestEnv && frameCount == 10)
			{
				auto scriptFail = [&](std::string const& why)
				{
					SDL_Log("orkige_editor: editor-scripts test - FAILED: %s",
						why.c_str());
					exitCode = 14;
					std::error_code cleanupErr;
					std::filesystem::remove_all(scriptTestTempRoot, cleanupErr);
					running = false;
				};
				// a copy of the project so the fixture writes never touch the tree
				std::error_code stErr;
				scriptTestTempRoot = (std::filesystem::temp_directory_path() /
					("orkige_scripttest_" + std::to_string(
						std::chrono::steady_clock::now()
							.time_since_epoch().count()))).string();
				std::filesystem::copy(scriptTestEnv, scriptTestTempRoot,
					std::filesystem::copy_options::recursive, stErr);
				const bool scriptingOn =
					Orkige::EditorScriptHost::scriptingAvailable();
				if (stErr || !openProjectFromPath(state, editorCore,
					scriptTestTempRoot))
				{
					scriptFail("could not prepare/open the temp copy");
				}
				else if (!scriptingOn)
				{
					// NOSCRIPT leg: the project opens fine, but running a tool is
					// an honest no-op (ran=false, an explaining error). The Tools
					// menu shows its disabled note (EditorScriptHost::
					// scriptingAvailable() is what the menu keys off).
					const std::filesystem::path scriptsDir =
						std::filesystem::path(scriptTestTempRoot) / "scripts";
					std::ofstream(scriptsDir / "noop.editor.lua")
						<< "-- tool: Noop\nreturn 0\n";
					editorScripts.scanProject(scriptsDir.string());
					const Orkige::EditorScriptHost::RunResult r =
						editorScripts.runToolByName("noop", controlContext);
					if (r.ran || r.ok || r.error.empty())
					{
						scriptFail("noscript: a tool must be an honest no-op");
					}
					else if (!state.project.isLoaded())
					{
						scriptFail("noscript: the project must still load");
					}
					else
					{
						SDL_Log("orkige_editor: editor-scripts test - OK "
							"(noscript: project loads, running is a no-op)");
						std::error_code cleanupErr;
						std::filesystem::remove_all(scriptTestTempRoot,
							cleanupErr);
						running = false;
					}
				}
				else
				{
					const std::filesystem::path scriptsDir =
						std::filesystem::path(scriptTestTempRoot) / "scripts";
					// a tool that authors a subtree through the editor.* verbs
					std::ofstream(scriptsDir / "fixture_ok.editor.lua")
						<< "-- tool: Fixture OK\n"
						<< "editor.create_object{ id = \"ToolCube\", "
						   "mesh = \"cube\", position = \"1 2 3\" }\n"
						<< "editor.set_component{ id = \"ToolCube\", "
						   "component = \"TransformComponent\", "
						   "position = \"5 6 7\" }\n"
						<< "editor.create_object{ id = \"ToolChild\", "
						   "mesh = \"cube\" }\n"
						<< "editor.reparent_object{ id = \"ToolChild\", "
						   "parent = \"ToolCube\" }\n"
						<< "editor.log(\"fixture ok: authored ToolCube + child\")"
						   "\n";
					// a tool that creates an object then ERRORS (a nil call) -
					// must roll back and report file:line
					std::ofstream(scriptsDir / "fixture_err.editor.lua")
						<< "-- tool: Fixture Err\n"
						<< "editor.create_object{ id = \"GhostCube\", "
						   "mesh = \"cube\" }\n"
						<< "local x = definitely_not_a_function()\n"
						<< "editor.log(\"unreachable\")\n";
					// a tool that asserts the EDITOR sandbox denies the unsafe
					// globals (a scene/editor script is untrusted content) and
					// still has the permitted computation stdlib. Every assert
					// must hold, so a clean run is the editor-sandbox proof.
					std::ofstream(scriptsDir / "fixture_security.editor.lua")
						<< "-- tool: Fixture Security\n"
						<< "assert(io == nil, \"io reachable\")\n"
						<< "assert(require == nil, \"require reachable\")\n"
						<< "assert(package == nil, \"package reachable\")\n"
						<< "assert(load == nil, \"load reachable\")\n"
						<< "assert(loadfile == nil, \"loadfile reachable\")\n"
						<< "assert(dofile == nil, \"dofile reachable\")\n"
						<< "assert(debug == nil, \"debug reachable\")\n"
						<< "assert(type(collectgarbage) == \"function\", "
						   "\"collectgarbage denied\")\n"
						<< "assert(os.execute == nil, \"os.execute reachable\")\n"
						<< "assert(os.remove == nil, \"os.remove reachable\")\n"
						<< "assert(os.time and os.date, \"os subset missing\")\n"
						<< "assert(math.floor(1.5) == 1, \"math missing\")\n"
						<< "assert(string.upper(\"x\") == \"X\", "
						   "\"string missing\")\n"
						<< "editor.log(\"security denials verified\")\n";
					editorScripts.scanProject(scriptsDir.string());

					// DISCOVERY: the fixtures + the shipped sample are listed, the
					// label override is honoured
					Orkige::EditorScriptTool const* okTool =
						editorScripts.findByName("fixture_ok");
					const bool discovered = okTool &&
						editorScripts.findByName("fixture_err") &&
						editorScripts.findByName("fixture_security") &&
						editorScripts.findByName("border_walls");
					if (!discovered)
					{
						scriptFail("discovery missed a fixture / the sample tool");
					}
					else if (okTool->label != "Fixture OK")
					{
						scriptFail("the -- tool: label override was not applied");
					}
					else
					{
						// RUN the OK fixture: it should apply as ONE undo step
						const Orkige::EditorScriptHost::RunResult okResult =
							editorScripts.runToolByName("fixture_ok",
								controlContext);
						std::string pos;
						editorCore.getObjectProperty("ToolCube",
							"TransformComponent", "position", pos);
						optr<Orkige::GameObject> child =
							gameObjectManager.getGameObject("ToolChild").lock();
						auto consoleHas = [&](std::string const& needle) -> bool
						{
							std::lock_guard<std::mutex> lock(console.mutex);
							for (ConsoleLine const& line : console.lines)
							{
								if (line.text.find(needle) != std::string::npos)
								{
									return true;
								}
							}
							return false;
						};
						if (!okResult.ran || !okResult.ok)
						{
							scriptFail("fixture_ok did not run cleanly: " +
								okResult.error);
						}
						else if (!gameObjectManager.objectExists("ToolCube") ||
							!child || child->getParentId() != "ToolCube")
						{
							scriptFail("fixture_ok did not author the subtree");
						}
						else if (pos != "5 6 7")
						{
							scriptFail("set_component did not take (pos=" +
								pos + ")");
						}
						else if (!consoleHas("authored ToolCube"))
						{
							scriptFail("editor.log did not reach the Console");
						}
						else if (editorCore.getUndoStackSize() != 1)
						{
							scriptFail("the run is not ONE undo step (" +
								std::to_string(editorCore.getUndoStackSize()) +
								")");
						}
						else if (!editorCore.undo() ||
							gameObjectManager.objectExists("ToolCube") ||
							gameObjectManager.objectExists("ToolChild"))
						{
							scriptFail("one undo did not revert the whole run");
						}
						else
						{
							// RUN the ERR fixture: file:line + no partial edits
							const std::size_t undoBefore =
								editorCore.getUndoStackSize();
							const Orkige::EditorScriptHost::RunResult errResult =
								editorScripts.runToolByName("fixture_err",
									controlContext);
							const bool reportsFileLine =
								errResult.error.find("fixture_err.editor.lua") !=
								std::string::npos;
							if (!errResult.ran || errResult.ok)
							{
								scriptFail("fixture_err should have failed");
							}
							else if (!reportsFileLine)
							{
								scriptFail("the error lacks file:line: " +
									errResult.error);
							}
							else if (gameObjectManager.objectExists("GhostCube"))
							{
								scriptFail("fixture_err left a partial edit");
							}
							else if (editorCore.getUndoStackSize() != undoBefore)
							{
								scriptFail("a failed tool touched the undo stack");
							}
							else
							{
								// RUN the SECURITY fixture: the editor sandbox
								// must deny io/os-process/require/load/dofile/
								// debug (its asserts hold -> a clean run)
								const Orkige::EditorScriptHost::RunResult secResult =
									editorScripts.runToolByName("fixture_security",
										controlContext);
								// RUN the shipped SAMPLE tool (roller frame)
								const Orkige::EditorScriptHost::RunResult sample =
									editorScripts.runToolByName("border_walls",
										controlContext);
								if (!secResult.ran || !secResult.ok)
								{
									scriptFail("the editor sandbox did not deny an "
										"unsafe global: " + secResult.error);
								}
								else if (!sample.ran || !sample.ok)
								{
									scriptFail("the shipped border_walls tool "
										"failed: " + sample.error);
								}
								else if (editorCore.getUndoStackSize() != 1)
								{
									scriptFail("the sample tool is not one undo "
										"step");
								}
								else
								{
									SDL_Log("orkige_editor: editor-scripts test "
										"- OK (discovery, editor.* verbs, one "
										"undo step, error rollback, sample tool)");
									std::error_code cleanupErr;
									std::filesystem::remove_all(
										scriptTestTempRoot, cleanupErr);
									running = false;
								}
							}
						}
					}
				}
			}

			// --- autosave + backup + component copy/paste selfcheck ---
			// (ORKIGE_EDITOR_AUTOSAVETEST=1) drives the WIRED paths on a temp
			// loose scene: a clean save, a dirty autosave sibling, a clean save
			// removing the autosave + writing a .bak, an automated-run stale
			// autosave open that auto-discards (no modal), and a component
			// copy/paste (one undo step). Single-shot, condition-driven.
			if (autosaveTestEnv && frameCount == 10)
			{
				auto autosaveFail = [&](std::string const& why)
				{
					SDL_Log("orkige_editor: autosave test - FAILED: %s",
						why.c_str());
					exitCode = 15;
					std::error_code ce;
					if (!autosaveTestDir.empty())
					{
						std::filesystem::remove_all(autosaveTestDir, ce);
					}
					running = false;
				};
				std::error_code ec;
				autosaveTestDir = (std::filesystem::temp_directory_path() /
					("orkige_autosavetest_" + std::to_string(
						std::chrono::steady_clock::now()
							.time_since_epoch().count()))).string();
				std::filesystem::create_directories(autosaveTestDir, ec);
				const std::string scenePath =
					(std::filesystem::path(autosaveTestDir) / "level.oscene")
						.string();
				const std::string autosavePath =
					Orkige::EditorAutosave::autosavePath(scenePath);
				const std::string backupPath =
					Orkige::EditorAutosave::backupPath(scenePath);

				// clean baseline: a fresh scene with one object, saved to disk
				newScene(state, editorCore);
				editorCore.createCube();	// dirties
				if (!saveSceneToPath(state, editorCore, scenePath) ||
					!std::filesystem::exists(scenePath))
				{
					autosaveFail("initial save did not write the scene");
				}
				else if (editorCore.isSceneDirty())
				{
					autosaveFail("save did not clear the dirty flag");
				}
				else if (std::filesystem::exists(autosavePath))
				{
					autosaveFail("a clean save left an autosave file");
				}
				// (a) a dirty scene autosaves to the SIBLING (never the file,
				// dirty flag preserved)
				else if (editorCore.createCube(), !editorCore.isSceneDirty())
				{
					autosaveFail("edit did not dirty the scene");
				}
				else if (!writeSceneAutosave(state, editorCore) ||
					!std::filesystem::exists(autosavePath))
				{
					autosaveFail("autosave did not appear");
				}
				else if (!editorCore.isSceneDirty())
				{
					autosaveFail("autosave cleared the dirty flag (must not)");
				}
				// (b) a clean save removes the autosave AND writes a .bak
				else if (!saveSceneToPath(state, editorCore, scenePath))
				{
					autosaveFail("second save failed");
				}
				else if (std::filesystem::exists(autosavePath))
				{
					autosaveFail("clean save did not remove the autosave");
				}
				else if (!std::filesystem::exists(backupPath))
				{
					autosaveFail("save did not create a .bak");
				}
				else
				{
					// (c) an automated-run open with a STALE (newer) autosave
					// auto-discards silently - no modal - and loads the real
					// scene, not the autosave content
					std::ofstream(autosavePath) << "not a real scene";
					std::filesystem::last_write_time(autosavePath,
						std::filesystem::last_write_time(scenePath) +
							std::chrono::seconds(5));
					const std::size_t before =
						gameObjectManager.getGameObjects().size();
					const bool opened =
						openSceneFromPath(state, editorCore, scenePath);
					if (!opened)
					{
						autosaveFail("stale-autosave open failed to load");
					}
					else if (state.openAutosaveRecoveryPopup)
					{
						autosaveFail("automated run raised the recovery modal");
					}
					else if (std::filesystem::exists(autosavePath))
					{
						autosaveFail("automated run did not auto-discard the "
							"stale autosave");
					}
					else if (gameObjectManager.getGameObjects().size() != before)
					{
						autosaveFail("the real scene did not load");
					}
					else
					{
						// (d) component copy/paste on two real objects, one undo
						// step that reverts. Pick two objects carrying a
						// TransformComponent.
						Orkige::StringVector withTransform;
						for (auto const& [id, go] :
							gameObjectManager.getGameObjects())
						{
							if (go->hasComponent<Orkige::TransformComponent>())
							{
								withTransform.push_back(id);
							}
						}
						if (withTransform.size() < 2)
						{
							autosaveFail("need two transform objects to test "
								"copy/paste");
						}
						else
						{
							const std::string& a = withTransform[0];
							const std::string& b = withTransform[1];
							editorCore.setObjectProperty(a,
								"TransformComponent", "position", "1 2 3");
							std::string want;
							editorCore.getObjectProperty(a,
								"TransformComponent", "position", want);
							const std::size_t undoBefore =
								editorCore.getUndoStackSize();
							std::string bBefore;
							editorCore.getObjectProperty(b,
								"TransformComponent", "position", bBefore);
							bool ok = editorCore.copyComponent(a,
								"TransformComponent") &&
								editorCore.pasteComponent(b);
							std::string bAfter;
							editorCore.getObjectProperty(b,
								"TransformComponent", "position", bAfter);
							if (!ok || bAfter != want)
							{
								autosaveFail("paste did not copy the values (" +
									bAfter + " != " + want + ")");
							}
							else if (editorCore.getUndoStackSize() !=
								undoBefore + 1)
							{
								autosaveFail("paste is not ONE undo step");
							}
							else if (!editorCore.undo())
							{
								autosaveFail("undo of paste failed");
							}
							else
							{
								std::string bReverted;
								editorCore.getObjectProperty(b,
									"TransformComponent", "position", bReverted);
								if (bReverted != bBefore)
								{
									autosaveFail("undo did not revert the paste");
								}
								else
								{
									SDL_Log("orkige_editor: autosave test - OK "
										"(autosave sibling, .bak on save, "
										"stale-open auto-discard, component "
										"copy/paste one-undo)");
									std::filesystem::remove_all(autosaveTestDir,
										ec);
									running = false;
								}
							}
						}
					}
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
					if (copyObject &&
						copyObject->hasComponent<Orkige::ModelComponent>())
					{
						SDL_Log("orkige_editor: edittest copy mesh name '%s'",
							copyObject
								->getComponentPtr<Orkige::ModelComponent>()
								->getCurrentModelFileName().c_str());
					}
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
				if (frameCount == 107)
				{
					// GENERIC reflection-driven property edit:
					// the auto Inspector routes every edit through
					// EditorCore::applyPropertyChange - ONE undoable
					// PropertyChangeCommand, values as PropertyValue canonical
					// strings, resolved+applied through the schema's reflected
					// setter. Prove apply + undo through that path AND that the
					// reflected setter takes effect LIVE (a scalar move + a mesh
					// reload, the viewport-live guarantee).
					std::string posBefore;
					require(editorCore.getObjectProperty("Cube1",
						"TransformComponent", "position", posBefore),
						"read reflected position");
					require(editorCore.applyPropertyChange("Cube1",
						"TransformComponent", "position", posBefore, "3 4 5"),
						"generic property change applied");
					Orkige::EditorTransform moved;
					require(editorCore.getObjectTransform("Cube1", moved) &&
						positionsEqual(moved.position,
							Orkige::Vec3(3.0f, 4.0f, 5.0f)),
						"reflected setter moved the node");
					require(editorCore.getUndoDescription() ==
						"Change TransformComponent.position",
						"generic undo description");
					require(editorCore.undo(),
						"undo generic property change");
					std::string posNow;
					require(editorCore.getObjectProperty("Cube1",
						"TransformComponent", "position", posNow) &&
						posNow == posBefore,
						"generic property change undone");
					// a mesh AssetRef edit through the SAME generic path reloads
					// the entity live - the reflected setter routes to
					// ModelComponent's real accessor (loadModel)
					optr<Orkige::GameObject> cube2b =
						gameObjectManager.getGameObject("Cube2").lock();
					Orkige::ModelComponent* model2 = cube2b
						->getComponentPtr<Orkige::ModelComponent>();
					std::string meshBefore;
					require(editorCore.getObjectProperty("Cube2",
						"ModelComponent", "mesh", meshBefore),
						"read reflected mesh");
					require(editorCore.applyPropertyChange("Cube2",
						"ModelComponent", "mesh", meshBefore, "test_mesh.glb"),
						"generic mesh change applied");
					require(model2->getCurrentModelFileName() ==
						"test_mesh.glb" &&
						model2->getMeshInstance() != nullptr,
						"reflected mesh setter reloaded the entity live");
					require(editorCore.undo(), "undo generic mesh change");
					require(model2->getMeshInstance() != nullptr,
						"mesh restored + entity loaded");
					SDL_Log("orkige_editor: edittest frame 107 - generic "
						"property edit (apply+undo, live setter) OK");
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
					// double-click focus (select + frame): drive the EXACT function
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
						"focus OK");
				}
				if (frameCount == 135)
				{
					// GameObject tree + active state (what the Hierarchy
					// panel's drag & drop / checkbox UI drives): re-parent
					// through the EXACT function a hierarchy drop calls,
					// verify the render-node composition (moving the parent
					// moves the child's WORLD pose), group, deactivate,
					// undo/redo, and the scene round-trip of both fields
					Orkige::Vec3 worldBefore = Orkige::Vec3::ZERO;
					std::string groupId;
					// scoped: the strong GameObject refs MUST release before
					// the reload below - a surviving ref would keep the old
					// (named) scene nodes alive through the world clear
					{
					optr<Orkige::GameObject> cube2 =
						gameObjectManager.getGameObject("Cube2").lock();
					require(cube2 != nullptr, "Cube2 exists");
					Orkige::TransformComponent* cube2Transform = cube2
						->getComponentPtr<Orkige::TransformComponent>();
					worldBefore = cube2Transform->getWorldPosition();
					require(editorCore.canReparent("Cube2", "Cube1") &&
						!editorCore.canReparent("Cube2", "Cube2"),
						"reparent validation");
					require(editorCore.reparentObject("Cube2", "Cube1"),
						"reparent");
					require(cube2->getParentId() == "Cube1",
						"parent link set");
					require(cube2Transform->getWorldPosition()
						.positionEquals(worldBefore, 1e-3f),
						"world position preserved across reparent");
					// the render graph composes: moving the parent moves the
					// child's world pose by the same delta
					const Orkige::Vec3 parentDelta(2.0f, 0.0f, 0.0f);
					Orkige::EditorTransform cube1Now;
					require(editorCore.getObjectTransform("Cube1", cube1Now),
						"Cube1 transform");
					Orkige::EditorTransform cube1Moved = cube1Now;
					cube1Moved.position += parentDelta;
					require(editorCore.applyTransformChange("Cube1",
						cube1Now, cube1Moved), "move parent");
					require(cube2Transform->getWorldPosition()
						.positionEquals(worldBefore + parentDelta, 1e-3f),
						"child world pose follows the parent");
					require(editorCore.undo(), "undo parent move");
					// undo the reparent: back to a root, world pose restored
					require(editorCore.undo(), "undo reparent");
					require(cube2->getParentId().empty(),
						"reparent undone");
					require(cube2Transform->getWorldPosition()
						.positionEquals(worldBefore, 1e-3f),
						"world position preserved across undo");
					// group Cube1+Cube2 under a fresh empty parent (Cmd+G)
					editorCore.selectObject("Cube1");
					editorCore.toggleSelection("Cube2");
					const std::size_t depthBefore =
						editorCore.getUndoStackSize();
					require(editorCore.groupSelected(), "group selection");
					require(editorCore.getUndoStackSize() == depthBefore + 1,
						"group = one undo step");
					groupId = editorCore.getSelectedObjectId();
					require(!groupId.empty() &&
						gameObjectManager.objectExists(groupId), "group exists");
					optr<Orkige::GameObject> group =
						gameObjectManager.getGameObject(groupId).lock();
					require(group->hasComponent<Orkige::TransformComponent>(),
						"group carries a transform");
					require(gameObjectManager.getGameObject("Cube1").lock()
						->getParentId() == groupId &&
						cube2->getParentId() == groupId, "members grouped");
					require(cube2Transform->getWorldPosition()
						.positionEquals(worldBefore, 1e-3f),
						"grouping kept the world pose");
					// active state: deactivating the group hides the members'
					// render content and gates their updates
					require(editorCore.setObjectActive(groupId, false),
						"deactivate group");
					require(!group->isActiveSelf() &&
						!cube2->isActiveInHierarchy() &&
						cube2->isActiveSelf(), "active state propagated");
					require(editorCore.undo(), "undo deactivate");
					require(cube2->isActiveInHierarchy(), "reactivated");
					require(editorCore.setObjectActive("Cube2", false),
						"deactivate member");
					}	// release the strong refs before the reload
					// persistence: parent links + active flags survive the
					// scene round-trip (v2 fields)
					const char* editScene =
						std::getenv("ORKIGE_EDITOR_EDITTEST_SCENE");
					const std::string scenePath =
						editScene ? editScene : "edittest.oscene";
					require(saveSceneToPath(state, editorCore, scenePath),
						"save tree scene");
					require(openSceneFromPath(state, editorCore, scenePath),
						"reload tree scene");
					optr<Orkige::GameObject> reloadedCube2 =
						gameObjectManager.getGameObject("Cube2").lock();
					require(reloadedCube2 &&
						reloadedCube2->getParentId() == groupId,
						"parent link persisted");
					require(!reloadedCube2->isActiveSelf() &&
						!reloadedCube2->isActiveInHierarchy(),
						"active flag persisted");
					require(reloadedCube2
						->getComponentPtr<Orkige::TransformComponent>()
						->getWorldPosition().positionEquals(worldBefore,
							1e-3f), "world pose persisted through the tree");
					SDL_Log("orkige_editor: edittest frame 135 - hierarchy + "
						"active state OK");
				}
				if (frameCount == 140)
				{
					// 2D editor mode: flip to 2D and apply the 2D
					// camera directly (drawScenePanel does the same next frame).
					// It reconfigures the editor's OWN camera - orthographic,
					// looking straight down -Z at the XY plane - and touches NO
					// scene object. Frame a root cube so the frame-145 pick is
					// deterministic.
					viewSettings.editor2D = true;
					Orkige::EditorTransform cube3;
					require(editorCore.getObjectTransform("Cube3", cube3),
						"Cube3 transform");
					state.camera.target = cube3.position;
					state.camera.distance = 10.0f;
					apply2DCamera(state, sceneTarget.camera, sceneCameraNode);
					require(sceneTarget.camera->getProjectionType() ==
						Orkige::RenderCamera::PT_ORTHOGRAPHIC,
						"editor camera became orthographic");
					require(orientationsEqual(sceneCameraNode->getOrientation(),
						Orkige::Quat::IDENTITY),
						"2D view looks straight down -Z (XY plane)");
					require(sceneCameraNode->getPosition().z >
						cube3.position.z + 1.0f,
						"2D camera stands off on +Z");
					// a scripted move in 2D goes through the SAME undoable
					// TransformChangeCommand and must land on the XY plane (its
					// world Z unchanged - Cube3 is a root, so local == world)
					Orkige::EditorTransform after = cube3;
					after.position = Orkige::Vec3(cube3.position.x + 1.5f,
						cube3.position.y + 1.0f, cube3.position.z);
					const std::size_t undoBefore =
						editorCore.getUndoStackSize();
					require(editorCore.applyTransformChange("Cube3", cube3,
						after), "2D move via TransformChangeCommand");
					require(editorCore.getUndoStackSize() == undoBefore + 1,
						"2D move is one undo step");
					Orkige::EditorTransform now;
					require(editorCore.getObjectTransform("Cube3", now) &&
						positionsEqual(now.position, after.position) &&
						std::abs(now.position.z - cube3.position.z) < 1e-4f,
						"2D move kept Z on the plane");
					// re-frame onto the moved object for the pick
					state.camera.target = now.position;
					apply2DCamera(state, sceneTarget.camera, sceneCameraNode);
					SDL_Log("orkige_editor: edittest frame 140 - 2D camera + "
						"planar move OK");
				}
				if (frameCount == 145)
				{
					// click-pick must work under the orthographic projection too
					// (the facade builds a projection-correct ray for an ortho
					// camera on BOTH render flavors - the WYSIWYG rule). Project
					// Cube3 to the panel and run the exact pick path the mouse
					// uses. The 2D camera is already applied by drawScenePanel
					// (editor2D has been on since frame 140) and its bounds
					// updated by the frame's render - querying here is safe,
					// exactly like the frame-45 selfcheck pick.
					editorCore.clearSelection();
					require(pickGameObjectThroughScenePanel(editorCore,
						gameObjectManager, sceneTarget.camera, "Cube3"),
						"2D pick projected");
					require(editorCore.getSelectedObjectId() == "Cube3",
						"2D click-pick selected Cube3");
					SDL_Log("orkige_editor: edittest frame 145 - 2D click-pick "
						"OK -> edittest PASSED");
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

			// live theme-switch selfcheck: pin Dark then Light through the same
			// path the settings toggle uses (viewSettings.themeMode +
			// themeReapplyRequested), asserting the active variant follows; then
			// return to System (variant is OS-dependent, so only assert no crash
			// and a valid resolve)
			if (themeSwitchTest)
			{
				auto forceTheme = [&](Orkige::EditorThemeMode mode)
				{
					viewSettings.themeMode = mode;
					state.themeReapplyRequested = true;
				};
				if (frameCount == 10)
				{
					forceTheme(Orkige::EditorThemeMode::Dark);
				}
				else if (frameCount == 12 &&
					Orkige::currentEditorThemeVariant() !=
						Orkige::EditorThemeVariant::Dark)
				{
					SDL_Log("orkige_editor: FAILED theme switch (Dark)");
					exitCode = 8;
					running = false;
				}
				else if (frameCount == 20)
				{
					forceTheme(Orkige::EditorThemeMode::Light);
				}
				else if (frameCount == 22 &&
					Orkige::currentEditorThemeVariant() !=
						Orkige::EditorThemeVariant::Light)
				{
					SDL_Log("orkige_editor: FAILED theme switch (Light)");
					exitCode = 8;
					running = false;
				}
				else if (frameCount == 30)
				{
					forceTheme(Orkige::EditorThemeMode::System);
				}
				else if (frameCount == 40)
				{
					SDL_Log("orkige_editor: theme switch selfcheck OK");
					running = false;
				}
			}

			// --- live human preview panels (real docking + texture submission) ---
			if (previewSelfcheckEnv && frameCount == 10)
			{
				if (!openProjectFromPath(state, editorCore, previewSelfcheckEnv))
				{
					SDL_Log("orkige_editor: FAILED preview selfcheck (open project)");
					exitCode = 13;
					running = false;
				}
			}
			if (previewSelfcheckEnv && frameCount == 20 && exitCode == 0)
			{
				// select the .oanim so the Inspector's animation section stages
				// it in animPreviewStage (the standalone Animation Preview panel
				// was retired - the Inspector owns this preview now)
				editorCore.clearSelection();
				state.assetBrowser.selection.clear();
				state.assetBrowser.selection.insert("assets/lottie/hamster.oanim");
				state.assetBrowser.selectionAnchor = "assets/lottie/hamster.oanim";
				viewSettings.showInspectorPanel = true;
				// show the asset's folder so the browser keeps it selected (a
				// selection is pruned when the shown folder lacks it) - the
				// Inspector animation preview reads the browser selection
				state.assetBrowser.currentDir =
					state.project.getAssetsDirectory() + "/lottie";
			}
			if (previewSelfcheckEnv && frameCount == 30 && exitCode == 0)
			{
				auto drawDataUses = [](ImTextureID wanted)
				{
					ImDrawData* draw = ImGui::GetDrawData();
					if (!draw || wanted == 0) return false;
					for (int list = 0; list < draw->CmdListsCount; ++list)
					{
						for (ImDrawCmd const& command :
							draw->CmdLists[list]->CmdBuffer)
						{
							if (command.GetTexID() == wanted) return true;
						}
					}
					return false;
				};
				const OrkigeEditor::AnimationPreviewInfo info =
					animPreviewStage.getInfo();
				const std::string firstUpload =
					animPreviewStage.getActiveUploadName();
				unsigned int textureW = 0, textureH = 0;
				const bool textureExists = !firstUpload.empty() &&
					Orkige::RenderSystem::get()->getTextureSize(firstUpload,
						textureW, textureH);
				const ImTextureID animationTexture = gImGuiRenderer
					? gImGuiRenderer->textureIdForResource(firstUpload) : 0;
				animPreviewStage.setTimeSeconds(0.25f);
				const std::string deferredUpload =
					animPreviewStage.uploadTexture();
				const std::string secondUpload =
					animPreviewStage.uploadTexture();
				const std::string stableUpload =
					animPreviewStage.uploadTexture();
				const bool animationOk = animPreviewStage.isLoaded() &&
					info.visiblePixelCount > 100 && info.colouredPixelCount > 100 &&
					textureExists && textureW == 256 && textureH == 256 &&
					drawDataUses(animationTexture) &&
					deferredUpload == firstUpload &&
					!secondUpload.empty() && secondUpload != firstUpload &&
					stableUpload == secondUpload;
				SDL_Log("orkige_editor: preview selfcheck - animation live image %s "
					"(%zu visible, %zu coloured)", animationOk ? "OK" : "FAILED",
					info.visiblePixelCount, info.colouredPixelCount);
				if (!animationOk)
				{
					exitCode = 13;
					running = false;
				}
				else
				{
					// select hud.oui so the Inspector shows its GUI-screen thumbnail
					// + Open Preview button (the full panel is closed by default)
					editorCore.clearSelection();
					state.assetBrowser.selection.clear();
					state.assetBrowser.selection.insert("assets/hud.oui");
					state.assetBrowser.selectionAnchor = "assets/hud.oui";
					state.assetBrowser.currentDir =
						state.project.getAssetsDirectory();
					viewSettings.showInspectorPanel = true;
				}
			}
			if (previewSelfcheckEnv && frameCount == 32 && exitCode == 0)
			{
				if (const char* shot =
					std::getenv("ORKIGE_EDITOR_INSPECTOR_PREVIEW_DIR"))
				{
					render->saveWindowContents(std::string(shot) + "/oui_inspector.png");
				}
			}
			if (previewSelfcheckEnv && frameCount == 34 && exitCode == 0)
			{
				// the Inspector baked the .oui GUI-screen thumbnail (a real texture,
				// not the placeholder); then the Open Preview button opens the panel
				unsigned int ouiTw = 0;
				unsigned int ouiTh = 0;
				const bool ouiThumbOk =
					!state.assetBrowser.ouiPreviewUpload.empty() &&
					Orkige::RenderSystem::get()->getTextureSize(
						state.assetBrowser.ouiPreviewUpload, ouiTw, ouiTh) &&
					ouiTw > 0 && ouiTh > 0;
				SDL_Log("orkige_editor: preview selfcheck - .oui inspector thumbnail "
					"%s (%ux%u)", ouiThumbOk ? "OK" : "FAILED", ouiTw, ouiTh);
				if (!ouiThumbOk)
				{
					exitCode = 13;
					running = false;
				}
				else
				{
					// the Open Preview button opens the full panel on this screen
					state.requestedGuiPreviewAsset = "assets/hud.oui";
					viewSettings.showGuiPreviewPanel = true;
				}
			}
			if (previewSelfcheckEnv && frameCount == 40 && exitCode == 0)
			{
				auto drawDataUses = [](ImTextureID wanted)
				{
					ImDrawData* draw = ImGui::GetDrawData();
					if (!draw || wanted == 0) return false;
					for (int list = 0; list < draw->CmdListsCount; ++list)
						for (ImDrawCmd const& command :
							draw->CmdLists[list]->CmdBuffer)
							if (command.GetTexID() == wanted) return true;
					return false;
				};
				const Orkige::optr<Orkige::RenderTexture> guiTarget =
					guiPreviewStage.getTarget();
				const ImTextureID guiTexture = gImGuiRenderer
					? gImGuiRenderer->textureIdFor(guiTarget) : 0;
				ImGuiWindow* sceneWindow = ImGui::FindWindowByName("Scene");
				ImGuiWindow* guiWindow = ImGui::FindWindowByName("GuiPreview");
				const bool guiOk = guiPreviewStage.isLoaded() && guiTarget &&
					guiPreviewStage.getLastBatchCount() > 0 &&
					drawDataUses(guiTexture) && sceneWindow && guiWindow &&
					sceneWindow->DockId != 0 &&
					guiWindow->DockId == sceneWindow->DockId;
				SDL_Log("orkige_editor: preview selfcheck - GUI live image %s "
					"(%zu batches)", guiOk ? "OK" : "FAILED",
					guiPreviewStage.getLastBatchCount());
				if (!guiOk)
				{
					exitCode = 13;
					running = false;
				}
			}
			if (previewSelfcheckEnv && frameCount == 41 && exitCode == 0)
			{
				if (const char* shot =
					std::getenv("ORKIGE_EDITOR_INSPECTOR_PREVIEW_DIR"))
				{
					render->saveWindowContents(std::string(shot) + "/oui_panel.png");
				}
			}
			// Device-preset SWITCH stress: selecting a different phone in the
			// GUI Preview combo setContext()s + re-show()s,
			// tearing the preview surface down and rebuilding it while the dying
			// incarnation's UI camera still lives (2D batches hold the old target
			// an extra frame). A stable camera name collides on createCamera ->
			// abort; per-incarnation naming must let N switches rebuild without a
			// crash and still render.
			if (previewSelfcheckEnv && exitCode == 0 &&
				(frameCount == 42 || frameCount == 44 || frameCount == 46))
			{
				OrkigeEditor::GuiPreviewContext ctx =
					guiPreviewStage.getContext();
				if (frameCount == 42)
				{
					ctx.width = 1284; ctx.height = 2778; ctx.contentScale = 3.0f;
				}
				else if (frameCount == 44)
				{
					ctx.width = 2048; ctx.height = 1536; ctx.contentScale = 2.0f;
				}
				else
				{
					ctx.width = 1170; ctx.height = 2532; ctx.contentScale = 3.0f;
				}
				guiPreviewStage.setContext(ctx);
				std::string switchError;
				const bool shown = guiPreviewStage.show(
					state.project.getRootDirectory(), "assets/hud.oui",
					switchError);
				const bool switchOk = shown && guiPreviewStage.isLoaded() &&
					guiPreviewStage.getTarget();
				SDL_Log("orkige_editor: preview selfcheck - device switch %ux%u "
					"%s (%s)", ctx.width, ctx.height, switchOk ? "OK" : "FAILED",
					switchError.c_str());
				if (!switchOk)
				{
					exitCode = 13;
					running = false;
				}
			}
			if (previewSelfcheckEnv && frameCount == 48 && exitCode == 0)
			{
				// reaching here at all proves no duplicate-camera abort; the
				// rebuilt surface must also still submit its batch
				const bool recovered = guiPreviewStage.isLoaded() &&
					guiPreviewStage.getTarget() &&
					guiPreviewStage.getLastBatchCount() > 0;
				SDL_Log("orkige_editor: preview selfcheck - survived device "
					"switches %s (%zu batches)", recovered ? "OK" : "FAILED",
					guiPreviewStage.getLastBatchCount());
				if (!recovered)
				{
					exitCode = 13;
					running = false;
				}
			}
			// --- Inspector asset previews: seed the browser selection to each
			// asset kind (not MCP-drivable) and prove the sections render. The
			// optional ORKIGE_EDITOR_INSPECTOR_PREVIEW_DIR writes preview PNGs
			// for eyeball verification; the ctest leaves it unset. ---
			if (previewSelfcheckEnv && exitCode == 0)
			{
				const char* previewShotDir =
					std::getenv("ORKIGE_EDITOR_INSPECTOR_PREVIEW_DIR");
				auto seedSelection = [&](const char* relPath)
				{
					editorCore.clearSelection();
					state.assetBrowser.selection.clear();
					state.assetBrowser.selection.insert(relPath);
					viewSettings.showInspectorPanel = true;
					// show the asset's own folder so the browser keeps it selected
					// (a selection is pruned when the shown folder lacks it)
					state.assetBrowser.currentDir = (std::filesystem::path(
						state.project.getRootDirectory()) /
						std::filesystem::path(relPath).parent_path()).string();
				};
				if (frameCount == 50)
				{
					seedSelection("assets/demo_material_cube.glb");
				}
				else if (frameCount == 56)
				{
					const OrkigeEditor::MeshPreviewInfo info =
						meshPreviewStage.getInfo();
					const bool meshOk = info.loaded && info.subMeshCount > 0 &&
						info.boundingRadius > 0.0f && meshPreviewStage.getTarget();
					SDL_Log("orkige_editor: inspector preview - .glb mesh %s "
						"(%d sub-meshes, radius %.2f)", meshOk ? "OK" : "FAILED",
						info.subMeshCount, info.boundingRadius);
					if (previewShotDir && meshOk)
					{
						// the RTT auto-rendered over the prior frames; write what
						// it holds (no extra renderOneFrame mid ImGui frame)
						meshPreviewStage.getTarget()->writeContentsToFile(
							std::string(previewShotDir) + "/mesh_preview.png");
						render->saveWindowContents(
							std::string(previewShotDir) + "/inspector_mesh.png");
					}
					if (!meshOk) { exitCode = 13; running = false; }
				}
				else if (frameCount == 60)
				{
					seedSelection("assets/demo_material.omat");
				}
				else if (frameCount == 66)
				{
					const bool matOk = !state.assetBrowser.editMaterialPath.empty()
						&& meshPreviewStage.getTarget() &&
						meshPreviewStage.getInfo().loaded;
					SDL_Log("orkige_editor: inspector preview - .omat editor %s "
						"(albedo %.2f/%.2f/%.2f)", matOk ? "OK" : "FAILED",
						state.assetBrowser.editMaterial.albedo.r,
						state.assetBrowser.editMaterial.albedo.g,
						state.assetBrowser.editMaterial.albedo.b);
					if (previewShotDir && matOk)
					{
						meshPreviewStage.getTarget()->writeContentsToFile(
							std::string(previewShotDir) + "/material_preview.png");
						render->saveWindowContents(
							std::string(previewShotDir) + "/inspector_material.png");
					}
					if (!matOk) { exitCode = 13; running = false; }
				}
				else if (frameCount == 70)
				{
					seedSelection("assets/blob.oshape");
				}
				else if (frameCount == 74)
				{
					const bool shapeOk =
						state.assetBrowser.shapePreviewVertices > 0 &&
						state.assetBrowser.shapePreviewTriangles > 0 &&
						!state.assetBrowser.shapePreviewUpload.empty();
					SDL_Log("orkige_editor: inspector preview - .oshape fill %s "
						"(%d verts, %d tris)", shapeOk ? "OK" : "FAILED",
						state.assetBrowser.shapePreviewVertices,
						state.assetBrowser.shapePreviewTriangles);
					if (previewShotDir && shapeOk)
					{
						render->saveWindowContents(
							std::string(previewShotDir) + "/inspector_shape.png");
					}
					if (!shapeOk) { exitCode = 13; running = false; }
				}
				else if (frameCount == 78)
				{
					seedSelection("assets/particle_dot.png");
				}
				else if (frameCount == 82)
				{
					// the texture section always draws for a texture; assert it
					// stayed the shown section and did not disturb the mesh stage
					const bool texOk =
						!state.assetBrowser.editImportPath.empty() ||
						classifyAsset("assets/particle_dot.png") ==
							AssetKind::Texture;
					SDL_Log("orkige_editor: inspector preview - texture section %s",
						texOk ? "OK" : "FAILED");
					if (previewShotDir && texOk)
					{
						render->saveWindowContents(
							std::string(previewShotDir) + "/inspector_texture.png");
					}
					if (!texOk) { exitCode = 13; }
					SDL_Log("orkige_editor: inspector asset previews done (exit %d)",
						exitCode);
					// when capturing evidence, continue to the asset-browser
					// thumbnail shot below; otherwise stop here (the ctest path)
					if (!previewShotDir) { running = false; }
				}
				// asset-browser thumbnail evidence (capture-only): navigate to the
				// assets folder so the .glb/.omat tiles draw + bake, then dump the
				// whole window once the baked thumbnails are on screen
				else if (previewShotDir && frameCount == 84)
				{
					editorCore.clearSelection();
					state.assetBrowser.selection.clear();
					state.assetBrowser.currentDir =
						state.project.getAssetsDirectory();
					state.assetBrowser.thumbnailSize = 96.0f;
					viewSettings.showAssetBrowserPanel = true;
					viewSettings.showInspectorPanel = false;
					// the Assets panel shares the bottom dock node with Stats +
					// Console (tabs); hide the siblings so it is the visible tab
					viewSettings.showStatsPanel = false;
					viewSettings.showConsolePanel = false;
				}
				else if (previewShotDir && frameCount == 130)
				{
					// captured well after the navigate so the deferred baker (one
					// thumbnail per frame) has drained every visible .glb/.omat tile
					render->saveWindowContents(
						std::string(previewShotDir) + "/asset_browser_thumbs.png");
					SDL_Log("orkige_editor: asset-browser thumbnail shot written");
					running = false;
				}
			}

			// --- cubemap thumbnail survival (ORKIGE_EDITOR_CUBEMAP_SELFCHECK) ---
			if (cubemapSelfcheckEnv && frameCount == 10)
			{
				if (!openProjectFromPath(state, editorCore, cubemapSelfcheckEnv))
				{
					SDL_Log("orkige_editor: FAILED cubemap selfcheck (open project)");
					exitCode = 14;
					running = false;
				}
				else
				{
					// show the asset browser on the assets folder so the panel's
					// budgeted thumbnail service walks the cubemap tile through the
					// real paint path (the reported crash path)
					state.assetBrowser.currentDir =
						state.project.getAssetsDirectory();
					viewSettings.showAssetBrowserPanel = true;
					viewSettings.showInspectorPanel = false;
				}
			}
			if (cubemapSelfcheckEnv && frameCount == 30 && exitCode == 0)
			{
				// reaching here proves the browser SURVIVED drawing/servicing the
				// cubemap tile (pre-fix: an aborted mid-map staging texture
				// SIGABRTs the process). A cubemap cannot back a 2D thumbnail, so
				// its tile resolves to id 0 (the kind glyph) - assert that through
				// the SAME free function the panel's thumbnail service calls.
				const std::string cubemap = (std::filesystem::path(
					state.project.getAssetsDirectory()) / "sky_faces.dds").string();
				const bool isTexture =
					classifyAsset(cubemap) == AssetKind::Texture;
				const ImTextureID thumb = assetThumbnailFor(state, cubemap);
				const bool cubemapOk = isTexture && thumb == 0;
				SDL_Log("orkige_editor: cubemap selfcheck - thumbnail fallback %s "
					"(texture=%d, thumb=%s)", cubemapOk ? "OK" : "FAILED",
					isTexture ? 1 : 0, thumb == 0 ? "glyph" : "bound");
				if (!cubemapOk)
				{
					exitCode = 14;
				}
				running = false;
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
						// whole module (a fat OGRE-including TU + full link),
						// and a sanitizer-instrumented compiler needs several
						// times the plain build - the deadline only exists to
						// fail a HUNG build, so it errs far on the long side
						nativeDeadline = nativeNow + std::chrono::seconds(600);
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
							// surface the compiler evidence in the test log -
							// a bare "build failed" is undiagnosable on CI,
							// and the full unclassified tail carries a cmake
							// error's continuation-line message body
							SDL_Log("orkige_editor: native playtest - build "
								"error tail:\n%s\nfull build tail:\n%s",
								playSession.buildErrorLog.c_str(),
								playSession.buildOutputTail.c_str());
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
						// a simulator target may have to boot the device
						// (+ install the app) before the player can even
						// start - this deadline must cover the play
						// session's OWN prep + connect budgets
						// (PLAY_SIM_PREP_TIMEOUT_SECONDS +
						// PLAY_CONNECT_TIMEOUT_SECONDS) with slack, or the
						// test reports the vaguer of two failures
						playtestDeadline = playtestNow + std::chrono::seconds(
							playSession.simulatorUdid.empty() ? 60 :
								PLAY_SIM_PREP_TIMEOUT_SECONDS +
								PLAY_CONNECT_TIMEOUT_SECONDS + 70);
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
							// simulate a player crash: a forced kill, not
							// Stop - the editor must recover via the link
							// drop. SDL_KillProcess(force) is the abrupt
							// death on every platform (SIGKILL / a hard
							// process termination).
							if (!SDL_KillProcess(playSession.process, true))
							{
								playtestFailed = true;
								playtestFailure = std::string(
									"could not kill the player: ") +
									SDL_GetError();
							}
							else
							{
								SDL_Log("orkige_editor: playtest - force-"
									"killed the player process");
							}
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

			// --- scripted safe-area DEVICE run (ORKIGE_EDITOR_SAFEAREA_CHECK) -
			// plays a project on a booted iOS simulator and asserts, over the
			// debug protocol, that the notch top inset is reported > 0 and every
			// visible HUD widget sits inside the safe box. The simulator
			// selection earlier already exited 77 (skip) when no sim exists.
			if (safeAreaCheckEnv)
			{
				const std::chrono::steady_clock::time_point safeAreaNow =
					std::chrono::steady_clock::now();
				if (safeAreaPhase == SafeAreaPhase::Idle && frameCount == 10)
				{
					if (!openProjectFromPath(state, editorCore,
						safeAreaCheckEnv))
					{
						safeAreaFailed = true;
						safeAreaFailure = std::string("could not open project '")
							+ safeAreaCheckEnv + "'";
					}
				}
				else if (safeAreaPhase == SafeAreaPhase::Idle &&
					frameCount == 40)
				{
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						safeAreaFailed = true;
						safeAreaFailure = "startPlay failed";
					}
					else
					{
						safeAreaPhase = SafeAreaPhase::WaitStream;
						safeAreaDeadline =
							safeAreaNow + std::chrono::seconds(240);
					}
				}
				else if (safeAreaPhase == SafeAreaPhase::WaitStream)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						safeAreaFailed = true;
						safeAreaFailure =
							"play session ended before the safe-area stream";
					}
					// note once that the window/layout stream arrived, so a
					// timeout can distinguish "no stream" from "notch never
					// reported" (the specific failure)
					else if (playSession.remoteWindowW > 0 &&
						!playSession.remoteUiLayout.empty())
					{
						if (!safeAreaSawStream)
						{
							safeAreaSawStream = true;
							safeAreaStreamAt = safeAreaNow;
						}
						// HOLD until a POSITIVE top inset arrives: the first
						// MSG_STATS can carry a still-settling 0 before UIKit lays
						// the view out, so re-check each frame (bounded by the
						// settle window below) rather than failing on the first
						if (playSession.remoteSafeTop > 0)
						{
							const long long boxLeft = playSession.remoteSafeLeft;
							const long long boxTop = playSession.remoteSafeTop;
							const long long boxRight = playSession.remoteWindowW -
								playSession.remoteSafeRight;
							const long long boxBottom =
								playSession.remoteWindowH -
								playSession.remoteSafeBottom;
							for (auto const& widget : playSession.remoteUiLayout)
							{
								if (!widget.visible)
								{
									continue;
								}
								if (widget.left < boxLeft ||
									widget.top < boxTop ||
									(widget.left + widget.width) > boxRight ||
									(widget.top + widget.height) > boxBottom)
								{
									safeAreaFailed = true;
									safeAreaFailure = "widget '" + widget.id +
										"' lies outside the safe box";
									break;
								}
							}
							if (!safeAreaFailed)
							{
								SDL_Log("orkige_editor: safe-area device run - top "
									"inset %lld px, %zu HUD widgets inside the safe "
									"box", playSession.remoteSafeTop,
									playSession.remoteUiLayout.size());
							}
							requestStopPlay(playSession);
							safeAreaPhase = SafeAreaPhase::WaitStop;
							safeAreaDeadline =
								safeAreaNow + std::chrono::seconds(30);
						}
						else if (safeAreaNow - safeAreaStreamAt >=
							safeAreaInsetSettle)
						{
							// the stream has flowed for the whole settle window
							// but the top inset never went positive - report the
							// specific cause (not the generic deadline)
							safeAreaFailed = true;
							safeAreaFailure = "reported top safe-area inset is not "
								"> 0 (expected a notch on the iPhone 16 profile)";
						}
						// else: keep waiting for the notch inset to settle
					}
				}
				else if (safeAreaPhase == SafeAreaPhase::WaitStop)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						SDL_Log("orkige_editor: safe-area device run PASSED");
#ifdef __APPLE__
						if (playSession.simulatorBootedByEditor &&
							!playSession.simulatorUdid.empty())
						{
							const char* shutdownArgs[] = { "/usr/bin/xcrun",
								"simctl", "shutdown",
								playSession.simulatorUdid.c_str(), nullptr };
							std::string shutdownOutput;
							int shutdownExit = 0;
							runProcessCaptured(shutdownArgs, shutdownOutput,
								shutdownExit);
						}
#endif
						safeAreaPhase = SafeAreaPhase::Done;
						running = false;
					}
				}
				if (!safeAreaFailed &&
					safeAreaPhase != SafeAreaPhase::Idle &&
					safeAreaPhase != SafeAreaPhase::Done &&
					safeAreaNow >= safeAreaDeadline)
				{
					safeAreaFailed = true;
					safeAreaFailure = "deadline exceeded in safe-area phase " +
						std::to_string(static_cast<int>(safeAreaPhase));
				}
				if (safeAreaFailed)
				{
					SDL_Log("orkige_editor: safe-area device run FAILED - %s",
						safeAreaFailure.c_str());
					exitCode = 2;
					running = false;
				}
			}

			// --- scripted device-rotation run (ORKIGE_EDITOR_ROTATION_CHECK) -
			// plays a project whose manifest opts into device rotation
			// (export.orientation=auto), rotates the device and asserts over
			// the debug protocol that the drawable swapped orientation while
			// the player kept rendering, then rotates back. The device
			// selection earlier already exited 77 (skip) when no prepared
			// device exists.
			if (rotationCheckEnv)
			{
				const std::chrono::steady_clock::time_point rotationNow =
					std::chrono::steady_clock::now();
				// one adb settings call on the session's device (Android leg)
				auto adbSetting = [&playSession](
					std::vector<std::string> const& tail, std::string& output)
				{
					std::vector<std::string> command = { adbPath(), "-s",
						playSession.androidSerial, "shell", "settings" };
					command.insert(command.end(), tail.begin(), tail.end());
					int adbExit = 0;
					const bool ran = runProcessCaptured(command, output,
						adbExit) && adbExit == 0;
					// settings get answers with a trailing newline
					while (!output.empty() && (output.back() == '\n' ||
						output.back() == '\r'))
					{
						output.pop_back();
					}
					return ran;
				};
				// restore the Android rotation settings captured below -
				// runs on every exit path (pass, fail, skip)
				auto restoreAndroidRotation = [&]()
				{
					if (!rotationSettingsSaved ||
						playSession.androidSerial.empty())
					{
						return;
					}
					std::string ignored;
					adbSetting({ "put", "system", "user_rotation",
						rotationSavedUser.empty() ? "0" : rotationSavedUser },
						ignored);
					adbSetting({ "put", "system", "accelerometer_rotation",
						rotationSavedAccel.empty() ? "1" : rotationSavedAccel },
						ignored);
					rotationSettingsSaved = false;
				};
				// rotate the play device; false = the mechanism itself is
				// unavailable (the iOS osascript refusal -> SKIP)
				auto rotateDevice = [&](bool toLandscape) -> bool
				{
					if (!playSession.androidSerial.empty())
					{
						std::string output;
						if (!rotationSettingsSaved)
						{
							adbSetting({ "get", "system",
								"accelerometer_rotation" }, rotationSavedAccel);
							adbSetting({ "get", "system", "user_rotation" },
								rotationSavedUser);
							rotationSettingsSaved = true;
						}
						return adbSetting({ "put", "system",
								"accelerometer_rotation", "0" }, output) &&
							adbSetting({ "put", "system", "user_rotation",
								toLandscape ? "1" : "0" }, output);
					}
#ifdef __APPLE__
					// Simulator.app's Device menu: the only scriptable
					// rotation the simulator offers. Needs the Automation/
					// Accessibility permission - a refusal is an unprepared
					// machine, not a rotation defect.
					const std::string script =
						std::string("tell application \"System Events\" to "
							"tell process \"Simulator\" to click menu item \"")
						+ (toLandscape ? "Rotate Left" : "Rotate Right") +
						"\" of menu \"Device\" of menu bar item \"Device\" "
						"of menu bar 1";
					std::string output;
					int osaExit = 0;
					const bool ran = runProcessCaptured(
						{ "/usr/bin/osascript", "-e", script }, output,
						osaExit) && osaExit == 0;
					if (!ran)
					{
						oDebugWarn("editor.play", 0, "rotation check - "
							"osascript could not drive Simulator.app (" <<
							output << "), skipping");
					}
					return ran;
#else
					return false;
#endif
				};
				const bool rotationOnSimulator =
					playSession.androidSerial.empty();
				if (rotationPhase == RotationPhase::Idle && frameCount == 10)
				{
					if (!openProjectFromPath(state, editorCore,
						rotationCheckEnv))
					{
						rotationFailed = true;
						rotationFailure = std::string(
							"could not open project '") + rotationCheckEnv + "'";
					}
				}
				else if (rotationPhase == RotationPhase::Idle &&
					frameCount == 40)
				{
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						rotationFailed = true;
						rotationFailure = "startPlay failed";
					}
					else
					{
						rotationPhase = RotationPhase::WaitStream;
						rotationDeadline =
							rotationNow + std::chrono::seconds(240);
					}
				}
				else if (rotationPhase == RotationPhase::WaitStream)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						rotationFailed = true;
						rotationFailure =
							"play session ended before the stats stream";
					}
					else if (playSession.remoteWindowW > 0 &&
						playSession.remoteWindowH > 0 &&
						playSession.remoteWindowW != playSession.remoteWindowH)
					{
						rotationBaselineW = playSession.remoteWindowW;
						rotationBaselineH = playSession.remoteWindowH;
						if (!rotateDevice(rotationBaselineW <
							rotationBaselineH))
						{
							// the rotation mechanism is unavailable on this
							// machine - end the run as SKIPPED after a clean
							// stop (never a false red)
							rotationSkip = true;
							requestStopPlay(playSession);
							rotationPhase = RotationPhase::WaitStop;
							rotationDeadline =
								rotationNow + std::chrono::seconds(30);
						}
						else
						{
							SDL_Log("orkige_editor: rotation check - baseline "
								"%lldx%lld, rotating", rotationBaselineW,
								rotationBaselineH);
							rotationPhase = RotationPhase::WaitRotated;
							rotationDeadline =
								rotationNow + std::chrono::seconds(90);
						}
					}
				}
				else if (rotationPhase == RotationPhase::WaitRotated)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						rotationFailed = true;
						rotationFailure = "play session died during rotation";
					}
					// the flipped-orientation stats message is the proof the
					// resize reached the render system AND the player kept
					// rendering (the stream carries the CURRENT frame state)
					else if (playSession.remoteWindowW > 0 &&
						playSession.remoteWindowH > 0 &&
						(playSession.remoteWindowW >
							playSession.remoteWindowH) !=
						(rotationBaselineW > rotationBaselineH))
					{
						// on a notched simulator the inset must have moved to
						// a side edge; hold until the post-rotation insets
						// settle (they can lag the size by a beat)
						if (rotationOnSimulator &&
							playSession.remoteSafeLeft <= 0 &&
							playSession.remoteSafeRight <= 0)
						{
							// keep waiting within the phase deadline
						}
						else
						{
							SDL_Log("orkige_editor: rotation check - rotated "
								"to %lldx%lld (safe insets l %lld t %lld r "
								"%lld b %lld), rotating back",
								playSession.remoteWindowW,
								playSession.remoteWindowH,
								playSession.remoteSafeLeft,
								playSession.remoteSafeTop,
								playSession.remoteSafeRight,
								playSession.remoteSafeBottom);
							if (!rotateDevice(rotationBaselineW >=
								rotationBaselineH))
							{
								rotationFailed = true;
								rotationFailure =
									"rotate-back request failed";
							}
							else
							{
								rotationPhase = RotationPhase::WaitRestored;
								rotationDeadline = rotationNow +
									std::chrono::seconds(90);
							}
						}
					}
				}
				else if (rotationPhase == RotationPhase::WaitRestored)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						rotationFailed = true;
						rotationFailure =
							"play session died during the rotate-back";
					}
					else if (playSession.remoteWindowW ==
						rotationBaselineW &&
						playSession.remoteWindowH == rotationBaselineH)
					{
						requestStopPlay(playSession);
						rotationPhase = RotationPhase::WaitStop;
						rotationDeadline =
							rotationNow + std::chrono::seconds(30);
					}
				}
				else if (rotationPhase == RotationPhase::WaitStop)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						restoreAndroidRotation();
#ifdef __APPLE__
						if (playSession.simulatorBootedByEditor &&
							!playSession.simulatorUdid.empty())
						{
							const char* shutdownArgs[] = { "/usr/bin/xcrun",
								"simctl", "shutdown",
								playSession.simulatorUdid.c_str(), nullptr };
							std::string shutdownOutput;
							int shutdownExit = 0;
							runProcessCaptured(shutdownArgs, shutdownOutput,
								shutdownExit);
						}
#endif
						if (rotationSkip)
						{
							SDL_Log("orkige_editor: rotation check SKIPPED - "
								"no scriptable rotation on this machine");
							exitCode = 77;
						}
						else
						{
							SDL_Log("orkige_editor: rotation check PASSED");
						}
						rotationPhase = RotationPhase::Done;
						running = false;
					}
				}
				if (!rotationFailed &&
					rotationPhase != RotationPhase::Idle &&
					rotationPhase != RotationPhase::Done &&
					rotationNow >= rotationDeadline)
				{
					rotationFailed = true;
					rotationFailure = "deadline exceeded in rotation phase " +
						std::to_string(static_cast<int>(rotationPhase));
				}
				if (rotationFailed)
				{
					restoreAndroidRotation();
					SDL_Log("orkige_editor: rotation check FAILED - %s",
						rotationFailure.c_str());
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
			// --- scripted hot-reload playtest ------------------------------
			// (ORKIGE_EDITOR_HOTRELOAD_PLAYTEST, see the env block above)
			if (hotreloadPlaytestEnv)
			{
				const std::chrono::steady_clock::time_point hotreloadNow =
					std::chrono::steady_clock::now();
				bool hotreloadFailed = false;
				std::string hotreloadFailure;
				if (frameCount == 10 &&
					hotreloadPhase == HotReloadPlaytestPhase::Idle)
				{
					// work on a temp COPY - the real fixture is never touched
					hotreloadTempRoot =
						(std::filesystem::temp_directory_path() /
						("orkige_hotreload_" + std::to_string(
							std::chrono::steady_clock::now()
								.time_since_epoch().count()))).string();
					std::error_code copyError;
					std::filesystem::copy(hotreloadPlaytestEnv,
						hotreloadTempRoot,
						std::filesystem::copy_options::recursive, copyError);
					// baseline the script so init parks the Probe at x=1 - the
					// player loads THIS before the first edit
					const bool baselineOk = hotreloadWriteScript(
						hotreloadMoveScript(1));
					if (copyError || !baselineOk)
					{
						hotreloadFailed = true;
						hotreloadFailure = "could not prepare the temp copy at " +
							hotreloadTempRoot;
					}
					else if (!openProjectFromPath(state, editorCore,
						hotreloadTempRoot))
					{
						hotreloadFailed = true;
						hotreloadFailure = "could not open the project copy '" +
							hotreloadTempRoot + "'";
					}
				}
				if (frameCount == 40 && !hotreloadFailed &&
					hotreloadPhase == HotReloadPlaytestPhase::Idle)
				{
					// the exact function the Play button calls
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						hotreloadFailed = true;
						hotreloadFailure = "startPlay failed";
					}
					else
					{
						hotreloadPhase = HotReloadPlaytestPhase::WaitBaseline;
						hotreloadDeadline =
							hotreloadNow + std::chrono::seconds(60);
						SDL_Log("orkige_editor: hot-reload playtest - Play "
							"pressed on the temp copy");
					}
				}
				else if (hotreloadPhase ==
					HotReloadPlaytestPhase::WaitBaseline)
				{
					// select the Probe so the player streams its transform
					if (playSession.mode == PlaySession::Mode::Playing &&
						playSession.remoteSelectedId != "Probe")
					{
						selectRemoteObject(playSession, "Probe");
					}
					const float x = hotreloadRemoteX();
					if (!std::isnan(x) && std::abs(x - 1.0f) < 0.01f)
					{
						// baseline confirmed - EDIT the file (init -> x=5): the
						// editor's watcher must pick this up and reload
						if (!hotreloadWriteScript(hotreloadMoveScript(5)))
						{
							hotreloadFailed = true;
							hotreloadFailure = "could not write the good edit";
						}
						else
						{
							hotreloadPhase = HotReloadPlaytestPhase::WaitGood;
							// generous ceiling: the ~4Hz watcher, the player's
							// frame cadence and the remote stream all slow down
							// on loaded shared runners
							hotreloadDeadline =
								hotreloadNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: hot-reload playtest - "
								"baseline x=1 up, edited the script to x=5");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						hotreloadFailed = true;
						hotreloadFailure = "session ended before the baseline "
							"transform arrived";
					}
				}
				else if (hotreloadPhase == HotReloadPlaytestPhase::WaitGood)
				{
					// the watcher fired ([reload] line) AND the player
					// recompiled-and-swapped (the streamed transform is x=5)
					const float x = hotreloadRemoteX();
					if (consoleHasReloadLine() && !std::isnan(x) &&
						std::abs(x - 5.0f) < 0.01f)
					{
						// now break it: a syntax error must NOT stop play
						if (!hotreloadWriteScript("this is not valid lua ((\n"))
						{
							hotreloadFailed = true;
							hotreloadFailure = "could not write the broken edit";
						}
						else
						{
							hotreloadPhase = HotReloadPlaytestPhase::WaitBroken;
							hotreloadDeadline =
								hotreloadNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: hot-reload playtest - live "
								"edit swapped in (x=5), now breaking the script");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						hotreloadFailed = true;
						hotreloadFailure = "session ended before the good "
							"reload took effect";
					}
				}
				else if (hotreloadPhase == HotReloadPlaytestPhase::WaitBroken)
				{
					// a failed reload: RED script error surfaces, the Probe is
					// in the error set, play CONTINUES and the old x=5 instance
					// keeps ticking (broken edit never kills the running game)
					const float x = hotreloadRemoteX();
					if (playSession.scriptErrorIds.count("Probe") != 0 &&
						consoleHasScriptErrorLine())
					{
						if (playSession.mode != PlaySession::Mode::Playing &&
							playSession.mode != PlaySession::Mode::Paused)
						{
							hotreloadFailed = true;
							hotreloadFailure = "the broken reload stopped play "
								"(it must keep running)";
						}
						else if (!std::isnan(x) && std::abs(x - 5.0f) > 0.01f)
						{
							hotreloadFailed = true;
							hotreloadFailure = "the broken reload clobbered the "
								"running transform (x != 5)";
						}
						else if (!hotreloadWriteScript(hotreloadMoveScript(8)))
						{
							hotreloadFailed = true;
							hotreloadFailure = "could not write the healing edit";
						}
						else
						{
							hotreloadPhase = HotReloadPlaytestPhase::WaitHeal;
							hotreloadDeadline =
								hotreloadNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: hot-reload playtest - broken "
								"edit surfaced a SCRIPT ERROR while play "
								"continued, now healing it to x=8");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						hotreloadFailed = true;
						hotreloadFailure = "session ended before the broken "
							"reload surfaced";
					}
				}
				else if (hotreloadPhase == HotReloadPlaytestPhase::WaitHeal)
				{
					// a good edit heals: the error clears AND the swap took
					// (x=8) - the object never died through the whole cycle
					const float x = hotreloadRemoteX();
					if (playSession.scriptErrorIds.empty() && !std::isnan(x) &&
						std::abs(x - 8.0f) < 0.01f)
					{
						requestStopPlay(playSession);
						hotreloadPhase = HotReloadPlaytestPhase::WaitRevert;
						hotreloadDeadline =
							hotreloadNow + std::chrono::seconds(90);
						SDL_Log("orkige_editor: hot-reload playtest - healing "
							"edit cleared the error and swapped in (x=8), "
							"stopping");
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						hotreloadFailed = true;
						hotreloadFailure = "session ended before the healing "
							"reload took effect";
					}
				}
				else if (hotreloadPhase == HotReloadPlaytestPhase::WaitRevert)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						if (playSession.process != nullptr ||
							playSession.client.isConnected())
						{
							hotreloadFailed = true;
							hotreloadFailure = "session not fully torn down "
								"after revert";
						}
						else
						{
							SDL_Log("orkige_editor: hot-reload playtest PASSED: "
								"watcher -> reload -> live swap (x 1->5->8), "
								"broken edit contained (SCRIPT ERROR, play "
								"continued), heal cleared it, clean Stop");
							std::error_code ignored;
							std::filesystem::remove_all(hotreloadTempRoot,
								ignored);
							hotreloadPhase = HotReloadPlaytestPhase::Done;
							running = false;
						}
					}
				}
				if (!hotreloadFailed &&
					hotreloadPhase != HotReloadPlaytestPhase::Idle &&
					hotreloadPhase != HotReloadPlaytestPhase::Done &&
					hotreloadNow >= hotreloadDeadline)
				{
					hotreloadFailed = true;
					// carry the chain evidence: what the remote stream last
					// showed tells WHERE the reload pipeline stopped
					const float lastX = hotreloadRemoteX();
					hotreloadFailure = "deadline exceeded in phase " +
						std::to_string(static_cast<int>(hotreloadPhase)) +
						" (last remote x=" +
						(std::isnan(lastX) ? std::string("none")
							: std::to_string(lastX)) +
						", mode=" + std::to_string(
							static_cast<int>(playSession.mode)) + ")";
				}
				if (hotreloadFailed)
				{
					SDL_Log("orkige_editor: hot-reload playtest FAILED - %s",
						hotreloadFailure.c_str());
					if (!hotreloadTempRoot.empty())
					{
						std::error_code ignored;
						std::filesystem::remove_all(hotreloadTempRoot, ignored);
					}
					exitCode = 7;
					running = false;
				}
			}

			// .oui hot-reload playtest (ORKIGE_EDITOR_UI_HOTRELOAD_PLAYTEST):
			// Play a temp copy, then overwrite assets/hud.oui on disk and assert
			// the editor's .oui watcher hot-reloaded the running screen (the
			// label rect moves over MSG_UI_LAYOUT), and that a broken edit keeps
			// the OLD screen up with a [remote] error.
			if (uiHotreloadPlaytestEnv)
			{
				const std::chrono::steady_clock::time_point uiNow =
					std::chrono::steady_clock::now();
				bool uiFailed = false;
				std::string uiFailure;
				if (frameCount == 10 &&
					uiHotreloadPhase == UiHotReloadPhase::Idle)
				{
					uiHotreloadTempRoot =
						(std::filesystem::temp_directory_path() /
						("orkige_ui_hotreload_" + std::to_string(
							std::chrono::steady_clock::now()
								.time_since_epoch().count()))).string();
					std::error_code copyError;
					std::filesystem::copy(uiHotreloadPlaytestEnv,
						uiHotreloadTempRoot,
						std::filesystem::copy_options::recursive, copyError);
					if (copyError)
					{
						uiFailed = true;
						uiFailure = "could not prepare the temp copy at " +
							uiHotreloadTempRoot;
					}
					else if (!openProjectFromPath(state, editorCore,
						uiHotreloadTempRoot))
					{
						uiFailed = true;
						uiFailure = "could not open the project copy '" +
							uiHotreloadTempRoot + "'";
					}
				}
				if (frameCount == 40 && !uiFailed &&
					uiHotreloadPhase == UiHotReloadPhase::Idle)
				{
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						uiFailed = true;
						uiFailure = "startPlay failed";
					}
					else
					{
						uiHotreloadPhase = UiHotReloadPhase::WaitBaseline;
						uiHotreloadDeadline = uiNow + std::chrono::seconds(90);
						SDL_Log("orkige_editor: ui hot-reload playtest - Play "
							"pressed on the temp copy");
					}
				}
				else if (uiHotreloadPhase == UiHotReloadPhase::WaitBaseline)
				{
					const float left = uiProbeLeft();
					if (!std::isnan(left))
					{
						// baseline rect confirmed - MOVE the label on disk
						uiBaselineLeft = left;
						if (!uiWriteOui(uiMovedOui(300)))
						{
							uiFailed = true;
							uiFailure = "could not write the moved .oui";
						}
						else
						{
							uiHotreloadPhase = UiHotReloadPhase::WaitMoved;
							uiHotreloadDeadline =
								uiNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: ui hot-reload playtest - "
								"baseline label left=%.0f up, moved the .oui",
								uiBaselineLeft);
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						uiFailed = true;
						uiFailure = "session ended before the baseline layout "
							"arrived";
					}
				}
				else if (uiHotreloadPhase == UiHotReloadPhase::WaitMoved)
				{
					// the watcher fired ([reload] line) AND the player rebuilt
					// the screen (the streamed label rect moved right)
					const float left = uiProbeLeft();
					if (consoleHasReloadLine() && !std::isnan(left) &&
						left >= uiBaselineLeft + 50.0f)
					{
						uiMovedLeft = left;
						// now break it: an unparseable .oui must NOT swap
						if (!uiWriteOui("[Broken section header\n"))
						{
							uiFailed = true;
							uiFailure = "could not write the broken .oui";
						}
						else
						{
							uiHotreloadPhase = UiHotReloadPhase::WaitBroken;
							uiHotreloadDeadline =
								uiNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: ui hot-reload playtest - "
								"live reload moved the label to left=%.0f, now "
								"breaking the .oui", uiMovedLeft);
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						uiFailed = true;
						uiFailure = "session ended before the moved reload took "
							"effect";
					}
				}
				else if (uiHotreloadPhase == UiHotReloadPhase::WaitBroken)
				{
					// a failed reload: a [remote] error surfaces, play continues
					// and the OLD (moved) screen keeps its rect
					const float left = uiProbeLeft();
					if (consoleHasReloadUiErrorLine())
					{
						if (playSession.mode != PlaySession::Mode::Playing &&
							playSession.mode != PlaySession::Mode::Paused)
						{
							uiFailed = true;
							uiFailure = "the broken reload stopped play "
								"(it must keep running)";
						}
						else if (!std::isnan(left) &&
							std::abs(left - uiMovedLeft) > 1.0f)
						{
							uiFailed = true;
							uiFailure = "the broken reload clobbered the running "
								"screen (label rect changed)";
						}
						else
						{
							requestStopPlay(playSession);
							uiHotreloadPhase = UiHotReloadPhase::WaitRevert;
							uiHotreloadDeadline =
								uiNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: ui hot-reload playtest - "
								"broken .oui surfaced a [remote] error while play "
								"continued and kept the old screen, stopping");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						uiFailed = true;
						uiFailure = "session ended before the broken reload "
							"surfaced";
					}
				}
				else if (uiHotreloadPhase == UiHotReloadPhase::WaitRevert)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						if (playSession.process != nullptr ||
							playSession.client.isConnected())
						{
							uiFailed = true;
							uiFailure = "session not fully torn down after "
								"revert";
						}
						else
						{
							SDL_Log("orkige_editor: ui hot-reload playtest "
								"PASSED: watcher -> reload_ui -> live rebuild "
								"(label moved), broken .oui contained ([remote] "
								"error, play continued, old screen kept), clean "
								"Stop");
							std::error_code ignored;
							std::filesystem::remove_all(uiHotreloadTempRoot,
								ignored);
							uiHotreloadPhase = UiHotReloadPhase::Done;
							running = false;
						}
					}
				}
				if (!uiFailed &&
					uiHotreloadPhase != UiHotReloadPhase::Idle &&
					uiHotreloadPhase != UiHotReloadPhase::Done &&
					uiNow >= uiHotreloadDeadline)
				{
					uiFailed = true;
					const float lastLeft = uiProbeLeft();
					uiFailure = "deadline exceeded in phase " +
						std::to_string(static_cast<int>(uiHotreloadPhase)) +
						" (last label left=" +
						(std::isnan(lastLeft) ? std::string("none")
							: std::to_string(lastLeft)) +
						", mode=" + std::to_string(
							static_cast<int>(playSession.mode)) +
						", reloadLine=" +
						(consoleHasReloadLine() ? "yes" : "no") +
						", reloadUiErr=" +
						(consoleHasReloadUiErrorLine() ? "yes" : "no") + ")";
				}
				if (uiFailed)
				{
					SDL_Log("orkige_editor: ui hot-reload playtest FAILED - %s",
						uiFailure.c_str());
					if (!uiHotreloadTempRoot.empty())
					{
						std::error_code ignored;
						std::filesystem::remove_all(uiHotreloadTempRoot,
							ignored);
					}
					exitCode = 7;
					running = false;
				}
			}

			// vector-animation hot-reload playtest
			// (ORKIGE_EDITOR_ANIM_HOTRELOAD_PLAYTEST): open a temp copy (the
			// drift scan must re-cook the fixture's drifted pair and leave the
			// record-less legacy pair untouched), Play, then edit / break the
			// Lottie source and the orphan .oanim on disk and assert the
			// watcher + MSG_RELOAD_ANIM round-trips.
			if (animHotreloadPlaytestEnv)
			{
				const std::chrono::steady_clock::time_point animNow =
					std::chrono::steady_clock::now();
				bool animFailed = false;
				std::string animFailure;
				if (frameCount == 10 &&
					animHotreloadPhase == AnimHotReloadPhase::Idle)
				{
					animHotreloadTempRoot =
						(std::filesystem::temp_directory_path() /
						("orkige_anim_hotreload_" + std::to_string(
							std::chrono::steady_clock::now()
								.time_since_epoch().count()))).string();
					std::error_code copyError;
					std::filesystem::copy(animHotreloadPlaytestEnv,
						animHotreloadTempRoot,
						std::filesystem::copy_options::recursive, copyError);
					if (copyError)
					{
						animFailed = true;
						animFailure = "could not prepare the temp copy at " +
							animHotreloadTempRoot;
					}
					else if (!openProjectFromPath(state, editorCore,
						animHotreloadTempRoot))
					{
						animFailed = true;
						animFailure = "could not open the project copy '" +
							animHotreloadTempRoot + "'";
					}
				}
				if (frameCount == 30 && !animFailed &&
					animHotreloadPhase == AnimHotReloadPhase::Idle)
				{
					// the OPEN ran the drift scan: the drifted pair re-cooked
					// (the [import] line + a fresh record on its sidecar), the
					// record-less legacy artifact stayed byte-untouched
					if (consoleCountLines(
						"re-cooking 'assets/probe.json'") < 1)
					{
						animFailed = true;
						animFailure = "the project open did not re-cook the "
							"drifted pair (no [import] re-cooking line)";
					}
					else if (consoleCountLines("legacy.json") > 0)
					{
						animFailed = true;
						animFailure = "the record-less legacy pair was touched "
							"by the drift scan";
					}
					else if (animReadAsset("legacy.oanim").find(
						"sentinel-untouched") == std::string::npos)
					{
						animFailed = true;
						animFailure = "the record-less legacy artifact was "
							"rewritten";
					}
					else if (animReadAsset("probe.json.orkmeta").find(
						"0000000000000000000000000000000000000000") !=
						std::string::npos)
					{
						animFailed = true;
						animFailure = "the re-cook did not refresh the drifted "
							"pair's recorded hashes";
					}
				}
				if (frameCount == 40 && !animFailed &&
					animHotreloadPhase == AnimHotReloadPhase::Idle)
				{
					if (!startPlay(playSession, gameObjectManager,
						state.project))
					{
						animFailed = true;
						animFailure = "startPlay failed";
					}
					else
					{
						animHotreloadPhase = AnimHotReloadPhase::WaitBaseline;
						animHotreloadDeadline =
							animNow + std::chrono::seconds(90);
						SDL_Log("orkige_editor: anim hot-reload playtest - "
							"drift scan verified, Play pressed on the temp "
							"copy");
					}
				}
				else if (animHotreloadPhase == AnimHotReloadPhase::WaitBaseline)
				{
					if (playSession.mode == PlaySession::Mode::Playing &&
						playSession.helloReceived &&
						playSession.scriptsWatchArmed)
					{
						// the session is live and the watcher baseline is armed:
						// rename the walk clip's marker in the SOURCE - the
						// watcher must re-cook, then hot-reload the rig
						const std::string source = animReadAsset("probe.json");
						const std::string::size_type marker =
							source.find("\"cm\":\"walk\"");
						if (source.empty() || marker == std::string::npos ||
							!animWriteAsset("probe.json", std::string(source)
								.replace(marker, 11, "\"cm\":\"swap\"")))
						{
							animFailed = true;
							animFailure = "could not edit the probe source";
						}
						else
						{
							animHotreloadPhase = AnimHotReloadPhase::WaitRecook;
							animHotreloadDeadline =
								animNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: anim hot-reload playtest - "
								"session live, renamed the walk clip to swap");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						animFailed = true;
						animFailure = "session ended before it became live";
					}
				}
				else if (animHotreloadPhase == AnimHotReloadPhase::WaitRecook)
				{
					// the watcher re-cooked (the [import] summary carries the
					// renamed clip) AND the player rebuilt the pair's rig
					if (consoleCountLines("swap 30-60") >= 1 &&
						consoleCountLines(
							"hot-reloaded animation 'probe.oanim'") >= 1)
					{
						// direct path next: touch the source-less orphan rig
						const std::string orphan = animReadAsset("orphan.oanim");
						if (orphan.empty() || !animWriteAsset("orphan.oanim",
							orphan + "\n# poked by the playtest\n"))
						{
							animFailed = true;
							animFailure = "could not edit the orphan rig";
						}
						else
						{
							animHotreloadPhase = AnimHotReloadPhase::WaitOrphan;
							animHotreloadDeadline =
								animNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: anim hot-reload playtest - "
								"source re-cook + live reload verified, poked "
								"the orphan rig");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						animFailed = true;
						animFailure = "session ended before the re-cooked rig "
							"hot-reloaded";
					}
				}
				else if (animHotreloadPhase == AnimHotReloadPhase::WaitOrphan)
				{
					if (consoleCountLines(
						"hot-reloaded animation 'orphan.oanim'") >= 1)
					{
						// break the COOK: an invalid source must fail honestly
						// and send no reload
						animProbeReloadsAtBreak = consoleCountLines(
							"hot-reloaded animation 'probe.oanim'");
						if (!animWriteAsset("probe.json", "{not json"))
						{
							animFailed = true;
							animFailure = "could not break the probe source";
						}
						else
						{
							animHotreloadPhase =
								AnimHotReloadPhase::WaitBrokenCook;
							animHotreloadDeadline =
								animNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: anim hot-reload playtest - "
								"orphan direct reload verified, broke the "
								"probe source");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						animFailed = true;
						animFailure = "session ended before the orphan rig "
							"hot-reloaded";
					}
				}
				else if (animHotreloadPhase ==
					AnimHotReloadPhase::WaitBrokenCook)
				{
					// the failed cook surfaced as an error line, play continued
					// and NO fresh reload reached the player
					if (consoleHasErrorLine("re-cook of 'probe.json' failed"))
					{
						if (playSession.mode != PlaySession::Mode::Playing &&
							playSession.mode != PlaySession::Mode::Paused)
						{
							animFailed = true;
							animFailure = "the broken cook stopped play (it "
								"must keep running)";
						}
						else if (consoleCountLines(
							"hot-reloaded animation 'probe.oanim'") !=
							animProbeReloadsAtBreak)
						{
							animFailed = true;
							animFailure = "a FAILED cook still hot-reloaded the "
								"running rig";
						}
						else if (!animWriteAsset("orphan.oanim", "not a rig\n"))
						{
							animFailed = true;
							animFailure = "could not break the orphan rig";
						}
						else
						{
							animHotreloadPhase =
								AnimHotReloadPhase::WaitBrokenReload;
							animHotreloadDeadline =
								animNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: anim hot-reload playtest - "
								"broken cook contained, broke the orphan rig");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						animFailed = true;
						animFailure = "session ended before the broken cook "
							"surfaced";
					}
				}
				else if (animHotreloadPhase ==
					AnimHotReloadPhase::WaitBrokenReload)
				{
					// the player's parse-before-swap refused the broken rig:
					// a [remote] reload_anim error, play continues
					if (consoleHasErrorLine("reload_anim"))
					{
						if (playSession.mode != PlaySession::Mode::Playing &&
							playSession.mode != PlaySession::Mode::Paused)
						{
							animFailed = true;
							animFailure = "the broken rig stopped play (the old "
								"rig must keep playing)";
						}
						else
						{
							requestStopPlay(playSession);
							animHotreloadPhase = AnimHotReloadPhase::WaitRevert;
							animHotreloadDeadline =
								animNow + std::chrono::seconds(90);
							SDL_Log("orkige_editor: anim hot-reload playtest - "
								"parse-before-swap refused the broken rig, "
								"stopping");
						}
					}
					else if (playSession.mode == PlaySession::Mode::Edit)
					{
						animFailed = true;
						animFailure = "session ended before the broken rig was "
							"refused";
					}
				}
				else if (animHotreloadPhase == AnimHotReloadPhase::WaitRevert)
				{
					if (playSession.mode == PlaySession::Mode::Edit)
					{
						if (playSession.process != nullptr ||
							playSession.client.isConnected())
						{
							animFailed = true;
							animFailure = "session not fully torn down after "
								"revert";
						}
						else
						{
							SDL_Log("orkige_editor: anim hot-reload playtest "
								"PASSED: open-scan re-cooked the drifted pair "
								"(legacy untouched), source edit -> re-cook -> "
								"live reload, orphan direct reload, broken "
								"cook and broken rig both contained, clean "
								"Stop");
							std::error_code ignored;
							std::filesystem::remove_all(animHotreloadTempRoot,
								ignored);
							animHotreloadPhase = AnimHotReloadPhase::Done;
							running = false;
						}
					}
				}
				if (!animFailed &&
					animHotreloadPhase != AnimHotReloadPhase::Idle &&
					animHotreloadPhase != AnimHotReloadPhase::Done &&
					animNow >= animHotreloadDeadline)
				{
					animFailed = true;
					animFailure = "deadline exceeded in phase " +
						std::to_string(static_cast<int>(animHotreloadPhase)) +
						" (mode=" + std::to_string(
							static_cast<int>(playSession.mode)) +
						", probeReloads=" + std::to_string(consoleCountLines(
							"hot-reloaded animation 'probe.oanim'")) +
						", orphanReloads=" + std::to_string(consoleCountLines(
							"hot-reloaded animation 'orphan.oanim'")) +
						", cookErr=" + (consoleHasErrorLine(
							"re-cook of 'probe.json' failed") ? "yes" : "no") +
						", reloadErr=" + (consoleHasErrorLine("reload_anim")
							? "yes" : "no") + ")";
				}
				if (animFailed)
				{
					SDL_Log("orkige_editor: anim hot-reload playtest FAILED - "
						"%s", animFailure.c_str());
					if (!animHotreloadTempRoot.empty())
					{
						std::error_code ignored;
						std::filesystem::remove_all(animHotreloadTempRoot,
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
			// MCP control-port self-test (editor_control / editor_control_debug
			// ctests): once the frame-2 fixtures exist, drive the control server
			// over a real socket and assert every reply (see
			// EditorControlSelfTest). The runtime-debug flavor boots Play over
			// MCP and drives the running game. Runs uncapped until it passes or
			// fails.
			if (controlSelfTestEnv != nullptr && controlServer.isListening())
			{
				if (frameCount == 3)
				{
					controlSelfTest.begin(controlServer.getPort(),
						controlServer.getToken(), controlSelfTestEnv,
						controlPlaytestEnv != nullptr,
						controlBrowserTestEnv != nullptr,
						controlBrowserSessionEnv != nullptr);
				}
				if (controlSelfTest.active())
				{
					controlSelfTest.update(gameObjectManager);
				}
				if (frameCount >= 3 && controlSelfTest.done())
				{
					if (!controlSelfTest.passed())
					{
						exitCode = 2;
					}
					else if (controlSelfTest.skipped())
					{
						// browser-play test on a machine without the wasm
						// player: the ctest SKIP contract
						exitCode = 77;
					}
					running = false;
				}
			}
			// Help self-test (editor_help_portal ctest): fire the menu
			// action's request flag and assert the frame loop resolved the
			// published documentation URL. The run is automated, so the
			// default-browser gate must hold - no browser and no network
			// are ever touched (the site itself is CI-deployed and gated by
			// make_help_portal_selftest).
			if (helpTestEnv != nullptr)
			{
				if (frameCount == 5)
				{
					SDL_Log("orkige_editor: help-test requesting the portal "
						"(the Help > Orkige Help action)");
					state.requestedHelpPortal = true;
				}
				if (frameCount >= 8)
				{
					const bool passed = helpOpenedUrl == HELP_PORTAL_URL;
					SDL_Log("orkige_editor: help-test %s (url '%s')",
						passed ? "PASSED" : "FAILED", helpOpenedUrl.c_str());
					if (!passed)
					{
						exitCode = 2;
					}
					running = false;
				}
			}

			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

		// editor shutdown while an MCP transaction is still open: it never
		// committed, so roll its uncommitted edits back (and log one line)
		// before the world is torn down - the document-lifecycle safety net
		controlServer.abortOpenTransaction(controlContext, "editor shutdown");

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

		// free the thumbnail cache's owned CPU uploads (vector shapes) while the
		// render system is still up - manual textures are not resource-group
		// content, so nothing else frees them in time for strict backends
		clearCachedThumbnails(state.assetBrowser);

		// ImGui teardown: destroying the context writes the ini; the facade
		// 2D layer + font texture die with the renderer/engine afterwards
		gImGuiRenderer = nullptr;
		ImGui::DestroyContext();
		// the console dies with this scope - detach the log hooks first
		// (the engine log capture detaches itself in its destructor). Clearing
		// the oDebug* sink here, before the console, is the lifetime contract:
		// logClearSink blocks until any in-flight emit finishes, so the sink's
		// console reference can never dangle.
		Orkige::logClearSink();
		engineLogCapture.detach();
		SDL_SetLogOutputFunction(sdlLogHook.previous,
			sdlLogHook.previousUserdata);
	}

	// AppHost's destructor mirrors the boot: world, engine, singletons,
	// then the SDL window
	return exitCode;
}
