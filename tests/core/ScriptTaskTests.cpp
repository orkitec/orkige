/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	ScriptTaskTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Tests of SCRIPT TASKS - the across-frames control flow behind
	`script.async` and the waits. Three layers, and only the last needs an
	interpreter: the PURE wait/budget decisions, the scheduler driven through
	a FAKE backend (so start / yield / complete / cancel / retire / teardown /
	time-out are all provable with no Lua at all, in every build
	configuration), and the real coroutine road through the test tier.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_filesystem/ResourceReader.h>
#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptTaskCore.h>
#include <core_script/ScriptTaskManager.h>
#include <core_script/ScriptTestReport.h>

#include <map>
#include <vector>

namespace
{
	//! @brief a scripted stand-in for a coroutine: it yields the waits it was
	//! given, in order, then finishes. No interpreter involved, so the whole
	//! scheduler is provable in an ORKIGE_SCRIPTING=OFF build too.
	struct FakeCoroutine
	{
		std::vector<Orkige::ScriptTaskWait>	waits;
		std::size_t							step = 0;
		int									resumes = 0;
		bool								fails = false;
		bool								conditionReady = false;
		bool								conditionRaises = false;
		int									conditionAsks = 0;
		bool								released = false;
	};

	//! the fake backend + its slots, installed on one manager
	struct FakeBackend
	{
		std::map<int, FakeCoroutine> slots;

		int add(FakeCoroutine coroutine)
		{
			const int slot = static_cast<int>(this->slots.size()) + 1;
			this->slots[slot] = coroutine;
			return slot;
		}

		void installOn(Orkige::ScriptTaskManager & manager)
		{
			FakeBackend * self = this;
			Orkige::ScriptTaskManager::Backend backend;
			backend.resume = [self](int slot, void const *,
				Orkige::ScriptTaskWait & outWait, Orkige::String & outError)
			{
				FakeCoroutine & coroutine = self->slots[slot];
				++coroutine.resumes;
				if(coroutine.fails)
				{
					outError = "the task raised";
					return Orkige::ScriptTaskStep::Failed;
				}
				if(coroutine.step >= coroutine.waits.size())
				{
					return Orkige::ScriptTaskStep::Finished;
				}
				outWait = coroutine.waits[coroutine.step++];
				return Orkige::ScriptTaskStep::Yielded;
			};
			backend.condition = [self](int slot, void const *, bool & outReady,
				Orkige::String & outError) -> bool
			{
				FakeCoroutine & coroutine = self->slots[slot];
				++coroutine.conditionAsks;
				if(coroutine.conditionRaises)
				{
					outError = "the condition raised";
					return false;
				}
				outReady = coroutine.conditionReady;
				return true;
			};
			backend.release = [self](int slot)
			{
				self->slots[slot].released = true;
			};
			manager.setBackend(backend);
		}
	};

	//! a fake in-test reader: test files by resource name, nothing on disk
	class TaskReader : public Orkige::ResourceReader
	{
	public:
		std::map<Orkige::String, Orkige::String> files;

		bool readText(Orkige::String const & name,
			Orkige::String & out) const override
		{
			std::map<Orkige::String, Orkige::String>::const_iterator it =
				this->files.find(name);
			if(it == this->files.end())
			{
				return false;
			}
			out = it->second;
			return true;
		}
	};

	struct InstalledReader
	{
		explicit InstalledReader(Orkige::ResourceReader * reader)
		{
			Orkige::ResourceAccess::setReader(reader);
		}
		~InstalledReader()
		{
			Orkige::ResourceAccess::setReader(nullptr);
		}
	};
}

TEST_CASE("a wait counts itself down and then resumes", "[script][task]")
{
	using namespace Orkige;

	// a bare yield comes back on the very next tick - the earliest a task
	// that suspended itself can possibly continue
	ScriptTaskWait tick = ScriptTaskCore::waitTick();
	CHECK(ScriptTaskCore::pollWait(tick, 0.016f) == ScriptTaskVerdict::Resume);

	// seconds: the SCALED delta is subtracted until the wait is spent
	ScriptTaskWait seconds = ScriptTaskCore::waitSeconds(0.05);
	CHECK(ScriptTaskCore::pollWait(seconds, 0.02f) == ScriptTaskVerdict::Hold);
	CHECK(ScriptTaskCore::pollWait(seconds, 0.02f) == ScriptTaskVerdict::Hold);
	CHECK(ScriptTaskCore::pollWait(seconds, 0.02f) == ScriptTaskVerdict::Resume);
	// a hitstop (delta 0) never advances a wait
	ScriptTaskWait frozen = ScriptTaskCore::waitSeconds(0.05);
	CHECK(ScriptTaskCore::pollWait(frozen, 0.0f) == ScriptTaskVerdict::Hold);

	// a non-positive delay cannot mean "right now" - it is the next tick
	ScriptTaskWait immediate = ScriptTaskCore::waitSeconds(-1.0);
	CHECK(immediate.kind == ScriptWaitKind::Tick);

	// ticks: exactly that many
	ScriptTaskWait ticks = ScriptTaskCore::waitTicks(3.0);
	CHECK(ScriptTaskCore::pollWait(ticks, 0.016f) == ScriptTaskVerdict::Hold);
	CHECK(ScriptTaskCore::pollWait(ticks, 0.016f) == ScriptTaskVerdict::Hold);
	CHECK(ScriptTaskCore::pollWait(ticks, 0.016f) == ScriptTaskVerdict::Resume);
	CHECK(ScriptTaskCore::waitTicks(0.0).kind == ScriptWaitKind::Tick);

	// a condition is never decided here: only the condition function knows
	ScriptTaskWait condition = ScriptTaskCore::waitCondition(0.0);
	CHECK(ScriptTaskCore::pollWait(condition, 0.016f) ==
		ScriptTaskVerdict::AskCondition);
	// ...and an unlimited one waits forever rather than giving up
	CHECK_FALSE(ScriptTaskCore::conditionGaveUp(condition));
	CHECK_FALSE(ScriptTaskCore::conditionGaveUp(condition));

	// a LIMITED condition gives up after its declared budget
	ScriptTaskWait limited = ScriptTaskCore::waitCondition(2.0);
	CHECK_FALSE(ScriptTaskCore::conditionGaveUp(limited));
	CHECK(ScriptTaskCore::conditionGaveUp(limited));
	CHECK(ScriptTaskCore::conditionGiveUpMessage(2).find("2 frames") !=
		Orkige::String::npos);
}

TEST_CASE("a task budget is unlimited for a game and bounded for a test",
	"[script][task]")
{
	using namespace Orkige;
	// 0 = unbudgeted: a game task waiting for a door to open is not a bug
	CHECK_FALSE(ScriptTaskCore::budgetSpent(100000, 0));
	CHECK_FALSE(ScriptTaskCore::budgetSpent(599, 600));
	CHECK(ScriptTaskCore::budgetSpent(600, 600));
	CHECK(ScriptTaskCore::defaultTestTickLimit() > 0);
	CHECK(ScriptTaskCore::budgetSpentMessage(600).find("600 frames") !=
		Orkige::String::npos);
}

TEST_CASE("a task is resumed only by the scheduler, one step per tick",
	"[script][task]")
{
	using namespace Orkige;
	ScriptTaskManager manager;
	FakeBackend backend;
	backend.installOn(manager);

	FakeCoroutine coroutine;
	coroutine.waits.push_back(ScriptTaskCore::waitTicks(2.0));
	const int slot = backend.add(coroutine);
	const ScriptTaskManager::TaskId task = manager.start(slot, 0, 0);
	REQUIRE(task != 0);

	// STARTING IS NOT RUNNING: nothing is resumed inside the call that
	// started it - the one resume site owns that, so starting a task from a
	// script never re-enters the script
	CHECK(backend.slots[slot].resumes == 0);
	CHECK(manager.isActive(task));

	manager.update(0.016f);		// first resume: the body yields a 2-tick wait
	CHECK(backend.slots[slot].resumes == 1);
	manager.update(0.016f);		// the wait holds
	CHECK(backend.slots[slot].resumes == 1);
	manager.update(0.016f);		// the wait is over: resumed, and it finishes
	CHECK(backend.slots[slot].resumes == 2);

	CHECK(manager.statusOf(task) == ScriptTaskStatus::Completed);
	CHECK_FALSE(manager.isActive(task));
	// the coroutine is released as the task lands - nothing lingers
	CHECK(backend.slots[slot].released);
	// the verdict stays readable for the frame it landed on, and is swept at
	// the START of the next tick
	manager.update(0.016f);
	CHECK(manager.statusOf(task) == ScriptTaskStatus::Unknown);
}

TEST_CASE("a task waits on a condition and gives up when it never comes true",
	"[script][task]")
{
	using namespace Orkige;
	ScriptTaskManager manager;
	FakeBackend backend;
	backend.installOn(manager);

	FakeCoroutine coroutine;
	coroutine.waits.push_back(ScriptTaskCore::waitCondition(0.0));
	const int waiting = backend.add(coroutine);
	const ScriptTaskManager::TaskId task = manager.start(waiting, 0, 0);
	manager.update(0.016f);		// the body yields the condition wait
	manager.update(0.016f);		// asked: not ready
	manager.update(0.016f);		// asked again: still not ready
	CHECK(backend.slots[waiting].conditionAsks == 2);
	CHECK(manager.isActive(task));
	backend.slots[waiting].conditionReady = true;
	manager.update(0.016f);		// ready: resumed, and the body finishes
	CHECK(manager.statusOf(task) == ScriptTaskStatus::Completed);

	// a LIMITED condition that never comes true fails by name
	FakeCoroutine limited;
	limited.waits.push_back(ScriptTaskCore::waitCondition(2.0));
	const int slot = backend.add(limited);
	const ScriptTaskManager::TaskId limitedTask = manager.start(slot, 0, 0);
	manager.update(0.016f);
	manager.update(0.016f);
	manager.update(0.016f);
	CHECK(manager.statusOf(limitedTask) == ScriptTaskStatus::Failed);
	CHECK(manager.errorOf(limitedTask).find("never became true") !=
		Orkige::String::npos);
}

TEST_CASE("a task budget makes a wedged wait a named failure", "[script][task]")
{
	using namespace Orkige;
	ScriptTaskManager manager;
	FakeBackend backend;
	backend.installOn(manager);

	FakeCoroutine coroutine;
	// a wait that is never satisfied - the shape a hung test would have
	coroutine.waits.push_back(ScriptTaskCore::waitSeconds(1000.0));
	const int slot = backend.add(coroutine);
	const ScriptTaskManager::TaskId task = manager.start(slot, 0, 4);
	for(int tick = 0; tick < 8 && manager.isActive(task); ++tick)
	{
		manager.update(0.016f);
	}
	// NOT a hang and NOT a silent pass: the task landed, named, on its budget
	CHECK(manager.statusOf(task) == ScriptTaskStatus::TimedOut);
	CHECK(manager.errorOf(task).find("timed out") != Orkige::String::npos);
	CHECK(backend.slots[slot].released);
}

TEST_CASE("a task dies with its sandbox and with its scene", "[script][task]")
{
	using namespace Orkige;
	ScriptTaskManager manager;
	FakeBackend backend;
	backend.installOn(manager);

	int const sandboxA = 0;
	int const sandboxB = 0;
	void const * const ownerA = &sandboxA;
	void const * const ownerB = &sandboxB;

	FakeCoroutine forever;
	forever.waits.push_back(ScriptTaskCore::waitSeconds(1000.0));
	const int slotA = backend.add(forever);
	const int slotB = backend.add(forever);
	const int slotC = backend.add(forever);
	const ScriptTaskManager::TaskId taskA = manager.start(slotA, ownerA, 0);
	const ScriptTaskManager::TaskId taskB = manager.start(slotB, ownerB, 0);
	const ScriptTaskManager::TaskId taskC = manager.start(slotC, ownerA, 0);
	manager.update(0.016f);
	CHECK(manager.getActiveCount() == 3);

	// RETIRE: a sandbox going away (component removed, script hot-reloaded)
	// cancels ITS tasks and only its own
	CHECK(manager.cancelOwner(ownerA) == 2);
	CHECK(manager.statusOf(taskA) == ScriptTaskStatus::Cancelled);
	CHECK(manager.statusOf(taskC) == ScriptTaskStatus::Cancelled);
	CHECK(manager.isActive(taskB));
	// cancelled means never resumed again
	const int resumesA = backend.slots[slotA].resumes;
	manager.update(0.016f);
	manager.update(0.016f);
	CHECK(backend.slots[slotA].resumes == resumesA);

	// a handle cancels its own task
	ScriptTaskHandle handle;
	handle.mId = taskB;
	CHECK(handle.isActive());
	CHECK(handle.cancel());
	CHECK_FALSE(handle.isActive());

	// SCENE TEARDOWN: everything goes, unresumed
	const int slotD = backend.add(forever);
	manager.start(slotD, ownerA, 0);
	manager.update(0.016f);
	manager.clear();
	CHECK(manager.getActiveCount() == 0);
	CHECK(backend.slots[slotD].released);
}

TEST_CASE("a task that raises lands as a named failure", "[script][task]")
{
	using namespace Orkige;
	ScriptTaskManager manager;
	FakeBackend backend;
	backend.installOn(manager);

	Orkige::String reported;
	manager.setErrorSink([&reported](Orkige::String const & message)
	{
		reported = message;
	});

	FakeCoroutine coroutine;
	coroutine.fails = true;
	const int slot = backend.add(coroutine);
	const ScriptTaskManager::TaskId task = manager.start(slot, 0, 0);
	manager.update(0.016f);
	CHECK(manager.statusOf(task) == ScriptTaskStatus::Failed);
	CHECK(manager.errorOf(task) == "the task raised");
	CHECK(reported == "the task raised");
}

TEST_CASE("without a backend nothing can start", "[script][task]")
{
	using namespace Orkige;
	ScriptTaskManager manager;
	manager.setBackend(ScriptTaskManager::Backend());
	CHECK_FALSE(manager.hasBackend());
	CHECK(manager.start(1, 0, 0) == 0);
	manager.update(0.016f);		// and ticking is a harmless no-op
	CHECK(manager.getActiveCount() == 0);
}

TEST_CASE("a play-mode test body suspends across ticks and passes",
	"[script][task]")
{
	using namespace Orkige;
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	if(!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - there is no coroutine to drive");
		return;
	}
	ScriptRuntime & runtime = env.scriptRuntime;
	ScriptTaskManager manager;

	// the counter lives in the shared globals, which a sandbox READS through
	// (its writes stay its own), so the driver below can move the world on
	// while the body is suspended - exactly what a play-mode test observes
	runtime.runString("playTicks = 0");

	TaskReader reader;
	reader.files["tests/play.test.lua"] =
		"test('waits and then asserts', { scene = 'scenes/level.oscene' },\n"
		"	function(t)\n"
		"		t.waitFrames(2)\n"
		"		t.waitUntil(function() return playTicks >= 3 end, 60)\n"
		"		t.eq(playTicks >= 3, true)\n"
		"	end)\n";
	InstalledReader installed(&reader);

	ScriptRuntime::ScriptTestSessionId session = 0;
	std::vector<ScriptTestCase> cases;
	Orkige::String error;
	REQUIRE(runtime.beginTestFile("tests/play.test.lua", "", session, cases, 0,
		&error));
	REQUIRE(cases.size() == 1);
	// the plan says this one needs a scene - the fact a runner acts on
	CHECK(cases[0].scene == "scenes/level.oscene");

	const ScriptTaskManager::TaskId task =
		runtime.startTestCase(session, cases[0].index, 600, &error);
	REQUIRE(task != 0);
	INFO("start error: " << error);

	// the body only advances when the scheduler ticks - never on its own
	for(int tick = 0; tick < 12 && manager.isActive(task); ++tick)
	{
		runtime.runString("playTicks = playTicks + 1");
		manager.update(0.016f);
	}
	CHECK(manager.statusOf(task) == ScriptTaskStatus::Completed);

	ScriptTestRecord record;
	REQUIRE(runtime.testCaseRecord(session, cases[0].index, record));
	INFO("record: " << record.status << " " << record.message);
	CHECK(record.status == "pass");
	runtime.endTestFile(session);
}

TEST_CASE("a play-mode test that never finishes times out by name",
	"[script][task]")
{
	using namespace Orkige;
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	if(!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - there is no coroutine to drive");
		return;
	}
	ScriptRuntime & runtime = env.scriptRuntime;
	ScriptTaskManager manager;

	TaskReader reader;
	reader.files["tests/wedged.test.lua"] =
		"test('waits for something that never happens',\n"
		"	{ scene = 'scenes/level.oscene' }, function(t)\n"
		"		t.waitUntil(function() return false end)\n"
		"		t.fail('this line is never reached')\n"
		"	end)\n";
	InstalledReader installed(&reader);

	ScriptRuntime::ScriptTestSessionId session = 0;
	std::vector<ScriptTestCase> cases;
	Orkige::String error;
	REQUIRE(runtime.beginTestFile("tests/wedged.test.lua", "", session, cases,
		0, &error));
	REQUIRE(cases.size() == 1);
	const ScriptTaskManager::TaskId task =
		runtime.startTestCase(session, cases[0].index, 5, &error);
	REQUIRE(task != 0);
	for(int tick = 0; tick < 20 && manager.isActive(task); ++tick)
	{
		manager.update(0.016f);
	}
	// the whole point: a wedged wait is a NAMED failure, not a hung run
	CHECK(manager.statusOf(task) == ScriptTaskStatus::TimedOut);
	CHECK(manager.errorOf(task).find("timed out") != Orkige::String::npos);
	// and it left no verdict of its own, which is what makes the runner
	// report the time-out instead of a pass
	ScriptTestRecord record;
	CHECK_FALSE(runtime.testCaseRecord(session, cases[0].index, record));
	runtime.endTestFile(session);
}

TEST_CASE("closing a test file cancels what it left running", "[script][task]")
{
	using namespace Orkige;
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	if(!ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - there is no coroutine to drive");
		return;
	}
	ScriptRuntime & runtime = env.scriptRuntime;
	ScriptTaskManager manager;

	TaskReader reader;
	reader.files["tests/left.test.lua"] =
		"test('is still waiting when the file closes',\n"
		"	{ scene = 'scenes/level.oscene' }, function(t)\n"
		"		t.wait(1000)\n"
		"	end)\n";
	InstalledReader installed(&reader);

	ScriptRuntime::ScriptTestSessionId session = 0;
	std::vector<ScriptTestCase> cases;
	Orkige::String error;
	REQUIRE(runtime.beginTestFile("tests/left.test.lua", "", session, cases, 0,
		&error));
	REQUIRE(cases.size() == 1);
	const ScriptTaskManager::TaskId task =
		runtime.startTestCase(session, cases[0].index, 600, &error);
	REQUIRE(task != 0);
	manager.update(0.016f);
	CHECK(manager.isActive(task));
	runtime.endTestFile(session);
	CHECK_FALSE(manager.isActive(task));
}
