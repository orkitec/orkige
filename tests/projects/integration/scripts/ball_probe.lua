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
		busContact = 0,
	}
	-- discover the goal by TAG, not a hardcoded id
	local goals = world.findByTag("goal")
	shared.integration.found = #goals
	if #goals > 0 then
		goalId = goals[1].id
		shared.integration.foundGoal = goalId
	end
	-- the SAME contact ALSO reaches the message bus (physics.contactBegin, the
	-- object ids in the payload) - additive to the bespoke onContactBegin hook
	-- below. The bus mirror is emitted at the contact drain, which runs AFTER
	-- the script phase, so it lands one frame LATER than the hook; the selfcheck
	-- waits for it and asserts the mirror count matches the bespoke count.
	events.subscribe("physics.contactBegin", function(e)
		if goalId ~= nil and (e.a == goalId or e.b == goalId) then
			shared.integration.busContact = shared.integration.busContact + 1
		end
	end)
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
