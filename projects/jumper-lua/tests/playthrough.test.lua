-- playthrough.test.lua - the PLAY-MODE tier: tests that need a live world.
--
-- Declaring a `scene` option is what makes a test a play-mode test. The
-- runner loads that scene into a freshly cleared world, runs this body as a
-- task, and resumes it once per frame in the script phase - so `t.wait`,
-- `t.waitFrames` and `t.waitUntil` suspend the test while the GAME keeps
-- running: physics steps, scripts update, the camera follows.
--
-- Every test gets a frame budget. A wait that never comes true fails BY NAME
-- ("the condition never became true within N frames") instead of hanging the
-- run - which is why every waitUntil below carries a limit and a message.

local KILL_PLANE_Y = -10.0

-- the player script publishes what a spectator can see (position, grounded,
-- wins, respawns) in `shared.jumper`; waiting for it is waiting for the game
-- to have actually started
local function playerIsUp()
	return shared.jumper ~= nil and shared.jumper.grounded
end

test("the level holds the player up", { scene = "scenes/main.oscene" },
	function(t)
		-- the body is created lazily on its first update and then has to
		-- FALL onto the platform, so this is a wait, not an assertion
		t.waitUntil(playerIsUp, 300)

		local state = shared.jumper
		t.truthy(state.y > KILL_PLANE_Y,
			"the player fell out of the level: y = " .. tostring(state.y))
		t.eq(state.respawns, 0, "the player respawned before doing anything")
		t.eq(state.wins, 0, "the round was won before the player moved")

		-- and it STAYS held: a second of simulation with nothing pressed
		t.wait(1.0)
		t.truthy(shared.jumper.y > KILL_PLANE_Y,
			"the player sank through the platform: y = " ..
			tostring(shared.jumper.y))
		t.eq(shared.jumper.respawns, 0, "the player fell out while idle")
	end)

test("reaching the buddy wins the round", { scene = "scenes/main.oscene" },
	function(t)
		t.waitUntil(playerIsUp, 300)

		-- the goal marker's position is SCENE DATA, like it is for the game
		local goalTransform = world.getTransform("Goal")
		t.truthy(goalTransform ~= nil, "the scene has no 'Goal' object")
		local goal = goalTransform:getPosition()

		-- put the player on the buddy the way the game itself respawns: the
		-- pose is reset in the simulation AND in the scene, all momentum
		-- killed. The win is then detected by the GAME's own rule, in the
		-- game's own update - this test only arranges the situation.
		local body = world.getRigidBody("Player")
		t.truthy(body ~= nil, "the scene has no 'Player' rigid body")
		local physics = PhysicsWorld.getSingleton()
		physics:setBodyTransform(body:getBodyId(),
			Vector3(goal.x, goal.y, goal.z), Quaternion(1, 0, 0, 0))
		body:setLinearVelocity(Vector3(0, 0, 0))
		body:setAngularVelocity(Vector3(0, 0, 0))
		world.getTransform("Player"):setPosition(
			Vector3(goal.x, goal.y, goal.z))

		-- the game reports its own finished state: the win counter the player
		-- script publishes goes up, and it respawns for another round
		t.waitUntil(function() return shared.jumper.wins >= 1 end, 300)
		t.eq(shared.jumper.wins, 1, "the win was not counted exactly once")
	end)
