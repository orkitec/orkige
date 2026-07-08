-- game.lua - the Continuity half of the roller prototype, attached to the
-- "Game" object in scenes/main.oscene through a ScriptComponent.
--
-- This script owns everything AROUND the rolling: the fastgui HUD, the
-- play/move mode state machine and the sliding-tile "move the world" logic.
--
--   play  the ball rolls under tilt gravity (ball.lua); TAB enters...
--   move  ...move-world mode: physics PAUSES (PhysicsWorld:setPaused), the
--         cursor sprite highlights the EMPTY grid slot and an arrow key
--         slides the neighboring tile INTO the empty slot, moving in the
--         pressed direction (15-puzzle style). The tile's sprites AND its
--         kinematic wall bodies move together: every object of the tile
--         group is teleported by the slot delta (RigidBodyComponent:
--         teleport moves the collision geometry even while paused).
--         TAB again resumes play (and re-centers the simulated tilt - the
--         arrows meant "slide", not "tilt", while in move mode).
--
-- v1 honesty: a tile carrying the BALL refuses to slide (warning flash) -
-- true Continuity edge-compatibility rules are future work. The tile groups
-- are plain Lua tables of object ids (scene = data, grouping = script).
--
-- Coordination with ball.lua through `shared.roller`:
--   mode, slides, refusals, emptySlot, gameReady   written HERE
--   x, y, wins, respawns, ballReady                written by ball.lua

local KC = KeyEventData.KeyCode
local LA = FastGuiLabel.LabelAlignment

local FONT_HUD   = 9	-- 10x14 px glyphs in the atlas
local FONT_TITLE = 24	-- 20x28 px glyphs
local Z_HUD, Z_WARN, Z_WIN = 12, 13, 14
local PROJECT_RESOURCE_GROUP = "OrkigeProject"

local TILE = 6.0
-- slot index -> center (grid: 0 bottom-left, 1 bottom-right, 2 top-left,
-- 3 top-right; must match Util/make_roller_assets.py)
local SLOTS = {
	[0] = { x = -3.0, y = -3.0, col = 0, row = 0 },
	[1] = { x =  3.0, y = -3.0, col = 1, row = 0 },
	[2] = { x = -3.0, y =  3.0, col = 0, row = 1 },
	[3] = { x =  3.0, y =  3.0, col = 1, row = 1 },
}
local WARN_SECONDS = 1.2
local WIN_BANNER_SECONDS = 2.5

--- per-instance state --------------------------------------------------------
local input                  -- InputManager singleton
local physics                -- PhysicsWorld singleton
local gui, factory           -- the Lua-booted UI system
local layers = {}            -- hud/warn/win layers
local hud = {}               -- mode, wins, hint labels
local mode = "play"
local warnTimer = 0.0
local winTimer = 0.0
local winsSeen = 0
local keyWasDown = {}        -- edge detection per key code

-- the movable world: each tile = its slot + the ids of ALL its objects
-- (frame + walls + riders like the goal star); the Ball is NOT in a group
local tiles = {
	A = { slot = 0, objects = { "TileA_Frame", "TileA_WallTop",
		"TileA_WallBottom", "TileA_WallLeft", "TileA_Ledge" } },
	B = { slot = 3, objects = { "TileB_Frame", "TileB_WallTop",
		"TileB_WallBottom", "TileB_WallRight", "Goal" } },
	C = { slot = 2, objects = { "TileC_Frame", "TileC_WallTop",
		"TileC_WallBottom", "TileC_WallLeft", "TileC_WallRight" } },
}
local emptySlot = 1
local slides = 0
local refusals = 0

--- helpers ---------------------------------------------------------------

local function keyPressed(kc)
	local down = input:isKeyDown(kc)
	local pressed = down and not keyWasDown[kc]
	keyWasDown[kc] = down
	return pressed
end

local function centeredLabel(id, font, text, y, z)
	local label = factory:createLabel(id, font, text, Vector2(0, y), "", z, false)
	label:centerHorizontal()
	return label
end

local function slotAt(col, row)
	for index, slot in pairs(SLOTS) do
		if slot.col == col and slot.row == row then
			return index
		end
	end
	return nil
end

local function tileAtSlot(slotIndex)
	for key, tile in pairs(tiles) do
		if tile.slot == slotIndex then
			return key, tile
		end
	end
	return nil, nil
end

local function moveCursorToEmpty()
	local cursor = world.getTransform("Cursor")
	if cursor ~= nil then
		local slot = SLOTS[emptySlot]
		cursor:setPosition(Vector3(slot.x, slot.y, 0))
	end
end

local function setCursorVisible(visible)
	local sprite = world.getSprite("Cursor")
	if sprite ~= nil then
		sprite:setSpriteVisible(visible)
	end
end

local function publishState()
	shared.roller.mode = mode
	shared.roller.slides = slides
	shared.roller.refusals = refusals
	shared.roller.emptySlot = emptySlot
end

local function setMode(newMode)
	mode = newMode
	local moving = (mode == "move")
	-- Continuity: the world only moves while the simulation stands still
	physics:setPaused(moving)
	setCursorVisible(moving)
	if moving then
		moveCursorToEmpty()
		hud.mode:setText("MOVE WORLD - arrows slide tiles - TAB back")
	else
		-- the arrows meant "slide tiles" in move mode - reset the simulated
		-- tilt so play resumes with gravity straight down (desktop only;
		-- a real accelerometer is not resettable, and needs no reset)
		input:setTiltAngle(0)
		hud.mode:setText("PLAY - LEFT/RIGHT tilt the world - TAB move mode")
	end
	publishState()
end

-- is the ball inside this tile's region right now?
local function ballInTile(tile)
	local ball = world.getTransform("Ball")
	if ball == nil then
		return false
	end
	local slot = SLOTS[tile.slot]
	local position = ball:getPosition()
	local half = TILE / 2.0 + 0.05
	return math.abs(position.x - slot.x) <= half and
		math.abs(position.y - slot.y) <= half
end

-- slide the tile NEXT TO the empty slot into it, moving in direction
-- (dx, dy) - i.e. the tile at (empty - direction). 15-puzzle semantics.
local function trySlide(dx, dy)
	local empty = SLOTS[emptySlot]
	local sourceSlot = slotAt(empty.col - dx, empty.row - dy)
	if sourceSlot == nil then
		return	-- the source would be outside the grid
	end
	local key, tile = tileAtSlot(sourceSlot)
	if tile == nil then
		return	-- no tile there (cannot happen with one hole, but honest)
	end
	if ballInTile(tile) then
		-- v1 rule: the ball must not ride a moving tile - refuse + flash
		refusals = refusals + 1
		warnTimer = WARN_SECONDS
		layers.warn:setVisible(true)
		publishState()
		return
	end
	local from, to = SLOTS[tile.slot], SLOTS[emptySlot]
	local deltaX, deltaY = to.x - from.x, to.y - from.y
	for _, id in ipairs(tile.objects) do
		local transform = world.getTransform(id)
		if transform ~= nil then
			local position = transform:getPosition()
			local target = Vector3(position.x + deltaX,
				position.y + deltaY, position.z)
			local body = world.getRigidBody(id)
			if body ~= nil and body:hasBody() then
				-- sprites AND collision geometry move together, even while
				-- the simulation is paused
				body:teleport(target, Quaternion(1, 0, 0, 0))
			else
				transform:setPosition(target)
			end
		end
	end
	emptySlot, tile.slot = tile.slot, emptySlot
	slides = slides + 1
	moveCursorToEmpty()
	publishState()
	print("game.lua: tile " .. key .. " slid to slot " .. tile.slot ..
		" (empty now " .. emptySlot .. ")")
end

--- ScriptComponent lifecycle -----------------------------------------------

function init(self)
	input = InputManager.getSingleton()
	physics = PhysicsWorld.getSingleton()

	factory = FastGuiFactory()
	gui = FastGuiManager(factory, "fastgui_default", PROJECT_RESOURCE_GROUP)

	local engine = Engine.getSingleton()
	local w, h = engine:getWindowWidth(), engine:getWindowHeight()

	-- HUD (z 12): mode indicator, wins counter, controls hint
	hud.mode = factory:createLabel("hud.mode", FONT_HUD, "", Vector2(16, 16),
		"", Z_HUD, false)
	hud.wins = factory:createLabel("hud.wins", FONT_HUD, "WINS: 0",
		Vector2(w - 148, 16), "", Z_HUD, false)
	hud.hint = centeredLabel("hud.hint", FONT_HUD,
		"roll the ball to the star - rearrange the world to get there",
		h - 34, Z_HUD)

	-- warning flash (z 13): shown when a slide is refused
	local warn = centeredLabel("warn.label", FONT_TITLE, "BALL IN TILE!",
		math.floor(h * 0.42), Z_WARN)
	-- win banner (z 14)
	local win = centeredLabel("win.banner", FONT_TITLE, "YOU WIN!",
		math.floor(h * 0.30), Z_WIN)

	layers.hud = hud.mode:getLayer()
	layers.warn = warn:getLayer()
	layers.win = win:getLayer()
	layers.warn:setVisible(false)
	layers.win:setVisible(false)

	shared.roller = shared.roller or {}
	shared.roller.gameReady = true
	setMode("play")
	print("game.lua: world of " .. slides .. " slides ready - TAB moves the world")
end

function update(self, dt)
	-- TAB toggles between rolling and rearranging the world
	if keyPressed(KC.KC_TAB) then
		setMode(mode == "play" and "move" or "play")
	end

	if mode == "move" then
		if keyPressed(KC.KC_LEFT) then
			trySlide(-1, 0)
		elseif keyPressed(KC.KC_RIGHT) then
			trySlide(1, 0)
		elseif keyPressed(KC.KC_UP) then
			trySlide(0, 1)
		elseif keyPressed(KC.KC_DOWN) then
			trySlide(0, -1)
		end
	end

	-- warning flash timeout
	if warnTimer > 0.0 then
		warnTimer = warnTimer - dt
		if warnTimer <= 0.0 then
			layers.warn:setVisible(false)
		end
	end

	-- win watch: ball.lua counts wins, the banner celebrates them
	local ballWins = shared.roller.wins or 0
	if ballWins ~= winsSeen then
		winsSeen = ballWins
		hud.wins:setText("WINS: " .. winsSeen)
		winTimer = WIN_BANNER_SECONDS
		layers.win:setVisible(true)
	end
	if winTimer > 0.0 then
		winTimer = winTimer - dt
		if winTimer <= 0.0 then
			layers.win:setVisible(false)
		end
	end
end

function shutdown(self)
	-- release the UI deterministically (same pattern as jumper-lua)
	if gui ~= nil then
		gui:destroyAllWidgets()
	end
	hud, layers = {}, {}
	gui, factory = nil, nil
	-- never leave the simulation paused behind
	if physics ~= nil then
		physics:setPaused(false)
	end
	print("game.lua: released")
end
