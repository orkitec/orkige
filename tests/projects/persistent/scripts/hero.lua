-- Persistent-object fixture, driven by the player_persistent_selfcheck ctest.
-- Hero is marked persistent in levelA.oscene, so the mid-play switch to levelB
-- must carry its whole live state across: this SANDBOX (the tick counter and
-- the one-time init below) and its rigid body (which re-collides with levelB's
-- lower ground). The selfcheck reads the shared.persist.* values this script
-- publishes plus the world objects.
local ticks = 0
local contacts = 0
local switched = false

function init(self)
	shared.persist = shared.persist or {}
	-- counts EVERY init of THIS sandbox: a survivor's init runs exactly once,
	-- a destroyed-and-recreated object would run it again
	shared.persist.inits = (shared.persist.inits or 0) + 1
	-- ticks / contacts are per-instance upvalues and are NOT reset here, so a
	-- surviving sandbox keeps counting straight across the scene switch
end

function update(self, dt)
	ticks = ticks + 1
	shared.persist.ticks = ticks
	shared.persist.contacts = contacts
	-- once Hero has landed on levelA's ground, request the deferred switch to
	-- levelB (applied at the frame boundary by the level system)
	if not switched and contacts >= 1 then
		switched = true
		shared.persist.switchRequested = 1
		world.loadScene("scenes/levelB.oscene")
	end
end

function onContactBegin(self, other)
	-- landing on levelA's ground (before the switch) then on levelB's lower
	-- ground (after it) - a second contact proves the body survived and
	-- interacts with the ARRIVING scene's geometry
	contacts = contacts + 1
	shared.persist.contacts = contacts
end
