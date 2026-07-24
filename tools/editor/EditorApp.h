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
#include "EditorPanelRegistry.h"
#include "EditorTheme.h"
#include "FileDialog.h"
#include "MarqueeSelection.h"
#include "PlayMirror.h"
#include "ScriptBreakpoints.h"
#include "SyntaxHighlight.h"

#include <core_debug/DebugMacros.h>	// tagged oDebug* diagnostics (SDL_Log policy)
#include <core_debugnet/DebugClient.h>
#include <core_debugnet/HttpServer.h>	// Play-in-Browser static server
#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_util/MaterialAsset.h>
#include <core_util/String.h>
#include <engine_base/EngineLog.h>
#include <engine_render/MeshInstance.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderTexture.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using Orkige::optr;
using Orkige::woptr;

namespace Orkige
{
	class ImGuiFacadeRenderer;
	class ImGuiSDL3Input;
	class RenderWorld;
	class GameObjectManager;	//!< the play mirror drives the editor world's nodes
	class EditorScriptHost;	//!< editor-tool host (EditorScriptHost.h)
}

namespace OrkigeEditor
{
	class GuiPreviewStage;	//!< the GUI Preview stage (GuiPreviewStage.h)
	class AnimationPreviewStage;	//!< the animation preview stage
	class MeshPreviewStage;	//!< the 3D mesh/material preview stage (MeshPreviewStage.h)
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
	//! part of the Console's line selection (click / shift-range / drag);
	//! lives on the line so it survives the store's half-trim
	bool selected = false;
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
	//! the selection anchor for shift-range/drag selection: an index into
	//! `lines`, -1 when nothing is anchored. Adjusted by the half-trim.
	int selectionAnchor = -1;

	void addLine(ConsoleLevel level, std::string const& text)
	{
		std::lock_guard<std::mutex> lock(this->mutex);
		if (this->lines.size() >= MAX_LINES)
		{
			this->lines.erase(this->lines.begin(),
				this->lines.begin() + MAX_LINES / 2);
			this->selectionAnchor = this->selectionAnchor < 0 ? -1
				: this->selectionAnchor - static_cast<int>(MAX_LINES / 2);
			if (this->selectionAnchor < 0)
			{
				this->selectionAnchor = -1;
			}
		}
		this->lines.push_back({ level, text });
		this->scrollToBottom = true;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(this->mutex);
		this->lines.clear();
		this->selectionAnchor = -1;
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
	//! @brief Play-in-Browser continuation: when set the successful "web"
	//! export is followed by serving the artifact directory + opening the
	//! default browser (the frame loop runs that step - see
	//! browserArtifactReady). A plain Build-menu web export leaves it false.
	bool deployBrowser = false;
	//! set by updateExportJob when a deployBrowser export succeeded; the
	//! frame loop consumes it (serve + open browser + Console URL line)
	bool browserArtifactReady = false;
	bool isActive() const { return this->process != nullptr; }
};

//! @brief launch the exporter for the open project (async; false when it
//! cannot start) - see EditorExport.cpp
bool startExport(ExportJob& job, Orkige::Project const& project,
	std::string const& platform, EditorConsole& console);

//! @brief per-frame pump: stream the exporter's output into the Console as
//! "[export]" lines and report the outcome on exit
void updateExportJob(ExportJob& job, EditorConsole& console);

//--- Play in Browser static server (EditorBrowserServe.cpp) -----------------

struct PlaySession;	// the debug-upgrade destination (defined below)

//! @brief the loopback static-file server behind Play in Browser: a second
//! instance of the core_debugnet HttpServer (the MCP endpoint keeps its own,
//! opt-in instance) serving ONE exported web build directory. The listener
//! starts lazily on the first browser play and stays for the editor's
//! lifetime (stable port), but files serve ONLY while a browser play
//! session is live - Stop ends the serve, so a reloading tab cannot restart
//! the game; it and requests for a PREVIOUS export's files answer with the
//! honest 404. Binds 127.0.0.1 only.
struct BrowserServe
{
	Orkige::HttpServer server;
	std::string docRoot;			//!< the served export directory ("" = idle)
	bool isServing() const { return !this->docRoot.empty(); }
};

//! @brief serve docRoot (starting the listener on its first use) and return
//! the page URL through outUrl; false + outError when the socket setup fails
bool browserServeStart(BrowserServe& serve, std::string const& docRoot,
	std::string& outUrl, std::string& outError);

//! @brief per-frame pump (accept/read/respond; never blocks); a no-op while
//! idle. The session is the debug-upgrade destination: a WebSocket upgrade
//! request on the serve port is accepted ONLY while that session is a
//! browser play waiting for its page (Launching + onBrowser, link down) -
//! the socket then lands in session.client via adoptWebSocket. Any other
//! upgrade attempt (a second tab, a page after the session ended) is
//! refused with a 409 and that page runs standalone.
void browserServeUpdate(BrowserServe& serve, PlaySession& session);

//! @brief the Content-Type a browser needs per served-file extension
//! (the browser-play static server); an unknown extension degrades to the
//! honest binary default
std::string staticContentTypeFor(std::string const& path);

//! @brief resolve a request target to a real file inside docRoot, or ""
//! when it escapes/misses - the canonical-path jail every editor-hosted
//! static file server uses (the control server's project-file discipline)
std::string staticResolveServedFile(std::string const& docRoot,
	std::string const& target);

//--- Help ---------------------------------------------------------------------

//! @brief Help > "Orkige Help": the published documentation site - the docs
//! corpus portal plus the engine API reference, regenerated and deployed by
//! CI on every main push. The editor simply opens this URL in the default
//! browser (a network connection is required: a distributed editor carries
//! no repository and no python toolchain, so there is nothing to generate
//! or serve locally). Automated runs never open a browser (gAutomatedRun) -
//! the editor_help_portal ctest asserts the menu wiring and that gate,
//! never the network.
constexpr char const* HELP_PORTAL_URL = "https://orkige.orkitec.com/help/";

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
	//! Tile Palette panel (project-only; arms a tile for 2D grid painting).
	//! Closed by default - it auto-opens when the Scene enters 2D editor mode
	//! (and never auto-closes); a saved layout in orkige_editor_view.ini wins.
	bool showTilePalettePanel = false;
	//! GUI Preview panel (project-only; renders a .oui screen at a simulated
	//! device context into an offscreen target - the collaborative UI loop)
	bool showGuiPreviewPanel = false;
	//! Debug panel (the script debugger's call-stack + locals + transport).
	//! Closed by default - it auto-opens/focuses on a debugger break-hit; a
	//! saved layout in orkige_editor_view.ini wins. The code editor itself is
	//! no longer a single panel: each open script/text file is its OWN docked
	//! window (see drawScriptDocuments), so it carries no visibility flag.
	bool showDebugPanel = false;
	//! GUI Preview language axis: the language tag the preview resolves `@key`
	//! captions in ("" = the project's source language). Persisted so the tab
	//! reopens on the last previewed language.
	std::string guiPreviewLanguage;
	//! Inspector: show rotation (Quat) properties as human-readable Euler X/Y/Z
	//! degrees (default) vs the raw quaternion x/y/z/w. Display-only; the tiny
	//! per-row toggle flips it globally so the choice sticks.
	bool rotationAsEuler = true;
	//! Debug panel: "Break on Errors" - when armed, a running Lua error pauses
	//! the game AT the error (the debugger jumps to the erroring file:line and
	//! shows the crash's real stack + locals). Persisted here and pushed to a
	//! running player on connect + on any change (like the breakpoint set).
	//! Default off = exactly today's behavior (the instance disables + reports).
	bool breakOnScriptErrors = false;
	//! @brief which file extensions double-click into the EMBEDDED code editor
	//! (space/comma-separated, dot-prefixed; editable in View Settings). Kinds
	//! with a richer double-click (.oscene/.oprefab/.oanim/.oui) ignore this -
	//! their behavior is fixed; the external editor stays reachable from the
	//! Inspector/context menu for everything.
	std::string internalEditorExtensions =
		".lua .ogui .omat .oshape .oactions .olayers .olevels .xlf .txt .md "
		".json .jsonl .c .cpp .cc .cxx .h .hpp .hh .inl .mm .cmake .py .glsl";
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
	//! external code-editor command template (View Settings): {file}/{line}
	//! placeholders, e.g. "code -g {file}:{line}". Empty = autodetect an
	//! installed CLI editor on PATH, else fall back to the platform file opener
	//! (no line jump). Drives the Console file:line clicks + the "Open in
	//! External Editor" context actions.
	std::string externalEditor;
	//! editor chrome appearance (View > Theme): System follows the OS, or the
	//! user pins Dark/Light. Applied at boot and on every change.
	Orkige::EditorThemeMode themeMode = Orkige::EditorThemeMode::System;
	//! the content scale the persisted dock layout was saved at. A layout is a
	//! set of absolute-pixel node sizes; restoring one saved at a different
	//! scale (e.g. moved from a 1x to a 2x display) mis-proportions the panels,
	//! so drawDockspace rebuilds the ratio-based default when this differs from
	//! the live scale. 0 = unknown (older ini / never saved).
	float layoutContentScale = 0.0f;
	//! layout-defaults schema version stamped into the ini. A one-time
	//! migration runs when a persisted ini predates the current defaults (an
	//! older or absent stamp): it reconciles the saved layout with a defaults
	//! rework the ini never saw, then bumps the stamp so it runs once. 0 = a
	//! pre-migration ini (panels/docks from before the panel rework).
	static constexpr int CURRENT_LAYOUT_VERSION = 1;
	int layoutVersion = 0;
	//! most-recently-used scene paths (newest first) for File > Open Recent;
	//! filled by every successful open/save, capped at MAX_RECENT_SCENES
	std::vector<std::string> recentScenes;
	//! most-recently-used project roots (newest first) for File > Open
	//! Recent Project; filled by every successful project open/create
	std::vector<std::string> recentProjects;
	std::string path;				//!< backing file ("" = don't persist)

	void load();
	void save() const;

	//! One-time reconciliation of a loaded ini that predates the current layout
	//! defaults (layoutVersion < CURRENT_LAYOUT_VERSION): applies the panel
	//! defaults the ini never saw and stamps the current version. Returns true
	//! when a Tile Palette re-dock is pending (the caller defers it to a frame
	//! where the Assets node exists). A no-op on an already-current ini.
	bool migrateLayoutDefaults();

	//! record a successfully opened/saved scene path for File > Open Recent:
	//! move-to-front, dedupe, cap at MAX_RECENT_SCENES (caller persists)
	void addRecentScene(std::string const& scenePath);

	//! record a successfully opened/created project root for File > Open
	//! Recent Project (same move-to-front/dedupe/cap rule as the scenes)
	void addRecentProject(std::string const& projectRoot);

	//! restore the factory camera/display values (View > Reset View Settings);
	//! panel visibility is NOT touched - that belongs to Reset Layout
	void resetCameraAndDisplayDefaults();

	//! restore each panel to its registry default visibility (Reset Layout):
	//! the default-visible panels re-open, the default-closed ones (Tile
	//! Palette, GUI Preview) stay closed so a reset matches a fresh launch
	void resetPanelVisibility();
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

// The live PlaySession (owned by main). Global so a scene SAVE (saveSceneToPath,
// reachable via Cmd+S even during play) can restore the mirror's authored
// transforms before serializing - the play mirror moves render nodes, so a save
// mid-play would otherwise write mirrored poses. Null-safe: "" outside main's
// wiring. See revertPlayMirror.
struct PlaySession; // defined below
extern PlaySession* gPlaySession;

// the ImGui-on-facade renderer (owned by main; global so drawScenePanel can
// register the scene RTT for ImGui::Image without threading it through every
// draw* signature - same pattern as gViewSettings)
extern Orkige::ImGuiFacadeRenderer* gImGuiRenderer;
// false during automated runs: the scripted tests open temp scenes/projects
// through the same functions a user does, and those must never pollute the
// interactive Open Recent lists (or become the reopened "last project")
extern bool gRecordRecents;

// true during automated runs (the automatedRun env probe): the timed autosave
// stands down entirely and an open with a stale autosave auto-discards silently
// instead of raising the recovery modal (a scripted test never blocks on one)
extern bool gAutomatedRun;

//! record a scene path in the Open Recent list and persist it
void recordRecentScene(std::string const& scenePath);

//! record a project root in the Open Recent Project list and persist it
void recordRecentProject(std::string const& projectRoot);

//--- external editor (open a file at a line) -------------------------------

//! @brief launch the user's external code editor on `absolutePath` at `line`
//! (line <= 0 = just open the file), DETACHED so it never blocks the editor.
//! The resolution order lives in ExternalEditor.h (setting -> autodetected CLI
//! on PATH -> platform file opener); the outcome is reported via SDL_Log, which
//! the Console mirrors. See ExternalEditorLaunch.cpp.
void openInExternalEditor(std::string const& absolutePath, int line,
	ViewSettings const& settings);

//! @brief does a bare executable name resolve on PATH? (the autodetect probe)
bool editorExecutableOnPath(std::string const& name);

//! @brief absolute path for a project file reference (a bare script filename, a
//! project-relative console path); absolute inputs pass through unchanged. Tries
//! the ref as-is, then under scripts/ and assets/, taking the first that exists.
std::string resolveProjectFilePath(Orkige::Project const& project,
	std::string const& ref);

//! @brief is this a file the "Open in External Editor" action offers? (the text
//! asset kinds: .lua/.oui/.omat and the other engine text formats)
bool isTextEditableAsset(std::string const& path);

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
	//! GPU-rendered thumbnail bake queue: .glb/.omat previews cannot decode on
	//! the CPU like a .oshape - they render through a MeshPreviewStage offscreen
	//! target, so they are staged + captured across frames by the browser's
	//! dedicated baker (one at a time, off the ImGui paint path). thumbBakeQueue
	//! holds the absolute paths still to bake; thumbBakeInFlight is the one
	//! currently staged ("" = idle) and thumbBakeStagedFrame the loop frame it
	//! was staged (a short residency delay before the readback).
	std::deque<std::string> thumbBakeQueue;
	std::string thumbBakeInFlight;
	int thumbBakeStagedFrame = -1;
	//! the panel held keyboard focus while drawing (gates its local shortcut
	//! handling, mirrors scenePanelFocused / hierarchyFocused)
	bool focused = false;
	//! the Inspector's Texture Import Settings edit cache: the settings being
	//! edited, the sidecar path they were read from and the file time they
	//! were read at (a changed path OR a changed file re-reads from disk, so
	//! the section always reflects the selected texture - including a sidecar
	//! rewritten from outside, e.g. an MCP write_project_file)
	Orkige::TextureImport editImport;
	std::string editImportPath;
	long long editImportFileTime = 0;
	//! the Inspector's Animation Import Settings edit cache (the texture cache's
	//! cooked-pair sibling): the cook settings being edited, the source sidecar
	//! they were read from and whether that sidecar carried a <cook> record yet
	Orkige::CookSettings editCook;
	std::string editCookMetaPath;
	bool editCookHasRecord = false;
	//! the import revision the settings cache was read at: a cook that
	//! happened elsewhere (watcher, scan, MCP) re-reads the recorded fields
	unsigned int editCookSeenRevision = 0;
	//! animation (re)cook revision: incremented by EVERY animation cook (import,
	//! Reimport, the project-open drift scan, the play watcher's re-cook, MCP) so
	//! the Inspector's animation preview can key its stale-rig guard on the
	//! RECORDED source hash without per-frame sidecar IO - it re-reads the
	//! sidecar only when this moved (and reloads the stage when the recorded
	//! hash differs from the one it loaded at)
	unsigned int animImportsRevision = 1;
	//! the preview guard's cache: the revision it last checked at and the
	//! freshness key (recorded sourceHash, or the artifact mtime for a
	//! source-less .oanim) the loaded stage corresponds to
	unsigned int animPreviewSeenRevision = 0;
	std::string animPreviewFreshnessKey;
	//! the Inspector's text-preview cache: the project-relative file whose
	//! highlighted read-only content is shown, its lines split for the clipper,
	//! the chosen highlight family and the size-cap bookkeeping. A changed path
	//! re-reads from disk (bounded at TEXT_PREVIEW_CAP_BYTES).
	std::string textPreviewPath;
	std::vector<std::string> textPreviewLines;
	Orkige::SyntaxFormat textPreviewFormat = Orkige::SyntaxFormat::PlainText;
	bool textPreviewTruncated = false;
	std::size_t textPreviewTotalBytes = 0;
	static constexpr std::size_t TEXT_PREVIEW_CAP_BYTES = 256u * 1024u;
	//! the Inspector texture-preview platform selector: "" (Base/desktop),
	//! "android" or "ios" - the resolved settings drive the "as imported" size
	std::string previewPlatform;

	//! the Inspector `.oshape` preview cache: the tessellated fill rasterised
	//! ONCE per file (keyed absolute-path + mtime) and uploaded as a named
	//! texture, plus the vertex/triangle counts shown as a status line and the
	//! "View source" toggle (switches the section to the raw-text view)
	std::string shapePreviewKey;		//!< "<abs>|<mtime>" the upload was built for
	std::string shapePreviewUpload;		//!< createTexture2D name ("" = none)
	int shapePreviewVertices = 0;
	int shapePreviewTriangles = 0;
	bool shapeShowSource = false;		//!< View source: raw text instead of the fill
	std::string shapeSourceText;		//!< cached raw .oshape text for the source view

	//! the Inspector `.oui` preview thumbnail cache: a .oui renders through the
	//! GPU GuiPreviewStage (not a CPU raster), so the Inspector cannot bake it
	//! mid-frame - it sets ouiPreviewRequest to the absolute path and the main
	//! loop bakes it once post-render (readback -> named texture), keyed by
	//! "<abs>|<mtime>". "Open Preview" opens the full panel on that screen.
	std::string ouiPreviewKey;			//!< "<abs>|<mtime>" the upload was built for
	std::string ouiPreviewUpload;		//!< createTexture2D name ("" = none)
	std::string ouiPreviewRequest;		//!< abs .oui the loop should bake ("" = none)

	//! the Inspector `.omat` editor cache: the parsed material being edited,
	//! the file it was read from (a changed path re-reads), the scene material
	//! ref (bare name - the create-or-update key the scene shares), the on-disk
	//! text at read time (Revert + a dirty check) and a transient status line
	Orkige::MaterialAsset::ParsedMaterial editMaterial;
	std::string editMaterialPath;		//!< absolute .omat path ("" = none)
	std::string editMaterialRef;		//!< bare file name (the scene material ref)
	std::string editMaterialOriginal;	//!< on-disk text at read time
	std::string editMaterialStatus;		//!< transient apply/parse message
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

//! @brief one open prefab edit stage: the live scene is snapshotted to a temp
//! .oscene and swapped OUT of the one GameObjectManager, the .oprefab is
//! instantiated in its place (root id = file stem) and edited with the
//! unchanged EditorCore toolset; closing loads the snapshot back - the open
//! scene's instances rebuild from the edited file with their per-instance
//! overrides re-applied by the scene loader (no dedicated refresh code).
//! Everything the pop needs to restore rides here. Held as a vector-of-one
//! (EditorState::prefabEditStack) so a nested-prefab v2 generalizes to a
//! stack without reshaping the state; v1 never holds more than one entry
//! (the .oprefab format refuses nesting in both directions).
struct PrefabEditContext
{
	std::string prefabPath;			//!< absolute .oprefab being edited
	std::string prefabRef;			//!< project-relative display ref ("" loose)
	std::string rootId;				//!< the stage root id (= the file stem)
	std::string snapshotPath;		//!< temp .oscene holding the swapped-out scene
	//! the scene session state restored on close
	std::string stashedScenePath;
	bool stashedSceneDirty = false;
	Orkige::StringVector stashedSelection;
	Orkige::EditorCameraState stashedCamera;
	bool stashedEditor2D = false;
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
	//! Console file:line quick-peek popup: a transient read-only view of ~20
	//! source lines around a clicked reference. openPeekPopup requests the popup
	//! open on the next draw; peekTitle is its "path:line" caption, peekLines the
	//! window text, peekFirstLine the 1-based number of peekLines[0] and
	//! peekTargetLine the highlighted line. Consumed by drawConsolePanel.
	bool openPeekPopup = false;
	std::string peekTitle;
	std::vector<std::string> peekLines;
	int peekFirstLine = 0;
	int peekTargetLine = 0;
	//! first-frame guard for the DockBuilder default layout
	bool dockLayoutChecked = false;
	//! pending one-time layout migration: re-dock the Tile Palette into the
	//! Assets (bottom) node so its 2D auto-open lands there, not a pre-rework
	//! slot a restored ini remembers. Set when an older-stamped ini loads,
	//! consumed once the Assets node exists (@see ViewSettings::layoutVersion).
	bool migratePaletteDock = false;
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
	bool handPanActive = false;	//!< Hand-tool / space left-drag pan in progress
	//! first-frame delta swallow per drag kind (see CameraDragGate)
	Orkige::CameraDragGate flyLookGate;
	Orkige::CameraDragGate orbitDragGate;
	Orkige::CameraDragGate panDragGate;
	Orkige::CameraDragGate handPanDragGate;
	//! deferred popup opens (menus - native or ImGui - set these; the modals
	//! are drawn once per frame by drawEditorModals)
	bool openAboutPopup = false;
	bool openQuitConfirmPopup = false;
	//! deferred "Autosave Recovery" confirm: openSceneFromPath raises it when the
	//! opened scene has a NEWER ".autosave" sibling (a crash left recoverable
	//! work); the modal offers Restore / Discard. autosaveRecoveryScenePath is
	//! the real scene path whose autosave is on offer. Automated runs never raise
	//! it (they auto-discard silently - see gAutomatedRun).
	bool openAutosaveRecoveryPopup = false;
	std::string autosaveRecoveryScenePath;
	//! the open prefab edit stage (empty = normal scene editing). A vector so
	//! a nested-prefab v2 becomes a stack; v1 holds at most ONE entry - see
	//! PrefabEditContext and the EditorDocument.cpp free functions.
	std::vector<PrefabEditContext> prefabEditStack;
	//! deferred "Close Prefab" confirm (raised when the prefab is dirty);
	//! prefabCloseQuitIntent makes a resolved close continue into the normal
	//! quit flow (requestQuit while a prefab stage is open routes here first)
	bool openPrefabClosePopup = false;
	bool prefabCloseQuitIntent = false;
	//! one-shot: frame the selected object next frame (the frame loop owns
	//! the scene camera, so panel-less callers - opening a prefab stage -
	//! request the framing instead of doing it; consumed by the main loop)
	bool frameSelectedRequested = false;
	//! Build menu request ("macos"/"ios-simulator"/"ios"/"android"; "" = none) -
	//! menus (native or ImGui) set it, the frame loop starts the export
	std::string requestedExport;
	//! Tools menu request: the stable name of an editor script tool to run once
	//! ("" = none) - menus (native or ImGui) set it, the frame loop runs it
	//! through state.editorScripts (kept off the menu's own callback so the run
	//! happens at a clean point in the loop, like requestedExport)
	std::string requestedEditorScript;
	//! Asset-browser request to open a project-relative `.oui` in GUI Preview.
	std::string requestedGuiPreviewAsset;
	//! the editor-tool host (owned by main): discovers *.editor.lua tools and
	//! runs one on demand through the verb handler. Non-null once main wires it;
	//! the menus read its tool list and EditorDocument rescans it on project
	//! open/close.
	Orkige::EditorScriptHost* editorScripts = nullptr;
	//! iOS-device deploy request (Play on a connected iPhone): the toolbar sets
	//! the UDID + label, the frame loop runs an "ios" export whose success
	//! installs + launches on the device (see the deploy fields on ExportJob).
	//! "" = none. Requires a loaded project + iOS signing configured; the frame
	//! loop reports honestly to the Console when those are missing.
	std::string requestedIosDeviceDeployUdid;
	std::string requestedIosDeviceDeployLabel;
	//! Play-in-Browser request: the toolbar (or the control port's play verb
	//! with target "browser") sets it, the frame loop runs a "web" export
	//! whose success serves the artifact directory over a loopback HttpServer
	//! and opens the default browser at it (see the deployBrowser field on
	//! ExportJob and browserServe* in EditorBrowserServe.cpp). Like Play on
	//! iOS hardware this is a deploy-and-run, never a live debug session - a
	//! page cannot host the debug socket. Requires a loaded project.
	bool requestedBrowserPlay = false;
	//! Help > "Orkige Help" clicked (native mac menu or the ImGui menu bar):
	//! the frame loop opens the published documentation site in the default
	//! browser (see HELP_PORTAL_URL; never on automated runs)
	bool requestedHelpPortal = false;
	//! the last browser-play outcome, for the toolbar/Console and the control
	//! port's get_state (browser_play_url/browser_play_status): "" until the
	//! first browser play; "exporting" while the web export runs; "serving"
	//! with the URL set once the page is up; "failed" when the export failed
	std::string browserPlayStatus;
	std::string browserPlayUrl;
	//! floating View Settings window (grid/gizmo/camera-feel sliders); on mac
	//! the native View > View Settings... opens it since the ImGui menu bar
	//! that used to host the sliders is not drawn there
	bool showViewSettingsWindow = false;
	//! Build > Project Settings...: the manifest-Settings editor (orientation)
	bool showProjectSettingsWindow = false;
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
	//! @brief the per-project script breakpoint store (the Script panel's
	//! gutter, the MCP set/clear/list_breakpoint verbs and the play session's
	//! MSG_DEBUG_BREAKPOINTS push all share it). Attached/detached by the
	//! project open/close path; persists under <project>/.orkige/breakpoints.
	Orkige::ScriptBreakpointStore breakpoints;
	//! @brief one-shot document-open request (consumed by drawScriptDocuments):
	//! open/focus this file - absolute or project-relative path - and, when
	//! scriptOpenLine > 0, scroll to that 1-based line. Raised by the Asset
	//! browser double-click, the debugger break-hit and the error markers.
	std::string scriptOpenRequest;
	int scriptOpenLine = 0;
	//! a code-editor document window held keyboard focus while drawing (gates
	//! the Cmd/Ctrl+S save routing, mirrors scenePanelFocused / hierarchyFocused)
	bool scriptPanelFocused = false;
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
	//! @brief the editor's OWN scene world (set at startPlay/beginBrowserPlay),
	//! whose render nodes the play mirror drives so the Scene view shows the
	//! running game's object motion live and is restored EXACTLY on stop. A
	//! borrowed pointer (the editor holds exactly one GameObjectManager for its
	//! whole life); null outside an active session.
	Orkige::GameObjectManager* editorWorld = nullptr;
	//! @brief the editor core (borrowed, set once at startup beside
	//! gPlaySession): the mirror-document swap needs its scene lifecycle
	//! (resetForScene, dirty flag, selection) - only ever used while a session
	//! is active (editorWorld set)
	Orkige::EditorCore* editorCore = nullptr;
	//! @brief MIRROR DOCUMENT state (a mid-play scene switch): the running
	//! game loaded another scene, so the editor's ONE world was swapped to a
	//! VIEW-ONLY load of that scene file (no undo, never dirty, edit
	//! affordances stay off as in every play session) and the AUTHORED
	//! document was serialized aside to authoredSnapshotPath first (after an
	//! exact mirror revert, so the snapshot holds authored truth). Stop/crash
	//! reload the snapshot - the document-swap extension of the v1
	//! exact-restore contract; a mid-play save writes the SNAPSHOT to the
	//! scene file, never the mirror content (saveSceneToPath's guard).
	bool mirrorDocument = false;
	std::string mirrorScenePath;		//!< absolute path of the mirror scene
	std::string mirrorSceneName;		//!< display name (project-relative)
	std::string authoredSnapshotPath;	//!< temp .oscene of the authored doc
	bool authoredSceneDirty = false;	//!< the authored doc's stashed dirty flag
	Orkige::StringVector authoredSelection;	//!< stashed selection (best-effort)
	//! a switch arrived but its scene file could not be resolved/loaded here
	//! (e.g. a path outside this project) - the mirror stays dark for the
	//! switched scene and spawn queries stand down until the next switch/stop
	bool mirrorSwapFailed = false;
	//! @brief the running-scene motion mirror: snapshots the authored transforms
	//! + visibility once, drives the editorWorld nodes from the streamed
	//! MSG_SCENE_TRANSFORMS / MSG_HIERARCHY, and restores them on session end.
	//! The edit DOCUMENT is never touched (render nodes only) - see PlayMirror.
	OrkigeEditor::PlayMirror mirror;
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
	//! bounded retry of the simulator BOOT step only (a transient CoreSimulator
	//! hiccup can fail an otherwise-healthy boot); 1 retry max, per play session
	int simBootRetries = 0;
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
	//! play target picked in the toolbar: the browser (WebGL). Play exports
	//! the project for the web (Util/orkige_export.py --platform web off
	//! the web-release tree), serves the artifact over a loopback
	//! HttpServer instance, opens the default browser at it - and then
	//! WAITS for the page to dial the debug link back in: the URL carries
	//! ?env.ORKIGE_DEBUG_CONNECT=127.0.0.1:<servePort>, the wasm runtime
	//! dials that endpoint (its socket emulation wraps the stream in a
	//! WebSocket) and the serve port's upgrade hands the socket to THIS
	//! session's DebugClient. From there it is a live session like desktop
	//! play - remote hierarchy/inspector, [remote] logs, pause/step. A page
	//! that never dials in (browser blocked, closed early) degrades
	//! honestly: the session times out back to edit mode and the tab runs
	//! standalone. Selectable once the web-release preset built the wasm
	//! player.
	bool browserTarget = false;
	//! the ACTIVE session is a browser page that dialed in (no process
	//! handle, no temp scene; Stop = MSG_QUIT + the socket close is the
	//! confirmation - the editor cannot close a tab, the page just ends)
	bool onBrowser = false;
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
	//! the last ~40 [build] lines REGARDLESS of classification - a cmake
	//! configure error spreads its message over unclassifiable continuation
	//! lines, and a failure diagnosed from CI logs needs the whole tail
	std::string buildOutputTail;
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
	//! the raw script-error texts of this session (script_error + [remote]
	//! error lines): the Script panel parses their "file:line" references
	//! into error markers on the open document. Bounded (newest kept);
	//! cleared on Stop / a new session (clearRemoteState).
	std::vector<std::string> scriptErrorMessages;
	//--- script debugger session state (MSG_DEBUG_*) ---
	//! paused at a script breakpoint/step (a MID-FRAME pause, visually and
	//! semantically distinct from the toolbar's frame-boundary Paused mode -
	//! the session mode stays Playing while broken)
	bool debugBroken = false;
	std::string debugBreakFile;		//!< paused script (project-relative)
	int debugBreakLine = 0;			//!< paused 1-based line
	//! the error text when THIS pause came from an uncaught script error
	//! (Break on Errors); "" for a breakpoint / step landing. Shown prominently
	//! in the Debug panel and returned as break_error over MCP get_debug_state.
	std::string debugBreakError;
	//! increments per received MSG_DEBUG_BREAK (a step landing raises a new
	//! one), so the panel can re-focus the hit line exactly once per pause
	unsigned int debugBreakSeq = 0;
	//! one captured stack frame of the held break (innermost first)
	struct DebugStackFrame
	{
		std::string source;
		int line = 0;
		std::string function;
	};
	std::vector<DebugStackFrame> debugStack;
	//! the panel's selected stack frame (locals shown for it; 0 = innermost)
	int debugSelectedFrame = 0;
	//! one variable row of a MSG_DEBUG_LOCALS reply
	struct DebugVariableRow
	{
		std::string name;
		std::string scope;
		std::string type;
		std::string value;
		bool expandable = false;
	};
	//! locals/expansion cache: "<frame>|<joined expand path>" -> the reply's
	//! rows. Filled by MSG_DEBUG_LOCALS replies; dropped whenever a new break
	//! arrives or the session resumes (the values are stale then).
	std::map<std::string, std::vector<DebugVariableRow>> debugLocalsCache;
	//! requests already in flight (same key) so panel redraws do not spam
	std::set<std::string> debugLocalsPending;
	//! increments per received MSG_DEBUG_LOCALS reply (the MCP get_locals
	//! poll freshness signal)
	unsigned int debugLocalsSeq = 0;
	//! the breakpoint-store revision last pushed to this player (0 = never;
	//! updatePlaySession re-sends on any change while the link is up)
	unsigned int sentBreakpointRevision = 0;
	//! the Break-on-Errors state last pushed to this player: -1 never sent,
	//! else 0/1. updatePlaySession pushes it on connect and re-sends whenever
	//! the persisted ViewSettings flag changes while the link is up.
	int sentBreakOnErrors = -1;
	//! Lua hot-reload watcher: poll <projectRoot>/scripts for edits
	//! (~4 Hz) while playing and send MSG_RELOAD_SCRIPT (reload-ALL v1) to the
	//! running player on any change. DESKTOP play only - the exported player
	//! never watches files, the trigger lives here in the editor. Armed lazily
	//! (first poll records the baseline; reset by clearRemoteState).
	std::chrono::steady_clock::time_point lastScriptCheck;
	long long scriptsNewestMtime = 0;	//!< newest scripts/*.lua write time seen (file_time count)
	bool scriptsWatchArmed = false;		//!< false = next poll only records the baseline
	//! .oui hot-reload watcher: rides the SAME poll/cadence/lifecycle as the
	//! scripts watcher (scriptsWatchArmed arms both). Per-file write times so a
	//! CHANGED layout hot-reloads just that screen (MSG_RELOAD_UI carries its
	//! name); keyed by the .oui basename (the name the game passes to
	//! GuiFactory::loadLayout). Reset by clearRemoteState.
	std::map<std::string, long long> uiFileMtimes;
	//! vector-animation hot-reload watcher: rides the SAME poll/cadence/
	//! lifecycle as the scripts/.oui watchers (scriptsWatchArmed arms all
	//! three). Two per-basename write-time maps: animSourceMtimes tracks Lottie
	//! .json sources that have a cooked sibling (a change re-cooks through the
	//! sidecar's recorded settings FIRST, then MSG_RELOAD_ANIM carries the
	//! artifact's basename - a cook failure reports and sends nothing);
	//! animFileMtimes tracks ORPHAN .oanim files only (agent-authored, no
	//! source pair - a change reloads directly; keeping paired artifacts out of
	//! this map is what stops a re-cook's own rewrite from double-firing).
	//! Reset by clearRemoteState.
	std::map<std::string, long long> animSourceMtimes;
	std::map<std::string, long long> animFileMtimes;
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
	//! running-game engine-level allocation counters (MSG_STATS): tracked
	//! allocation events at the engine's own seams, last frame + session peak
	//! (-1 = not reported yet), with the per-tag breakdown in parallel
	//! tags/counts lists. Served by get_state. Reset by clearRemoteState.
	long long remoteAllocPerFrame = -1;
	long long remoteAllocPeak = -1;
	std::vector<std::string> remoteAllocTags;
	std::vector<long long> remoteAllocCounts;
	//! running-game frame wall time in ms (MSG_STATS; -1 = not reported yet)
	double remoteFrameMs = -1.0;
	//! the running game's current named state (MSG_STATS, core_game/GameState
	//! via Lua game.setState); "" until the game sets one. Served by get_state.
	std::string remoteGameState;
	//! running-game CPU frame profile (MSG_PROFILE_DATA): the last streamed
	//! hierarchical scope snapshot, flattened depth-first. profileSeq counts
	//! received snapshots (0 = none yet) so get_profile can report freshness.
	//! Reset by clearRemoteState.
	struct RemoteProfileNode
	{
		std::string name;
		int depth = 0;
		long long calls = 0;
		double milliseconds = 0.0;
		double maxMilliseconds = 0.0;
	};
	std::vector<RemoteProfileNode> remoteProfile;
	double remoteProfileFrameMs = -1.0;
	unsigned int profileSeq = 0;
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

//! seconds the editor keeps re-connecting while the player engine boots -
//! generous because a first launch on a freshly booted simulator compiles
//! shaders and warms caches before the debug port answers
const int PLAY_CONNECT_TIMEOUT_SECONDS = 90;
//! seconds a shutdown simulator gets to boot (+ app install) before Play
//! gives up with an honest error. A hosted CI runner boots even a warm
//! simulator in 4-6 minutes (the widely-used simulator actions default to
//! 360s per boot attempt), and an overloaded runner has been observed to
//! stall per-xcrun calls by 30s+ and exceed 480s for a pre-warmed device;
//! the scripted boot test's outer deadlines are spaced above this, prep +
//! connect must stay inside them
const int PLAY_SIM_PREP_TIMEOUT_SECONDS = 900;
//! milliseconds between connect attempts while launching
const int PLAY_CONNECT_RETRY_MS = 250;
//! milliseconds a stopped player gets before it is killed
const int PLAY_STOP_GRACE_MS = 3000;
//! @brief seconds a browser play session waits for the served page to dial
//! the debug link in before it degrades to a standalone tab (edit mode,
//! serving continues). The page's first visit streams + compiles a
//! multi-megabyte wasm module before main() runs, and a hosted runner or a
//! GPU-starved machine under full test parallelism stretches that boot into
//! tens of seconds - the budget sits above that, while a browser that never
//! loads the page at all still resolves within one sitting. Scripted tests
//! shrink it through ORKIGE_BROWSER_CONNECT_TIMEOUT_MS (the degradation leg
//! must not wait a minute for the timeout it exists to verify).
const int BROWSER_PAGE_CONNECT_TIMEOUT_SECONDS = 60;

//! @brief run a command synchronously with captured stdout (used for the
//! short-lived simctl/adb/devicectl calls). False when the process cannot be
//! spawned; exitCode/output are only valid on true.
bool runProcessCaptured(const char* const* args, std::string& output,
	int& exitCode);

//! bounded variant for device probes: kills the child at timeoutMs and
//! returns false (a stalled cold simctl/devicectl must read as "no devices",
//! never hang its caller past a deadline)
bool runProcessCapturedTimeout(std::vector<std::string> const& args,
	std::string& output, int& exitCode, unsigned int timeoutMs);

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

//! @brief drive the editor world's render nodes from a MSG_SCENE_TRANSFORMS
//! delta (ids parallel to transform strings): the running-scene motion mirror.
//! Snapshots the authored poses on the first apply. A no-op without editorWorld.
void applyPlayMirrorTransforms(PlaySession& session,
	Orkige::StringVector const& ids, Orkige::StringVector const& transforms);

//! @brief drive the mirrored objects' visibility from the latest remote
//! hierarchy (effective activeInHierarchy). A no-op without editorWorld.
void applyPlayMirrorActive(PlaySession& session);

//! @brief restore the editor world to its AUTHORED transforms + visibility,
//! destroy every runtime-spawn mirror stand-in and drop the mirror snapshot -
//! the exact-restore path, called on session end (and before a manual save
//! while playing so the document stays authored). A no-op when nothing is
//! mirrored.
void revertPlayMirror(PlaySession& session);

//! @brief the running game switched scenes mid-play (MSG_SCENE_LOADED, or a
//! hello reporting a different scene): swap the editor's Scene view to a
//! VIEW-ONLY load of that scene file - the authored document is serialized
//! aside first (once per session, after an exact mirror revert) and restored
//! by endPlaySession. scenePath is the player-reported identity
//! (project-relative preferred); an unresolvable/unloadable file degrades
//! honestly: the authored document stays, the mirror goes dark for the
//! switched scene.
void handleRemoteSceneLoaded(PlaySession& session,
	std::string const& scenePath);

//! @brief undo the mirror-document swap: reload the AUTHORED document from
//! its snapshot (dirty flag + selection restored, snapshot file removed). A
//! no-op when no swap happened. Called by endPlaySession after the mirror
//! revert.
void restoreAuthoredDocument(PlaySession& session);

//! @brief ask the player to describe hierarchy ids the mirror cannot match
//! (runtime-spawned objects) and prune stand-ins whose ids vanished - called
//! on every received hierarchy. A no-op without a world / after a failed
//! mirror-document swap.
void updatePlayMirrorSpawns(PlaySession& session);

//! @brief materialize mirror stand-ins from a MSG_SCENE_SPAWNS reply
void applyPlayMirrorSpawns(PlaySession& session,
	Orkige::DebugMessage const& message);

//! @brief the mid-play save path while a mirror document holds the world:
//! copy the authored snapshot to targetPath (the honest "save the AUTHORED
//! scene" - the world holds the running scene's mirror content, which must
//! never reach disk). False when no snapshot exists or the copy failed.
bool saveAuthoredSnapshotTo(PlaySession& session,
	std::string const& targetPath);

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

//! @brief Play in Browser, the session half: the web export was served and
//! the page opened - enter Launching and WAIT for the page to dial the
//! debug link in (browserServeUpdate lands the upgrade in session.client).
//! The timeout back to a standalone tab lives in updatePlaySession
//! (BROWSER_PAGE_CONNECT_TIMEOUT_SECONDS).
void beginBrowserPlaySession(PlaySession& session,
	std::string const& projectRoot);

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

//! @brief .oui hot-reload: tell the running player to destroy-and-rebuild one
//! declarative screen (MSG_RELOAD_UI). @p ouiName is the .oui name the game
//! passed to GuiFactory::loadLayout. Sent automatically by the project-tree
//! .oui watcher in updatePlaySession and by the MCP reload_ui verb.
void reloadRemoteUi(PlaySession& session, EditorConsole& console,
	std::string const& ouiName);

//! @brief vector-animation hot-reload: tell the running player to re-read one
//! `.oanim` rig and rebuild its components (MSG_RELOAD_ANIM, parse-before-swap
//! on the player). @p animName is the rig's resource basename. Sent
//! automatically by the project-tree animation watcher in updatePlaySession
//! (which re-cooks a changed Lottie source first) and by the MCP reload_anim
//! verb.
void reloadRemoteAnim(PlaySession& session, EditorConsole& console,
	std::string const& animName);

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

//--- script debugger (the MSG_DEBUG_* family) --------------------------------

//! @brief push the CURRENT breakpoint set to the running player
//! (MSG_DEBUG_BREAKPOINTS full-list replace) and record the pushed revision.
//! A no-op when no player is connected. updatePlaySession calls it on connect
//! and on every store change; verbs/panels just mutate the store.
void sendDebugBreakpoints(EditorState& state, PlaySession& session);

//! @brief push the persisted "Break on Errors" state (gViewSettings) to the
//! running player (MSG_DEBUG_BREAK_ON_ERRORS) and record it in
//! session.sentBreakOnErrors. A no-op when no player is connected.
//! updatePlaySession calls it on connect and whenever the setting changes.
void sendDebugBreakOnErrors(PlaySession& session, bool armed);

//! @brief release the held script break (MSG_DEBUG_RESUME or one of the step
//! messages - pass the DebugProtocol MSG_DEBUG_* type). Optimistically clears
//! the broken flag (the player confirms with MSG_DEBUG_RESUMED; a landing
//! step raises a fresh MSG_DEBUG_BREAK). A no-op unless broken and connected.
void sendDebugCommand(PlaySession& session, Orkige::String const& messageType);

//! @brief BREAK ON NEXT STATEMENT: ask the running player to pause on the first
//! script line it executes next (MSG_DEBUG_BREAK_NEXT). Unlike sendDebugCommand
//! this fires while the session is RUNNING or frame-paused (not broken) - the
//! hit arrives asynchronously as MSG_DEBUG_BREAK, so nothing flips here. A no-op
//! unless connected, and never for a browser session (it cannot block).
void sendDebugBreakNext(PlaySession& session);

//! @brief request variables at a frame of the held break (MSG_DEBUG_LOCALS;
//! empty expandPath = the frame's locals/upvalues, else one nested table).
//! The reply lands in session.debugLocalsCache under "<frame>|<joined path>".
//! Deduped while a same-key request is in flight. A no-op unless broken.
void requestDebugLocals(PlaySession& session, int frameIndex,
	std::vector<std::string> const& expandPath);

//! the cache key requestDebugLocals / the reply handler / readers share
std::string debugLocalsKey(int frameIndex,
	std::vector<std::string> const& expandPath);

//! per-frame pump: connection progress, protocol messages, build/process
//! supervision, crash detection. @p state backs the hot-reload watcher's
//! animation re-cook (project resolution + the import revision); the session
//! itself stays the play-mode source of truth.
void updatePlaySession(EditorState& state, PlaySession& session,
	EditorConsole& console);

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

//! @brief write the timed autosave copy of the current world to the
//! ".autosave" sibling of state.currentScenePath (SceneSerializer::saveScene,
//! NOT the real file, dirty flag untouched). A no-op returning false for an
//! untitled scene (no path to sit next to). Called by the frame loop's autosave
//! tick; the crash-recovery counterpart to a manual save.
bool writeSceneAutosave(EditorState& state, Orkige::EditorCore& core);

//! @brief restore a recovered autosave into the world: load the ".autosave"
//! sibling of scenePath, keep currentScenePath = scenePath, mark the scene dirty
//! (the recovered state is newer than the file and unsaved) and remove the
//! autosave file. The Restore branch of the recovery modal. False on load
//! failure (the already-open scene is left in place).
bool restoreSceneAutosave(EditorState& state, Orkige::EditorCore& core,
	std::string const& scenePath);

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
//! A Lottie .json source cooks to .oanim with cookSettings when given (MCP
//! import_asset's optional cook params - they become the sidecar's recorded
//! intent), else with the destination sidecar's recorded settings (a re-import
//! keeps per-asset intent), else the cook defaults.
std::string importAssetFile(EditorState& state, std::string const& sourcePath,
	std::string* error = nullptr,
	Orkige::CookSettings const* cookSettings = nullptr);

//! @brief re-cook one imported animation pair IN PLACE: run the Lottie cook on
//! the project's kept source .json (project-relative or absolute path) with
//! the given settings (nullptr = the sidecar's recorded ones), mirror the cook
//! summary as [import] Console lines, record the fresh import inputs (source/
//! tool/settings hashes) on the source's sidecar and bump the animation-import
//! revision. The force behind the Asset browser's "Reimport", the play
//! watcher's source re-cook and the MCP reimport_asset verb. Returns the
//! produced artifact's ABSOLUTE path (.oanim, or .oshape when nothing
//! animates), or "" (+ *error).
std::string recookAnimationPair(EditorState& state,
	std::string const& sourcePath,
	Orkige::CookSettings const* settingsOverride = nullptr,
	std::string* error = nullptr);

//! @brief the project-open drift scan: for every id-tracked .json whose
//! sidecar carries a <cook> record, compare the recorded import inputs against
//! the CURRENT source bytes / cook script / settings and re-cook on any
//! mismatch (or a vanished artifact). Sidecars without a record - everything
//! imported before records existed, and plain data .json files - are left
//! alone by design: no record, no auto-re-cook. Per-pair failures report and
//! the scan continues.
void recookProjectAnimationsOnScan(EditorState& state);

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

//--- prefab edit mode (EditorDocument.cpp) ----------------------------------
// The isolation stage that edits a .oprefab ASSET in place of the scene: the
// ONE GameObjectManager world is swapped (scene -> temp snapshot -> prefab
// subtree) so every editor tool - commands, selection, inspector, gizmos -
// operates on the prefab unchanged. Undo history is scoped per context
// (resetForScene at push AND pop, the openSceneFromPath precedent). While a
// stage is open the scene/project lifecycle, Play, the paint tool and the
// prefab instance operations are refused (prefabEditBlocks - one central
// guard, honest Console lines). The free functions below are the whole
// surface; the MCP verbs wrap them 1:1.

//! is a prefab edit stage open
inline bool isPrefabEditActive(EditorState const& state)
{
	return !state.prefabEditStack.empty();
}

//! @brief central prefab-mode guard: true (and a Console line naming the
//! refused action) while a prefab stage is open. Callers bail on true.
bool prefabEditBlocks(EditorState const& state, char const* action);

//! @brief open a .oprefab for editing: snapshot the live scene to a temp
//! .oscene FIRST (the snapshot must predate any prefab change - the stored
//! overrides were diffed against the file the instances were instantiated
//! from), clear the world, instantiate the prefab as root "<file stem>" and
//! reset the undo history. Refused (Console line) while a stage is already
//! open, for a missing/corrupt/nested prefab (the scene is restored) or when
//! the snapshot cannot be written. Selects the root and requests framing.
bool openPrefabForEdit(EditorState& state, Orkige::EditorCore& core,
	std::string const& prefabPath);

//! @brief Hierarchy context menu "Open Prefab": resolve the selected prefab
//! instance root's .oprefab (the Apply/Revert resolution rules, refusals
//! logged) and open it for editing.
bool openSelectedInstancePrefab(EditorState& state, Orkige::EditorCore& core);

//! @brief Cmd/Ctrl+S in prefab mode / File > Save Prefab: write the stage
//! back to its .oprefab (asset id re-imported in project mode). Refused with
//! a Console line when the recorded root was deleted or OTHER root-level
//! objects exist (savePrefab writes ONE subtree - strays would be silently
//! lost; the message lists them, parent them under the root instead).
bool savePrefabEdit(EditorState& state, Orkige::EditorCore& core);

//! how closePrefabEdit resolves a dirty stage (the UI confirm modal picks;
//! automated runs and the MCP verb pass the policy explicitly - they never
//! see a modal)
enum class PrefabClosePolicy
{
	Save,		//!< save the prefab first; a REFUSED save cancels the close
	Discard		//!< drop unsaved stage edits (the file keeps its last save)
};

//! @brief close the prefab stage: optionally save (Save policy - a failed
//! save cancels the close, never a silent discard), then load the scene
//! snapshot back, restore the stashed scene path/dirty flag/selection/camera
//! and delete the temp file. The reopened scene's instances rebuild from the
//! edited .oprefab with their per-instance overrides re-applied - the scene
//! loader's normal merge, no extra code. Undo history resets (per-context
//! scope).
bool closePrefabEdit(EditorState& state, Orkige::EditorCore& core,
	PrefabClosePolicy policy);

//! @brief the UI close affordance (breadcrumb, File > Close Prefab): a dirty
//! stage raises the Save/Discard/Cancel confirm modal, a clean one closes
//! immediately. quitAfter makes the resolved close continue into requestQuit.
void requestClosePrefabEdit(EditorState& state, Orkige::EditorCore& core,
	bool quitAfter = false);

//! @brief the Cmd/Ctrl+S / File > Save routing: Save Prefab while a prefab
//! stage is open, otherwise save the scene (Save-As dialog when untitled).
void saveCurrentDocument(EditorState& state, Orkige::EditorCore& core,
	SDL_Window* window);

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

// the floating Project Settings window: the manifest Settings that shape an
// export (screen orientation) - reads/writes the open project + saves .orkproj
void drawProjectSettingsWindow(EditorState& state);

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

//! @brief the status footer strip pinned under the dockspace: the FIRST live
//! problem - the focused document's syntax error, else the newest streamed
//! script error of the play session - as one red clickable line that jumps to
//! its file:line; a quiet session shows the muted session state instead.
//! Returns the height the dockspace must leave free - EditorToolbar.cpp
float drawStatusFooter(EditorState& state, PlaySession& session);

// fullscreen dockspace + first-run DockBuilder layout - EditorMenus.cpp.
// contentScale is the live UI scale: a persisted layout saved at a different
// scale is rebuilt from the ratio-based default rather than restored verbatim
// (absolute-pixel node sizes would otherwise mis-proportion the panels).
// footerHeight = the status strip pinned at the BOTTOM of the work area.
void drawDockspace(EditorState& state, float toolbarHeight,
	ViewSettings& viewSettings, float contentScale, float footerHeight = 0.0f);

//! Put a newly opened preview into the Scene panel's dock node. Existing saved
//! docking wins, and the one-shot flag lets users undock it afterwards.
void dockPreviewBesideSceneOnce(const char* panelWindowName, bool& attempted);

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

// the shared preview WIDGET body (clip dropdown, Play/Pause/Reset, time scrub,
// blend try-out, status line + the rasterised pose image), stage-backed so it
// carries no external UI state. The Inspector's asset-view animation section
// calls it (the standalone Animation Preview panel was retired); assumes
// stage.isLoaded(). Defined in AnimationPreviewPanel.cpp.
void drawAnimationPreviewBody(OrkigeEditor::AnimationPreviewStage& stage);

// the Scene Hierarchy panel (edit mode: EditorCore world; play mode: the
// remote hierarchy) - EditorHierarchyPanel.cpp
void drawHierarchyPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, optr<Orkige::RenderCamera> const& sceneCamera,
	bool* visible);

// the Inspector panel (edit mode: component editors; play mode: the remote
// object_state) - EditorInspectorPanel.cpp. The animation preview stage backs
// the asset-view's inline .oanim preview (the Inspector owns this preview; it
// shares the one stage's clock with the preview_animation MCP verb).
void drawInspectorPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, OrkigeEditor::AnimationPreviewStage& animStage,
	OrkigeEditor::MeshPreviewStage& meshPreview,
	bool* visible);

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

//--- Script document windows + Debug panel (EditorScriptPanel.cpp) -----------

// The embedded code editor is a set of TextEditor widgets (syntax highlight,
// engine-API completion, click-to-toggle breakpoint gutter over
// state.breakpoints for .lua) - but each open file is its OWN docked window
// (title = filename, dirty marker, stable ###path id) so multiple files read
// as sibling tabs in one dock node, like every other panel. Open-file state
// lives inside the TU; everything shared rides EditorState/PlaySession.
// Drawn unconditionally each frame: zero open files draws nothing.
void drawScriptDocuments(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, ViewSettings& viewSettings);

// The Debug panel: the script debugger's transport (Continue / Step In / Over
// / Out, FontAwesome glyphs), call-stack pane and locals/upvalues pane - a
// docked panel (bottom group, beside Console) that auto-opens/focuses on a
// break-hit. Idle when no break is held.
void drawDebugPanel(EditorState& state, PlaySession& session,
	ViewSettings& viewSettings, bool* visible);

//! @brief open a script/text file as a docked document window: absolute or
//! project-relative path; line > 0 scrolls/highlights that 1-based line. An
//! already-open file is focused instead of reopened. The request is consumed
//! on the next drawScriptDocuments pass. The Asset browser double-click, the
//! debugger break-hit and the error markers route through here. (viewSettings
//! is accepted for signature stability; document windows carry no panel flag.)
void scriptPanelOpenFile(EditorState& state, ViewSettings& viewSettings,
	std::string const& path, int line = 0);

//! drop every open document window (project close / switch - unsaved edits are
//! discarded after the confirm the caller runs; v1 asks nothing and logs)
void scriptPanelCloseAll();

//! any open document window with unsaved edits? (the quit-confirm surfaces it)
bool scriptPanelHasUnsavedEdits();

//! @brief Cmd/Ctrl+S routing: when a code-editor document window holds
//! keyboard focus, save THAT document (returns true - the caller skips the
//! scene save). saveCurrentDocument consults this so the one shortcut always
//! saves what the user is looking at, including through the native macOS menu.
bool scriptPanelSaveActiveIfFocused(EditorState& state);

//! @brief editor selfcheck hook: is at least one code-editor document window
//! open AND docked into @p sceneDockId (the shared script dock node lands
//! beside the Scene panel on first open)? Non-zero @p sceneDockId required.
bool scriptDocumentDockedWithNode(unsigned int sceneDockId);

//--- code-editor selfcheck seams (the dirty-close modal choreography) --------
// The scripted selfcheck drives the SAME functions the modal buttons call -
// everything short of the literal mouse click - so the queue semantics
// (close-all over several dirty documents asking one at a time) are asserted,
// not just hand-traced.

//! make the open document at `path` dirty via an undo-recorded edit (the
//! editor-buffer equivalent of typing); false when it is not open
bool scriptPanelTestDirtyDocument(std::string const& path,
	std::string const& text);

//! request Close All through the tab-action machinery (as the context menu does)
void scriptPanelTestCloseAll();

//! the absolute path of the document currently asking save/discard/cancel
//! ("" = no modal showing)
std::string scriptPanelTestConfirmPath();

//! resolve the showing confirm as the modal buttons would:
//! 0 = Save, 1 = Discard, 2 = Cancel. False when no modal is showing.
bool scriptPanelTestResolveConfirm(int choice);

//! how many document windows are open
std::size_t scriptPanelTestDocumentCount();

//! @brief the current live SYNTAX error among the open documents (the focused
//! one first), for the status footer: returns the message ("" = none) and
//! fills the file + 1-based line to jump to on click.
std::string scriptPanelActiveSyntaxError(std::string& outPath, int& outLine);

//! @brief the debug-shortcut dispatch (F5/F10/F11/Shift+F11 and the
//! Cmd/Ctrl-based alternates) - called from handleEditorShortcuts while a
//! break is held so the steps work wherever focus sits
void handleScriptDebugShortcuts(EditorState& state, PlaySession& session);

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
//! @brief bake at most one queued .glb/.omat thumbnail this frame into @p baker
//! (a MeshPreviewStage on its own instance slot), reading the target back into
//! an owned thumbnail texture. Driven from the main loop OUTSIDE the ImGui
//! frame; @p frameCount is the loop frame (a residency delay before readback).
void serviceThumbnailBakes(EditorState& state,
	OrkigeEditor::MeshPreviewStage& baker, int frameCount);
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

//! @brief disarm the paint tile if one is armed (a no-op otherwise). The
//! shared "you did something that isn't painting" exit: selecting a scene or
//! hierarchy object, clicking empty scene space, or selecting a browser asset
//! all call this so painting is easy to leave (@see paletteArmAsset).
void disarmPaintTileOnIntent(EditorState& state, Orkige::EditorCore& core);

//! @brief build the EditorPaintDesc the armed asset + paint options describe.
//! PREFAB: the open edges become suppressed wall-child locals + the openEdges
//! bitmask stamp (a TileComponent added when the prefab root lacks one).
//! TEXTURE/SHAPE: a bare sprite/shape tile carrying the source-asset id. The
//! tags field is parsed comma-separated for either kind. The Scene panel paint
//! path and the MCP paint verb both feed this to EditorCore::paintTileAtCell.
Orkige::EditorPaintDesc paletteMakePaintDesc(TilePaletteState const& palette);

//! @brief resolve the thumbnail texture a palette tile shows: a bare
//! texture/shape shows itself; a prefab shows its probed primary visual (its
//! first sprite texture or .oshape) - all through the asset-browser thumbnail
//! cache. Returns 0 for a pure-logic prefab with no cheap drawable (a generic
//! tile). Shared by the panel grid and the level-paint selfcheck.
ImTextureID paletteTileThumbnail(EditorState& state,
	AssetBrowserItem const& item);

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
