// EditorApp.h - the editor shell's shared state and cross-TU seams.
//
// main.cpp used to be one ~8600-line TU; it is now split into per-concern
// translation units (EditorConsole/EditorExport/EditorSettings/
// EditorDeviceTargets/EditorPlaySession/EditorDocument/EditorScenePanel/
// EditorFileDialogs/EditorMenus/EditorToolbar/EditorHierarchyPanel/
// EditorInspectorPanel/EditorShortcuts/EditorStatsPanel + main.cpp) so a
// one-panel edit recompiles one small TU. This header carries the shared
// structs (EditorState, PlaySession, ViewSettings, EditorConsole, ...), the
// few editor-wide globals and the function seams between those TUs. It is a
// MECHANICAL decomposition of the former anonymous-namespace contents -
// the moved code itself is unchanged (see main.cpp's history).
#ifndef ORKIGE_EDITORAPP_H_09072026
#define ORKIGE_EDITORAPP_H_09072026

#include <SDL3/SDL.h>
#include <imgui.h>

#include "EditorAssetDnd.h"
#include "EditorCamera.h"
#include "EditorCore.h"
#include "EditorTheme.h"
#include "FileDialog.h"
#include "MarqueeSelection.h"

#include <core_debugnet/DebugClient.h>
#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_util/String.h>
#include <engine_base/EngineLog.h>
#include <engine_render/MeshInstance.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderTexture.h>

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

using Orkige::optr;
using Orkige::woptr;

namespace Orkige
{
	class ImGuiFacadeRenderer;
	class ImGuiSDL3Input;
	class RenderWorld;
}

namespace OrkigeEditor
{
	class GuiPreviewStage;	//!< the GUI Preview stage (GuiPreviewStage.h)
}

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
	EditorConsole& console);

//! SDL log output hook state: the editor's own SDL_Log lines go into the
//! Console AND to the previous (default) output so the terminal keeps them
struct SdlLogHook
{
	EditorConsole* console = nullptr;
	SDL_LogOutputFunction previous = nullptr;
	void* previousUserdata = nullptr;
};

void SDLCALL consoleSdlLogOutput(void* userdata, int category,
	SDL_LogPriority priority, const char* message);

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
	std::string platform;			//!< "macos" / "ios-simulator" / "ios" / "android"
	std::string outputBuffer;		//!< partial (unterminated) last line
	std::string artifactPath;		//!< from the final OK line
	//! @brief iOS-device deploy continuation (Play on a connected iPhone): when
	//! non-empty the successful "ios" export is followed by an install + launch
	//! on this device via devicectl (see updateExportJob). Empty for a plain
	//! Build-menu export - the behavior there is unchanged. The game runs
	//! standalone on the device; the editor opens NO live debug link (a USB
	//! device has no dependency-free TCP tunnel - see Docs/ios-signing.md).
	std::string deployDeviceUdid;
	std::string deployDeviceLabel;	//!< display name of the deploy device
	bool isActive() const { return this->process != nullptr; }
};

//! @brief launch the exporter for the open project (async; false when it
//! cannot start) - see EditorExport.cpp
bool startExport(ExportJob& job, Orkige::Project const& project,
	std::string const& platform, EditorConsole& console);

//! @brief per-frame pump: stream the exporter's output into the Console as
//! "[export]" lines and report the outcome on exit
void updateExportJob(ExportJob& job, EditorConsole& console);

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
	//! 2D editor mode: the Scene viewport's OWN camera switches to an
	//! orthographic top-down look at the XY plane (orbit/fly disabled, pan+zoom
	//! kept, the transform gizmo constrains to that plane). A pure view/
	//! interaction feature - no scene object is touched.
	bool editor2D = false;
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
	//! project Asset browser panel (project-only content)
	bool showAssetBrowserPanel = true;
	//! Tile Palette panel (project-only; arms a prefab for 2D grid painting)
	bool showTilePalettePanel = true;
	//! GUI Preview panel (project-only; renders a .oui screen at a simulated
	//! device context into an offscreen target - the collaborative UI loop)
	bool showGuiPreviewPanel = false;
	//! snap settings (toolbar toggle + editable step values);
	//! mirrored into EditorCore on startup, persisted on every popover edit
	bool snapEnabled = false;
	float snapTranslate = Orkige::EditorCore::SNAP_TRANSLATE;
	float snapRotateDegrees = Orkige::EditorCore::SNAP_ROTATE_DEGREES;
	float snapScale = Orkige::EditorCore::SNAP_SCALE;
	//! Asset browser thumbnail cell size in points (the content-pane size
	//! slider); persisted so the browser reopens at the chosen zoom
	float assetThumbnailSize = 64.0f;
	//! reopen the most recent project on launch; automation
	//! runs (any ORKIGE_EDITOR_*/ORKIGE_DEMO_* hook) always start blank
	bool reopenLastProject = true;
	//! editor chrome appearance (View > Theme): System follows the OS, or the
	//! user pins Dark/Light. Applied at boot and on every change.
	Orkige::EditorThemeMode themeMode = Orkige::EditorThemeMode::System;
	//! the content scale the persisted dock layout was saved at. A layout is a
	//! set of absolute-pixel node sizes; restoring one saved at a different
	//! scale (e.g. moved from a 1x to a 2x display) mis-proportions the panels,
	//! so drawDockspace rebuilds the ratio-based default when this differs from
	//! the live scale. 0 = unknown (older ini / never saved).
	float layoutContentScale = 0.0f;
	//! most-recently-used scene paths (newest first) for File > Open Recent;
	//! filled by every successful open/save, capped at MAX_RECENT_SCENES
	std::vector<std::string> recentScenes;
	//! most-recently-used project roots (newest first) for File > Open
	//! Recent Project; filled by every successful project open/create
	std::vector<std::string> recentProjects;
	std::string path;				//!< backing file ("" = don't persist)

	void load();
	void save() const;

	//! record a successfully opened/saved scene path for File > Open Recent:
	//! move-to-front, dedupe, cap at MAX_RECENT_SCENES (caller persists)
	void addRecentScene(std::string const& scenePath);

	//! record a successfully opened/created project root for File > Open
	//! Recent Project (same move-to-front/dedupe/cap rule as the scenes)
	void addRecentProject(std::string const& projectRoot);

	//! restore the factory camera/display values (View > Reset View Settings);
	//! panel visibility is NOT touched - that belongs to Reset Layout
	void resetCameraAndDisplayDefaults();

	//! all panels visible again (Reset Layout re-opens everything)
	void showAllPanels();
};

// The live ViewSettings instance (owned by main), reachable from the scene
// open/save functions so every successful open/save feeds File > Open Recent
// without threading a ViewSettings& through all their call sites.
extern ViewSettings* gViewSettings;

// The live EditorState (owned by main). Global so the shared menu widgets
// (drawn from both the ImGui View menu and the floating View Settings window)
// can raise one-shot requests - e.g. a Theme change flagging a re-apply - back
// to the main loop without threading EditorState& through every widget helper.
struct EditorState; // defined below
extern EditorState* gEditorState;

// the ImGui-on-facade renderer (owned by main; global so drawScenePanel can
// register the scene RTT for ImGui::Image without threading it through every
// draw* signature - same pattern as gViewSettings)
extern Orkige::ImGuiFacadeRenderer* gImGuiRenderer;
// false during automated runs: the scripted tests open temp scenes/projects
// through the same functions a user does, and those must never pollute the
// interactive Open Recent lists (or become the reopened "last project")
extern bool gRecordRecents;

//! record a scene path in the Open Recent list and persist it
void recordRecentScene(std::string const& scenePath);

//! record a project root in the Open Recent Project list and persist it
void recordRecentProject(std::string const& projectRoot);

//! one cached thumbnail: the ImGui texture id (0 = tried, not a loadable
//! image) and the file mtime it was loaded at (a changed mtime reloads).
//! uploadName is non-empty only for CPU-rasterized thumbnails we own as a
//! RenderSystem::createTexture2D upload (vector shapes) - the eviction sites
//! destroy it so the named GPU texture is not leaked; resource-system
//! textures (images) leave it empty (nothing per-tile to free).
struct AssetThumbnail
{
	ImTextureID textureId = 0;
	long long mtime = 0;
	std::string uploadName;
};

//! Asset browser panel state that lives across frames: folder navigation with
//! back/forward history, search + type filtering, the thumbnail cache and its
//! per-frame load queue, and the multi-item asset-op working set (selection,
//! inline rename, pending delete). Selection and rename are keyed by
//! project-relative path so they survive re-enumeration and renames of OTHER
//! items.
struct AssetBrowserState
{
	//! the folder shown in the content pane (absolute path; "" = reset to the
	//! open project's root on the next draw). The folder TREE, breadcrumb and
	//! back/forward buttons drive it; descending into a subfolder also sets it.
	std::string currentDir;
	//! navigation history (back/forward buttons; navigateTo pushes the old
	//! folder onto backHistory and clears forwardHistory)
	std::vector<std::string> backHistory;
	std::vector<std::string> forwardHistory;
	//! thumbnail cell size in points; below LIST_THRESHOLD the content pane
	//! renders as compact list rows (the slider spans both continuously)
	float thumbnailSize = 64.0f;
	static constexpr float THUMBNAIL_MIN = 20.0f;
	static constexpr float THUMBNAIL_MAX = 160.0f;
	static constexpr float LIST_THRESHOLD = 28.0f;
	//! search text (name substring, case-insensitive); a non-empty value
	//! switches the content pane into recursive/flat result mode
	char searchText[256] = "";
	//! search descends into subfolders of currentDir (else the current folder
	//! only)
	bool searchRecursive = true;
	//! type-filter chips: a bitmask over AssetKind (bit = 1u << kind; 0 = show
	//! everything). Applies in both folder mode and search mode.
	unsigned int kindFilterMask = 0;
	//! multi-selection, keyed by relativePath (survives refresh); the last
	//! focused path is the range anchor and the "primary"
	std::set<std::string> selection;
	std::string selectionAnchor;
	//! inline rename target ("" = none) + edit buffer + first-frame focus
	std::string renamingPath;
	char renameBuffer[256] = "";
	bool renameFocusPending = false;
	//! delete-confirm modal payload (absolute paths; folders included). A
	//! non-empty set opens the confirm modal and is consumed on Delete/Cancel.
	std::vector<std::string> pendingDelete;
	//! thumbnail cache keyed by ABSOLUTE path (two folders may hold same-named
	//! files while the user reorganises). Bounded (cleared wholesale past
	//! ASSET_THUMBNAIL_CACHE_MAX - visible working sets are small).
	std::map<std::string, AssetThumbnail> thumbnails;
	//! per-frame thumbnail load queue: absolute paths the last draw wanted but
	//! did not have cached, serviced oldest-first a few per frame so a big
	//! folder never hitches
	std::deque<std::string> thumbnailQueue;
	//! the panel held keyboard focus while drawing (gates its local shortcut
	//! handling, mirrors scenePanelFocused / hierarchyFocused)
	bool focused = false;
	//! the Inspector's Texture Import Settings edit cache: the settings being
	//! edited and the sidecar path they were read from (a changed path re-reads
	//! from disk, so the section always reflects the selected texture)
	Orkige::TextureImport editImport;
	std::string editImportPath;
	//! transient footer notice (e.g. a create that failed): shown in the status
	//! footer until statusMessageExpiry (ImGui::GetTime seconds) passes. A
	//! failed filesystem op is otherwise invisible, so it surfaces here.
	std::string statusMessage;
	double statusMessageExpiry = 0.0;
};

//! @brief Tile Palette panel state: the prefab currently ARMED for grid
//! painting plus the paint options applied to every painted cell, and the
//! in-progress stroke bookkeeping. Arming a prefab switches the active tool to
//! Paint; the Scene panel then paints/erases instances snapped to the grid in
//! 2D editor mode. A passive data struct (plain camelCase fields).
struct TilePaletteState
{
	//! the armed paint asset ("" = nothing armed, the Paint tool paints nothing).
	//! The palette arms three kinds: a Prefab (instantiates the .oprefab), a
	//! Texture or a VectorShape - the last two paint BARE tiles (a grid-cell
	//! sprite/shape object, no prefab file). armedKind says which.
	std::string armedAssetPath;		//!< absolute asset path (.oprefab / texture / .oshape)
	std::string armedAssetRef;			//!< project-relative reference
	std::string armedAssetId;		//!< stable .orkmeta id
	AssetKind armedKind = AssetKind::Prefab;	//!< Prefab / Texture / VectorShape
	//! ghost-preview image for the armed asset: for a prefab, the bare texture
	//! (else vector-shape) ref probed from its visual at arm time; for a bare
	//! texture/shape, the asset itself. Plus the resolved absolute path the
	//! thumbnail machinery loads. Both empty for a pure-logic prefab (no
	//! drawable) - the paint tool then shows only the outline.
	std::string previewImageRef;
	std::string previewImagePath;
	//! prefab probe results, refreshed on arm (PrefabSerializer::listPrefabInfo);
	//! empty for a bare-asset arm (no prefab file, no edge walls)
	std::vector<std::string> prefabLocalIds;	//!< every local id in the file
	bool hasEdgeWalls = false;			//!< all four TileComponent wall locals present
	bool rootHasTileComponent = false;	//!< the prefab root already carries one
	//! paint options: which edges to leave open (top/bottom/left/right) and the
	//! comma-separated tags stamped on each painted root
	bool edgeOpen[4] = {};
	char paintTags[256] = "";
	//! stroke bookkeeping (reset on mouse release / tool switch): one undo step
	//! per stroke rides strokeSession, lastCol/lastRow throttle same-cell repaints
	unsigned int strokeSession = 0;
	bool strokeActive = false;
	int lastCol = 0;
	int lastRow = 0;
	//! the panel held keyboard focus while drawing
	bool focused = false;
};

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
	//! Build menu request ("macos"/"ios-simulator"/"ios"/"android"; "" = none) -
	//! menus (native or ImGui) set it, the frame loop starts the export
	std::string requestedExport;
	//! iOS-device deploy request (Play on a connected iPhone): the toolbar sets
	//! the UDID + label, the frame loop runs an "ios" export whose success
	//! installs + launches on the device (see the deploy fields on ExportJob).
	//! "" = none. Requires a loaded project + iOS signing configured; the frame
	//! loop reports honestly to the Console when those are missing.
	std::string requestedIosDeviceDeployUdid;
	std::string requestedIosDeviceDeployLabel;
	//! floating View Settings window (grid/gizmo/camera-feel sliders); on mac
	//! the native View > View Settings... opens it since the ImGui menu bar
	//! that used to host the sliders is not drawn there
	bool showViewSettingsWindow = false;
	//! View > Reset Layout: rebuild the default dock layout next frame
	bool resetDockLayout = false;
	//! View > Theme changed: re-apply the ImGui style (and refresh the window
	//! clear colour) next frame; the main loop consumes and clears it
	bool themeReapplyRequested = false;
	//! Scene panel interaction state recorded while drawing (gates the tool
	//! shortcuts to "Scene panel hovered/focused")
	bool scenePanelHovered = false;
	bool scenePanelFocused = false;
	bool hierarchyFocused = false;
	//! gizmo drag bracketing: the whole drag merges into ONE undo command
	bool gizmoWasUsing = false;
	unsigned int gizmoMergeSession = 0;
	//! marquee (rubber-band) box select in the Scene viewport (Select/Translate
	//! tools): a left-drag on EMPTY space band-selects every object whose
	//! projected bounds fall inside the box; Cmd/Ctrl/Shift EXTEND the current
	//! selection instead of replacing it. Tracked in render-target pixels
	//! (io.MousePos space). marqueePending is a press that has not yet travelled
	//! past the drag threshold (still a click, not a marquee); it flips
	//! marqueeActive on once the cursor moves far enough. A press that never
	//! becomes a drag falls back to the plain empty-space deselect on release.
	bool marqueePending = false;
	bool marqueeActive = false;
	bool marqueeExtend = false;			//!< Cmd/Ctrl/Shift held at press
	ImVec2 marqueeStart = ImVec2(0.0f, 0.0f);
	ImVec2 marqueeCurrent = ImVec2(0.0f, 0.0f);
	//! the Scene image's screen rect recorded each frame (render-target pixels):
	//! the panel writes it while drawing so the selfchecks can synthesise mouse
	//! events at known viewport positions (marquee/drag drives the real path)
	ImVec2 sceneImageMin = ImVec2(0.0f, 0.0f);
	ImVec2 sceneImageSize = ImVec2(0.0f, 0.0f);
	//! inspector drag bracketing (only one drag widget is active at a time)
	unsigned int inspectorMergeSession = 0;
	//! Hierarchy search/filter box; ImGuiTextFilter supports
	//! comma-separated terms and "-term" exclusion, empty = show everything
	ImGuiTextFilter hierarchyFilter;
	//! Asset browser panel state (navigation, filter, thumbnails, asset-op
	//! working set) - see AssetBrowserState
	AssetBrowserState assetBrowser;
	//! Tile Palette panel state (the armed prefab + paint options + stroke
	//! bookkeeping) - see TilePaletteState
	TilePaletteState tilePalette;
	//! inline rename in the Hierarchy (F2 / context menu)
	std::string renamingObjectId;
	char renameBuffer[256] = "";
	bool renameFocusPending = false;
	//! "Add Component" popup search state (Inspector)
	char addComponentSearch[128] = "";
	bool addComponentFocusPending = false;
	// (the ModelComponent mesh / ScriptComponent script / SpriteComponent
	// texture field buffers are gone: the auto Inspector's generic property
	// widgets keep their own per-field state)
	//! object tags field edit buffer (comma-separated; rebuilt when the
	//! selection or the object's current tag set changes)
	char tagsEditBuffer[512] = "";
	std::string tagsEditObjectId;
	std::string tagsEditCurrent;
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

// The editor's out-of-process play mode: Play saves the CURRENT scene to a
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
	//! play target picked in the toolbar: empty = not iOS hardware, otherwise
	//! the UDID of a USB-connected iPhone/iPad (enabled only once iOS signing
	//! is configured - see isIosSigningConfigured). Unlike the simulator/adb
	//! targets this is NOT a live play session: Play on a device builds + signs
	//! + installs + launches the game standalone (an "ios" export followed by a
	//! devicectl install/launch), because a USB device shares neither the host
	//! filesystem (no temp-scene handoff) nor its loopback, and no dependency-
	//! free CLI forwards a debug-port TCP tunnel to it. Live debug over USB is
	//! the documented gap (Docs/ios-signing.md); the pick drives an export-and-
	//! deploy, not the remote hierarchy/inspector link.
	std::string iosDeviceUdid;
	std::string iosDeviceLabel;		//!< display name of the picked iOS device
	//! native module compile-on-Play (mode == Building): the remaining
	//! cmake command queue (configure-if-needed, then build), the running
	//! step (stdout+stderr piped, streamed into the Console per frame) and
	//! the executable a successful build launches as the play process
	std::vector<std::vector<std::string>> buildSteps;
	SDL_Process* buildProcess = nullptr;
	std::string buildOutputBuffer;		//!< partial (unterminated) last line
	std::string nativeExecutable;		//!< built play-process executable
	std::string nativeTarget;			//!< the module's CMake target name
	//! last compile-on-Play build outcome, KEPT after the session reverts to
	//! edit mode (clearRemoteState/endPlaySession leave it alone) so an agent
	//! can read whether the module compiled - a failed build stays in edit mode
	//! and launches nothing, and the reason must survive for the control port's
	//! get_state (build_status/build_target/build_errors). None until the first
	//! native Play; Building while a build runs; Ok/Failed once it finishes.
	//! Reset to None at the start of every startPlay.
	enum class BuildOutcome { None, Building, Ok, Failed };
	BuildOutcome buildOutcome = BuildOutcome::None;
	std::string buildStatusTarget;		//!< the module target the outcome is for
	std::string buildErrorLog;			//!< accumulated [build] error-line tail
	//! remote state streamed by the player
	std::string remoteScenePath;
	bool helloReceived = false;
	bool hierarchyReceived = false;
	bool remoteLogSeen = false;		//!< at least one remote log line arrived
	Orkige::StringVector remoteHierarchy;
	//! parent id per remote object ("" = root), parallel to remoteHierarchy;
	//! EMPTY VECTOR when the player predates the tree extension (protocol v1
	//! without the additive "parents" list) - the panel then renders flat
	Orkige::StringVector remoteParents;
	//! activeSelf flag per remote object ("1"/"0"), parallel to
	//! remoteHierarchy (same additive-extension caveat)
	Orkige::StringVector remoteActive;
	//! objects whose ScriptComponent reported a failure (script_error
	//! messages, deduped per object per session): feeds the RED Console
	//! line, the toolbar warning marker and the remote hierarchy tint;
	//! cleared on Stop / a new session (clearRemoteState)
	std::set<std::string> scriptErrorIds;
	//! Lua hot-reload watcher: poll <projectRoot>/scripts for edits
	//! (~4 Hz) while playing and send MSG_RELOAD_SCRIPT (reload-ALL v1) to the
	//! running player on any change. DESKTOP play only - the exported player
	//! never watches files, the trigger lives here in the editor. Armed lazily
	//! (first poll records the baseline; reset by clearRemoteState).
	std::chrono::steady_clock::time_point lastScriptCheck;
	long long scriptsNewestMtime = 0;	//!< newest scripts/*.lua write time seen (file_time count)
	bool scriptsWatchArmed = false;		//!< false = next poll only records the baseline
	std::string remoteSelectedId;
	std::string stateObjectId;					//!< object of the latest object_state
	Orkige::StringVector stateComponents;		//!< its component type names
	std::map<std::string, std::string> stateProperties;	//!< "<Comp>.<prop>" -> value
	//! reflection metadata parallel to stateProperties: the
	//! ordered "<Comp>.<prop>" keys and, per key, the PropertyKind int, the
	//! widget hint (enum "label=value,..." table / asset-kind) and the
	//! read-only flag - so the remote Inspector renders a TYPED widget per
	//! property off the ONE registry. EMPTY when the player predates the
	//! metadata lists (the panel then falls back to a read-only value dump).
	Orkige::StringVector statePropKeys;
	std::map<std::string, int> statePropKind;
	std::map<std::string, std::string> statePropHint;
	std::set<std::string> statePropReadonly;
	//! running-game screenshot (MSG_SCREENSHOT / MSG_SCREENSHOT_SAVED): the
	//! path last CONFIRMED written by the player, its ok flag and any error.
	//! screenshotSeq increments on every confirmation so a poller can tell a
	//! fresh capture from a stale one; all reset by clearRemoteState.
	std::string lastScreenshotPath;
	std::string lastScreenshotError;
	bool lastScreenshotOk = false;
	unsigned int screenshotSeq = 0;
	//! running-game trace recording (MSG_RECORD_START/STOP / MSG_RECORD_SAVED):
	//! recordingActive is set the moment a record request goes out and cleared
	//! on the player's confirmation; lastRecordPath/Ok/Error hold the last
	//! written .jsonl trace and recordSeq increments per confirmation so a
	//! poller tells a fresh save from a stale one. All reset by clearRemoteState.
	bool recordingActive = false;
	std::string lastRecordPath;
	std::string lastRecordError;
	bool lastRecordOk = false;
	unsigned int recordSeq = 0;
	//! running-game memory metrics (MSG_STATS): the latest resident set size
	//! and the session peak, both in bytes. -1 = no reading yet (the player
	//! has not streamed one, or its platform cannot query memory) - surfaced
	//! as n/a. Reset by clearRemoteState.
	long long remoteMemRss = -1;
	long long remoteMemRssPeak = -1;
	//! running-game window size + safe-area insets (MSG_STATS), in pixels;
	//! -1 = not reported yet. The MCP get_safe_area verb surfaces these so an
	//! agent can assert the HUD sits inside the notch-safe box. Reset by
	//! clearRemoteState.
	long long remoteWindowW = -1;
	long long remoteWindowH = -1;
	long long remoteSafeLeft = -1;
	long long remoteSafeTop = -1;
	long long remoteSafeRight = -1;
	long long remoteSafeBottom = -1;
	//! running-game gui widget layout (MSG_UI_LAYOUT): one entry per widget,
	//! parallel ids/rects. The MCP get_ui_layout verb serves these.
	struct RemoteWidgetRect
	{
		std::string id;
		long long left = 0;
		long long top = 0;
		long long width = 0;
		long long height = 0;
		bool visible = true;
		bool enabled = true;	//!< interactive (false = dimmed / input-inert)
		bool modal = false;		//!< part of an active modal (scrim / dialog)
	};
	std::vector<RemoteWidgetRect> remoteUiLayout;
	//! the running game's screen router state (MSG_UI_LAYOUT): the current (top)
	//! screen name and the space-joined bottom-to-top stack path. Empty when the
	//! game uses no screen stack. Served by get_ui_layout. Reset by clearRemoteState.
	std::string remoteScreenCurrent;
	std::string remoteScreenStack;
	//! running-game streamed music (MSG_STATS): one entry per track. The MCP
	//! get_state verb folds these into structuredContent so an agent sees what
	//! is playing, its playhead and its effective gain. Reset by clearRemoteState.
	struct RemoteMusicTrack
	{
		std::string id;
		std::string file;
		bool playing = false;
		float positionSec = 0.0f;
		float durationSec = 0.0f;
		float baseGain = 1.0f;
		float groupVolume = 1.0f;
		float effectiveGain = 1.0f;
		bool loop = true;
	};
	std::vector<RemoteMusicTrack> remoteMusic;
	//! timing
	std::chrono::steady_clock::time_point launchStart;
	std::chrono::steady_clock::time_point lastConnectAttempt;
	std::chrono::steady_clock::time_point stopRequestTime;

	bool isActive() const { return this->mode != Mode::Edit; }
};

//! lower-case name of a play session's mode (edit/building/launching/playing/
//! paused/stopping) - the MCP control port reports play state as this string
inline const char* playSessionModeName(PlaySession const& session)
{
	switch (session.mode)
	{
	case PlaySession::Mode::Edit:		return "edit";
	case PlaySession::Mode::Building:	return "building";
	case PlaySession::Mode::Launching:	return "launching";
	case PlaySession::Mode::Playing:	return "playing";
	case PlaySession::Mode::Paused:		return "paused";
	case PlaySession::Mode::Stopping:	return "stopping";
	}
	return "edit";
}

//! lower-case name of the last compile-on-Play build outcome (none/building/
//! ok/failed) - the MCP control port reports it in get_state so an agent can
//! tell whether a native-module project compiled before it launched
inline const char* playSessionBuildName(PlaySession const& session)
{
	switch (session.buildOutcome)
	{
	case PlaySession::BuildOutcome::None:		return "none";
	case PlaySession::BuildOutcome::Building:	return "building";
	case PlaySession::BuildOutcome::Ok:			return "ok";
	case PlaySession::BuildOutcome::Failed:		return "failed";
	}
	return "none";
}

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
	int& exitCode);

//! vector-of-strings convenience wrapper around runProcessCaptured
bool runProcessCaptured(std::vector<std::string> const& args,
	std::string& output, int& exitCode);

//--- Android play targets (any desktop host with an SDK) -------------------

//! package + activity the player APK installs as (tools/player/android/)
const char* const PLAY_ANDROID_PACKAGE = "com.orkitec.orkigeplayer";
const char* const PLAY_ANDROID_ACTIVITY =
	"com.orkitec.orkigeplayer/.OrkigeActivity";
//! the temp scene's name inside the app files dir (delivered via adb)
const char* const PLAY_ANDROID_SCENE_NAME = "orkige_play.oscene";

//! adb from ANDROID_HOME (default: the per-user SDK), PATH as last resort
std::string adbPath();

//! a connected Android device or emulator (Play deployment target)
struct AndroidDevice
{
	std::string serial;
	std::string label;
};

//! @brief connected Android devices/emulators via 'adb devices -l' -
//! see EditorDeviceTargets.cpp
std::vector<AndroidDevice> listAdbDevices();

//! true when the given adb device has the player APK installed
bool androidPlayerInstalled(std::string const& serial);

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
//! devices available' - see EditorDeviceTargets.cpp
std::vector<SimulatorDevice> listSimulators();

//! is the simulator currently Booted? (false also when it is not in the
//! available-device list at all)
bool simulatorIsBooted(std::string const& udid);

//! true when OrkigePlayer.app is installed on the (BOOTED) simulator
bool simulatorPlayerInstalled(std::string const& udid);

//! is the INSTALLED player app at least as new as the locally built one?
bool simulatorPlayerUpToDate(std::string const& udid);

//! @brief is a codesigning identity available? (iOS hardware deploys are
//! gated on one - see EditorDeviceTargets.cpp)
bool hasCodesignIdentity();

//! @brief is a provisioning profile configured? A signed device install needs
//! both an identity AND a profile bound to the app id + device; the profile
//! path comes from ORKIGE_IOS_PROVISIONING_PROFILE (machine-local, never
//! committed - see Docs/ios-signing.md).
bool hasProvisioningProfile();

//! @brief is iOS device signing fully configured (identity AND profile)? The
//! hardware play targets stay enumerated-but-gated until this is true.
bool isIosSigningConfigured();

//! a physically connected iOS device (enumerated, not yet deployable)
struct IosHardwareDevice
{
	std::string name;
	std::string udid;
};

//! @brief connected iOS hardware via 'xcrun devicectl list devices'
std::vector<IosHardwareDevice> listIosHardwareDevices();

//! @brief install a signed .app on a USB iOS device (devicectl); on success
//! 'bundleId' receives the installed app's identifier. See EditorDeviceTargets.cpp
bool iosHardwareInstallApp(std::string const& udid, std::string const& appPath,
	std::string& bundleId, std::string& error);

//! @brief launch an installed app on a USB iOS device (devicectl). The game
//! runs standalone (no live debug link - a USB device has no dependency-free
//! TCP tunnel; see Docs/ios-signing.md). See EditorDeviceTargets.cpp
bool iosHardwareLaunchApp(std::string const& udid, std::string const& bundleId,
	std::string& error);
#endif // __APPLE__

//--- play session (EditorPlaySession.cpp) ----------------------------------

//! parse exactly count whitespace-separated floats; false on any junk
bool parsePlayFloats(std::string const& text, float* out, int count);

//! format count floats space-separated (the set_property wire format)
std::string formatPlayFloats(const float* values, int count);

//! forget everything streamed by the (previous) player
void clearRemoteState(PlaySession& session);

//! @brief tear the session down (reap/kill the player, drop the link,
//! delete the temp scene) and revert to edit mode - the single exit path
//! for Stop, crash detection and editor shutdown
void endPlaySession(PlaySession& session, std::string const& reason);

//! @brief Play: save the current scene to a temp file and hand it to the
//! picked play target (desktop player/native module build/simulator/adb) -
//! see EditorPlaySession.cpp
bool startPlay(PlaySession& session,
	Orkige::GameObjectManager& gameObjectManager,
	Orkige::Project const& project);

//! Stop: ask the player to quit (the grace timeout kill happens in
//! updatePlaySession)
void requestStopPlay(PlaySession& session);

//! select a remote object (sends select so the player streams its state)
void selectRemoteObject(PlaySession& session, std::string const& id);

//! toggle a remote object's own active flag (sends set_active; the player
//! answers with a fresh hierarchy so the tree reflects the change)
void setRemoteObjectActive(PlaySession& session, std::string const& id,
	bool active);

//! @brief Lua hot-reload: tell the running player to recompile-and-
//! swap its scripts (MSG_RELOAD_SCRIPT, reload-ALL). Sent automatically by the
//! scripts/ watcher in updatePlaySession and by the toolbar "Reload Scripts"
//! button. Optimistically clears scriptErrorIds - the player re-pushes
//! script_error only if a reload actually failed.
void reloadRemoteScripts(PlaySession& session, EditorConsole& console);

//! @brief write a live component property on the RUNNING game (MSG_SET_PROPERTY,
//! the reflected setter on the player - takes effect immediately, not undoable
//! and NOT an edit-world change). A no-op when no player is connected.
void setRemoteObjectProperty(PlaySession& session, std::string const& id,
	std::string const& component, std::string const& property,
	std::string const& value);

//! @brief change a console variable on the RUNNING game (MSG_SET_CVAR). A no-op
//! when no player is connected.
void setRemoteCvar(PlaySession& session, std::string const& name,
	std::string const& value);

//! @brief ask the RUNNING game to screenshot its next rendered frame to path
//! (MSG_SCREENSHOT). The player answers with MSG_SCREENSHOT_SAVED, which
//! updatePlaySession records in lastScreenshotPath/Ok/Error + screenshotSeq. A
//! no-op when no player is connected. Desktop play only - the path lives on the
//! player's filesystem, shared with the editor there.
void requestRemoteScreenshot(PlaySession& session, std::string const& path);

//! @brief ask the RUNNING game to record a .jsonl flight-recorder trace to path
//! (MSG_RECORD_START), sampling the world every everyNth frame for up to
//! maxSeconds wall-clock, optionally narrowed to the objects allowlist
//! (comma-separated ids/names, "" = all). The player answers with
//! MSG_RECORD_SAVED, which updatePlaySession records in lastRecordPath/Ok/Error
//! + recordSeq. A no-op when no player is connected. Desktop play only - the
//! path lives on the player's filesystem.
void requestRemoteRecord(PlaySession& session, std::string const& path,
	float maxSeconds, unsigned int everyNth, std::string const& objects);
//! @brief stop an in-progress trace early (MSG_RECORD_STOP): the player writes
//! what it captured and answers with MSG_RECORD_SAVED. A no-op when no player
//! is connected.
void stopRemoteRecord(PlaySession& session);

//! per-frame pump: connection progress, protocol messages, build/process
//! supervision, crash detection
void updatePlaySession(PlaySession& session, EditorConsole& console);

// The offscreen scene render target: the editor's scene camera renders into a
// facade RenderTexture (the old manual TU_RENDERTARGET pattern moved
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

// (re)size the scene RTT: first call creates it, later calls
// resize-by-recreate behind the facade - see EditorScenePanel.cpp
void createSceneRenderTexture(SceneRenderTarget& target, int width, int height);

// place the scene camera on its orbit sphere around the orbit target
// (the position math lives in EditorCamera.h, shared with the fly mode)
void applyOrbitCamera(EditorState const& state,
	optr<Orkige::RenderNode> const& cameraNode);

// 2D editor mode: point the editor's OWN camera straight down -Z at
// the XY plane and switch it to orthographic - the orbit "distance" drives the
// ortho half-extent (zoom). Independent of any scene CameraComponent; see
// EditorScenePanel.cpp
void apply2DCamera(EditorState const& state,
	optr<Orkige::RenderCamera> const& camera,
	optr<Orkige::RenderNode> const& cameraNode);

// the ground-plane reference grid, all facade (the returned mesh handle
// must stay alive with the node - RAII); see EditorScenePanel.cpp
optr<Orkige::MeshInstance> createEditorGrid(Orkige::RenderWorld* world,
	optr<Orkige::RenderNode> const& gridNode);

// F: frame the selected object - retarget the orbit to the object's world
// bounds centre and fit the orbit distance to its bounding radius
void frameSelectedObject(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera);

// double-click focus (select + frame): select the object AND frame it - the same
// orbit retarget/refit the F shortcut does. The edittest drives this.
void focusObjectFromDoubleClick(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, std::string const& id);

// ModelComponent does not serialize material tweaks (yet), so re-apply the
// unlit vertex-colour render state to every model after a scene load
void applyUnlitFixToLoadedModels(Orkige::EditorCore& core);

// viewport click-picking: cast a camera ray through the click point and
// select the nearest hit that belongs to a GameObject - see
// EditorScenePanel.cpp for the full rules
bool pickObjectAtCursor(Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera,
	float normalizedX, float normalizedY, bool additive = false);

// select a GameObject by driving the Scene panel's pick path (the demo/test
// hooks use this) - see EditorScenePanel.cpp
bool pickGameObjectThroughScenePanel(Orkige::EditorCore& core,
	Orkige::GameObjectManager& gameObjectManager,
	optr<Orkige::RenderCamera> const& camera, std::string const& id);

// marquee (rubber-band) box select: the ids of every object with a
// TransformComponent whose projected screen bounds intersect the marquee box
// (render-target pixels within the Scene image rect). The Scene panel's mouse
// path AND the selfcheck drive this - see EditorScenePanel.cpp
Orkige::StringVector objectsInMarquee(Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, Orkige::ScreenRect const& marquee);

// apply a marquee result to the selection set: replace it, or (extend) union the
// hits onto the current selection - see EditorScenePanel.cpp
void applyMarqueeSelection(Orkige::EditorCore& core,
	Orkige::StringVector const& hits, bool extend);

//--- scene/project document operations (EditorDocument.cpp) -----------------

// File > New Scene: clear all GameObjects
void newScene(EditorState& state, Orkige::EditorCore& core);

bool saveSceneToPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path);

bool openSceneFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path);

// "open a project, not a scene" - see EditorDocument.cpp
bool openProjectFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path);

// File > Close Project: back to loose-scene mode
void closeProject(EditorState& state, Orkige::EditorCore& core);

// File > New Project...: create the skeleton in the chosen folder + open it
bool newProjectAtPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& folder);

//! @brief register the project's assets/ directory AND each of its
//! subdirectories as its OWN flat resource location in the project group, so a
//! texture/mesh in a subfolder resolves by BARE name. A single recursive
//! registration indexes subfolder files by their sub-path on the next backend,
//! so bare-name loads miss there; per-directory flat locations resolve
//! identically on both flavors. Idempotent (remove-then-add per directory), so
//! it doubles as the re-index after a browser op creates a folder or moves a
//! file into a subfolder. Missing assets/ (or an unloaded project) is a no-op;
//! scenes/ is a separate single flat location the callers register.
void registerProjectAssetLocations(std::string const& assetsDirectory);

//! the directory File > Import Mesh copies into (an open project ROOTS the
//! import, otherwise the historical loose-scene rule applies)
std::string meshImportDestination(EditorState const& state);

bool importMeshFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& sourcePath);

//! @brief the generic asset import (Finder drop of a non-mesh file, the Asset
//! browser): copy sourcePath into the import destination (meshImportDestination
//! - the open project's assets/, else the loose-scene media dir), register that
//! directory as a resource location and mint the AssetDatabase sidecar (project
//! mode) so the copied texture/script/prefab/scene resolves by name at once.
//! NOT undoable (a filesystem side effect, like the mesh import). Returns the
//! destination path ("" on failure; error, if given, receives the reason).
std::string importAssetFile(EditorState& state, std::string const& sourcePath,
	std::string* error = nullptr);

// GameObject > Create Prefab / Hierarchy context menu: write the selection's
// subtree as "<assets>/<rootId>.oprefab" (stable .orkmeta id included) and
// convert it into a prefab instance (undoable); on an instance root it
// re-makes (overwrites) the instance's own prefab file instead
bool createPrefabFromSelection(EditorState& state, Orkige::EditorCore& core);

// Prefab > Apply / Revert on the selected prefab instance root. Apply writes
// the instance's current state (per-child overrides baked in) back to its
// .oprefab so every instance picks it up on reload (NOT undoable - a fs write).
// Revert drops the instance's overrides + suppressions to the pristine prefab
// (undoable). Both resolve the prefab file through the open project's root and
// log refusals to the Console.
bool applyPrefabOverrides(EditorState& state, Orkige::EditorCore& core);
bool revertPrefabInstance(EditorState& state, Orkige::EditorCore& core);

//! @brief Project > "Add Scene to Level Sequence": append the CURRENT scene to
//! the open project's levels.olevels (created if missing; the manifest "levels"
//! Setting is minted when absent). The display name is the scene's file stem
//! and par is read from the scene's LevelComponent (0 when none). Refused (and
//! logged to the Console) when there is no open project, the scene is unsaved or
//! lies outside the project root, or it is already in the sequence. A filesystem
//! side effect, NOT undoable - the same policy as prefab Apply.
bool addCurrentSceneToLevels(EditorState& state, Orkige::EditorCore& core);

//--- console Lua REPL (EditorConsole.cpp) ----------------------------------

// run the console input line: a set/get/find/reset line routes to the cvar
// command grammar (and a `set` during Play is sent to the running player as
// MSG_SET_CVAR so it tunes the live game), everything else goes through the
// ScriptRuntime seam - in a no-scripting build the latter reports the honest
// "scripting disabled" error instead of not existing
void runLuaConsoleInput(EditorState& state, PlaySession& session);

//--- native file dialogs (EditorFileDialogs.cpp) ----------------------------

// the directory scene dialogs default into: the open project ROOTS them
// (scenes/), loose-scene mode keeps the historical sample scene dir
std::string defaultSceneDirectory(EditorState const& state);

// File > Open/Save As/Import Mesh via SDL3's ASYNC native file dialogs; the
// callback deposits outcomes in a mailbox, dispatchFileDialogResults consumes
// them once per frame on the main thread - see EditorFileDialogs.cpp
void requestFileDialog(EditorState& state, SDL_Window* window,
	Orkige::FileDialogAction action);

// once per frame on the main thread: act on whatever the dialog callback
// deposited - the ONLY place dialog outcomes touch the scene/editor state
void dispatchFileDialogResults(EditorState& state, Orkige::EditorCore& core);

// modifier label for the menu shortcut column (the handler accepts both
// Super and Ctrl either way)
#ifdef __APPLE__
#define ORKIGE_EDITOR_MOD_LABEL "Cmd"
#else
#define ORKIGE_EDITOR_MOD_LABEL "Ctrl"
#endif

//--- panels, menus, toolbar, shortcuts --------------------------------------

// inline rename in the Hierarchy (F2 / context menu) - EditorHierarchyPanel.cpp
void startRenameSelected(EditorState& state, Orkige::EditorCore& core);

// every quit path funnels through here: unsaved changes raise the confirm
// modal instead of silently dropping the scene - EditorMenus.cpp
void requestQuit(EditorState& state, Orkige::EditorCore& core);

// the floating View Settings window (on mac the native menu opens it)
void drawViewSettingsWindow(EditorState& state, ViewSettings& viewSettings,
	optr<Orkige::RenderCamera> const& sceneCamera);

// the in-window ImGui menu bar (NOT drawn on macOS - MacMenu.mm mirrors it)
void drawMainMenuBar(EditorState& state, Orkige::EditorCore& core,
	ViewSettings& viewSettings, optr<Orkige::RenderCamera> const& sceneCamera,
	SDL_Window* window);

// the editor's modal popups (About, Scene Path, Unsaved Changes) - drawn
// every frame INDEPENDENTLY of the menu bar
void drawEditorModals(EditorState& state, Orkige::EditorCore& core);

// the play toolbar strip (Play/Pause/Step/Stop + target picker); returns the
// height the dockspace below must leave free - EditorToolbar.cpp
float drawToolbar(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core);

// fullscreen dockspace + first-run DockBuilder layout - EditorMenus.cpp.
// contentScale is the live UI scale: a persisted layout saved at a different
// scale is rebuilt from the ratio-based default rather than restored verbatim
// (absolute-pixel node sizes would otherwise mis-proportion the panels).
void drawDockspace(EditorState& state, float toolbarHeight,
	ViewSettings& viewSettings, float contentScale);

// the Scene panel: RTT image, gizmos, picking, camera navigation -
// EditorScenePanel.cpp
void drawScenePanel(EditorState& state, Orkige::EditorCore& core,
	bool editMode, SceneRenderTarget& sceneTarget,
	optr<Orkige::RenderNode> const& cameraNode,
	ViewSettings& viewSettings, float contentScale,
	Orkige::ImGuiSDL3Input& imguiInput);

// the GUI Preview panel: a real gui screen (.oui) rendered at a simulated
// device context into an offscreen target, shown here - EditorGuiPreviewPanel.cpp
void drawGuiPreviewPanel(EditorState& state, OrkigeEditor::GuiPreviewStage& stage,
	Orkige::EditorCore& core, ViewSettings& viewSettings);

// the Scene Hierarchy panel (edit mode: EditorCore world; play mode: the
// remote hierarchy) - EditorHierarchyPanel.cpp
void drawHierarchyPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, optr<Orkige::RenderCamera> const& sceneCamera,
	bool* visible);

// the Inspector panel (edit mode: component editors; play mode: the remote
// object_state) - EditorInspectorPanel.cpp
void drawInspectorPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, bool* visible);

// the editor-wide keyboard map - EditorShortcuts.cpp documents the bindings
void handleEditorShortcuts(EditorState& state, Orkige::EditorCore& core,
	PlaySession& session, optr<Orkige::RenderCamera> const& sceneCamera,
	SDL_Window* window);

// the Stats panel (4 Hz windowed frame stats + rolling plot)
void drawStatsPanel(PlaySession const& play, bool* visible);

// the Console panel: Log tab (engine/editor/remote lines) + Lua REPL tab
// (the session lets a `set` cvar line during Play tune the running player)
void drawConsolePanel(EditorState& state, PlaySession& session,
	EditorConsole& console, bool* visible);

//--- asset browser (EditorAssetBrowserPanel.cpp) --------------------

// The Asset browser is the codebase's FIRST user of ImGui drag & drop across
// panels: it sets an "ORKIGE_ASSET" payload (an AssetDragDropPayload value -
// kind + absolute path) that the Scene and Hierarchy panels accept and turn
// into a scene object, or an "ORKIGE_ASSET_MULTI" payload (newline-joined
// paths) when a multi-selection is dragged. The AssetKind enum, the payload
// struct and both tags live in EditorAssetDnd.h (shared with the ImGui-only
// property-widget layer). The hierarchy re-parent drag uses a SEPARATE payload
// tag ("ORKIGE_HIERARCHY_OBJECT"), so the two never cross.

//! classify a file path by its extension (case-insensitive)
AssetKind classifyAsset(std::string const& path);

//! one enumerated Asset browser row: a project asset (or scene) plus whether
//! it carries a stable id (sidecar-less id-trackable assets render dimmed)
struct AssetBrowserItem
{
	std::string absolutePath;	//!< absolute filesystem path
	std::string relativePath;	//!< project-relative path ("assets/ball.png")
	AssetKind kind = AssetKind::Unknown;
	bool isFolder = false;		//!< a directory (search results carry folders too)
	bool hasId = false;			//!< an AssetDatabase id resolves for it
	bool dimmed = false;		//!< sidecar-less id-trackable asset (assets/, scripts/)
	long long mtime = 0;		//!< last-write time (keys the thumbnail cache)
};

//! @brief enumerate the open project's assets/, scripts/ and scenes/ (a live
//! RECURSIVE filesystem walk cross-referenced against the AssetDatabase for
//! ids), sorted by project-relative path. Kept for the flat listing / the
//! selfcheck's whole-project assertions; the v2 panel browses folders through
//! enumerateAssetFolder instead.
std::vector<AssetBrowserItem> enumerateProjectAssets(
	Orkige::Project const& project);

//! @brief one folder-browser directory listing (Asset browser v2): the
//! immediate SUBFOLDERS and the FILES directly in a folder (non-recursive).
//! Files are classified + id-checked exactly like enumerateProjectAssets.
struct AssetFolderListing
{
	std::vector<std::string> subfolders;		//!< absolute subfolder paths, sorted
	std::vector<AssetBrowserItem> files;		//!< the folder's files, sorted
};

//! @brief enumerate ONE folder (non-recursive) under the open project: its
//! subfolders and its files. Sidecars (.orkmeta) are never listed; a file
//! under assets/ or scripts/ carrying no AssetDatabase id is dimmed (as in
//! the flat listing), scenes/ files are never dimmed. Outside a loaded
//! project (or a non-directory) the listing is empty.
AssetFolderListing enumerateAssetFolder(Orkige::Project const& project,
	std::string const& absoluteDir);

//! @brief load + cache a small ImGui thumbnail texture id for a texture asset
//! and return it (SYNCHRONOUS - the panel drives this off the paint path
//! through the per-frame queue; the selfcheck calls it directly). Returns 0
//! when the file is not a loadable image (the caller then draws a glyph icon).
//! The texture binds by BARE file name through the DrawLayer2D named-texture
//! path - the SAME mechanism the Scene RTT / font atlas use (see
//! ImGuiFacadeRenderer::textureIdForResource). The cache is keyed by ABSOLUTE
//! path + mtime; bounded (cleared wholesale past ASSET_THUMBNAIL_CACHE_MAX).
//! DEFERRED: rendered mesh/scene/prefab previews need a per-asset RTT render
//! pass and are a heavier follow-on - those kinds get a glyph type icon here.
ImTextureID assetThumbnailFor(EditorState& state,
	std::string const& absolutePath);
//! @brief drop the whole thumbnail cache, destroying every CPU-rasterized
//! upload it owns (vector shapes) first - the owner calls this at shutdown
//! while the render system is still up (manual textures are not resource-group
//! content, so nothing else frees them in time for strict backends)
void clearCachedThumbnails(AssetBrowserState& browser);
//! thumbnail cache cap (see AssetBrowserState::thumbnails)
const std::size_t ASSET_THUMBNAIL_CACHE_MAX = 512;

//! @brief navigate the content pane to a folder (absolute path), pushing the
//! current folder onto the back history and clearing the forward history - the
//! single seam every navigation (tree, breadcrumb, folder double-click, "..")
//! routes through so back/forward stay consistent.
void navigateTo(AssetBrowserState& browser, std::string const& dir);

//! @brief back/forward history navigation (the toolbar arrow buttons). A no-op
//! when the respective history is empty.
void navigateBack(AssetBrowserState& browser);
void navigateForward(AssetBrowserState& browser);

//! @brief search a folder subtree for files whose name contains query
//! (case-insensitive substring). Walks recursively when recursive is set, the
//! folder only otherwise; skips sidecars; applies kindMask (bit = 1u << kind,
//! 0 = every kind). Results carry the same kind/id/dimming as a folder listing
//! and are sorted by project-relative path (whose parent is the containing
//! folder). Empty outside a loaded project or for a non-directory root.
std::vector<AssetBrowserItem> searchAssets(Orkige::Project const& project,
	std::string const& rootDir, std::string const& query, bool recursive,
	unsigned int kindMask);

//! @brief drop selection entries whose relativePath is no longer present (an
//! asset moved/renamed/deleted out from under the folder listing). Keyed by
//! relativePath, so it is a set intersection; the anchor follows.
void pruneAssetSelection(AssetBrowserState& browser,
	std::set<std::string> const& presentRelativePaths);

//--- Asset browser v2 create + open helpers --------------------------------

//! @brief create a new subfolder "name" under dir (mkdir). Returns its
//! absolute path, or "" on failure / when it already exists.
std::string createFolderInDir(std::string const& dir, std::string const& name);

//! @brief write a minimal ScriptComponent .lua template (init/update/shutdown
//! stubs) as "name" into dir and, in project mode, mint its AssetDatabase
//! sidecar so it is id-tracked at once. Returns the script's absolute path,
//! or "" on failure. A missing ".lua" extension is appended.
std::string createScriptInDir(EditorState& state, std::string const& dir,
	std::string const& name);

//! @brief write an empty but valid .oscene (magic + current version + zero
//! objects, through the real SceneSerializer/XMLArchive) as "name" into dir.
//! Scenes are not id-tracked, so no sidecar is minted. Returns the scene's
//! absolute path, or "" on failure. A missing ".oscene" extension is appended.
std::string createSceneInDir(std::string const& dir, std::string const& name);

//! @brief the Create menu actions (New Folder / Script / Scene): make a
//! uniquely-named item in the browser's CURRENT folder, then REVEAL it - drop
//! any active search/type filter (so the fresh item is never hidden by a stale
//! filter), select it and open the inline rename so the user names it at once.
//! A folder no longer navigates INTO its own empty interior. On a filesystem
//! failure a notice is posted to the browser's status footer (the op is
//! otherwise silent). Returns the new item's absolute path, or "" on failure.
std::string createFolderAndReveal(EditorState& state);
std::string createScriptAndReveal(EditorState& state);
std::string createSceneAndReveal(EditorState& state);

//! @brief the file:// URL SDL_OpenURL opens for a local absolute path (RFC
//! 8089; the path is percent-encoded, '/' kept). Exposed so the selfcheck can
//! assert the composed URL WITHOUT spawning a GUI app. On Windows a drive
//! path becomes "file:///C:/..." (backslashes normalised).
std::string fileUrlForPath(std::string const& absolutePath);

//! @brief open a file with the OS default application. Routes through
//! SDL_OpenURL on a file:// URL - the tidiest cross-platform path: SDL maps it
//! to `open` (macOS), `xdg-open` (Linux) and ShellExecute/`start` (Windows).
//! Used for scripts/images/other non-engine files the browser cannot
//! instantiate (Reveal-in-Finder stays a direct `open -R`, SDL has no reveal).
void openWithDefaultApp(std::string const& absolutePath);

//! short human label for an AssetKind ("mesh"/"texture"/"script"/"scene"/
//! "prefab"/"file")
const char* assetKindLabel(AssetKind kind);

//! @brief instantiate/open a project asset in the current scene (the browser's
//! "Instantiate" context item and a single-item Scene/Hierarchy drop): mesh ->
//! CreateObjectCommand, texture -> CreateSpriteObjectCommand, prefab ->
//! CreatePrefabInstanceCommand, scene -> openSceneFromPath. Scripts are not
//! instantiable on their own (logged). Placement is the origin for v1.
void instantiateAssetIntoScene(EditorState& state, Orkige::EditorCore& core,
	AssetKind kind, std::string const& absolutePath);

//! @brief instantiate a batch of assets (absolute paths, kind classified per
//! path) into the current scene as ONE undo step: the instantiable kinds
//! (mesh/texture/prefab) collapse into a single CompositeCommand; a lone .oscene
//! opens instead (scenes/scripts are skipped inside a multi-drop). The multi-
//! item drop path (Scene/Hierarchy accepting ORKIGE_ASSET_MULTI) routes here.
void instantiateAssetsIntoScene(EditorState& state, Orkige::EditorCore& core,
	std::vector<std::string> const& absolutePaths);

//! @brief if an ImGui drag-drop target is currently active, accept an
//! ORKIGE_ASSET or ORKIGE_ASSET_MULTI payload and instantiate its asset(s) as
//! one undo step (called by the Scene + Hierarchy panels right after the item
//! that is the drop target)
void handleAssetDropTarget(EditorState& state, Orkige::EditorCore& core);

//! @brief the single id-tracked TEXTURE currently selected in the Asset browser
//! (empty/multi selection or a non-texture -> false): fills its absolute
//! sidecar path and its asset id. The seam behind the Inspector's Texture
//! Import Settings section and applyTextureImportEdit.
bool selectedBrowserTextureMeta(EditorState& state, std::string& metaFilePath,
	std::string& assetId);

//! @brief write the given import settings onto the single selected texture's
//! sidecar (its id preserved), the Inspector "Texture Import Settings" Apply.
//! A filesystem side effect, NOT undoable (the sidecar is edited in place);
//! already-loaded sprites re-sample the texture on its next (re)load. False
//! when no single id-tracked texture is selected or the write fails.
bool applyTextureImportEdit(EditorState& state,
	Orkige::TextureImport const& texture);

//--- Asset browser operations (filesystem side effects, NOT undoable) -------
// These operate on asset files directly; the stable asset id survives (the
// sidecar travels with the asset) and scene references self-heal on the next
// load, so rename/move/duplicate need no confirm - only delete does.

//! @brief rename one asset in place (same folder), keeping its extension when
//! the new name omits one. An id-tracked file (assets/, scripts/) moves
//! through the AssetDatabase so its id is preserved; a scenes/ or sidecar-less
//! file is renamed directly (its sidecar too, when present). Follows the
//! selection key. Returns false on a bad name / an existing target / an IO
//! failure.
bool renameAssetEntry(EditorState& state, AssetBrowserItem const& item,
	std::string const& newFileName);

//! @brief move assets (absolute paths, files and/or folders) into destDir
//! (absolute). id-tracked files go through the AssetDatabase (id preserved);
//! others are renamed directly (+ sidecar when present). A folder moved into
//! its own descendant, or a name that already exists in the destination, is
//! refused. Follows the selection + thumbnail keys. Returns the count moved.
int moveAssetsIntoFolder(EditorState& state,
	std::vector<std::string> const& absolutePaths, std::string const& destDir);

//! @brief duplicate assets (absolute paths) next to themselves. An id-tracked
//! file goes through AssetDatabase::duplicateAsset (a FRESH id, the import
//! block carried over); a scenes/ or sidecar-less file is copied with a unique
//! "<stem> Copy" name. The copies become the new selection. Returns the count.
int duplicateAssetEntries(EditorState& state,
	std::vector<std::string> const& absolutePaths);

//! @brief delete assets AND folders (absolute paths): files drop their sidecar
//! too, folders go recursively, then ONE AssetDatabase refresh prunes the
//! stale id-map entries. Clears the affected selection + thumbnail keys.
//! Returns the count deleted. The confirm modal is UI sugar around this.
int deleteAssetEntries(EditorState& state,
	std::vector<std::string> const& absolutePaths);

//! the Asset browser panel (v3): a content browser over the open project - a
//! folder TREE left and, right, a size-slider-scaled content grid (large
//! thumbnail tiles down to compact list rows) with a back/forward/breadcrumb/
//! search/filter-chip toolbar. Textures preview with real thumbnails (loaded
//! off the paint path via a budgeted queue), other kinds draw glyph type
//! icons. Multi-selection (ctrl/shift/box/keyboard) drives the asset ops -
//! the ORKIGE_ASSET drag source, Instantiate/Open, Reveal, Rename (F2),
//! Duplicate (Cmd/Ctrl+D), Copy Path/Asset ID and confirm-Delete - and
//! sidecar-less assets render dimmed - EditorAssetBrowserPanel.cpp
void drawAssetBrowserPanel(EditorState& state, Orkige::EditorCore& core,
	bool* visible);

//--- Tile Palette (EditorTilePalettePanel.cpp) ------------------------------

//! @brief arm a paint asset for grid painting (the palette click AND the
//! test/MCP seam): classify the path (prefab / texture / .oshape), derive its
//! project-relative ref + asset id, and for a PREFAB probe its local ids /
//! edge-wall children / root TileComponent; a bare texture/shape arms a
//! BARE-tile paint (its own thumbnail is the ghost). Stores it in
//! state.tilePalette and switches the active tool to Paint. Passing "" disarms
//! (tool back to Translate). False when the path is not a paintable asset (or a
//! prefab probe fails).
bool paletteArmAsset(EditorState& state, Orkige::EditorCore& core,
	std::string const& absolutePath);

//! @brief build the EditorPaintDesc the armed asset + paint options describe.
//! PREFAB: the open edges become suppressed wall-child locals + the openEdges
//! bitmask stamp (a TileComponent added when the prefab root lacks one).
//! TEXTURE/SHAPE: a bare sprite/shape tile carrying the source-asset id. The
//! tags field is parsed comma-separated for either kind. The Scene panel paint
//! path and the MCP paint verb both feed this to EditorCore::paintTileAtCell.
Orkige::EditorPaintDesc paletteMakePaintDesc(TilePaletteState const& palette);

//! @brief the Tile Palette panel: lists the open project's prefabs (click to
//! arm), the armed prefab's edge-open toggles + tags field, a 2D-mode hint and
//! the resolved paint grid. Project-only content (like the Asset browser).
void drawTilePalettePanel(EditorState& state, Orkige::EditorCore& core,
	bool* visible);

//! @brief the Scene panel's 2D grid-paint input (left = paint, right/Alt+left =
//! erase, drag = one undo step), plus the hovered-cell highlight overlay. A
//! no-op unless the Paint tool is armed and the view is in 2D editor mode.
//! Returns true when it consumed the mouse (the pick/camera paths stand down).
bool handleScenePaintInput(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, bool editMode, ViewSettings const& viewSettings);

//! @brief the Scene viewport's drop target (see the block comment above the
//! declaration further up) - a prefab drop paints its cell in 2D mode, else the
//! generic instantiate-at-origin runs. Returns true when it consumed a prefab
//! drop into the grid.
bool handleSceneDropTarget(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, bool editor2D);

#endif // ORKIGE_EDITORAPP_H_09072026
