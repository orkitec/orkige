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
local LA = GuiLabel and GuiLabel.LabelAlignment

-- self-limit budget: a ramp stops adding load once a frame costs more than
-- this (40 fps floor). Overridable via the `benchmark.rampBudgetMs` cvar so a
-- deterministic test run can force the ramp to its ceiling regardless of the
-- machine (a debug build's frame cost would otherwise cap it immediately).
local RAMP_BUDGET_MS = 1000.0 / 40.0
local rampBudgetMs = RAMP_BUDGET_MS

-- concurrent dynamic point-light ceiling for the night-lights ramp. Filled in
-- init from engine:getLightBudget() - the ACTIVE render backend's honest
-- dynamic-light ceiling (@see RenderSystem::lightBudget): the classic forward
-- renderer's per-pass headroom on one flavor, the far higher clustered-forward
-- light-list bound on the other. Querying it (instead of a hard authored
-- constant) lets the many-lights showcase climb as high as each flavor allows
-- rather than pinning the stronger flavor to the weaker one's floor. The
-- `benchmark.lightCeiling` cvar (default 0 = use the queried budget) is an
-- OPTIONAL manual cap for a tighter budget on a weak device; the frame-budget
-- self-limit still applies below whichever ceiling wins, so a stalling device
-- records where it stopped. FALLBACK_LIGHT_CEILING covers a query of 0 (before
-- the render system exists - never in a real play session).
local FALLBACK_LIGHT_CEILING = 16
local lightCeiling = FALLBACK_LIGHT_CEILING

--- per-instance state --------------------------------------------------------
local engine, camNode, levels
local mode, label, seconds = "vista", "Scene", 10.0
local frames = 0
local budget = 600
local advancing = false
local switchAtFrame = nil
local scale, wipe = 1.0, 1.0
local elapsed = 0.0
local fpsSmoothed = 60.0

-- HUD
local gui, factory, hudTitle, hudInfo, hasUI = nil, nil, nil, nil, false
local hudScrim = nil

-- results-card restart control + the tour-restart bookkeeping the button and
-- the automation seam share: BOTH restart paths route through restartTour, so
-- the interactive click and the headless self-check exercise the same wiring.
local resRestartBtn = nil
local restartFired = false
local forcedNextIndex = nil   -- when set, the next level switch loads THIS index
local autoRestart = 0.0       -- benchmark.autoRestart: replay the tour from the
                              -- card after N frames (a kiosk/soak + test seam)

-- ramp bookkeeping
local pool = {}
local activeCount = 0
local rampCapped = false

-- camera drive
local camRadius, camHeight, camCenter = 16.0, 8.0, -8.0

-- vista sky state: true while the day skybox cubemap + IBL are showing; the
-- night leg flips it to the procedural dome so the sky darkens with the sun
local vistaSkybox = false

-- cast (character crowd) state: the front-and-centre hero's animation handle
-- (reached via world.getAnimation) plus a small state machine that cross-fades
-- it walk<->idle on a timer so the WEIGHTED blend reads on stage
local castHeroAnim, castHeroState, castHeroTimer = nil, "walk", 0.0

local PROJECT_GROUP = "OrkigeProject"

--- helpers -------------------------------------------------------------------

-- perspective framing fit: the orbit distances are tuned for a landscape
-- aspect, but the window is portrait on a phone (the default orientation) - and
-- a perspective camera's horizontal field shrinks with the aspect (hFOV grows
-- from vFOV by the width/height ratio), so a portrait window crops the wide
-- vista to a narrow centre slice. Pull the camera back (and up, to hold the
-- pitch so the horizon stays put) proportionally, capped so the content never
-- shrinks to nothing - the panorama keeps its breadth in portrait.
local DESIGN_ASPECT = 16.0 / 9.0
local MAX_FRAMING_PULLBACK = 1.7
local function framingFit()
	local w, h = engine:getWindowWidth(), engine:getWindowHeight()
	if w <= 0 or h <= 0 then
		return 1.0
	end
	local aspect = w / h
	if aspect >= DESIGN_ASPECT then
		return 1.0
	end
	local fit = DESIGN_ASPECT / aspect
	if fit > MAX_FRAMING_PULLBACK then
		fit = MAX_FRAMING_PULLBACK
	end
	return fit
end

local camLookY = 1.0

local function setPerspectiveCamera(radius, height, center, lookY)
	engine:setCameraPerspective()
	camRadius, camHeight, camCenter = radius, height, center
	camLookY = lookY or 1.0
	if camNode ~= nil then
		local fit = framingFit()
		camNode:setPosition(Vector3(0, height * fit, center + radius * fit))
		camNode:lookAt(Vector3(0, camLookY, center), TS.TS_WORLD,
			Vector3(0, 0, -1))
	end
end

-- the slow showcase orbit. `benchmark.cameraOrbit` 0 freezes the camera at
-- the init framing (an automation seam: the orbit runs on WALL time, so a
-- pixel probe would sample machine-dependent framings otherwise).
local cameraOrbit = 1.0

local function orbitCamera(t)
	if camNode == nil or cameraOrbit < 0.5 then
		return
	end
	local fit = framingFit()
	local radius, height = camRadius * fit, camHeight * fit
	local a = t * 0.15
	local x = math.sin(a) * radius
	local z = camCenter + math.cos(a) * radius
	camNode:setPosition(Vector3(x, height, z))
	camNode:lookAt(Vector3(0, camLookY, camCenter), TS.TS_WORLD,
		Vector3(0, 0, -1))
end

-- rotate the "Sun" directional light so the atmosphere sky sweeps with it;
-- pitch goes from dawn (low) through noon (high) to SETTING over the arc `p`
-- 0..1 - the arc ends 15 degrees BELOW the horizon (pitch 195), so the tour
-- reaches a true night with the sun down (the sky model needs a genuinely low
-- sun for its sunset/night looks; a still-high sun under the sunset haze
-- whites the sky out). The sun crosses the horizon at p ~0.92.
-- The light's -Z is the LIGHT-TRAVEL direction (down for a daytime sun, like
-- the scene's authored orientation), so the arc rotates NEGATIVE about X:
-- toward-the-sun is the negation the atmosphere linkage derives itself.
local function driveSun(p)
	local sun = world.getTransform("Sun")
	if sun == nil then
		return
	end
	local pitch = math.rad(15.0 + p * 180.0)   -- 15deg -> 195deg (below horizon)
	local half = pitch * 0.5
	sun:setOrientation(Quaternion(math.cos(half), -math.sin(half), 0, 0))
end

-- day -> sunset -> night atmosphere blend as `p` goes 0..1. The sky, fog and
-- exposure numbers all come from the TESTED AtmospherePreset looks (C++,
-- unit-tested, un-tonemapped-safe) via engine:setAtmosphereBlend - the director
-- only picks the two looks + a weight, it authors no raw sky/fog/exposure values
-- (raw guesses whited surfaces out / blacked the sky - see the preset ranges).
-- The blend TRACKS driveSun's arc: full sunset lands where the sun is LOW
-- (p ~0.78, elevation ~0.35 and falling) and full night as the sun CROSSES
-- the horizon (p ~0.92) - the sunset preset's thick haze paired with a HIGH
-- sun whites the whole sky out (the sky model divides its density by the sun
-- elevation, on both flavors), and a bright sky with the sun still up is not
-- a night.
local function driveAtmosphere(p)
	if p < 0.78 then
		engine:setAtmosphereBlend("day", "sunset", p / 0.78)
	else
		engine:setAtmosphereBlend("sunset", "night",
			math.min(1.0, (p - 0.78) / 0.14))
	end
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
-- The ramp always reaches RAMP_MIN before the frame budget may cap it: the
-- vignette must SHOW its content (a slow machine scores low instead of
-- rendering an empty scene).
local RAMP_MIN = 12
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
	if frameMs > rampBudgetMs and activeCount >= math.min(RAMP_MIN, ceiling) then
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
		local safe = engine:getSafeAreaInsets()
		local x0 = 16 + safe.mLeft
		local y0 = 14 + safe.mTop
		hudTitle = factory:createLabel("hud.title", 24, label,
			Vector2(x0, y0), "", 12, false)
		-- the info line sits UNDER the MEASURED title: glyphs scale with the
		-- display density (getSize folds the ui scale in), so a fixed second
		-- row offset overlaps the title on a 2x-3x screen
		local titleBottom = y0 + hudTitle:getSize().y
		hudInfo = factory:createLabel("hud.info",
			9, "Scene 00 / 00   00.0 ms   00 fps",
			Vector2(x0, titleBottom + 4), "", 12, false)
		-- a subtle dark scrim band BEHIND the HUD text (z below the labels'
		-- 12) so the white glyphs stay legible on ANY vignette background - the
		-- bright day/sunset skies and the pale water washed the title out. A
		-- spriteless DecorWidget is a solid fill; a low alpha keeps it from
		-- dominating. Sized from the MEASURED rows (the info row is seeded with
		-- a representative string so the band covers the live stats too).
		local pad = 8
		local infoBottom = titleBottom + 4 + hudInfo:getSize().y
		local bandW = math.max(hudTitle:getSize().x, hudInfo:getSize().x)
			+ pad * 2
		local bandH = infoBottom - y0 + pad * 2
		hudScrim = factory:createDecorWidget("hud.scrim", "",
			Vector2(math.floor(x0 - pad), math.floor(y0 - pad)),
			Vector2(math.floor(bandW), math.floor(bandH)), "", 11)
		hudScrim:setColour(0.0, 0.0, 0.0, 1.0)
		hudScrim:setAlpha(0.42)
	end)
	if not ok then
		gui, factory, hudTitle, hudInfo, hudScrim = nil, nil, nil, nil, nil
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

-- a localised string with a literal fallback: the results card is localised
-- through the project string table, but a missing key must never leave a raw
-- "bench.foo" on a button, so fall back to the supplied text.
local function locOr(key, fallback)
	local ok, s = pcall(loc, key)
	if not ok or s == nil or s == "" or s == key then
		return fallback
	end
	return s
end

-- restart the whole tour from its first scene. Reached BOTH from the results
-- card's Restart button (a deliberate replay) and from the benchmark.autoRestart
-- automation seam - one path, so a headless run exercises the button's wiring.
-- The switch always goes through the LEVEL sequence (loadLevel), like every
-- other advance, and is guarded so a held card cannot fire it twice.
local function restartTour()
	if restartFired then
		return
	end
	restartFired = true
	if world ~= nil and world.getTimeScale() ~= 1.0 then
		world.setTimeScale(1.0)
	end
	forcedNextIndex = 0
	advancing = true
	if wipe > 0.5 and screen ~= nil then
		pcall(function() screen.fadeOut(0.3) end)
		switchAtFrame = frames + 22   -- ~0.35 s at the vsync pace
	else
		switchAtFrame = frames        -- switch this frame
	end
	print("director[tally]: restart -> loadLevel 0")
end

local function buildResults()
	if not hasUI then
		return
	end
	pcall(function()
		factory = GuiFactory()
		gui = GuiManager(factory, "gui_default", PROJECT_GROUP)
		local w, h = engine:getWindowWidth(), engine:getWindowHeight()
		-- The card is laid out from the ACTUAL rendered text extents, measured
		-- off the real labels (getSize folds in the device ui-scale), so the
		-- spacing and panel bounds stay proportional on any DPI - a desktop 1x
		-- screen or a 2-3x phone - with no scale-factor guessing. Everything is
		-- placed in raw window pixels via setPosition/setSize, so every line
		-- lands INSIDE the panel (the reported bug was labels pinned to the
		-- screen's left edge while the panel drew centred - two different spaces).
		local panel = factory:createDecorWidget("res.panel", "panel",
			Vector2(0, 0), Vector2(64, 64), "", 4)
		if panel.setNineSlice ~= nil then
			panel:setNineSlice(true)
		end
		local title = factory:createLabel("res.title", 24, loc("bench.results"),
			Vector2(0, 0), "", 6, false)
		local titleSize = title:getSize()

		local rowLabels = {}
		local widest = titleSize.x
		local tour = shared.tour or {}
		for i, name in ipairs(SCENE_ORDER) do
			local info = tour[name]
			local line = name
			if info ~= nil and info.detail ~= nil then
				line = name .. "  -  " .. info.detail
			end
			local row = factory:createLabel("res." .. name, 9, line,
				Vector2(0, 0), "", 6, false)
			rowLabels[i] = row
			widest = math.max(widest, row:getSize().x)
		end
		local frame = factory:createLabel("res.frame", 9,
			string.format("%s: %.2f ms", loc("bench.frameMs"),
				1000.0 / fpsSmoothed), Vector2(0, 0), "", 6, false)
		widest = math.max(widest, frame:getSize().x)

		-- metrics derived from the measured glyph heights (UiRenderer requires
		-- integer pixel positions/sizes, so every coordinate is floored)
		local floor = math.floor
		local rowH = rowLabels[1] ~= nil and rowLabels[1]:getSize().y or titleSize.y
		local pad = floor(rowH * 1.3)
		local lineH = floor(rowH * 1.8)
		local titleGap = floor(titleSize.y + rowH * 0.9)
		local nlines = #rowLabels + 1               -- scene lines + frame line
		local panelW = floor(math.min(widest + pad * 2, w - pad))
		local panelH = pad + titleGap + nlines * lineH + pad
		local px = floor((w - panelW) * 0.5)
		local py = floor((h - panelH) * 0.5)

		panel:setPosition(px, py)
		panel:setSize(panelW, panelH)
		-- centre the title over the panel
		title:setPosition(px + floor((panelW - titleSize.x) * 0.5), py + pad)

		local y = py + pad + titleGap
		for _, row in ipairs(rowLabels) do
			row:setPosition(px + pad, y)
			y = y + lineH
		end
		frame:setPosition(px + pad, y)

		-- the Restart button: a touch-friendly, safe-area-aware control that
		-- replays the whole tour from its first scene. Placed BELOW the panel
		-- (never over the tally content) and clamped inside the safe rect so
		-- the notch/home-bar never eats it. The nine-slice "button" sprite
		-- gives it hover/pressed states for free.
		local safe = engine:getSafeAreaInsets()
		local btnW = math.max(160, floor(panelW * 0.5))
		local btnH = math.max(44, floor(rowH * 2.6))
		local gap = floor(rowH * 1.2)
		local btnX = floor(px + (panelW - btnW) * 0.5)
		local btnY = py + panelH + gap
		-- clamp horizontally into the safe rect
		local minX = safe.mLeft
		local maxX = w - safe.mRight - btnW
		if btnX < minX then btnX = minX end
		if maxX >= minX and btnX > maxX then btnX = maxX end
		-- keep the whole button above the bottom safe inset; never push it back
		-- up INTO the panel (that would overlap the results content)
		local maxY = h - safe.mBottom - btnH - floor(rowH * 0.6)
		if btnY > maxY and maxY >= py + panelH + floor(gap * 0.5) then
			btnY = maxY
		end
		resRestartBtn = factory:createButton("res.restart", "button", 9,
			locOr("bench.restart", "Restart"), Vector2(btnX, btnY),
			LA and LA.LA_CENTER or 1, Vector2(btnW, btnH), "", 7, false, 0)
		if resRestartBtn.setNineSlice ~= nil then
			resRestartBtn:setNineSlice(true)
		end
		-- the readback the restart self-check parses: proves the button EXISTS
		-- on the card and lands inside the safe area, clear of the panel below
		print(string.format(
			"director[tally]: restart button ready rect=(%d,%d,%d,%d) "
			.. "panel=(%d,%d,%d,%d) safe=(%d,%d,%d,%d) window=(%d,%d)",
			btnX, btnY, btnW, btnH, px, py, panelW, panelH,
			safe.mLeft, safe.mTop, safe.mRight, safe.mBottom, w, h))
	end)
end

-- the fixed scene order for the results card (mirrors levels.olevels labels)
SCENE_ORDER = SCENE_ORDER or {
	"Terrace Vista", "Still Water", "Night Lumens", "Ember Swarm",
	"Instance Field", "Character Cast", "Flatland", "Console", "Cascade",
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
	-- arriving through a faded-out switch: reveal the new scene (a fade-in
	-- from an already-visible screen is a no-op flicker-free ramp)
	if screen ~= nil then
		pcall(function() screen.fadeIn(0.35) end)
	end

	mode = self.mode or "vista"
	label = self.sceneLabel or "Scene"
	seconds = self.seconds or 10.0

	cvar.registerNumber("benchmark.sceneScale", 1.0)
	cvar.registerNumber("benchmark.wipe", 1.0)
	cvar.registerNumber("benchmark.lightCeiling", 0.0)
	cvar.registerNumber("benchmark.rampBudgetMs", RAMP_BUDGET_MS)
	cvar.registerNumber("benchmark.cameraOrbit", 1.0)
	cvar.registerNumber("benchmark.autoRestart", 0.0)
	scale = cvar.getNumber("benchmark.sceneScale", 1.0)
	wipe = cvar.getNumber("benchmark.wipe", 1.0)
	-- the flavor's queried dynamic-light ceiling drives the many-lights ramp
	lightCeiling = math.floor(engine:getLightBudget())
	if lightCeiling < 1 then
		lightCeiling = FALLBACK_LIGHT_CEILING
	end
	-- optional manual cap for a tighter budget (0 = use the queried budget)
	local lightCap = math.floor(cvar.getNumber("benchmark.lightCeiling", 0.0))
	if lightCap >= 1 then
		lightCeiling = math.min(lightCeiling, lightCap)
	end
	-- report the queried budget + effective ceiling (the many-lights mechanism
	-- probe parses this and asserts the ramp never exceeds the queried budget)
	print(string.format("director: light budget %d (ramp ceiling %d)",
		math.floor(engine:getLightBudget()), lightCeiling))
	rampBudgetMs = cvar.getNumber("benchmark.rampBudgetMs", RAMP_BUDGET_MS)
	cameraOrbit = cvar.getNumber("benchmark.cameraOrbit", 1.0)
	autoRestart = cvar.getNumber("benchmark.autoRestart", 0.0)
	budget = math.max(6, math.floor(seconds * 60.0 * scale))

	frames = 0
	elapsed = 0.0
	advancing = false
	switchAtFrame = nil
	resRestartBtn = nil
	restartFired = false
	forcedNextIndex = nil

	if benchmark ~= nil then
		benchmark.begin(label)
	end

	local cam = engine:getCamera()
	camNode = cam ~= nil and cam:getNode() or nil

	shared.tour = shared.tour or {}

	-- reset the STICKY render state so every scene starts from a clean look and
	-- only the vignette that wants a feature opts into it below (the render
	-- world persists across the LevelManager scene switches): the procedural
	-- sky dome, no image-based lighting, no bloom. The vista re-enables the
	-- skybox + IBL, lumens re-enables bloom.
	engine:setAtmosphereSky("procedural", "")
	engine:setImageLighting(false, 1.0)
	engine:setBloom(false, 0.72, 0.9)
	vistaSkybox = false

	-- per-mode setup ---------------------------------------------------------
	if mode == "vista" then
		setPerspectiveCamera(18.0, 9.0, -8.0)
		driveSun(0.0)
		driveAtmosphere(0.0)
		-- SKY VISUAL: a real skybox cubemap (the generated day sky) for the
		-- bright day/sunset portion, plus skybox-sourced IMAGE-BASED LIGHTING
		-- so the terrace's PBS props (the polished metal + the crystal) pick up
		-- cubemap reflections and a diffuse sky fill. Sky-type and the sun arc
		-- are INDEPENDENT by design (@see AtmosphereDesc): the sticky skybox
		-- survives the setAtmosphereBlend exposure/fog arc that driveAtmosphere
		-- keeps running. A fixed cubemap depicts ONE time of day, so it can NOT
		-- darken with the descending sun - the night leg hands back to the
		-- PROCEDURAL dome (in update, at p >= 0.6) so the sky actually goes
		-- dark, and drops IBL (a day cubemap gives no honest night reflection).
		-- On the classic flavor IBL rides the RTSS image-lighting stage; on next
		-- the native HlmsPbs reflection/diffuse-GI env feature.
		engine:setAtmosphereSky("skybox", "sky_day.dds")
		-- a gentle image-based fill: the earlier 0.4 stacked a strong diffuse
		-- sky wash on top of the day preset's exposure, flattening the terrace
		-- to a bright, low-contrast haze (the tour's over-bright opener). Half
		-- the fill restores the shadow contrast and calms the exposure while
		-- the props keep their honest cubemap reflections + a soft sky fill.
		engine:setImageLighting(true, 0.2)
		vistaSkybox = true
		buildHud()
	elseif mode == "lake" then
		-- the water showcase framing: a LOW camera looking ACROSS the surface
		-- toward the sun (grazing angles maximise the fresnel sky mirror, the
		-- specular streak and the swell silhouette)
		setPerspectiveCamera(24.0, 2.0, -18.0, -2.6)
		-- golden hour: a LOW sun paired with the full sunset preset (the pairing
		-- the sky model expects - a high sun under the sunset haze whites out),
		-- so the lake mirrors a warm gradient sky instead of a blown white one
		driveSun(0.81)
		driveAtmosphere(0.78)
		-- WATER VISUAL: image-based lighting sourced from the PROCEDURAL sky (a
		-- runtime SkyEnvMap capture, not a baked cubemap), so the water surface
		-- picks up sky reflections instead of reading as a flat tinted slab. A
		-- gentle 0.2 fill matches the vista's calibration (a stronger fill washes
		-- the terrain); the capture re-derives as the sun descends, so the water
		-- reflection darkens naturally into the night leg. On a GLES2/WebGL1
		-- context that can't run the IBL stage the capability gate keeps it off
		-- and the water falls back to its analytic look - mobile/web safe.
		engine:setImageLighting(true, 0.2)
		buildHud()
	elseif mode == "lumens" then
		setPerspectiveCamera(18.0, 9.0, -8.0)
		driveSun(0.75)
		-- the TRUE night look (no sunset dilution): a dark dome, a dim white
		-- moon low over the horizon - the lamps are the stars of this scene
		driveAtmosphere(1.0)
		-- LDR BLOOM: the emissive point-lamp pools bleed a soft glow - the
		-- showcase of this vignette. Called UNCONDITIONALLY: bloom renders on
		-- the next flavor (RenderCaps::Bloom) and is an honest no-op on classic
		-- (gated off, the byte-identical contract), so the per-flavor pixel
		-- probes/budgets reflect each flavor's actual image. The threshold
		-- sits just ABOVE the moonlit terrain and below the lamp pools' bright
		-- cores, so the coloured pools bloom into soft haloes ON TOP of the
		-- night ground while the moonlit surface itself does not haze over.
		-- The MoonFill directional lifts the terrain into the old 0.15 cutoff
		-- (the moonlit ground now reads at ~0.13-0.22 luminance, the pool cores
		-- at ~0.35+ on the deterministic probe frame), so the threshold rose to
		-- 0.28 to keep the bloom a property of the pools, not the whole terrain.
		engine:setBloom(true, 0.28, 2.2)
		gatherPool()
		buildHud()
	elseif mode == "swarm" then
		setPerspectiveCamera(16.0, 8.0, -8.0)
		-- a clean night sky (tested preset: dim sun, cool fill, sane haze) so
		-- the glowing embers read against the dark
		engine:setAtmosphereBlend("night", "night", 0.0)
		gatherPool()
		buildHud()
	elseif mode == "field" then
		setPerspectiveCamera(22.0, 12.0, -12.0)
		driveSun(0.17)
		driveAtmosphere(0.15)
		gatherPool()
		buildHud()
	elseif mode == "cast" then
		-- pulled back far enough that the hero's full height and the crowd's
		-- rim rows stay in frame on the standing-on-terrain staging
		setPerspectiveCamera(18.0, 6.5, -9.0)
		-- a flat sky-blue backdrop (no HDR sky dome, which blew the stage's
		-- exposure out) and a FIXED key light (no sun arc in update): the flat
		-- background keeps the exposure neutral AND gives the anim probe a
		-- guaranteed byte-stable static reference band
		engine:setAtmosphere(false, 0, 0, 0, 0, 0)
		engine:setWindowBackgroundColour(0.42, 0.55, 0.70)
		driveSun(0.33)
		gatherPool()
		-- the front-and-centre hero: cache its rig via world.getAnimation (the
		-- OTHER-object accessor leg of the seam) and start it walking; the
		-- update loop cross-fades it walk<->idle so the blend shows
		castHeroAnim = world.getAnimation("HeroCast")
		castHeroState, castHeroTimer = "walk", 0.0
		if castHeroAnim ~= nil then
			castHeroAnim:playAnimation("walk", true)
			print("cast: hero anim ok")
		else
			print("cast: hero anim FAIL - world.getAnimation('HeroCast') nil")
		end
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
		-- the gui vignette cycles the active language and leaves the cycle
		-- wherever its timer stopped; the results card reads in the authored
		-- language, not in a pseudo-locale like en-XA's [!bracketed!] text
		pcall(function() locale.set(locale.getSource()) end)
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
		-- hand the day skybox back to the procedural dome for the night leg so
		-- the sky darkens with the descending sun (a fixed day cubemap can't),
		-- and drop the day IBL. One-shot at the sunset->night transition.
		if vistaSkybox and p >= 0.6 then
			vistaSkybox = false
			engine:setAtmosphereSky("procedural", "")
			engine:setImageLighting(false, 0.4)
		end
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
	elseif mode == "cast" then
		orbitCamera(elapsed * 0.2)
		-- the skinning stress ramp: activate mannequins until the frame budget
		-- bends (default ~40 fps) or the pool is spent. Two per frame saturates
		-- the pool quickly under the automation ceiling so the structural
		-- budget capture reflects the full crowd.
		rampPool(frameMs, 2, #pool)
		-- one visible walk<->idle<->walk crossfade cycle on the hero
		castHeroTimer = castHeroTimer + dt
		if castHeroAnim ~= nil and castHeroTimer > 1.5 then
			castHeroTimer = 0.0
			if castHeroState == "walk" then
				castHeroState = "idle"
				castHeroAnim:crossFadeTo("idle", 0.4)
			else
				castHeroState = "walk"
				castHeroAnim:crossFadeTo("walk", 0.4)
			end
		end
	elseif mode == "flatland" then
		-- nothing per-frame; tweens + morph + vector anim run themselves
	elseif mode == "console" then
		cycleLanguage(dt)
	elseif mode == "cascade" then
		driveCascade(dt, p)
	elseif mode == "tally" then
		-- the Restart button replays the tour from its first scene; the
		-- automation seam fires the SAME path after autoRestart frames so a
		-- headless run proves the wiring without a synthetic click
		if resRestartBtn ~= nil and resRestartBtn:wasClicked() then
			restartTour()
		end
		if autoRestart >= 1.0 and frames >= autoRestart then
			restartTour()
		end
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
	-- the switch always goes through the LEVEL sequence (loadLevel advances
	-- the LevelManager index; a raw scene switch would leave it in place and
	-- re-resolve the SAME "next" forever). The wipe is a fade layered around
	-- that same switch, never a different code path.
	if frames >= budget and not advancing then
		advancing = true
		if world.getTimeScale() ~= 1.0 then
			world.setTimeScale(1.0)
		end
		-- end of the tour: HOLD on the final scene (the results card) so a
		-- played run visibly ends. Decided BEFORE the wipe - fading out and
		-- back in on the held card read as the results being shown twice.
		-- Attract-mode looping is the opt-in (benchmark.loop=1 - an exported
		-- kiosk/soak build sets it).
		local atEnd = levels == nil or count == 0 or (index + 1) >= count
		if atEnd and cvar.getNumber("benchmark.loop", 0) < 0.5 then
			return
		end
		if wipe > 0.5 and screen ~= nil then
			screen.fadeOut(0.3)
			switchAtFrame = frames + 22   -- ~0.35 s at the vsync pace
		else
			switchAtFrame = frames        -- switch this frame
		end
	end
	if advancing and switchAtFrame ~= nil and frames >= switchAtFrame then
		switchAtFrame = nil
		-- a restart forces the first scene; a normal advance walks the sequence
		local nextIndex = forcedNextIndex or (index + 1)
		if forcedNextIndex == nil and nextIndex >= count then
			nextIndex = 0	-- attract-mode loop (the hold case never gets here)
		end
		forcedNextIndex = nil
		if levels ~= nil then
			levels:loadLevel(nextIndex)
		else
			world.loadScene("scenes/vista.oscene")
		end
	end
end

function shutdown(self)
	if gui ~= nil then
		pcall(function() gui:destroyAllWidgets() end)
	end
	gui, factory, hudTitle, hudInfo, hudScrim = nil, nil, nil, nil, nil
	resRestartBtn = nil
	pool = {}
	if world ~= nil then
		pcall(function() world.setTimeScale(1.0) end)
	end
end
