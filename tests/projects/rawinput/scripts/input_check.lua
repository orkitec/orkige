-- input_check.lua - the raw-input fixture script
-- (run by the player_raw_input_selfcheck ctest: ORKIGE_RAWINPUT_SELFCHECK=1
-- orkige_player --project tests/projects/rawinput).
--
-- It reads ONLY what a game reads - the `input` table for raw devices and
-- InputActions for named intent - and publishes what it saw into
-- shared.rawinput. The C++ selfcheck block in tools/player/PlayerSelfChecks.cpp
-- injects synthetic fingers and controller edges through the real SDL path and
-- asserts against these numbers, so the whole chain (event -> the frame
-- snapshot taken in the loop's input slot -> Lua) is under test.

local actions = nil

function init(self)
	actions = InputActions.getSingleton()
	shared.rawinput = {
		-- the highest touch count seen, and the phases in the order they came
		touches = 0,
		phases = "",
		beganX = -1, beganY = -1,	-- where the finger landed (window pixels)
		dragX = -1,					-- where it was dragged to
		dragDelta = 0,				-- how far it moved in one frame
		-- the pointer half
		pointerX = -1, pointerY = -1,
		pointerPresses = 0,
		-- the controller half: raw reads plus the NAMED action a pad drives
		padSeen = false,
		padSouth = false,
		padAxis = 0,
		jumpPresses = 0,
		moveX = 0,
	}
end

function update(self, dt)
	local raw = shared.rawinput

	-- touch: record the phase stream of finger 1 and the positions it visited
	local count = input.touchCount()
	if count > raw.touches then
		raw.touches = count
	end
	if count > 0 then
		local id, x, y, phase = input.touch(1)
		if phase == "began" then
			raw.phases = raw.phases .. "b"
			raw.beganX = x
			raw.beganY = y
		elseif phase == "moved" then
			local dx, dy = input.touchDelta(1)
			if dx ~= 0 then
				raw.phases = raw.phases .. "m"
				raw.dragX = x
				raw.dragDelta = dx
			end
		elseif phase == "ended" then
			raw.phases = raw.phases .. "e"
		end
	end

	-- pointer: position plus the one-frame press edge
	if input.pointerPressed() then
		raw.pointerPresses = raw.pointerPresses + 1
		local p = input.pointer()
		raw.pointerX = p.x
		raw.pointerY = p.y
	end

	-- controller: the raw reads a button prompt needs...
	if input.gamepadConnected() then
		raw.padSeen = true
	end
	if input.gamepadButton("south") then
		raw.padSouth = true
	end
	local axis = input.gamepadAxis("leftx")
	if math.abs(axis) > math.abs(raw.padAxis) then
		raw.padAxis = axis
	end
	-- ...and the NAMED actions the same pad drives with zero authoring
	if actions:pressed("jump") then
		raw.jumpPresses = raw.jumpPresses + 1
	end
	local move = actions:value2("move")
	if math.abs(move.x) > math.abs(raw.moveX) then
		raw.moveX = move.x
	end
end
