/**************************************************************
	created:	2026/07/11 at 15:00
	filename: 	AppLifecycleTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless AppLifecycle unit tests: the pure backgrounding state machine that
	maps platform lifecycle events to the engine's contract (flush save, suspend
	audio, pause the sim, stop rendering, notify scripts, breadcrumbs). Proves
	the actions each transition emits, the sim/render gates, idempotence against
	duplicate events, and the defensive folding when a platform drops the
	WILL/DID half of a transition. The rendered end-to-end proof (the real
	player wiring reacting to synthetic SDL events) is the
	player_lifecycle_selfcheck integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_game/AppLifecycle.h>

#include <string>

using Orkige::AppLifecycle;

namespace
{
	//! the breadcrumb kind an Actions carries, as a comparable string ("" = none)
	std::string crumb(AppLifecycle::Actions const & actions)
	{
		return actions.breadcrumb ? std::string(actions.breadcrumb) : std::string();
	}
}

TEST_CASE("AppLifecycle starts foreground and running", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	REQUIRE(lifecycle.phase() == AppLifecycle::Phase::Foreground);
	REQUIRE_FALSE(lifecycle.isBackgrounded());
	REQUIRE_FALSE(lifecycle.isSimPaused());
	REQUIRE_FALSE(lifecycle.isRenderingStopped());
	REQUIRE_FALSE(lifecycle.isTerminating());
}

TEST_CASE("WillEnterBackground does the crash-safe cleanup", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	const AppLifecycle::Actions actions =
		lifecycle.handle(AppLifecycle::Event::WillEnterBackground);

	// the contract: flush the save (silent-kill safety), pause the game, suspend
	// audio, drop a "background" crumb - all on WILL, while still foregrounded
	REQUIRE(actions.flushSave);
	REQUIRE(actions.notifyPause);
	REQUIRE(actions.suspendAudio);
	REQUIRE(crumb(actions) == "background");
	// the sim gate engages immediately; rendering only stops once fully
	// backgrounded (DidEnterBackground) - a WILL frame may still present
	REQUIRE(lifecycle.isSimPaused());
	REQUIRE_FALSE(lifecycle.isRenderingStopped());
	// resume-only actions must stay quiet
	REQUIRE_FALSE(actions.resumeAudio);
	REQUIRE_FALSE(actions.notifyResume);
}

TEST_CASE("DidEnterBackground stops rendering", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	lifecycle.handle(AppLifecycle::Event::WillEnterBackground);
	const AppLifecycle::Actions actions =
		lifecycle.handle(AppLifecycle::Event::DidEnterBackground);

	// WILL already did the cleanup - DID must not repeat it, only stop drawing
	REQUIRE_FALSE(actions.flushSave);
	REQUIRE_FALSE(actions.suspendAudio);
	REQUIRE_FALSE(actions.notifyPause);
	REQUIRE(lifecycle.isRenderingStopped());
	REQUIRE(lifecycle.isBackgrounded());
	REQUIRE(lifecycle.phase() == AppLifecycle::Phase::Background);
}

TEST_CASE("foreground resumes rendering, audio and the sim", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	lifecycle.handle(AppLifecycle::Event::WillEnterBackground);
	lifecycle.handle(AppLifecycle::Event::DidEnterBackground);

	// WILL_ENTER_FOREGROUND: rendering and audio come back before the game runs
	const AppLifecycle::Actions willFg =
		lifecycle.handle(AppLifecycle::Event::WillEnterForeground);
	REQUIRE(willFg.resumeAudio);
	REQUIRE_FALSE(lifecycle.isRenderingStopped());
	// the sim is still gated until fully foreground (no gameplay under the
	// resume animation)
	REQUIRE(lifecycle.isSimPaused());
	REQUIRE_FALSE(willFg.notifyResume);

	// DID_ENTER_FOREGROUND: the sim resumes running and the game is notified
	const AppLifecycle::Actions didFg =
		lifecycle.handle(AppLifecycle::Event::DidEnterForeground);
	REQUIRE(didFg.notifyResume);
	REQUIRE(crumb(didFg) == "foreground");
	REQUIRE_FALSE(lifecycle.isSimPaused());
	REQUIRE(lifecycle.phase() == AppLifecycle::Phase::Foreground);
	// audio was already restored on WILL - DID must not double-resume it
	REQUIRE_FALSE(didFg.resumeAudio);
}

TEST_CASE("duplicate background events are no-ops", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	lifecycle.handle(AppLifecycle::Event::WillEnterBackground);
	// a second WILL (or a redundant DID) must not flush/suspend again
	const AppLifecycle::Actions again =
		lifecycle.handle(AppLifecycle::Event::WillEnterBackground);
	REQUIRE_FALSE(again.flushSave);
	REQUIRE_FALSE(again.suspendAudio);
	REQUIRE_FALSE(again.notifyPause);
	REQUIRE(crumb(again).empty());
	REQUIRE(lifecycle.isSimPaused());
}

TEST_CASE("DidEnterBackground alone folds in the cleanup", "[unit][lifecycle]")
{
	// a platform that drops the WILL half: DID must still deliver the full
	// background contract, not just stop rendering
	AppLifecycle lifecycle;
	const AppLifecycle::Actions actions =
		lifecycle.handle(AppLifecycle::Event::DidEnterBackground);
	REQUIRE(actions.flushSave);
	REQUIRE(actions.suspendAudio);
	REQUIRE(actions.notifyPause);
	REQUIRE(crumb(actions) == "background");
	REQUIRE(lifecycle.isSimPaused());
	REQUIRE(lifecycle.isRenderingStopped());
}

TEST_CASE("DidEnterForeground alone restores everything", "[unit][lifecycle]")
{
	// a platform that drops the WILL_ENTER_FOREGROUND half: DID must restore
	// rendering AND audio AND the sim in one go
	AppLifecycle lifecycle;
	lifecycle.handle(AppLifecycle::Event::DidEnterBackground);
	const AppLifecycle::Actions actions =
		lifecycle.handle(AppLifecycle::Event::DidEnterForeground);
	REQUIRE(actions.resumeAudio);
	REQUIRE(actions.notifyResume);
	REQUIRE(crumb(actions) == "foreground");
	REQUIRE_FALSE(lifecycle.isRenderingStopped());
	REQUIRE_FALSE(lifecycle.isSimPaused());
}

TEST_CASE("Terminating asks for a final flush", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	const AppLifecycle::Actions actions =
		lifecycle.handle(AppLifecycle::Event::Terminating);
	REQUIRE(actions.flushSave);
	REQUIRE(actions.terminating);
	REQUIRE(crumb(actions) == "terminating");
	REQUIRE(lifecycle.isTerminating());
}

TEST_CASE("LowMemory flushes cheaply and crumbs", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	const AppLifecycle::Actions actions =
		lifecycle.handle(AppLifecycle::Event::LowMemory);
	REQUIRE(actions.flushSave);
	REQUIRE(crumb(actions) == "low_memory");
	// low memory alone does not background the app
	REQUIRE_FALSE(lifecycle.isSimPaused());
	REQUIRE_FALSE(lifecycle.isRenderingStopped());
}

TEST_CASE("a full background/foreground cycle returns to rest", "[unit][lifecycle]")
{
	AppLifecycle lifecycle;
	lifecycle.handle(AppLifecycle::Event::WillEnterBackground);
	lifecycle.handle(AppLifecycle::Event::DidEnterBackground);
	lifecycle.handle(AppLifecycle::Event::WillEnterForeground);
	lifecycle.handle(AppLifecycle::Event::DidEnterForeground);
	REQUIRE(lifecycle.phase() == AppLifecycle::Phase::Foreground);
	REQUIRE_FALSE(lifecycle.isSimPaused());
	REQUIRE_FALSE(lifecycle.isRenderingStopped());

	// and a second cycle behaves identically (the gates reset cleanly)
	const AppLifecycle::Actions second =
		lifecycle.handle(AppLifecycle::Event::WillEnterBackground);
	REQUIRE(second.flushSave);
	REQUIRE(second.suspendAudio);
	REQUIRE(lifecycle.isSimPaused());
}
