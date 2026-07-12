/**************************************************************
	created:	2026/07/12 at 15:00
	filename: 	ScriptEventBusTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The script MESSAGE BUS (core_script/ScriptEventBus): subscribe / emit /
	cancel, the deterministic queue-then-drain delivery rule (subscription
	order, same-phase re-entrancy with a cap), the bounded payload conversion,
	and SANDBOX SCOPING (a retired ScriptInstance auto-cancels its
	subscriptions). Compiles in every scripting configuration: without a
	backend the bus is inert and the behaviour cases skip. The multi-kind
	(several script components on one object) proof lives in tests/engine.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptEventBus.h>
#include <core_script/ScriptEventPayload.h>
#include <core_event/GlobalEventManager.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

using Orkige::optr;
using Orkige::String;
using Orkige::ScriptRuntime;
using Orkige::ScriptEventBus;
using Orkige::GlobalEventManager;
using Orkige::ScriptEventPayload;
using Orkige::ScriptCallback;
using Orkige::ScriptArgs;
using Orkige::EventSubscription;
using Orkige::ScriptInstance;

namespace
{
	//! register a minimal `events` Lua table (the same two seam calls the engine
	//! wires in ScriptComponent::ensureScriptApi) so the pure-core suite can
	//! drive the full Lua flow; idempotent
	void ensureEventsApi()
	{
		ScriptRuntime & runtime = ScriptRuntime::getSingleton();
		runtime.ensureGlobalTable("shared");
		if (runtime.hasGlobalTable("events"))
		{
			return;
		}
		runtime.registerFunction("events", "subscribe",
			[](String const & name, ScriptArgs args) -> EventSubscription
		{
			EventSubscription handle;
			handle.mId = ScriptEventBus::getSingleton().subscribe(
				name, ScriptCallback::fromArgs(args, 0));
			return handle;
		});
		runtime.registerFunction("events", "emit",
			[](String const & name, ScriptArgs args)
		{
			ScriptRuntime::getSingleton().emitEventFromScript(name, args);
		});
	}
	//! the behaviour cases need a live backend; without one they pass trivially
	bool scriptingAvailable()
	{
		if (ScriptRuntime::available())
		{
			return true;
		}
		SUCCEED("scripting disabled - bus behaviour test skipped");
		return false;
	}
	//! a fresh bus + a cleared `shared` scratch table for each case
	void resetForCase()
	{
		ScriptEventBus::getSingleton().clear();
		ScriptRuntime::getSingleton().runString(
			"for k in pairs(shared) do shared[k] = nil end");
	}
	//! run a chunk; FAIL with the error when it does not
	void run(String const & code)
	{
		const ScriptRuntime::Result result =
			ScriptRuntime::getSingleton().runString(code);
		if (!result.success)
		{
			FAIL("script error: " << result.error);
		}
	}
	double sharedNumber(String const & key)
	{
		return ScriptRuntime::getSingleton().getNumber({ "shared", key }, -1.0);
	}
	String sharedString(String const & key)
	{
		return ScriptRuntime::getSingleton().getString({ "shared", key }, "");
	}

	//! a plain C++ listener on GlobalEventManager (a FastDelegate handler, no
	//! scripting) that reads the ScriptEventData payload an event carries - the
	//! proof that C++ and Lua share the ONE bus
	struct BusCatcher
	{
		int		count = 0;
		String	lastId;
		bool onEvent(Orkige::Event const & event)
		{
			++this->count;
			optr<Orkige::ScriptEventData> data =
				std::dynamic_pointer_cast<Orkige::ScriptEventData>(event.getData());
			if (data)
			{
				for (auto const & field : data->payload().fields)
				{
					if (!field.first.isIndex && field.first.name == "id" &&
						!field.second.isTable)
					{
						this->lastId = field.second.scalar.stringValue;
					}
				}
			}
			return false;	// do not consume - other listeners still run
		}
	};
}

TEST_CASE("ScriptEventBus delivers on tick, not on emit", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	run("shared.count = 0\n"
		"events.subscribe('ping', function(e) shared.count = shared.count + 1 end)\n"
		"events.emit('ping')\n");
	// emission QUEUES onto GlobalEventManager: the handler has not run yet
	CHECK(sharedNumber("count") == 0.0);

	// the engine bus tick (the player loop's script phase) delivers it
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("count") == 1.0);
}

TEST_CASE("ScriptEventBus calls handlers in subscription order", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	run("shared.order = ''\n"
		"events.subscribe('go', function(e) shared.order = shared.order .. 'A' end)\n"
		"events.subscribe('go', function(e) shared.order = shared.order .. 'B' end)\n"
		"events.subscribe('go', function(e) shared.order = shared.order .. 'C' end)\n"
		"events.emit('go')\n");
	CHECK(ScriptEventBus::getSingleton().subscriberCount("go") == 3u);
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedString("order") == "ABC");
}

TEST_CASE("ScriptEventBus carries a bounded payload to the handler", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	run("shared.id = ''\n"
		"shared.n = 0\n"
		"shared.flag = false\n"
		"shared.px = 0\n"
		"shared.py = 0\n"
		"events.subscribe('hit', function(e)\n"
		"  shared.id = e.id\n"
		"  shared.n = e.count\n"
		"  shared.flag = e.on\n"
		"  shared.px = e.pos[1]\n"		// nested array survives one level
		"  shared.py = e.pos[2]\n"
		"end)\n"
		"events.emit('hit', { id = 'Widget', count = 7, on = true, pos = {3, 4} })\n");
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedString("id") == "Widget");
	CHECK(sharedNumber("n") == 7.0);
	CHECK(ScriptRuntime::getSingleton().getBool({ "shared", "flag" }, false));
	CHECK(sharedNumber("px") == 3.0);
	CHECK(sharedNumber("py") == 4.0);
}

TEST_CASE("ScriptEventBus rejects an out-of-bounds payload at emit", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	run("shared.got = 0\n"
		"events.subscribe('bad', function(e) shared.got = shared.got + 1 end)\n");
	// a function value cannot ride the bus - emit must raise at the call site
	ScriptRuntime::Result result = ScriptRuntime::getSingleton().runString(
		"events.emit('bad', { fn = function() end })");
	CHECK_FALSE(result.success);
	// a table nested deeper than one level is out of bounds too
	result = ScriptRuntime::getSingleton().runString(
		"events.emit('bad', { deep = { inner = {1} } })");
	CHECK_FALSE(result.success);
	// nothing was queued or delivered
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("got") == 0.0);
}

TEST_CASE("ScriptEventBus re-entrant emit defers to the next tick (buffer swap)", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	// a handler that re-emits its own event. GlobalEventManager's double-buffered
	// queue is the cascade-safety: the re-emit lands in the OPPOSING buffer, so a
	// handler runs at most ONCE per tick and can never recurse into the same
	// drain. Each tick delivers exactly one generation. The handler self-limits
	// so the cascade terminates.
	run("shared.n = 0\n"
		"events.subscribe('loop', function(e)\n"
		"  shared.n = shared.n + 1\n"
		"  if shared.n < 3 then events.emit('loop') end\n"
		"end)\n"
		"events.emit('loop')\n");
	// ONE tick delivers exactly one generation (no same-tick recursion) - THE
	// cascade-safety proof the heritage queue provides
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("n") == 1.0);
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("n") == 2.0);
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("n") == 3.0);	// the handler stopped re-emitting
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("n") == 3.0);	// nothing left to deliver
}

TEST_CASE("ScriptEventBus cancels a subscription by handle", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	run("shared.n = 0\n"
		"shared.h = events.subscribe('t', function(e) shared.n = shared.n + 1 end)\n"
		"events.emit('t')\n");
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("n") == 1.0);

	run("shared.h:cancel()\n"
		"events.emit('t')\n");
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("n") == 1.0);	// no further delivery after cancel
	CHECK(ScriptEventBus::getSingleton().subscriberCount("t") == 0u);
}

TEST_CASE("ScriptEventBus emits engine payloads from C++", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	run("shared.a = ''\n"
		"events.subscribe('physics.contactBegin', function(e) shared.a = e.a .. '|' .. e.b end)\n");
	// a C++ producer builds the bounded payload directly (no scripting side)
	ScriptEventPayload payload;
	payload.setString("a", "Ball");
	payload.setString("b", "Floor");
	ScriptEventBus::getSingleton().emit("physics.contactBegin", payload);
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedString("a") == "Ball|Floor");
}

TEST_CASE("ScriptEventBus IS the engine bus: a C++ listener hears a script emit", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	// a PLAIN C++ FastDelegate listener binds directly to GlobalEventManager for
	// the same event name a script emits - the unification proof: they share the
	// ONE bus, this is not a parallel mechanism
	BusCatcher catcher;
	optr<EventListener> listener = GlobalEventManager::getSingleton().bind(
		EventType("game.custom"), &BusCatcher::onEvent, &catcher);

	run("events.emit('game.custom', { id = 'hero' })");
	GlobalEventManager::getSingleton().tick();
	CHECK(catcher.count == 1);
	CHECK(catcher.lastId == "hero");	// the C++ side read the script's payload

	GlobalEventManager::getSingleton().delListener(listener,
		EventType("game.custom"));
}

TEST_CASE("ScriptEventBus IS the engine bus: a script handler hears a C++ emit", "[events]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	// a script subscribes; a C++ system queues the SAME-named event straight onto
	// GlobalEventManager (carrying a ScriptEventData payload) - the reverse
	// direction of the shared bus
	run("shared.got = 0\n"
		"events.subscribe('cpp.signal', function(e) shared.got = e.n end)\n");
	ScriptEventPayload payload;
	payload.setNumber("n", 5);
	GlobalEventManager::getSingleton().queueEvent(onew(new Event(
		EventType("cpp.signal"), onew(new ScriptEventData(payload)))));
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("got") == 5.0);
}

TEST_CASE("ScriptEventBus auto-cancels a retired sandbox's subscriptions", "[events][sandbox]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	// write a script that subscribes in init(); loading + init'ing an instance
	// tags the subscription with THAT sandbox (ScriptCallScope in callInit)
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "orkige_eventbus_sandbox_test";
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
	std::filesystem::create_directories(dir / "scripts");
	{
		std::ofstream file(dir / "scripts" / "sub.lua");
		file <<
			"function init(self)\n"
			"  events.subscribe('world', function(e)\n"
			"    shared.hits = (shared.hits or 0) + 1\n"
			"  end)\n"
			"end\n";
	}
	ScriptRuntime::getSingleton().setScriptSearchRoot(dir.string());
	String error;
	optr<ScriptInstance> instance =
		ScriptRuntime::getSingleton().loadScriptInstance("scripts/sub.lua", &error);
	REQUIRE(instance);
	REQUIRE(instance->callInit(&error));
	CHECK(ScriptEventBus::getSingleton().subscriberCount("world") == 1u);

	// the subscription is live and delivers
	ScriptEventBus::getSingleton().emit("world", ScriptEventPayload());
	GlobalEventManager::getSingleton().tick();
	CHECK(sharedNumber("hits") == 1.0);

	// retiring the sandbox (dropping the instance) must cancel its subscription
	instance.reset();
	CHECK(ScriptEventBus::getSingleton().subscriberCount("world") == 0u);

	ScriptRuntime::getSingleton().setScriptSearchRoot("");
	std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("ScriptEventBus trace capture records emitted events", "[events][trace]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	// pure C++ path - no scripting backend needed (the trace recorder folds
	// these into record_trace's event stream)
	ScriptEventBus & bus = ScriptEventBus::getSingleton();
	bus.clear();

	bus.setTraceCapture(true);
	ScriptEventPayload payload;
	payload.setString("id", "hero");
	payload.setNumber("hp", 5);
	// capture records the emission even with NO subscribers (the trace is a
	// readback of what happened, not of what was delivered)
	bus.emit("game.custom", payload);

	std::vector<ScriptEventBus::FrameEvent> events = bus.takeFrameEvents();
	REQUIRE(events.size() == 1u);
	CHECK(events[0].name == "game.custom");
	// top-level scalars flatten to (name, string) fields
	bool sawId = false, sawHp = false;
	for (std::pair<String, String> const & field : events[0].fields)
	{
		if (field.first == "id" && field.second == "hero") { sawId = true; }
		if (field.first == "hp" && field.second == "5") { sawHp = true; }
	}
	CHECK(sawId);
	CHECK(sawHp);
	// the take drained the buffer
	CHECK(bus.takeFrameEvents().empty());

	// capture OFF: emit records nothing (zero hot-path cost when not recording)
	bus.setTraceCapture(false);
	bus.emit("game.custom", payload);
	CHECK(bus.takeFrameEvents().empty());
	bus.clear();
}

TEST_CASE("ScriptEventBus stress: 200 subscribers, 1000 emits/frame", "[events][perf]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	if (!scriptingAvailable()) { return; }
	ensureEventsApi();
	resetForCase();

	// 200 subscribers spread over 20 event names (10 each): the per-name map
	// keeps dispatch O(subscribers of the emitted name), never a global scan
	const int kNames = 20;
	const int kSubsPerName = 10;
	const int kEmitsPerFrame = 1000;
	const int kFrames = 30;
	run("shared.n = 0");
	for (int name = 0; name < kNames; ++name)
	{
		for (int s = 0; s < kSubsPerName; ++s)
		{
			run("events.subscribe('ev" + std::to_string(name) +
				"', function(e) shared.n = shared.n + 1 end)");
		}
	}
	CHECK(ScriptEventBus::getSingleton().subscriberCount("ev0") ==
		static_cast<std::size_t>(kSubsPerName));

	ScriptEventBus & bus = ScriptEventBus::getSingleton();
	long long totalDispatches = 0;
	double bestFrameMs = 1.0e30;
	double worstFrameMs = 0.0;
	double totalMs = 0.0;
	for (int frame = 0; frame < kFrames; ++frame)
	{
		const auto start = std::chrono::steady_clock::now();
		// emit 1000 events this frame across the name set (payload with two ids,
		// the realistic engine-mirror shape), then drain in the script phase
		for (int e = 0; e < kEmitsPerFrame; ++e)
		{
			ScriptEventPayload payload;
			payload.setNumber("i", e);
			bus.emit("ev" + std::to_string(e % kNames), payload);
		}
		GlobalEventManager::getSingleton().tick();
		const auto end = std::chrono::steady_clock::now();
		const double ms =
			std::chrono::duration<double, std::milli>(end - start).count();
		totalMs += ms;
		bestFrameMs = ms < bestFrameMs ? ms : bestFrameMs;
		worstFrameMs = ms > worstFrameMs ? ms : worstFrameMs;
		totalDispatches += static_cast<long long>(kEmitsPerFrame) * kSubsPerName;
	}
	// each emit reaches kSubsPerName handlers -> total handler invocations
	const double avgFrameMs = totalMs / kFrames;
	const double nsPerDispatch =
		totalDispatches > 0 ? (totalMs * 1.0e6) / static_cast<double>(totalDispatches)
		: 0.0;
	std::printf("[events][perf] %d emits/frame x %d subscribers each: "
		"avg %.3f ms/frame (best %.3f, worst %.3f), %.0f ns per handler "
		"dispatch (%lld dispatches over %d frames)\n",
		kEmitsPerFrame, kSubsPerName, avgFrameMs, bestFrameMs, worstFrameMs,
		nsPerDispatch, totalDispatches, kFrames);
	std::fflush(stdout);
	// correctness alongside the timing: every dispatch ran its handler
	CHECK(sharedNumber("n") == static_cast<double>(totalDispatches));
	// a sane frame budget backstop (generous - this is a stress far above any
	// real game's event volume); the printed numbers are the real deliverable
	CHECK(avgFrameMs < 100.0);
}
