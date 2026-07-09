-- ball.lua - the Rolando half of the roller prototype, attached to the
-- "Ball" object in scenes/main.oscene through a ScriptComponent.
--
-- The ball is a dynamic PLANAR sphere (RigidBodyComponent planar mode locks
-- it to the XY plane and Z-axis rotation) with a SpriteComponent visual -
-- the sprite rotates with the body, so the ball visibly ROLLS.
--
-- The tilt mechanic: InputManager:getTilt() answers the normalized gravity
-- direction - accelerometer-backed on devices, LEFT/RIGHT(A/D) simulated on
-- desktops - and every frame in play mode this script points world gravity
-- along it (physics:setGravity = the Rolando mechanic).
--
-- The 2D camera: init switches the engine camera to ORTHOGRAPHIC projection
-- (Engine:setCameraOrthographic) framing the whole 2x2 tile grid; it never
-- moves - the WORLD is what moves in this game.
--
-- The win: the "Goal" object carries a STATIC SENSOR RigidBodyComponent (WP
-- #88) on the Trigger layer, which senses the dynamic ball. When the ball
-- rolls into it, the engine's contact drain calls onContactBegin(self, other)
-- below with the goal as `other` - the OLD GOAL_RADIUS distance poll is gone.
--
-- Coordination with game.lua (the Continuity half) through `shared.roller`:
--   mode ("play"/"move")  written by game.lua; gravity/win/respawn only run
--                         while "play" (the sim is paused in "move" anyway)
--   x, y, wins, respawns, tiltX, tiltY, ballReady   written HERE

local TS = RenderNode.TransformSpace

--- tuning --------------------------------------------------------------------
-- GRAVITY is promoted to the `roller_gravity` console variable (WP #83): its
-- default is 18.0, but it can be tuned LIVE from the editor Console ("set
-- roller_gravity 30"), the Lua cvar table or the MSG_SET_CVAR debug message,
-- and persists per project as the manifest Setting "cvar.roller_gravity". The
-- magnitude is read fresh every frame (below), so a change takes effect at
-- once - this is the "equivalent live path" of an onChange for a pure-Lua game
-- (the C++ onChange seam is exercised by the CVarProtocolBehaviour engine test).
local GRAVITY_DEF  = 18.0   -- gravity magnitude m/s^2 (snappy rolling) - the cvar default
local KILL_PLANE_Y = -12.0  -- below the grid = fell into the empty slot
local ORTHO_SIZE   = 7.5    -- vertical half-extent: frames the 12wu grid

-- the live gravity magnitude: the cvar when available (registered in init),
-- otherwise the built-in default (keeps the script honest with scripting quirks)
local function gravity()
	if cvar ~= nil then
		return cvar.getNumber("roller_gravity", GRAVITY_DEF)
	end
	return GRAVITY_DEF
end

--- per-instance state --------------------------------------------------------
local physics               -- PhysicsWorld singleton
local input                 -- InputManager singleton
-- the respawn pose: captured from the Ball's SCENE position in init - the
-- generated scene is the single source of the spawn (no hand-copied
-- derivation of the generator's floor math anymore)
local SPAWN    = { x = 0.0, y = 0.0, z = 0.0 }
local wins     = 0
local respawns = 0

local function publishState(x, y, tilt)
	local state = shared.roller
	state.x, state.y = x, y
	state.wins, state.respawns = wins, respawns
	state.tiltX, state.tiltY = tilt.x, tilt.y
end

-- pose reset in the simulation AND the scene, momentum killed
local function resetBall(self)
	self.rigidbody:teleport(Vector3(SPAWN.x, SPAWN.y, SPAWN.z),
		Quaternion(1, 0, 0, 0))
end

--- ScriptComponent lifecycle -------------------------------------------------

function init(self)
	physics = PhysicsWorld.getSingleton()
	input = InputManager.getSingleton()

	-- the 2D camera: orthographic, fixed on the grid center. The grid spans
	-- y -6..6; ORTHO_SIZE leaves a margin for the HUD.
	local engine = Engine.getSingleton()
	engine:setCameraOrthographic(ORTHO_SIZE)
	engine:setWindowBackgroundColour(0.10, 0.12, 0.18)	-- dark slate void
	local cameraNode = engine:getCamera():getNode()
	cameraNode:setPosition(Vector3(0, 0, 20))
	cameraNode:lookAt(Vector3(0, 0, 0), TS.TS_WORLD, Vector3(0, 0, -1))

	-- the scene position IS the spawn (the Ball is a scene root, so world
	-- position == the serialized transform)
	local start = self.transform:getWorldPosition()
	SPAWN.x, SPAWN.y, SPAWN.z = start.x, start.y, start.z

	-- register the tunable gravity cvar (idempotent: a manifest override or a
	-- value already set over the debug link survives this registration)
	if cvar ~= nil then
		cvar.registerNumber("roller_gravity", GRAVITY_DEF)
	end
	physics:setGravity(Vector3(0, -gravity(), 0))

	shared.roller = shared.roller or {}
	shared.roller.ballReady = true
	publishState(SPAWN.x, SPAWN.y, input:getTilt())
	print("ball.lua: rolling (tilt sensor: " ..
		tostring(input:isTiltSensorAvailable()) .. ")")
end

function update(self, dt)
	-- tick counter: the player selfcheck probes that a DEACTIVATED ball
	-- stops receiving updates (GameObject active state gates the ticks)
	shared.roller.ballUpdates = (shared.roller.ballUpdates or 0) + 1

	local body = self.rigidbody
	if body == nil or not body:hasBody() then
		return	-- the rigid body is created lazily on its first update
	end
	local position = self.transform:getPosition()
	local tilt = input:getTilt()

	local mode = shared.roller.mode or "play"
	if mode == "play" then
		-- Rolando: gravity follows the tilt every frame (magnitude from the
		-- live roller_gravity cvar, so tuning it is felt immediately)
		local g = gravity()
		physics:setGravity(Vector3(tilt.x * g, tilt.y * g, 0))
		-- Jolt lets resting bodies fall asleep and a gravity CHANGE alone
		-- does not wake them - nudge the ball with a tiny impulse so a
		-- re-tilted world always starts it rolling again
		if not physics:isBodyActive(body:getBodyId()) then
			body:applyImpulse(Vector3(tilt.x * 0.01, tilt.y * 0.01, 0))
		end

		-- rolled out of the world (usually: through an opening into the
		-- empty slot)? back to the spawn
		if position.y < KILL_PLANE_Y then
			print("ball.lua: fell out of the world - respawning")
			respawns = respawns + 1
			resetBall(self)
			publishState(SPAWN.x, SPAWN.y, tilt)
			return
		end
		-- the win is no longer polled here: the goal SENSOR fires
		-- onContactBegin below when the ball rolls into it (WP #88)
	end

	publishState(position.x, position.y, tilt)
end

-- WP #88: the goal's static sensor detected the ball. The engine drains the
-- worker-thread contact on the main thread and calls this with the goal as
-- `other`. Only "Goal" contacts win (the ball also contacts obstacle walls,
-- but those are not sensors and never reach here). Guarded to "play" mode -
-- the sim is paused in "move" mode, so no contacts fire there anyway.
function onContactBegin(self, other)
	if other.id ~= "Goal" then
		return
	end
	if (shared.roller.mode or "play") ~= "play" then
		return
	end
	wins = wins + 1
	print("ball.lua: WIN #" .. wins .. " (goal sensor) - back to the start")
	-- star-collect juice: a burst of golden particles (WP #82)
	if self.particles ~= nil then
		self.particles:burst(24)
	end
	resetBall(self)
	publishState(SPAWN.x, SPAWN.y, input:getTilt())
end

function shutdown(self)
	print("ball.lua: detached from '" .. self.id .. "'")
end
