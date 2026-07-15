-- handle_surface_probe.lua : the weak-handle COMPLETENESS sweep. The owning
-- GameObject carries every scriptable component, so `self` hands a handle for
-- each; world.get* covers the sound/camera/level accessors and a gui label backs
-- the layer handle. init() CALLS every method each handle binds: a name is
-- hard-recorded as MISSING when it does not resolve to a bound function (a
-- dropped/renamed binding fails the test), and the call itself is best-effort
-- (a semantic failure - a missing mesh, an empty sound - is tolerated, the point
-- is that every method DISPATCHES). Results land in the shared table for the
-- C++ side to assert.

local function init_probe(self)
	local V, Q = Vector3, Quaternion
	local missing, called = {}, 0

	-- assert each name is a bound function, then best-effort call it; a method
	-- flagged noCall is only presence-verified (disruptive to invoke here).
	local function drive(h, label, methods)
		if h == nil then
			table.insert(missing, label .. " (handle is nil)")
			return
		end
		for _, m in ipairs(methods) do
			local name = m[1]
			if type(h[name]) ~= "function" then
				table.insert(missing, label .. ":" .. name)
			elseif m.noCall then
				called = called + 1				-- presence-verified
			else
				pcall(function() h[name](h, table.unpack(m, 2)) end)
				called = called + 1
			end
		end
	end

	-- the GameObject handle (self.gameObject) - .id is a locked property
	if type(self.gameObject.id) ~= "string" then
		table.insert(missing, "GameObject.id (property)")
	end
	drive(self.gameObject, "GameObject", {
		{"getObjectID"}, {"loadTemplate", "nonexistent.template"},
		{"saveTemplate", noCall = true}, {"getParentId"}, {"setParent", ""},
		{"getChildIds"}, {"getPrefabRef"}, {"setActive", true}, {"isActiveSelf"},
		{"isActiveInHierarchy"}, {"hasTag", "surfaceTag"}, {"addTag", "t2"},
		{"removeTag", "t2"}, {"getParent"},
	})

	-- the ScriptComponent handle (self.script) - reload/hotReload/setScriptFile
	-- would disrupt THIS running instance, so they are presence-verified only
	drive(self.script, "Script", {
		{"getScriptFile"}, {"setScriptEnabled", true}, {"isScriptEnabled"},
		{"hasScriptError"}, {"getScriptError"}, {"isScriptStarted"},
		{"hasReloadError"}, {"getLastReloadError"},
		{"setScriptFile", noCall = true}, {"reloadScript", noCall = true},
		{"hotReload", noCall = true},
	})

	drive(self.transform, "Transform", {
		{"getPosition"}, {"getScale"}, {"getOrientation"}, {"getWorldPosition"},
		{"getWorldOrientation"}, {"getWorldScale"}, {"setPosition", V(0,0,0)},
		{"setScale", V(1,1,1)}, {"setOrientation", Q(1,0,0,0)},
		{"setWorldPosition", V(0,0,0)}, {"setWorldOrientation", Q(1,0,0,0)},
		{"teleport", V(0,0,0)},
	})

	drive(self.rigidbody, "RigidBody", {
		{"setBodyType", 2}, {"setBoxShape", V(0.5,0.5,0.5)},
		{"setSphereShape", 0.5}, {"setCapsuleShape", 0.5, 1.0}, {"setMass", 1.0},
		{"setFriction", 0.5}, {"setRestitution", 0.2}, {"setPlanarMode", true},
		{"getPlanarMode"}, {"setLayer", "default"}, {"getLayer"},
		{"setIsSensor", false}, {"isSensor"}, {"setLinearVelocity", V(0,0,0)},
		{"getLinearVelocity"}, {"setAngularVelocity", V(0,0,0)},
		{"getAngularVelocity"}, {"applyImpulse", V(0,0,0)}, {"applyForce", V(0,0,0)},
		{"teleport", V(0,0,0)}, {"hasBody"}, {"getBodyId"},
	})

	drive(self.model, "Model", {
		{"loadModel", "nonexistent.mesh"}, {"getCurrentModelFileName"},
		{"setMaterialReference", "none.omat"}, {"getMaterialFileName"},
	})

	drive(self.sprite, "Sprite", {
		{"loadSprite", "player.png"}, {"hasSprite"}, {"getTextureName"},
		{"setSize", 64.0, 64.0}, {"getWidth"}, {"getHeight"}, {"getRenderedWidth"},
		{"getRenderedHeight"}, {"setUVRect", 0.0, 0.0, 1.0, 1.0},
		{"setTint", 1.0, 1.0, 1.0, 1.0}, {"setFlip", false, false}, {"getFlipX"},
		{"getFlipY"}, {"setZOrder", 0}, {"getZOrder"}, {"setSpriteVisible", true},
		{"isSpriteVisible"}, {"loadSpriteFromAtlas", "none.oatlas", "x"},
		{"removeSprite"},
	})

	drive(self.particles, "Particles", {
		{"setTexture", "none.png"}, {"getTextureName"}, {"burst", 4}, {"start"},
		{"stop"}, {"setEmitting", true}, {"isEmitting"}, {"getLiveCount"},
	})

	drive(self.shape, "VectorShape", {
		{"loadShape", "none.oshape"}, {"getShapeName"}, {"hasShape"},
		{"getTriangleCount"}, {"setTint", 1.0, 1.0, 1.0, 1.0}, {"setScale", 1.0},
		{"getScale"}, {"setEdgeSoftness", 1.0}, {"getEdgeSoftness"},
		{"setZOrder", 0}, {"getZOrder"}, {"setShapeVisible", true},
		{"isShapeVisible"}, {"setSoftBodyEnabled", false}, {"isSoftBodyEnabled"},
		{"impulse", V(0,0,0)}, {"playMorph", "none"}, {"stopMorph"},
		{"getDeformDisplacement"}, {"getSquash"}, {"isDeforming"},
		{"getControlPointCount"}, {"getMorphTargetCount"}, {"removeShape"},
	})

	drive(self.anim, "VectorAnimation", {
		{"loadAnimation", "none.oanim"}, {"getAnimationName"}, {"hasAnimation"},
		{"getTriangleCount"}, {"getVertexCount"}, {"getPoseSignature"}, {"play"},
		{"stop"}, {"setClip", "idle"}, {"crossFade", "idle", 0.2}, {"scrub", 0.5},
		{"isPlaying"}, {"currentClip"}, {"getClipCount"}, {"getClipNames"},
		{"currentFrame"}, {"isAtEnd"}, {"setSpeed", 1.0}, {"getSpeed"},
		{"removeAnimation"},
	})

	-- sound / camera / level have no `self` field; reach them via world.get*
	drive(world.getSound("surface"), "Sound", {
		{"addSound", "s", "none.wav", false, false}, {"play", "s"}, {"stop", "s"},
		{"stopAllSounds"}, {"setVolume", "s", 0.5}, {"getVolume", "s"},
		{"setGroup", "s", "sfx"}, {"getGroup", "s"}, {"setPitchVariation", "s", 0.1},
		{"setVolumeVariation", "s", 0.1},
	})

	drive(world.getCamera("surface"), "Camera", {
		{"setProjectionMode", 1}, {"getProjectionMode"}, {"setOrthoSize", 5.0},
		{"getOrthoSize"}, {"setFitMode", 0}, {"getFitMode"}, {"setDesignWidth", 800.0},
		{"getDesignWidth"}, {"setDesignHeight", 600.0}, {"getDesignHeight"},
		{"follow", "surface", 0.2}, {"stopFollow"}, {"setFollowTarget", "surface"},
		{"getFollowTarget"}, {"setFollowDamping", 0.2}, {"getFollowDamping"},
		{"setFollowOffset", V(0,0,0)}, {"getFollowOffset"},
	})

	drive(world.getLevel("surface"), "Level", {
		{"getCols"}, {"getRows"}, {"getTileSize"}, {"getOriginX"}, {"getOriginY"},
		{"getGoalSlot"}, {"getPar"}, {"getSlotCount"}, {"slotForPosition", 0.0, 0.0},
		{"slotForCell", 0, 0}, {"slotCol", 0}, {"slotRow", 0}, {"slotCenterX", 0},
		{"slotCenterY", 0}, {"starsForMoves", 1},
	})

	-- a gui label -> the screen-scoped LAYER handle
	shared.surf_factory = GuiFactory()
	shared.surf_gui = GuiManager(shared.surf_factory, "gui_default", "General")
	shared.surf_factory:createLabel(
		"surfLabel", 9, "surf", Vector2(0, 0), "", 6, false)
	local label = shared.surf_gui:findLabel("surfLabel")
	drive(label and label:getLayer() or nil, "Layer", {
		{"isVisible"}, {"show"}, {"hide"}, {"setVisible", true},
	})

	shared.surf_called = called
	shared.surf_missing = table.concat(missing, ", ")
	shared.surf_missing_n = #missing
end

function init(self)
	init_probe(self)
end
