-- Mid-play scene-switch fixture, driven by the editor_play_mirror_switch
-- ctest: after a short settle the Switcher requests a deferred scene switch
-- to scenes/second.oscene (world.loadScene -> the frame-boundary reload).
-- The editor asserts its Scene view swaps to a view-only mirror of the second
-- scene and that Stop restores the authored document byte-exactly.
local elapsed = 0.0
local switched = false

function init(self)
	elapsed = 0.0
end

function update(self, dt)
	elapsed = elapsed + dt
	if not switched and elapsed > 1.0 then
		switched = true
		world.loadScene("scenes/second.oscene")
	end
end
