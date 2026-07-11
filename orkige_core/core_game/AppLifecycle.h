/**************************************************************
	created:	2026/07/11 at 15:00
	filename: 	AppLifecycle.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __AppLifecycle_h__11_7_2026__15_00_00__
#define __AppLifecycle_h__11_7_2026__15_00_00__

#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	//! @brief the mobile app lifecycle state machine: the ENGINE's backgrounding
	//! contract as pure logic. It maps the platform lifecycle events (an app
	//! going to / returning from the background, being terminated, or warned of
	//! low memory) to the concrete actions the host runtime must perform, and
	//! tracks the two gates the game loop reads back (the sim must not advance
	//! and rendering must be suppressed while backgrounded).
	//! @remarks Kept renderer- and SDL-free on purpose so the whole contract
	//! unit-tests headlessly (tests/core/AppLifecycleTests.cpp): the player owns
	//! the wiring (translate the SDL_EVENT_* to an Event, then perform the
	//! returned Actions against its SoundManager / SaveStore / Breadcrumbs /
	//! ScriptComponent surface - see tools/player/main.cpp).
	//! @remarks The contract, in one place:
	//!  - Going to background (WillEnterBackground): FLUSH the save store (a
	//!    backgrounded mobile app may be killed silently - this is the
	//!    crash-safe autosave point), deliver onAppPause to game scripts, PAUSE
	//!    the sim, SUSPEND audio, drop a "background" breadcrumb. The heavy
	//!    lifting happens on WILL (while still foregrounded), before the OS
	//!    suspends the process.
	//!  - Fully backgrounded (DidEnterBackground): STOP rendering. Mobile GPU
	//!    work in the background is an OS kill (iOS especially), so the loop must
	//!    not draw until the app returns.
	//!  - Returning (WillEnterForeground): RESUME rendering and audio.
	//!  - Fully foregrounded (DidEnterForeground): RESUME the sim (the engine
	//!    default is resume-RUNNING) and deliver onAppResume so the GAME decides
	//!    whether to re-pause itself behind an overlay, drop a "foreground"
	//!    breadcrumb.
	//!  - Terminating: a final save flush + "terminating" breadcrumb.
	//!  - LowMemory: a cheap save flush + "low_memory" breadcrumb.
	//! @remarks Robust to platforms that drop or reorder the WILL/DID halves:
	//! every transition is guarded by the gate flags, so a redundant event
	//! yields an all-false Actions and a missing WILL is folded into its DID.
	class ORKIGE_CORE_DLL AppLifecycle
	{
		//--- Types -------------------------------------------
	public:
		//! the platform lifecycle events (the SDL_EVENT_* the player translates)
		enum class Event
		{
			WillEnterBackground,	//!< about to background - do crash-safe cleanup
			DidEnterBackground,		//!< fully backgrounded - stop drawing
			WillEnterForeground,	//!< about to return - resume drawing/audio
			DidEnterForeground,		//!< fully foreground - resume the sim
			Terminating,			//!< the OS is killing the app - final flush
			LowMemory				//!< memory pressure warning - cheap flush
		};
		//! the foreground / background phase the app is in
		enum class Phase
		{
			Foreground,
			Background
		};
		//! @brief the host-side effects one event produced. Every field defaults
		//! to "nothing to do", so the host acts only on the true ones; breadcrumb
		//! is a short kind string or NULL. The sim/render gates are NOT in here -
		//! they are read back as state (isSimPaused / isRenderingStopped).
		struct Actions
		{
			bool flushSave = false;		//!< persist the save store now
			bool suspendAudio = false;	//!< tear the audio device down (interrupt)
			bool resumeAudio = false;	//!< bring the audio device back
			bool notifyPause = false;	//!< deliver onAppPause(self) to scripts
			bool notifyResume = false;	//!< deliver onAppResume(self) to scripts
			bool terminating = false;	//!< the OS is killing us - final shutdown
			char const * breadcrumb = nullptr;	//!< a crumb kind to record, or NULL
		};
		//--- Methods -----------------------------------------
	public:
		AppLifecycle();

		//! @brief feed one lifecycle event; returns the actions the host must
		//! perform. Idempotent against duplicate / out-of-order events (a
		//! redundant event yields an all-false Actions) and updates the gates.
		Actions handle(Event event);

		//! the foreground / background phase
		Phase phase() const { return mPhase; }
		//! is the app currently backgrounded
		bool isBackgrounded() const { return mPhase == Phase::Background; }
		//! @brief the sim gate: true while the app is backgrounded - the loop
		//! must NOT advance the world. Cleared on foreground (the engine default
		//! is resume-running; a game re-pauses itself from onAppResume).
		bool isSimPaused() const { return mSimPaused; }
		//! @brief the render gate: true while drawing must be suppressed. Mobile
		//! GPU work in the background is an OS kill, so the loop skips its frame.
		bool isRenderingStopped() const { return mRenderingStopped; }
		//! has a Terminating event been seen (the app is going down for good)
		bool isTerminating() const { return mTerminating; }
	private:
		Phase	mPhase;				//!< current foreground/background phase
		bool	mSimPaused;			//!< the sim gate (backgrounded)
		bool	mAudioSuspended;	//!< audio device torn down
		bool	mRenderingStopped;	//!< the render gate (backgrounded)
		bool	mTerminating;		//!< a Terminating event was seen
	};
}

#endif //__AppLifecycle_h__11_7_2026__15_00_00__
