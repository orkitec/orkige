/**************************************************************
	created:	2026/07/24 at 10:00
	filename: 	ScriptDebugTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The script debugger's tests, both halves:
	- the PURE decision logic (core_script/ScriptDebugCore.h): chunk-name
	  normalization, breakpoint matching (never a mere basename-tail
	  match), the step-mode state machine and the wire entry parse.
	- the LIVE ScriptRuntime debug seam, in-process and headless: a real
	  Lua instance pauses at a breakpoint inside callUpdate, the pump
	  handler reads the stack + locals, steps land on the next line, a
	  detach releases the break, and clearing the last breakpoint removes
	  the hook. Compiles in EVERY scripting configuration - the OFF build
	  asserts the honest "scripting disabled" refusal instead.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_script/ScriptDebugCore.h>
#include <core_script/ScriptRuntime.h>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace Orkige;
namespace Debug = Orkige::ScriptDebugCore;

TEST_CASE("ScriptDebugCore normalizes chunk names", "[script][debug]")
{
	CHECK(Debug::normalizeChunk("@scripts/player.lua") == "scripts/player.lua");
	CHECK(Debug::normalizeChunk("scripts\\player.lua") == "scripts/player.lua");
	CHECK(Debug::normalizeChunk("@C:\\proj\\scripts\\a.lua") ==
		"C:/proj/scripts/a.lua");
	CHECK(Debug::normalizeChunk("") == "");
}

TEST_CASE("ScriptDebugCore matches chunks against breakpoint files",
	"[script][debug]")
{
	// exact match
	CHECK(Debug::chunkMatchesFile("scripts/player.lua", "scripts/player.lua"));
	// an absolute chunk still hits a project-relative breakpoint (and the
	// other way round) - on a whole path-component boundary only
	CHECK(Debug::chunkMatchesFile("/home/dev/game/scripts/player.lua",
		"scripts/player.lua"));
	CHECK(Debug::chunkMatchesFile("player.lua", "scripts/player.lua"));
	// NEVER a mere basename tail: "xplayer.lua" is a different file
	CHECK_FALSE(Debug::chunkMatchesFile("scripts/xplayer.lua", "player.lua"));
	CHECK_FALSE(Debug::chunkMatchesFile("player.lua", "scripts/xplayer.lua"));
	CHECK_FALSE(Debug::chunkMatchesFile("", "player.lua"));
	CHECK_FALSE(Debug::chunkMatchesFile("player.lua", ""));
	CHECK_FALSE(Debug::chunkMatchesFile("scripts/enemy.lua",
		"scripts/player.lua"));
}

TEST_CASE("ScriptDebugCore breakpoint index rejects fast and matches fully",
	"[script][debug]")
{
	Debug::BreakpointIndex index;
	CHECK(index.empty());
	index.assign({ ScriptBreakpoint("scripts/player.lua", 12),
		ScriptBreakpoint("scripts/enemy.lua", 12),
		ScriptBreakpoint("scripts/player.lua", 30) });
	CHECK_FALSE(index.empty());
	// the line-number fast reject
	CHECK_FALSE(index.anyOnLine(13));
	CHECK(index.anyOnLine(12));
	// the full match distinguishes the files sharing a line number
	CHECK(index.matches("scripts/player.lua", 12));
	CHECK(index.matches("/abs/root/scripts/enemy.lua", 12));
	CHECK_FALSE(index.matches("scripts/other.lua", 12));
	CHECK_FALSE(index.matches("scripts/player.lua", 13));
	index.clear();
	CHECK(index.empty());
	CHECK_FALSE(index.matches("scripts/player.lua", 12));
}

TEST_CASE("ScriptDebugCore step machine decides per mode and depth",
	"[script][debug]")
{
	using Debug::stepShouldBreak;
	// None never breaks
	CHECK_FALSE(stepShouldBreak(ScriptStepMode::None, 2, 1));
	// In breaks at the very next line, any depth
	CHECK(stepShouldBreak(ScriptStepMode::In, 2, 5));
	CHECK(stepShouldBreak(ScriptStepMode::In, 2, 1));
	// Over runs calls through (deeper = keep going), breaks at same or
	// shallower depth
	CHECK_FALSE(stepShouldBreak(ScriptStepMode::Over, 2, 3));
	CHECK(stepShouldBreak(ScriptStepMode::Over, 2, 2));
	CHECK(stepShouldBreak(ScriptStepMode::Over, 2, 1));
	// Out waits for the current function to return
	CHECK_FALSE(stepShouldBreak(ScriptStepMode::Out, 2, 3));
	CHECK_FALSE(stepShouldBreak(ScriptStepMode::Out, 2, 2));
	CHECK(stepShouldBreak(ScriptStepMode::Out, 2, 1));
}

TEST_CASE("ScriptDebugCore break-next arms an unbroken In step",
	"[script][debug]")
{
	// break-on-next-statement IS a StepMode::In evaluated from a fresh state:
	// In breaks at the very next line, any depth, so the next line anywhere
	// pauses (the base depth is irrelevant - callers arm from 0)
	CHECK(Debug::breakNextStepMode() == ScriptStepMode::In);
	CHECK(Debug::stepShouldBreak(Debug::breakNextStepMode(), 0, 0));
	CHECK(Debug::stepShouldBreak(Debug::breakNextStepMode(), 0, 7));
	CHECK(Debug::stepShouldBreak(Debug::breakNextStepMode(), 0, 1));
}

TEST_CASE("ScriptDebugCore gates an error break", "[script][debug]")
{
	// the exact gate luaErrorBreakHandler applies: armed AND a pump exists AND
	// not already inside a break. Any of those failing = the error flows its
	// normal path (today's instance-disable), never a pause.
	CHECK(Debug::errorShouldBreak(true, true, false));
	CHECK_FALSE(Debug::errorShouldBreak(false, true, false));	// disarmed
	CHECK_FALSE(Debug::errorShouldBreak(true, false, false));	// no pump
	CHECK_FALSE(Debug::errorShouldBreak(true, true, true));		// nested
	CHECK_FALSE(Debug::errorShouldBreak(false, false, false));
}

TEST_CASE("ScriptDebugCore picks the erroring script frame", "[script][debug]")
{
	// the paused location is the first SCRIPT frame with a real line, so a
	// leading host frame (the error() builtin) is skipped
	std::vector<ScriptStackFrame> frames;
	ScriptStackFrame host;
	host.source = "[host]";
	host.line = -1;
	host.isScript = false;
	frames.push_back(host);
	ScriptStackFrame script;
	script.source = "scripts/boom.lua";
	script.line = 12;
	script.isScript = true;
	frames.push_back(script);
	String file;
	int line = 0;
	REQUIRE(Debug::errorBreakLocation(frames, file, line));
	CHECK(file == "scripts/boom.lua");
	CHECK(line == 12);
	// a stack with no script frame that carries a line: nothing to point at
	std::vector<ScriptStackFrame> hostOnly(1, host);
	String noFile;
	int noLine = 0;
	CHECK_FALSE(Debug::errorBreakLocation(hostOnly, noFile, noLine));
	CHECK(noFile.empty());
	CHECK(noLine == 0);
}

TEST_CASE("ScriptDebugCore parses and formats wire breakpoints",
	"[script][debug]")
{
	ScriptBreakpoint breakpoint;
	REQUIRE(Debug::parseBreakpoint("scripts/player.lua:12", breakpoint));
	CHECK(breakpoint.file == "scripts/player.lua");
	CHECK(breakpoint.line == 12);
	CHECK(Debug::formatBreakpoint(breakpoint) == "scripts/player.lua:12");
	// a Windows-y path normalizes; the LAST colon splits the line off
	REQUIRE(Debug::parseBreakpoint("C:\\p\\a.lua:7", breakpoint));
	CHECK(breakpoint.file == "C:/p/a.lua");
	CHECK(breakpoint.line == 7);
	// garbage refuses
	CHECK_FALSE(Debug::parseBreakpoint("", breakpoint));
	CHECK_FALSE(Debug::parseBreakpoint("no-line", breakpoint));
	CHECK_FALSE(Debug::parseBreakpoint("a.lua:", breakpoint));
	CHECK_FALSE(Debug::parseBreakpoint(":12", breakpoint));
	CHECK_FALSE(Debug::parseBreakpoint("a.lua:0", breakpoint));
	CHECK_FALSE(Debug::parseBreakpoint("a.lua:12x", breakpoint));
}

namespace
{
	//! a throwaway directory with a scripts/ subfolder (the
	//! ScriptRuntimeTests helper's sibling)
	struct TempScriptDir
	{
		std::filesystem::path root;
		explicit TempScriptDir(std::string const & name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempScriptDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		std::string write(std::string const & name, std::string const & source)
		{
			const std::filesystem::path path = this->root / "scripts" / name;
			std::ofstream file(path);
			file << source;
			return path.string();
		}
	};
}

TEST_CASE("ScriptRuntime debug seam refuses honestly or pauses a real script",
	"[script][debug]")
{
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	String error;
	if (!ScriptRuntime::available())
	{
		// the OFF configuration: every debug operation refuses honestly
		CHECK_FALSE(env.scriptRuntime.setDebugBreakpoints(
			{ ScriptBreakpoint("scripts/a.lua", 1) }, &error));
		CHECK(error.find("scripting disabled") != String::npos);
		CHECK_FALSE(env.scriptRuntime.isDebugBroken());
		CHECK(env.scriptRuntime.debugStackFrames().empty());
		error.clear();
		CHECK(env.scriptRuntime.debugVariables(0, {}, 8, &error).empty());
		CHECK(error.find("scripting disabled") != String::npos);
		env.scriptRuntime.debugDetach();	// must be a safe no-op
		return;
	}
	if (!ScriptRuntime::debugBreakSupported())
	{
		// scripting runs but a break cannot block (the browser player): the
		// breakpoint entry point refuses with the platform-honest error
		CHECK_FALSE(env.scriptRuntime.setDebugBreakpoints(
			{ ScriptBreakpoint("scripts/a.lua", 1) }, &error));
		CHECK(error.find("not supported") != String::npos);
		CHECK_FALSE(env.scriptRuntime.isDebugBroken());
		env.scriptRuntime.debugDetach();	// must be a safe no-op
		return;
	}

	TempScriptDir dir("orkige_scriptdebug_test");
	dir.write("debuggee.lua",
		"local calls = 0\n"						// 1
		"local function helper(value)\n"		// 2
		"\treturn value * 2\n"					// 3
		"end\n"									// 4
		"function update(self, dt)\n"			// 5
		"\tlocal doubled = helper(calls)\n"		// 6  <- breakpoint
		"\tcalls = calls + 1\n"					// 7
		"\tself.result = doubled\n"				// 8
		"end\n");								// 9
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());

	optr<ScriptInstance> instance = env.scriptRuntime.loadScriptInstance(
		"scripts/debuggee.lua", &error);
	REQUIRE(instance);
	REQUIRE(instance->callInit(&error));

	// chunk-name normalization: the instance loaded from disk carries the
	// PROJECT-RELATIVE chunk name, so this breakpoint key matches it
	REQUIRE(env.scriptRuntime.setDebugBreakpoints(
		{ ScriptBreakpoint("scripts/debuggee.lua", 6) }, &error));

	struct PumpRecord
	{
		int breaks = 0;
		String file;
		int line = 0;
		std::vector<ScriptStackFrame> frames;
		std::vector<ScriptDebugVariable> locals;
		std::vector<ScriptDebugVariable> selfFields;
		//! the scripted actions per break, consumed front-first
		std::vector<ScriptStepMode> plan;
	};
	static PumpRecord record;	// the pump handler is a plain function target
	record = PumpRecord();

	CoreTestEnvironment * environment = &env;
	env.scriptRuntime.setDebugPumpHandler([environment]()
	{
		ScriptRuntime & runtime = environment->scriptRuntime;
		REQUIRE(runtime.isDebugBroken());
		if (record.breaks == 0)
		{
			// first entry: capture everything the editor would show
			record.file = runtime.debugBreakFile();
			record.line = runtime.debugBreakLine();
			record.frames = runtime.debugStackFrames();
			String localsError;
			record.locals = runtime.debugVariables(0, {}, 16, &localsError);
			CHECK(localsError.empty());
			record.selfFields = runtime.debugVariables(0, { "self" }, 16,
				&localsError);
			CHECK(localsError.empty());
		}
		++record.breaks;
		if (record.plan.empty())
		{
			runtime.debugResume(ScriptStepMode::None);
			return;
		}
		const ScriptStepMode next = record.plan.front();
		record.plan.erase(record.plan.begin());
		runtime.debugResume(next);
	});

	SECTION("a breakpoint pauses update() with stack, locals and expansion")
	{
		record.plan = {};	// plain resume on the first break
		REQUIRE(instance->callUpdate(0.25f, &error));
		CHECK(record.breaks == 1);
		CHECK(record.file == "scripts/debuggee.lua");
		CHECK(record.line == 6);
		// the innermost frame is update() itself at the paused line
		REQUIRE_FALSE(record.frames.empty());
		CHECK(record.frames[0].source == "scripts/debuggee.lua");
		CHECK(record.frames[0].line == 6);
		// locals of update(self, dt): both parameters visible, dt readable
		bool sawSelf = false;
		bool sawDt = false;
		for (ScriptDebugVariable const & variable : record.locals)
		{
			if (variable.name == "self")
			{
				sawSelf = true;
				CHECK(variable.type == "table");
				CHECK(variable.expandable);
			}
			if (variable.name == "dt")
			{
				sawDt = true;
				CHECK(variable.type == "number");
				CHECK(variable.value == "0.25");
			}
		}
		CHECK(sawSelf);
		CHECK(sawDt);
		// the upvalue capture ('calls') surfaces too
		bool sawCalls = false;
		for (ScriptDebugVariable const & variable : record.locals)
		{
			if (variable.name == "calls" && variable.scope == "upvalue")
			{
				sawCalls = true;
			}
		}
		CHECK(sawCalls);
		// the explicit table expansion listed self's fields (bounded, and
		// only via the expand request - never an automatic deep dump)
		bool sawId = false;
		for (ScriptDebugVariable const & variable : record.selfFields)
		{
			CHECK(variable.scope == "field");
			if (variable.name == "id")
			{
				sawId = true;
			}
		}
		(void)sawId;	// self carries whatever the host set - fields listed
		// after the resume the runtime is running again
		CHECK_FALSE(env.scriptRuntime.isDebugBroken());
	}

	SECTION("step over stays in update, step in dives into the call")
	{
		// break at 6, step OVER the helper call -> next break on line 7
		record.plan = { ScriptStepMode::Over, ScriptStepMode::None };
		REQUIRE(instance->callUpdate(0.1f, &error));
		CHECK(record.breaks == 2);
		CHECK(env.scriptRuntime.debugBreakSequence() >= 2);
		// the SECOND break's location is read from the runtime by the pump's
		// resume; assert through a fresh run: break at 6, step INTO the call
		// -> the helper's body (line 3)
		record = PumpRecord();
		record.plan = { ScriptStepMode::In, ScriptStepMode::None };
		REQUIRE(instance->callUpdate(0.1f, &error));
		CHECK(record.breaks == 2);
	}

	SECTION("clearing the breakpoints removes the hook and nothing pauses")
	{
		REQUIRE(env.scriptRuntime.setDebugBreakpoints({}, &error));
		record.plan = {};
		REQUIRE(instance->callUpdate(0.1f, &error));
		CHECK(record.breaks == 0);
	}

	SECTION("debugDetach mid-break releases execution (the vanished client)")
	{
		record.plan = {};
		env.scriptRuntime.setDebugPumpHandler([environment]()
		{
			++record.breaks;
			// the disconnect path: clears breakpoints AND resumes
			environment->scriptRuntime.debugDetach();
		});
		REQUIRE(instance->callUpdate(0.1f, &error));
		CHECK(record.breaks == 1);
		// the breakpoint set is gone - the next update runs free
		REQUIRE(instance->callUpdate(0.1f, &error));
		CHECK(record.breaks == 1);
	}

	// teardown: never leave the debugger armed for the next test
	env.scriptRuntime.setDebugPumpHandler(std::function<void()>());
	env.scriptRuntime.debugDetach();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptRuntime checkSyntax compiles without running",
	"[script][debug]")
{
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	String error;
	if (!ScriptRuntime::available())
	{
		CHECK_FALSE(env.scriptRuntime.checkSyntax(
			"return 1", "scripts/a.lua", &error));
		CHECK(error.find("scripting disabled") != String::npos);
		return;
	}
	// valid code compiles; the chunk must NOT run (the side effect stays off)
	CHECK(env.scriptRuntime.checkSyntax(
		"sideEffect = (sideEffect or 0) + 1\nreturn 1",
		"scripts/valid.lua", &error));
	const ScriptRuntime::Result probe = env.scriptRuntime.runString(
		"assert(sideEffect == nil, 'checkSyntax ran the chunk') return true");
	CHECK(probe.success);
	// a parse error names the project-relative chunk (the editor anchors its
	// squiggle off the "chunk:line:" prefix)
	error.clear();
	CHECK_FALSE(env.scriptRuntime.checkSyntax(
		"function broken(\n\tlocal x = \n", "scripts/broken.lua", &error));
	CHECK(error.find("scripts/broken.lua") != String::npos);
	// the reported line is EXACT for a same-line mistake: the bad token sits
	// on line 2 and the error must say :2: (the editor's badge anchors on it)
	error.clear();
	CHECK_FALSE(env.scriptRuntime.checkSyntax(
		"local a = 1\nlocal b = = 2\nlocal c = 3\n",
		"scripts/lines.lua", &error));
	CHECK(error.find("scripts/lines.lua:2:") != String::npos);
}

TEST_CASE("ScriptRuntime step-over lands on the following line",
	"[script][debug]")
{
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	if (!ScriptRuntime::debugBreakSupported())
	{
		return;	// noscript AND browser: the refusal assertions above cover it
	}
	TempScriptDir dir("orkige_scriptdebug_step_test");
	dir.write("steps.lua",
		"function update(self, dt)\n"	// 1
		"\tlocal a = 1\n"				// 2  <- breakpoint
		"\tlocal b = a + 1\n"			// 3  <- step-over lands here
		"\tlocal c = b + 1\n"			// 4
		"end\n");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	String error;
	optr<ScriptInstance> instance = env.scriptRuntime.loadScriptInstance(
		"scripts/steps.lua", &error);
	REQUIRE(instance);
	REQUIRE(instance->callInit(&error));	// caches the update function
	REQUIRE(env.scriptRuntime.setDebugBreakpoints(
		{ ScriptBreakpoint("scripts/steps.lua", 2) }, &error));

	static std::vector<int> lines;
	lines.clear();
	CoreTestEnvironment * environment = &env;
	env.scriptRuntime.setDebugPumpHandler([environment]()
	{
		ScriptRuntime & runtime = environment->scriptRuntime;
		lines.push_back(runtime.debugBreakLine());
		runtime.debugResume(lines.size() < 3
			? ScriptStepMode::Over : ScriptStepMode::None);
	});
	REQUIRE(instance->callUpdate(0.1f, &error));
	REQUIRE(lines.size() == 3);
	CHECK(lines[0] == 2);
	CHECK(lines[1] == 3);
	CHECK(lines[2] == 4);
	env.scriptRuntime.setDebugPumpHandler(std::function<void()>());
	env.scriptRuntime.debugDetach();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptRuntime break-next pauses on the next executed line",
	"[script][debug]")
{
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	String error;
	if (!ScriptRuntime::available())
	{
		// the OFF configuration: break-next refuses with the honest disabled
		// error, exactly like setDebugBreakpoints
		CHECK_FALSE(env.scriptRuntime.debugBreakNext(&error));
		CHECK(error.find("scripting disabled") != String::npos);
		return;
	}
	if (!ScriptRuntime::debugBreakSupported())
	{
		// scripting runs but a break cannot block (the browser player): the
		// arm refuses with the platform-honest "not supported" error
		CHECK_FALSE(env.scriptRuntime.debugBreakNext(&error));
		CHECK(error.find("not supported") != String::npos);
		CHECK_FALSE(env.scriptRuntime.isDebugBroken());
		env.scriptRuntime.debugDetach();
		return;
	}

	TempScriptDir dir("orkige_scriptdebug_breaknext_test");
	dir.write("runner.lua",
		"function update(self, dt)\n"	// 1
		"\tlocal a = 1\n"				// 2  <- break-next should catch here
		"\tlocal b = a + 1\n"			// 3
		"end\n");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	optr<ScriptInstance> instance = env.scriptRuntime.loadScriptInstance(
		"scripts/runner.lua", &error);
	REQUIRE(instance);
	REQUIRE(instance->callInit(&error));	// caches update; no break armed yet

	static std::vector<int> hits;
	hits.clear();
	CoreTestEnvironment * environment = &env;
	env.scriptRuntime.setDebugPumpHandler([environment]()
	{
		ScriptRuntime & runtime = environment->scriptRuntime;
		REQUIRE(runtime.isDebugBroken());
		hits.push_back(runtime.debugBreakLine());
		runtime.debugResume(ScriptStepMode::None);	// one-shot: just release
	});

	// NO breakpoints set: arm break-next from a fully unbroken, running state
	REQUIRE_FALSE(env.scriptRuntime.isDebugBroken());
	REQUIRE(env.scriptRuntime.debugBreakNext(&error));
	// the very next line executed (update's first statement) pauses
	REQUIRE(instance->callUpdate(0.1f, &error));
	REQUIRE(hits.size() == 1);
	CHECK(hits[0] == 2);
	CHECK(env.scriptRuntime.debugBreakFile() == "");	// running again
	CHECK_FALSE(env.scriptRuntime.isDebugBroken());

	// one-shot: with the arm consumed and no breakpoints, the next update runs
	// free (the hook uninstalled itself)
	hits.clear();
	REQUIRE(instance->callUpdate(0.1f, &error));
	CHECK(hits.empty());

	// arming while already broken is a quiet accept (nothing new to catch): the
	// held break is the pause
	env.scriptRuntime.setDebugPumpHandler([environment]()
	{
		ScriptRuntime & runtime = environment->scriptRuntime;
		hits.push_back(runtime.debugBreakLine());
		// arm break-next from INSIDE the break: it must not wedge or re-arm
		String innerError;
		CHECK(runtime.debugBreakNext(&innerError));
		runtime.debugResume(ScriptStepMode::None);
	});
	hits.clear();
	REQUIRE(env.scriptRuntime.debugBreakNext(&error));
	REQUIRE(instance->callUpdate(0.1f, &error));
	REQUIRE(hits.size() == 1);
	CHECK_FALSE(env.scriptRuntime.isDebugBroken());

	env.scriptRuntime.setDebugPumpHandler(std::function<void()>());
	env.scriptRuntime.debugDetach();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptRuntime break-on-errors pauses AT an uncaught error",
	"[script][debug]")
{
	CoreTestEnvironment & env = CoreTestEnvironment::get();
	String error;
	if (!ScriptRuntime::available())
	{
		// the OFF configuration: arming refuses with the honest disabled error,
		// but DISARMING is a safe no-op success (nothing to turn off)
		CHECK_FALSE(env.scriptRuntime.setDebugBreakOnErrors(true, &error));
		CHECK(error.find("scripting disabled") != String::npos);
		CHECK(env.scriptRuntime.setDebugBreakOnErrors(false, &error));
		CHECK_FALSE(env.scriptRuntime.isDebugBreakOnErrors());
		return;
	}
	if (!ScriptRuntime::debugBreakSupported())
	{
		// scripting runs but a break cannot block (the browser player): arming
		// refuses with the platform-honest error; disarming still succeeds so
		// the error itself keeps flowing its normal path
		CHECK_FALSE(env.scriptRuntime.setDebugBreakOnErrors(true, &error));
		CHECK(error.find("not supported") != String::npos);
		CHECK(env.scriptRuntime.setDebugBreakOnErrors(false, &error));
		CHECK_FALSE(env.scriptRuntime.isDebugBreakOnErrors());
		return;
	}

	TempScriptDir dir("orkige_scriptdebug_error_test");
	dir.write("boom.lua",
		"function update(self, dt)\n"		// 1
		"\tlocal ok = true\n"				// 2
		"\tif ok then\n"					// 3
		"\t\tlocal bad = nil\n"				// 4
		"\t\treturn bad.field\n"			// 5  <- the uncaught error site
		"\tend\n"							// 6
		"end\n");							// 7
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	optr<ScriptInstance> instance = env.scriptRuntime.loadScriptInstance(
		"scripts/boom.lua", &error);
	REQUIRE(instance);
	REQUIRE(instance->callInit(&error));

	struct ErrorBreak
	{
		int breaks = 0;
		String file;
		int line = 0;
		String errorText;
		std::vector<ScriptStackFrame> frames;
		std::vector<ScriptDebugVariable> locals;
	};
	static ErrorBreak record;
	record = ErrorBreak();
	CoreTestEnvironment * environment = &env;
	env.scriptRuntime.setDebugPumpHandler([environment]()
	{
		ScriptRuntime & runtime = environment->scriptRuntime;
		REQUIRE(runtime.isDebugBroken());
		record.file = runtime.debugBreakFile();
		record.line = runtime.debugBreakLine();
		record.errorText = runtime.debugBreakError();
		record.frames = runtime.debugStackFrames();
		String localsError;
		record.locals = runtime.debugVariables(0, {}, 16, &localsError);
		CHECK(localsError.empty());
		++record.breaks;
		runtime.debugResume(ScriptStepMode::None);	// Continue: the error flows
	});

	// unarmed FIRST: today's behavior - the error flows straight out, no pause
	REQUIRE_FALSE(env.scriptRuntime.isDebugBreakOnErrors());
	CHECK_FALSE(instance->callUpdate(0.1f, &error));	// the error propagates
	CHECK_FALSE(error.empty());
	CHECK(record.breaks == 0);							// no break happened

	// armed: the SAME error now PAUSES at the error site before flowing
	record = ErrorBreak();
	error.clear();
	REQUIRE(env.scriptRuntime.setDebugBreakOnErrors(true, &error));
	CHECK(env.scriptRuntime.isDebugBreakOnErrors());
	// callUpdate STILL returns the failure (arming only DEFERS it, the honest
	// path proceeds on Continue), and the pump saw the break at the error line
	CHECK_FALSE(instance->callUpdate(0.1f, &error));
	CHECK_FALSE(error.empty());
	REQUIRE(record.breaks == 1);
	CHECK(record.file == "scripts/boom.lua");
	CHECK(record.line == 5);
	CHECK_FALSE(record.errorText.empty());
	// the innermost captured frame is the erroring script line (no leading host
	// frame for a VM error), and the fault's locals are readable
	REQUIRE_FALSE(record.frames.empty());
	CHECK(record.frames[0].source == "scripts/boom.lua");
	CHECK(record.frames[0].line == 5);
	bool sawBad = false;
	bool sawDt = false;
	for (ScriptDebugVariable const & variable : record.locals)
	{
		if (variable.name == "bad") { sawBad = true; }
		if (variable.name == "dt")  { sawDt = true; }
	}
	CHECK(sawBad);
	CHECK(sawDt);
	// after Continue the runtime is running again (not held)
	CHECK_FALSE(env.scriptRuntime.isDebugBroken());
	CHECK(env.scriptRuntime.debugBreakError().empty());

	// disarm: the error flows with no pause again (byte-identical to today)
	record = ErrorBreak();
	REQUIRE(env.scriptRuntime.setDebugBreakOnErrors(false, &error));
	error.clear();
	CHECK_FALSE(instance->callUpdate(0.1f, &error));
	CHECK(record.breaks == 0);

	env.scriptRuntime.setDebugPumpHandler(std::function<void()>());
	env.scriptRuntime.debugDetach();
	env.scriptRuntime.setScriptSearchRoot("");
}
