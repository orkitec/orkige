/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	EventTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_event/GlobalEventManager.h>
#include <core_base/Object.h>

#include <vector>

using Orkige::optr;
using Orkige::woptr;

namespace
{
	//! member-function (FastDelegate MakeDelegate path) event probe
	struct EventProbe
	{
		int callCount = 0;
		bool consume = false;
		Orkige::String lastPayloadId;
		int lastPayloadValue = 0;

		bool onEvent(Orkige::Event const & event)
		{
			++this->callCount;
			if(event.getData())
			{
				this->lastPayloadId = event.getData()->getObjectID();
				if(event.getData()->hasAttribute("value"))
				{
					this->lastPayloadValue =
						event.getData()->getAttribute<int>("value");
				}
			}
			return this->consume;
		}
	};

	//! free-function (FastDelegate static-function path) state
	int freeFunctionCallCount = 0;
	bool freeFunctionHandler(Orkige::Event const & event)
	{
		++freeFunctionCallCount;
		return false;
	}

	//! records the order listeners fire in for the priority test
	std::vector<int> priorityFireOrder;
	struct PriorityProbe
	{
		int tag;
		explicit PriorityProbe(int t) : tag(t) {}
		bool onEvent(Orkige::Event const & event)
		{
			priorityFireOrder.push_back(this->tag);
			return false;
		}
	};
}

TEST_CASE("GlobalEventManager bind/trigger/unbind delivers exactly to bound listeners", "[events]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::GlobalEventManager & manager =
		Orkige::GlobalEventManager::getSingleton();

	EventProbe probe;
	const Orkige::EventType type("unit_test_event");
	optr<Orkige::EventListener> listener =
		manager.bind(type, &EventProbe::onEvent, &probe);
	REQUIRE(listener);

	// non-consuming handler: delivered, trigger reports not consumed
	CHECK_FALSE(manager.trigger(Orkige::Event(type)));
	CHECK(probe.callCount == 1);

	// consuming handler: trigger reports consumption
	probe.consume = true;
	CHECK(manager.trigger(Orkige::Event(type)));
	CHECK(probe.callCount == 2);

	// an unrelated event type must not reach the listener
	CHECK_FALSE(manager.trigger(Orkige::Event(
		Orkige::EventType("unit_test_other_event"))));
	CHECK(probe.callCount == 2);

	// after unbinding nothing is delivered anymore
	REQUIRE(manager.delListener(listener, type));
	CHECK_FALSE(manager.trigger(Orkige::Event(type)));
	CHECK(probe.callCount == 2);
	// removing twice fails
	CHECK_FALSE(manager.delListener(listener, type));
}

TEST_CASE("Event data payload reaches the listener intact", "[events]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::GlobalEventManager & manager =
		Orkige::GlobalEventManager::getSingleton();

	EventProbe probe;
	const Orkige::EventType type("unit_test_payload_event");
	optr<Orkige::EventListener> listener =
		manager.bind(type, &EventProbe::onEvent, &probe);

	optr<Orkige::Object> payload =
		Orkige::onew(new Orkige::Object("payload_object"));
	payload->setAttribute("value", 1337);
	manager.trigger(Orkige::Event(type, payload));

	CHECK(probe.callCount == 1);
	CHECK(probe.lastPayloadId == "payload_object");
	CHECK(probe.lastPayloadValue == 1337);

	REQUIRE(manager.delListener(listener, type));
}

TEST_CASE("FastDelegate free-function handlers work through addListener", "[events]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::GlobalEventManager & manager =
		Orkige::GlobalEventManager::getSingleton();

	freeFunctionCallCount = 0;
	const Orkige::EventType type("unit_test_free_function_event");
	optr<Orkige::EventListener> listener = Orkige::createEventListenerPtr(
		Orkige::EventHandlerFunction(&freeFunctionHandler));
	REQUIRE(manager.addListener(listener, type));

	manager.trigger(Orkige::Event(type));
	CHECK(freeFunctionCallCount == 1);

	REQUIRE(manager.delListener(listener, type));
	manager.trigger(Orkige::Event(type));
	CHECK(freeFunctionCallCount == 1);
}

TEST_CASE("Listeners fire ordered by ascending priority", "[events]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::GlobalEventManager & manager =
		Orkige::GlobalEventManager::getSingleton();

	priorityFireOrder.clear();
	const Orkige::EventType type("unit_test_priority_event");
	PriorityProbe late(2);
	PriorityProbe early(1);
	// add the HIGH priority value first - the sort must reorder them
	optr<Orkige::EventListener> lateListener = Orkige::createEventListenerPtr(
		&PriorityProbe::onEvent, &late, (signed short)10);
	optr<Orkige::EventListener> earlyListener = Orkige::createEventListenerPtr(
		&PriorityProbe::onEvent, &early, (signed short)-10);
	REQUIRE(manager.addListener(lateListener, type));
	REQUIRE(manager.addListener(earlyListener, type));

	manager.trigger(Orkige::Event(type));
	REQUIRE(priorityFireOrder.size() == 2);
	CHECK(priorityFireOrder[0] == 1);
	CHECK(priorityFireOrder[1] == 2);

	REQUIRE(manager.delListener(lateListener, type));
	REQUIRE(manager.delListener(earlyListener, type));
}

TEST_CASE("queueEvent defers delivery until tick", "[events]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::GlobalEventManager & manager =
		Orkige::GlobalEventManager::getSingleton();

	EventProbe probe;
	const Orkige::EventType type("unit_test_queued_event");

	// queueing an event nobody listens to is rejected
	CHECK_FALSE(manager.queueEvent(Orkige::onew(new Orkige::Event(type))));

	optr<Orkige::EventListener> listener =
		manager.bind(type, &EventProbe::onEvent, &probe);

	REQUIRE(manager.queueEvent(Orkige::onew(new Orkige::Event(type))));
	CHECK(probe.callCount == 0);	// not delivered synchronously
	REQUIRE(manager.tick());
	CHECK(probe.callCount == 1);	// delivered by tick
	REQUIRE(manager.tick());		// empty tick delivers nothing new
	CHECK(probe.callCount == 1);

	REQUIRE(manager.delListener(listener, type));
}
