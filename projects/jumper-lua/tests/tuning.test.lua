-- tuning.test.lua - the game's SHIPPED tuning file, checked as content.
--
-- Run it with:  orkige_player --project projects/jumper-lua --run-tests
--
-- data/tuning.json is read here through the same `data` table scripts/player.lua
-- reads it with, resolved through the live content mounts - so this asserts the
-- file that actually ships, not a copy of its numbers.

local jumper = script.require("scripts/jumperlib.lua")

test("the shipped tuning file parses", function(t)
	local tuning, err = data.readJson("data/tuning.json")
	t.isnil(err)
	t.truthy(tuning)
end)

test("the shipped tuning file is playable", function(t)
	local tuning = data.readJson("data/tuning.json")
	local checked, reason = jumper.checkTuning(tuning)
	-- the same check the game boots with: a refusal names the field
	t.truthy(checked, reason)
end)

test("checkTuning refuses a missing field", function(t)
	local tuning = data.readJson("data/tuning.json")
	tuning.jumpSpeed = nil
	local checked, reason = jumper.checkTuning(tuning)
	t.isnil(checked)
	t.truthy(string.find(reason, "jumpSpeed", 1, true) ~= nil, reason)
end)

test("checkTuning refuses an out-of-range field", function(t)
	local tuning = data.readJson("data/tuning.json")
	-- gravity pointing UP is not a tuning choice, it is a typo
	tuning.gravityY = 20.0
	local checked, reason = jumper.checkTuning(tuning)
	t.isnil(checked)
	t.truthy(string.find(reason, "gravityY", 1, true) ~= nil, reason)
end)

test("checkTuning refuses a non-table", function(t)
	t.isnil(jumper.checkTuning(nil))
	t.isnil(jumper.checkTuning("not a table"))
end)

test("a data read outside the project is refused", function(t)
	-- the `data` jail, exercised from a test: a game's own suite is where a
	-- project would first notice if this ever loosened
	local text, err = data.read("../../../etc/passwd")
	t.isnil(text)
	t.truthy(err)
end)
