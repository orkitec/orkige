/**************************************************************
	created:	2026/07/14
	filename: 	GameObjectManagerTeardownTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Destroying a GameObjectManager that still holds live objects - the AppHost
	shutdown path (uptr<GameObjectManager>::reset()), a scene that never
	funnelled through the clear() teardown hook - must not touch freed memory.
	updatableComponents is declared before the objects map, so plain member
	destruction frees the update list FIRST; the objects map (destroyed last)
	then destroys its GameObjects, whose component removal re-enters
	GameObjectManager::disableUpdates() and std::find()s over the freed update
	vector. This test builds exactly that world - an object with an
	update-wanting component, no clear() before teardown - and destroys the
	manager, so a regression is caught under AddressSanitizer.

	Platform note: the raw dangling read reproduces the crash under libstdc++
	(the Linux ASan CI job, where this was first caught), whose ~vector frees
	the buffer while leaving size non-zero. libc++ (macOS) empties the vector in
	~vector before deallocating, so std::find sees begin()==end() and the read
	is skipped - the bug is latent there but this test still exercises the whole
	teardown path cleanly. The fix (the destructor runs the clear() hook up
	front) makes it correct on every standard library.

	Deliberately standalone: it does NOT boot CoreTestEnvironment, so the
	GameObjectManager singleton slot stays free and the test can own the one
	instance it constructs and destroys. Every TEST_CASE is registered as its
	own ctest process (catch_discover_tests), so no other case has booted the
	environment in this process; the raw-binary all-in-one-process run is
	handled by the skip guard below.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "TestComponents.h"

#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>

using Orkige::optr;

TEST_CASE("GameObjectManager destructor tears a populated world down without a prior clear()",
	"[unit][gameobject][teardown]")
{
	// register ONLY the component factory entry (backend-neutral, no scripting
	// usertype export), so this process never boots a Lua state / the
	// environment and the singleton slot stays free
	if(!Orkige::GameObject::isComponentRegistered<
		Orkige::TestActivationProbeComponent>())
	{
		Orkige::GameObject::registerComponent<
			Orkige::TestActivationProbeComponent>();
	}
	REQUIRE(Orkige::GameObject::isComponentRegistered<
		Orkige::TestActivationProbeComponent>());

	// this test must OWN the sole GameObjectManager instance to destroy it.
	// When another case in this process already booted a *TestEnvironment (only
	// possible when the whole binary is run in one process, not under the
	// per-case ctest isolation), the singleton slot is taken - skip rather than
	// trip the single-instance assertion.
	if(Orkige::GameObjectManager::getSingletonPtr() != nullptr)
	{
		SKIP("a GameObjectManager singleton is already live in this process");
	}

	// heap-owned, exactly like AppHost's uptr<GameObjectManager>: the teardown
	// under test is the destructor reached through delete
	Orkige::GameObjectManager* manager = new Orkige::GameObjectManager();

	// an object whose component wants updates, so it lands in the manager's
	// updatableComponents list (numUpdatableComponents becomes non-zero) - the
	// precondition that makes teardown re-enter disableUpdates()
	{
		optr<Orkige::GameObject> object =
			manager->createGameObject("teardownObject").lock();
		REQUIRE(object);
		// TestActivationProbeComponent sets wantsUpdates -> enableUpdates
		REQUIRE(object->addComponent<Orkige::TestActivationProbeComponent>());
		Orkige::TestActivationProbeComponent* probe =
			object->getComponentPtr<Orkige::TestActivationProbeComponent>();
		// confirm the component really is on the update list (else the test
		// would be vacuous): one tick calls it exactly once
		manager->update(0.016f);
		REQUIRE(probe->updateCalls == 1);
	}
	// the local strong reference is gone: the manager's object map now holds the
	// ONLY owner, so destroying the manager destroys the object and re-enters
	// disableUpdates() during teardown - the failing path

	// destroy WITHOUT a prior clear(). Pre-fix: member destruction frees
	// updatableComponents before the objects map and the object destructor's
	// disableUpdates() reads the freed vector (ASan heap-use-after-free under
	// libstdc++). Post-fix: the destructor's clear() detaches the update list
	// first, so this is clean everywhere.
	delete manager;

	// reaching here (and a clean ASan exit) is the pass: the singleton slot is
	// free again for any later test in this process
	CHECK(Orkige::GameObjectManager::getSingletonPtr() == nullptr);
}
