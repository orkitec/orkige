/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	TimerManagerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless timer-scheduler unit tests: after() fires once at the delay,
	every() repeats each period (with catch-up), cancel()/handle stop a
	timer, cancelOwner() drops a retired sandbox's timers WITHOUT firing them
	(the auto-cancel-on-retire the ScriptInstance destructor drives), and the
	scene-teardown clear() drops everything silently. The rendered end-to-end
	proof (Lua timer.after/every/cancel through the real loop) is the
	player_gameplay_selfcheck integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_tween/TimerManager.h>

TEST_CASE("TimerManager.after fires exactly once at the delay", "[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	int fired = 0;
	manager.after(1.0f, [&fired]() { ++fired; });

	// not yet due after half the delay
	manager.update(0.5f);
	REQUIRE(fired == 0);
	REQUIRE(manager.getActiveCount() == 1);
	// crosses the delay -> fires once, then is swept
	manager.update(0.6f);
	REQUIRE(fired == 1);
	REQUIRE(manager.getActiveCount() == 0);
	// never fires again
	manager.update(5.0f);
	REQUIRE(fired == 1);
}

TEST_CASE("TimerManager.after with a non-positive delay fires on the next update",
	"[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	int fired = 0;
	manager.after(0.0f, [&fired]() { ++fired; });
	manager.update(0.016f);
	REQUIRE(fired == 1);
}

TEST_CASE("TimerManager.every repeats on its period", "[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	int fired = 0;
	manager.every(0.2f, [&fired]() { ++fired; });

	// ~1.0s in 0.1s steps -> five periods elapse
	for(int i = 0; i < 10; ++i)
	{
		manager.update(0.1f);
	}
	REQUIRE(fired == 5);
	REQUIRE(manager.getActiveCount() == 1);	// still scheduled
}

TEST_CASE("TimerManager.every catches up missed periods in one big step",
	"[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	int fired = 0;
	manager.every(0.1f, [&fired]() { ++fired; });
	// one 0.55s frame spans five whole periods
	manager.update(0.55f);
	REQUIRE(fired == 5);
}

TEST_CASE("TimerManager cancel stops a timer before it fires", "[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	int afterFired = 0;
	int everyFired = 0;
	Orkige::TimerManager::TimerId a =
		manager.after(1.0f, [&afterFired]() { ++afterFired; });
	Orkige::TimerManager::TimerId e =
		manager.every(0.2f, [&everyFired]() { ++everyFired; });

	REQUIRE(manager.isActive(a));
	REQUIRE(manager.cancel(a));
	REQUIRE_FALSE(manager.isActive(a));
	REQUIRE_FALSE(manager.cancel(a));	// second cancel is a no-op

	// the repeating one runs until cancelled
	manager.update(0.25f);
	REQUIRE(everyFired == 1);
	REQUIRE(manager.cancel(e));
	manager.update(1.0f);
	REQUIRE(everyFired == 1);	// no more fires after cancel
	REQUIRE(afterFired == 0);	// the cancelled one-shot never fired
}

TEST_CASE("TimerManager cancelOwner drops a retired sandbox's timers without firing",
	"[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	// two owners plus one unowned timer
	int ownerAFired = 0;
	int ownerBFired = 0;
	int unownedFired = 0;
	// distinct non-null owner tokens (stand in for two ScriptInstance sandboxes)
	int sandboxA = 0;
	int sandboxB = 0;
	void const * ownerA = &sandboxA;
	void const * ownerB = &sandboxB;

	manager.after(0.5f, [&ownerAFired]() { ++ownerAFired; }, ownerA);
	manager.every(0.2f, [&ownerAFired]() { ++ownerAFired; }, ownerA);
	manager.after(0.5f, [&ownerBFired]() { ++ownerBFired; }, ownerB);
	manager.after(0.5f, [&unownedFired]() { ++unownedFired; });

	// retire sandbox A: its two timers are cancelled, nobody else's
	REQUIRE(manager.cancelOwner(ownerA) == 2);
	// a NULL owner matches nothing (unowned timers are never bulk-cancelled)
	REQUIRE(manager.cancelOwner(0) == 0);

	manager.update(1.0f);
	REQUIRE(ownerAFired == 0);	// retired before firing
	REQUIRE(ownerBFired == 1);	// the other sandbox still fires
	REQUIRE(unownedFired == 1);	// the unowned timer still fires
}

TEST_CASE("TimerManager clear drops every timer without firing", "[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	int fired = 0;
	manager.after(0.1f, [&fired]() { ++fired; });
	manager.every(0.1f, [&fired]() { ++fired; });
	REQUIRE(manager.getActiveCount() == 2);
	manager.clear();
	REQUIRE(manager.getActiveCount() == 0);
	manager.update(1.0f);
	REQUIRE(fired == 0);
}

TEST_CASE("TimerManager callbacks may schedule new timers (fire next update)",
	"[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TimerManager manager;

	int inner = 0;
	// a one-shot that, when it fires, schedules another one-shot
	manager.after(0.1f, [&manager, &inner]()
	{
		manager.after(0.1f, [&inner]() { ++inner; });
	});
	manager.update(0.2f);	// outer fires, inner scheduled (not yet due)
	REQUIRE(inner == 0);
	manager.update(0.2f);	// inner fires now
	REQUIRE(inner == 1);
}

TEST_CASE("TimerHandle is a no-op without a TimerManager (editor-inert)",
	"[unit][timer]")
{
	Orkige::CoreTestEnvironment::get();
	// no TimerManager exists in this scope
	Orkige::TimerHandle handle;
	handle.mId = 42;
	REQUIRE_FALSE(handle.isActive());
	REQUIRE_FALSE(handle.cancel());
}
