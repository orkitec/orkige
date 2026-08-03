-- movement.test.lua - the jumper's feel math, tested in Lua.
--
-- Run it with:  orkige_player --project projects/jumper-lua --run-tests
--
-- These exercise the SAME library scripts/player.lua runs on
-- (scripts/jumperlib.lua), loaded the SAME way the game loads it, so a test
-- passing here means the shipped code is right - not that a copy of it is.

local jumper = script.require("scripts/jumperlib.lua")

test("approach never overshoots its target", function(t)
	-- one big step: still short of the target, never past it
	local v = jumper.approach(0.0, 10.0, 12.0, 1.0)
	t.truthy(v < 10.0)
	t.truthy(v > 9.9)
	-- and it moves in the right direction from above, too
	t.truthy(jumper.approach(10.0, 0.0, 12.0, 1.0) < 10.0)
	t.truthy(jumper.approach(10.0, 0.0, 12.0, 1.0) > 0.0)
end)

test("approach is a no-op with no time and at the target", function(t)
	t.near(jumper.approach(3.0, 9.0, 12.0, 0.0), 3.0)
	t.near(jumper.approach(9.0, 9.0, 12.0, 0.016), 9.0)
end)

test("approach covers ~63% of the distance in 1/rate seconds", function(t)
	-- the defining property of the curve: 1 - e^-1
	t.near(jumper.approach(0.0, 1.0, 4.0, 0.25), 0.6321205588, 1e-6)
end)

test("a jump buffered just before landing still fires", function(t)
	-- pressed in the air, 0.1s before touching down (the buffer is 0.12s)
	local buffer, jumped = jumper.tickJumpBuffer(0.0, 0.05, true, false, 0.12)
	t.falsy(jumped)
	t.truthy(buffer > 0.0)
	-- next frame the player lands: the stored press fires
	buffer, jumped = jumper.tickJumpBuffer(buffer, 0.05, false, true, 0.12)
	t.truthy(jumped)
	t.eq(buffer, 0.0)
end)

test("a jump buffered too long expires", function(t)
	local buffer, jumped = jumper.tickJumpBuffer(0.0, 0.0, true, false, 0.12)
	t.falsy(jumped)
	-- 0.2s in the air is past the window
	buffer, jumped = jumper.tickJumpBuffer(buffer, 0.2, false, false, 0.12)
	t.eq(buffer, 0.0)
	-- landing now does nothing
	buffer, jumped = jumper.tickJumpBuffer(buffer, 0.016, false, true, 0.12)
	t.falsy(jumped)
end)

test("no press, no jump - however long you stand there", function(t)
	local buffer, jumped = jumper.tickJumpBuffer(0.0, 0.016, false, true, 0.12)
	t.falsy(jumped)
	t.eq(buffer, 0.0)
end)

test("the kill plane catches a fall and nothing else", function(t)
	t.truthy(jumper.belowKillPlane(-10.5, -10.0))
	t.falsy(jumper.belowKillPlane(-10.0, -10.0))	-- exactly on it survives
	t.falsy(jumper.belowKillPlane(1.0, -10.0))
end)

test("the goal is a sphere, not a box", function(t)
	-- dead centre
	t.truthy(jumper.withinGoal(0, 0, 0, 0, 0, 0, 1.5))
	-- on the radius, on one axis
	t.truthy(jumper.withinGoal(0, 0, 0, 1.5, 0, 0, 1.5))
	-- just outside, on one axis
	t.falsy(jumper.withinGoal(0, 0, 0, 1.51, 0, 0, 1.5))
	-- the corner of the enclosing box is OUTSIDE the sphere: a box test
	-- would wrongly win here
	t.falsy(jumper.withinGoal(0, 0, 0, 1.4, 1.4, 0, 1.5))
	-- distance is what counts, not the origin
	t.truthy(jumper.withinGoal(20, 3, -4, 20.5, 3.5, -4.5, 1.5))
end)
