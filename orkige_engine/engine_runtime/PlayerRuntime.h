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

#include "core_util/String.h"
#include "core_debugnet/DebugServer.h"

#include <chrono>
#include <memory>
#include <set>

namespace Orkige
{
	class GameObjectManager;
	class PlayerLogForwarder;

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
		bool			debugRequested = false;	//!< --debug-port was given
		unsigned short	debugPort = 0;			//!< requested port (0 = ephemeral)
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

		//! @brief the engine media dir (Main/ + RTShaderLib/) a runtime
		//! should register: "<baseDir>/Media" when an exported app bundled
		//! it (detected via Media/Main), else the given build-tree fallback
		String resolveMediaDirectory(String const & fallbackMediaDir,
			String const & baseDir = String());
	}

	//! @brief the player side of the editor's play-mode debug protocol,
	//! shared by tools/player and native game modules: owns the DebugServer,
	//! answers editor commands (pause/resume/step/quit/select/set_property/
	//! request_hierarchy), streams the hierarchy on change and the selected
	//! object's state at ~15Hz, pushes a script_error message for every
	//! GameObject whose ScriptComponent fails (once per object per
	//! connection - failures on never-selected objects must not stay
	//! invisible), and forwards the runtime's Ogre log to the editor Console
	//! ("[remote]" lines).
	//! @remarks Call update() once per frame BEFORE stepping the world (so
	//! pause/step/set_property apply to the frame) and stream() AFTER it.
	//! The world-advance gate for a frame is
	//!   !isActive() || !isPaused() || step   with step = consumePendingStep()
	//! and a consumed step must advance exactly one fixed physics tick.
	//! Public signatures stay free of renderer types (renderer containment);
	//! the Ogre log listener is an implementation detail.
	class PlayerDebugLink
	{
		//--- Types -------------------------------------------
	public:
		//! frames between hierarchy change checks (~4 checks/s at 60 fps)
		static const unsigned long HIERARCHY_CHECK_INTERVAL;
		//! minimum milliseconds between object_state messages (~15 Hz)
		static const int OBJECT_STATE_INTERVAL_MS;
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		DebugServer		mServer;
		bool			mActive = false;		//!< start() succeeded
		bool			mPaused = false;		//!< update/physics stepping gated
		int				mPendingSteps = 0;		//!< queued single-steps (only while paused)
		bool			mQuitRequested = false;	//!< editor sent quit
		String			mSelectedObjectId;		//!< object whose state is streamed
		StringVector	mLastSentHierarchy;
		bool			mHierarchySent = false;	//!< has any hierarchy gone out yet
		//! ids already reported via script_error (cleared on client loss so
		//! a re-connecting editor session learns about them again)
		std::set<String>	mReportedScriptErrors;
		//! default-constructed = clock epoch, so the first send never waits
		std::chrono::steady_clock::time_point mLastStateSend;
		//! Ogre-log -> editor-Console forwarder (attached while active)
		std::unique_ptr<PlayerLogForwarder> mLogForwarder;
		//--- Methods -----------------------------------------
	public:
		PlayerDebugLink();
		~PlayerDebugLink();	//!< detaches the log forwarder (see shutdown)

		//! @brief listen on 127.0.0.1:port and start forwarding the default
		//! Ogre log to the (future) editor client; false when the port
		//! cannot be bound. Call after the engine is up (the log must exist).
		bool start(unsigned short port);

		bool isActive() const { return mActive; }
		unsigned short getPort() const { return mServer.getPort(); }
		bool isPaused() const { return mActive && mPaused; }
		bool isQuitRequested() const { return mQuitRequested; }
		//! one queued single-step? (true at most once per queued step; only
		//! ever true while paused)
		bool consumePendingStep();

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

		//! @brief orderly protocol shutdown: detach the log forwarder, tell
		//! the editor we are going down (unless quit was its idea) and give
		//! the socket a moment to flush; safe to call when never started
		void shutdown();
	protected:
	private:
		void sendError(String const & text);
		void sendHierarchyIfChanged(GameObjectManager & gameObjectManager,
			bool force);
		void sendNewScriptErrors(GameObjectManager & gameObjectManager);
		void handleSetProperty(GameObjectManager & gameObjectManager,
			DebugMessage const & message);
		void processMessages(GameObjectManager & gameObjectManager);
		void streamObjectState(GameObjectManager & gameObjectManager);
	};
}

#endif //__PlayerRuntime_h__8_7_2026__12_00_00__
