-- player.lua - the jumper game, reimplemented in pure Lua.
--
-- This is the first real Orkige game script: the whole behavior of
-- samples/jumper (velocity-controlled movement, buffered jumping, kill-plane
-- respawn, goal detection, camera follow) lives HERE, attached to the Player
-- object in scenes/main.oscene through a ScriptComponent. Open the project,
-- press Play, play the game - nothing is compiled.
--
-- ScriptComponent contract: init(self) once after load, update(self, dt)
-- every frame, shutdown(self) on remove. `self` carries the owner
-- (self.id, self.gameObject) and the sibling components (self.transform,
-- self.rigidbody, self.model). Other objects are reached through the global
-- `world` table; deliberately shared state lives in the global `shared`
-- table (each script instance runs in its own environment - locals here are
-- per-instance and invisible to other scripts).
--
-- Controls: A/D or Left/Right run along the level (x), W/S or Up/Down dodge
-- in depth (z), SPACE jumps (with a 0.12s buffer - a press just before
-- landing still jumps). Falling below y = -10 respawns at the start;
-- reaching the buddy at the end wins and respawns for another round.

local KC = KeyEventData.KeyCode
local TS = SceneNode.TransformSpace

--- tuning (the "feel" numbers - same values as samples/jumper/main.cpp) ----
local MOVE_SPEED   = 4.5        -- target run speed m/s
local ACCEL_RATE   = 12.0       -- velocity approach rate 1/s
local JUMP_SPEED   = 8.0        -- take-off velocity m/s (apex ~1.6 m)
local GRAVITY_Y    = -20.0      -- ~2x earth, snappy platformer arcs
local KILL_PLANE_Y = -10.0      -- fall-out respawn line
local GOAL_RADIUS  = 1.5        -- win distance to the goal marker
local CAMERA_RATE  = 5.0        -- camera follow approach rate
local JUMP_BUFFER_SECONDS = 0.12

-- the player capsule (matches jumper_player.glb and the RigidBodyComponent
-- shape in the scene); the ground probe must start OUTSIDE the capsule -
-- Jolt's closest-hit ray treats convex shapes as solid (see JumperLogic.h)
local CAPSULE_HALF_HEIGHT = 0.25
local CAPSULE_RADIUS      = 0.35
local GROUND_SKIN         = 0.02
local GROUND_PROBE_LENGTH = 0.2

local SPAWN             = { x = 0.0, y = 1.0, z = 0.0 }
local CAMERA_OFFSET     = { x = 0.0, y = 2.2, z = 8.0 }
local CAMERA_LOOK_AHEAD = { x = 1.5, y = 0.8, z = 0.0 }

--- per-instance state -------------------------------------------------------
local physics                   -- PhysicsWorld singleton
local input                     -- InputManager singleton
local cameraNode                -- the engine camera's SceneNode
local goalTransform             -- the Goal object's TransformComponent or nil
local grounded     = false
local jumpBuffer   = 0.0
local spaceWasDown = false      -- isKeyDown edge detection for the buffer
local wins         = 0
local respawns     = 0

--- helpers -------------------------------------------------------------------

-- frame-rate independent exponential approach (the same curve as
-- JumperLogic::approach): never overshoots, ~63% of the distance per 1/rate s
local function approach(current, target, rate, dt)
	return current + (target - current) * (1.0 - math.exp(-rate * dt))
end

-- 1 / 0 / -1 from a pair of keys (each with its WASD alias)
local function axis(negativeA, negativeB, positiveA, positiveB)
	local value = 0
	if input:isKeyDown(positiveA) or input:isKeyDown(positiveB) then
		value = value + 1
	end
	if input:isKeyDown(negativeA) or input:isKeyDown(negativeB) then
		value = value - 1
	end
	return value
end

-- teleport the player body (respawn and the selfcheck use this): pose reset
-- in the simulation AND the scene, all momentum killed
local function teleport(self, x, y, z)
	local body = self.rigidbody
	physics:setBodyTransform(body:getBodyId(), Vector3(x, y, z),
		Quaternion(1, 0, 0, 0))
	body:setLinearVelocity(Vector3(0, 0, 0))
	body:setAngularVelocity(Vector3(0, 0, 0))
	self.transform:setPosition(Vector3(x, y, z))
end

local function respawn(self)
	respawns = respawns + 1
	teleport(self, SPAWN.x, SPAWN.y, SPAWN.z)
end

-- what this run looks like from the outside: the selfcheck (and anything
-- else) watches the shared table instead of poking script internals
local function publishState(x, y, z)
	local state = shared.jumper
	state.x, state.y, state.z = x, y, z
	state.grounded = grounded
	state.wins = wins
	state.respawns = respawns
end

--- ScriptComponent lifecycle -------------------------------------------------

function init(self)
	physics = PhysicsWorld.getSingleton()
	input = InputManager.getSingleton()
	physics:setGravity(Vector3(0.0, GRAVITY_Y, 0.0))

	-- scene = data: the goal marker's position comes from the loaded level
	goalTransform = world.getTransform("Goal")
	if goalTransform == nil then
		print("player.lua: no 'Goal' object in the scene - win check disabled")
	end

	-- the camera starts on the spawn point and follows from update().
	-- lookAt takes all three arguments across the binding (no default args);
	-- Vector3(0,0,-1) is Ogre's NEGATIVE_UNIT_Z default localDirectionVector.
	-- Per-frame lookAt is roll-free: the engine sets a fixed yaw axis on its
	-- camera nodes (createDefaultCameraAndViewport).
	cameraNode = Engine.getSingleton():getCamera(0):getParentSceneNode()
	cameraNode:setPosition(Vector3(SPAWN.x + CAMERA_OFFSET.x,
		SPAWN.y + CAMERA_OFFSET.y, SPAWN.z + CAMERA_OFFSET.z))
	cameraNode:lookAt(Vector3(SPAWN.x, SPAWN.y, SPAWN.z), TS.TS_WORLD,
		Vector3(0, 0, -1))

	shared.jumper = {}
	publishState(SPAWN.x, SPAWN.y, SPAWN.z)
	print("player.lua: jumper behavior attached to '" .. self.id .. "'")
end

function update(self, dt)
	local body = self.rigidbody
	if body == nil or not body:hasBody() then
		return	-- the rigid body is created lazily on its first update
	end
	local position = self.transform:getPosition()
	local px, py, pz = position.x, position.y, position.z

	-- grounded: short ray just below the capsule; never accept a self-hit
	local probe = physics:castRayHit(
		Vector3(px, py - (CAPSULE_HALF_HEIGHT + CAPSULE_RADIUS + GROUND_SKIN), pz),
		Vector3(0, -1, 0), GROUND_PROBE_LENGTH)
	grounded = probe.hit and probe.bodyId ~= body:getBodyId()

	-- velocity control: exponential approach to the input direction, the
	-- same in the air (full air control - tight, forgiving feel)
	local inputX = axis(KC.KC_A, KC.KC_LEFT, KC.KC_D, KC.KC_RIGHT)
	local inputZ = axis(KC.KC_W, KC.KC_UP, KC.KC_S, KC.KC_DOWN)
	local velocity = body:getLinearVelocity()
	local vx = approach(velocity.x, inputX * MOVE_SPEED, ACCEL_RATE, dt)
	local vz = approach(velocity.z, inputZ * MOVE_SPEED, ACCEL_RATE, dt)
	local vy = velocity.y

	-- buffered jump: a SPACE press up to 0.12s before landing still jumps
	local spaceDown = input:isKeyDown(KC.KC_SPACE)
	if spaceDown and not spaceWasDown then
		jumpBuffer = JUMP_BUFFER_SECONDS
	end
	spaceWasDown = spaceDown
	jumpBuffer = math.max(0.0, jumpBuffer - dt)
	if grounded and jumpBuffer > 0.0 then
		vy = JUMP_SPEED
		jumpBuffer = 0.0
		grounded = false
	end
	body:setLinearVelocity(Vector3(vx, vy, vz))

	-- keep the capsule upright: kill contact-induced spin every frame and
	-- re-align when friction still managed to tilt it (no rotation-lock
	-- DOFs are exposed through PhysicsWorld yet)
	body:setAngularVelocity(Vector3(0, 0, 0))
	local orientation = self.transform:getOrientation()
	if math.abs(orientation.w) < 0.9995 then
		physics:setBodyTransform(body:getBodyId(), Vector3(px, py, pz),
			Quaternion(1, 0, 0, 0))
	end

	-- fell out of the level?
	if py < KILL_PLANE_Y then
		print("player.lua: fell out of the level - respawning")
		respawn(self)
		publishState(SPAWN.x, SPAWN.y, SPAWN.z)
		return
	end
	-- reached the buddy at the end?
	if goalTransform ~= nil then
		local goal = goalTransform:getPosition()
		local dx, dy, dz = goal.x - px, goal.y - py, goal.z - pz
		if dx * dx + dy * dy + dz * dz <= GOAL_RADIUS * GOAL_RADIUS then
			wins = wins + 1
			print("player.lua: WIN! you reached your buddy (win #" .. wins ..
				") - respawning for another round")
			respawn(self)
			publishState(SPAWN.x, SPAWN.y, SPAWN.z)
			return
		end
	end

	-- smooth camera follow, looking slightly ahead of the player
	local cam = cameraNode:getPosition()
	cameraNode:setPosition(Vector3(
		approach(cam.x, px + CAMERA_OFFSET.x, CAMERA_RATE, dt),
		approach(cam.y, py + CAMERA_OFFSET.y, CAMERA_RATE, dt),
		approach(cam.z, pz + CAMERA_OFFSET.z, CAMERA_RATE, dt)))
	cameraNode:lookAt(Vector3(px + CAMERA_LOOK_AHEAD.x,
		py + CAMERA_LOOK_AHEAD.y, pz + CAMERA_LOOK_AHEAD.z), TS.TS_WORLD,
		Vector3(0, 0, -1))

	publishState(px, py, pz)
end

function shutdown(self)
	print("player.lua: detached from '" .. self.id .. "'")
end
