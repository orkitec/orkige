-- Script export properties fixture, driven by the
-- player_scriptprop_selfcheck ctest (ORKIGE_SCRIPTPROP_SELFCHECK). The
-- top-level `properties` table declares an EXPORTED property (script-declared,
-- auto-exposed in the inspector): the engine surfaces it in the inspector /
-- debug protocol / MCP through the SAME reflection registry as a C++ component
-- property, serializes it per-instance, and INJECTS its value onto `self`
-- before init runs. The selfcheck sets moveSpeed to a distinctive value, then
-- asserts the script both SEES the injected value (init publishes it) and
-- BEHAVES with it (moves at that speed).
properties = {
	moveSpeed = { type = "number", default = 1.0, min = 0, max = 50 },
}

function init(self)
	shared.scriptprop = shared.scriptprop or {}
	-- the value the engine injected onto self before init (the payoff)
	shared.scriptprop.injectedSpeed = self.moveSpeed
	local startX = 0.0
	if self.transform then
		startX = self.transform:getPosition().x
	end
	shared.scriptprop.startX = startX
	shared.scriptprop.x = startX
	shared.scriptprop.elapsed = 0.0
end

function update(self, dt)
	shared.scriptprop.elapsed = shared.scriptprop.elapsed + dt
	if self.transform then
		local p = self.transform:getPosition()
		self.transform:setPosition(Vector3(p.x + self.moveSpeed * dt, p.y, p.z))
		shared.scriptprop.x = self.transform:getPosition().x
	end
end
