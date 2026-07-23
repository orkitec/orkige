--- watertest camera: a fixed raised vantage over the test tank so the
--- submerged bed and blocks read immediately (a grazing camera hides
--- transmission behind fresnel - this one looks ~30 degrees down).
--- The day atmosphere comes from the TESTED preset (raw sky/fog values
--- white surfaces out or black the sky - always blend presets).

function init(self)
	engine = Engine.getSingleton()
	engine:setAtmosphereSky("procedural", "")
	engine:setAtmosphereBlend("day", "sunset", 0.0)
	engine:setCameraPerspective()
	local cam = engine:getCamera()
	local node = cam ~= nil and cam:getNode() or nil
	if node ~= nil then
		node:setPosition(Vector3(0, 7, 13))
		node:lookAt(Vector3(0, -1.0, -2.0), TS.TS_WORLD, Vector3(0, 0, -1))
	end
end

function update(self, dt)
end
