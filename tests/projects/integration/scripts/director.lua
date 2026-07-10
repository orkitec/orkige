-- director.lua - level A of the level-switch integration
-- (player_integration_levelswitch_selfcheck). It starts a TWEEN on itself and
-- requests a DEFERRED level switch WHILE the tween (and the Fx particle
-- emitter) are still live, so the switch must tear a running tween + emitter
-- down cleanly. shared.integ2.carry proves the shared table survives the swap.
local elapsed = 0
local switched = false

function init(self)
	shared.integ2 = shared.integ2 or {}
	shared.integ2.aliveA = 1
	shared.integ2.carry = 4242
	shared.integ2.moverStartX = self.transform:getPosition().x
	-- a 1.5s move: still running when we switch ~0.3s in
	tween.move("Mover", 2.0, 0.0, 0.0, 1.5)
	-- and a live particle burst on the sibling emitter (world.getParticles):
	-- these are ALSO in flight when the switch tears the scene down
	local fx = world.getParticles("Fx")
	if fx ~= nil then
		fx:burst(24)
	end
end

function update(self, dt)
	elapsed = elapsed + dt
	shared.integ2.moverX = self.transform:getPosition().x
	if not switched and elapsed > 0.3 then
		switched = true
		shared.integ2.switched = 1
		world.loadScene("scenes/levelB.oscene")
	end
end
