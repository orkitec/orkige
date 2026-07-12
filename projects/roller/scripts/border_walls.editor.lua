-- tool: Paint Wall Border
-- An EDITOR TOOL (see Docs/lua-api.md). Frames the level's grid with wall tiles:
-- reads the scene's LevelComponent for the grid size, then paints a wall.png
-- bare tile into every outer-edge cell with editor.paint_asset. The whole run
-- folds into ONE undo step, so a single Cmd+Z clears the whole frame.
--
-- Run it from the editor's Tools menu, or over MCP with
-- run_editor_script { "name": "border_walls" }.

local WALL = "assets/wall.png"

-- find the object that carries a LevelComponent (the grid source)
local function findLevelObject()
	local hierarchy = editor.list_hierarchy()
	for _, id in ipairs(hierarchy.ids or {}) do
		local object = editor.get_object{ id = id }
		for _, component in ipairs(object.components or {}) do
			if component == "LevelComponent" then
				return id
			end
		end
	end
	return nil
end

local levelId = findLevelObject()
if not levelId then
	editor.log("no LevelComponent in this scene - nothing to frame")
	return
end

local level = editor.get_component{ id = levelId, component = "LevelComponent" }
local cols = math.floor(tonumber(level.cols) or 0)
local rows = math.floor(tonumber(level.rows) or 0)
if cols < 2 or rows < 2 then
	editor.log("the level grid is too small to frame (" ..
		tostring(cols) .. "x" .. tostring(rows) .. ")")
	return
end

local painted = 0
for col = 0, cols - 1 do
	for row = 0, rows - 1 do
		local onEdge = col == 0 or col == cols - 1 or
			row == 0 or row == rows - 1
		if onEdge then
			-- paint_asset replaces any existing occupant of the same cell, so
			-- re-running the tool is safe (a second run repaints, not stacks)
			editor.paint_asset{ asset = WALL, cell = { col = col, row = row } }
			painted = painted + 1
		end
	end
end

editor.log("framed the " .. cols .. "x" .. rows ..
	" level with " .. painted .. " wall tile(s)")
