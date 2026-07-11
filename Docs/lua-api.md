# Lua API reference

The scripting surface a game reaches from a `ScriptComponent` (`projects/*/scripts/*.lua`).
Game behavior lives in these scripts; `projects/jumper-lua/scripts/player.lua` and
`projects/roller/scripts/ball.lua` are the reference reads.

This page is layered. The **signature index** below is the whole API, one line per
symbol, for a fast top-to-bottom scan (an agent ingests it in a single read). Below
it are the **conventions**, **canonical snippets** and a fuller **type reference**.

The index and the type reference are GENERATED from the binding sources by
`Util/update_docs.py` (a `ctest` keeps them from drifting). Signatures are curated
in `Util/lua_api_annotations.json`; fix a signature there, never in the generated
text. Everything gui-widget lives in [Docs/gui.md](gui.md).

## Signature index

Legend: `table.fn(args) -> ret` is a global-table call, `Type:method(...)` an
instance method, `Type.field` a member, `Type(ctor)` a constructor. `?` marks a
value that can be `nil`. `[x]` is an optional argument.

<!-- GENERATED:lua-api-index - edit Util/update_docs.py / lua_api_annotations.json; do not hand-edit -->
```text
# GLOBAL TABLES  (reach any object by id; ? = may be nil)

## world
world.exists(id) -> bool  -- is a GameObject with this id alive
world.get(id) -> GameObject?  -- the live GameObject by id (nil if gone)
world.getTransform(id) -> TransformComponent?  -- an object's TransformComponent (nil if none)
world.getRigidBody(id) -> RigidBodyComponent?  -- an object's RigidBodyComponent (nil if none)
world.getModel(id) -> ModelComponent?  -- an object's ModelComponent (nil if none)
world.getSprite(id) -> SpriteComponent?  -- an object's SpriteComponent (nil if none)
world.getParticles(id) -> ParticleComponent?  -- an object's ParticleComponent (nil if none)
world.getScript(id) -> ScriptComponent?  -- an object's ScriptComponent (nil if none)
world.getSound(id) -> SoundComponent?  -- an object's SoundComponent (nil if none)
world.getLevel(id) -> LevelComponent?  -- an object's LevelComponent (nil if none)
world.loadScene(path)  -- deferred scene switch at the next frame boundary
world.findByTag(tag) -> {GameObject}  -- array of live objects carrying the tag
world.setTimeScale(scale)  -- gameplay time scale (1 normal, 0 hitstop)
world.getTimeScale() -> number  -- the current gameplay time scale

## screen
screen.fadeOut(seconds)  -- ramp the full-screen overlay to opaque
screen.fadeIn(seconds)  -- ramp the full-screen overlay to clear
screen.setFadeColor(r, g, b)  -- fade overlay colour (default black), 0..1
screen.isFading() -> bool  -- is a fade in progress (gate input)
screen.loadScene(path, outSeconds, inSeconds)  -- fade out, switch scene at opacity, fade in
screen.shake(amplitude, duration [, frequency])  -- decaying camera shake (world units, seconds, Hz)
screen.stopShake()  -- end the camera shake immediately
screen.isShaking() -> bool  -- is the camera currently shaking

## sound
sound.setGroupVolume(group, volume)  -- set a mixer group volume 0..1
sound.getGroupVolume(group) -> number  -- read a mixer group volume
sound.setMasterVolume(volume)  -- set the master volume 0..1
sound.getMasterVolume() -> number  -- read the master volume

## music
music.play(id, file [, loop]) -> bool  -- start/replace a streamed track (loops by default)
music.stop(id) -> bool  -- stop and free one streamed track
music.stopAll()  -- stop every streamed track
music.isPlaying(id) -> bool  -- is that track currently playing
music.setVolume(id, volume)  -- per-track own volume 0..1
music.getPosition(id) -> number  -- playback position in seconds

## tween
tween.to(from, to, duration, ease [, onUpdate [, onComplete [, delay]]]) -> TweenHandle  -- generic value tween; onUpdate(v) applies it
tween.move(id, x, y, z, duration [, ease [, delay]]) -> TweenHandle  -- tween an object's local position
tween.scale(id, x, y, z, duration [, ease [, delay]]) -> TweenHandle  -- tween an object's local scale
tween.rotate(id, degrees, duration [, ease [, delay]]) -> TweenHandle  -- tween Z rotation (the 2D spin)
tween.fade(id, alpha, duration [, ease [, delay]]) -> TweenHandle  -- tween a sprite's tint alpha
tween.volume(group, volume, duration [, ease [, delay]]) -> TweenHandle  -- tween a mixer group volume (the ducking recipe)
tween.property(id, componentType, propertyName, toValue, duration [, ease [, onComplete [, delay]]]) -> TweenHandle  -- tween any reflected Float/Int/Vec3/Color by name

## guitween
guitween.alpha(id, alpha, duration [, ease [, delay [, onComplete]]]) -> TweenHandle  -- widget group alpha 0..1 (cascades)
guitween.scale(id, scale, duration [, ...]) -> TweenHandle  -- uniform widget scale about the centre
guitween.rotate(id, degrees, duration [, ...]) -> TweenHandle  -- widget Z rotation about the centre
guitween.move(id, x, y, duration [, ...]) -> TweenHandle  -- widget anchoredPosition / position
guitween.size(id, w, h, duration [, ...]) -> TweenHandle  -- widget sizeDelta / size
guitween.color(id, r, g, b, a, duration [, ...]) -> TweenHandle  -- decor-widget tint
guitween.show(id) -> bool  -- play the widget's enter transition
guitween.hide(id) -> bool  -- play the exit transition, then park hidden
guitween.stop(id)  -- cancel every running tween on the widget

## haptics
haptics.play(strength, ms)  -- generic vibration; no-op off-device
haptics.pattern(name)  -- named haptic (light..selection); preferred on iOS
haptics.isAvailable() -> bool  -- does this device have a vibrator
haptics.setEnabled(enabled)  -- honour an in-game vibration on/off setting

## cvar
cvar.registerNumber(name, default)  -- register a Float cvar (idempotent)
cvar.getNumber(name [, fallback]) -> number  -- read a cvar as a number
cvar.getBool(name [, fallback]) -> bool  -- read a cvar as a bool
cvar.get(name) -> string  -- read a cvar's canonical string ('' if unset)
cvar.exists(name) -> bool  -- is the cvar registered
cvar.set(name, value) -> bool  -- set from a string (validated); false + logs on reject

## save
save.set(key, value)  -- store number/bool/string by its type
save.getNumber(key [, fallback]) -> number  -- read a saved number
save.getBool(key [, fallback]) -> bool  -- read a saved bool
save.getString(key [, fallback]) -> string  -- read a saved string
save.get(key [, fallback]) -> string  -- read any value as its canonical string
save.has(key) -> bool  -- is the key present
save.remove(key)  -- drop the key
save.flush() -> bool  -- autosave point - write to disk now

## globals
loc(key [, ...]) -> string  -- localised string; %%N%% filled by trailing args

# CORE TYPES  (Type(ctor); Type:method; Type.field)

## Vector3
Vector3(x, y, z)  -- 3D vector / point
Vector3:length() -> number  -- vector magnitude
Vector3:distance(v) -> number  -- distance to another point
Vector3:squaredDistance(v) -> number  -- squared distance (cheaper compare)
Vector3:dotProduct(v) -> number  -- dot product
Vector3:crossProduct(v) -> Vector3  -- cross product
Vector3:normalisedCopy() -> Vector3  -- unit-length copy
Vector3.x  -- x component
Vector3.y  -- y component
Vector3.z  -- z component

## Vector2
Vector2(x, y)  -- 2D vector (UI pixels / analog2D)
Vector2.x  -- x component
Vector2.y  -- y component

## Quaternion
Quaternion(w, x, y, z)  -- orientation quaternion
Quaternion.w  -- w component
Quaternion.x  -- x component
Quaternion.y  -- y component
Quaternion.z  -- z component

## InputActions
InputActions.getSingleton() -> InputActions  -- the named-input action map singleton
InputActions:down(name) -> bool  -- action held this frame (digital)
InputActions:pressed(name) -> bool  -- action went down this frame
InputActions:released(name) -> bool  -- action went up this frame
InputActions:value(name) -> number  -- analog 1D value
InputActions:value2(name) -> Vector2  -- analog 2D value (e.g. 'move' stick)
InputActions:hasAction(name) -> bool  -- is the action mapped

## TweenHandle
TweenHandle:cancel()  -- stop the tween now
TweenHandle:isActive() -> bool  -- is the tween still running (completion poll)
TweenHandle:setLoops(count, pingpong)  -- loop count (<0 forever); pingpong runs it back and forth

## SafeAreaInsets
SafeAreaInsets.mLeft  -- left inset in pixels
SafeAreaInsets.mTop  -- top inset in pixels
SafeAreaInsets.mRight  -- right inset in pixels
SafeAreaInsets.mBottom  -- bottom inset in pixels

## RayHit
RayHit.hit
RayHit.position
RayHit.bodyId
```
<!-- /GENERATED:lua-api-index -->

## Conventions

**The script shape.** A script file defines free functions the runtime calls on a
per-instance sandbox. None are required; define the ones you need:

```lua
function init(self)                    -- once, after load (self is populated)
function update(self, dt)              -- every frame while playing
function shutdown(self)                -- on stop / hot-reload retire / removal
function onContactBegin(self, other)   -- physics contact enter (other: GameObject)
function onContactEnd(self, other)     -- physics contact exit
function onAppPause(self)              -- app backgrounded (mobile)
function onAppResume(self)             -- app foregrounded (mobile)
```

**`self`** carries the owner and its sibling components, resolved at load:
`self.id` (string), `self.gameObject`, `self.script`, and — when attached —
`self.transform`, `self.rigidbody`, `self.model`, `self.sprite`, `self.particles`,
`self.shape` (a `VectorShapeComponent`). Script-declared export properties are
injected as `self.<name>` before `init` (so `self.moveSpeed` is a designer-tunable
that also shows in the Inspector). Components added later are reached through
`world`, not `self`.

**`world`** reaches OTHER objects by id (raw component pointers, valid while the
object lives — re-fetch when in doubt rather than caching across frames).

**`shared`** is the one cross-instance table (per-instance sandboxes make sharing
opt-in). Use it for game-wide state; `world`/`shared` are the two globals every
script sees.

**Honest no-ops.** Most tables tolerate the editor's edit mode (no runtime ticking
scripts): a call with no backing singleton logs once and returns a default rather
than erroring. `ORKIGE_SCRIPTING=OFF` builds keep `ScriptRuntime::available()`
false and every call honest.

**Wiring a `.oui` screen.** Author the layout as text, load it, then find widgets
by id and wire behavior — the bridge between declarative UI and script:

```lua
factory:loadLayout("screens/pause.oui")   -- or gui:loadLayout(...)
local resume = gui:findWidget("resumeBtn")  -- nil when absent
-- each frame: if resume:wasClicked() then ... end
```

## Canonical snippets

**Screen from a `.oui` + wire it.**

```lua
function init(self)
    factory:loadLayout("screens/hud.oui")
    self.coins = gui:findWidget("coinLabel")
end
function update(self, dt)
    if self.coins then self.coins:setText(tostring(save.getNumber("coins", 0))) end
end
```

**Save round-trip (autosave on a checkpoint).**

```lua
save.set("hero.coins", save.getNumber("hero.coins", 0) + 1)
save.set("hero.level", "forest")     -- number / bool / string by type
save.flush()                          -- write to disk NOW (a set alone is dirty-only)
```

**Music (survives scene switches) + ducking under a stinger.**

```lua
music.play("bgm", "music/level1.ogg")            -- loops by default
tween.volume("music", 0.3, 0.2)                   -- duck
tween.volume("music", 1.0, 0.8, "quadOut", 1.5)   -- restore after 1.5s
```

**Haptics (mobile; no-op on desktop).**

```lua
if haptics.isAvailable() then haptics.pattern("selection") end
haptics.play(0.8, 40)     -- generic strength + milliseconds
```

**Soft-body impulse (a `VectorShapeComponent` blob reacting to a hit).**

```lua
function onContactBegin(self, other)
    if self.shape then self.shape:impulse(Vector3(0, 12, 0), 0.6) end
end
```

**guitween chains (an endless spinner, a pulse, a menu transition).**

```lua
guitween.rotate("spinner", 360, 1.0, "linear"):setLoops(-1, false)
guitween.scale("badge", 1.2, 0.3, "quadInOut"):setLoops(-1, true)
panel:setTransition("slide-up 0.3"); guitween.show("menu")
```

**Screen shake + hitstop (time scale).**

```lua
screen.shake(0.4, 0.25)          -- amplitude (world units), duration
world.setTimeScale(0)            -- freeze the world (still renders)
tween.to(0, 1, 0.12, "linear", nil, function() world.setTimeScale(1) end)
```

**Scene wipe (fade over a deferred switch).**

```lua
screen.loadScene("scenes/level2.oscene", 0.3, 0.3)  -- out, switch at opacity, in
```

## Type reference

Every registered usertype not already in the index above: constructors,
methods (`:`) and fields (`.`), with the source's own inline notes where present.
Reached from the index accessors (`world.get*`, `self.*`, `engine:*`, the gui
factory). Generated from the `OSIMPLEEXPORT`/`OEXPORT` blocks.

<!-- GENERATED:lua-api-types - edit Util/update_docs.py / lua_api_annotations.json; do not hand-edit -->
```text
## LevelManager
LevelManager.getSingleton() -> LevelManager  -- the runtime level director singleton
LevelManager:count() -> int  -- number of levels in the sequence
LevelManager:currentIndex() -> int  -- the live level index
LevelManager:levelName(i) -> string  -- a level's display name
LevelManager:levelPar(i) -> int  -- a level's par slide count
LevelManager:levelScene(i) -> string  -- a level's project-relative scene path
LevelManager:hasNext() -> bool  -- is there a level after the current one
LevelManager:loadLevel(i)  -- request the deferred load of level i
LevelManager:loadScenePath(path)  -- deferred load of any scene path
LevelManager:resumeLevel() -> int  -- the persisted resume index
LevelManager:setResumeLevel(i)  -- persist the resume index
LevelManager:bestMoves(i) -> int  -- fewest recorded slides (-1 = none)
LevelManager:recordBestMoves(i, moves)  -- record a best-moves result for level i
LevelManager:saveProgress()  -- write the progression save file

## GameObject
GameObject(...)
GameObject:loadTemplate(...)
GameObject:saveTemplate(...)
GameObject:getParentId(...)
GameObject:getParent(...)
GameObject:getChildIds(...)
GameObject:getPrefabRef(...)
GameObject:setActive(...)
GameObject:isActiveSelf(...)
GameObject:isActiveInHierarchy(...)
GameObject:hasTag(...)
GameObject:addTag(...)
GameObject:removeTag(...)

## GameObjectManager
GameObjectManager.getSingleton(...)
GameObjectManager:addGameObject(...)
GameObjectManager:delGameObject(...)
GameObjectManager:getGameObject(...)
GameObjectManager:objectExists(...)
GameObjectManager:createGameObject(...)
GameObjectManager:enableEvent(...)
GameObjectManager:disableEvent(...)

## LevelComponent
LevelComponent:getCols(...)
LevelComponent:getRows(...)
LevelComponent:getTileSize(...)
LevelComponent:getOriginX(...)
LevelComponent:getOriginY(...)
LevelComponent:getGoalSlot(...)
LevelComponent:getPar(...)
LevelComponent:getSlotCount(...)
LevelComponent:slotForPosition(...)
LevelComponent:slotForCell(...)
LevelComponent:slotCol(...)
LevelComponent:slotRow(...)
LevelComponent:slotCenterX(...)
LevelComponent:slotCenterY(...)
LevelComponent:starsForMoves(...)
LevelComponent.cols
LevelComponent.rows
LevelComponent.tileSize
LevelComponent.originX
LevelComponent.originY
LevelComponent.goalSlot
LevelComponent.par

## TileComponent
TileComponent:getOpenEdges(...)
TileComponent:setOpenEdges(...)
TileComponent:isEdgeOpen(...)
TileComponent.openEdges

## GuiFactory
GuiFactory(...)
GuiFactory:createLabel(...)
GuiFactory:createButton(...)
GuiFactory:createProgressBar(...)
GuiFactory:createCheckBox(...)
GuiFactory:createSlider(...)
GuiFactory:createSelectMenu(...)
GuiFactory:createDecorWidget(...)
GuiFactory:createTextEntry(...)
GuiFactory:createScrollView(...)
GuiFactory:createDropDown(...)
GuiFactory:loadLayout(...)

## GuiToggleGroup
GuiToggleGroup:addMember(...)
GuiToggleGroup:getSelected(...)
GuiToggleGroup:setSelected(...)
GuiToggleGroup:setAllowNone(...)
GuiToggleGroup:getMemberCount(...)
GuiToggleGroup:pollChanged(...)

## GuiLayer
GuiLayer:show(...)
GuiLayer:hide(...)
GuiLayer:isVisible(...)
GuiLayer:setVisible(...)

## RenderNode
RenderNode:getPosition(...)
RenderNode:setPosition(...)
RenderNode:getOrientation(...)
RenderNode:setOrientation(...)
RenderNode:getScale(...)
RenderNode:setScale(...)
RenderNode:getWorldPosition(...)
RenderNode:getWorldOrientation(...)
RenderNode:translate(...)
RenderNode:lookAt(...)
RenderNode:setDirection(...)
RenderNode:setFixedYawAxis(...)
RenderNode:setVisible(...)
RenderNode:createChild(...)
RenderNode:getParent(...)
RenderNode:numChildren(...)
RenderNode.TransformSpace = { TS_LOCAL, TS_PARENT, TS_WORLD }

## RenderCamera
RenderCamera:getNode(...)
RenderCamera:setOrthographic(...)
RenderCamera:getProjectionType(...)
RenderCamera:getNearClip(...)
RenderCamera:getFarClip(...)
RenderCamera:setAspectRatio(...)
RenderCamera:setWireframe(...)
RenderCamera.ProjectionType = { PT_PERSPECTIVE, PT_ORTHOGRAPHIC }

## RenderSystem
RenderSystem:getWindowCamera(...)
RenderSystem:getWorld(...)
RenderSystem:saveWindowContents(...)

## RenderWorld
RenderWorld:getRootNode(...)
RenderWorld:createNode(...)

## TransformComponent
TransformComponent:getPosition(...)
TransformComponent:getScale(...)
TransformComponent:getOrientation(...)
TransformComponent:setPosition(...)
TransformComponent:setScale(...)
TransformComponent:setOrientation(...)
TransformComponent:getWorldPosition(...)
TransformComponent:getWorldOrientation(...)
TransformComponent:getWorldScale(...)
TransformComponent:setWorldPosition(...)
TransformComponent:setWorldOrientation(...)
TransformComponent:teleport(...)
TransformComponent.position
TransformComponent.orientation
TransformComponent.scale

## ModelComponent
ModelComponent:loadModel(...)
ModelComponent:getCurrentModelFileName(...)
ModelComponent.mesh

## SpriteComponent
SpriteComponent:loadSprite(...)
SpriteComponent:loadSpriteFromAtlas(...)
SpriteComponent:removeSprite(...)
SpriteComponent:getTextureName(...)
SpriteComponent:hasSprite(...)
SpriteComponent:setSize(...)
SpriteComponent:getWidth(...)
SpriteComponent:getHeight(...)
SpriteComponent:getRenderedWidth(...)
SpriteComponent:getRenderedHeight(...)
SpriteComponent:setUVRect(...)
SpriteComponent:setTint(...)
SpriteComponent:setFlip(...)
SpriteComponent:getFlipX(...)
SpriteComponent:getFlipY(...)
SpriteComponent:setZOrder(...)
SpriteComponent:getZOrder(...)
SpriteComponent:setSpriteVisible(...)
SpriteComponent:isSpriteVisible(...)
SpriteComponent.width
SpriteComponent.height
SpriteComponent.u0
SpriteComponent.v0
SpriteComponent.u1
SpriteComponent.v1
SpriteComponent.tint
SpriteComponent.flipX
SpriteComponent.flipY
SpriteComponent.zOrder
SpriteComponent.visible
SpriteComponent.texture

## VectorShapeComponent
VectorShapeComponent:loadShape(...)
VectorShapeComponent:removeShape(...)
VectorShapeComponent:getShapeName(...)
VectorShapeComponent:hasShape(...)
VectorShapeComponent:getTriangleCount(...)
VectorShapeComponent:setTint(...)
VectorShapeComponent:setScale(...)
VectorShapeComponent:getScale(...)
VectorShapeComponent:setEdgeSoftness(...)
VectorShapeComponent:getEdgeSoftness(...)
VectorShapeComponent:setZOrder(...)
VectorShapeComponent:getZOrder(...)
VectorShapeComponent:setShapeVisible(...)
VectorShapeComponent:isShapeVisible(...)
VectorShapeComponent:setSoftBodyEnabled(...)
VectorShapeComponent:isSoftBodyEnabled(...)
VectorShapeComponent:impulse(...)
VectorShapeComponent:playMorph(...)
VectorShapeComponent:stopMorph(...)
VectorShapeComponent:getDeformDisplacement(...)
VectorShapeComponent:getSquash(...)
VectorShapeComponent:isDeforming(...)
VectorShapeComponent:getControlPointCount(...)
VectorShapeComponent:getMorphTargetCount(...)
VectorShapeComponent.tint
VectorShapeComponent.scale
VectorShapeComponent.edgeSoftness
VectorShapeComponent.zOrder
VectorShapeComponent.visible
VectorShapeComponent.softBody
VectorShapeComponent.wobbleStiffness
VectorShapeComponent.wobbleDamping
VectorShapeComponent.wobbleAmount
VectorShapeComponent.squashAmount
VectorShapeComponent.morphClip
VectorShapeComponent.morphSpeed
VectorShapeComponent.morphLoop
VectorShapeComponent.shape

## SpriteAnimationComponent
SpriteAnimationComponent:setGrid(...)
SpriteAnimationComponent:getGridColumns(...)
SpriteAnimationComponent:getGridRows(...)
SpriteAnimationComponent:addClip(...)
SpriteAnimationComponent:removeClip(...)
SpriteAnimationComponent:hasClip(...)
SpriteAnimationComponent:getClipCount(...)
SpriteAnimationComponent:setDefaultClip(...)
SpriteAnimationComponent:getDefaultClip(...)
SpriteAnimationComponent:play(...)
SpriteAnimationComponent:stop(...)
SpriteAnimationComponent:setClip(...)
SpriteAnimationComponent:isPlaying(...)
SpriteAnimationComponent:getCurrentClip(...)
SpriteAnimationComponent:setFrame(...)
SpriteAnimationComponent:getCurrentFrame(...)
SpriteAnimationComponent:setSpeed(...)
SpriteAnimationComponent:getSpeed(...)
SpriteAnimationComponent.gridColumns
SpriteAnimationComponent.gridRows
SpriteAnimationComponent.defaultClip

## ParticleComponent
ParticleComponent:setTexture(...)
ParticleComponent:getTextureName(...)
ParticleComponent:burst(...)
ParticleComponent:start(...)
ParticleComponent:stop(...)
ParticleComponent:setEmitting(...)
ParticleComponent:isEmitting(...)
ParticleComponent:getLiveCount(...)

## AnimationComponent
AnimationComponent:getAvailableAnimations(...)
AnimationComponent:getBoneNames(...)
AnimationComponent:getDefaultAnimation(...)
AnimationComponent:setDefaultAnimation(...)
AnimationComponent:getHandleMotion(...)
AnimationComponent:getHandleRotation(...)
AnimationComponent:getExtractMotion(...)
AnimationComponent:getExtractRotation(...)
AnimationComponent:setHandleMotion(...)
AnimationComponent:setHandleRotation(...)
AnimationComponent:setExtractMotion(...)
AnimationComponent:setExtractRotation(...)
AnimationComponent:getMotionBone(...)
AnimationComponent:setMotionBone(...)

## CameraComponent
CameraComponent:setProjectionMode(...)
CameraComponent:getProjectionMode(...)
CameraComponent:setOrthoSize(...)
CameraComponent:getOrthoSize(...)
CameraComponent:setFitMode(...)
CameraComponent:getFitMode(...)
CameraComponent:setDesignWidth(...)
CameraComponent:getDesignWidth(...)
CameraComponent:setDesignHeight(...)
CameraComponent:getDesignHeight(...)
CameraComponent.projectionMode
CameraComponent.orthoSize
CameraComponent.fitMode
CameraComponent.designWidth
CameraComponent.designHeight
CameraComponent.ProjectionMode = { PM_PERSPECTIVE, PM_ORTHOGRAPHIC }
CameraComponent.FitMode = { FM_HEIGHT, FM_WIDTH, FM_EXPAND }

## SoundComponent
SoundComponent:addSound(...)
SoundComponent:play(...)
SoundComponent:stop(...)
SoundComponent:stopAllSounds(...)
SoundComponent:setVolume(...)
SoundComponent:getVolume(...)
SoundComponent:setGroup(...)
SoundComponent:getGroup(...)

## RigidBodyComponent
RigidBodyComponent:setBodyType(...)
RigidBodyComponent:setBoxShape(...)
RigidBodyComponent:setSphereShape(...)
RigidBodyComponent:setCapsuleShape(...)
RigidBodyComponent:setMass(...)
RigidBodyComponent:setFriction(...)
RigidBodyComponent:setRestitution(...)
RigidBodyComponent:setPlanarMode(...)
RigidBodyComponent:getPlanarMode(...)
RigidBodyComponent:setLayer(...)
RigidBodyComponent:getLayer(...)
RigidBodyComponent:setIsSensor(...)
RigidBodyComponent:isSensor(...)
RigidBodyComponent:setLinearVelocity(...)
RigidBodyComponent:getLinearVelocity(...)
RigidBodyComponent:setAngularVelocity(...)
RigidBodyComponent:getAngularVelocity(...)
RigidBodyComponent:applyImpulse(...)
RigidBodyComponent:applyForce(...)
RigidBodyComponent:teleport(...)
RigidBodyComponent:hasBody(...)
RigidBodyComponent:getBodyId(...)
RigidBodyComponent.bodyType
RigidBodyComponent.shapeType
RigidBodyComponent.halfExtents
RigidBodyComponent.radius
RigidBodyComponent.halfHeight
RigidBodyComponent.mass
RigidBodyComponent.friction
RigidBodyComponent.restitution
RigidBodyComponent.planar
RigidBodyComponent.layer
RigidBodyComponent.isSensor
RigidBodyComponent.linear_velocity
RigidBodyComponent.angular_velocity
RigidBodyComponent.has_body

## ScriptComponent
ScriptComponent:setScriptFile(...)
ScriptComponent:getScriptFile(...)
ScriptComponent:setScriptEnabled(...)
ScriptComponent:isScriptEnabled(...)
ScriptComponent:hasScriptError(...)
ScriptComponent:getScriptError(...)
ScriptComponent:isScriptStarted(...)
ScriptComponent:reloadScript(...)
ScriptComponent:hotReload(...)
ScriptComponent:hasReloadError(...)
ScriptComponent:getLastReloadError(...)
ScriptComponent.script
ScriptComponent.enabled
ScriptComponent.started
ScriptComponent.error

## Engine
Engine(...)
Engine.getSingleton(...)
Engine:setup(...)
Engine:getTopLevelWindowHandle(...)
Engine:renderOneFrame(...)
Engine:enableWireframeMode(...)
Engine:disableWireframeMode(...)
Engine:getCamera(...)
Engine:getRenderSystem(...)
Engine:getWindowWidth(...)
Engine:getWindowHeight(...)
Engine:getSafeAreaInsets(...)
Engine:getContentScale(...)
Engine:setCameraOrthographic(...)
Engine:setCameraOrthographicFit(...)
Engine:setCameraPerspective(...)
Engine:setWindowBackgroundColour(...)
Engine:hasUISystem(...)

## FrameEventData
FrameEventData.timeSinceLastEvent
FrameEventData.timeSinceLastFrame

## KeyEventData
KeyEventData.text
KeyEventData.key
KeyEventData.KeyCode = { KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J, KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T, KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_0, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_LEFT, KC_RIGHT, KC_UP, KC_DOWN, KC_SPACE, KC_ESCAPE, KC_RETURN, KC_TAB, KC_LSHIFT, KC_RSHIFT, KC_LCONTROL, KC_RCONTROL }

## MouseEventData
MouseEventData.button

## InputManager
InputManager(...)
InputManager.getSingleton(...)
InputManager:enable(...)
InputManager:disable(...)
InputManager:isKeyDown(...)
InputManager:getTilt(...)
InputManager:isTiltSensorAvailable(...)
InputManager:setTiltAngle(...)
InputManager:getTiltAngle(...)
InputManager:calibrateTilt(...)
InputManager:clearTiltCalibration(...)
InputManager:getTiltCalibration(...)

## PhysicsWorld
PhysicsWorld.getSingleton(...)
PhysicsWorld:update(...)
PhysicsWorld:setPaused(...)
PhysicsWorld:isPaused(...)
PhysicsWorld:setGravity(...)
PhysicsWorld:getGravity(...)
PhysicsWorld:setBodyTransform(...)
PhysicsWorld:setBodyPlanarMode(...)
PhysicsWorld:setLinearVelocity(...)
PhysicsWorld:getLinearVelocity(...)
PhysicsWorld:setAngularVelocity(...)
PhysicsWorld:getAngularVelocity(...)
PhysicsWorld:applyImpulse(...)
PhysicsWorld:applyForce(...)
PhysicsWorld:isBodyActive(...)
PhysicsWorld:castRayHit(...)

## GuiWidget
GuiWidget:setPosition(...)
GuiWidget:setSize(...)
GuiWidget:getSize(...)
GuiWidget:getPosition(...)
GuiWidget:centerHorizontal(...)
GuiWidget:setEnabled(...)
GuiWidget:isEnabled(...)
GuiWidget:getLayer(...)
GuiWidget:setParent(...)
GuiWidget:setAnchors(...)
GuiWidget:setAnchorPreset(...)
GuiWidget:setPivot(...)
GuiWidget:setOffsets(...)
GuiWidget:setAnchoredPosition(...)
GuiWidget:setSizeDelta(...)
GuiWidget:setUseSafeArea(...)
GuiWidget:setLayoutGroup(...)
GuiWidget:setGroupPadding(...)
GuiWidget:setGroupSpacing(...)
GuiWidget:setChildAlignment(...)
GuiWidget:setChildForceExpand(...)
GuiWidget:setGridCellSize(...)
GuiWidget:setGridConstraint(...)
GuiWidget:setContentSizeFit(...)
GuiWidget:setRenderScale(...)
GuiWidget:getRenderScaleX(...)
GuiWidget:getRenderScaleY(...)
GuiWidget:setRenderRotation(...)
GuiWidget:getRenderRotation(...)
GuiWidget:setGroupAlpha(...)
GuiWidget:getGroupAlpha(...)
GuiWidget:getEffectiveAlpha(...)
GuiWidget:setAlphaBlocksInput(...)
GuiWidget:setTransition(...)

## GuiTextEntry
GuiTextEntry:setPosition(...)
GuiTextEntry:setSize(...)
GuiTextEntry:getSize(...)
GuiTextEntry:getPosition(...)
GuiTextEntry:getText(...)
GuiTextEntry:setText(...)
GuiTextEntry:setPlaceholder(...)
GuiTextEntry:setMaxLength(...)
GuiTextEntry:getMaxLength(...)
GuiTextEntry:isFocused(...)
GuiTextEntry:wasSubmitted(...)

## GuiSelectMenu
GuiSelectMenu:setItems(...)
GuiSelectMenu:setItemsString(...)
GuiSelectMenu:getSelectedItemIndex(...)
GuiSelectMenu:getSelectedItem(...)
GuiSelectMenu:selectItemIndex(...)
GuiSelectMenu:selectItem(...)
GuiSelectMenu:getCaption(...)
GuiSelectMenu:setCaption(...)

## GuiProgressBar
GuiProgressBar:setProgress(...)
GuiProgressBar:addProgress(...)
GuiProgressBar:getProgress(...)
GuiProgressBar:getCaption(...)
GuiProgressBar:setCaption(...)

## GuiManager
GuiManager(...)
GuiManager.getSingleton(...)
GuiManager:getFactory(...)
GuiManager:enableInputEvents(...)
GuiManager:disableInputEvents(...)
GuiManager:widgetExists(...)
GuiManager:findWidget(...)
GuiManager:destroyWidget(...)
GuiManager:destroyAllWidgets(...)
GuiManager:hideAllViews(...)
GuiManager:showAllViews(...)
GuiManager:isPointOverWidget(...)
GuiManager:setDesignResolution(...)
GuiManager:setLayoutMatchMode(...)
GuiManager:setRootSpace(...)
GuiManager:getLayoutScale(...)
GuiManager:showModal(...)
GuiManager:getModalContentZ(...)
GuiManager:registerModalWidget(...)
GuiManager:dismissModal(...)
GuiManager:dismissTopModal(...)
GuiManager:dismissAllModals(...)
GuiManager:isModalActive(...)
GuiManager:getTopModalId(...)
GuiManager:getModalCount(...)
GuiManager:showConfirm(...)
GuiManager:showAlert(...)
GuiManager:getDialogResult(...)
GuiManager:cancelCurrentInputUpdate(...)
GuiManager:createToggleGroup(...)
GuiManager:getToggleGroup(...)
GuiManager:destroyToggleGroup(...)
GuiManager:showToast(...)
GuiManager:isToastVisible(...)
GuiManager:getLastBatchCount(...)
GuiManager:getRebuildCount(...)
GuiManager:getGeometryRebuildCount(...)
GuiManager:getScratchCapacity(...)

## GuiLabel
GuiLabel:setText(...)
GuiLabel:setAlignment(...)
GuiLabel:setAlpha(...)
GuiLabel.LabelAlignment = { LA_TOPLEFT, LA_TOP, LA_TOPRIGHT, LA_LEFT, LA_CENTER, LA_RIGHT, LA_BOTTOMLEFT, LA_BOTTOM, LA_BOTTOMRIGHT }

## GuiDecorWidget
GuiDecorWidget:setSprite(...)
GuiDecorWidget:setColour(...)
GuiDecorWidget:setAlpha(...)
GuiDecorWidget:setNineSlice(...)
GuiDecorWidget:setTiled(...)

## GuiCheckBox
GuiCheckBox:isChecked(...)
GuiCheckBox:setChecked(...)
GuiCheckBox:toggle(...)

## GuiButton
GuiButton:getCaption(...)
GuiButton:setCaption(...)
GuiButton:getState(...)
GuiButton:wasClicked(...)
GuiButton:setNineSlice(...)
GuiButton:setTiled(...)
GuiButton:setPressFeedback(...)
GuiButton.ButtonState = { BS_DISABLED, BS_UP, BS_OVER, BS_DOWN }

## GuiDropDown
GuiDropDown:setItems(...)
GuiDropDown:setItemsString(...)
GuiDropDown:getSelectedIndex(...)
GuiDropDown:getSelectedItem(...)
GuiDropDown:selectIndex(...)
GuiDropDown:isMenuOpen(...)

## GuiScrollView
GuiScrollView:setScroll(...)
GuiScrollView:getScroll(...)
GuiScrollView:getMaxScroll(...)

## DragEventData
DragEventData.button
DragEventData.state
DragEventData.position
```
<!-- /GENERATED:lua-api-types -->
