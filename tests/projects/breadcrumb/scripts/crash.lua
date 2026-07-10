-- Deliberately-failing script for the player_breadcrumb_selfcheck ctest
-- (ORKIGE_BREADCRUMB_SELFCHECK): init raises a Lua runtime error, which the
-- ScriptComponent catches and reports (hasScriptError), the player records as
-- a "script_error" breadcrumb, and the selfcheck reads back off disk. The
-- game keeps running (a failed script disables itself, non-fatal).
function init(self)
	-- call a global that does not exist -> a runtime error at init time
	this_function_does_not_exist()
end

function update(self, dt)
end
