-- sfx_check.lua - the procedural-sound fixture script
-- (run by the player_sfx_selfcheck ctest: ORKIGE_SFX_SELFCHECK=1
-- orkige_player --project tests/projects/sfx).
--
-- THE point of this script is that there is nothing new in it: a `.osfx`
-- procedural sound is added and played through the SAME SoundComponent
-- surface a `.wav` goes through - same addSound, same mixer group, same
-- per-play variation. The extension dispatch happens below Lua, where a
-- wave file is decoded, so this file would read identically for either.
--
-- It only OBSERVES (the C++ leg in tools/player/PlayerSelfChecks.cpp judges,
-- because only the C++ side knows whether an audio device came up).

function init(self)
	shared.sfx = {
		failed = "",		-- first failure reason ("" = healthy)
		added = false,		-- addSound accepted the .osfx
		played = false,		-- play() reported the source running
		volume = -1.0,		-- the sound's own gain after setVolume
		group = "",			-- the mixer group it landed in
		done = false,		-- the script ran to the end
	}

	local sound = world.getSound(self.id)
	if not sound then
		shared.sfx.failed = "world.getSound found no SoundComponent on " ..
			tostring(self.id)
		return
	end

	-- a procedural effect, added exactly like a wave file would be
	shared.sfx.added = sound:addSound("coin", "coin.osfx", false, true)
	if shared.sfx.added then
		sound:setVolume("coin", 0.8)
		sound:setGroup("coin", "sfx")
		sound:setPitchVariation("coin", 0.1)
		shared.sfx.volume = sound:getVolume("coin")
		shared.sfx.group = sound:getGroup("coin")
		shared.sfx.played = sound:play("coin")
	end
	shared.sfx.done = true
end

function update(self, dt)
end

function shutdown(self)
end
