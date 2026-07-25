-- lines_check.lua - the dynamic-lines end-to-end fixture script
-- (run by the player_lines_selfcheck ctest: ORKIGE_LINES_SELFCHECK=1
-- orkige_player --project tests/projects/lines).
--
-- Exercises both consumer surfaces of the one line mechanism:
--   * the immediate-mode `draw` table: a frame-only line, a short-TTL box and
--     a frame-only sphere are queued every frame of the draw window, then the
--     script stops so the frame-only + TTL primitives drain
--   * the authored LineComponent (self.line): reshaped every frame at the SAME
--     point count, so it rides the dynamic updateVertices fast path (the C++
--     side asserts the rebuild count stays small - no per-frame churn)
--
-- The C++ selfcheck block in tools/player/PlayerSelfChecks.cpp reads
-- shared.lines and independently verifies the LineComponent + DebugDraw state.

local frame = 0
local DRAW_FRAMES = 30

function init(self)
	shared.lines = {
		drawing = true,		-- the draw window is open
		points = 0,			-- self.line:getPointCount() at boot (authored strip)
		verts = 0,			-- self.line:getVertexCount() (live mesh)
		rebuilds = 0,		-- self.line:getRebuildCount() (churn probe)
		hasLine = false,
	}
	if self.line ~= nil then
		shared.lines.hasLine = true
		shared.lines.points = self.line:getPointCount()
		shared.lines.verts = self.line:getVertexCount()
		shared.lines.rebuilds = self.line:getRebuildCount()
	end
end

function update(self, dt)
	frame = frame + 1
	if frame <= DRAW_FRAMES then
		-- immediate-mode debug draw (world space, RGB in 0..1):
		draw.line(0, 0, 0, 2, 2, 0, 1, 1, 1)		-- this frame only
		draw.box(0, 0, 0, 1, 1, 1, 0, 1, 0, 0.2)	-- a 0.2s TTL box
		draw.sphere(3, 0, 0, 1, 0, 0, 1)			-- this frame only
		-- reshape the authored line at the SAME count (4 points -> the dynamic
		-- fast path; the middle points bob so the geometry genuinely changes)
		if self.line ~= nil then
			local y = math.sin(frame * 0.3)
			-- reshape via the incremental builder (4 points, stable count -> the
			-- dynamic fast path); the middle points bob so the geometry moves
			self.line:beginPoints()
			self.line:addPoint(0, 0, 0)
			self.line:addPoint(1, y, 0)
			self.line:addPoint(2, 0, 0)
			self.line:addPoint(3, y, 0)
			self.line:commitPoints()
			shared.lines.verts = self.line:getVertexCount()
			shared.lines.rebuilds = self.line:getRebuildCount()
		end
	else
		-- close the draw window: no more draw.* calls, so the frame-only
		-- primitives are gone next frame and the last TTL box ages out
		shared.lines.drawing = false
	end
end
