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

#include "EditorCamera.h"
#include "EditorCore.h"
#include "FileDialog.h"

#include <core_debugnet/DebugClient.h>
#include <core_project/Project.h>
#include <core_util/String.h>
#include <engine_base/EngineLog.h>
#include <engine_render/MeshInstance.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderTexture.h>

#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Orkige
{
	class ImGuiFacadeRenderer;
	class ImGuiSDL3Input;
	class RenderWorld;
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
	std::string platform;			//!< "macos" / "ios-simulator" / "android"
	std::string outputBuffer;		//!< partial (unterminated) last line
	std::string artifactPath;		//!< from the final OK line
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
	//! Lua hot-reload watcher (WP #77): poll <projectRoot>/scripts for edits
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

//! a physically connected iOS device (enumerated, not yet deployable)
struct IosHardwareDevice
{
	std::string name;
	std::string udid;
};

//! @brief connected iOS hardware via 'xcrun devicectl list devices'
std::vector<IosHardwareDevice> listIosHardwareDevices();
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

//! @brief Lua hot-reload (WP #77): tell the running player to recompile-and-
//! swap its scripts (MSG_RELOAD_SCRIPT, reload-ALL). Sent automatically by the
//! scripts/ watcher in updatePlaySession and by the toolbar "Reload Scripts"
//! button. Optimistically clears scriptErrorIds - the player re-pushes
//! script_error only if a reload actually failed.
void reloadRemoteScripts(PlaySession& session, EditorConsole& console);

//! per-frame pump: connection progress, protocol messages, build/process
//! supervision, crash detection
void updatePlaySession(PlaySession& session, EditorConsole& console);

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

// (re)size the scene RTT: first call creates it, later calls
// resize-by-recreate behind the facade - see EditorScenePanel.cpp
void createSceneRenderTexture(SceneRenderTarget& target, int width, int height);

// place the scene camera on its orbit sphere around the orbit target
// (the position math lives in EditorCamera.h, shared with the fly mode)
void applyOrbitCamera(EditorState const& state,
	optr<Orkige::RenderNode> const& cameraNode);

// the ground-plane reference grid, all facade (the returned mesh handle
// must stay alive with the node - RAII); see EditorScenePanel.cpp
optr<Orkige::MeshInstance> createEditorGrid(Orkige::RenderWorld* world,
	optr<Orkige::RenderNode> const& gridNode);

// F: frame the selected object - retarget the orbit to the object's world
// bounds centre and fit the orbit distance to its bounding radius
void frameSelectedObject(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera);

// Unity-style double-click focus: select the object AND frame it - the same
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

//--- scene/project document operations (EditorDocument.cpp) -----------------

// File > New Scene: clear all GameObjects
void newScene(EditorState& state, Orkige::EditorCore& core);

bool saveSceneToPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path);

bool openSceneFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path);

// Unity-style "open a project, not a scene" - see EditorDocument.cpp
bool openProjectFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path);

// File > Close Project: back to loose-scene mode
void closeProject(EditorState& state, Orkige::EditorCore& core);

// File > New Project...: create the skeleton in the chosen folder + open it
bool newProjectAtPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& folder);

//! the directory File > Import Mesh copies into (an open project ROOTS the
//! import, otherwise the historical loose-scene rule applies)
std::string meshImportDestination(EditorState const& state);

bool importMeshFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& sourcePath);

// GameObject > Create Prefab / Hierarchy context menu: write the selection's
// subtree as "<assets>/<rootId>.oprefab" (stable .orkmeta id included) and
// convert it into a prefab instance (undoable); on an instance root it
// re-makes (overwrites) the instance's own prefab file instead
bool createPrefabFromSelection(EditorState& state, Orkige::EditorCore& core);

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

// fullscreen dockspace + first-run DockBuilder layout - EditorMenus.cpp
void drawDockspace(EditorState& state, float toolbarHeight,
	ViewSettings& viewSettings);

// the Scene panel: RTT image, gizmos, picking, camera navigation -
// EditorScenePanel.cpp
void drawScenePanel(EditorState& state, Orkige::EditorCore& core,
	bool editMode, SceneRenderTarget& sceneTarget,
	optr<Orkige::RenderNode> const& cameraNode,
	ViewSettings& viewSettings, float contentScale,
	Orkige::ImGuiSDL3Input& imguiInput);

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
void drawStatsPanel(bool* visible);

// the Console panel: Log tab (engine/editor/remote lines) + Lua REPL tab
// (the session lets a `set` cvar line during Play tune the running player)
void drawConsolePanel(EditorState& state, PlaySession& session,
	EditorConsole& console, bool* visible);

#endif // ORKIGE_EDITORAPP_H_09072026
