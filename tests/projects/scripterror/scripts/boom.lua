-- Break-on-script-errors fixture, driven by the player_script_debug driver's
-- error legs. update() ticks harmlessly until the cvar "test.boom" is set (the
-- driver flips it over the debug link), then raises an UNCAUGHT runtime error
-- by indexing a nil value. With Break on Errors armed the runtime pauses AT the
-- erroring line carrying the crash text; unarmed it flows the normal
-- instance-disable + MSG_SCRIPT_ERROR report. registerNumber runs at load so
-- the cvar exists when the driver sets it (the driver locates the fault line by
-- scanning the code for the nil-index return, so edits here move it along).
cvar.registerNumber("test.boom", 0)

function update(self, dt)
	if cvar.getNumber("test.boom", 0) >= 1 then
		local bad = nil
		return bad.field
	end
end
