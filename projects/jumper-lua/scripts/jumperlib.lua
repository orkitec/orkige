-- jumperlib.lua - the jumper's PURE "feel" math, as a library.
--
-- A plain .lua file is a LIBRARY: it is not a component kind (that would be
-- *.component.lua) and nothing attaches it to an object. Another script picks
-- it up with `script.require("scripts/jumperlib.lua")`, which reads it through
-- the content mounts - so this file is reachable identically as a loose file,
-- from inside a pak and from inside a phone's package.
--
-- Everything here is a PURE function of its arguments: no engine singletons,
-- no `self`, no globals. That is what makes it testable from
-- tests/*.test.lua without a scene, and it is why the game's numbers live here
-- rather than inline in scripts/player.lua.

local M = {}

--- movement -----------------------------------------------------------------

-- frame-rate independent exponential approach: never overshoots, covers ~63%
-- of the remaining distance per 1/rate seconds. The whole feel of the run and
-- of the camera follow is this one curve.
function M.approach(current, target, rate, dt)
	return current + (target - current) * (1.0 - math.exp(-rate * dt))
end

--- jumping ------------------------------------------------------------------

-- the buffered jump, as a pure decision. A press up to `bufferSeconds` before
-- landing still jumps, which is what makes the controls forgiving.
-- Returns the new buffer plus whether the jump fires THIS frame.
function M.tickJumpBuffer(buffer, dt, pressed, grounded, bufferSeconds)
	if pressed then
		buffer = bufferSeconds
	end
	buffer = math.max(0.0, buffer - dt)
	if grounded and buffer > 0.0 then
		return 0.0, true
	end
	return buffer, false
end

--- level rules --------------------------------------------------------------

-- fell out of the level?
function M.belowKillPlane(y, killPlaneY)
	return y < killPlaneY
end

-- reached the buddy at the end? (squared distance - no square root needed)
function M.withinGoal(px, py, pz, gx, gy, gz, radius)
	local dx, dy, dz = gx - px, gy - py, gz - pz
	return dx * dx + dy * dy + dz * dz <= radius * radius
end

--- tuning -------------------------------------------------------------------

-- the fields data/tuning.json must carry, and the range each has to be in for
-- the game to be playable at all
M.TUNING_FIELDS = {
	{ name = "moveSpeed",         min = 0.1,   max = 100.0 },
	{ name = "accelRate",         min = 0.1,   max = 1000.0 },
	{ name = "jumpSpeed",         min = 0.1,   max = 100.0 },
	{ name = "gravityY",          min = -200.0, max = -0.1 },
	{ name = "killPlaneY",        min = -1000.0, max = 0.0 },
	{ name = "goalRadius",        min = 0.01,  max = 100.0 },
	{ name = "cameraRate",        min = 0.1,   max = 1000.0 },
	{ name = "jumpBufferSeconds", min = 0.0,   max = 2.0 },
}

-- validate a decoded tuning table.
-- Returns the table on success, or nil plus an honest reason naming the first
-- offending field - the game refuses to run on numbers it cannot trust rather
-- than silently substituting defaults.
function M.checkTuning(tuning)
	if type(tuning) ~= "table" then
		return nil, "the tuning table is missing"
	end
	for _, field in ipairs(M.TUNING_FIELDS) do
		local value = tuning[field.name]
		if type(value) ~= "number" then
			return nil, "tuning field '" .. field.name .. "' is missing"
		end
		if value < field.min or value > field.max then
			return nil, "tuning field '" .. field.name .. "' is " ..
				tostring(value) .. ", outside [" .. tostring(field.min) ..
				", " .. tostring(field.max) .. "]"
		end
	end
	return tuning
end

return M
