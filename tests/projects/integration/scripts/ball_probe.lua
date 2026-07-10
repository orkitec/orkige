-- ball_probe.lua - the tags + input-action + physics + contact-event chain in
-- one script (player_integration_contact_selfcheck). The ball hangs still
-- (gravity off) until the injected "jump" action fires, then falls into the
-- goal SENSOR discovered BY TAG; the sensor overlap fires onContactBegin.
local physics
local actions
local goalId

function init(self)
	physics = PhysicsWorld.getSingleton()
	actions = InputActions.getSingleton()
	-- gravity OFF at boot: the input action is what causally drops the ball
	physics:setGravity(Vector3(0.0, 0.0, 0.0))
	shared.integration = {
		found = 0, foundGoal = "", input = 0, contact = 0, contactOther = "",
	}
	-- discover the goal by TAG, not a hardcoded id
	local goals = world.findByTag("goal")
	shared.integration.found = #goals
	if #goals > 0 then
		goalId = goals[1].id
		shared.integration.foundGoal = goalId
	end
end

function update(self, dt)
	-- the named action edge (SPACE), injected by the selfcheck: it turns
	-- gravity on, so the contact that follows is INPUT-driven
	if actions:pressed("jump") then
		shared.integration.input = shared.integration.input + 1
		physics:setGravity(Vector3(0.0, -14.0, 0.0))
	end
end

function onContactBegin(self, other)
	-- only count the contact when the other body is the tag-discovered goal
	if goalId ~= nil and other.id == goalId then
		shared.integration.contact = shared.integration.contact + 1
		shared.integration.contactOther = other.id
	end
end
