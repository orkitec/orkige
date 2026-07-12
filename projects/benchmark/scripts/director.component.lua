-- director.component.lua - the shared, autonomous director for the benchmark
-- showcase. ONE script component KIND ("director") attached to a "Director"
-- object in every scene; a `mode` export property selects the vignette
-- behaviour. No input is required: the director sets up the scene's camera and
-- atmosphere, drives its motion or stress ramp, draws a small HUD (scene name +
-- live frame time), and after a frame budget wipes to the next scene in the
-- level sequence - looping on the final results card.
--
-- Timing is FRAME-COUNT based, scaled by the `benchmark.sceneScale` cvar
-- (default 1): a scene lasts `seconds * 60 * scale` frames. A normal run is
-- vsync-paced (~60 fps) so that reads as seconds; an automated run seeds a tiny
-- scale (through the ORKIGE_CVARS env the player forwards to the cvar system)
-- so every scene traverses in a handful of deterministic frames. The wipe is a
-- fade transition unless `benchmark.wipe` is 0 (automation shortcut).
--
-- The benchmark recorder scores each scene automatically at the level-switch
-- boundary; the director additionally names the record via benchmark.begin and
-- publishes its per-scene ramp result / live frame time through `shared.tour`
-- so the results scene can lay out a card.

-- designer-tunable exports (the scene overrides these per vignette): which
-- benchmark label to record under, which vignette behaviour to run, and how
-- long (in seconds at ~60 fps) the scene lasts.
properties = {
	sceneLabel = { type = "string", default = "Scene" },
	mode       = { type = "string", default = "vista" },
	seconds    = { type = "number", default = 10.0, min = 1.0, max = 120.0 },
}

local TS = RenderNode.TransformSpace

-- self-limit budget: a ramp stops adding load once a frame costs more than this
local RAMP_BUDGET_MS = 1000.0 / 40.0     -- 40 fps floor

-- concurrent dynamic point-light ceiling for the night-lights ramp, read from
-- the `benchmark.lightCeiling` cvar in init (default 16 - a real "many lights"
-- ramp the default render backend takes comfortably at mobile budget). The
-- frame-budget self-limit still applies below it, so a weaker device stops the
-- ramp early and records where it stalled. Lower the cvar for a tighter budget.
local lightCeiling = 16

--- per-instance state --------------------------------------------------------
local engine, camNode, levels
local mode, label, seconds = "vista", "Scene", 10.0
local frames = 0
local budget = 600
local advancing = false
local scale, wipe = 1.0, 1.0
local elapsed = 0.0
local fpsSmoothed = 60.0

-- HUD
local gui, factory, hudTitle, hudInfo, hasUI = nil, nil, nil, nil, false

-- ramp bookkeeping
local pool = {}
local activeCount = 0
local rampCapped = false

-- camera drive
local camRadius, camHeight, camCenter = 16.0, 8.0, -8.0

local PROJECT_GROUP = "OrkigeProject"

--- helpers -------------------------------------------------------------------

local function setPerspectiveCamera(radius, height, center)
	engine:setCameraPerspective()
	camRadius, camHeight, camCenter = radius, height, center
	if camNode ~= nil then
		camNode:setPosition(Vector3(0, height, center + radius))
		camNode:lookAt(Vector3(0, 0, center), TS.TS_WORLD, Vector3(0, 0, -1))
	end
end

local function orbitCamera(t)
	if camNode == nil then
		return
	end
	local a = t * 0.15
	local x = math.sin(a) * camRadius
	local z = camCenter + math.cos(a) * camRadius
	camNode:setPosition(Vector3(x, camHeight, z))
	camNode:lookAt(Vector3(0, 1, camCenter), TS.TS_WORLD, Vector3(0, 0, -1))
end

-- rotate the "Sun" directional light so the atmosphere sky sweeps with it;
-- pitch goes from dawn (low) through noon (high) to dusk over the arc `p` 0..1
local function driveSun(p)
	local sun = world.getTransform("Sun")
	if sun == nil then
		return
	end
	local pitch = math.rad(15.0 + p * 150.0)   -- 15deg -> 165deg
	local half = pitch * 0.5
	sun:setOrientation(Quaternion(math.cos(half), math.sin(half), 0, 0))
end

-- day -> sunset -> night atmosphere blend as `p` goes 0..1
local function driveAtmosphere(p)
	local function lerp(a, b, t) return a + (b - a) * t end
	local sky, density, fog
	if p < 0.45 then
		local t = p / 0.45
		sky = { lerp(0.45, 0.9, t), lerp(0.62, 0.55, t), lerp(0.85, 0.35, t) }
		density, fog = 0.02, 0.010
	elseif p < 0.75 then
		local t = (p - 0.45) / 0.30
		sky = { lerp(0.9, 0.25, t), lerp(0.55, 0.20, t), lerp(0.35, 0.30, t) }
		density, fog = 0.03, 0.018
	else
		local t = (p - 0.75) / 0.25
		sky = { lerp(0.25, 0.04, t), lerp(0.20, 0.05, t), lerp(0.30, 0.12, t) }
		density, fog = 0.05, 0.025
	end
	engine:setAtmosphere(true, sky[1], sky[2], sky[3], density, fog)
end

local function gatherPool()
	pool = {}
	for _, obj in ipairs(world.findByTag("pool")) do
		pool[#pool + 1] = obj
		obj:setActive(false)
	end
	activeCount = 0
	rampCapped = false
end

-- activate the next `n` pooled objects unless the last frame blew the budget or
-- a hard ceiling was reached. `ceiling` keeps the ramp inside the compatibility
-- flavour's forward-rendering headroom (the mobile-budget discipline): many
-- dynamic point lights on the classic renderer must stay bounded.
local function rampPool(frameMs, n, ceiling)
	ceiling = math.min(ceiling or #pool, #pool)
	if rampCapped or activeCount >= ceiling then
		if not rampCapped and activeCount >= ceiling then
			rampCapped = true
			print(string.format("director[%s]: ramp reached ceiling %d",
				mode, activeCount))
		end
		return
	end
	if frameMs > RAMP_BUDGET_MS and activeCount > 0 then
		rampCapped = true
		print(string.format("director[%s]: ramp capped at %d (%.2f ms/frame)",
			mode, activeCount, frameMs))
		return
	end
	for _ = 1, n do
		if activeCount >= ceiling then
			break
		end
		activeCount = activeCount + 1
		pool[activeCount]:setActive(true)
	end
end

local function setActiveObject(name, active)
	local obj = world.get(name)
	if obj ~= nil then
		obj:setActive(active)
	end
end

--- HUD -----------------------------------------------------------------------

local function buildHud()
	if not hasUI then
		return
	end
	local ok = pcall(function()
		factory = GuiFactory()
		gui = GuiManager(factory, "gui_default", PROJECT_GROUP)
		local w = engine:getWindowWidth()
		local safe = engine:getSafeAreaInsets()
		hudTitle = factory:createLabel("hud.title", 24, label,
			Vector2(16 + safe.mLeft, 14 + safe.mTop), "", 12, false)
		hudInfo = factory:createLabel("hud.info", 9, "",
			Vector2(16 + safe.mLeft, 46 + safe.mTop), "", 12, false)
	end)
	if not ok then
		gui, factory, hudTitle, hudInfo = nil, nil, nil, nil
	end
end

local function updateHud(index, count)
	if hudInfo == nil then
		return
	end
	local text = string.format("%s %d / %d   %.1f ms   %.0f fps",
		loc("bench.scene"), index + 1, count, 1000.0 / fpsSmoothed, fpsSmoothed)
	hudInfo:setText(text)
end

--- results card --------------------------------------------------------------

local function buildResults()
	if not hasUI then
		return
	end
	pcall(function()
		factory = GuiFactory()
		gui = GuiManager(factory, "gui_default", PROJECT_GROUP)
		local w, h = engine:getWindowWidth(), engine:getWindowHeight()
		local panel = factory:createDecorWidget("res.panel", "panel",
			Vector2(w * 0.5 - 220, 40), Vector2(440, h - 100), "", 4)
		if panel.setNineSlice ~= nil then
			panel:setNineSlice(true)
		end
		local title = factory:createLabel("res.title", 24, loc("bench.results"),
			Vector2(0, 60), "", 6, false)
		title:centerHorizontal()
		local y = 120
		local tour = shared.tour or {}
		for _, name in ipairs(SCENE_ORDER) do
			local info = tour[name]
			local line = name
			if info ~= nil and info.detail ~= nil then
				line = name .. "  -  " .. info.detail
			end
			factory:createLabel("res." .. name, 9, line, Vector2(60, y), "", 6, false)
			y = y + 30
		end
		factory:createLabel("res.frame", 9,
			string.format("%s: %.2f ms", loc("bench.frameMs"),
				1000.0 / fpsSmoothed), Vector2(60, y + 10), "", 6, false)
	end)
end

-- the fixed scene order for the results card (mirrors levels.olevels labels)
SCENE_ORDER = SCENE_ORDER or {
	"Terrace Vista", "Still Water", "Night Lumens", "Ember Swarm",
	"Instance Field", "Flatland", "Console", "Cascade",
}

--- console (GUI) mode --------------------------------------------------------

local langTimer, langIndex = 0.0, 1

local function buildConsole()
	if not hasUI then
		return
	end
	local built = false
	pcall(function()
		factory = GuiFactory()
		gui = GuiManager(factory, "gui_default", PROJECT_GROUP)
		factory:loadLayout("settings.oui")
		built = true
	end)
	if not built then
		-- robust fallback: a programmatic settings-style card
		pcall(function()
			if gui == nil then
				factory = GuiFactory()
				gui = GuiManager(factory, "gui_default", PROJECT_GROUP)
			end
			local w, h = engine:getWindowWidth(), engine:getWindowHeight()
			local panel = factory:createDecorWidget("cons.panel", "panel",
				Vector2(w * 0.5 - 240, 60), Vector2(480, h - 140), "", 4)
			factory:createLabel("cons.title", 24, loc("bench.settings"),
				Vector2(w * 0.5 - 210, 80), "", 6, false)
			local rows = { "bench.graphics", "bench.audio", "bench.shadows",
				"bench.language", "bench.hello" }
			for i, key in ipairs(rows) do
				factory:createLabel("cons.row" .. i, 9, loc(key),
					Vector2(w * 0.5 - 200, 140 + (i - 1) * 40), "", 6, false)
			end
		end)
	end
end

local function cycleLanguage(dt)
	langTimer = langTimer + dt
	if langTimer < 2.5 then
		return
	end
	langTimer = 0.0
	local ok, langs = pcall(function() return locale.list() end)
	if not ok or langs == nil or #langs == 0 then
		return
	end
	langIndex = (langIndex % #langs) + 1
	locale.set(langs[langIndex])
	-- rebuild the screen so the new language captions show
	if gui ~= nil then
		pcall(function() gui:destroyAllWidgets() end)
	end
	buildConsole()
end

--- cascade (physics) mode ----------------------------------------------------

local cascadeWave, hitstopDone = 0, false

local function driveCascade(dt, p)
	-- rain pooled bodies in waves
	local target = math.floor(p * #pool)
	while activeCount < target do
		activeCount = activeCount + 1
		pool[activeCount]:setActive(true)
	end
	-- a time-scale hitstop beat near the middle
	if not hitstopDone and p > 0.5 then
		hitstopDone = true
		world.setTimeScale(0.1)
		if screen ~= nil then
			pcall(function() screen.shake(0.4, 0.3) end)
		end
	elseif hitstopDone and p > 0.58 then
		world.setTimeScale(1.0)
	end
end

--- lifecycle -----------------------------------------------------------------

function init(self)
	engine = Engine.getSingleton()
	levels = LevelManager.getSingleton()
	hasUI = engine:hasUISystem()

	mode = self.mode or "vista"
	label = self.sceneLabel or "Scene"
	seconds = self.seconds or 10.0

	cvar.registerNumber("benchmark.sceneScale", 1.0)
	cvar.registerNumber("benchmark.wipe", 1.0)
	cvar.registerNumber("benchmark.lightCeiling", 16.0)
	scale = cvar.getNumber("benchmark.sceneScale", 1.0)
	wipe = cvar.getNumber("benchmark.wipe", 1.0)
	lightCeiling = math.max(0, math.floor(cvar.getNumber("benchmark.lightCeiling", 16.0)))
	budget = math.max(6, math.floor(seconds * 60.0 * scale))

	frames = 0
	elapsed = 0.0
	advancing = false

	if benchmark ~= nil then
		benchmark.begin(label)
	end

	local cam = engine:getCamera()
	camNode = cam ~= nil and cam:getNode() or nil

	shared.tour = shared.tour or {}

	-- per-mode setup ---------------------------------------------------------
	if mode == "vista" then
		setPerspectiveCamera(18.0, 9.0, -8.0)
		driveSun(0.0)
		driveAtmosphere(0.0)
		buildHud()
	elseif mode == "lake" then
		setPerspectiveCamera(20.0, 7.0, -6.0)
		driveSun(0.55)
		driveAtmosphere(0.6)
		buildHud()
	elseif mode == "lumens" then
		setPerspectiveCamera(18.0, 9.0, -8.0)
		driveSun(0.9)
		driveAtmosphere(0.9)
		gatherPool()
		buildHud()
	elseif mode == "swarm" then
		setPerspectiveCamera(16.0, 8.0, -8.0)
		engine:setAtmosphere(true, 0.10, 0.10, 0.16, 0.03, 0.015)
		gatherPool()
		buildHud()
	elseif mode == "field" then
		setPerspectiveCamera(22.0, 12.0, -12.0)
		driveSun(0.2)
		driveAtmosphere(0.15)
		gatherPool()
		buildHud()
	elseif mode == "flatland" then
		engine:setAtmosphere(false, 0, 0, 0, 0, 0)
		engine:setWindowBackgroundColour(0.20, 0.28, 0.42)
		engine:setCameraOrthographic(8.0)
		buildHud()
		-- gentle looping motion
		pcall(function()
			tween.move("Blob", -4.0, 1.0, 0.0, 1.2, "quadInOut")
		end)
	elseif mode == "console" then
		engine:setAtmosphere(false, 0, 0, 0, 0, 0)
		engine:setWindowBackgroundColour(0.10, 0.12, 0.18)
		buildConsole()
	elseif mode == "cascade" then
		engine:setAtmosphere(false, 0, 0, 0, 0, 0)
		engine:setWindowBackgroundColour(0.12, 0.10, 0.14)
		engine:setCameraOrthographic(9.0)
		gatherPool()
		buildHud()
	elseif mode == "tally" then
		engine:setAtmosphere(false, 0, 0, 0, 0, 0)
		engine:setWindowBackgroundColour(0.08, 0.09, 0.12)
		buildResults()
	end

	print(string.format("director[%s]: '%s' ready (budget %d frames, scale %g)",
		mode, label, budget, scale))
end

function update(self, dt)
	frames = frames + 1
	elapsed = elapsed + dt
	local frameMs = dt * 1000.0
	if dt > 0.0 then
		fpsSmoothed = fpsSmoothed * 0.9 + (1.0 / dt) * 0.1
	end
	local p = math.min(1.0, frames / budget)

	-- per-mode per-frame drive ----------------------------------------------
	if mode == "vista" then
		orbitCamera(elapsed)
		driveSun(p)
		driveAtmosphere(p)
		-- rain weather phase in the back third
		setActiveObject("Rain", p > 0.6)
	elseif mode == "lake" then
		orbitCamera(elapsed * 0.6)
	elseif mode == "lumens" then
		orbitCamera(elapsed * 0.5)
		rampPool(frameMs, 1, lightCeiling)
	elseif mode == "swarm" then
		orbitCamera(elapsed * 0.4)
		rampPool(frameMs, 1, 8)
	elseif mode == "field" then
		orbitCamera(elapsed * 0.3)
		rampPool(frameMs, 8, 200)
	elseif mode == "flatland" then
		-- nothing per-frame; tweens + morph + vector anim run themselves
	elseif mode == "console" then
		cycleLanguage(dt)
	elseif mode == "cascade" then
		driveCascade(dt, p)
	elseif mode == "tally" then
		-- idle on the card
	end

	local index = levels ~= nil and levels:currentIndex() or 0
	local count = levels ~= nil and levels:count() or 1
	updateHud(index, count)

	-- record this scene's headline result for the tally card
	if activeCount > 0 then
		shared.tour[label] = { detail = string.format("%d live", activeCount) }
	elseif shared.tour[label] == nil then
		shared.tour[label] = { detail = string.format("%.1f ms", 1000.0 / fpsSmoothed) }
	end

	-- advance at the end of the budget --------------------------------------
	if frames >= budget and not advancing then
		advancing = true
		if world.getTimeScale() ~= 1.0 then
			world.setTimeScale(1.0)
		end
		local nextIndex = index + 1
		if levels == nil or nextIndex >= count then
			nextIndex = 0                 -- loop the tour
		end
		local nextPath = levels ~= nil and levels:levelScene(nextIndex)
			or "scenes/vista.oscene"
		if wipe > 0.5 and screen ~= nil then
			screen.loadScene(nextPath, 0.3, 0.3)
		elseif levels ~= nil then
			levels:loadLevel(nextIndex)
		else
			world.loadScene(nextPath)
		end
	end
end

function shutdown(self)
	if gui ~= nil then
		pcall(function() gui:destroyAllWidgets() end)
	end
	gui, factory, hudTitle, hudInfo = nil, nil, nil, nil
	pool = {}
	if world ~= nil then
		pcall(function() world.setTimeScale(1.0) end)
	end
end
