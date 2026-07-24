-- atmofixture.component.lua - the driver of the AtmosphereComponent player
-- fixtures (scenes/fixture_atmo_switch.oscene / fixture_atmo_override.oscene).
-- Two actions over ONE script kind:
--   "override": init immediately arms the tested NIGHT look through
--     engine:setAtmosphereBlend - the proof that a script's runtime drive
--     WINS over the scene's component-authored base after boot.
--   "switch": at switchFrame the script deactivates the owning
--     "EnvironmentA" object, so the dormant "EnvironmentB" (night) is
--     promoted and the sky visibly changes between the test's two captures -
--     the take-over contract exercised at runtime.

properties = {
	action      = { type = "string", default = "override" },
	switchFrame = { type = "number", default = 80.0, min = 1.0, max = 100000.0 },
}

local frames = 0

function init(self)
	frames = 0
	if self.action == "override" then
		-- the component armed the DAY base at scene load; this runtime drive
		-- replaces it (the component never re-applies per frame)
		local engine = Engine.getSingleton()
		engine:setAtmosphereBlend("night", "night", 0.0)
		print("atmofixture: override armed")
	end
end

function update(self, dt)
	frames = frames + 1
	if self.action == "switch" and frames == math.floor(self.switchFrame) then
		local owner = world.get("EnvironmentA")
		if owner ~= nil then
			owner:setActive(false)
			print("atmofixture: owner deactivated")
		else
			print("atmofixture: FAIL - EnvironmentA not found")
		end
	end
end
