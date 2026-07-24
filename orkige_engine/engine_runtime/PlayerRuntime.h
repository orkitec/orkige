/**************************************************************
	created:	2026/07/08 at 12:00
	filename: 	PlayerRuntime.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlayerRuntime_h__8_7_2026__12_00_00__
#define __PlayerRuntime_h__8_7_2026__12_00_00__
#include "core_util/optr.h"
#include "core_util/String.h"
#include "core_debug/ProfileManager.h"
#include "core_debugnet/DebugClient.h"
#include "core_debugnet/DebugServer.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>


namespace Orkige
{
	class GameObjectManager;
	class EngineLogCapture;
	class TraceWriter;
	class ScriptRuntime;	//core_script/ScriptRuntime.h - the debug seam the break pump installs onto

	//! @brief THE player CLI contract, parsed: every runtime the editor's
	//! play mode can launch (tools/player, a project's native module built
	//! via cmake/OrkigeGameModule.cmake) must accept
	//!   [scene.oscene] [--project <dir-or-.orkproj>] [--debug-port N]
	//! - an optional positional scene file, the project to root resource
	//! locations in, and the debug-protocol port the editor connects to.
	//! Using this parser keeps the contract identical everywhere.
	struct PlayerArguments
	{
		String			scenePath;				//!< positional scene ("" = none given)
		String			projectPath;			//!< --project value ("" = loose-scene mode)
		//! --orientation value ("" = none given): the project manifest's
		//! export.orientation (portrait/landscape/auto), delivered explicitly
		//! for play sessions where the manifest itself does not travel to the
		//! device (the editor's Android play deploys only the temp scene)
		String			orientation;
		bool			debugRequested = false;	//!< --debug-port was given
		unsigned short	debugPort = 0;			//!< requested port (0 = ephemeral)
		//! --debug-bind all/0.0.0.0: bind the debug port to EVERY interface
		//! instead of loopback (the explicit network-exposure opt-in); default
		//! false keeps the link on 127.0.0.1
		bool			debugExposeNonLoopback = false;
		bool			valid = true;			//!< false on an unknown argument
		String			unknownArgument;		//!< the offender when !valid

		//! parse argv (argv[0] ignored); never throws, unknown args set !valid
		static PlayerArguments parse(int argc, char ** argv);
	};

	//! @brief exported-app support (project export, Util/orkige_export.py):
	//! an exported app carries its project and engine media NEXT TO the
	//! executable's resources and finds them WITHOUT command-line arguments
	//! through a tiny marker file - the no-args default-project mechanism
	//! every runtime (tools/player, native game modules) shares.
	//! @remarks The marker "orkige_project.txt" lives in the app's BASE
	//! directory - SDL_GetBasePath(): macOS .app = Contents/Resources/, iOS
	//! .app = the flat bundle root, a plain executable = its own directory;
	//! on Android the caller passes the extracted-assets root explicitly
	//! (SDL has no base path there) - and holds a single line: the project
	//! path relative to that base (the exporter writes "project"). A missing
	//! marker simply means "not an exported app" - dev runs are unaffected.
	//! Everything here is pure filesystem logic (plus SDL_GetBasePath), so
	//! the engine unit tests cover it headlessly via the explicit baseDir.
	namespace PlayerBundle
	{
		//! the marker file's name ("orkige_project.txt")
		extern const String PROJECT_MARKER_FILE_NAME;

		//! @brief the app's base directory (SDL_GetBasePath(), separator-
		//! terminated); "" when SDL cannot provide one (e.g. on Android)
		String baseDirectory();

		//! @brief the bundled default project the marker under baseDir names
		//! ("" = use baseDirectory()): the marker's first line resolved
		//! against baseDir. Returns "" when there is no marker (not an
		//! exported app), the marker is empty or the named path is missing.
		String findBundledProject(String const & baseDir = String());

		//! @brief the engine media dir a runtime should register:
		//! "<baseDir>/Media" when an exported app bundled it, else the given
		//! build-tree fallback. A bundle is detected via its flavor-specific
		//! shader media - the classic RTSS library (Media/Main) or the
		//! Ogre-Next Hlms shader templates (Media/Hlms).
		String resolveMediaDirectory(String const & fallbackMediaDir,
			String const & baseDir = String());
	}

	//! @brief the player side of the editor's play-mode debug protocol,
	//! shared by tools/player and native game modules: owns the DebugServer,
	//! answers editor commands (pause/resume/step/quit/select/set_property/
	//! set_cvar/reload_script/screenshot/record/request_hierarchy), streams the
	//! hierarchy on change and the selected
	//! object's state at ~15Hz, pushes a script_error message for every
	//! GameObject whose ScriptComponent fails (once per object per
	//! connection - failures on never-selected objects must not stay
	//! invisible), and forwards the runtime's engine log to the editor
	//! Console ("[remote]" lines) via the shared EngineLogCapture service
	//! (engine_base/EngineLog.h).
	//! @remarks Call update() once per frame BEFORE stepping the world (so
	//! pause/step/set_property apply to the frame) and stream() AFTER it.
	//! The world-advance gate for a frame is
	//!   !isActive() || !isPaused() || step   with step = consumePendingStep()
	//! and a consumed step must advance exactly one fixed physics tick.
	//! Public signatures stay free of renderer types (renderer containment);
	//! the log capture hides the logging backend entirely.
	//! @par Two transports, one protocol
	//! start(port) is the historical LISTEN mode (the editor dials the
	//! player). startConnect(endpoint) REVERSES the direction for runtimes
	//! that cannot listen: the browser player dials the editor's serve
	//! port, where the byte stream rides a WebSocket the platform's socket
	//! emulation wraps transparently. Same messages, same handlers, same
	//! verbs either way; a dial that never lands (no editor session waiting)
	//! gives up after a bounded retry window and the game runs standalone.
	class PlayerDebugLink
	{
		//--- Types -------------------------------------------
	public:
		//! frames between hierarchy change checks (~4 checks/s at 60 fps)
		static const unsigned long HIERARCHY_CHECK_INTERVAL;
		//! minimum milliseconds between object_state messages (~15 Hz)
		static const int OBJECT_STATE_INTERVAL_MS;
		//! minimum milliseconds between full-scene transform messages (~15 Hz):
		//! the whole-scene motion mirror rides the same cadence as object_state
		static const int SCENE_TRANSFORM_INTERVAL_MS;
		//! @brief reverse-connect retry cadence: the editor listens BEFORE
		//! it opens the page, so the first dial normally lands - retries
		//! only cover an editor briefly stalled in a synchronous UI moment
		static const int DIAL_RETRY_INTERVAL_MS;
		//! @brief reverse-connect give-up budget: after this many seconds of
		//! refused dials there is no editor session waiting (the page was
		//! opened by hand / re-opened after the session ended) and the game
		//! runs standalone. Generous against a host machine under full test
		//! parallelism, still short enough that a hand-opened page stops
		//! probing quickly.
		static const int DIAL_GIVE_UP_SECONDS;
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		DebugServer		mServer;
		//! @brief the reverse-connect transport (startConnect): the runtime
		//! dials the editor instead of listening. Exactly one of the two is
		//! in use per session; the link* helpers dispatch on mDialMode so
		//! every handler above them stays transport-blind.
		DebugClient		mDialer;
		bool			mDialMode = false;		//!< startConnect() mode
		String			mDialHost;				//!< editor endpoint host
		unsigned short	mDialPort = 0;			//!< editor endpoint port
		bool			mDialWasConnected = false;	//!< previous pump's link state (edge tracking)
		bool			mDialConnectedEvent = false;	//!< pending "link up" edge
		bool			mDialDisconnectedEvent = false;	//!< pending "link lost" edge
		bool			mDialEverConnected = false;	//!< a dial landed this session
		std::chrono::steady_clock::time_point mDialStart;		//!< first attempt
		std::chrono::steady_clock::time_point mDialLastAttempt;	//!< retry pacing
		bool			mActive = false;		//!< start()/startConnect() succeeded
		bool			mPaused = false;		//!< update/physics stepping gated
		int				mPendingSteps = 0;		//!< queued single-steps (only while paused)
		bool			mQuitRequested = false;	//!< editor sent quit
		//! a MSG_SCREENSHOT arrived: the main loop consumes the path after
		//! rendering, captures the window and reports back (kept out of the
		//! renderer-agnostic protocol code on purpose)
		bool			mHasPendingScreenshot = false;
		String			mPendingScreenshotPath;
		//! a MSG_RECORD_START trace is active: the main loop samples the world
		//! every Nth frame (positions/velocities/flags) and this class records
		//! interleaved events (contacts, scene loads, script errors, warnings)
		//! until the time budget is spent or MSG_RECORD_STOP arrives, then
		//! writes the .jsonl trace and reports back with notifyTraceSaved
		bool			mRecording = false;
		String			mRecordPath;			//!< output .jsonl trace path
		unsigned int	mRecordEveryNth = 1;	//!< sample every Nth frame
		float			mRecordMaxSeconds = 0.0f;	//!< wall-clock budget
		float			mRecordElapsed = 0.0f;		//!< seconds recorded so far (event line t)
		unsigned long	mRecordFrameCounter = 0;	//!< frames seen while recording
		unsigned long	mRecordLastFrame = 0;		//!< frame number of the last tick (event line frame)
		bool			mRecordShouldFinish = false;//!< budget spent / stop asked
		std::set<String>	mRecordFilter;			//!< id/name allowlist (empty = all named objects)
		Orkige::uptr<TraceWriter> mTrace;		//!< the JSONL flight recorder (null when idle)
		String			mSelectedObjectId;		//!< object whose state is streamed
		StringVector	mLastSentHierarchy;
		StringVector	mLastSentParents;		//!< parent ids parallel to mLastSentHierarchy
		StringVector	mLastSentActives;		//!< activeSelf flags parallel to mLastSentHierarchy
		bool			mHierarchySent = false;	//!< has any hierarchy gone out yet
		//! ids already reported via script_error (cleared on client loss so
		//! a re-connecting editor session learns about them again)
		std::set<String>	mReportedScriptErrors;
		//! default-constructed = clock epoch, so the first send never waits
		std::chrono::steady_clock::time_point mLastStateSend;
		//! whole-scene transform mirror bookkeeping: the last LOCAL transform
		//! streamed per object id (10 floats: pos, orientation w/x/y/z, scale),
		//! so only CHANGED objects ride each delta. mSceneTransformsFullResend
		//! forces the next stream to carry EVERY object (a fresh client, a
		//! mid-play scene switch); it is armed on connect and on scene reload.
		std::map<String, std::array<float, 10>> mLastSentTransforms;
		bool			mSceneTransformsFullResend = true;
		std::chrono::steady_clock::time_point mLastTransformSend;
		//! peak resident set size observed while streaming (bytes); 0 until the
		//! first sample or on a platform without a memory query
		std::size_t		mPeakResidentBytes = 0;
		//! engine-log -> editor-Console capture (attached while active)
		Orkige::uptr<EngineLogCapture> mLogCapture;
		//! @brief script-debugger session state: non-debug messages that
		//! arrived during a break's nested pump are DEFERRED here and replayed
		//! by the next normal processMessages (a set_property mid-break must
		//! not run inside script execution); mNotifiedBreakSequence tracks the
		//! last ScriptRuntime break the editor was told about (one
		//! MSG_DEBUG_BREAK per pause); mDebugPumpInstalled says the runtime's
		//! break pump handler points at this link.
		std::vector<DebugMessage> mDeferredMessages;
		unsigned int	mNotifiedBreakSequence = 0;
		bool			mDebugPumpInstalled = false;
		//! reused snapshot buffer for the profile stream (steady state keeps
		//! its capacity - the readback must not become churn itself)
		std::vector<ProfileManager::SnapshotNode> mProfileScratch;
		//--- Methods -----------------------------------------
	public:
		PlayerDebugLink();
		~PlayerDebugLink();	//!< detaches the log capture (see shutdown)

		//! @brief listen on port and start forwarding the engine log to the
		//! (future) editor client; false when the port cannot be bound. Call
		//! after the engine is up (the log must exist). Binds 127.0.0.1 ONLY by
		//! default - the debug link must not be reachable off the machine;
		//! @p exposeNonLoopback is the explicit opt-in that binds every
		//! interface, only safe behind a trusted boundary.
		bool start(unsigned short port, bool exposeNonLoopback = false);

		//! @brief reverse-connect mode: DIAL the editor's debug endpoint
		//! ("host:port", or just "port" for 127.0.0.1) instead of listening -
		//! the transport for runtimes that cannot host a socket (a browser
		//! page dials out over a WebSocket its platform wraps around this
		//! plain connect). False on a malformed endpoint or an immediate
		//! socket failure; a dial that stays unanswered gives up after
		//! DIAL_GIVE_UP_SECONDS and deactivates the link (standalone run).
		bool startConnect(String const & endpoint);

		bool isActive() const { return mActive; }
		unsigned short getPort() const { return mServer.getPort(); }
		bool isPaused() const { return mActive && mPaused; }
		bool isQuitRequested() const { return mQuitRequested; }
		//! one queued single-step? (true at most once per queued step; only
		//! ever true while paused)
		bool consumePendingStep();

		//! @brief a queued screenshot request? On true, outPath receives the
		//! editor-requested capture path and the request is consumed (at most
		//! once per MSG_SCREENSHOT). The player's main loop performs the actual
		//! window capture (renderer containment keeps the render call out of
		//! this class) AFTER rendering the frame, then reports the outcome with
		//! notifyScreenshotSaved.
		bool consumePendingScreenshot(String & outPath);
		//! @brief answer a consumed screenshot request: sends MSG_SCREENSHOT_SAVED
		//! back to the editor (path echoed, ok flag, error text on failure). A
		//! no-op when the link has no client.
		void notifyScreenshotSaved(String const & path, bool ok,
			String const & error = String());

		//! @brief is a MSG_RECORD_START trace in progress? The player's main
		//! loop gates its per-frame sampling on this.
		bool isRecording() const { return mActive && mRecording; }
		//! @brief advance the active trace by one rendered frame: accrue the
		//! wall-clock budget (finishing when it is spent) and, on every Nth
		//! frame, sample the world (named objects' world position, velocity
		//! when a rigid body exists, active + in-view flags) into the trace.
		//! Reuses the render facade's window camera for the visibility test.
		void traceFrame(GameObjectManager & gameObjectManager,
			unsigned long frameCount, float deltaSeconds);
		//! @brief record a physics contact as a trace event AT the frame it
		//! occurred (both object names). A no-op when not recording. The main
		//! loop resolves the contact bodies to names and calls this.
		void traceContact(String const & nameA, String const & nameB,
			bool began);
		//! @brief fold the frame's script-emitted / gui / engine-mirror bus
		//! events into the trace's event stream (and drain the bus capture
		//! buffer). The main loop calls this each recorded frame; a no-op harvest
		//! when not recording.
		void traceScriptEvents();
		//! @brief should the trace be wrapped up now (budget spent or
		//! MSG_RECORD_STOP received)? The main loop polls this and calls
		//! finishRecording on a true.
		bool recordingShouldFinish() const;
		//! @brief write the sampled trace to its path and send MSG_RECORD_SAVED
		//! (path echoed, ok flag, error text on failure), then end the trace.
		//! A no-op when nothing is recording.
		void finishRecording();

		//! @brief per-frame pump BEFORE stepping the world: accept/lose the
		//! client (hello + initial hierarchy on connect, un-pause on a
		//! vanished editor) and act on every queued editor command
		void update(GameObjectManager & gameObjectManager,
			String const & scenePath);

		//! @brief per-frame streaming AFTER stepping: hierarchy on change and
		//! new script errors (checked every HIERARCHY_CHECK_INTERVAL frames),
		//! selected object state at ~15Hz, queued log lines - also while paused
		void stream(GameObjectManager & gameObjectManager,
			unsigned long frameCount);

		//! @brief a mid-play scene switch happened (deferred level load):
		//! the previous world was torn down and a new one loaded, so any
		//! remembered selection id now dangles and the last-sent hierarchy is
		//! stale. Drop the selection, force the next stream() to re-send the
		//! full hierarchy of the new scene and tell the editor which scene runs
		//! now (MSG_SCENE_LOADED with @p scenePath - project-relative when the
		//! run plays a project, so the editor resolves it against ITS copy of
		//! the same files; sent from here so it always precedes the new
		//! scene's hierarchy/transform streams).
		void onSceneReloaded(String const & scenePath);

		//! @brief orderly protocol shutdown: detach the log forwarder, tell
		//! the editor we are going down (unless quit was its idea) and give
		//! the socket a moment to flush; safe to call when never started
		void shutdown();
	protected:
	private:
		//--- the transport seam: every handler talks to the link through
		//--- these, blind to whether the session listens or dialed out
		//! pump the active transport (incl. dial retry/give-up bookkeeping)
		void linkUpdate();
		//! is an editor attached right now
		bool linkHasClient() const;
		//! send to the attached editor (false without one)
		bool linkSend(DebugMessage const & message);
		//! pop the next received message
		bool linkReceive(DebugMessage & out);
		//! true once per new editor attachment (edge, consumed)
		bool linkConsumeConnected();
		//! true once per lost editor (edge, consumed)
		bool linkConsumeDisconnected();
		//! drop the transport (both modes)
		void linkStop();
		//! wind the link down mid-run (dial gave up / editor closed the
		//! session): detach the log capture and deactivate - standalone
		void deactivateStandalone(String const & reason);
		void sendError(String const & text);
		void sendHierarchyIfChanged(GameObjectManager & gameObjectManager,
			bool force);
		void sendNewScriptErrors(GameObjectManager & gameObjectManager);
		void handleSetProperty(GameObjectManager & gameObjectManager,
			DebugMessage const & message);
		void handleReloadScript(GameObjectManager & gameObjectManager,
			DebugMessage const & message);
		void handleReloadUi(DebugMessage const & message);
		void handleReloadAnim(GameObjectManager & gameObjectManager,
			DebugMessage const & message);
		void handleSetCvar(DebugMessage const & message);
		void handleRecordStart(DebugMessage const & message);
		//! @brief answer MSG_QUERY_SPAWNS with MSG_SCENE_SPAWNS descriptors
		//! (component kinds + the reflected property records) for every
		//! requested id the world still holds - the editor's scene mirror
		//! instantiates lightweight stand-ins for runtime-spawned objects from
		//! them. Batched into several messages when many ids are asked at once
		//! (each batch internally consistent) so no reply outgrows the
		//! transport line cap.
		void handleQuerySpawns(GameObjectManager & gameObjectManager,
			DebugMessage const & message);
		//--- script debugger (the MSG_DEBUG_* family) ---
		//! @brief apply a full breakpoint-set replace through the ScriptRuntime
		//! debug seam (installing this link as the break pump on first use);
		//! refusals (browser player, scripting off) answer with an error
		void handleDebugBreakpoints(DebugMessage const & message);
		//! @brief release a held break: resume freely or arm a step, then
		//! confirm with MSG_DEBUG_RESUMED; an un-paused runtime answers an error
		void handleDebugResume(DebugMessage const & message);
		//! @brief BREAK ON NEXT STATEMENT: arm a one-shot break on the first
		//! script line the runtime executes next (installing this link as the
		//! break pump on first use, like a breakpoint). Works while running and
		//! while frame-paused (the arm persists until scripts tick again); the
		//! hit rides the SAME MSG_DEBUG_BREAK path. Refusals (browser player,
		//! scripting off) answer with an error.
		void handleDebugBreakNext(DebugMessage const & message);
		//! @brief BREAK ON SCRIPT ERROR: arm/disarm pausing at an uncaught script
		//! error (FIELD_VALUE "1"/"0"). Arming installs this link as the break
		//! pump (like a breakpoint) so a subsequent error break can report over
		//! the SAME MSG_DEBUG_BREAK path (with FIELD_ERROR). Refusals (browser
		//! player, scripting off) answer with an error; on those the error itself
		//! still flows today's path. Pushed as full state on connect + on change.
		void handleDebugBreakOnErrors(DebugMessage const & message);
		//! @brief make THIS link the runtime's break pump on first use (the
		//! shared install both handleDebugBreakpoints and handleDebugBreakNext
		//! need, so a break-next armed without any breakpoint still reports)
		void ensureDebugPumpInstalled(ScriptRuntime & runtime);
		//! @brief answer a MSG_DEBUG_LOCALS request from the held break's
		//! frames (locals/upvalues, or one expanded table) - error outside one
		void handleDebugLocals(DebugMessage const & message);
		//! @brief ONE iteration of the nested break pump the ScriptRuntime
		//! calls in a loop while script execution is paused at a break: sends
		//! the break notification once, services the transport (debug commands
		//! + quit inline, everything else deferred to the next normal frame
		//! drain), keeps the OS event queue pumped and paces itself. A client
		//! that vanishes mid-break auto-resumes - the game never stays wedged.
		void serviceBreakPump();
		//! announce the current break to the editor (MSG_DEBUG_BREAK with the
		//! paused location + call stack)
		void sendDebugBreakNotification();
		//! @brief shared client-loss bookkeeping (update() and the break pump):
		//! un-pause, drop selection/stream baselines, clear the reported-error
		//! dedupe AND detach the script debugger (clear breakpoints, resume a
		//! held break) - a vanished editor must never leave the game frozen
		void onEditorClientLost();
		//! @brief dispatch ONE received editor command (the message chain
		//! processMessages and the deferred replay share)
		void handleOneMessage(GameObjectManager & gameObjectManager,
			DebugMessage const & message);
		//! record an event on the active trace (a no-op when idle): the hook
		//! the scene-reload / script-error / warning observers funnel through
		void traceEvent(String const & event,
			std::vector<std::pair<String, String>> const & fields);
		void processMessages(GameObjectManager & gameObjectManager);
		void streamObjectState(GameObjectManager & gameObjectManager);
		//! @brief stream the whole scene's LOCAL transforms as MSG_SCENE_TRANSFORMS
		//! (a delta - only objects whose transform changed since the last send;
		//! a full set on the first send after connect / a scene switch), on the
		//! ~15Hz cadence. Fire-and-forget; nothing goes out when nothing moved.
		void streamSceneTransforms(GameObjectManager & gameObjectManager);
		//! query the process resident set size, fold it into the session peak
		//! and return the current value (0 when the platform cannot query it)
		std::size_t sampleMemory();
		//! send an MSG_STATS metrics line (process memory + window size +
		//! safe-area insets) to the editor; a no-op when neither is available
		void streamStats();
		//! send an MSG_UI_LAYOUT line (gui widget ids + pixel rects +
		//! visibility) to the editor; a no-op when the game has no UI system
		void streamUiLayout();
		//! send an MSG_PROFILE_DATA line (the last frame's hierarchical CPU
		//! scope tree + frame time) to the editor; a no-op while the profiler
		//! is disabled or has no completed frame yet
		void streamProfile();
	};
}

#endif //__PlayerRuntime_h__8_7_2026__12_00_00__
