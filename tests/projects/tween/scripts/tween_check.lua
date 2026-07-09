-- tween_check.lua - the tween system's end-to-end fixture script
-- (run by the player_tween_selfcheck ctest: ORKIGE_TWEEN_SELFCHECK=1
-- orkige_player --project tests/projects/tween).
--
-- Exercises the whole Lua tween surface in one run and publishes what it
-- observes into shared.tween; the C++ selfcheck block in
-- tools/player/main.cpp reads the verdict and independently verifies the
-- world (Tweener transform, sprite tint, mixer volumes). Everything is
-- driven by SIMULATED time (the dt sum), so the run is deterministic under
-- the floored dt automated runs use.
--
-- Covered here:
--   * tween.to with an onUpdate closure in the RECOMMENDED style (re-fetch
--     via the world API every step, never capture components) + onComplete
--   * tween.fade / tween.volume typed helpers (ease + implicit from-value)
--   * handle:cancel() - updates stop, onComplete never fires
--   * the delay parameter - the first step waits for simulated time
--   * exact landing - the last step applies the end value exactly

local time = 0.0
local cancelHandle = nil
local cancelUpdatesAtCancel = -1
local finishedAt = -1.0

local function fail(reason)
	if shared.tween.failed == "" then
		shared.tween.failed = reason
	end
end

function init(self)
	shared.tween = {
		updates = 0,			-- tween.to onUpdate calls
		completes = 0,			-- tween.to onComplete calls (must end at exactly 1)
		cancelUpdates = 0,		-- updates of the tween we cancel
		delayFirstAt = -1.0,	-- sim time of the delayed tween's first step
		delayDone = false,		-- the delayed tween completed
		failed = "",			-- first failure reason ("" = healthy)
		done = false,			-- everything ran and self-verified
	}

	-- [1] the generic closure tween: drive the Tweener's x from 0 to 3 in
	-- half a second. The closure RE-FETCHES the transform by id every step -
	-- the safe style (captured component pointers dangle when objects die;
	-- id-keyed helpers below get reaped automatically instead).
	tween.to(0.0, 3.0, 0.5, "quadOut",
		function(x)
			shared.tween.updates = shared.tween.updates + 1
			local transform = world.getTransform("Tweener")
			if transform then
				local position = transform:getPosition()
				transform:setPosition(Vector3(x, position.y, position.z))
			end
		end,
		function()
			shared.tween.completes = shared.tween.completes + 1
			finishedAt = time
		end)

	-- [2] typed helpers: sprite alpha and the mixer group volume (the same
	-- channel the two-line ducking recipe uses)
	tween.fade("Tweener", 0.25, 0.5, "sineInOut")
	tween.volume("music", 0.25, 0.5)

	-- [3] a long tween we cancel shortly after start: onComplete firing or
	-- updates continuing past the cancel are failures
	cancelHandle = tween.to(0.0, 1.0, 30.0, "linear",
		function(v)
			shared.tween.cancelUpdates = shared.tween.cancelUpdates + 1
		end,
		function()
			fail("the cancelled tween fired onComplete")
		end)

	-- [4] a delayed tween: its first step must not arrive before 0.4s of
	-- simulated time
	tween.to(0.0, 1.0, 0.2, "linear",
		function(v)
			if shared.tween.delayFirstAt < 0 then
				shared.tween.delayFirstAt = time
			end
		end,
		function()
			shared.tween.delayDone = true
		end,
		0.4)

	-- [5] the SoundComponent surface (the first sound binding): reached via
	-- the world accessor. Guarded honestly: without a working OpenAL device
	-- addSound refuses and the sound checks stay skipped
	-- (shared.tween.soundChecked stays false - the C++ side mirrors that).
	local snd = world.getSound(self.id)
	if snd == nil then
		fail("world.getSound found no SoundComponent on " .. self.id)
	elseif snd:addSound("beep", "beep.wav", false, true) then
		snd:setVolume("beep", 0.6)
		snd:setGroup("beep", "music")
		if math.abs(snd:getVolume("beep") - 0.6) > 0.001 then
			fail("snd:setVolume did not stick")
		end
		if snd:getGroup("beep") ~= "music" then
			fail("snd:setGroup did not stick")
		end
		if not snd:play("beep") then
			fail("snd:play('beep') did not start the sound")
		end
		shared.tween.soundChecked = true
	end
end

function update(self, dt)
	time = time + dt

	-- cancel [3] once it demonstrably ran
	if cancelHandle ~= nil and time >= 0.1 and shared.tween.cancelUpdates > 0 then
		if not cancelHandle:isActive() then
			fail("the long tween was not active before cancel")
		end
		cancelHandle:cancel()
		if cancelHandle:isActive() then
			fail("handle:cancel() left the tween active")
		end
		cancelUpdatesAtCancel = shared.tween.cancelUpdates
		cancelHandle = nil
	end

	-- verdict once everything had time to run its course (all durations plus
	-- the delay end well before 1.5s of simulated time)
	if not shared.tween.done and time >= 1.5 then
		if shared.tween.completes ~= 1 then
			fail("onComplete fired " .. shared.tween.completes .. " times")
		end
		if cancelUpdatesAtCancel < 0 then
			fail("the cancel scenario never armed")
		elseif shared.tween.cancelUpdates ~= cancelUpdatesAtCancel then
			fail("the cancelled tween kept updating")
		end
		-- slack: the script's dt sum (Lua doubles) and the manager's
		-- (C++ floats) accumulate the same steps with different rounding
		if shared.tween.delayFirstAt >= 0 and shared.tween.delayFirstAt < 0.35 then
			fail(string.format("the delayed tween started at %.3f (< 0.4s delay)",
				shared.tween.delayFirstAt))
		end
		if not shared.tween.delayDone then
			fail("the delayed tween never completed")
		end
		if finishedAt >= 0 and finishedAt < 0.45 then
			fail(string.format("tween.to completed at %.3f (< its 0.5s duration)",
				finishedAt))
		end
		if shared.tween.soundChecked then
			-- exercise the stop side of the binding on the way out
			local snd = world.getSound(self.id)
			if snd ~= nil then
				snd:stop("beep")
				snd:stopAllSounds()
			end
		end
		shared.tween.done = true
	end
end
