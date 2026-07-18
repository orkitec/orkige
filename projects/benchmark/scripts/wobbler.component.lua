-- wobbler.component.lua - keeps the 2D foreground soft-body blob alive in the
-- character-cast vignette. The blob's wobble springs snap back to rest with no
-- drift, so without a driver it would sit still; this script kicks it on a
-- timer (self.shape:impulse) so it breathes continuously beside the crowd.

local timer = 0.0
local flip = 1.0

function init(self)
	timer = 0.0
	flip = 1.0
end

function update(self, dt)
	timer = timer + dt
	if self.shape ~= nil and timer > 0.8 then
		timer = 0.0
		flip = -flip
		-- an upward-ish kick, alternating side to side, so the deform reads
		self.shape:impulse(0.25 * flip, 1.0, 0.6)
	end
end
