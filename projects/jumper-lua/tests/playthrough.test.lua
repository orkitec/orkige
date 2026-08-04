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
-- run - which is why every waitUntil below carries a limit.

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

-- The INPUT-DRIVEN leg: the test presses what a player presses and then reads
-- what the game did with it. Every press goes through the engine's one input
-- synthesis path, so the game sees a key exactly as the platform would have
-- delivered it - the action layer, the buffered jump and the title-screen
-- state machine all run for real.
--
-- Targets are NAMED ACTIONS ("jump", "move+x"), which is what the game code
-- reads back, so re-binding a key leaves this test meaning what it meant.
-- "RETURN" is a raw key because the title screen watches the key itself.
--
-- Deliberately modest in distance: the starting platform spans x = -2..2, so
-- the walk stays well inside it. Proving that a press moves the character is
-- the point; walking the whole level would be a slower test that fails for
-- reasons other than input.
test("the character walks and jumps when the test presses",
	{ scene = "scenes/main.oscene" },
	function(t)
		t.waitUntil(playerIsUp, 300)

		-- the game boots into its title screen and the player script only
		-- takes control while the flow says "playing" - so start the game the
		-- way a player does. One tap = one press edge, which is what the
		-- state machine's own edge detection needs.
		t.tap("RETURN")
		t.waitUntil(function()
			return shared.game == nil or shared.game.state == "playing"
		end, 120)

		-- WALK: hold the move action's positive x direction, then let go
		local startX = shared.jumper.x
		t.press("move+x")
		t.wait(0.3)
		t.release("move+x")
		t.wait(0.1)		-- the velocity approach coasts to a stop
		t.truthy(shared.jumper.x > startX + 0.5,
			"holding move+x moved the character by only " ..
			tostring(shared.jumper.x - startX) .. " m")
		t.eq(shared.jumper.respawns, 0, "the character walked off the level")

		-- JUMP: one tap, and the game's own buffered-jump rule lifts it
		t.waitUntil(function() return shared.jumper.grounded end, 300)
		local groundY = shared.jumper.y
		t.tap("jump")
		-- a tap that never lifted the character times out here, by name
		t.waitUntil(function() return shared.jumper.y > groundY + 0.2 end, 60)

		-- and it comes back down onto the platform it took off from
		t.waitUntil(function() return shared.jumper.grounded end, 300)
		t.eq(shared.jumper.respawns, 0, "the character fell out while jumping")
	end)
