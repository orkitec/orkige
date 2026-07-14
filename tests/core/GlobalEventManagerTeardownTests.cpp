/**************************************************************
	created:	2026/07/14
	filename: 	GlobalEventManagerTeardownTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The "destroy a singleton that still holds live cross-object state" teardown
	regression pattern (see GameObjectManagerTeardownTests) applied to the
	GlobalEventManager: AppHost tears it down (uptr::reset()) while its listener
	registry can still hold bound listeners and its queues a pending event, with
	no prior drain/unbind. Today the EventManager destructor clears both cleanly
	and the EventListener destructor is trivial, so this passes; the test is a
	standing guard so a future re-entrant teardown (e.g. a listener that
	unregisters itself in its destructor, or the manager calling back into
	handlers on shutdown) is caught under AddressSanitizer instead of shipping.

	Deliberately standalone: it does NOT boot CoreTestEnvironment, so the
	GlobalEventManager singleton slot stays free and the test can own the one
	instance it constructs and destroys (per-case ctest isolation via
	catch_discover_tests; the skip guard covers the all-in-one-process run).
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_event/Event.h>
#include <core_event/EventListener.h>
#include <core_event/GlobalEventManager.h>

using Orkige::optr;

namespace
{
	//! a handler target that outlives the manager - teardown must never call
	//! back into it (a dangling delegate) nor read freed listener storage
	struct EventProbe
	{
		int calls = 0;
		bool onEvent(Orkige::Event const &)
		{
			++calls;
			return false;
		}
	};
}

TEST_CASE("GlobalEventManager destructor tears live listeners + a queued event "
	"down without a prior drain",
	"[unit][event][teardown]")
{
	// this test must OWN the sole GlobalEventManager instance to destroy it;
	// skip if another case in this process already booted the environment (only
	// possible in the all-in-one-process raw-binary run, not per-case ctest).
	if(Orkige::GlobalEventManager::getSingletonPtr() != nullptr)
	{
		SKIP("a GlobalEventManager singleton is already live in this process");
	}

	// heap-owned, exactly like AppHost's uptr<GlobalEventManager>: the teardown
	// under test is the destructor reached through delete
	Orkige::GlobalEventManager * manager = new Orkige::GlobalEventManager();

	EventProbe probe;
	const Orkige::EventType eventType("test.teardown.event");
	{
		// once this local strong ref drops, the manager's registry holds the
		// ONLY owner of the listener - so teardown destroys it during shutdown
		optr<Orkige::EventListener> listener =
			manager->bind(eventType, &EventProbe::onEvent, &probe);
		REQUIRE(listener);
		// non-vacuous: the binding really is wired (else the test proves nothing)
		manager->trigger(Orkige::Event(eventType));
		REQUIRE(probe.calls == 1);
	}
	// an undrained queued event leaves live state in the queue at teardown too
	manager->queueEvent(Orkige::onew(new Orkige::Event(eventType)));

	// destroy with a live listener + a pending event, no drain/unbind first.
	// A clean ASan exit is the pass; a re-entrant teardown regression trips here.
	delete manager;

	// the singleton slot is free again, and teardown never fired the handler
	CHECK(Orkige::GlobalEventManager::getSingletonPtr() == nullptr);
	CHECK(probe.calls == 1);
}
