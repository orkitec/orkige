-- survivor.lua - level B of the level-switch integration. Its init proves the
-- new level booted (and that shared.integ2.carry survived the teardown); its
-- update proves the switched-to world actually ticks.
function init(self)
	shared.integ2 = shared.integ2 or {}
	shared.integ2.levelBBooted = 1
	shared.integ2.carrySeen = shared.integ2.carry or -1
	shared.integ2.levelBTicks = 0
end

function update(self, dt)
	shared.integ2.levelBTicks = (shared.integ2.levelBTicks or 0) + 1
end
