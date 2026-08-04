/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptTestPrelude.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptTestPrelude.h"

namespace Orkige
{
	namespace
	{
		//! The vocabulary itself. Written against the SANDBOX's permitted
		//! surface only - base, string, table and the pruned `os` (os.clock is
		//! the timer) - so it runs unchanged in the one hardened Lua state and
		//! needs no capability a game script does not already have.
		char const * const kPrelude =
R"LUA(
-- Orkige test vocabulary. Loaded into a *.test.lua file's own sandbox.

local registered = {}
__orkige_tests = registered

-- render a value for an assertion message: strings quoted, tables spelled out
-- (array part first, then the remaining keys in a stable order) so an
-- "expected X, got Y" line shows the difference instead of a table address
local function render(value, depth)
	local kind = type(value)
	if kind == "string" then
		return string.format("%q", value)
	end
	if kind ~= "table" then
		return tostring(value)
	end
	depth = depth or 0
	if depth >= 4 then
		return "{...}"
	end
	local parts = {}
	local arrayCount = 0
	for index, item in ipairs(value) do
		parts[#parts + 1] = render(item, depth + 1)
		arrayCount = index
	end
	local keys = {}
	for key in pairs(value) do
		local isArrayIndex = type(key) == "number" and key % 1 == 0
			and key >= 1 and key <= arrayCount
		if not isArrayIndex then
			keys[#keys + 1] = key
		end
	end
	table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
	for _, key in ipairs(keys) do
		parts[#parts + 1] =
			tostring(key) .. " = " .. render(value[key], depth + 1)
	end
	return "{" .. table.concat(parts, ", ") .. "}"
end

-- recursive structural equality: identical scalars, or tables with the same
-- key set whose values are themselves deep-equal. Cycles terminate through the
-- `seen` pair set. Metatables are NOT consulted - two tables are equal by their
-- CONTENT, which is what a data assertion means.
local function deepEqual(a, b, seen)
	if a == b then
		return true
	end
	if type(a) ~= "table" or type(b) ~= "table" then
		return false
	end
	seen = seen or {}
	local pairsSeen = seen[a]
	if pairsSeen ~= nil and pairsSeen[b] then
		return true
	end
	if pairsSeen == nil then
		pairsSeen = {}
		seen[a] = pairsSeen
	end
	pairsSeen[b] = true
	for key, value in pairs(a) do
		if not deepEqual(value, b[key], seen) then
			return false
		end
	end
	for key in pairs(b) do
		if a[key] == nil then
			return false
		end
	end
	return true
end

-- the assertion table one test body receives. `state.failed` separates an
-- ASSERTION failure from an unexpected Lua error, so the report can say which
-- happened; the raise level (3) is "the caller of the caller of raise" = the
-- line inside the test body, which Lua prefixes as "<file>:<line>: ".
local function newAssertions(state)
	local t = {}
	local function raise(message)
		state.failed = true
		error(message, 3)
	end
	t.fail = function(message)
		raise(message ~= nil and tostring(message) or "t.fail()")
	end
	t.eq = function(actual, expected, message)
		if deepEqual(actual, expected) then
			return true
		end
		raise((message and (message .. ": ") or "") ..
			"expected " .. render(expected) .. ", got " .. render(actual))
	end
	-- // portability-ok: `near` here is a Lua function NAME inside this string
	-- literal, not a C++ identifier the Win32 preprocessor could erase
	t.near = function(actual, expected, tolerance, message)
		tolerance = tolerance or 1e-6
		if type(actual) ~= "number" or type(expected) ~= "number" then
			-- // portability-ok: a Lua function name in a string literal
			local kinds = "t.near expects numbers, got " .. type(actual) ..
				" and " .. type(expected)
			raise((message and (message .. ": ") or "") .. kinds)
		end
		local difference = actual - expected
		if difference < 0 then
			difference = -difference
		end
		if difference <= tolerance then
			return true
		end
		raise((message and (message .. ": ") or "") ..
			"expected " .. render(expected) .. " +/- " .. render(tolerance) ..
			", got " .. render(actual) .. " (off by " ..
			render(difference) .. ")")
	end
	t.truthy = function(value, message)
		if value then
			return true
		end
		raise((message and (message .. ": ") or "") ..
			"expected a truthy value, got " .. render(value))
	end
	t.falsy = function(value, message)
		if not value then
			return true
		end
		raise((message and (message .. ": ") or "") ..
			"expected a falsy value, got " .. render(value))
	end
	t.isnil = function(value, message)
		if value == nil then
			return true
		end
		raise((message and (message .. ": ") or "") ..
			"expected nil, got " .. render(value))
	end
	-- t.errors(fn [, contains]) - the function must raise; the raised message
	-- is returned so a test can assert more about it. An assertion that fails
	-- INSIDE fn must not be mistaken for the expected error, so the failed flag
	-- is restored around the protected call.
	t.errors = function(fn, contains, message)
		if type(fn) ~= "function" then
			raise((message and (message .. ": ") or "") ..
				"t.errors expects a function, got " .. type(fn))
		end
		local wasFailed = state.failed
		local ok, raised = pcall(fn)
		state.failed = wasFailed
		if ok then
			raise((message and (message .. ": ") or "") ..
				"expected the call to raise an error, but it returned normally")
		end
		raised = tostring(raised)
		if contains ~= nil and
			string.find(raised, tostring(contains), 1, true) == nil then
			raise((message and (message .. ": ") or "") ..
				"expected an error containing " .. render(contains) ..
				", got " .. render(raised))
		end
		return raised
	end
	-- THE SAME waits a game script uses, reachable through `t` so a play-mode
	-- test body reads like one thing: t.wait(0.25) / t.waitFrames(2) /
	-- t.waitUntil(fn, 120). They suspend the test's task until the next
	-- script phase, so a test never spins and never advances a frame itself.
	-- Called from a test that runs WITHOUT a scene (no task, no frames) they
	-- raise honestly - there is nothing to suspend.
	t.wait = function(seconds) return wait(seconds) end
	t.waitFrames = function(frames) return waitFrames(frames) end
	t.waitUntil = function(condition, limitFrames)
		return waitUntil(condition, limitFrames)
	end
	-- DRIVING INPUT. A test presses what a PLAYER presses: t.press("jump"),
	-- t.press("move+x"), t.tap("RETURN") - named actions first (that is what
	-- game code reads, so a test keeps its meaning when a binding moves), a
	-- key name for anything no action covers.
	--
	-- It lives on `t` and NOT on the global `input` table on purpose. A game
	-- script that can fake input is a real capability with real consequences -
	-- isKeyDown answering true for something nobody pressed muddies the input
	-- model for every reader - so the ability to press is opened where it
	-- belongs, to test files, and stays out of the surface a game runs on.
	-- The seam (__orkige_test_input) is bound into THIS sandbox by the host;
	-- a runner that drives no frames binds none and every call says so.
	--
	-- An input refusal is an ERROR, not a failed assertion: naming something
	-- unpressable is a mistake in the test, not a fact about the game - so
	-- state.failed stays untouched and the record reads "error". Level 4 is
	-- the test body (raiseInput <- drive <- t.press/t.tap <- the body).
	local function raiseInput(message)
		error(message, 4)
	end
	local function drive(verb, target)
		if __orkige_test_input == nil then
			raiseInput("t." .. verb ..
				" needs the frame-driven runner (this run drives no input)")
		end
		if type(target) ~= "string" then
			raiseInput("t." .. verb ..
				" expects an action or key name, got " .. type(target))
		end
		local ok, reason = __orkige_test_input(verb, target)
		if not ok then
			raiseInput(tostring(reason))
		end
	end
	-- press and HOLD until released; the key is down from the next frame's
	-- input slot on, which is where the action map takes its edge snapshot -
	-- so the very next game update sees the press
	t.press = function(target) drive("press", target) end
	t.release = function(target) drive("release", target) end
	-- press, hold for `frames` frames (default 1), release: exactly ONE press
	-- edge, because the release cannot land before the frame that read it
	t.tap = function(target, frames)
		drive("press", target)
		waitFrames(frames or 1)
		drive("release", target)
	end
	return t
end

-- test(name, fn) / test(name, options, fn) - the declaration. Called while the
-- file's chunk runs; nothing executes until the host asks for a case.
function test(name, optionsOrBody, maybeBody)
	local options, body = nil, optionsOrBody
	if type(optionsOrBody) == "table" then
		options, body = optionsOrBody, maybeBody
	end
	if type(name) ~= "string" then
		error("test(name, [options,] fn): the name must be a string", 2)
	end
	if type(body) ~= "function" then
		error("test('" .. tostring(name) ..
			"'): expected a function body", 2)
	end
	registered[#registered + 1] =
		{ name = name, options = options, body = body }
end

-- where a finished test left its verdict, by declaration index
local records = {}

-- the PLAN pass. `shouldRun(name)` (supplied by the host) selects; a nil
-- selector selects everything. Nothing runs here: the host learns WHICH tests
-- were selected and which of them name a scene, so it can run the ones that
-- need no world right away and give the play-mode ones a loaded scene and
-- frames.
function __orkige_plan(shouldRun)
	local plan = {}
	for index = 1, #registered do
		local entry = registered[index]
		if shouldRun == nil or shouldRun(entry.name) then
			plan[#plan + 1] = {
				index = index,
				name = entry.name,
				scene = entry.options and entry.options.scene or nil,
			}
		end
	end
	return plan
end

-- ONE test as a callable. The host either calls it directly (a test that
-- needs no frames) or starts it as a TASK (a play-mode test suspends itself
-- across frames) - either way the body runs under pcall and the SAME record
-- lands, which is what makes the two tiers one vocabulary. pcall is
-- yield-transparent, so an assertion still separates a FAILURE from an
-- unexpected error on the far side of a wait.
function __orkige_case(index)
	local entry = registered[index]
	if entry == nil then
		return nil
	end
	return function()
		local state = { failed = false }
		local t = newAssertions(state)
		local record = { name = entry.name, ms = 0.0 }
		local started = os.clock()
		local ok, raised = pcall(entry.body, t)
		record.ms = (os.clock() - started) * 1000.0
		if ok then
			record.status = "pass"
			record.message = ""
		else
			record.status = state.failed and "fail" or "error"
			record.message = tostring(raised)
		end
		records[index] = record
	end
end

-- the verdict of a test that ran (nil when it never got that far - a task
-- that was cancelled or ran out of frames, which the host reports itself)
function __orkige_record(index)
	return records[index]
end
)LUA";
	}
	//---------------------------------------------------------
	char const * scriptTestPrelude()
	{
		return kPrelude;
	}
	//---------------------------------------------------------
}
