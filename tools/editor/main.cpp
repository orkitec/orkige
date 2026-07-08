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
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_input/InputManager.h>
#include <engine_render/DrawLayer2D.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_project/Project.h>
#include <core_project/NativeModule.h>
#include <core_debugnet/DebugClient.h>
#include <core_debugnet/DebugServer.h>
#include <core_util/StringUtil.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#include <core_script/ScriptRuntime.h>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder API (programmatic first-run layout)
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
#include <iterator>
#include <map>
#include <mutex>
#include <set>
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

//--- Console (engine/editor/remote log streaming) --------------------------

//! severity of one Console line (drives the line colour)
enum class ConsoleLevel
{
	Info,
	Warning,
	Error
};

//! one Console entry
struct ConsoleLine
{
	ConsoleLevel level;
	std::string text;
};

// The editor Console's line store: the engine log (via the engine_base
// EngineLogCapture service, drained once per frame), the editor's own SDL_Log
// lines (via the SDL log output hook) and, during play mode, the "[remote]"
// lines the player streams over the debug protocol all land here. Log
// callbacks may in principle fire off the main thread, so the store is
// mutex-guarded.
struct EditorConsole
{
	//! line cap: when reached, the OLDEST half is dropped in one go
	static const std::size_t MAX_LINES = 5000;

	std::mutex mutex;
	std::vector<ConsoleLine> lines;
	bool autoScroll = true;
	bool scrollToBottom = false;
	ImGuiTextFilter filter;

	void addLine(ConsoleLevel level, std::string const& text)
	{
		std::lock_guard<std::mutex> lock(this->mutex);
		if (this->lines.size() >= MAX_LINES)
		{
			this->lines.erase(this->lines.begin(),
				this->lines.begin() + MAX_LINES / 2);
		}
		this->lines.push_back({ level, text });
		this->scrollToBottom = true;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(this->mutex);
		this->lines.clear();
	}
};

//! per-frame pump: move the engine log lines the EngineLogCapture service
//! collected since the last frame into the Console (severity string ->
//! Console level; the service replaced the editor's own Ogre::LogListener,
//! sharing one capture implementation with PlayerDebugLink)
void drainEngineLogIntoConsole(Orkige::EngineLogCapture& capture,
	EditorConsole& console)
{
	for (Orkige::EngineLogCapture::Line const& line : capture.drain())
	{
		ConsoleLevel level = ConsoleLevel::Info;
		if (line.level == "warning")
		{
			level = ConsoleLevel::Warning;
		}
		else if (line.level == "error")
		{
			level = ConsoleLevel::Error;
		}
		console.addLine(level, line.text);
	}
}

//! SDL log output hook state: the editor's own SDL_Log lines go into the
//! Console AND to the previous (default) output so the terminal keeps them
struct SdlLogHook
{
	EditorConsole* console = nullptr;
	SDL_LogOutputFunction previous = nullptr;
	void* previousUserdata = nullptr;
};

void SDLCALL consoleSdlLogOutput(void* userdata, int category,
	SDL_LogPriority priority, const char* message)
{
	SdlLogHook* hook = static_cast<SdlLogHook*>(userdata);
	ConsoleLevel level = ConsoleLevel::Info;
	if (priority >= SDL_LOG_PRIORITY_ERROR)
	{
		level = ConsoleLevel::Error;
	}
	else if (priority == SDL_LOG_PRIORITY_WARN)
	{
		level = ConsoleLevel::Warning;
	}
	if (hook->console)
	{
		hook->console->addLine(level, message);
	}
	if (hook->previous)
	{
		hook->previous(hook->previousUserdata, category, priority, message);
	}
}

//--- project export (Build menu -> Util/orkige_export.py) -------------------

//! @brief an async export run (Build > Build for <platform>, milestone 4):
//! Util/orkige_export.py packages the open project; stdout+stderr are piped
//! and streamed into the Console as "[export]" lines, the same pattern as
//! the native-module "[build]" lines. One export runs at a time; the
//! artifact path is parsed from the exporter's final
//! "orkige_export: OK <path>" line for the Reveal-in-Finder nicety.
struct ExportJob
{
	SDL_Process* process = nullptr;
	std::string platform;			//!< "macos" / "ios-simulator" / "android"
	std::string outputBuffer;		//!< partial (unterminated) last line
	std::string artifactPath;		//!< from the final OK line
	bool isActive() const { return this->process != nullptr; }
};

//! @brief launch the exporter for the open project (async; false when it
//! cannot start). The build tree the exporter packages from is per-platform:
//! THIS editor's build tree for macOS, the ios-simulator-debug /
//! android-debug preset trees for the mobile targets - the exporter reports
//! honestly when one of those was never built.
bool startExport(ExportJob& job, Orkige::Project const& project,
	std::string const& platform, EditorConsole& console)
{
	if (job.isActive())
	{
		console.addLine(ConsoleLevel::Warning,
			"[export] an export is already running - wait for it to finish");
		return false;
	}
	if (!project.isLoaded())
	{
		return false; // the menu items are disabled without a project
	}
	const std::string exporter =
		std::string(ORKIGE_EDITOR_ENGINE_ROOT) + "/Util/orkige_export.py";
	std::string engineBuild = ORKIGE_EDITOR_ENGINE_BUILD_DIR;
	if (platform == "ios-simulator")
	{
		engineBuild = std::string(ORKIGE_EDITOR_ENGINE_ROOT) +
			"/build/ios-simulator-debug";
	}
	else if (platform == "android")
	{
		engineBuild = std::string(ORKIGE_EDITOR_ENGINE_ROOT) +
			"/build/android-debug";
	}
	const std::vector<std::string> command = { "python3", exporter,
		"--project", project.getRootDirectory(), "--platform", platform,
		"--engine-build", engineBuild };
	std::vector<const char*> args;
	std::string commandLine;
	args.reserve(command.size() + 1);
	for (std::string const& arg : command)
	{
		args.push_back(arg.c_str());
		commandLine += (commandLine.empty() ? "" : " ") + arg;
	}
	args.push_back(nullptr);
	SDL_PropertiesID spawnProperties = SDL_CreateProperties();
	SDL_SetPointerProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
		const_cast<char**>(args.data()));
	SDL_SetNumberProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
		SDL_PROCESS_STDIO_APP);
	SDL_SetBooleanProperty(spawnProperties,
		SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
	job.process = SDL_CreateProcessWithProperties(spawnProperties);
	SDL_DestroyProperties(spawnProperties);
	if (!job.process)
	{
		console.addLine(ConsoleLevel::Error, "[export] FAILED to run '" +
			commandLine + "': " + SDL_GetError());
		return false;
	}
	job.platform = platform;
	job.outputBuffer.clear();
	job.artifactPath.clear();
	// the SDL log hook mirrors this into the Console as an "[export]" line
	SDL_Log("[export] $ %s", commandLine.c_str());
	return true;
}

//! @brief per-frame pump: stream the exporter's output into the Console as
//! "[export]" lines, capture the artifact path, and on exit report success
//! (revealing the artifact in Finder) or failure honestly
void updateExportJob(ExportJob& job, EditorConsole& console)
{
	if (!job.isActive())
	{
		return;
	}
	auto emitLine = [&console, &job](std::string const& text)
	{
		const std::string okPrefix = "orkige_export: OK ";
		if (text.rfind(okPrefix, 0) == 0)
		{
			job.artifactPath = text.substr(okPrefix.size());
		}
		ConsoleLevel level = ConsoleLevel::Info;
		if (text.find("ERROR") != std::string::npos ||
			text.find("error") != std::string::npos ||
			text.find("FAILED") != std::string::npos)
		{
			level = ConsoleLevel::Error;
		}
		else if (text.find("WARNING") != std::string::npos ||
			text.find("warning") != std::string::npos)
		{
			level = ConsoleLevel::Warning;
		}
		console.addLine(level, "[export] " + text);
	};
	SDL_IOStream* output = SDL_GetProcessOutput(job.process);
	if (output)
	{
		char chunk[4096];
		size_t bytesRead = 0;
		while ((bytesRead = SDL_ReadIO(output, chunk, sizeof(chunk))) > 0)
		{
			job.outputBuffer.append(chunk, bytesRead);
		}
	}
	std::size_t lineStart = 0;
	std::size_t newline = std::string::npos;
	while ((newline = job.outputBuffer.find('\n', lineStart)) !=
		std::string::npos)
	{
		std::string line =
			job.outputBuffer.substr(lineStart, newline - lineStart);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		emitLine(line);
		lineStart = newline + 1;
	}
	job.outputBuffer.erase(0, lineStart);
	int exitCode = 0;
	if (!SDL_WaitProcess(job.process, false, &exitCode))
	{
		return; // still exporting; the editor stays responsive
	}
	if (!job.outputBuffer.empty())
	{
		emitLine(job.outputBuffer); // drain an unterminated tail line
		job.outputBuffer.clear();
	}
	SDL_DestroyProcess(job.process);
	job.process = nullptr;
	if (exitCode != 0)
	{
		console.addLine(ConsoleLevel::Error, "[export] " + job.platform +
			" export FAILED (exit " + std::to_string(exitCode) +
			") - see the lines above");
		return;
	}
	console.addLine(ConsoleLevel::Info, "[export] " + job.platform +
		" export succeeded: " + job.artifactPath);
#ifdef __APPLE__
	if (!job.artifactPath.empty())
	{
		// Reveal in Finder (fire and forget)
		// TODO(linux): xdg-open on the artifact's parent directory would be
		// the equivalent - wire it when exports exist on Linux (the export
		// tests/targets are APPLE-gated today, see tests/CMakeLists.txt)
		const char* revealArgs[] =
			{ "open", "-R", job.artifactPath.c_str(), nullptr };
		if (SDL_Process* reveal = SDL_CreateProcess(revealArgs, false))
		{
			SDL_DestroyProcess(reveal);
		}
	}
#endif
}

//--- view settings (grid, orientation gizmo, camera) ------------------------

// User-tweakable viewport settings, persisted as a simple key=value file
// next to the imgui ini (orkige_editor_view.ini) so they survive restarts.
// The panel_* flags carry the View > Panels visibility state (a closed panel
// must be reopenable, and stay closed across restarts if that's how it was
// left).
struct ViewSettings
{
	//! File > Open Recent keeps at most this many scene paths
	static const std::size_t MAX_RECENT_SCENES = 5;
	//! File > Open Recent Project keeps at most this many project roots
	static const std::size_t MAX_RECENT_PROJECTS = 5;

	bool showGrid = true;			//!< reference grid on the ground plane
	bool showViewGizmo = true;		//!< axis orientation gizmo (top-right)
	float orbitSpeed = 0.4f;		//!< orbit drag: degrees per dragged point
	//! fly-mode mouselook: degrees per relative mouse count (0.15 by
	//! default; fly mode runs in SDL relative mouse mode, whose counts
	//! track physical mouse travel 1:1 - no content-scale factor)
	float lookSpeed = Orkige::FLY_LOOK_SPEED_DEFAULT;
	float zoomSpeed = 1.0f;			//!< scroll zoom multiplier
	float flySpeed = 6.0f;			//!< fly-mode base speed (units/s; scroll while flying tunes it)
	float fovDeg = 45.0f;			//!< scene camera vertical FOV
	//! dockable panel visibility (View > Panels; indices = EditorPanelIndex
	//! order: Hierarchy, Inspector, Console, Stats, Scene)
	bool showHierarchyPanel = true;
	bool showInspectorPanel = true;
	bool showConsolePanel = true;
	bool showStatsPanel = true;
	bool showScenePanel = true;
	//! snap settings (toolbar toggle + editable step values, Unity-style);
	//! mirrored into EditorCore on startup, persisted on every popover edit
	bool snapEnabled = false;
	float snapTranslate = Orkige::EditorCore::SNAP_TRANSLATE;
	float snapRotateDegrees = Orkige::EditorCore::SNAP_ROTATE_DEGREES;
	float snapScale = Orkige::EditorCore::SNAP_SCALE;
	//! reopen the most recent project on launch (Unity behavior); automation
	//! runs (any ORKIGE_EDITOR_*/ORKIGE_DEMO_* hook) always start blank
	bool reopenLastProject = true;
	//! most-recently-used scene paths (newest first) for File > Open Recent;
	//! filled by every successful open/save, capped at MAX_RECENT_SCENES
	std::vector<std::string> recentScenes;
	//! most-recently-used project roots (newest first) for File > Open
	//! Recent Project; filled by every successful project open/create
	std::vector<std::string> recentProjects;
	std::string path;				//!< backing file ("" = don't persist)

	void load()
	{
		std::ifstream file(this->path);
		std::string line;
		while (std::getline(file, line))
		{
			const std::size_t equals = line.find('=');
			if (equals == std::string::npos)
			{
				continue;
			}
			const std::string key = line.substr(0, equals);
			const std::string value = line.substr(equals + 1);
			if (key == "show_grid")
			{
				this->showGrid = (value == "1");
			}
			else if (key == "show_view_gizmo")
			{
				this->showViewGizmo = (value == "1");
			}
			else if (key == "orbit_speed")
			{
				this->orbitSpeed = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "look_speed")
			{
				this->lookSpeed = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "zoom_speed")
			{
				this->zoomSpeed = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "fly_speed")
			{
				this->flySpeed = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "fov_deg")
			{
				this->fovDeg = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "panel_hierarchy")
			{
				this->showHierarchyPanel = (value == "1");
			}
			else if (key == "panel_inspector")
			{
				this->showInspectorPanel = (value == "1");
			}
			else if (key == "panel_console")
			{
				this->showConsolePanel = (value == "1");
			}
			else if (key == "panel_stats")
			{
				this->showStatsPanel = (value == "1");
			}
			else if (key == "panel_scene")
			{
				this->showScenePanel = (value == "1");
			}
			else if (key == "snap_enabled")
			{
				this->snapEnabled = (value == "1");
			}
			else if (key == "snap_translate")
			{
				this->snapTranslate = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "snap_rotate_deg")
			{
				this->snapRotateDegrees = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "snap_scale")
			{
				this->snapScale = std::strtof(value.c_str(), nullptr);
			}
			else if (key == "reopen_last_project")
			{
				this->reopenLastProject = (value == "1");
			}
			else if (key == "recent_scene")
			{
				// one line per entry, newest first (the save order)
				if (!value.empty() &&
					this->recentScenes.size() < MAX_RECENT_SCENES)
				{
					this->recentScenes.push_back(value);
				}
			}
			else if (key == "recent_project")
			{
				if (!value.empty() &&
					this->recentProjects.size() < MAX_RECENT_PROJECTS)
				{
					this->recentProjects.push_back(value);
				}
			}
		}
		// keep loaded values inside the UI's ranges (a hand-edited file must
		// not wedge the camera)
		this->orbitSpeed = std::clamp(this->orbitSpeed, 0.05f, 2.0f);
		this->lookSpeed = std::clamp(this->lookSpeed, 0.02f, 1.0f);
		this->zoomSpeed = std::clamp(this->zoomSpeed, 0.1f, 3.0f);
		this->flySpeed = std::clamp(this->flySpeed,
			Orkige::FLY_SPEED_MIN, Orkige::FLY_SPEED_MAX);
		this->fovDeg = std::clamp(this->fovDeg, 20.0f, 120.0f);
		// same clamping rule EditorCore::setSnapValues applies (a zero step
		// from a hand-edited file must not freeze the gizmo)
		this->snapTranslate = std::max(this->snapTranslate, 0.001f);
		this->snapRotateDegrees = std::max(this->snapRotateDegrees, 0.1f);
		this->snapScale = std::max(this->snapScale, 0.001f);
	}

	void save() const
	{
		if (this->path.empty())
		{
			return;
		}
		std::ofstream file(this->path, std::ios::trunc);
		file << "show_grid=" << (this->showGrid ? 1 : 0) << "\n"
			<< "show_view_gizmo=" << (this->showViewGizmo ? 1 : 0) << "\n"
			<< "orbit_speed=" << this->orbitSpeed << "\n"
			<< "look_speed=" << this->lookSpeed << "\n"
			<< "zoom_speed=" << this->zoomSpeed << "\n"
			<< "fly_speed=" << this->flySpeed << "\n"
			<< "fov_deg=" << this->fovDeg << "\n"
			<< "panel_hierarchy=" << (this->showHierarchyPanel ? 1 : 0) << "\n"
			<< "panel_inspector=" << (this->showInspectorPanel ? 1 : 0) << "\n"
			<< "panel_console=" << (this->showConsolePanel ? 1 : 0) << "\n"
			<< "panel_stats=" << (this->showStatsPanel ? 1 : 0) << "\n"
			<< "panel_scene=" << (this->showScenePanel ? 1 : 0) << "\n"
			<< "snap_enabled=" << (this->snapEnabled ? 1 : 0) << "\n"
			<< "snap_translate=" << this->snapTranslate << "\n"
			<< "snap_rotate_deg=" << this->snapRotateDegrees << "\n"
			<< "snap_scale=" << this->snapScale << "\n"
			<< "reopen_last_project="
			<< (this->reopenLastProject ? 1 : 0) << "\n";
		for (std::string const& recent : this->recentScenes)
		{
			file << "recent_scene=" << recent << "\n";
		}
		for (std::string const& recent : this->recentProjects)
		{
			file << "recent_project=" << recent << "\n";
		}
	}

	//! record a successfully opened/saved scene path for File > Open Recent:
	//! move-to-front, dedupe, cap at MAX_RECENT_SCENES (caller persists)
	void addRecentScene(std::string const& scenePath)
	{
		if (scenePath.empty())
		{
			return;
		}
		this->recentScenes.erase(std::remove(this->recentScenes.begin(),
			this->recentScenes.end(), scenePath), this->recentScenes.end());
		this->recentScenes.insert(this->recentScenes.begin(), scenePath);
		if (this->recentScenes.size() > MAX_RECENT_SCENES)
		{
			this->recentScenes.resize(MAX_RECENT_SCENES);
		}
	}

	//! record a successfully opened/created project root for File > Open
	//! Recent Project (same move-to-front/dedupe/cap rule as the scenes)
	void addRecentProject(std::string const& projectRoot)
	{
		if (projectRoot.empty())
		{
			return;
		}
		this->recentProjects.erase(std::remove(this->recentProjects.begin(),
			this->recentProjects.end(), projectRoot),
			this->recentProjects.end());
		this->recentProjects.insert(this->recentProjects.begin(), projectRoot);
		if (this->recentProjects.size() > MAX_RECENT_PROJECTS)
		{
			this->recentProjects.resize(MAX_RECENT_PROJECTS);
		}
	}

	//! restore the factory camera/display values (View > Reset View Settings);
	//! panel visibility is NOT touched - that belongs to Reset Layout
	void resetCameraAndDisplayDefaults()
	{
		const ViewSettings defaults;
		this->showGrid = defaults.showGrid;
		this->showViewGizmo = defaults.showViewGizmo;
		this->orbitSpeed = defaults.orbitSpeed;
		this->lookSpeed = defaults.lookSpeed;
		this->zoomSpeed = defaults.zoomSpeed;
		this->flySpeed = defaults.flySpeed;
		this->fovDeg = defaults.fovDeg;
	}

	//! all panels visible again (Reset Layout re-opens everything)
	void showAllPanels()
	{
		this->showHierarchyPanel = true;
		this->showInspectorPanel = true;
		this->showConsolePanel = true;
		this->showStatsPanel = true;
		this->showScenePanel = true;
	}
};

// The live ViewSettings instance (owned by main), reachable from the scene
// open/save functions so every successful open/save feeds File > Open Recent
// without threading a ViewSettings& through all their call sites.
ViewSettings* gViewSettings = nullptr;

// the ImGui-on-facade renderer (owned by main; global so drawScenePanel can
// register the scene RTT for ImGui::Image without threading it through every
// draw* signature - same pattern as gViewSettings)
Orkige::ImGuiFacadeRenderer* gImGuiRenderer = nullptr;
// false during automated runs: the scripted tests open temp scenes/projects
// through the same functions a user does, and those must never pollute the
// interactive Open Recent lists (or become the reopened "last project")
bool gRecordRecents = true;

//! record a scene path in the Open Recent list and persist it
void recordRecentScene(std::string const& scenePath)
{
	if (gViewSettings && gRecordRecents)
	{
		gViewSettings->addRecentScene(scenePath);
		gViewSettings->save();
	}
}

//! record a project root in the Open Recent Project list and persist it
void recordRecentProject(std::string const& projectRoot)
{
	if (gViewSettings && gRecordRecents)
	{
		gViewSettings->addRecentProject(projectRoot);
		gViewSettings->save();
	}
}

// Editor UI state that lives across frames. Everything UI-independent
// (selection, dirty flag, tools, undo/redo) lives in EditorCore instead.
struct EditorState
{
	bool quitRequested = false;
	//! current scene file (empty = unsaved "untitled" scene); the dirty
	//! marker lives in EditorCore, both are reflected in the window title
	std::string currentScenePath;
	//! @brief the open project (unloaded = loose-scene mode, the historical
	//! behavior). A loaded project ROOTS everything: the window title, the
	//! scene dialog defaults (scenes/), the mesh import destination
	//! (assets/), the "OrkigeProject" resource group and the --project the
	//! play mode passes to the player.
	Orkige::Project project;
	//! Open/Save As/Import go through the native SDL3 file dialogs
	//! (requestFileDialog); the "Scene Path" modal only steps in as the
	//! fallback when a native dialog reports failure. Which action the
	//! fallback path input would confirm:
	Orkige::FileDialogAction scenePathAction = Orkige::FileDialogAction::None;
	//! media directories already registered as Ogre resource locations by
	//! the mesh importer (each directory registers exactly once per run)
	std::set<std::string> importResourceDirs;
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
	//! scene camera (orbit sphere + fly mode, see EditorCamera.h); the
	//! defaults reproduce the old fixed camera at (0, 2.5, 9) looking at
	//! the origin. Bindings: right-hold = fly (mouselook + WASD/QE),
	//! Alt+left drag = orbit, middle drag = pan, scroll = zoom,
	//! F (frame selected) retargets.
	Orkige::EditorCameraState camera;
	bool orbitActive = false;	//!< Alt+left orbit drag in progress
	bool panActive = false;		//!< middle-drag pan in progress
	bool flyActive = false;		//!< right-hold fly mode in progress
	//! first-frame delta swallow per drag kind (see CameraDragGate)
	Orkige::CameraDragGate flyLookGate;
	Orkige::CameraDragGate orbitDragGate;
	Orkige::CameraDragGate panDragGate;
	//! deferred popup opens (menus - native or ImGui - set these; the modals
	//! are drawn once per frame by drawEditorModals)
	bool openAboutPopup = false;
	bool openQuitConfirmPopup = false;
	//! Build menu request ("macos"/"ios-simulator"/"android"; "" = none) -
	//! menus (native or ImGui) set it, the frame loop starts the export
	std::string requestedExport;
	//! floating View Settings window (grid/gizmo/camera-feel sliders); on mac
	//! the native View > View Settings... opens it since the ImGui menu bar
	//! that used to host the sliders is not drawn there
	bool showViewSettingsWindow = false;
	//! View > Reset Layout: rebuild the default dock layout next frame
	bool resetDockLayout = false;
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
	//! Hierarchy search/filter box (Unity-style); ImGuiTextFilter supports
	//! comma-separated terms and "-term" exclusion, empty = show everything
	ImGuiTextFilter hierarchyFilter;
	//! inline rename in the Hierarchy (F2 / context menu)
	std::string renamingObjectId;
	char renameBuffer[256] = "";
	bool renameFocusPending = false;
	//! "Add Component" popup search state (Inspector)
	char addComponentSearch[128] = "";
	bool addComponentFocusPending = false;
	//! ModelComponent mesh field edit buffer (rebuilt when the selection or
	//! the component's current mesh changes)
	char meshEditBuffer[512] = "";
	std::string meshEditObjectId;
	std::string meshEditCurrentMesh;
	//! ScriptComponent script path field edit buffer (same rebuild rules)
	char scriptEditBuffer[512] = "";
	std::string scriptEditObjectId;
	std::string scriptEditCurrentScript;
	//! SpriteComponent texture field edit buffer (same rebuild rules)
	char spriteEditBuffer[512] = "";
	std::string spriteEditObjectId;
	std::string spriteEditCurrentTexture;
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
// Native project modules (milestone 3): a project whose manifest carries the
// "native.*" settings (core_project/NativeModule.h) brings its own COMPILED
// game code. Play then becomes compile-on-Play: temp scene -> incremental
// cmake build of the module (async, output streamed into the Console as
// "[build]" lines, Stop cancels) -> on success the PROJECT'S executable is
// spawned as the play process (it implements the same CLI contract as the
// player: scene, --project, --debug-port - see engine_runtime/
// PlayerRuntime.h) -> the debug protocol works as always. On a failed build
// the editor stays in edit mode with the errors in the Console and nothing
// is launched. Desktop target only for now - simulator/Android native
// modules wait for the export pipeline.
struct PlaySession
{
	enum class Mode { Edit, Building, Launching, Playing, Paused, Stopping };
	Mode mode = Mode::Edit;
	SDL_Process* process = nullptr;
	Orkige::DebugClient client;
	unsigned short port = 0;
	std::string tempScenePath;
	//! project root the ACTIVE session plays in ("" = loose-scene mode);
	//! passed to the player as --project so its assets/scenes (and, from
	//! milestone 2 on, scripts) resolve against the same roots as the editor
	std::string projectRoot;
	//! desktop play flavor picked in the toolbar: empty = this editor
	//! build's own player (ORKIGE_EDITOR_PLAYER_PATH), otherwise the
	//! OTHER render flavor's player binary (the debug protocol is
	//! flavor-agnostic, so a next-flavor editor can play on the classic
	//! player and vice versa - the entry greys out while that flavor's
	//! build tree has no player binary)
	std::string desktopPlayerPath;
	std::string desktopLabel;		//!< display name of the picked desktop flavor
	//! play target picked in the toolbar: empty = local desktop player,
	//! otherwise the UDID of a booted iOS simulator (launched via simctl;
	//! OrkigePlayer.app must have been installed on it once - see the
	//! ios-simulator-debug preset). Simulator apps read the host filesystem
	//! and share the host loopback, so the temp scene path and the
	//! 127.0.0.1 debug link work unchanged.
	std::string simulatorUdid;
	std::string simulatorLabel;		//!< display name of the picked simulator
	bool onSimulator = false;		//!< the ACTIVE session runs on a simulator
	//! simulator preparation pipeline (Play on a SHUTDOWN simulator): boot ->
	//! wait for Booted -> app check (+ install when missing) -> launch. The
	//! long-running steps (simctl boot/install) run as background processes
	//! polled per frame so the editor stays responsive and the toolbar shows
	//! honest status text - never a silent no-op.
	enum class SimPrep { None, WaitBootProcess, WaitBooted, Installing };
	SimPrep simPrep = SimPrep::None;
	SDL_Process* simPrepProcess = nullptr;	//!< async simctl boot/install
	std::chrono::steady_clock::time_point simPrepStart;
	std::chrono::steady_clock::time_point simLastPoll;
	//! the editor booted the simulator for THIS run (the scripted play test
	//! shuts it down again afterwards; interactive runs leave it running)
	bool simulatorBootedByEditor = false;
	//! toolbar status line while mode == Launching ("" = generic text)
	std::string launchStatus;
	//! play target picked in the toolbar: empty = not Android, otherwise the
	//! adb serial of a connected device/emulator (the player APK must be
	//! installed - see tools/player/android/package_apk.sh). The temp scene
	//! travels via 'adb push + run-as' into the app files dir; the debug link
	//! rides an 'adb forward tcp' bridge, so the editor still connects to
	//! 127.0.0.1. Physical Android phones work through the exact same flow.
	std::string androidSerial;
	std::string androidLabel;		//!< display name of the picked device
	bool onAndroid = false;			//!< the ACTIVE session runs on Android
	bool androidForwarded = false;	//!< an 'adb forward' is active for port
	//! native module compile-on-Play (mode == Building): the remaining
	//! cmake command queue (configure-if-needed, then build), the running
	//! step (stdout+stderr piped, streamed into the Console per frame) and
	//! the executable a successful build launches as the play process
	std::vector<std::vector<std::string>> buildSteps;
	SDL_Process* buildProcess = nullptr;
	std::string buildOutputBuffer;		//!< partial (unterminated) last line
	std::string nativeExecutable;		//!< built play-process executable
	std::string nativeTarget;			//!< the module's CMake target name
	//! remote state streamed by the player
	std::string remoteScenePath;
	bool helloReceived = false;
	bool hierarchyReceived = false;
	bool remoteLogSeen = false;		//!< at least one remote log line arrived
	Orkige::StringVector remoteHierarchy;
	//! objects whose ScriptComponent reported a failure (script_error
	//! messages, deduped per object per session): feeds the RED Console
	//! line, the toolbar warning marker and the remote hierarchy tint;
	//! cleared on Stop / a new session (clearRemoteState)
	std::set<std::string> scriptErrorIds;
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
//! seconds a shutdown simulator gets to boot (+ app install) before Play
//! gives up with an honest error - cold boots are slow, but never silent
const int PLAY_SIM_PREP_TIMEOUT_SECONDS = 180;
//! milliseconds between connect attempts while launching
const int PLAY_CONNECT_RETRY_MS = 250;
//! milliseconds a stopped player gets before it is killed
const int PLAY_STOP_GRACE_MS = 3000;

//! @brief run a command synchronously with captured stdout (used for the
//! short-lived simctl/adb/devicectl calls). False when the process cannot be
//! spawned; exitCode/output are only valid on true.
bool runProcessCaptured(const char* const* args, std::string& output,
	int& exitCode)
{
	SDL_Process* process = SDL_CreateProcess(args, true);
	if (!process)
	{
		return false;
	}
	size_t outputSize = 0;
	void* data = SDL_ReadProcess(process, &outputSize, &exitCode);
	output.assign(data ? static_cast<char*>(data) : "", data ? outputSize : 0);
	SDL_free(data);
	SDL_DestroyProcess(process);
	return true;
}

//! vector-of-strings convenience wrapper around runProcessCaptured
bool runProcessCaptured(std::vector<std::string> const& args,
	std::string& output, int& exitCode)
{
	std::vector<const char*> argv;
	argv.reserve(args.size() + 1);
	for (std::string const& arg : args)
	{
		argv.push_back(arg.c_str());
	}
	argv.push_back(nullptr);
	return runProcessCaptured(argv.data(), output, exitCode);
}

//--- Android play targets (any desktop host with an SDK) -------------------

//! package + activity the player APK installs as (tools/player/android/)
const char* const PLAY_ANDROID_PACKAGE = "com.orkitec.orkigeplayer";
const char* const PLAY_ANDROID_ACTIVITY =
	"com.orkitec.orkigeplayer/.OrkigeActivity";
//! the temp scene's name inside the app files dir (delivered via adb)
const char* const PLAY_ANDROID_SCENE_NAME = "orkige_play.oscene";

//! adb from ANDROID_HOME (default: the per-user SDK), PATH as last resort
std::string adbPath()
{
	const char* androidHome = std::getenv("ANDROID_HOME");
	std::string sdk = androidHome ? androidHome : "";
	if (sdk.empty())
	{
		if (const char* home = std::getenv("HOME"))
		{
			sdk = std::string(home) + "/Library/Android/sdk";
		}
	}
	const std::string adb = sdk + "/platform-tools/adb";
	std::error_code ignored;
	if (!sdk.empty() && std::filesystem::exists(adb, ignored))
	{
		return adb;
	}
	return "adb";
}

//! a connected Android device or emulator (Play deployment target)
struct AndroidDevice
{
	std::string serial;
	std::string label;
};

//! @brief connected Android devices/emulators via 'adb devices -l'. Lines
//! look like "emulator-5554  device product:... model:sdk_gphone64_arm64 ..."
//! - only state "device" qualifies (offline/unauthorized are skipped). An
//! empty result also covers a missing adb - the picker then simply offers no
//! Android entries.
std::vector<AndroidDevice> listAdbDevices()
{
	std::vector<AndroidDevice> devices;
	std::string output;
	int exitCode = 0;
	if (!runProcessCaptured({ adbPath(), "devices", "-l" }, output, exitCode) ||
		exitCode != 0)
	{
		return devices;
	}
	std::istringstream stream(output);
	std::string line;
	while (std::getline(stream, line))
	{
		std::istringstream fields(line);
		AndroidDevice device;
		std::string state;
		if (!(fields >> device.serial >> state) || state != "device")
		{
			continue; // header, blank and non-ready lines
		}
		device.label = device.serial;
		std::string field;
		while (fields >> field)
		{
			if (field.rfind("model:", 0) == 0)
			{
				device.label = field.substr(6) + " (" + device.serial + ")";
			}
		}
		devices.push_back(std::move(device));
	}
	return devices;
}

//! true when the given adb device has the player APK installed
bool androidPlayerInstalled(std::string const& serial)
{
	std::string output;
	int exitCode = 0;
	return runProcessCaptured({ adbPath(), "-s", serial, "shell", "pm", "path",
		PLAY_ANDROID_PACKAGE }, output, exitCode) && exitCode == 0 &&
		output.find("package:") != std::string::npos;
}

#ifdef __APPLE__
//! bundle id the iOS player app is installed under (tools/player/CMakeLists.txt)
const char* const PLAY_SIMULATOR_BUNDLE_ID = "com.orkitec.orkige-player";

//! an available iOS simulator device (Play deployment target); a shutdown
//! one is booted on Play (simctl boot + Simulator.app - see startPlay)
struct SimulatorDevice
{
	std::string name;
	std::string udid;
	bool booted = false;
};

//! @brief available iOS simulators (ANY state) via 'xcrun simctl list
//! devices available'. Only devices under the "-- iOS" runtime sections
//! qualify (tvOS/watchOS cannot run the player). Lines look like
//! "    iPhone 16 (137F509C-....) (Booted)"; transient states (Booting,
//! Shutting Down) are skipped. An empty result also covers simctl failures -
//! the picker then simply offers only "Desktop".
std::vector<SimulatorDevice> listSimulators()
{
	std::vector<SimulatorDevice> devices;
	const char* args[] = { "/usr/bin/xcrun", "simctl", "list", "devices",
		"available", nullptr };
	std::string output;
	int exitCode = 0;
	if (!runProcessCaptured(args, output, exitCode) || exitCode != 0)
	{
		return devices;
	}
	std::istringstream stream(output);
	std::string line;
	bool inIosSection = false;
	while (std::getline(stream, line))
	{
		// strip trailing whitespace/CR (simctl pads the state suffix)
		const std::size_t lineEnd = line.find_last_not_of(" \t\r");
		if (lineEnd == std::string::npos)
		{
			continue;
		}
		line.resize(lineEnd + 1);
		if (line.rfind("-- ", 0) == 0)
		{
			inIosSection = (line.rfind("-- iOS", 0) == 0);
			continue;
		}
		if (!inIosSection)
		{
			continue;
		}
		// "<name> (<udid>) (Booted|Shutdown)"
		bool booted = false;
		std::size_t statePos = line.rfind(" (Booted)");
		if (statePos != std::string::npos)
		{
			booted = true;
		}
		else
		{
			statePos = line.rfind(" (Shutdown)");
		}
		if (statePos == std::string::npos || statePos < 3)
		{
			continue;
		}
		const std::size_t udidOpen = line.rfind(" (", statePos - 1);
		if (udidOpen == std::string::npos || statePos < udidOpen + 3)
		{
			continue;
		}
		SimulatorDevice device;
		device.booted = booted;
		device.udid = line.substr(udidOpen + 2, statePos - udidOpen - 3);
		device.name = line.substr(0, udidOpen);
		const std::size_t nameStart = device.name.find_first_not_of(" \t");
		device.name = (nameStart == std::string::npos)
			? "" : device.name.substr(nameStart);
		if (!device.name.empty() && !device.udid.empty())
		{
			devices.push_back(std::move(device));
		}
	}
	return devices;
}

//! is the simulator currently Booted? (false also when it is not in the
//! available-device list at all)
bool simulatorIsBooted(std::string const& udid)
{
	for (SimulatorDevice const& device : listSimulators())
	{
		if (device.udid == udid)
		{
			return device.booted;
		}
	}
	return false;
}

//! true when OrkigePlayer.app is installed on the (BOOTED) simulator -
//! 'simctl get_app_container' only answers for booted devices
bool simulatorPlayerInstalled(std::string const& udid)
{
	const char* args[] = { "/usr/bin/xcrun", "simctl", "get_app_container",
		udid.c_str(), PLAY_SIMULATOR_BUNDLE_ID, "app", nullptr };
	std::string output;
	int exitCode = 0;
	return runProcessCaptured(args, output, exitCode) && exitCode == 0;
}

//--- iOS hardware (task: physical-device tooling, honestly gated) ----------

//! @brief is a codesigning identity available? Deploying to iOS HARDWARE
//! (unlike the simulator) requires the app to be signed with an Apple
//! Developer identity - without one the device entries stay disabled with an
//! explanatory tooltip.
bool hasCodesignIdentity()
{
	const char* args[] = { "/usr/bin/security", "find-identity", "-v", "-p",
		"codesigning", nullptr };
	std::string output;
	int exitCode = 0;
	if (!runProcessCaptured(args, output, exitCode) || exitCode != 0)
	{
		return false;
	}
	return output.find("0 valid identities found") == std::string::npos &&
		output.find("valid identities found") != std::string::npos;
}

//! a physically connected iOS device (enumerated, not yet deployable)
struct IosHardwareDevice
{
	std::string name;
	std::string udid;
};

//! @brief connected iOS hardware via 'xcrun devicectl list devices'. The
//! human-readable table has fixed labels; rather than depending on column
//! widths, ask for the json dump and scan it crudely for the
//! identifier/name pairs (the editor has no JSON parser - the two keys are
//! unambiguous in devicectl's output). Empty on any failure.
std::vector<IosHardwareDevice> listIosHardwareDevices()
{
	std::vector<IosHardwareDevice> devices;
	const std::string jsonPath =
		(std::filesystem::temp_directory_path() / "orkige_devicectl.json")
			.string();
	const char* args[] = { "/usr/bin/xcrun", "devicectl", "list", "devices",
		"--json-output", jsonPath.c_str(), nullptr };
	std::string output;
	int exitCode = 0;
	if (!runProcessCaptured(args, output, exitCode) || exitCode != 0)
	{
		return devices;
	}
	std::ifstream json(jsonPath);
	std::string text((std::istreambuf_iterator<char>(json)),
		std::istreambuf_iterator<char>());
	std::error_code ignored;
	std::filesystem::remove(jsonPath, ignored);
	// each device object carries "identifier" : "<udid>" and (deviceProperties)
	// "name" : "<user-visible name>"; scan pairwise
	std::size_t searchPos = 0;
	while (true)
	{
		const std::size_t idKey = text.find("\"identifier\"", searchPos);
		if (idKey == std::string::npos)
		{
			break;
		}
		IosHardwareDevice device;
		std::size_t valueStart = text.find('"', text.find(':', idKey) + 1);
		std::size_t valueEnd = (valueStart == std::string::npos)
			? std::string::npos : text.find('"', valueStart + 1);
		if (valueEnd != std::string::npos)
		{
			device.udid = text.substr(valueStart + 1, valueEnd - valueStart - 1);
		}
		const std::size_t nameKey = text.find("\"name\"", idKey);
		if (nameKey != std::string::npos)
		{
			valueStart = text.find('"', text.find(':', nameKey) + 1);
			valueEnd = (valueStart == std::string::npos)
				? std::string::npos : text.find('"', valueStart + 1);
			if (valueEnd != std::string::npos)
			{
				device.name =
					text.substr(valueStart + 1, valueEnd - valueStart - 1);
			}
		}
		if (!device.udid.empty() && !device.name.empty())
		{
			devices.push_back(std::move(device));
		}
		searchPos = idKey + 1;
	}
	return devices;
}
#endif // __APPLE__

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
	session.remoteLogSeen = false;
	session.remoteHierarchy.clear();
	session.scriptErrorIds.clear();
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
	if (session.buildProcess)
	{
		// a running native module build dies with the session (Stop while
		// building = cancel; a failed build also funnels through here)
		int buildExit = 0;
		if (!SDL_WaitProcess(session.buildProcess, false, &buildExit))
		{
			SDL_KillProcess(session.buildProcess, true);
			SDL_WaitProcess(session.buildProcess, true, &buildExit);
		}
		SDL_DestroyProcess(session.buildProcess);
		session.buildProcess = nullptr;
	}
	session.buildSteps.clear();
	session.buildOutputBuffer.clear();
	session.nativeExecutable.clear();
	session.nativeTarget.clear();
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
	if (session.simPrepProcess)
	{
		// a still-running simctl boot/install is left to finish on its own
		// (killing simctl mid-boot can wedge CoreSimulator) - only the handle
		// is dropped; the session itself is over
		int prepExit = 0;
		if (!SDL_WaitProcess(session.simPrepProcess, false, &prepExit))
		{
			SDL_Log("orkige_editor: play - abandoning the running simulator "
				"preparation step (simctl finishes in the background)");
		}
		SDL_DestroyProcess(session.simPrepProcess);
		session.simPrepProcess = nullptr;
	}
	session.simPrep = PlaySession::SimPrep::None;
	session.launchStatus.clear();
#ifdef __APPLE__
	if (session.onSimulator)
	{
		// belt-and-braces: MSG_QUIT normally ends the app, but a hung or
		// disconnected player must not keep running on the device
		const char* terminateArgs[] = { "/usr/bin/xcrun", "simctl",
			"terminate", session.simulatorUdid.c_str(),
			PLAY_SIMULATOR_BUNDLE_ID, nullptr };
		std::string terminateOutput;
		int terminateExit = 0;
		runProcessCaptured(terminateArgs, terminateOutput, terminateExit);
	}
#endif
	if (session.onAndroid)
	{
		// belt-and-braces stop, mirroring the simulator teardown
		std::string ignoredOutput;
		int ignoredExit = 0;
		runProcessCaptured({ adbPath(), "-s", session.androidSerial, "shell",
			"am", "force-stop", PLAY_ANDROID_PACKAGE },
			ignoredOutput, ignoredExit);
	}
	if (session.androidForwarded)
	{
		const std::string portSpec = "tcp:" + std::to_string(session.port);
		std::string ignoredOutput;
		int ignoredExit = 0;
		runProcessCaptured({ adbPath(), "-s", session.androidSerial,
			"forward", "--remove", portSpec }, ignoredOutput, ignoredExit);
		session.androidForwarded = false;
	}
	session.onAndroid = false;
	session.onSimulator = false;
	session.client.disconnect();
	if (!session.tempScenePath.empty())
	{
		std::error_code ignored;
		std::filesystem::remove(session.tempScenePath, ignored);
		session.tempScenePath.clear();
	}
	clearRemoteState(session);
	session.projectRoot.clear();
	session.mode = PlaySession::Mode::Edit;
	SDL_Log("orkige_editor: play mode ended (%s)", reason.c_str());
}

#ifdef __APPLE__
//! @brief reap a finished simulator prep process (simctl boot/install);
//! false while it is still running. Output and exit code are only valid on
//! true; the process handle is destroyed.
bool reapSimPrepProcess(PlaySession& session, std::string& output,
	int& exitCode)
{
	if (!session.simPrepProcess ||
		!SDL_WaitProcess(session.simPrepProcess, false, &exitCode))
	{
		return false;
	}
	size_t outputSize = 0;
	void* data =
		SDL_ReadProcess(session.simPrepProcess, &outputSize, &exitCode);
	output.assign(data ? static_cast<char*>(data) : "", data ? outputSize : 0);
	SDL_free(data);
	SDL_DestroyProcess(session.simPrepProcess);
	session.simPrepProcess = nullptr;
	return true;
}

//! @brief launch the installed player app on the (booted) simulator - the
//! final SimPrep step; on success the normal connect loop takes over with a
//! fresh connect-timeout window
void launchPlayerOnSimulator(PlaySession& session)
{
	const std::string portString = std::to_string(session.port);
	// simulator apps read the host filesystem, so the project root travels
	// as a plain --project path exactly like on the desktop
	std::vector<std::string> launchArgs = { "/usr/bin/xcrun", "simctl",
		"launch", "--terminate-running-process", session.simulatorUdid,
		PLAY_SIMULATOR_BUNDLE_ID, session.tempScenePath,
		"--debug-port", portString };
	if (!session.projectRoot.empty())
	{
		launchArgs.push_back("--project");
		launchArgs.push_back(session.projectRoot);
	}
	std::string launchOutput;
	int launchExit = 0;
	if (!runProcessCaptured(launchArgs, launchOutput, launchExit) ||
		launchExit != 0)
	{
		SDL_Log("orkige_editor: play failed - simctl launch on '%s' (exit "
			"%d): %s", session.simulatorLabel.c_str(), launchExit,
			launchOutput.c_str());
		endPlaySession(session, "simctl launch failed");
		return;
	}
	session.simPrep = PlaySession::SimPrep::None;
	session.launchStatus = "launching player on '" + session.simulatorLabel +
		"'...";
	// the connect window starts NOW - boot/install time must not eat into it
	session.launchStart = std::chrono::steady_clock::now();
	session.lastConnectAttempt = std::chrono::steady_clock::time_point();
	SDL_Log("orkige_editor: play - launched player on simulator '%s' (scene "
		"'%s', port %u)", session.simulatorLabel.c_str(),
		session.tempScenePath.c_str(), static_cast<unsigned>(session.port));
}

//! @brief per-frame simulator preparation pump (mode == Launching with
//! simPrep != None): wait for boot -> poll until Booted -> app check (auto-
//! install from the ios-simulator-debug build when missing) -> launch. Every
//! failure and the hard timeout end the session with an honest Console line.
void advanceSimulatorPrep(PlaySession& session)
{
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	if (now - session.simPrepStart >
		std::chrono::seconds(PLAY_SIM_PREP_TIMEOUT_SECONDS))
	{
		SDL_Log("orkige_editor: play failed - simulator '%s' did not become "
			"ready within %d seconds", session.simulatorLabel.c_str(),
			PLAY_SIM_PREP_TIMEOUT_SECONDS);
		endPlaySession(session, "simulator preparation timed out");
		return;
	}
	switch (session.simPrep)
	{
	case PlaySession::SimPrep::WaitBootProcess:
	{
		std::string output;
		int exitCode = 0;
		if (!reapSimPrepProcess(session, output, exitCode))
		{
			return; // simctl boot still running
		}
		// 'simctl boot' on an already-booted device exits 149 ("current
		// state: Booted") - a race with a manually started boot, not an error
		if (exitCode != 0 &&
			output.find("current state: Booted") == std::string::npos)
		{
			SDL_Log("orkige_editor: play failed - 'simctl boot %s' exited "
				"with %d: %s", session.simulatorUdid.c_str(), exitCode,
				output.c_str());
			endPlaySession(session, "simctl boot failed");
			return;
		}
		session.simPrep = PlaySession::SimPrep::WaitBooted;
		session.launchStatus = "waiting for simulator '" +
			session.simulatorLabel + "' to finish booting...";
		return;
	}
	case PlaySession::SimPrep::WaitBooted:
	{
		// poll the device state once a second (simctl list is not free)
		if (session.simLastPoll != std::chrono::steady_clock::time_point() &&
			now - session.simLastPoll < std::chrono::milliseconds(1000))
		{
			return;
		}
		session.simLastPoll = now;
		if (!simulatorIsBooted(session.simulatorUdid))
		{
			return; // still coming up
		}
		// booted - is the player app on it?
		if (simulatorPlayerInstalled(session.simulatorUdid))
		{
			launchPlayerOnSimulator(session);
			return;
		}
		std::error_code ignored;
		if (!std::filesystem::exists(ORKIGE_EDITOR_IOS_PLAYER_APP, ignored))
		{
			SDL_Log("orkige_editor: play failed - OrkigePlayer.app is "
				"neither installed on '%s' nor built at '%s'. Build the iOS "
				"player first: cmake --preset ios-simulator-debug && cmake "
				"--build --preset ios-simulator-debug",
				session.simulatorLabel.c_str(), ORKIGE_EDITOR_IOS_PLAYER_APP);
			endPlaySession(session, "player app not available");
			return;
		}
		const char* installArgs[] = { "/usr/bin/xcrun", "simctl", "install",
			session.simulatorUdid.c_str(), ORKIGE_EDITOR_IOS_PLAYER_APP,
			nullptr };
		session.simPrepProcess = SDL_CreateProcess(installArgs, true);
		if (!session.simPrepProcess)
		{
			SDL_Log("orkige_editor: play failed - could not run 'simctl "
				"install': %s", SDL_GetError());
			endPlaySession(session, "simctl install spawn failed");
			return;
		}
		session.simPrep = PlaySession::SimPrep::Installing;
		session.launchStatus = "installing player on '" +
			session.simulatorLabel + "'...";
		SDL_Log("orkige_editor: play - installing '%s' on '%s'",
			ORKIGE_EDITOR_IOS_PLAYER_APP, session.simulatorLabel.c_str());
		return;
	}
	case PlaySession::SimPrep::Installing:
	{
		std::string output;
		int exitCode = 0;
		if (!reapSimPrepProcess(session, output, exitCode))
		{
			return; // simctl install still running
		}
		if (exitCode != 0)
		{
			SDL_Log("orkige_editor: play failed - 'simctl install' exited "
				"with %d: %s", exitCode, output.c_str());
			endPlaySession(session, "simctl install failed");
			return;
		}
		SDL_Log("orkige_editor: play - player installed on '%s'",
			session.simulatorLabel.c_str());
		launchPlayerOnSimulator(session);
		return;
	}
	case PlaySession::SimPrep::None:
		return;
	}
}
#endif // __APPLE__

//! @brief spawn a desktop play process (the generic player or a project's
//! built native module executable) on the session's temp scene + debug port
//! and enter Launching. The automation env hooks aimed at THIS editor
//! process (frame caps, screenshot paths) must not leak into the spawned
//! process - it honours the same variables and would e.g. exit after N
//! frames or overwrite the requested screenshot.
bool spawnDesktopPlayProcess(PlaySession& session,
	std::string const& executablePath)
{
	const std::string portString = std::to_string(session.port);
	std::vector<const char*> args = { executablePath.c_str(),
		session.tempScenePath.c_str(), "--debug-port", portString.c_str() };
	if (!session.projectRoot.empty())
	{
		// with a project open the process plays IN that project: its assets/
		// and scenes/ register as resource locations over there too
		args.push_back("--project");
		args.push_back(session.projectRoot.c_str());
	}
	args.push_back(nullptr);
	SDL_Environment* playerEnvironment = SDL_CreateEnvironment(true);
	SDL_UnsetEnvironmentVariable(playerEnvironment, "ORKIGE_DEMO_FRAMES");
	SDL_UnsetEnvironmentVariable(playerEnvironment, "ORKIGE_DEMO_SCREENSHOT");
	SDL_PropertiesID spawnProperties = SDL_CreateProperties();
	SDL_SetPointerProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
		const_cast<char**>(args.data()));
	SDL_SetPointerProperty(spawnProperties,
		SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, playerEnvironment);
	session.process = SDL_CreateProcessWithProperties(spawnProperties);
	SDL_DestroyProperties(spawnProperties);
	SDL_DestroyEnvironment(playerEnvironment);
	if (!session.process)
	{
		SDL_Log("orkige_editor: play failed - SDL_CreateProcess '%s': %s",
			executablePath.c_str(), SDL_GetError());
		endPlaySession(session, "spawn failed");
		return false;
	}
	clearRemoteState(session);
	session.mode = PlaySession::Mode::Launching;
	session.launchStatus.clear();
	session.launchStart = std::chrono::steady_clock::now();
	session.lastConnectAttempt = std::chrono::steady_clock::time_point();
	SDL_Log("orkige_editor: play - spawned '%s' (scene '%s', port %u)",
		executablePath.c_str(), session.tempScenePath.c_str(),
		static_cast<unsigned>(session.port));
	return true;
}

//! @brief start the next queued native-module build step (mode == Building):
//! stdout+stderr are piped so updatePlaySession streams them into the
//! Console as "[build]" lines. False (with the session ended) when the
//! process cannot be spawned.
bool startNextBuildStep(PlaySession& session)
{
	if (session.buildSteps.empty())
	{
		return false;
	}
	const std::vector<std::string> step = session.buildSteps.front();
	session.buildSteps.erase(session.buildSteps.begin());
	std::vector<const char*> args;
	std::string commandLine;
	args.reserve(step.size() + 1);
	for (std::string const& arg : step)
	{
		args.push_back(arg.c_str());
		commandLine += (commandLine.empty() ? "" : " ") + arg;
	}
	args.push_back(nullptr);
	SDL_PropertiesID spawnProperties = SDL_CreateProperties();
	SDL_SetPointerProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
		const_cast<char**>(args.data()));
	SDL_SetNumberProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
		SDL_PROCESS_STDIO_APP);
	SDL_SetBooleanProperty(spawnProperties,
		SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
	session.buildProcess = SDL_CreateProcessWithProperties(spawnProperties);
	SDL_DestroyProperties(spawnProperties);
	if (!session.buildProcess)
	{
		SDL_Log("[build] FAILED to run '%s': %s", commandLine.c_str(),
			SDL_GetError());
		endPlaySession(session, "native build spawn failed");
		return false;
	}
	// the SDL log hook mirrors this into the Console as a "[build]" line
	SDL_Log("[build] $ %s", commandLine.c_str());
	return true;
}

//! @brief drain whatever the running build step wrote (non-blocking pipe)
//! into the Console, one "[build]"-tagged line each; compiler diagnostics
//! colour as errors/warnings so a failed Play is impossible to miss
void pumpBuildOutput(PlaySession& session, EditorConsole& console,
	bool flushPartialLine)
{
	SDL_IOStream* output = session.buildProcess
		? SDL_GetProcessOutput(session.buildProcess) : nullptr;
	if (output)
	{
		char chunk[4096];
		size_t bytesRead = 0;
		while ((bytesRead = SDL_ReadIO(output, chunk, sizeof(chunk))) > 0)
		{
			session.buildOutputBuffer.append(chunk, bytesRead);
		}
	}
	std::size_t lineStart = 0;
	std::size_t newline = std::string::npos;
	auto emitLine = [&console](std::string const& text)
	{
		ConsoleLevel level = ConsoleLevel::Info;
		if (text.find("error") != std::string::npos ||
			text.find("FAILED") != std::string::npos)
		{
			level = ConsoleLevel::Error;
		}
		else if (text.find("warning") != std::string::npos)
		{
			level = ConsoleLevel::Warning;
		}
		console.addLine(level, "[build] " + text);
	};
	while ((newline = session.buildOutputBuffer.find('\n', lineStart)) !=
		std::string::npos)
	{
		std::string line =
			session.buildOutputBuffer.substr(lineStart, newline - lineStart);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		emitLine(line);
		lineStart = newline + 1;
	}
	session.buildOutputBuffer.erase(0, lineStart);
	if (flushPartialLine && !session.buildOutputBuffer.empty())
	{
		emitLine(session.buildOutputBuffer);
		session.buildOutputBuffer.clear();
	}
}

//! @brief Play: save the current scene to a temp play file, spawn the
//! player with --debug-port on a probed free port and start connecting.
//! The user's scene file and the editor world stay untouched.
//! @param project the open project (unloaded = loose-scene mode). Its root
//! is forwarded to the play process as --project so the project's resource
//! roots (and its scripts) resolve identically in the runtime; a project
//! with native module settings switches Play to compile-on-Play (build the
//! module, then run ITS executable instead of the generic player).
bool startPlay(PlaySession& session,
	Orkige::GameObjectManager& gameObjectManager,
	Orkige::Project const& project)
{
	if (session.isActive())
	{
		return false;
	}
	const std::string projectRoot = project.getRootDirectory();
	const Orkige::NativeModule::Config nativeConfig = project.isLoaded()
		? Orkige::NativeModule::configFromProject(project)
		: Orkige::NativeModule::Config();
	if (nativeConfig.enabled &&
		(!session.simulatorUdid.empty() || !session.androidSerial.empty()))
	{
		// honest refusal instead of silently playing the wrong binary: the
		// module was built for the desktop host - device builds of native
		// modules are the export milestone's job
		SDL_Log("orkige_editor: play refused - project '%s' has a native "
			"module ('%s'), which can only play on the Desktop target for "
			"now (device targets need the export pipeline)",
			project.getName().c_str(), nativeConfig.target.c_str());
		return false;
	}
	if (nativeConfig.enabled && !session.desktopPlayerPath.empty())
	{
		// same honesty for the cross-flavor desktop target: the module
		// compiles and links against THIS editor's build tree - it cannot
		// play on the other render flavor's runtime
		SDL_Log("orkige_editor: play refused - project '%s' has a native "
			"module ('%s'), which plays its own executable built against "
			"this editor's render flavor (pick the plain Desktop target)",
			project.getName().c_str(), nativeConfig.target.c_str());
		return false;
	}
	session.projectRoot = projectRoot;
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
	if (nativeConfig.enabled)
	{
		// compile-on-Play: queue configure (only when the build tree has no
		// cache yet) + incremental build of the project's native module and
		// enter Building; updatePlaySession pumps the output into the
		// Console and launches the module executable on success. All paths
		// (cmake, engine build tree, toolchain settings) are this editor
		// build's own constants - the engine libs the module links are the
		// ones this very editor runs on.
		const std::string moduleSourceDir =
			project.resolvePath(nativeConfig.cmakeDir);
		const std::string moduleBuildDir =
			project.resolvePath(nativeConfig.buildDir);
		std::error_code ignored;
		if (!std::filesystem::exists(
			std::filesystem::path(moduleSourceDir) / "CMakeLists.txt", ignored))
		{
			SDL_Log("orkige_editor: play failed - project '%s' declares "
				"native module '%s' but '%s' has no CMakeLists.txt",
				project.getName().c_str(), nativeConfig.target.c_str(),
				moduleSourceDir.c_str());
			endPlaySession(session, "native module misconfigured");
			return false;
		}
		Orkige::StringVector extraArguments = {
			std::string("-DCMAKE_MAKE_PROGRAM=") + ORKIGE_EDITOR_MAKE_PROGRAM,
			std::string("-DORKIGE_SCRIPTING=") + ORKIGE_EDITOR_SCRIPTING,
			// hermeticity, same as the presets: never let the module build
			// pick up the banned /usr/local prefix
			"-DCMAKE_IGNORE_PREFIX_PATH=/usr/local",
		};
#ifdef __APPLE__
		if (ORKIGE_EDITOR_OSX_SYSROOT[0] != '\0')
		{
			extraArguments.push_back(std::string("-DCMAKE_OSX_SYSROOT=") +
				ORKIGE_EDITOR_OSX_SYSROOT);
		}
#endif
		session.buildSteps.clear();
		if (Orkige::NativeModule::needsConfigure(moduleBuildDir))
		{
			session.buildSteps.push_back(
				Orkige::NativeModule::configureCommand(ORKIGE_EDITOR_CMAKE,
					moduleSourceDir, moduleBuildDir,
					ORKIGE_EDITOR_ENGINE_ROOT, ORKIGE_EDITOR_ENGINE_BUILD_DIR,
					ORKIGE_EDITOR_BUILD_TYPE, extraArguments));
		}
		session.buildSteps.push_back(Orkige::NativeModule::buildCommand(
			ORKIGE_EDITOR_CMAKE, moduleBuildDir));
		session.nativeExecutable = Orkige::NativeModule::executablePath(
			moduleBuildDir, nativeConfig.target);
		session.nativeTarget = nativeConfig.target;
		clearRemoteState(session);
		session.mode = PlaySession::Mode::Building;
		session.launchStatus = "building native module '" +
			nativeConfig.target + "'...";
		SDL_Log("[build] building native module '%s' of project '%s' "
			"(build dir '%s')", nativeConfig.target.c_str(),
			project.getName().c_str(), moduleBuildDir.c_str());
		return startNextBuildStep(session);
	}
	if (!session.androidSerial.empty())
	{
		// Play on a connected Android device/emulator: unlike the iOS
		// simulator, the device shares neither the filesystem nor the
		// loopback with the host. The temp scene is dropped into the app
		// files dir (adb push to /data/local/tmp + run-as copy - the
		// standard debuggable-app file drop) and the debug link rides an
		// 'adb forward tcp' bridge: the player LISTENS on the device
		// loopback, the editor keeps connecting to 127.0.0.1 on the host.
		// Physical Android phones deploy through this identical flow.
		const std::string adb = adbPath();
		const std::string portString = std::to_string(session.port);
		const std::string portSpec = "tcp:" + portString;
		const std::string devicePushPath =
			std::string("/data/local/tmp/") + PLAY_ANDROID_SCENE_NAME;
		const std::string deviceScenePath =
			std::string("files/") + PLAY_ANDROID_SCENE_NAME;
		const std::vector<std::vector<std::string>> steps = {
			// a fresh process (and thus a fresh SDL_main) per session
			{ adb, "-s", session.androidSerial, "shell",
				"am", "force-stop", PLAY_ANDROID_PACKAGE },
			{ adb, "-s", session.androidSerial, "push",
				session.tempScenePath, devicePushPath },
			{ adb, "-s", session.androidSerial, "shell",
				"run-as", PLAY_ANDROID_PACKAGE, "mkdir", "-p", "files" },
			{ adb, "-s", session.androidSerial, "shell",
				"run-as", PLAY_ANDROID_PACKAGE, "cp",
				devicePushPath, deviceScenePath },
			{ adb, "-s", session.androidSerial,
				"forward", portSpec, portSpec },
			// scene + port travel as intent extras; OrkigeActivity turns
			// them into the SDL_main argv (the relative scene path is
			// resolved against the app files dir by the player)
			{ adb, "-s", session.androidSerial, "shell",
				"am", "start", "-n", PLAY_ANDROID_ACTIVITY,
				"--es", "scene", PLAY_ANDROID_SCENE_NAME,
				"--ei", "debugPort", portString },
		};
		for (std::vector<std::string> const& step : steps)
		{
			std::string output;
			int exitCode = 0;
			if (!runProcessCaptured(step, output, exitCode) || exitCode != 0)
			{
				SDL_Log("orkige_editor: play failed - adb step '%s' on '%s' "
					"(exit %d): %s (is the player APK installed? see "
					"tools/player/android/package_apk.sh)",
					step[3].c_str(), session.androidLabel.c_str(), exitCode,
					output.c_str());
				endPlaySession(session, "adb deploy failed");
				return false;
			}
			if (step[3] == "forward")
			{
				session.androidForwarded = true;
			}
		}
		session.onAndroid = true;
		clearRemoteState(session);
		session.mode = PlaySession::Mode::Launching;
		session.launchStart = std::chrono::steady_clock::now();
		session.lastConnectAttempt = std::chrono::steady_clock::time_point();
		SDL_Log("orkige_editor: play - launched player on Android '%s' "
			"(scene '%s', forwarded port %u)", session.androidLabel.c_str(),
			session.tempScenePath.c_str(), static_cast<unsigned>(session.port));
		return true;
	}
#ifdef __APPLE__
	if (!session.simulatorUdid.empty())
	{
		// Play on an iOS simulator. A booted one goes straight to the app
		// check + launch; a SHUTDOWN one is booted first ('simctl boot' is
		// headless, so Simulator.app is opened alongside to bring the device
		// window up), then checked/auto-installed, then launched - all
		// asynchronously through the SimPrep pipeline (advanceSimulatorPrep)
		// with live status text in the toolbar, never a silent no-op.
		// Simulator apps read the host filesystem and share the host
		// loopback, so the temp scene path and the 127.0.0.1 debug link work
		// unchanged. simctl launch returns as soon as the app process starts,
		// so session.process stays null and liveness is tracked purely
		// through the debug link.
		const bool booted = simulatorIsBooted(session.simulatorUdid);
		session.onSimulator = true;
		clearRemoteState(session);
		session.mode = PlaySession::Mode::Launching;
		session.launchStart = std::chrono::steady_clock::now();
		session.lastConnectAttempt = std::chrono::steady_clock::time_point();
		session.simPrepStart = session.launchStart;
		session.simLastPoll = std::chrono::steady_clock::time_point();
		session.simulatorBootedByEditor = false;
		// bring the Simulator UI up in every case: 'simctl boot' alone is
		// headless, and even a Booted device shows no window when
		// Simulator.app was quit - "Play didn't open the simulator" must not
		// happen again. A failed open is logged and the run continues
		// headless (the player still works, only the window is missing).
		{
			const char* openArgs[] = { "/usr/bin/open", "-a", "Simulator",
				"--args", "-CurrentDeviceUDID",
				session.simulatorUdid.c_str(), nullptr };
			std::string openOutput;
			int openExit = 0;
			if (!runProcessCaptured(openArgs, openOutput, openExit) ||
				openExit != 0)
			{
				SDL_Log("orkige_editor: play - 'open -a Simulator' failed "
					"(exit %d): %s - continuing headless", openExit,
					openOutput.c_str());
			}
		}
		if (!booted)
		{
			const char* bootArgs[] = { "/usr/bin/xcrun", "simctl", "boot",
				session.simulatorUdid.c_str(), nullptr };
			session.simPrepProcess = SDL_CreateProcess(bootArgs, true);
			if (!session.simPrepProcess)
			{
				SDL_Log("orkige_editor: play failed - could not run 'simctl "
					"boot' for '%s': %s", session.simulatorLabel.c_str(),
					SDL_GetError());
				endPlaySession(session, "simctl boot spawn failed");
				return false;
			}
			session.simulatorBootedByEditor = true;
			session.simPrep = PlaySession::SimPrep::WaitBootProcess;
			session.launchStatus = "booting simulator '" +
				session.simulatorLabel + "'...";
			SDL_Log("orkige_editor: play - booting shutdown simulator '%s' "
				"(%s)", session.simulatorLabel.c_str(),
				session.simulatorUdid.c_str());
		}
		else
		{
			session.simPrep = PlaySession::SimPrep::WaitBooted;
			session.launchStatus = "checking player on '" +
				session.simulatorLabel + "'...";
		}
		return true;
	}
#endif
	// spawn the generic player: this build's own (ORKIGE_EDITOR_PLAYER_PATH,
	// baked in by CMake as $<TARGET_FILE:orkige_player>) or the OTHER render
	// flavor's binary when the toolbar picked it (the debug protocol is
	// flavor-agnostic). SDL's process API keeps the editor free of platform
	// spawn code; stdio stays inherited so the player log shows up in the
	// editor console.
	return spawnDesktopPlayProcess(session, session.desktopPlayerPath.empty()
		? ORKIGE_EDITOR_PLAYER_PATH : session.desktopPlayerPath);
}

//! Stop: ask the player to quit; updatePlaySession reaps it (or kills it
//! after the grace timeout)
void requestStopPlay(PlaySession& session)
{
	if (!session.isActive() || session.mode == PlaySession::Mode::Stopping)
	{
		return;
	}
	if (session.mode == PlaySession::Mode::Building)
	{
		// Stop while the native module compiles = cancel the build outright
		// (endPlaySession kills the cmake process); nothing was launched yet
		endPlaySession(session, "native build canceled");
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
//! messages (remote log lines land in the Console tagged "[remote]"), watch
//! the process and the link, enforce the stop grace timeout
void updatePlaySession(PlaySession& session, EditorConsole& console)
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
			// player log lines go into the Console tagged [remote]; severity
			// travels in the "level" field (errors always colour as errors)
			const std::string levelText = message.get(Protocol::FIELD_LEVEL);
			ConsoleLevel level = ConsoleLevel::Info;
			if (message.type == Protocol::MSG_ERROR || levelText == "error")
			{
				level = ConsoleLevel::Error;
			}
			else if (levelText == "warning")
			{
				level = ConsoleLevel::Warning;
			}
			console.addLine(level,
				"[remote] " + message.get(Protocol::FIELD_MESSAGE));
			session.remoteLogSeen = true;
		}
		else if (message.type == Protocol::MSG_SCRIPT_ERROR)
		{
			// a ScriptComponent failed in the player - make it LOUD: one RED
			// Console line per object per session (the player already dedupes
			// per connection; the set guards against reconnect repeats), plus
			// the toolbar marker and the hierarchy tint fed by scriptErrorIds
			const std::string id = message.get(Protocol::FIELD_ID);
			if (session.scriptErrorIds.insert(id).second)
			{
				console.addLine(ConsoleLevel::Error,
					"[remote] SCRIPT ERROR on '" + id + "': " +
					message.get(Protocol::FIELD_MESSAGE));
			}
		}
		else if (message.type == Protocol::MSG_BYE)
		{
			SDL_Log("orkige_editor: play - player said bye");
		}
		// unknown message types fall through silently on purpose: newer
		// players may add message types (the protocol grows additively)
	}
	int exitCode = 0;
	const bool processExited = session.process &&
		SDL_WaitProcess(session.process, false, &exitCode);
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	switch (session.mode)
	{
	case PlaySession::Mode::Building:
	{
		// native module compile-on-Play: stream the compiler output into
		// the Console, then either run the next step, launch the built
		// executable, or - on a nonzero exit - revert to edit mode with the
		// errors on record and NOTHING launched
		pumpBuildOutput(session, console, false);
		int buildExit = 0;
		if (!session.buildProcess ||
			!SDL_WaitProcess(session.buildProcess, false, &buildExit))
		{
			return; // still compiling; the editor stays responsive
		}
		pumpBuildOutput(session, console, true); // drain the tail
		SDL_DestroyProcess(session.buildProcess);
		session.buildProcess = nullptr;
		if (buildExit != 0)
		{
			console.addLine(ConsoleLevel::Error, "[build] native module '" +
				session.nativeTarget + "' failed (exit " +
				std::to_string(buildExit) + ") - staying in edit mode");
			endPlaySession(session, "native build failed");
			return;
		}
		if (!session.buildSteps.empty())
		{
			startNextBuildStep(session);
			return;
		}
		console.addLine(ConsoleLevel::Info, "[build] native module '" +
			session.nativeTarget + "' built - launching " +
			session.nativeExecutable);
		spawnDesktopPlayProcess(session, session.nativeExecutable);
		return;
	}
	case PlaySession::Mode::Launching:
#ifdef __APPLE__
		// simulator preparation (boot / install) runs before any launch or
		// connect attempt - the pump reports its own errors and timeout
		if (session.onSimulator &&
			session.simPrep != PlaySession::SimPrep::None)
		{
			advanceSimulatorPrep(session);
			return;
		}
#endif
		if (processExited)
		{
			endPlaySession(session, "player exited during launch (code " +
				std::to_string(exitCode) + ")");
			return;
		}
		// Android: the editor connects to adb's HOST-side forward listener,
		// which accepts immediately even while the app is still booting -
		// adb then drops the bridge when the device-side connect fails. A
		// TCP connect is therefore no proof of a live player there; only the
		// protocol hello is. Pre-hello drops simply feed the retry loop.
		if (session.client.isConnected() &&
			(!session.onAndroid || session.helloReceived))
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
		// simulator/Android sessions have no process handle - the link
		// closing is the quit confirmation (endPlaySession terminates the
		// remote app anyway, belt-and-braces)
		if ((session.onSimulator || session.onAndroid) &&
			!session.client.isConnected())
		{
			endPlaySession(session,
				session.onAndroid ? "stopped (android)" : "stopped (simulator)");
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
// facade RenderTexture (WP-A1.4; the old manual TU_RENDERTARGET pattern moved
// behind engine_render), and the Scene panel displays that texture via
// ImGui::Image. The ImTextureID comes from ImGuiFacadeRenderer::textureIdFor:
// it registers the facade HANDLE, and DrawLayer2D binds the target's CURRENT
// backend texture per draw - so the id is stable across resizes on every
// render flavor.
struct SceneRenderTarget
{
	optr<Orkige::RenderTexture> texture;
	optr<Orkige::RenderCamera> camera;
	int width = 0;
	int height = 0;
};

// (re)size the scene RTT: first call creates it (camera + editor viewport
// state), later calls resize-by-recreate behind the facade - the ImGui
// overlay resolves texture ids per draw call, so the one frame that could
// still show the vanished old texture degrades gracefully
void createSceneRenderTexture(SceneRenderTarget& target, int width, int height)
{
	if (!target.texture)
	{
		target.texture = Orkige::RenderSystem::get()->createRenderTexture(
			"EditorSceneRT", static_cast<unsigned int>(width),
			static_cast<unsigned int>(height));
		target.texture->setCamera(target.camera);
		// dark neutral backdrop, in tune with the macOS-dark editor theme
		target.texture->setBackgroundColour(
			Orkige::Color(0.09f, 0.10f, 0.12f));
		target.texture->setShadowsEnabled(true);
		// no 2D overlays in the scene panel (DrawLayer2D never renders
		// into RTTs by contract anyway - this is belt+braces)
		target.texture->setOverlaysEnabled(false);
	}
	else
	{
		// recreates the backend texture, keeps camera + viewport state and
		// re-derives the camera aspect
		target.texture->resize(static_cast<unsigned int>(width),
			static_cast<unsigned int>(height));
	}
	target.width = width;
	target.height = height;
}

// place the scene camera on its orbit sphere around the orbit target
// (the position math lives in EditorCamera.h, shared with the fly mode)
void applyOrbitCamera(EditorState const& state,
	optr<Orkige::RenderNode> const& cameraNode)
{
	cameraNode->setPosition(Orkige::editorCameraPosition(state.camera));
	// Orientation is built EXPLICITLY from the same yaw/pitch that place the
	// camera - NOT via lookAt: Node::lookAt rotates by shortest arc from the
	// CURRENT orientation, and doing that every navigation frame accumulates
	// roll (jiggling the mouse in fly mode visibly tilted the horizon).
	// yaw about world Y, then pitch about local X; -pitch because positive
	// orbit pitch raises the camera, which must look DOWN at the target.
	cameraNode->setOrientation(
		Orkige::Quat(Orkige::Degree(state.camera.yawDeg),
			Orkige::Vec3::UNIT_Y) *
		Orkige::Quat(Orkige::Degree(-state.camera.pitchDeg),
			Orkige::Vec3::UNIT_X));
}

// The ground-plane reference grid, all facade: the line list becomes a mesh
// resource through RenderWorld::createLineListMesh (the WP-A1.3 cube-service
// pattern - shared unlit "VertexColour" look, works on every render flavor)
// and instantiates onto its own root child node; the View menu toggles
// visibility. Query flags 0 keep it invisible to the click-picking ray
// queries (facade queryRay masks against them). Only the scene RTT renders
// it - the window is UI-only (showUIOnlyWindow).
// The returned mesh handle must stay alive with the node (RAII).
optr<Orkige::MeshInstance> createEditorGrid(Orkige::RenderWorld* world,
	optr<Orkige::RenderNode> const& gridNode)
{
	const int halfLineCount = 10;		// lines each side of the axes
	const float spacing = 1.0f;			// one world unit per cell
	const float extent = halfLineCount * spacing;
	const Orkige::Color minorColour(0.32f, 0.32f, 0.32f);
	const Orkige::Color axisXColour(0.75f, 0.30f, 0.30f);	// X axis line
	const Orkige::Color axisZColour(0.30f, 0.45f, 0.85f);	// Z axis line

	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	auto addSegment = [&](Orkige::Vec3 const& from, Orkige::Vec3 const& to,
		Orkige::Color const& colour)
	{
		points.push_back(from);
		points.push_back(to);
		colours.push_back(colour);
		colours.push_back(colour);
	};
	for (int i = -halfLineCount; i <= halfLineCount; ++i)
	{
		const float d = i * spacing;
		// line parallel to the X axis (constant z); the z=0 one IS the X axis
		addSegment(Orkige::Vec3(-extent, 0.0f, d),
			Orkige::Vec3(extent, 0.0f, d),
			(i == 0) ? axisXColour : minorColour);
		// line parallel to the Z axis (constant x); the x=0 one IS the Z axis
		addSegment(Orkige::Vec3(d, 0.0f, -extent),
			Orkige::Vec3(d, 0.0f, extent),
			(i == 0) ? axisZColour : minorColour);
	}
	world->createLineListMesh("EditorGrid.mesh", points.data(),
		colours.data(), points.size());
	optr<Orkige::MeshInstance> grid =
		world->createMeshInstance("EditorGrid.mesh");
	if (grid)
	{
		grid->setCastShadows(false);
		grid->setQueryFlags(0); // never a picking hit
		grid->attachTo(gridNode);
	}
	return grid;
}

// F: frame the selected object - retarget the orbit to the object's world
// bounds centre and fit the orbit distance to its bounding radius
void frameSelectedObject(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera)
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
	Orkige::Vec3 center = transform->getWorldPosition();
	float radius = 1.0f;
	const Orkige::AABB box = transform->getWorldAABB();
	if (box.isFinite() && !box.isNull())
	{
		center = box.getCenter();
		radius = std::max(box.getHalfSize().length(), 0.25f);
	}
	state.camera.target = center;
	const float halfFov = std::min(
		camera->getFOVy().valueRadians() * 0.5f, 1.2f);
	state.camera.distance = std::clamp(
		radius / std::sin(halfFov) * 1.25f, 2.0f, 200.0f);
}

// Unity-style double-click focus (Hierarchy entries; the Scene viewport's
// double-click runs its pick first and then frames the result): select the
// object AND frame it - the same orbit retarget/refit the F shortcut does.
// The edittest drives this exact function.
void focusObjectFromDoubleClick(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, std::string const& id)
{
	core.selectObject(id);
	frameSelectedObject(state, core, camera);
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

// viewport click-picking: cast a camera ray through the click point (facade
// RenderWorld::queryRay, AABB-level, nearest first) and select the nearest
// hit that belongs to a GameObject - a TransformComponent tags its node with
// itself as the user pointer, queryRay walks hits back up to the first tag.
// A Cmd/Ctrl click (additive) toggles the hit's selection-set membership
// instead of replacing the selection. AABB-level picking is right for the
// editor bootstrap; polygon-accurate picking is PhysicsWorld::castRay
// territory (against collision shapes) when the need arrives.
bool pickObjectAtCursor(Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera,
	float normalizedX, float normalizedY, bool additive = false)
{
	const Orkige::Ray3 ray =
		camera->viewportPointToRay(normalizedX, normalizedY);
	bool picked = false;
	for (Orkige::RenderWorld::RayQueryHit const& hit :
		Orkige::RenderSystem::get()->getWorld()->queryRay(ray))
	{
		if (!hit.userPointer)
		{
			continue; // not GameObject content (grid opts out via query flags)
		}
		// within the engine only TransformComponent tags scene nodes
		// (@see TransformComponent::getComponentFromNode)
		Orkige::GameObject* gameObject =
			static_cast<Orkige::TransformComponent*>(hit.userPointer)
				->getComponentOwner();
		if (gameObject)
		{
			if (additive)
			{
				core.toggleSelection(gameObject->getObjectID());
			}
			else
			{
				core.selectObject(gameObject->getObjectID());
			}
			picked = true;
			break;
		}
	}
	if (!picked && !additive)
	{
		// clicking empty space deselects, like Unity
		core.clearSelection();
	}
	return picked;
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
	recordRecentScene(path);
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
	recordRecentScene(path);
	return true;
}

//--- project handling (Unity-style "open a project, not a scene") ----------

// A loaded project registers its assets/ and scenes/ directories as engine
// resource locations in the DEDICATED "OrkigeProject" group (the player's
// --project mode registers the identical set). The dedicated group is what
// makes switching projects clean: destroyResourceGroup unloads and
// unindexes every resource that came from the outgoing project, so
// name-cached meshes never leak into the next one.

//! tear down the previous project's resource group (call with the world
//! already cleared - entities referencing those meshes must be gone)
void unregisterProjectResources()
{
	Orkige::RenderSystem::get()->destroyResourceGroup(
		Orkige::Project::RESOURCE_GROUP_NAME);
}

//! register the project's assets/ and scenes/ in the project group;
//! missing directories are skipped with an honest Console line
void registerProjectResources(Orkige::Project const& project)
{
	for (std::string const& projectDir : { project.getAssetsDirectory(),
		project.getScenesDirectory() })
	{
		std::error_code ignored;
		if (std::filesystem::is_directory(projectDir, ignored))
		{
			Orkige::RenderSystem::get()->addResourceLocation(projectDir,
				Orkige::RenderSystem::LT_FILESYSTEM,
				Orkige::Project::RESOURCE_GROUP_NAME);
		}
		else
		{
			SDL_Log("orkige_editor: project directory '%s' does not exist - "
				"not registered", projectDir.c_str());
		}
	}
}

// File > Open Project... / Open Recent Project / the scripted project test:
// accepts a project directory or a .orkproj manifest. The current world is
// cleared FIRST (its entities may reference the outgoing project's
// resources), then the old project group is torn down, the new project's
// roots registered and its main scene opened (a project without a main
// scene starts on an empty untitled scene). On a failed load nothing
// changes - the previously open project stays open.
bool openProjectFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	Orkige::Project project;
	std::string error;
	if (!project.load(path, &error))
	{
		SDL_Log("orkige_editor: opening project failed - %s", error.c_str());
		return false;
	}
	newScene(state, core);
	unregisterProjectResources();
	state.project = project;
	registerProjectResources(state.project);
	recordRecentProject(state.project.getRootDirectory());
	// scripts stay dormant in the editor (edit mode never ticks components;
	// the spawned player runs them), but the script console resolves project
	// scripts the same way the runtimes do
	Orkige::ScriptRuntime::getSingleton().setScriptSearchRoot(
		state.project.getRootDirectory());
	SDL_Log("orkige_editor: project '%s' opened (root '%s', %zu scenes)",
		state.project.getName().c_str(),
		state.project.getRootDirectory().c_str(),
		state.project.listScenes().size());
	const std::string mainScenePath = state.project.getMainScenePath();
	std::error_code ignored;
	if (!mainScenePath.empty() &&
		std::filesystem::exists(mainScenePath, ignored))
	{
		openSceneFromPath(state, core, mainScenePath);
	}
	else
	{
		SDL_Log("orkige_editor: project '%s' has no main scene yet - "
			"starting on an empty scene",
			state.project.getName().c_str());
	}
	return true;
}

// File > Close Project: back to loose-scene mode (empty untitled scene,
// project resources torn down)
void closeProject(EditorState& state, Orkige::EditorCore& core)
{
	if (!state.project.isLoaded())
	{
		return;
	}
	SDL_Log("orkige_editor: project '%s' closed",
		state.project.getName().c_str());
	newScene(state, core);
	unregisterProjectResources();
	state.project.close();
	Orkige::ScriptRuntime::getSingleton().setScriptSearchRoot("");
}

// File > New Project...: create the skeleton (project name = the picked
// folder's name) in the chosen folder, open it, and save the initial empty
// main scene the manifest points at - a fresh project is instantly playable
bool newProjectAtPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& folder)
{
	Orkige::Project created;
	std::string error;
	if (!Orkige::Project::create(folder, "", created, &error))
	{
		SDL_Log("orkige_editor: creating project failed - %s", error.c_str());
		return false;
	}
	if (!openProjectFromPath(state, core, created.getRootDirectory()))
	{
		return false;
	}
	const std::string mainScenePath = state.project.getMainScenePath();
	std::error_code ignored;
	if (!mainScenePath.empty() &&
		!std::filesystem::exists(mainScenePath, ignored))
	{
		saveSceneToPath(state, core, mainScenePath);
	}
	return true;
}

// File > Import Mesh... / SDL_EVENT_DROP_FILE: copy the mesh file into the
// scene's media folder (MeshImport.h documents the destination rule:
// "<sceneDir>/media" for a saved scene, the project import dir -
// samples/scenes/media, ORKIGE_EDITOR_IMPORT_DIR overrides - while unsaved),
// register that folder as an Ogre resource location (once per run) and create
// a GameObject with a ModelComponent at the origin through the undoable
// CreateObjectCommand (undo removes the object; the copied file stays). Any
// failure - unsupported extension, copy error, mesh load error - logs to the
// Console and leaves the scene untouched (CreateObjectCommand tears down a
// half-created object itself).
//! the directory File > Import Mesh copies into: an open project ROOTS the
//! import (everything lands in its assets/), otherwise the historical
//! loose-scene rule applies (<sceneDir>/media for a saved scene; the sample
//! import dir - or the ORKIGE_EDITOR_IMPORT_DIR test override - while
//! unsaved)
std::string meshImportDestination(EditorState const& state)
{
	if (state.project.isLoaded())
	{
		return state.project.getAssetsDirectory();
	}
	const char* importDirOverride = std::getenv("ORKIGE_EDITOR_IMPORT_DIR");
	const std::string projectImportDir = importDirOverride
		? importDirOverride : ORKIGE_EDITOR_SCENE_DIR "/media";
	return Orkige::meshImportDestinationDir(state.currentScenePath,
		projectImportDir);
}

bool importMeshFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& sourcePath)
{
	if (!Orkige::isSupportedMeshFile(sourcePath))
	{
		SDL_Log("orkige_editor: import refused - '%s' is not a supported mesh "
			"file (.glb/.gltf/.obj/.fbx/.dae/.stl/.ply/.3ds/.mesh)",
			sourcePath.c_str());
		return false;
	}
	const std::string destDir = meshImportDestination(state);
	std::string error;
	const std::string destPath =
		Orkige::importMeshFileToDir(sourcePath, destDir, &error);
	if (destPath.empty())
	{
		SDL_Log("orkige_editor: import of '%s' failed - %s",
			sourcePath.c_str(), error.c_str());
		return false;
	}
	if (state.project.isLoaded())
	{
		// the assets/ location index was built when the project opened -
		// re-register it in the project group so the just-copied file is
		// findable by bare filename right away (also covers an assets/ that
		// was missing at open time and got created by this copy);
		// removeResourceLocation is idempotent by facade contract
		Orkige::RenderSystem* render = Orkige::RenderSystem::get();
		render->removeResourceLocation(destDir,
			Orkige::Project::RESOURCE_GROUP_NAME);
		render->addResourceLocation(destDir,
			Orkige::RenderSystem::LT_FILESYSTEM,
			Orkige::Project::RESOURCE_GROUP_NAME);
	}
	else if (state.importResourceDirs.insert(destDir).second)
	{
		// indexes the directory contents immediately - the mesh is loadable
		// by bare filename right after this
		Orkige::RenderSystem::get()->addResourceLocation(destDir);
	}
	const std::string meshName =
		std::filesystem::path(destPath).filename().string();
	std::string objectId = Orkige::meshImportObjectBaseName(destPath);
	if (objectId.empty())
	{
		objectId = "ImportedMesh";
	}
	if (core.getGameObjectManager().objectExists(objectId))
	{
		int suffix = 2;
		std::string candidate;
		do
		{
			candidate = objectId + " " + std::to_string(suffix);
			++suffix;
		} while (core.getGameObjectManager().objectExists(candidate));
		objectId = candidate;
	}
	// undoable create at the origin; execute selects the new object
	if (!core.executeCommand(Orkige::onew(new Orkige::CreateObjectCommand(
		objectId, meshName, Orkige::Vec3::ZERO))))
	{
		SDL_Log("orkige_editor: import of '%s' failed - mesh '%s' did not "
			"load (see the log above)", sourcePath.c_str(), meshName.c_str());
		return false;
	}
	SDL_Log("orkige_editor: imported '%s' -> '%s' as GameObject '%s'",
		sourcePath.c_str(), destPath.c_str(), objectId.c_str());
	return true;
}

// selfcheck helper: compute the viewport-normalized Scene-panel position of
// a GameObject from the RTT camera (facade projectPoint - the old
// worldToViewportNormalized moved behind RenderCamera) and run it through
// pickObjectAtCursor - the same function the Scene panel's mouse path calls
// (the panel image fills the panel content region, so panel-relative and
// viewport-normalized coordinates coincide). Returns false if the object is
// missing or behind the camera.
bool pickGameObjectThroughScenePanel(Orkige::EditorCore& core,
	Orkige::GameObjectManager& gameObjectManager,
	optr<Orkige::RenderCamera> const& camera, std::string const& id)
{
	optr<Orkige::GameObject> gameObject =
		gameObjectManager.getGameObject(id).lock();
	Orkige::Real normalizedX = 0.0f;
	Orkige::Real normalizedY = 0.0f;
	if (!gameObject || !camera->projectPoint(
		gameObject->getComponentPtr<Orkige::TransformComponent>()
			->getPosition(),
		normalizedX, normalizedY))
	{
		return false;
	}
	pickObjectAtCursor(core, camera, normalizedX, normalizedY);
	return true;
}

// run the console buffer through the ScriptRuntime seam, capture returns/
// errors - in a no-scripting build this reports the honest "scripting
// disabled" error instead of not existing
void runLuaConsoleInput(EditorState& state)
{
	std::string code(state.luaInput);
	state.luaHistory.push_back("lua> " + code);
	const Orkige::ScriptRuntime::Result result =
		Orkige::ScriptRuntime::getSingleton().runString(code);
	if (!result.success)
	{
		state.luaHistory.push_back("error: " + result.error);
	}
	else if (result.returnValues.empty())
	{
		state.luaHistory.push_back("ok");
	}
	else
	{
		std::string out;
		for (std::size_t i = 0; i < result.returnValues.size(); ++i)
		{
			if (i > 0)
			{
				out += "\t";
			}
			out += result.returnValues[i];
		}
		state.luaHistory.push_back(out);
	}
	state.luaScrollToBottom = true;
}

// the directory scene dialogs default into: the open project ROOTS them
// (scenes/), loose-scene mode keeps the historical sample scene dir
std::string defaultSceneDirectory(EditorState const& state)
{
	return state.project.isLoaded()
		? state.project.getScenesDirectory()
		: std::string(ORKIGE_EDITOR_SCENE_DIR);
}

// open the "Scene Path" FALLBACK modal preloaded with a sensible path (the
// current scene / a default inside the scene directory, the asset directory
// for a mesh import, or the project root for the project actions) - only
// reached when a native file dialog reported failure (requestFileDialog
// logs that and lands here)
void requestScenePathPopup(EditorState& state,
	Orkige::FileDialogAction action)
{
	state.scenePathAction = action;
	state.openScenePathPopup = true;
	std::string defaultPath;
	if (action == Orkige::FileDialogAction::ImportMesh)
	{
		defaultPath = ORKIGE_EDITOR_ASSET_DIR "/";
	}
	else if (action == Orkige::FileDialogAction::NewProject ||
		action == Orkige::FileDialogAction::OpenProject)
	{
		// a folder path (or a .orkproj for Open) - start where the current
		// project lives
		defaultPath = state.project.isLoaded()
			? std::filesystem::path(state.project.getRootDirectory())
				.parent_path().string() + "/"
			: std::string();
	}
	else
	{
		defaultPath = state.currentScenePath.empty()
			? defaultSceneDirectory(state) + "/scene.oscene"
			: state.currentScenePath;
	}
	SDL_strlcpy(state.scenePathInput, defaultPath.c_str(),
		sizeof(state.scenePathInput));
}

//--- native file dialogs ----------------------------------------------------

// File > Open/Save As/Import Mesh use SDL3's native file dialogs (NSOpenPanel/
// NSSavePanel on macOS - the save panel brings its own "replace existing
// file?" confirmation). The dialogs are ASYNC: SDL_ShowOpenFileDialog/
// SDL_ShowSaveFileDialog return immediately and invoke fileDialogCallback
// later - on macOS on the main thread (panel completion handler inside SDL's
// event pump), on other platforms possibly from a worker thread. The callback
// therefore only deposits the outcome in this mailbox; the main loop consumes
// it once per frame (dispatchFileDialogResults) and runs the scene/Ogre work
// on the main thread. A failed dialog falls back to the "Scene Path" modal
// (see requestScenePathPopup) so the action stays reachable.
Orkige::FileDialogResultQueue gFileDialogResults;

// filter tables passed to SDL - must stay valid until the callback fired
// (SDL requirement), hence static storage
const SDL_DialogFileFilter SCENE_FILE_FILTERS[] = {
	{ "Orkige scene", "oscene" },
};
// keep in sync with Orkige::isSupportedMeshFile (MeshImport.cpp)
const SDL_DialogFileFilter MESH_FILE_FILTERS[] = {
	{ "Mesh files", "glb;gltf;obj;fbx;dae;stl;ply;3ds;mesh" },
	{ "All files", "*" },
};

//! heap-allocated per dialog, owned by (and freed in) the callback
struct FileDialogRequest
{
	Orkige::FileDialogAction action = Orkige::FileDialogAction::None;
};

// the SDL dialog callback: NO UI/scene/ImGui work here (thread rules above) -
// classify the outcome and hand it to the mailbox
void SDLCALL fileDialogCallback(void* userdata, char const* const* filelist,
	int /*filterIndex*/)
{
	FileDialogRequest* request = static_cast<FileDialogRequest*>(userdata);
	Orkige::FileDialogResult result;
	result.action = request->action;
	delete request;
	if (!filelist)
	{
		// dialog error - SDL_GetError is thread-local, so read it HERE on
		// the callback's thread while it still carries the dialog's error
		result.failed = true;
		result.errorMessage = SDL_GetError();
	}
	else if (*filelist)
	{
		// the editor's dialogs are all single-selection - the first entry
		// is the choice
		result.accepted = true;
		result.path = *filelist;
	}
	// neither: the user cancelled - a consumed no-op
	gFileDialogResults.deliver(result);
}

// show the native dialog for one editor action (all entry points funnel
// through here: ImGui menu, native mac menu, Cmd/Ctrl shortcuts)
void requestFileDialog(EditorState& state, SDL_Window* window,
	Orkige::FileDialogAction action)
{
	// default location rule (SDL: trailing '/' = start directory, otherwise
	// directory + pre-filled file name): Save As offers the current scene
	// file, Open starts where the current scene lives, Import Mesh in the
	// sample asset directory - the scene actions fall back to the OPEN
	// PROJECT's scenes/ (or the sample scene dir in loose-scene mode). The
	// project actions use SDL's native FOLDER dialog: New Project picks (or
	// creates, via the panel's New Folder button) the project folder, Open
	// Project picks an existing project folder.
	std::string defaultLocation;
	switch (action)
	{
	case Orkige::FileDialogAction::SaveSceneAs:
		defaultLocation = state.currentScenePath.empty()
			? defaultSceneDirectory(state) + "/scene.oscene"
			: state.currentScenePath;
		break;
	case Orkige::FileDialogAction::OpenScene:
		defaultLocation = state.currentScenePath.empty()
			? defaultSceneDirectory(state) + "/"
			: std::filesystem::path(state.currentScenePath)
				.parent_path().string() + "/";
		break;
	case Orkige::FileDialogAction::ImportMesh:
		defaultLocation = ORKIGE_EDITOR_ASSET_DIR "/";
		break;
	case Orkige::FileDialogAction::NewProject:
	case Orkige::FileDialogAction::OpenProject:
		if (state.project.isLoaded())
		{
			defaultLocation = std::filesystem::path(
				state.project.getRootDirectory())
					.parent_path().string() + "/";
		}
		break;
	case Orkige::FileDialogAction::None:
		return;
	}
	FileDialogRequest* request = new FileDialogRequest{ action };
	if (action == Orkige::FileDialogAction::SaveSceneAs)
	{
		SDL_ShowSaveFileDialog(fileDialogCallback, request, window,
			SCENE_FILE_FILTERS, 1, defaultLocation.c_str());
	}
	else if (action == Orkige::FileDialogAction::OpenScene)
	{
		SDL_ShowOpenFileDialog(fileDialogCallback, request, window,
			SCENE_FILE_FILTERS, 1, defaultLocation.c_str(), false);
	}
	else if (action == Orkige::FileDialogAction::NewProject ||
		action == Orkige::FileDialogAction::OpenProject)
	{
		SDL_ShowOpenFolderDialog(fileDialogCallback, request, window,
			defaultLocation.empty() ? nullptr : defaultLocation.c_str(),
			false);
	}
	else
	{
		SDL_ShowOpenFileDialog(fileDialogCallback, request, window,
			MESH_FILE_FILTERS, 2, defaultLocation.c_str(), false);
	}
}

// once per frame on the main thread: act on whatever the dialog callback
// deposited - the ONLY place dialog outcomes touch the scene/editor state
void dispatchFileDialogResults(EditorState& state, Orkige::EditorCore& core)
{
	Orkige::FileDialogResult result;
	while (gFileDialogResults.consume(result))
	{
		if (result.failed)
		{
			SDL_Log("orkige_editor: native file dialog failed (%s) - "
				"falling back to the path-input modal",
				result.errorMessage.c_str());
			requestScenePathPopup(state, result.action);
			continue;
		}
		if (!result.accepted)
		{
			continue; // cancelled
		}
		switch (result.action)
		{
		case Orkige::FileDialogAction::SaveSceneAs:
			saveSceneToPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::OpenScene:
			openSceneFromPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::ImportMesh:
			importMeshFromPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::NewProject:
			newProjectAtPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::OpenProject:
			openProjectFromPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::None:
			break;
		}
	}
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

// every quit path (File > Quit, ESC with nothing selected, the window close
// button / Cmd+Q via SDL_EVENT_QUIT, the native mac menu) funnels through
// here: unsaved changes raise the "Unsaved Changes" confirm modal instead of
// silently dropping the scene. Frame-capped automation runs bypass this on
// purpose (they stop the loop directly).
void requestQuit(EditorState& state, Orkige::EditorCore& core)
{
	if (core.isSceneDirty())
	{
		state.openQuitConfirmPopup = true;
	}
	else
	{
		state.quitRequested = true;
	}
}

// View > Panels checkables - both menu variants (ImGui bar and the floating
// window opened from the native mac menu) route through here; true when a
// visibility flag flipped (caller persists)
bool drawPanelToggleItems(ViewSettings& viewSettings)
{
	bool changed = false;
	changed |= ImGui::MenuItem("Scene Hierarchy", nullptr,
		&viewSettings.showHierarchyPanel);
	changed |= ImGui::MenuItem("Inspector", nullptr,
		&viewSettings.showInspectorPanel);
	changed |= ImGui::MenuItem("Console", nullptr,
		&viewSettings.showConsolePanel);
	changed |= ImGui::MenuItem("Stats", nullptr,
		&viewSettings.showStatsPanel);
	changed |= ImGui::MenuItem("Scene", nullptr,
		&viewSettings.showScenePanel);
	return changed;
}

// the view-settings widgets (grid/gizmo toggles, camera feel sliders, FOV) -
// hosted by the ImGui View menu on non-mac and by the floating "View
// Settings" window the native menu opens on mac; true when anything changed
// (caller persists)
bool drawViewSettingsWidgets(ViewSettings& viewSettings,
	optr<Orkige::RenderCamera> const& sceneCamera)
{
	bool settingsChanged = false;
	settingsChanged |= ImGui::Checkbox("Show Grid", &viewSettings.showGrid);
	settingsChanged |= ImGui::Checkbox("Orientation Gizmo",
		&viewSettings.showViewGizmo);
	settingsChanged |= ImGui::Checkbox("Reopen Last Project on Launch",
		&viewSettings.reopenLastProject);
	ImGui::SetItemTooltip(
		"start the editor in the most recent project (Unity behavior)");
	ImGui::Separator();
	ImGui::TextDisabled("Camera");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Orbit Speed",
		&viewSettings.orbitSpeed, 0.05f, 2.0f, "%.2f");
	ImGui::SetItemTooltip("Alt+left orbit drag (degrees per point)");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Look Speed",
		&viewSettings.lookSpeed, 0.02f, 1.0f, "%.2f");
	ImGui::SetItemTooltip(
		"fly-mode mouselook (degrees per count of mouse travel)");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Zoom Speed",
		&viewSettings.zoomSpeed, 0.1f, 3.0f, "%.2f");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Fly Speed",
		&viewSettings.flySpeed, Orkige::FLY_SPEED_MIN,
		Orkige::FLY_SPEED_MAX, "%.1f");
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::SliderFloat("FOV", &viewSettings.fovDeg, 20.0f, 120.0f,
		"%.0f\xC2\xB0"))
	{
		sceneCamera->setFOVy(Orkige::Degree(viewSettings.fovDeg));
		settingsChanged = true;
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Reset View Settings"))
	{
		viewSettings.resetCameraAndDisplayDefaults();
		sceneCamera->setFOVy(Orkige::Degree(viewSettings.fovDeg));
		settingsChanged = true;
	}
	return settingsChanged;
}

// the floating View Settings window: on mac the native menu bar replaces the
// ImGui menu bar that used to host these widgets, so View > View Settings...
// opens them here instead (available on every platform)
void drawViewSettingsWindow(EditorState& state, ViewSettings& viewSettings,
	optr<Orkige::RenderCamera> const& sceneCamera)
{
	if (!state.showViewSettingsWindow)
	{
		return;
	}
	if (ImGui::Begin("View Settings", &state.showViewSettingsWindow,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (drawViewSettingsWidgets(viewSettings, sceneCamera))
		{
			viewSettings.save();
		}
	}
	ImGui::End();
}

// the in-window ImGui menu bar. NOT drawn on macOS - the native NSMenu bar
// (MacMenu.mm) mirrors this structure there and routes into the exact same
// functions; keeping both would duplicate every menu.
void drawMainMenuBar(EditorState& state, Orkige::EditorCore& core,
	ViewSettings& viewSettings, optr<Orkige::RenderCamera> const& sceneCamera,
	SDL_Window* window)
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Project..."))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::NewProject);
			}
			if (ImGui::MenuItem("Open Project..."))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::OpenProject);
			}
			if (ImGui::BeginMenu("Open Recent Project",
				!viewSettings.recentProjects.empty()))
			{
				// iterate a copy: a click reorders the list mid-iteration
				const std::vector<std::string> recents =
					viewSettings.recentProjects;
				for (std::string const& recentRoot : recents)
				{
					if (ImGui::MenuItem(recentRoot.c_str()))
					{
						openProjectFromPath(state, core, recentRoot);
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Close Project", nullptr, false,
				state.project.isLoaded()))
			{
				closeProject(state, core);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("New Scene", ORKIGE_EDITOR_MOD_LABEL "+N"))
			{
				newScene(state, core);
			}
			if (ImGui::MenuItem("Open Scene...",
				ORKIGE_EDITOR_MOD_LABEL "+O"))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::OpenScene);
			}
			if (ImGui::BeginMenu("Open Recent",
				!viewSettings.recentScenes.empty()))
			{
				// iterate a copy: a click reorders the list mid-iteration
				const std::vector<std::string> recents =
					viewSettings.recentScenes;
				for (std::string const& recentPath : recents)
				{
					if (ImGui::MenuItem(recentPath.c_str()))
					{
						openSceneFromPath(state, core, recentPath);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Save Scene", ORKIGE_EDITOR_MOD_LABEL "+S"))
			{
				if (state.currentScenePath.empty())
				{
					requestFileDialog(state, window,
						Orkige::FileDialogAction::SaveSceneAs);
				}
				else
				{
					saveSceneToPath(state, core, state.currentScenePath);
				}
			}
			if (ImGui::MenuItem("Save Scene As...",
				"Shift+" ORKIGE_EDITOR_MOD_LABEL "+S"))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::SaveSceneAs);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Import Mesh..."))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::ImportMesh);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Quit", "Esc"))
			{
				requestQuit(state, core);
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
		// project export (async - Util/orkige_export.py; the output lands in
		// the Console as [export] lines); enabled only with a project open
		if (ImGui::BeginMenu("Build"))
		{
			const bool canExport = state.project.isLoaded();
			if (ImGui::MenuItem("Build for macOS", nullptr, false, canExport))
			{
				state.requestedExport = "macos";
			}
			if (ImGui::MenuItem("Build for iOS Simulator", nullptr, false,
				canExport))
			{
				state.requestedExport = "ios-simulator";
			}
			if (ImGui::MenuItem("Build for Android APK", nullptr, false,
				canExport))
			{
				state.requestedExport = "android";
			}
			ImGui::EndMenu();
		}
		// panel visibility, layout reset and the viewport settings (grid,
		// orientation gizmo, camera feel) - persisted to the key=value file
		// next to the imgui ini on change
		if (ImGui::BeginMenu("View"))
		{
			bool settingsChanged = false;
			ImGui::TextDisabled("Panels");
			settingsChanged |= drawPanelToggleItems(viewSettings);
			if (ImGui::MenuItem("Reset Layout"))
			{
				state.resetDockLayout = true;
			}
			ImGui::Separator();
			settingsChanged |=
				drawViewSettingsWidgets(viewSettings, sceneCamera);
			if (settingsChanged)
			{
				viewSettings.save();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About"))
			{
				state.openAboutPopup = true;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

// The editor's modal popups (About, Scene Path, Unsaved Changes) - drawn
// every frame INDEPENDENTLY of the menu bar: on mac the native menu opens
// them via the EditorState flags while the ImGui menu bar is not drawn.
void drawEditorModals(EditorState& state, Orkige::EditorCore& core)
{
	if (state.openAboutPopup)
	{
		ImGui::OpenPopup("About Orkige Editor");
		state.openAboutPopup = false;
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
	// "Scene Path" modal: the plain path-input FALLBACK for when a native
	// file dialog failed (see requestFileDialog); confirms the pending
	// SaveSceneAs/OpenScene/ImportMesh action
	if (state.openScenePathPopup)
	{
		ImGui::OpenPopup("Scene Path");
		state.openScenePathPopup = false;
	}
	if (ImGui::BeginPopupModal("Scene Path", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		const char* prompt = "Open scene (.oscene):";
		const char* confirmLabel = "Open";
		if (state.scenePathAction == Orkige::FileDialogAction::SaveSceneAs)
		{
			prompt = "Save scene as (.oscene):";
			confirmLabel = "Save";
		}
		else if (state.scenePathAction ==
			Orkige::FileDialogAction::ImportMesh)
		{
			prompt = "Import mesh (.glb/.gltf/.obj/.fbx/...):";
			confirmLabel = "Import";
		}
		else if (state.scenePathAction ==
			Orkige::FileDialogAction::NewProject)
		{
			prompt = "New project folder:";
			confirmLabel = "Create";
		}
		else if (state.scenePathAction ==
			Orkige::FileDialogAction::OpenProject)
		{
			prompt = "Open project (folder or .orkproj):";
			confirmLabel = "Open";
		}
		ImGui::TextUnformatted(prompt);
		ImGui::SetNextItemWidth(620.0f);
		ImGui::InputText("##scenepath", state.scenePathInput,
			sizeof(state.scenePathInput));
		if (ImGui::Button(confirmLabel))
		{
			const std::string path(state.scenePathInput);
			if (!path.empty())
			{
				switch (state.scenePathAction)
				{
				case Orkige::FileDialogAction::SaveSceneAs:
					saveSceneToPath(state, core, path);
					break;
				case Orkige::FileDialogAction::OpenScene:
					openSceneFromPath(state, core, path);
					break;
				case Orkige::FileDialogAction::ImportMesh:
					importMeshFromPath(state, core, path);
					break;
				case Orkige::FileDialogAction::NewProject:
					newProjectAtPath(state, core, path);
					break;
				case Orkige::FileDialogAction::OpenProject:
					openProjectFromPath(state, core, path);
					break;
				case Orkige::FileDialogAction::None:
					break;
				}
			}
			state.scenePathAction = Orkige::FileDialogAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			state.scenePathAction = Orkige::FileDialogAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	// "Unsaved Changes" confirm: raised by requestQuit when the scene is
	// dirty - the ONLY way a quit proceeds past a dirty scene
	if (state.openQuitConfirmPopup)
	{
		ImGui::OpenPopup("Unsaved Changes");
		state.openQuitConfirmPopup = false;
	}
	if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("The scene has unsaved changes.");
		ImGui::Spacing();
		if (!state.currentScenePath.empty())
		{
			if (ImGui::Button("Save and Quit"))
			{
				if (saveSceneToPath(state, core, state.currentScenePath))
				{
					state.quitRequested = true;
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("Quit Without Saving"))
		{
			state.quitRequested = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
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
		// play target picker: Desktop, a booted iOS simulator or a connected
		// Android device/emulator (plus enumerated-but-gated iOS hardware).
		// The device lists are scanned when the popup opens (short
		// synchronous simctl/adb calls - acceptable for an explicit user
		// action).
#ifdef __APPLE__
		static std::vector<SimulatorDevice> availableSimulators;
		static std::vector<IosHardwareDevice> iosHardware;
		static bool codesignIdentityPresent = false;
#endif
		static std::vector<AndroidDevice> androidDevices;
		static bool otherFlavorPlayerPresent = false;
		ImGui::BeginDisabled(mode != PlaySession::Mode::Edit);
		ImGui::SetNextItemWidth(150.0f);
		const char* targetPreview = "Desktop";
		if (!session.desktopLabel.empty())
		{
			targetPreview = session.desktopLabel.c_str();
		}
		else if (!session.simulatorUdid.empty())
		{
			targetPreview = session.simulatorLabel.c_str();
		}
		else if (!session.androidSerial.empty())
		{
			targetPreview = session.androidLabel.c_str();
		}
		// the two desktop flavors: this build's own player and the OTHER
		// render flavor's (conventional preset build tree - baked in by
		// CMake). The debug protocol is flavor-agnostic; the visual result
		// must be identical (the WYSIWYG backend-parity rule).
#ifdef ORKIGE_RENDER_NEXT
		const char* const ownFlavorLabel = "Desktop (Ogre-Next)";
		const char* const otherFlavorLabel = "Desktop (classic OGRE)";
		const char* const otherFlavorPlayerPath =
			ORKIGE_EDITOR_PLAYER_PATH_CLASSIC;
#else
		const char* const ownFlavorLabel = "Desktop (classic OGRE)";
		const char* const otherFlavorLabel = "Desktop (Ogre-Next)";
		const char* const otherFlavorPlayerPath =
			ORKIGE_EDITOR_PLAYER_PATH_NEXT;
#endif
		if (ImGui::BeginCombo("##PlayTarget", targetPreview))
		{
			if (ImGui::IsWindowAppearing())
			{
#ifdef __APPLE__
				availableSimulators = listSimulators();
				// iOS hardware needs signed builds: only enumerate once a
				// codesigning identity exists (the devicectl call is the
				// slower one, so gate it too)
				codesignIdentityPresent = hasCodesignIdentity();
				iosHardware = codesignIdentityPresent
					? listIosHardwareDevices()
					: std::vector<IosHardwareDevice>();
#endif
				androidDevices = listAdbDevices();
				std::error_code ignored;
				otherFlavorPlayerPresent = std::filesystem::exists(
					otherFlavorPlayerPath, ignored);
			}
			const bool desktopSelected = session.simulatorUdid.empty() &&
				session.androidSerial.empty();
			if (ImGui::Selectable(ownFlavorLabel,
				desktopSelected && session.desktopPlayerPath.empty()))
			{
				session.desktopPlayerPath.clear();
				session.desktopLabel.clear();
				session.simulatorUdid.clear();
				session.simulatorLabel.clear();
				session.androidSerial.clear();
				session.androidLabel.clear();
			}
			// the other flavor's player: selectable only when its build
			// tree carries the binary (grey + tooltip otherwise - honest
			// gating over a Play that cannot work)
			ImGui::BeginDisabled(!otherFlavorPlayerPresent);
			if (ImGui::Selectable(otherFlavorLabel,
				desktopSelected && !session.desktopPlayerPath.empty()))
			{
				session.desktopPlayerPath = otherFlavorPlayerPath;
				session.desktopLabel = otherFlavorLabel;
				session.simulatorUdid.clear();
				session.simulatorLabel.clear();
				session.androidSerial.clear();
				session.androidLabel.clear();
			}
			ImGui::EndDisabled();
			if (!otherFlavorPlayerPresent &&
				ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("that flavor's player is not built - "
					"configure + build its preset first (%s)",
					otherFlavorPlayerPath);
			}
#ifdef __APPLE__
			// every AVAILABLE simulator is a valid target: Play boots a
			// shutdown one automatically (state marked in the entry)
			for (SimulatorDevice const& device : availableSimulators)
			{
				const std::string entryLabel = device.booted
					? device.name : device.name + "  (shutdown)";
				if (ImGui::Selectable(
					(entryLabel + "##" + device.udid).c_str(),
					session.simulatorUdid == device.udid))
				{
					session.simulatorUdid = device.udid;
					session.simulatorLabel = device.name;
					session.androidSerial.clear();
					session.androidLabel.clear();
					session.desktopPlayerPath.clear();
					session.desktopLabel.clear();
				}
				if (!device.booted && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip(
						"shut down - Play boots it (takes a moment)");
				}
			}
#endif
			for (AndroidDevice const& device : androidDevices)
			{
				if (ImGui::Selectable(
					(device.label + "##" + device.serial).c_str(),
					session.androidSerial == device.serial))
				{
					session.androidSerial = device.serial;
					session.androidLabel = device.label;
					session.simulatorUdid.clear();
					session.simulatorLabel.clear();
					session.desktopPlayerPath.clear();
					session.desktopLabel.clear();
				}
			}
#ifdef __APPLE__
			// iOS hardware (task: physical devices): enumerated but not
			// deployable yet - unlike the simulator, hardware shares neither
			// filesystem nor loopback and requires signed app installs, so
			// the entries stay disabled until that path lands (tested with a
			// real device). Honest gating over a Play that cannot work.
			if (!codesignIdentityPresent)
			{
				ImGui::BeginDisabled(true);
				ImGui::Selectable("iPhone/iPad (USB)");
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip(
						"requires an Apple Developer signing identity");
				}
			}
			for (IosHardwareDevice const& device : iosHardware)
			{
				ImGui::BeginDisabled(true);
				ImGui::Selectable(
					(device.name + "##" + device.udid).c_str());
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Play on iOS hardware is not wired up "
						"yet (needs signed installs, scene transfer and a "
						"debug-port tunnel) - use a booted simulator");
				}
			}
#endif
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(mode != PlaySession::Mode::Edit);
		if (ImGui::Button("Play"))
		{
			startPlay(session, gameObjectManager, state.project);
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
			if (gViewSettings)
			{
				gViewSettings->snapEnabled = snapEnabled;
				gViewSettings->save();
			}
		}
		ImGui::SameLine();
		// the current steps double as the button into the snap-settings
		// popover (editable values, Unity's snap settings)
		char snapLabel[64];
		SDL_snprintf(snapLabel, sizeof(snapLabel),
			"%.2g / %.2g\xC2\xB0 / %.2g###SnapSettings",
			core.getSnapTranslate(), core.getSnapRotateDegrees(),
			core.getSnapScale());
		if (ImGui::SmallButton(snapLabel))
		{
			ImGui::OpenPopup("##SnapSettingsPopover");
		}
		ImGui::SetItemTooltip("snap step settings (move / rotate / scale)");
		if (ImGui::BeginPopup("##SnapSettingsPopover"))
		{
			float snapTranslate = core.getSnapTranslate();
			float snapRotate = core.getSnapRotateDegrees();
			float snapScale = core.getSnapScale();
			bool snapEdited = false;
			ImGui::TextDisabled("Snap Steps");
			ImGui::SetNextItemWidth(120.0f);
			snapEdited |= ImGui::DragFloat("Move", &snapTranslate,
				0.05f, 0.001f, 100.0f, "%.3f");
			ImGui::SetNextItemWidth(120.0f);
			snapEdited |= ImGui::DragFloat("Rotate", &snapRotate,
				0.5f, 0.1f, 180.0f, "%.1f\xC2\xB0");
			ImGui::SetNextItemWidth(120.0f);
			snapEdited |= ImGui::DragFloat("Scale", &snapScale,
				0.01f, 0.001f, 10.0f, "%.3f");
			if (ImGui::MenuItem("Reset to Defaults"))
			{
				snapTranslate = Orkige::EditorCore::SNAP_TRANSLATE;
				snapRotate = Orkige::EditorCore::SNAP_ROTATE_DEGREES;
				snapScale = Orkige::EditorCore::SNAP_SCALE;
				snapEdited = true;
			}
			if (snapEdited)
			{
				core.setSnapValues(snapTranslate, snapRotate, snapScale);
				if (gViewSettings)
				{
					// persist the CLAMPED values EditorCore actually uses
					gViewSettings->snapTranslate = core.getSnapTranslate();
					gViewSettings->snapRotateDegrees =
						core.getSnapRotateDegrees();
					gViewSettings->snapScale = core.getSnapScale();
					gViewSettings->save();
				}
			}
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();
		switch (mode)
		{
		case PlaySession::Mode::Edit:
			ImGui::TextDisabled("editing");
			break;
		case PlaySession::Mode::Building:
			// compile-on-Play: the Console carries the [build] output
			ImGui::TextUnformatted(session.launchStatus.empty()
				? "building..." : session.launchStatus.c_str());
			break;
		case PlaySession::Mode::Launching:
			// the simulator prep pipeline reports its phase here
			// (booting / installing / launching)
			ImGui::TextUnformatted(session.launchStatus.empty()
				? "launching player..." : session.launchStatus.c_str());
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
		// script failures must be loud: a red marker next to the play status
		// while any script error is known in the current session (fed by the
		// player's script_error messages; cleared on Stop / a new session)
		if (session.isActive() && !session.scriptErrorIds.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.94f, 0.35f, 0.35f, 1.0f),
				"%zu script error%s - see Console",
				session.scriptErrorIds.size(),
				session.scriptErrorIds.size() == 1 ? "" : "s");
		}
	}
	ImGui::End();
	(void)state;
	return toolbarHeight;
}

// Dockspace filling the work area below the toolbar strip. The first run
// builds the default layout programmatically with the DockBuilder:
// Hierarchy left, Inspector right, Console + Stats tabbed at the
// bottom, Scene panel filling the centre. Afterwards the layout persists
// through imgui.ini (stored next to the executable, see main()) and the
// builder stays out of the way - until View > Reset Layout sets
// state.resetDockLayout, which reruns the builder from scratch (and re-opens
// every panel).
void drawDockspace(EditorState& state, float toolbarHeight,
	ViewSettings& viewSettings)
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
	const bool resetRequested = state.resetDockLayout;
	if ((state.dockLayoutChecked && !resetRequested) ||
		hostSize.x <= 0.0f || hostSize.y <= 0.0f)
	{
		// the very first frame may have no display size yet - try again
		// next frame, DockBuilderSetNodeSize needs a real size (a pending
		// reset request also survives until a frame with a real size)
		return;
	}
	state.dockLayoutChecked = true;
	state.resetDockLayout = false;
	if (resetRequested)
	{
		// View > Reset Layout: rebuild from scratch and re-open every panel
		// (a hidden panel would otherwise stay lost in the fresh layout)
		viewSettings.showAllPanels();
		viewSettings.save();
	}
	else
	{
		ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId);
		if (rootNode && rootNode->IsSplitNode())
		{
			return; // imgui.ini restored a layout - keep it
		}
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
	ImGui::DockBuilderDockWindow("Console", bottomId);
	ImGui::DockBuilderDockWindow("Stats", bottomId);
	ImGui::DockBuilderDockWindow("Scene", centerId);
	ImGui::DockBuilderFinish(dockspaceId);
}

// The engine Mat4 (Ogre-layout math per RenderMath.h) stores row-major
// (m[row][col]); ImGuizmo expects the usual OpenGL-style column-major
// float16 - copying transposed converts between the two (both directions).
// The facade camera matrices (RenderCamera::getViewMatrix/
// getProjectionMatrix) return the same row-major Mat4 the raw camera did,
// so the transpose convention is unchanged - the gizmo/picking selfchecks
// cover it.
void matrixToImGuizmo(Orkige::Mat4 const& matrix, float* out16)
{
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			out16[col * 4 + row] = matrix[row][col];
		}
	}
}

Orkige::Mat4 imGuizmoToMatrix(const float* in16)
{
	Orkige::Mat4 matrix;
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
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize)
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
	matrixToImGuizmo(camera->getViewMatrix(), view);
	matrixToImGuizmo(camera->getProjectionMatrix(), projection);
	Orkige::Mat4 modelMatrix;
	modelMatrix.makeTransform(current.position, current.scale,
		current.orientation);
	matrixToImGuizmo(modelMatrix, model);

	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	// the editable snap steps from the toolbar popover (default to the
	// SNAP_* constants)
	float snapValues[3] = { core.getSnapTranslate(),
		core.getSnapTranslate(),
		core.getSnapTranslate() };
	if (tool == Orkige::EditorTool::Rotate)
	{
		operation = ImGuizmo::ROTATE;
		snapValues[0] = snapValues[1] = snapValues[2] =
			core.getSnapRotateDegrees();
	}
	else if (tool == Orkige::EditorTool::Scale)
	{
		operation = ImGuizmo::SCALE;
		snapValues[0] = snapValues[1] = snapValues[2] =
			core.getSnapScale();
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
			Orkige::Affine3(imGuizmoToMatrix(model)).decomposition(
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
// coords because the image always fills the content region), and the camera:
// right-HOLD = fly mode (true relative-mouse mouselook + WASD move, Q/E
// down/up, Shift = boost, scroll tunes the fly speed), Alt+left drag =
// classic orbit, middle-drag pans, scroll zooms. Fly mode captures the mouse
// via imguiInput.setRelativeMode (cursor hidden, raw relative counts drive
// the look, cursor restored on release).
void drawScenePanel(EditorState& state, Orkige::EditorCore& core,
	bool editMode, SceneRenderTarget& sceneTarget,
	optr<Orkige::RenderNode> const& cameraNode,
	ViewSettings& viewSettings, float contentScale,
	Orkige::ImGuiSDL3Input& imguiInput)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin("Scene", &viewSettings.showScenePanel);
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
			// the RTT binds by facade HANDLE (ImGuiFacadeRenderer registry;
			// DrawLayer2D re-resolves the current backend texture per draw,
			// so the id is stable across resizes on every render flavor)
			ImGui::Image(gImGuiRenderer->textureIdFor(sceneTarget.texture),
				avail);
			const ImVec2 rectMin = ImGui::GetItemRectMin();
			state.scenePanelHovered = ImGui::IsItemHovered();
			// gizmo first: while it is hovered/dragged the click-pick and
			// the camera drags stand down (input priority). Editing the
			// local scene is pointless while the panels show the remote one.
			const bool gizmoOwnsMouse = editMode &&
				drawSceneGizmo(state, core, sceneTarget.camera, rectMin, avail);
			// axis orientation gizmo (top-right corner): displays the camera
			// orientation and drives the orbit - ImGuizmo manipulates the
			// view matrix around a pivot orbitDistance away (the orbit
			// target), so the new camera pose decomposes straight back into
			// the orbit yaw/pitch. While it is hovered/dragged, picking and
			// the camera drags stand down like for the transform gizmo.
			// mutual exclusion: while a fly/orbit/pan drag is running, the
			// corner gizmo must NOT also write camera state (both paths
			// mutate the same yaw/pitch - running them simultaneously made
			// the view fight itself and "rotate weirdly")
			bool viewGizmoOwnsMouse = false;
			if (viewSettings.showViewGizmo && !state.flyActive &&
				!state.orbitActive && !state.panActive)
			{
				const float viewGizmoSize = 96.0f;
				if (avail.x > viewGizmoSize * 1.5f &&
					avail.y > viewGizmoSize * 1.5f)
				{
					ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
					float view[16];
					matrixToImGuizmo(sceneTarget.camera->getViewMatrix(),
						view);
					float viewBefore[16];
					std::memcpy(viewBefore, view, sizeof(view));
					ImGuizmo::ViewManipulate(view, state.camera.distance,
						ImVec2(rectMin.x + avail.x - viewGizmoSize - 8.0f,
							rectMin.y + 8.0f),
						ImVec2(viewGizmoSize, viewGizmoSize), 0x00000000);
					viewGizmoOwnsMouse = ImGuizmo::IsUsingViewManipulate() ||
						ImGuizmo::IsViewManipulateHovered();
					if (std::memcmp(viewBefore, view, sizeof(view)) != 0)
					{
						// decompose the manipulated view back into the orbit
						// spherical coordinates (distance stays fixed)
						const Orkige::Mat4 inverseView =
							imGuizmoToMatrix(view).inverse();
						const Orkige::Vec3 cameraPos(inverseView[0][3],
							inverseView[1][3], inverseView[2][3]);
						const Orkige::Vec3 offset =
							cameraPos - state.camera.target;
						const float distance = offset.length();
						if (distance > 1e-3f)
						{
							state.camera.pitchDeg = std::clamp(
								Orkige::Radian(std::asin(
									offset.y / distance)).valueDegrees(),
								-85.0f, 85.0f);
							state.camera.yawDeg = Orkige::Radian(std::atan2(
								offset.x, offset.z)).valueDegrees();
						}
					}
				}
			}
			ImGuiIO& io = ImGui::GetIO();
			if (state.scenePanelHovered && !gizmoOwnsMouse &&
				!viewGizmoOwnsMouse)
			{
				// Alt+left starts an orbit drag, a plain left click picks
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
				{
					// Cmd/Ctrl+click toggles selection-set membership
					pickObjectAtCursor(core, sceneTarget.camera,
						(io.MousePos.x - rectMin.x) / avail.x,
						(io.MousePos.y - rectMin.y) / avail.y,
						io.KeySuper || io.KeyCtrl);
					// Unity-style double-click: the pick above selected the
					// hit - frame it too (a double-click on empty space just
					// cleared the selection; frameSelectedObject no-ops then)
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						frameSelectedObject(state, core, sceneTarget.camera);
					}
				}
				if (io.MouseWheel != 0.0f && !state.flyActive)
				{
					// scroll up zooms in (while flying the wheel tunes the
					// fly speed instead, below)
					state.camera.distance = std::clamp(state.camera.distance *
						std::pow(0.9f, io.MouseWheel * viewSettings.zoomSpeed),
						2.0f, 200.0f);
				}
				// the camera modes are mutually exclusive - a second button
				// pressed mid-drag must not start a competing mode that
				// would double-apply deltas onto the same yaw/pitch
				if (ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
					!state.orbitActive && !state.panActive &&
					!state.flyActive)
				{
					// fly begins: capture the mouse (relative mode - cursor
					// hidden, look input arrives as raw xrel/yrel counts)
					state.flyActive = true;
					imguiInput.setRelativeMode(true);
				}
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyAlt &&
					!state.flyActive && !state.panActive)
				{
					state.orbitActive = true;
				}
				if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
					!state.flyActive && !state.orbitActive)
				{
					state.panActive = true;
				}
			}
			// fly/orbit/pan keep going while their button is held, even when
			// the cursor leaves the panel mid-drag.
			// Mouse deltas come in TWO units here: fly mode reads the raw
			// relative-mode counts accumulated by ImGuiSDL3Input (1:1 with
			// physical mouse travel, NO retina/content scale applies), while
			// orbit/pan still use io.MouseDelta - ImGui's coordinate space =
			// render-target PIXELS (window points x backing-store factor),
			// so those divide by the content scale to get back to the
			// per-point sensitivities.
			if (state.flyActive)
			{
				// drain the relative motion every fly frame (even gated ones
				// below - a stale first-frame delta must not leak into the
				// second frame)
				float flyLookDeltaX = 0.0f;
				float flyLookDeltaY = 0.0f;
				imguiInput.consumeRelativeDelta(flyLookDeltaX, flyLookDeltaY);
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					// releasing the right button ends fly mode; the orbit
					// target is already "distance units ahead" (flyCameraStep
					// keeps it there), so orbit behavior stays sane - release
					// the mouse capture (restores the pre-fly cursor
					// position) and persist a scroll-tuned fly speed now
					state.flyActive = false;
					state.flyLookGate.update(false);
					imguiInput.setRelativeMode(false);
					viewSettings.save();
				}
				else
				{
					Orkige::FlyInput fly;
					// the hold's first frame may still carry a bogus delta
					// (absolute-motion backlog from before the capture, or a
					// platform-synthesized jump on entering relative mode) -
					// the gate swallows it (WASD movement is unaffected)
					if (state.flyLookGate.update(true))
					{
						fly.lookDeltaX = flyLookDeltaX;
						fly.lookDeltaY = flyLookDeltaY;
					}
					fly.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
					fly.moveBack = ImGui::IsKeyDown(ImGuiKey_S);
					fly.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
					fly.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
					fly.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
					fly.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
					fly.boost = io.KeyShift;
					fly.speedScroll = io.MouseWheel;
					Orkige::flyCameraStep(state.camera, fly, io.DeltaTime,
						viewSettings.lookSpeed, viewSettings.flySpeed);
				}
			}
			if (state.orbitActive)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					state.orbitActive = false;
					state.orbitDragGate.update(false);
				}
				else if (state.orbitDragGate.update(true))
				{
					state.camera.yawDeg -= io.MouseDelta.x / contentScale *
						viewSettings.orbitSpeed;
					state.camera.pitchDeg = std::clamp(state.camera.pitchDeg +
						io.MouseDelta.y / contentScale *
							viewSettings.orbitSpeed,
						-85.0f, 85.0f);
				}
			}
			if (state.panActive)
			{
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				{
					state.panActive = false;
					state.panDragGate.update(false);
				}
				else if (state.panDragGate.update(true))
				{
					// slide the orbit target along the camera plane; the
					// factor scales with distance so a point of mouse travel
					// moves the scene about the same visual amount
					const float panScale = state.camera.distance * 0.003f;
					state.camera.target += cameraNode->getOrientation() *
						Orkige::Vec3(
							-io.MouseDelta.x / contentScale * panScale,
							io.MouseDelta.y / contentScale * panScale, 0.0f);
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
// Edit mode extras: double-click selects AND frames the object in the Scene
// viewport (Unity behavior; inline rename is F2 or the context menu),
// right-click opens Duplicate/Rename/Delete (per object) or Create Cube/
// Create Test Mesh (empty space), up/down arrows move the selection.
void drawHierarchyPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, optr<Orkige::RenderCamera> const& sceneCamera,
	bool* visible)
{
	const bool remote = session.isActive();
	const bool open =
		ImGui::Begin(remote ? HIERARCHY_WINDOW_REMOTE : HIERARCHY_WINDOW_EDIT,
			visible);
	state.hierarchyFocused = open && !remote && ImGui::IsWindowFocused();
	if (open)
	{
		// search/filter box (Unity's Hierarchy search): shared between edit
		// and remote mode, ImGuiTextFilter semantics ("a,b" = a or b,
		// "-a" = exclude a); an active filter never hides the row that is
		// being renamed (the edit field must stay reachable)
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##hierarchyFilter", "Filter",
			state.hierarchyFilter.InputBuf,
			IM_ARRAYSIZE(state.hierarchyFilter.InputBuf)))
		{
			state.hierarchyFilter.Build();
		}
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
					if (!state.hierarchyFilter.PassFilter(id.c_str()))
					{
						continue;
					}
					// objects with a reported script error show in red - the
					// cheap always-visible cue (details are in the Console)
					const bool scriptError =
						session.scriptErrorIds.count(id) != 0;
					if (scriptError)
					{
						ImGui::PushStyleColor(ImGuiCol_Text,
							ImVec4(0.94f, 0.35f, 0.35f, 1.0f));
					}
					const bool selected = (session.remoteSelectedId == id);
					if (ImGui::Selectable(id.c_str(), selected) && !selected)
					{
						selectRemoteObject(session, id);
					}
					if (scriptError)
					{
						ImGui::PopStyleColor();
						ImGui::SetItemTooltip("script error - see Console");
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
				if (!state.hierarchyFilter.PassFilter(id.c_str()) &&
					state.renamingObjectId != id)
				{
					continue;
				}
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
					// Cmd/Ctrl+click toggles selection-set membership,
					// a plain click replaces the selection
					const bool additive = ImGui::GetIO().KeySuper ||
						ImGui::GetIO().KeyCtrl;
					if (additive)
					{
						core.toggleSelection(id);
					}
					else
					{
						core.selectObject(id);
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							// Unity-style: double-click = select + frame
							// (rename moved to F2 / the context menu)
							focusObjectFromDoubleClick(state, core,
								sceneCamera, id);
						}
					}
				}
				// right-click selects (a right-click on a member of a
				// multi-selection keeps the set - Duplicate/Delete then
				// operate on ALL selected), then offers the operations
				if (ImGui::BeginPopupContextItem("##objectmenu"))
				{
					if (!core.isSelected(id))
					{
						core.selectObject(id);
					}
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
			// keyboard: up/down moves the selection through the (sorted,
			// filtered) list; F2/Delete/Cmd+D live in the central shortcut
			// map (handleEditorShortcuts), which covers the focused
			// Hierarchy too
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
		after.position = Orkige::Vec3(position[0], position[1], position[2]);
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
		Orkige::Mat3 rotation;
		rotation.FromEulerAnglesYXZ(
			Orkige::Degree(yawPitchRoll[0]),
			Orkige::Degree(yawPitchRoll[1]),
			Orkige::Degree(yawPitchRoll[2]));
		after.orientation = Orkige::Quat(rotation);
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
		after.scale = Orkige::Vec3(scale[0], scale[1], scale[2]);
		edited = true;
	}

	if (edited)
	{
		core.applyTransformChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

// ModelComponent editor: the mesh name is an editable text field; Enter
// applies through EditorCore::changeObjectMesh (undoable, reloads the
// entity). A failed load logs to the Console and keeps the old mesh.
void drawModelComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId, Orkige::ModelComponent* model)
{
	const std::string currentMesh = model->getCurrentModelFileName();
	// rebuild the edit buffer when the selection or the mesh changed behind
	// the field (undo/redo, another panel)
	if (state.meshEditObjectId != objectId ||
		state.meshEditCurrentMesh != currentMesh)
	{
		state.meshEditObjectId = objectId;
		state.meshEditCurrentMesh = currentMesh;
		SDL_strlcpy(state.meshEditBuffer, currentMesh.c_str(),
			sizeof(state.meshEditBuffer));
	}
	if (ImGui::InputText("Mesh", state.meshEditBuffer,
		sizeof(state.meshEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (!core.changeObjectMesh(objectId, state.meshEditBuffer))
		{
			SDL_Log("orkige_editor: mesh change to '%s' refused/failed",
				state.meshEditBuffer);
			// snap the field back to reality
			SDL_strlcpy(state.meshEditBuffer, currentMesh.c_str(),
				sizeof(state.meshEditBuffer));
		}
		// on success the next frame re-syncs via meshEditCurrentMesh
	}
	ImGui::SetItemTooltip("mesh resource name (Enter reloads the entity)");
}

// ScriptComponent editor: project-relative script path (Enter applies) +
// enabled checkbox - both undoable through ONE ChangeScriptCommand - plus a
// "(script error)" indicator fed from the component. In the editor scripts
// never run (edit mode does not tick components), so errors show up here
// only for state carried in from elsewhere; the live play-mode error state
// arrives through the remote Inspector (ScriptComponent.error).
void drawScriptComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId, Orkige::ScriptComponent* script)
{
	const std::string currentScript = script->getScriptFile();
	// rebuild the edit buffer when the selection or the path changed behind
	// the field (undo/redo, another panel)
	if (state.scriptEditObjectId != objectId ||
		state.scriptEditCurrentScript != currentScript)
	{
		state.scriptEditObjectId = objectId;
		state.scriptEditCurrentScript = currentScript;
		SDL_strlcpy(state.scriptEditBuffer, currentScript.c_str(),
			sizeof(state.scriptEditBuffer));
	}
	if (ImGui::InputText("Script", state.scriptEditBuffer,
		sizeof(state.scriptEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (!core.changeObjectScript(objectId, state.scriptEditBuffer,
			script->isScriptEnabled()))
		{
			// refused = no-op (same path); snap the field back to reality
			SDL_strlcpy(state.scriptEditBuffer, currentScript.c_str(),
				sizeof(state.scriptEditBuffer));
		}
		// on success the next frame re-syncs via scriptEditCurrentScript
	}
	ImGui::SetItemTooltip("project-relative Lua script path, e.g. "
		"scripts/player.lua (Enter applies; runs in Play mode only)");
	bool enabled = script->isScriptEnabled();
	if (ImGui::Checkbox("Enabled", &enabled))
	{
		core.changeObjectScript(objectId, currentScript, enabled);
	}
	ImGui::SetItemTooltip("disabled scripts load but never update");
	if (script->hasScriptError())
	{
		ImGui::TextColored(ImVec4(0.94f, 0.35f, 0.35f, 1.0f),
			"(script error)");
		ImGui::SetItemTooltip("%s", script->getScriptError().c_str());
	}
}

// RigidBodyComponent editor: creation parameters (BodyDesc) only - every
// edit is one undoable RigidBodyChangeCommand; drag widgets merge their
// per-frame edits into ONE undo step exactly like the transform drags.
void drawRigidBodyComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::PhysicsWorld::BodyDesc before;
	if (!core.getRigidBodyDesc(objectId, before))
	{
		return;
	}
	Orkige::PhysicsWorld::BodyDesc after = before;
	bool edited = false;

	static const char* const bodyTypeNames[] =
		{ "Static", "Kinematic", "Dynamic" };
	int bodyType = static_cast<int>(before.bodyType);
	if (ImGui::Combo("Body Type", &bodyType, bodyTypeNames, 3))
	{
		after.bodyType =
			static_cast<Orkige::PhysicsWorld::BodyType>(bodyType);
		// click widgets get a fresh session - they never merge
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	static const char* const shapeTypeNames[] =
		{ "Box", "Sphere", "Capsule" };
	int shapeType = static_cast<int>(before.shapeType);
	if (ImGui::Combo("Shape", &shapeType, shapeTypeNames, 3))
	{
		after.shapeType =
			static_cast<Orkige::PhysicsWorld::ShapeType>(shapeType);
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	// shape dimensions (which ones apply follows the CURRENT shape)
	if (before.shapeType == Orkige::PhysicsWorld::ST_BOX)
	{
		float halfExtents[3] = { before.halfExtents.x, before.halfExtents.y,
			before.halfExtents.z };
		const bool changed =
			ImGui::DragFloat3("Half Extents", halfExtents, 0.05f, 0.01f,
				1000.0f);
		if (ImGui::IsItemActivated())
		{
			state.inspectorMergeSession = core.beginMergeSession();
		}
		if (changed)
		{
			after.halfExtents = Orkige::Vec3(halfExtents[0], halfExtents[1],
				halfExtents[2]);
			edited = true;
		}
	}
	else
	{
		float radius = before.radius;
		bool changed = ImGui::DragFloat("Radius", &radius, 0.05f, 0.01f,
			1000.0f);
		if (ImGui::IsItemActivated())
		{
			state.inspectorMergeSession = core.beginMergeSession();
		}
		if (changed)
		{
			after.radius = radius;
			edited = true;
		}
		if (before.shapeType == Orkige::PhysicsWorld::ST_CAPSULE)
		{
			float halfHeight = before.halfHeight;
			changed = ImGui::DragFloat("Half Height", &halfHeight, 0.05f,
				0.01f, 1000.0f);
			if (ImGui::IsItemActivated())
			{
				state.inspectorMergeSession = core.beginMergeSession();
			}
			if (changed)
			{
				after.halfHeight = halfHeight;
				edited = true;
			}
		}
	}

	float mass = before.mass;
	bool changed = ImGui::DragFloat("Mass", &mass, 0.1f, 0.0f, 100000.0f,
		"%.2f kg");
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.mass = mass;
		edited = true;
	}

	float friction = before.friction;
	changed = ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 5.0f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.friction = friction;
		edited = true;
	}

	float restitution = before.restitution;
	changed = ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.restitution = restitution;
		edited = true;
	}

	bool planar = before.planar;
	if (ImGui::Checkbox("Planar (2D: X/Y plane)", &planar))
	{
		after.planar = planar;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	if (edited)
	{
		core.applyRigidBodyChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

// CameraComponent editor: projection mode combo + ortho size drag - one
// undoable CameraChangeCommand per edit, drags merge like the transforms
void drawCameraComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::EditorCameraSettings before;
	if (!core.getCameraSettings(objectId, before))
	{
		return;
	}
	Orkige::EditorCameraSettings after = before;
	bool edited = false;

	static const char* const projectionNames[] =
		{ "Perspective", "Orthographic" };
	int projection = before.projectionMode;
	if (ImGui::Combo("Projection", &projection, projectionNames, 2))
	{
		after.projectionMode = projection;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}
	ImGui::SetItemTooltip("orthographic = 2D projection (applies to the "
		"engine camera in Play mode; the editor viewport stays perspective)");

	if (before.projectionMode ==
		static_cast<int>(Orkige::CameraComponent::PM_ORTHOGRAPHIC))
	{
		float orthoSize = before.orthoSize;
		const bool changed = ImGui::DragFloat("Ortho Size", &orthoSize,
			0.1f, 0.01f, 10000.0f, "%.2f wu");
		if (ImGui::IsItemActivated())
		{
			state.inspectorMergeSession = core.beginMergeSession();
		}
		if (changed)
		{
			after.orthoSize = orthoSize;
			edited = true;
		}
		ImGui::SetItemTooltip("world units from the view center to the top "
			"edge (the camera sees 2x this height)");
	}

	if (edited)
	{
		core.applyCameraChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

// SpriteComponent editor: texture name (Enter applies), size/tint/flip/
// z-order/visibility - one undoable SpriteChangeCommand per edit
void drawSpriteComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::EditorSpriteSettings before;
	if (!core.getSpriteSettings(objectId, before))
	{
		return;
	}
	Orkige::EditorSpriteSettings after = before;
	bool edited = false;

	// texture field: rebuild the buffer when the selection or the texture
	// changed behind the field (undo/redo, another panel)
	if (state.spriteEditObjectId != objectId ||
		state.spriteEditCurrentTexture != before.textureName)
	{
		state.spriteEditObjectId = objectId;
		state.spriteEditCurrentTexture = before.textureName;
		SDL_strlcpy(state.spriteEditBuffer, before.textureName.c_str(),
			sizeof(state.spriteEditBuffer));
	}
	if (ImGui::InputText("Texture", state.spriteEditBuffer,
		sizeof(state.spriteEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		after.textureName = state.spriteEditBuffer;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}
	ImGui::SetItemTooltip("texture resource name, e.g. ball.png from the "
		"project's assets/ (Enter reloads the sprite)");

	float size[2] = { before.width, before.height };
	bool changed = ImGui::DragFloat2("Size", size, 0.05f, 0.0f, 10000.0f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.width = size[0];
		after.height = size[1];
		edited = true;
	}
	ImGui::SetItemTooltip("world units; 0 derives the dimension from the "
		"texture aspect ratio");

	float tint[4] = { before.tint[0], before.tint[1], before.tint[2],
		before.tint[3] };
	changed = ImGui::ColorEdit4("Tint", tint);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		for (int each = 0; each < 4; ++each)
		{
			after.tint[each] = tint[each];
		}
		edited = true;
	}

	bool flipX = before.flipX;
	if (ImGui::Checkbox("Flip X", &flipX))
	{
		after.flipX = flipX;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}
	ImGui::SameLine();
	bool flipY = before.flipY;
	if (ImGui::Checkbox("Flip Y", &flipY))
	{
		after.flipY = flipY;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	int zOrder = before.zOrder;
	changed = ImGui::DragInt("Z-Order", &zOrder, 0.1f,
		Orkige::SpriteComponent::ZORDER_MIN,
		Orkige::SpriteComponent::ZORDER_MAX);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.zOrder = zOrder;
		edited = true;
	}
	ImGui::SetItemTooltip("higher renders on top; overlapping sprites should "
		"use distinct values (alpha sorting within one value is by camera "
		"distance)");

	bool visible = before.visible;
	if (ImGui::Checkbox("Visible", &visible))
	{
		after.visible = visible;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	if (edited)
	{
		core.applySpriteChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

//! case-insensitive substring match for the Add Component search box
bool containsIgnoreCase(std::string const& haystack, std::string const& needle)
{
	if (needle.empty())
	{
		return true;
	}
	return Orkige::StringUtil::to_lower_copy(haystack).find(
		Orkige::StringUtil::to_lower_copy(needle)) != std::string::npos;
}

// The "Add Component" button + searchable popup at the bottom of the
// Inspector: lists every registered component type (already-attached ones
// are disabled), the text box filters, a click adds through the undoable
// AddComponentCommand (dependencies come along automatically).
void drawAddComponentButton(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::GameObject> const& gameObject)
{
	ImGui::Spacing();
	const float buttonWidth = 180.0f;
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	if (availableWidth > buttonWidth)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
			(availableWidth - buttonWidth) * 0.5f);
	}
	if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0.0f)))
	{
		state.addComponentSearch[0] = '\0';
		state.addComponentFocusPending = true;
		ImGui::OpenPopup("##addcomponent");
	}
	if (ImGui::BeginPopup("##addcomponent"))
	{
		if (state.addComponentFocusPending)
		{
			ImGui::SetKeyboardFocusHere();
			state.addComponentFocusPending = false;
		}
		ImGui::SetNextItemWidth(240.0f);
		ImGui::InputTextWithHint("##componentsearch", "search components...",
			state.addComponentSearch, sizeof(state.addComponentSearch));
		ImGui::Separator();
		for (std::string const& typeName : core.getAddableComponentTypes())
		{
			if (!containsIgnoreCase(typeName, state.addComponentSearch))
			{
				continue;
			}
			const bool attached =
				gameObject->hasComponent(Orkige::TypeInfo(typeName));
			ImGui::BeginDisabled(attached);
			if (ImGui::MenuItem(typeName.c_str(), attached ? "added" : nullptr))
			{
				if (!core.addComponentToObject(gameObject->getObjectID(),
					typeName))
				{
					SDL_Log("orkige_editor: adding %s to '%s' failed",
						typeName.c_str(),
						gameObject->getObjectID().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
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
	// neutral component headers, same reasoning as the edit-mode inspector
	ImGui::PushStyleColor(ImGuiCol_Header,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
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
	ImGui::PopStyleColor(3); // neutral component headers
}

void drawInspectorPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, bool* visible)
{
	const bool remote = session.isActive();
	if (ImGui::Begin(remote ? INSPECTOR_WINDOW_REMOTE : INSPECTOR_WINDOW_EDIT,
		visible))
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
			const std::string objectId = gameObject->getObjectID();
			ImGui::Text("%s", objectId.c_str());
			if (core.getSelectionCount() > 1)
			{
				// multi-select groundwork: the Inspector edits the PRIMARY
				ImGui::TextDisabled("%zu selected - showing primary",
					core.getSelectionCount());
			}
			ImGui::TextDisabled("type: %s",
				gameObject->getTypeInfo().getName().c_str());
			ImGui::Separator();
			// the theme's accent Header colour is for list selection; the
			// component CollapsingHeaders read better neutral (macOS
			// disclosure groups are grey, not blue)
			ImGui::PushStyleColor(ImGuiCol_Header,
				ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
				ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive,
				ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
			// iterate a snapshot of the attached types: the remove button
			// mutates the component map mid-loop
			std::vector<Orkige::TypeInfo> componentTypes;
			for (auto const& [componentType, component] :
				gameObject->getComponents())
			{
				componentTypes.push_back(componentType);
			}
			for (Orkige::TypeInfo const& componentType : componentTypes)
			{
				const std::string typeName = componentType.getName();
				ImGui::PushID(typeName.c_str());
				// remember where the header's line ends so the small remove
				// button can overlap its right edge
				const float headerRight = ImGui::GetCursorPosX() +
					ImGui::GetContentRegionAvail().x;
				const bool headerOpen = ImGui::CollapsingHeader(
					typeName.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
					ImGuiTreeNodeFlags_AllowOverlap);
				// remove affordances: a small x on the header + context menu.
				// Removal is blocked while another attached component depends
				// on this one (honest check against the addDependency info).
				std::string blockedBy;
				const bool removable = core.canRemoveComponent(objectId,
					typeName, &blockedBy);
				if (ImGui::BeginPopupContextItem("##componentmenu"))
				{
					ImGui::BeginDisabled(!removable);
					if (ImGui::MenuItem("Remove Component"))
					{
						core.removeComponentFromObject(objectId, typeName);
					}
					ImGui::EndDisabled();
					if (!removable && !blockedBy.empty())
					{
						ImGui::TextDisabled("required by %s",
							blockedBy.c_str());
					}
					ImGui::EndPopup();
				}
				bool removedNow = false;
				const float removeButtonWidth = ImGui::GetFrameHeight();
				ImGui::SameLine(headerRight - removeButtonWidth);
				ImGui::BeginDisabled(!removable);
				if (ImGui::SmallButton("x"))
				{
					removedNow =
						core.removeComponentFromObject(objectId, typeName);
				}
				ImGui::EndDisabled();
				if (!removable && !blockedBy.empty())
				{
					ImGui::SetItemTooltip("required by %s", blockedBy.c_str());
				}
				else
				{
					ImGui::SetItemTooltip("Remove Component");
				}
				if (removedNow || !gameObject->hasComponent(componentType))
				{
					ImGui::PopID();
					continue;
				}
				if (!headerOpen)
				{
					ImGui::PopID();
					continue;
				}
				Orkige::GameObjectComponent* component =
					gameObject->getComponentPtr(componentType);
				if (dynamic_cast<Orkige::TransformComponent*>(component))
				{
					drawTransformComponentUI(state, core, objectId);
				}
				else if (auto* model =
					dynamic_cast<Orkige::ModelComponent*>(component))
				{
					drawModelComponentUI(state, core, objectId, model);
				}
				else if (dynamic_cast<Orkige::RigidBodyComponent*>(component))
				{
					drawRigidBodyComponentUI(state, core, objectId);
				}
				else if (dynamic_cast<Orkige::SpriteComponent*>(component))
				{
					drawSpriteComponentUI(state, core, objectId);
				}
				else if (dynamic_cast<Orkige::CameraComponent*>(component))
				{
					drawCameraComponentUI(state, core, objectId);
				}
				else if (auto* script =
					dynamic_cast<Orkige::ScriptComponent*>(component))
				{
					drawScriptComponentUI(state, core, objectId, script);
				}
				else
				{
					ImGui::TextDisabled("(no editable properties yet)");
				}
				ImGui::PopID();
			}
			ImGui::PopStyleColor(3); // neutral component headers
			ImGui::Separator();
			drawAddComponentButton(state, core, gameObject);
		}
	}
	ImGui::End();
}

// The keyboard shortcut map (checked once per frame, after the panels have
// recorded their hover/focus state; inactive while a text field is being
// edited; only Cmd/Ctrl+P works while a play session runs):
//   global: Cmd/Ctrl+P play/stop toggle (Unity), Cmd/Ctrl+Z undo,
//   Shift+Cmd/Ctrl+Z redo, Cmd/Ctrl+N new scene, Cmd/Ctrl+O open scene,
//   Cmd/Ctrl+S save, Shift+Cmd/Ctrl+S save as
//   Scene panel hovered/focused: Q select, W translate, E rotate, R scale,
//   X world/local, F frame selected
//   Scene panel OR focused Hierarchy: F2 rename, Delete/Backspace delete,
//   Cmd/Ctrl+D duplicate (Unity's hierarchy shortcuts work there too)
//   ... all of which stand down while fly mode is active (right mouse held):
//   W/A/S/D/Q/E are camera movement then
// On mac WITH the native menu bar installed the File shortcuts never reach
// this map: AppKit consumes the menu key equivalents (Cmd+N/O/S/Shift+S)
// before SDL sees them - the ImGui-side bindings below cover the non-mac
// and headless-fallback cases with the same keys, no double execution.
void handleEditorShortcuts(EditorState& state, Orkige::EditorCore& core,
	PlaySession& session, optr<Orkige::RenderCamera> const& sceneCamera,
	SDL_Window* window)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput)
	{
		return;
	}
	const bool commandDown = io.KeySuper || io.KeyCtrl;
	// Cmd/Ctrl+P: Unity's play toggle - Play in edit mode, Stop while a
	// session runs (this calls the exact functions the toolbar buttons call;
	// no shortcut for Pause, that stays a toolbar action)
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_P, false))
	{
		if (!session.isActive())
		{
			startPlay(session, core.getGameObjectManager(), state.project);
		}
		else if (session.mode != PlaySession::Mode::Stopping)
		{
			requestStopPlay(session);
		}
		return;
	}
	if (session.isActive())
	{
		return;
	}
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
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_N, false))
	{
		newScene(state, core);
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_O, false))
	{
		requestFileDialog(state, window, Orkige::FileDialogAction::OpenScene);
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		// Shift = Save As; a plain save on an unsaved scene also needs the
		// dialog (same rule as the File menu)
		if (io.KeyShift || state.currentScenePath.empty())
		{
			requestFileDialog(state, window,
				Orkige::FileDialogAction::SaveSceneAs);
		}
		else
		{
			saveSceneToPath(state, core, state.currentScenePath);
		}
		return;
	}
	// object shortcuts (duplicate/rename/delete) work from the Scene panel
	// AND the focused Hierarchy - Unity muscle memory; tool switching stays
	// Scene-panel-only (letters typed while other panels have focus must
	// not silently flip tools)
	const bool sceneContext = state.scenePanelHovered ||
		state.scenePanelFocused;
	if (!sceneContext && !state.hierarchyFocused)
	{
		return;
	}
	// while flying (right mouse held) the letter keys belong to the camera
	// (WASD move, Q/E vertical) - no tool switching, no destructive edits
	if (state.flyActive || ImGui::IsMouseDown(ImGuiMouseButton_Right))
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
	if (sceneContext)
	{
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

void drawStatsPanel(bool* visible)
{
	if (ImGui::Begin("Stats", visible))
	{
		const Orkige::RenderSystem::FrameStats stats =
			Orkige::RenderSystem::get()->getFrameStats();
		ImGui::Text("FPS: %.1f (avg %.1f)", stats.lastFPS, stats.avgFPS);
		ImGui::Text("Triangles: %zu", stats.triangleCount);
		ImGui::Text("Batches: %zu", stats.batchCount);
	}
	ImGui::End();
}

// the Console's Log tab: engine (Ogre) log + editor SDL_Log lines +
// [remote] player lines, severity-coloured, with clear / auto-scroll /
// text filter controls
void drawConsoleLogTab(EditorConsole& console)
{
	if (ImGui::Button("Clear"))
	{
		console.clear();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &console.autoScroll);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	console.filter.Draw("##consolefilter");
	ImGui::SetItemTooltip("filter (\"incl,-excl\" syntax)");
	if (ImGui::BeginChild("##consolelines", ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar))
	{
		std::lock_guard<std::mutex> lock(console.mutex);
		for (ConsoleLine const& line : console.lines)
		{
			if (!console.filter.PassFilter(line.text.c_str()))
			{
				continue;
			}
			switch (line.level)
			{
			case ConsoleLevel::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.95f, 0.80f, 0.25f, 1.0f));
				break;
			case ConsoleLevel::Error:
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
				break;
			case ConsoleLevel::Info:
			default:
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImGui::GetStyleColorVec4(ImGuiCol_Text));
				break;
			}
			ImGui::TextUnformatted(line.text.c_str());
			ImGui::PopStyleColor();
		}
		if (console.scrollToBottom)
		{
			if (console.autoScroll)
			{
				ImGui::SetScrollHereY(1.0f);
			}
			console.scrollToBottom = false;
		}
	}
	ImGui::EndChild();
}

// The Console panel: the former Lua Console grown into a proper console -
// a Log tab streaming the engine/editor/remote log lines plus the Lua REPL
// as a second tab.
void drawConsolePanel(EditorState& state, EditorConsole& console,
	bool* visible)
{
	if (ImGui::Begin("Console", visible))
	{
		if (ImGui::BeginTabBar("##consoletabs"))
		{
			if (ImGui::BeginTabItem("Log"))
			{
				drawConsoleLogTab(console);
				ImGui::EndTabItem();
			}
			// the script REPL tab only exists while a scripting runtime is
			// live (a ORKIGE_SCRIPTING=OFF build shows just the Log tab)
			if (Orkige::ScriptRuntime::available() &&
				ImGui::BeginTabItem("Lua"))
			{
				const float footerHeight =
					ImGui::GetFrameHeightWithSpacing() * 4.0f;
				if (ImGui::BeginChild("history", ImVec2(0, -footerHeight),
					ImGuiChildFlags_Borders))
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
					runLuaConsoleInput(state);
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear"))
				{
					state.luaHistory.clear();
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

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
