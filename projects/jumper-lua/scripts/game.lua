-- game.lua - game flow and UI for the pure-Lua jumper, attached to the
-- "Game" object in scenes/main.oscene through a ScriptComponent.
--
-- This script owns everything AROUND the gameplay: it boots the engine's
-- fastgui UI system from Lua (FastGuiFactory + FastGuiManager with the
-- project's own atlas from assets/fastgui_default.{ogui,png}), builds the
-- three screens and runs the state machine:
--
--   title    "ORKIGE JUMPER" + START button   ENTER or click -> playing
--   playing  HUD: progress bar to the goal, wins counter, controls hint
--   win      "YOU WIN!" + AGAIN button        ENTER or click -> playing
--
-- The gameplay itself lives in scripts/player.lua (on the Player object).
-- The two scripts never see each other's locals - they coordinate through
-- the global `shared` table only:
--   shared.game.state    ("title"/"playing"/"win")  written HERE, the player
--                        script enables its controls only while "playing"
--   shared.jumper.*      (x, wins, ...)             written by player.lua,
--                        read here to drive the progress bar and win screen
--
-- Widget visibility rides on the shared per-z UiLayers (widgets of one
-- screen share a z), exactly like the C++ jumper sample's HUD: hiding layer
-- Z_TITLE hides the whole title screen without touching the HUD widgets.
--
-- RENDER FLAVORS: fastgui exists only on the classic backend (the
-- facade HUD replaces it later). engine:hasUISystem() answers which
-- world we are in - without a UI system this script skips every widget and
-- runs the same state machine on ENTER alone (title -> playing -> win).

local KC = KeyEventData.KeyCode
-- nil-safe: the FastGui usertypes only exist when the flavor carries the UI
-- system (engine:hasUISystem(), see init) - LA is only read on that path
local LA = FastGuiLabel and FastGuiLabel.LabelAlignment

local FONT_HUD   = 9	-- 10x14 px glyphs in the atlas
local FONT_TITLE = 24	-- 20x28 px glyphs
local Z_HUD, Z_TITLE, Z_WIN = 12, 13, 14

-- the project's assets/ directory is registered under this resource group
-- by every runtime that opens a project (player, editor play mode)
local PROJECT_RESOURCE_GROUP = "OrkigeProject"

--- per-instance state ---------------------------------------------------
local input                    -- InputManager singleton
local hasUI = false            -- engine:hasUISystem() (false = HUD-less flavor)
local gui, factory             -- the Lua-booted UI system (nil when HUD-less)
local layers = {}              -- the three screen layers: hud/title/win
local hud = {}                 -- progress, wins, hint
local title = {}               -- name, prompt, start (button)
local win = {}                 -- banner, prompt, again (button)
local state = "title"
local winsSeen = 0             -- shared.jumper.wins already shown
local enterWasDown = false     -- isKeyDown edge detection
local startX, goalX = 0.0, 36.0	-- progress range (measured from the scene)

--- helpers ----------------------------------------------------------------

-- a horizontally centered label (whole-pixel positions only - the caption
-- caption asserts on subpixel coordinates, and centerHorizontal floors)
local function centeredLabel(id, font, text, y, z)
	local label = factory:createLabel(id, font, text, Vector2(0, y), "", z, false)
	label:centerHorizontal()
	return label
end

-- a horizontally centered click button ("button" + _over/_down sprites from
-- the atlas give it the hover/pressed states for free)
local function centeredButton(id, caption, y, z, width, height)
	local x = math.floor((layers.screenWidth - width) / 2)
	return factory:createButton(id, "button", FONT_HUD, caption,
		Vector2(x, y), LA.LA_CENTER, Vector2(width, height), "", z, false, 0)
end

-- publish the button's center so the outside (the selfcheck) can aim a
-- real mouse click at it
local function publishButtonCenter(button, keyX, keyY)
	local pos, size = button:getPosition(), button:getSize()
	shared.game[keyX] = pos.x + size.x / 2
	shared.game[keyY] = pos.y + size.y / 2
end

-- one state, three layers: show exactly what the state needs (the layers
-- only exist with a UI system; the state machine itself always runs)
local function setState(newState)
	state = newState
	if hasUI then
		layers.title:setVisible(state == "title")
		layers.hud:setVisible(state ~= "title")
		layers.win:setVisible(state == "win")
	end
	shared.game.state = state
end

-- ENTER, edge-triggered (isKeyDown is level-triggered)
local function enterPressed()
	local down = input:isKeyDown(KC.KC_RETURN)
	local pressed = down and not enterWasDown
	enterWasDown = down
	return pressed
end

--- ScriptComponent lifecycle -----------------------------------------------

function init(self)
	input = InputManager.getSingleton()

	local engine = Engine.getSingleton()
	hasUI = engine:hasUISystem()

	-- progress range: scene = data, the goal marker position IS the level
	local goalTransform = world.getTransform("Goal")
	if goalTransform ~= nil then
		goalX = goalTransform:getPosition().x
	end

	if not hasUI then
		-- HUD-less flavor (Ogre-Next until the facade HUD lands): no widgets,
		-- the game flow runs on ENTER alone
		shared.game = {}
		setState("title")
		print("game.lua: no UI system on this flavor - HUD skipped, "
			.. "ENTER drives the game flow")
		return
	end

	-- boot the UI: the factory builds widgets, the manager owns the UI
	-- screen for the project's atlas and (enableInputEvents) feeds engine
	-- mouse events to the widgets - that is what makes buttons clickable
	factory = FastGuiFactory()
	gui = FastGuiManager(factory, "fastgui_default", PROJECT_RESOURCE_GROUP)
	gui:enableInputEvents()

	local w, h = engine:getWindowWidth(), engine:getWindowHeight()
	layers.screenWidth = w

	-- HUD (z 12): progress to the goal, wins counter, controls hint
	hud.progress = factory:createProgressBar("hud.progress", "progressbar",
		FONT_HUD, "", Vector2(16, 16), LA.LA_CENTER, Vector2(192, 20), "",
		Z_HUD)
	hud.progress:setProgress(0)
	hud.wins = factory:createLabel("hud.wins", FONT_HUD, "WINS: 0",
		Vector2(w - 148, 16), "", Z_HUD, false)
	hud.hint = centeredLabel("hud.hint", FONT_HUD,
		"A/D move - W/S dodge - SPACE jump", h - 34, Z_HUD)

	-- title screen (z 13)
	title.name = centeredLabel("title.name", FONT_TITLE, "ORKIGE JUMPER",
		math.floor(h * 0.22), Z_TITLE)
	title.prompt = centeredLabel("title.prompt", FONT_HUD,
		"press ENTER or click START", math.floor(h * 0.22) + 44, Z_TITLE)
	title.start = centeredButton("title.start", "START",
		math.floor(h * 0.52), Z_TITLE, 160, 40)

	-- win screen (z 14, shown over the HUD)
	win.banner = centeredLabel("win.banner", FONT_TITLE, "YOU WIN!",
		math.floor(h * 0.30), Z_WIN)
	win.prompt = centeredLabel("win.prompt", FONT_HUD,
		"press ENTER or click AGAIN to play again",
		math.floor(h * 0.30) + 44, Z_WIN)
	win.again = centeredButton("win.again", "AGAIN",
		math.floor(h * 0.52), Z_WIN, 160, 40)

	-- the widgets of one screen share their z layer - grab each once
	layers.hud = hud.progress:getLayer()
	layers.title = title.name:getLayer()
	layers.win = win.banner:getLayer()

	shared.game = {}
	publishButtonCenter(title.start, "startButtonX", "startButtonY")
	publishButtonCenter(win.again, "againButtonX", "againButtonY")
	setState("title")
	print("game.lua: UI up (atlas 'fastgui_default'), entering title screen")
end

function update(self, dt)
	-- sample ENTER every frame regardless of state: edge detection needs a
	-- fresh enterWasDown, or an ENTER held into "playing" (the HUD-less
	-- title flow) reads as still-down when the win screen shows
	local enter = enterPressed()
	if state == "title" then
		if enter or (hasUI and title.start:wasClicked()) then
			setState("playing")
		end
	elseif state == "playing" then
		-- HUD from the gameplay stats player.lua publishes
		local jumper = shared.jumper
		if jumper ~= nil then
			local span = goalX - startX
			if hasUI and span > 0 then
				hud.progress:setProgress(
					(jumper.x - startX) / span * 100.0)
			end
			if jumper.wins ~= winsSeen then
				winsSeen = jumper.wins
				if hasUI then
					hud.wins:setText("WINS: " .. winsSeen)
				end
				setState("win")	-- player.lua already respawned the player
			end
		end
	elseif state == "win" then
		if enter or (hasUI and win.again:wasClicked()) then
			if hasUI then
				hud.progress:setProgress(0)
			end
			setState("playing")
		end
	end
end

function shutdown(self)
	-- release the UI deterministically: drop every reference here; the
	-- engine collects the instance's garbage on unload, which destroys the
	-- widgets and then the manager while the renderer is still alive
	if gui ~= nil then
		gui:disableInputEvents()
		gui:destroyAllWidgets()
	end
	hud, title, win, layers = {}, {}, {}, {}
	gui, factory = nil, nil
	print("game.lua: UI released")
end
