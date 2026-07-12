-- game.component.lua - the world-sliding half of the roller prototype: the
-- "game" script component KIND, attached to the "Game" object in every level scene.
--
-- This script owns everything AROUND the rolling: the gui HUD, the
-- play/move mode state machine, the sliding-tile "move the world" logic AND
-- the level progression (win -> next level via a DEFERRED scene load, star
-- rating, resume-on-boot).
--
--   play  the ball rolls under tilt gravity (ball.lua); TAB enters...
--   move  ...move-world mode: physics PAUSES (PhysicsWorld:setPaused), the
--         cursor sprite highlights the EMPTY grid slot and an arrow key
--         slides the neighboring tile INTO the empty slot, moving in the
--         pressed direction (15-puzzle style). Each tile is ONE GameObject
--         subtree (a "Tile<key>" prefab instance root, frame/walls/goal as
--         children): the slide is a single TransformComponent:teleport of the
--         parent, and the engine snaps every kinematic wall body in the
--         subtree along, even while paused. TAB again resumes play.
--   complete  the goal was reached: the sim freezes, the banner shows, the
--         run is scored/persisted and after a beat the DEFERRED load switches
--         to the next level (looping to level 1 after the last).
--
-- THE SLOT MAP IS DERIVED FROM THE SCENE: the grid geometry lives once,
-- in the "Level" object's LevelComponent (cols/rows/tileSize/origin/goal/par).
-- init snaps every tile root (world.findByTag("tile")) through the component's
-- grid to recover which slot it sits in - the empty slot is the cell with no
-- tile. The old hand-kept, triplicated SLOTS Lua table is gone.
--
-- Coordination with ball.lua through `shared.roller`:
--   mode, slides, refusals, emptySlot, gameReady, moves, par, stars,
--     levelIndex, levelComplete, saved   written HERE
--   x, y, wins, respawns, ballReady                          written by ball.lua
--
-- RENDER FLAVORS: gui exists only on the classic backend (the facade
-- HUD replaces it later). engine:hasUISystem() answers which world we are in -
-- without a UI system this script skips the HUD/banners; modes, tile slides,
-- cursor sprite, the win flow and progression work identically.

-- nil-safe: the Gui usertypes only exist when the flavor carries the UI
-- system (engine:hasUISystem(), see init) - LA is only read on that path
local LA = GuiLabel and GuiLabel.LabelAlignment
-- keycode alias (the Lua convention: KeyEventData.KeyCode) for the pause toggle
local KC = KeyEventData.KeyCode

local FONT_HUD   = 9	-- 10x14 px glyphs in the atlas
local FONT_TITLE = 24	-- 20x28 px glyphs
local Z_HUD, Z_WARN, Z_WIN, Z_PAUSE = 12, 13, 14, 15
local PROJECT_RESOURCE_GROUP = "OrkigeProject"

local WARN_SECONDS = 1.2
local WIN_BANNER_SECONDS = 2.5
-- how long the level-complete banner lingers before the DEFERRED load switches
-- to the next level (a beat of celebration; the sim is frozen meanwhile)
local ADVANCE_SECONDS = 2.5

--- per-instance state --------------------------------------------------------
local input                  -- InputManager singleton (tilt reset only)
local actions                -- InputActionMap singleton (named menu actions)
local physics                -- PhysicsWorld singleton
local levels                 -- LevelManager singleton (sequence + progression)
local level                  -- LevelComponent of the "Level" object (grid geometry)
local hasUI = false          -- engine:hasUISystem() (false = HUD-less flavor)
local gui, factory           -- the Lua-booted UI system (nil when HUD-less)
local winBanner              -- win banner label (retitled on level complete)
local layers = {}            -- hud/warn/win layers
local hud = {}               -- mode, wins, hint labels
local mode = "play"
local warnTimer = 0.0
local winTimer = 0.0
local advanceTimer = 0.0     -- > 0 while the complete banner lingers
local advancePending = false -- a level advance is scheduled
local winsSeen = 0
local paused = false         -- pause overlay up (P toggles it)
local pauseWasDown = false   -- edge-detect the pause key

-- the movable world: one entry per tile root GameObject id (slot assignments
-- DERIVED from the scene in init), plus the tile geometry (from LevelComponent)
local slots = {}             -- slot index -> { x, y, col, row } (from the grid)
local slotCount = 0
local tileSize = 6.0
local tiles = {}             -- tile id -> { slot = index }
local emptySlot = 0
local slides = 0
local refusals = 0
local par = 0
local levelIndex = 0

--- helpers ---------------------------------------------------------------

local function centeredLabel(id, font, text, y, z)
	local label = factory:createLabel(id, font, text, Vector2(0, y), "", z, false)
	label:centerHorizontal()
	return label
end

-- the grid geometry, built from the level's LevelComponent (the single source)
local function buildSlots()
	slots = {}
	slotCount = level:getSlotCount()
	tileSize = level:getTileSize()
	par = level:getPar()
	for s = 0, slotCount - 1 do
		slots[s] = {
			x = level:slotCenterX(s),
			y = level:slotCenterY(s),
			col = level:slotCol(s),
			row = level:slotRow(s),
		}
	end
end

local function slotAt(col, row)
	local s = level:slotForCell(col, row)
	if s < 0 then
		return nil
	end
	return s
end

local function tileAtSlot(slotIndex)
	for id, tile in pairs(tiles) do
		if tile.slot == slotIndex then
			return id, tile
		end
	end
	return nil, nil
end

-- derive the tiles' slot assignments (and thereby the empty slot) from the
-- tile roots' scene positions - the scene is the single source of the world
-- layout. Tile roots carry the "tile" tag (set by the generator / the editor's
-- 2D authoring), so there is no id list to keep in sync anymore.
local function discoverTiles()
	tiles = {}
	local occupied = {}
	for _, tileObject in ipairs(world.findByTag("tile")) do
		local id = tileObject.id
		local transform = world.getTransform(id)
		if transform ~= nil then
			local position = transform:getWorldPosition()
			local s = level:slotForPosition(position.x, position.y)
			if s >= 0 then
				tiles[id] = { slot = s }
				occupied[s] = true
			end
		end
	end
	emptySlot = 0
	for s = 0, slotCount - 1 do
		if not occupied[s] then
			emptySlot = s
			break
		end
	end
end

local function moveCursorToEmpty()
	local cursor = world.getTransform("Cursor")
	if cursor ~= nil then
		local slot = slots[emptySlot]
		cursor:setPosition(Vector3(slot.x, slot.y, 0))
	end
end

local function setCursorVisible(visible)
	local sprite = world.getSprite("Cursor")
	if sprite ~= nil then
		sprite:setSpriteVisible(visible)
	end
end

local function stars()
	return level:starsForMoves(slides)
end

local function publishState()
	shared.roller.mode = mode
	shared.roller.slides = slides
	shared.roller.moves = slides
	shared.roller.refusals = refusals
	shared.roller.emptySlot = emptySlot
	shared.roller.par = par
	shared.roller.stars = stars()
	shared.roller.levelIndex = levelIndex
	shared.roller.levelComplete = (mode == "complete")
end

local function setMode(newMode)
	mode = newMode
	local moving = (mode == "move")
	-- the world only moves while the simulation stands still
	physics:setPaused(moving)
	setCursorVisible(moving)
	if moving then
		moveCursorToEmpty()
		if hasUI then
			hud.mode:setText("MOVE WORLD - arrows slide tiles - TAB back")
		end
	else
		-- the arrows meant "slide tiles" in move mode - reset the simulated
		-- tilt so play resumes with gravity straight down (desktop only;
		-- a real accelerometer is not resettable, and needs no reset)
		input:setTiltAngle(0)
		if hasUI then
			hud.mode:setText("PLAY - LEFT/RIGHT tilt the world - TAB move mode")
		end
	end
	publishState()
end

-- is the ball inside this tile's region right now?
local function ballInTile(tile)
	local ball = world.getTransform("Ball")
	if ball == nil then
		return false
	end
	local slot = slots[tile.slot]
	local position = ball:getWorldPosition()
	local half = tileSize / 2.0 + 0.05
	return math.abs(position.x - slot.x) <= half and
		math.abs(position.y - slot.y) <= half
end

-- slide the tile NEXT TO the empty slot into it, moving in direction
-- (dx, dy) - i.e. the tile at (empty - direction). 15-puzzle semantics.
local function trySlide(dx, dy)
	local empty = slots[emptySlot]
	local sourceSlot = slotAt(empty.col - dx, empty.row - dy)
	if sourceSlot == nil then
		return	-- the source would be outside the grid
	end
	local id, tile = tileAtSlot(sourceSlot)
	if tile == nil then
		return	-- no tile there (cannot happen with one hole, but honest)
	end
	if ballInTile(tile) then
		-- v1 rule: the ball must not ride a moving tile - refuse + flash
		refusals = refusals + 1
		warnTimer = WARN_SECONDS
		if hasUI then
			layers.warn:setVisible(true)
		end
		publishState()
		return
	end
	local to = slots[emptySlot]
	local transform = world.getTransform(id)
	if transform == nil then
		return
	end
	-- ONE teleport of the tile parent: the child sprites follow through the
	-- render node graph and the engine snaps every rigid body in the subtree
	-- to its new world pose - even while the simulation is paused
	transform:teleport(Vector3(to.x, to.y, 0), Quaternion(1, 0, 0, 0))
	emptySlot, tile.slot = tile.slot, emptySlot
	slides = slides + 1
	moveCursorToEmpty()
	publishState()
	print("game.lua: tile " .. id .. " slid to slot " .. tile.slot ..
		" (empty now " .. emptySlot .. ")")
end

-- the goal was reached: freeze the world, celebrate, score+persist, and queue
-- the DEFERRED switch to the next level
local function completeLevel()
	mode = "complete"
	physics:setPaused(true)
	setCursorVisible(false)
	advanceTimer = ADVANCE_SECONDS
	advancePending = true

	-- score: keep the best (fewest) slides for this level and resume from the
	-- NEXT level on a fresh boot; write the small versioned progression save
	if levels ~= nil then
		levels:recordBestMoves(levelIndex, slides)
		local nextIndex = levelIndex + 1
		if nextIndex >= levels:count() then
			nextIndex = 0	-- looped the sequence: replayable from the top
		end
		levels:setResumeLevel(nextIndex)
		shared.roller.saved = levels:saveProgress()
	end

	if hasUI then
		hud.wins:setText("STARS: " .. stars() .. "/3")
		if winBanner ~= nil then
			winBanner:setText("LEVEL COMPLETE - " .. stars() .. "/3 stars")
		end
		layers.win:setVisible(true)
	end
	publishState()
	print("game.lua: level " .. levelIndex .. " complete in " .. slides ..
		" slides (" .. stars() .. "/3 stars)")
end

-- apply the queued level advance (called when the banner beat elapsed)
local function advanceLevel()
	advancePending = false
	if levels == nil or levels:count() == 0 then
		return
	end
	local nextIndex = levelIndex + 1
	if nextIndex >= levels:count() then
		nextIndex = 0
	end
	-- DEFERRED, re-entrant scene load: the runtime applies it at the frame
	-- boundary (never mid-update), tearing this world down through the
	-- GameObjectManager::clear hook; the next level's scripts init next frame
	levels:loadLevel(nextIndex)
	print("game.lua: advancing to level " .. nextIndex)
end

--- ScriptComponent lifecycle -----------------------------------------------

function init(self)
	input = InputManager.getSingleton()
	-- named menu actions (menu_toggle on TAB, menu_left/right/up/down on the
	-- arrows) from the built-in default set; the raw InputManager stays for
	-- the tilt reset in setMode
	actions = InputActions.getSingleton()
	physics = PhysicsWorld.getSingleton()
	levels = LevelManager.getSingleton()

	-- the grid geometry lives on the "Level" object's LevelComponent
	level = world.getLevel("Level")
	if level == nil then
		print("game.lua: FATAL - no Level object with a LevelComponent")
		return
	end
	levelIndex = levels:currentIndex()

	local engine = Engine.getSingleton()
	hasUI = engine:hasUISystem()

	buildSlots()
	discoverTiles()

	shared.roller = shared.roller or {}
	shared.roller.gameReady = true
	-- the `shared` table survives the scene switch, so a previous level's win
	-- count lingers - reset it here (and our watch baseline) so the new level
	-- only completes on a FRESH win, never on the stale carry-over
	shared.roller.wins = 0
	winsSeen = 0

	-- resume-on-boot: if progress was saved past level 0, jump straight there
	-- (a fresh save file starts at 0 - the honest fallback). The deferred load
	-- re-inits this script on the resumed level; nothing below matters then.
	if levelIndex == 0 and levels:resumeLevel() > 0 then
		print("game.lua: resuming at level " .. levels:resumeLevel())
		levels:loadLevel(levels:resumeLevel())
	end

	if not hasUI then
		-- HUD-less flavor (Ogre-Next until the facade HUD): no widgets; modes,
		-- slides, wins and progression still run and publish through shared.roller
		setMode("play")
		print("game.lua: no UI system on this flavor - HUD skipped, "
			.. "TAB still moves the world, levels still advance")
		return
	end

	factory = GuiFactory()
	gui = GuiManager(factory, "gui_default", PROJECT_RESOURCE_GROUP)

	local w, h = engine:getWindowWidth(), engine:getWindowHeight()

	-- safe-area insets (notch / rounded corners / home indicator; all zero on
	-- desktop): keep the HUD inside the drawable box on a notched phone. Top/
	-- side corners hug the top inset, the bottom hint sits above the home bar.
	local safe = engine:getSafeAreaInsets()

	-- HUD (z 12): mode indicator, stars counter, controls hint
	hud.mode = factory:createLabel("hud.mode", FONT_HUD, "",
		Vector2(16 + safe.mLeft, 16 + safe.mTop), "", Z_HUD, false)
	hud.wins = factory:createLabel("hud.wins", FONT_HUD,
		"LEVEL " .. (levelIndex + 1) .. "  PAR " .. par,
		Vector2(w - 220 - safe.mRight, 16 + safe.mTop), "", Z_HUD, false)
	hud.hint = centeredLabel("hud.hint", FONT_HUD,
		"roll the ball to the star - rearrange the world to get there",
		h - 34 - safe.mBottom, Z_HUD)

	-- warning flash (z 13): shown when a slide is refused
	local warn = centeredLabel("warn.label", FONT_TITLE, "BALL IN TILE!",
		math.floor(h * 0.42), Z_WARN)
	-- win banner (z 14)
	winBanner = centeredLabel("win.banner", FONT_TITLE, "LEVEL COMPLETE!",
		math.floor(h * 0.30), Z_WIN)

	-- pause overlay (z 15, above everything): the reference pattern for a
	-- modal screen - a full-window DecorWidget with an EMPTY sprite name (a
	-- solid whitepixel fill) tinted dark and faded to a scrim, plus a title,
	-- both on one top z layer toggled as a unit. P shows/hides it.
	local pauseBackdrop = factory:createDecorWidget("pause.backdrop", "",
		Vector2(0, 0), Vector2(w, h), "", Z_PAUSE)
	pauseBackdrop:setColour(0.0, 0.0, 0.0, 1.0)
	pauseBackdrop:setAlpha(0.6)
	centeredLabel("pause.label", FONT_TITLE, "PAUSED",
		math.floor(h * 0.42), Z_PAUSE)

	layers.hud = hud.mode:getLayer()
	layers.warn = warn:getLayer()
	layers.win = winBanner:getLayer()
	layers.pause = pauseBackdrop:getLayer()
	layers.warn:setVisible(false)
	layers.win:setVisible(false)
	layers.pause:setVisible(false)

	setMode("play")
	print("game.lua: level " .. levelIndex .. " (\"" ..
		levels:levelName(levelIndex) .. "\") ready - " ..
		slides .. " slides, par " .. par)
end

function update(self, dt)
	if level == nil then
		return
	end

	-- pause overlay: P toggles the dimmed modal screen; while it is up the
	-- gameplay freezes (early return) but the HUD keeps drawing under the scrim
	if hasUI and input ~= nil then
		local pauseDown = input:isKeyDown(KC.KC_P)
		if pauseDown and not pauseWasDown then
			paused = not paused
			layers.pause:setVisible(paused)
		end
		pauseWasDown = pauseDown
		if paused then
			return
		end
	end

	-- level-complete: freeze input, count down the banner, then advance
	if mode == "complete" then
		if advanceTimer > 0.0 then
			advanceTimer = advanceTimer - dt
			if advanceTimer <= 0.0 and advancePending then
				advanceLevel()
			end
		end
		return
	end

	-- TAB toggles between rolling and rearranging the world (menu_toggle)
	if actions:pressed("menu_toggle") then
		setMode(mode == "play" and "move" or "play")
	end

	if mode == "move" then
		if actions:pressed("menu_left") then
			trySlide(-1, 0)
		elseif actions:pressed("menu_right") then
			trySlide(1, 0)
		elseif actions:pressed("menu_up") then
			trySlide(0, 1)
		elseif actions:pressed("menu_down") then
			trySlide(0, -1)
		end
	end

	-- warning flash timeout
	if warnTimer > 0.0 then
		warnTimer = warnTimer - dt
		if warnTimer <= 0.0 and hasUI then
			layers.warn:setVisible(false)
		end
	end

	-- win watch: ball.lua counts wins, reaching the goal completes the level.
	-- Strictly INCREASING (not just changed) so a reset carry-over can never
	-- trip it.
	local ballWins = shared.roller.wins or 0
	if ballWins > winsSeen then
		winsSeen = ballWins
		completeLevel()
	end
end

function shutdown(self)
	-- release the UI deterministically (same pattern as jumper-lua)
	if gui ~= nil then
		gui:destroyAllWidgets()
	end
	hud, layers = {}, {}
	gui, factory, winBanner = nil, nil, nil
	-- never leave the simulation paused behind (the next level starts fresh)
	if physics ~= nil then
		physics:setPaused(false)
	end
	print("game.lua: released")
end
