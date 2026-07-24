-- Runtime-spawn fixture, driven by the editor_play_mirror_spawn ctest: after
-- a short settle the Director spawns "RuntimeProbe" from the project prefab
-- (world.spawn), moves it every tick (position is a pure function of elapsed
-- time, so any two observations differ), and after a generous window despawns
-- it again - the editor asserts its scene mirror gains a moving stand-in for
-- the spawned object, loses it on the despawn, and restores the authored
-- scene byte-exactly on Stop. The window is wall-clock based (dt), so a fast
-- headless player and a slow CI host walk the same sequence.
local elapsed = 0.0
local spawned = false
local removed = false

function init(self)
	elapsed = 0.0
end

function update(self, dt)
	elapsed = elapsed + dt
	if not spawned and elapsed > 0.5 then
		spawned = world.spawn("assets/probe.oprefab", "RuntimeProbe")
	end
	if spawned and not removed then
		local mover = world.getTransform("RuntimeProbe")
		if mover then
			mover:setPosition(Vector3(0.5 * elapsed, 1.0, 0.0))
		end
		if elapsed > 12.0 then
			removed = world.despawn("RuntimeProbe")
		end
	end
end
