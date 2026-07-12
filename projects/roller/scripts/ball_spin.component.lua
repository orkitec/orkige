-- ball_spin.component.lua - a SECOND script component on the "Ball" object,
-- alongside ball.component.lua. It exists to show that a script is a per-name
-- component KIND and that several DIFFERENT scripts coexist on one object, each
-- in its own sandbox with its own lifecycle.
--
-- It is deliberately cosmetic/observational (it never touches physics or the
-- transform, so it cannot perturb ball.component.lua's gameplay): it keeps a
-- heartbeat and publishes it into `shared.roller`, driven by a DESIGNER-TUNABLE
-- declared property.
--
-- `blinkRate` is a declared export property: it shows in the Inspector, is
-- injected onto `self.blinkRate` before init, and is OVERRIDDEN in the scene
-- (default 1.0 -> 2.0) - the scene's value wins.

properties = {
	-- heartbeats per second (a small, safe designer knob)
	blinkRate = { type = "number", default = 1.0, min = 0.1, max = 10.0 },
}

function init(self)
	shared.roller = shared.roller or {}
	-- prove this second script started and that the scene override reached self
	shared.roller.spinReady = true
	shared.roller.blinkRate = self.blinkRate
	self._elapsed = 0.0
	self._blinks = 0
end

function update(self, dt)
	local period = 1.0 / self.blinkRate
	self._elapsed = self._elapsed + dt
	while self._elapsed >= period do
		self._elapsed = self._elapsed - period
		self._blinks = self._blinks + 1
		shared.roller.blinks = self._blinks
	end
end

function shutdown(self)
	shared.roller.spinReady = false
end
