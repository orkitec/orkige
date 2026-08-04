/**************************************************************
	created:	2026/08/02 at 09:00
	filename: 	GameHost.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __GameHost_h__2_8_2026__09_00_00__
#define __GameHost_h__2_8_2026__09_00_00__

//! @file GameHost.h
//! @brief the reusable game host: the platform harness every shipped runtime
//! needs, the canonical gameplay tick order and the frame-loop driver
//! @remarks AppHost ends where the loop begins - it owns the window, the
//! engine singletons and the world. GameHost owns what sits AROUND that on a
//! real device: materialising a packaged app's content (an APK's assets are
//! not files, a browser export's payload is one archive), resolving the
//! per-platform media/writable directories, pinning the mobile orientation,
//! the process-level abort diagnostics, the ONE gameplay tick order and the
//! frame loop itself. Compiled C++ game code links this instead of
//! re-deriving it: on mobile the packaging prologue is not optional, and on
//! the browser a hand-written `while` loop in main() cannot exist at all.

#include "engine_module/EnginePrerequisites.h"
#include "core_util/String.h"

#include <functional>
#include <utility>
#include <vector>

namespace Orkige
{
	class DebugDraw;
	class GameObjectManager;
	class HttpClient;
	class MonetizationService;
	class InputActionMap;
	class LevelManager;
	class PhysicsWorld;
	class RenderSystem;
	class ScreenFade;
	class ScriptTaskManager;
	class ScreenShake;
	class SoundManager;
	class TimerManager;
	class TweenManager;

	//======================================================================
	// the platform harness
	//======================================================================

	//! @brief what a host tells the platform harness before any window or
	//! engine object exists; every field has a working default
	struct GamePlatformConfig
	{
		//! names the desktop writable directory (the engine log, the
		//! breadcrumb trail, saves) under the platform's app-support root
		String			appName = "Orkige";
		//! prefixes every harness log line, so a packaging failure names the
		//! runtime that hit it
		String			logTag = "orkige";
		//! the build-tree engine media root a DESKTOP run reads; an exported
		//! bundle's own Media/ overrides it. The packaged platforms ignore
		//! this - they always read the media they carry.
		String			desktopMediaDirectory;
		//! extra content directories a desktop run registers as given (the
		//! sample media a dev tree can reach); each is skipped when absent
		StringVector	desktopContentDirectories;
		//! the sub-directory names under a packaged app's materialised
		//! content root that carry the same content ("assets", ...)
		StringVector	bundleContentSubdirectories;
		//! the scene a packaged app falls back to when launched with no
		//! arguments and carrying no bundled project
		String			bundledSceneName = "example.oscene";
	};

	//! @brief the platform prologue every shipped runtime runs before the
	//! engine boots, and the facts it resolved
	//! @remarks The call order is the boot order: boot() materialises the
	//! packaged content (so a project manifest can be READ), the host then
	//! resolves its project, resolveScenePath() applies the platform's
	//! scene-default rule, applyOrientationPolicy() pins the mobile window
	//! before it is created and resolveDirectories() names the writable
	//! paths (which depend on whether the run turned out to be a bundled
	//! one). mountPackagedContent() runs later, inside the render system's
	//! resource-registration callback.
	class ORKIGE_ENGINE_DLL GamePlatform
	{
		//--- Variables ---------------------------------------
	private:
		GamePlatformConfig	mConfig;
		//! a packaged app's materialised content root (separator-terminated);
		//! "" wherever the app's own base directory already is that root
		String				mContentRoot;
		String				mMediaDirectory;
		StringVector		mContentDirectories;
		//! (archive, archive-internal sub-tree) pairs read IN PLACE rather
		//! than written out - @see PlayerBundle::isMountedMediaPath
		std::vector<std::pair<String, String>>	mPakMounts;
		String				mEngineLogPath;
		String				mStateDirectory;
		bool				mFollowDeviceRotation = false;
		//--- Methods -----------------------------------------
	public:
		//! @brief materialise the packaged content and resolve the media +
		//! content directories this platform reads from.
		//! On a packaged Android app this brings SDL's video subsystem up
		//! early (asset IO runs through it), points the writable root at the
		//! app files dir and either mounts the uncompressed APK in place or
		//! extracts it; in the browser it unpacks the payload archive the
		//! page fetched. A desktop run has no prologue: it resolves an
		//! exported bundle's own media over the configured build-tree one and
		//! returns.
		//! @return false only when packaged content could not be read or
		//! written - a run that cannot continue honestly
		bool boot(GamePlatformConfig const & config);

		//! @brief the platform's rule for the scene a run was given.
		//! A packaged app defaults an empty path to its bundled scene and
		//! anchors a relative one in the writable root (the path shape a
		//! device-side play session pushes); a desktop run gets its path
		//! back unchanged, so an empty one stays empty and the host can
		//! report its usage line.
		String resolveScenePath(String const & scenePath) const;

		//! @brief pin the window orientation before the window is created.
		//! "landscape"/"portrait" constrain it (anything unrecognised lands
		//! on portrait, the default); "auto" leaves the constraint off AND
		//! asks for a rotation-following window - both halves are needed,
		//! since the window system derives the allowed set from an
		//! unconstrained window and only a resizable one may follow the
		//! device. A no-op on desktop.
		void applyOrientationPolicy(String const & orientation);

		//! @brief name the writable paths: the engine log file and the state
		//! directory (breadcrumb trail, benchmark artifacts, saves).
		//! A sandboxed app writes into its container; a desktop run writes an
		//! exported app's log into the app-support directory (a
		//! double-clicked app runs with an unwritable cwd) and a dev run's
		//! into the cwd, as always.
		void resolveDirectories(String const & logFileName, bool bundledRun);

		//! @brief mount the packaged archives whose bulk media never left
		//! them, each sub-tree flat so its files resolve by BARE resource
		//! name. Runs inside the render system's resource registration; a
		//! platform that extracted everything mounts nothing.
		void mountPackagedContent(RenderSystem & render,
			String const & resourceGroup) const;

		//! the materialised content root, "" when the app's base directory
		//! already is it (every platform but a packaged Android app)
		String const & getContentRoot() const { return this->mContentRoot; }
		//! the engine media root to register and to hand the render backend
		String const & getMediaDirectory() const { return this->mMediaDirectory; }
		//! the content directories to register when present, in order
		StringVector const & getContentDirectories() const
			{ return this->mContentDirectories; }
		String const & getEngineLogPath() const { return this->mEngineLogPath; }
		//! the writable directory diagnostics and saves default into
		//! (separator-terminated)
		String const & getStateDirectory() const { return this->mStateDirectory; }
		//! true when the window must be resizable so the device may rotate it
		bool followsDeviceRotation() const { return this->mFollowDeviceRotation; }
		//! true when getMediaDirectory() is NOT the configured build-tree
		//! default, so the render backend's baked media path must be
		//! overridden (always the case on a packaged app, whose baked default
		//! names a build tree that does not exist there)
		bool overridesEngineMedia() const;
		//! true on the platforms whose window is a fullscreen native surface
		//! (the orientation, rotation and bundled-scene rules apply there)
		static bool isMobile();
	};

	//======================================================================
	// process-level abort diagnostics
	//======================================================================

	//! @brief make a non-assert abort name itself in a captured log.
	//! Routes the Debug CRT's assertion reporting to stderr where the
	//! platform would otherwise report through a modal window, and installs
	//! a std::terminate hook that prints the escaping exception plus the
	//! breadcrumb tail before chaining to the previous handler - so the exit
	//! code stays honest and a headless runner's log says what died.
	void ORKIGE_ENGINE_DLL installAbortDiagnostics(String const & logTag);

	//! @brief chain a stderr last-gasp line in FRONT of whatever SIGABRT
	//! disposition is currently installed. Call it AFTER
	//! Breadcrumbs::installCrashHandler, so the durable file marker is the
	//! prior disposition this captures and calls. Only the platform whose
	//! raw aborts are otherwise silent installs anything; elsewhere the file
	//! marker already names the signal (and a sanitizer build must keep its
	//! own handlers).
	void ORKIGE_ENGINE_DLL installAbortSignalTrap(String const & logTag);

	//! @brief what this binary is, as the strings a performance artifact and
	//! a bug report quote
	struct GameBuildIdentity
	{
		String	flavor;			//!< the render flavor this build carries
		String	platform;		//!< the platform it runs on
		String	renderSystem;	//!< the render system it boots
		String	build;			//!< the build configuration
	};

	//! @brief describe the running binary (@see GameBuildIdentity)
	GameBuildIdentity ORKIGE_ENGINE_DLL describeBuild();

	//======================================================================
	// the canonical gameplay tick
	//======================================================================

	//! @brief the subsystems ONE gameplay tick advances, in the ONE order
	//! @remarks Every member is optional: a host that owns no such subsystem
	//! leaves it null and that slot is simply not ticked. @see advanceGameWorld
	struct GameTick
	{
		//! [1] the named actions this frame's raw input maps to
		InputActionMap*		inputActions = nullptr;
		//! [1b] the transport whose off-thread answers land at this boundary,
		//! and the store/ad surface whose platform callbacks land at the same
		//! one (a payment sheet answers on the platform's own queue)
		HttpClient*			httpClient = nullptr;
		MonetizationService* monetization = nullptr;
		//! [2] the world whose components run the game code, and the tasks
		//! those scripts suspended - resumed in that SAME phase and nowhere
		//! else (@see core_script/ScriptTaskManager.h)
		GameObjectManager*	gameObjects = nullptr;
		ScriptTaskManager*	scriptTasks = nullptr;
		//! [3] the scheduled interpolations and callbacks
		TweenManager*		tweens = nullptr;
		TimerManager*		timers = nullptr;
		//! [4] the simulation, stepped only when the scene needs one
		PhysicsWorld*		physics = nullptr;
		bool				physicsNeeded = false;
		//! [5] the director holding a pending scene switch
		LevelManager*		levels = nullptr;
		//! the host's re-entrant scene load the deferred-load pump calls with
		//! the pending scene NAME (the host resolves it against whatever it
		//! plays - an open project, a bundle - and tears the old world down
		//! through the GameObjectManager::clear teardown hook)
		std::function<bool(String const &)>	loadScene;
		//! the presentation layers ticked after the fence, on the REAL delta
		SoundManager*		sound = nullptr;
		ScreenFade*			screenFade = nullptr;
		ScreenShake*		screenShake = nullptr;
		DebugDraw*			debugDraw = nullptr;
		//! the gameplay time scale (0 = hitstop, still renders); presentation,
		//! input and rendering stay on the real delta
		float				timeScale = 1.0f;
	};

	//! @brief advance the world by one frame in the CANONICAL tick order.
	//! @param deltaTime the REAL frame delta (the gameplay phases apply
	//!        GameTick::timeScale themselves)
	//! @remarks Call it only on a frame the host decided to advance - a
	//! paused or backgrounded runtime keeps rendering and streaming but does
	//! not come here. The order itself is fenced and reasoned in the
	//! implementation; read it there before adding anything.
	void ORKIGE_ENGINE_DLL advanceGameWorld(GameTick const & tick,
		float deltaTime);

	//======================================================================
	// the frame loop
	//======================================================================

	//! @brief the frame loop a host hands to the platform
	//! @remarks CALLBACK-SHAPED because the browser leaves no choice: a page
	//! owns the frame cadence and the runtime must RETURN to it between
	//! frames, so a blocking `while` in main() cannot exist there. The
	//! callback form is therefore the general form, and every other
	//! platform's plain loop is expressed in terms of it.
	struct GameFrameLoop
	{
		//! the run state both callbacks receive
		void*	context = nullptr;
		//! one frame; returns false when the run has ended
		bool	(*frame)(void*) = nullptr;
		//! the orderly post-loop sequence (world teardown, protocol shutdown)
		void	(*finish)(void*) = nullptr;
		//! the run's exit code, read after finish() - browser only, where the
		//! runtime exits from inside the loop instead of returning to main()
		int		(*exitCode)(void*) = nullptr;
		//! destroy the context - browser only, for the same reason
		void	(*dispose)(void*) = nullptr;
		//! a scripted/frame-capped run paces itself instead of following the
		//! display, so a headless browser session's virtual clock can
		//! fast-forward the frames
		bool	automatedRun = false;
	};

	//! @brief true when runGameFrameLoop TAKES OWNERSHIP of the context.
	//! A host that keeps its run state in a smart pointer releases it into
	//! the loop when this says so and keeps it otherwise - one line instead
	//! of a platform conditional in every host.
	bool ORKIGE_ENGINE_DLL gameFrameLoopOwnsContext();

	//! @brief run the frame loop in the shape the platform allows.
	//! Everywhere but the browser this loops until GameFrameLoop::frame
	//! returns false, calls finish() and RETURNS with the context untouched
	//! - the caller still owns it and reads its exit code. In the browser the
	//! loop TAKES OWNERSHIP of the context (the caller must already have
	//! released it), never returns, and at the end of the run calls finish(),
	//! reads exitCode(), calls dispose() exactly once and exits the runtime.
	void ORKIGE_ENGINE_DLL runGameFrameLoop(GameFrameLoop const & loop);
}

#endif //__GameHost_h__2_8_2026__09_00_00__
