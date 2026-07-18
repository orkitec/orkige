-- mannequin.component.lua - one crowd member of the character-cast vignette.
-- The director ramps how many mannequins are active (the skinning-cost stress
-- ramp); when THIS one activates, its script runs once and staggers its walk
-- cycle so the crowd never marches in lock-step. This is the moving proof of
-- the Lua animation seam: the sibling skeletal rig is reached through the
-- `self.animation` handle and driven with the reflected clip-playback verbs
-- (playAnimation / setAnimationTime / setSpeed).

-- a per-instance seed in [0, 1), authored by make_benchmark_assets.py across
-- the grid, so each mannequin picks a distinct phase + tempo
properties = {
	seed = { type = "number", default = 0.0, min = 0.0, max = 1.0 },
}

-- the walk clip is 1 s long (Util/make_character_rig.py)
local WALK_LENGTH = 1.0

function init(self)
	local seed = self.seed or 0.0
	if self.animation == nil then
		-- the seam is broken: no sibling handle reached the rig
		print("mannequin: seam FAIL - no self.animation handle")
		return
	end
	-- enable the walk cycle, then SEEK it to a phase offset and vary the tempo
	-- a little around 1.0 - all through the reflected verbs on self.animation
	local played = self.animation:playAnimation("walk", true)
	self.animation:setAnimationTime("walk", seed * WALK_LENGTH)
	self.animation:setSpeed(0.85 + seed * 0.3)
	-- also confirm the OTHER-object accessor resolves this same rig (the
	-- world.getAnimation leg of the seam), and report so the anim probe can
	-- assert the whole seam functioned on the running flavor
	local viaWorld = world.getAnimation(self.id) ~= nil
	print(string.format("mannequin: seam ok anim=1 world=%d played=%d",
		viaWorld and 1 or 0, played and 1 or 0))
end

function update(self, dt)
	-- the animation advances itself (the AnimationComponent ticks); nothing
	-- to do per frame - the stagger is a one-time phase/tempo offset
end
