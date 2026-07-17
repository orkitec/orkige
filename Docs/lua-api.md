# Lua API reference

The scripting surface a game reaches from a script component
(`projects/*/scripts/*.component.lua`). Game behavior lives in these scripts;
`projects/jumper-lua/scripts/player.lua` and
`projects/roller/scripts/ball.component.lua` are the reference reads.

A script is a **component kind** iff its file name ends in `.component.lua` (see
[Script components](#script-components) below): `player.component.lua` attaches
as the component `player`. Plain `.lua` files are libraries, never attachable.

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
world.getCamera(id) -> CameraComponent  -- the object's CameraComponent (nil when absent); drives smooth follow
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
music.crossFade(id, file, seconds)  -- swap `id` in and fade every other track out over `seconds` (equal-power)

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

## screens
screens.define(name, ouiPath)  -- register a .oui-backed screen
screens.defineBuilder(name, builder)  -- register a Lua-built screen
screens.push(name)  -- push a screen onto the stack
screens.replace(name)  -- swap the top screen
screens.pop() -> string  -- pop the top screen; returns its name
screens.current() -> string  -- the top screen's name
screens.depth() -> int  -- screens on the stack
screens.clear()  -- pop every screen
screens.setBackHandler(handler)  -- own the back gesture (handler decides)

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

## events
events.subscribe(name, fn) -> EventSubscription  -- subscribe fn(payload) to an event (script phase, subscription order)
events.emit(name [, payload])  -- queue an event; payload a table of string/number/bool (one nesting level)

## locale
locale.set(tag) -> bool  -- switch the active language (true if the tag is loaded); re-push screens after
locale.get() -> string  -- the active language code
locale.list() -> table  -- the loaded language codes, sorted (source included)
locale.getSource() -> string  -- the source (authored) language code

## benchmark
benchmark.begin(name)  -- start/restart/rename the current benchmark scene aggregation (no-op unless armed)
benchmark.endScene()  -- close the current benchmark scene and write its record (no-op unless armed)
benchmark.isArmed() -> bool  -- is a benchmark run being recorded

## timer
timer.after(seconds, fn) -> TimerHandle  -- run fn() ONCE after a delay; sandbox-scoped (auto-cancels on retire)
timer.every(seconds, fn) -> TimerHandle  -- run fn() every `seconds`; sandbox-scoped (auto-cancels on retire)
timer.cancel(handle) -> bool  -- stop a scheduled timer (also handle:cancel())

## game
game.setState(name)  -- set the game state; fires game.stateChanged {old,new} on the event bus
game.getState() -> string  -- the current game state name ("" when unset)

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

## EventSubscription
EventSubscription:cancel() -> bool  -- drop the subscription (true if it was live)
EventSubscription:isActive() -> bool  -- is the subscription still live

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

### Script components

A behavior script is an attachable **component kind** when its file name ends in
`.component.lua`. Its kind name is the base name:
`scripts/player.component.lua` → the component `player`. Plain `.lua` files are
libraries/helpers and never attach.

- **Several scripts, one object.** Different script kinds attach to the same
  GameObject, each in its own sandbox with its own lifecycle. The *same* kind
  twice on one object is unsupported. The editor's Add Component menu lists the
  project's script kinds; the runtime hierarchy/inspector and MCP
  (`add_component("player")` / `get_component` / `set_component` /
  `list_addable_components`) address them by kind name.
- **Declared properties.** A top-level `properties` table auto-exposes
  designer-tunable fields: each shows in the Inspector, serializes per-instance
  overrides into the scene, is injected onto `self.<name>` before `init`, and is
  read/written live over the debug protocol / MCP by name. The value is read
  WITHOUT running behavior (a broken `properties` table is an honest error, never
  executes `init`). Types: `number`/`bool`/`string`/`vec3`/`color`/`asset`/`object`;
  `min`/`max` add a slider range, `kind` an asset/object hint.

```lua
-- scripts/enemy.component.lua  ->  the "enemy" component kind
properties = {
    speed  = { type = "number", default = 3.0, min = 0, max = 20 },
    patrol = { type = "bool",   default = true },
    tint   = { type = "color",  default = {1, 1, 1, 1} },
    icon   = { type = "asset",  kind = "texture" },
}
function init(self)     -- self.speed / self.tint ... are the (overridable) values
    self.dir = self.speed
end
function update(self, dt) end
```

The low-level `ScriptComponent` (an explicit `script` file path property) still
exists for any script bound by path rather than by kind name.

- **Event delivery reaches EVERY component.** The lifecycle hooks fire on *each*
  script component of an object independently: `update` (the tick),
  `onContactBegin`/`onContactEnd` (the physics contact drain), `onAppPause`/
  `onAppResume` (the app-lifecycle broadcast). Two scripts on one object each get
  their own call in their own sandbox.
- **The `events` message bus (multi-consumer).** For signals SEVERAL scripts
  should hear — or that cross objects/systems — use the bus:
  `events.subscribe(name, fn) -> handle` and `events.emit(name [, payload])`.
  A handler runs as `fn(payload)`, `payload` a plain table. This is the Lua face
  of the engine's ONE event bus (`core_event/GlobalEventManager`), so a C++
  system and a script share it — a C++ listener bound to an event name receives a
  script-emitted event of that name, and vice versa. It is the multi-consumer
  complement to `shared` / tags / the object graph, not a replacement — reach a
  *specific* object by id, broadcast a *signal* on the bus.

  **Delivery is deterministic.** An `emit` QUEUES onto the engine bus; it never
  calls a handler inline. The player loop drains the queue ONCE per frame in the
  script phase (right after the component updates, before tweens/physics),
  delivering each event to its subscribers IN SUBSCRIPTION ORDER. The bus's
  double-buffered queue is the cascade-safety: an `emit` from INSIDE a handler
  lands in the next buffer and is delivered at the NEXT frame's drain — a handler
  can never recurse into the same drain. An `emit` that happens later in the tick
  — the mirrored `physics.contactBegin`/`contactEnd`, emitted at the contact
  drain which runs after the script phase — is likewise delivered next frame. gui
  input is pumped before the script phase, so a widget event is seen the SAME
  frame it was clicked.

  **Subscriptions are sandbox-scoped.** A subscription belongs to the script
  component that made it; removing that component, tearing down its scene, or
  hot-reloading it CANCELS its subscriptions. So the idiom is **subscribe in
  `init`** — a hot reload re-runs `init` and re-subscribes naturally.
  `handle:cancel()` drops one early.

  **Payload bounds.** A payload is a plain table of `string`/`number`/`bool`
  values, one nesting level deep (a field may itself be such a table). Anything
  else — a function, userdata, a deeper table — is an honest ERROR raised at the
  `emit` call site. `events.emit("name")` with no payload is an empty event.

  **Engine mirrors.** Engine producers emit the same named events so scripts can
  react without polling: `gui.clicked {id}`, `gui.toggled {id, state}`,
  `gui.submitted {id, text}`, `gui.valueChanged {id, value}`,
  `gui.dialogResult {id, result}`, `gui.screenPushed`/`gui.screenPopped {name}`,
  `gui.toastShown {text}`, `physics.contactBegin`/`physics.contactEnd {a, b}`
  (object ids), `animation.ended {clip, object}` (a `once` clip on a
  vector-animation rig finished; `object` is the rig's owner id, so several
  rigs sharing clip names stay distinguishable), `app.pause`/`app.resume`
  (no payload), and `ui.reloaded {file}`
  (a declarative `.oui` screen was hot-reloaded during Play — its widgets were
  destroyed and rebuilt, so any handle a script holds to that screen is stale;
  subscribe to re-acquire them via `gui:findWidget(id)`).

  ```lua
  function init(self)
      self.sub = events.subscribe("gui.clicked", function(e)
          if e.id == "startBtn" then screens.push("game") end
      end)
      events.subscribe("player.died", function(e) self.lives = self.lives - 1 end)
  end
  -- somewhere else: events.emit("player.died", { by = "spikes" })
  ```
- **GUI events are ALSO single-consumer-pollable.** The gui poll idiom is
  latch-and-clear: `widget:wasClicked()` / `wasSubmitted()` /
  `group:pollChanged()` / `gui:getDialogResult()` return the pending event ONCE
  and clear it. So if two script components poll the SAME widget in one frame, the
  FIRST to poll consumes it and the second sees nothing. **Convention for
  polling: exactly one script component owns a given widget's events.** When
  SEVERAL scripts must react to the same widget, use the `gui.*` bus events above
  (multi-consumer) instead of racing pollers — the two channels coexist and the
  bus never consumes the poll latch.

### Script component performance

The multi-sandbox model is cheap: measured ~**0.25 µs per component per frame**
(500 components — a quarter carrying declared properties and doing per-frame
work — tick in ~0.12 ms/frame; the `[perf]` unit test logs the number). What
scales and what to watch:

- **The floor** (an empty `update`) is a sol2 `protected_function` call plus the
  component gate — sub-microsecond. Attaching many script components is fine.
- **Allocation**: the per-frame `update(self, dt)` path packs its arguments for
  the sol2 call; it does not allocate engine-side per component per frame (no
  per-tick container growth). Declared properties add NO per-frame cost — they
  are injected onto `self` once at init, then read as plain Lua fields.
- **GC**: the Lua state runs the standard incremental collector; a script that
  allocates each frame (building tables/strings in `update`) feeds it — prefer
  reusing `self` fields over per-frame table churn in hot scripts. A stress tick
  showed no GC spikes in the frame log at this scale.
- **Avoid**: heavy per-frame work in `update` on hundreds of instances (do it
  event-driven or on a subset), and per-frame string/table allocation in hot
  scripts.

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
-- TYPED finders return the leaf so its own methods work; nil when absent OR
-- a different type. findWidget returns the base GuiWidget (layout setters only).
local resume = gui:findButton("resumeBtn")
-- each frame: if resume and resume:wasClicked() then ... end
```

## Widget & object handles (weak, per-call-locked)

Accessors that hand a script a reference to an engine-owned object return a
**weak handle**, not an owning reference. This is the whole object surface:

- every `GuiManager` finder (`findWidget`, `findLabel`, …), `gui:getFactory()`,
  `getToggleGroup`, every factory `create*`;
- every `world.*` lookup — `world.get(id)`, the per-component `world.getTransform`
  / `getRigidBody` / `getModel` / `getSprite` / `getParticles` / `getScript` /
  `getSound` / `getCamera` / `getLevel`, and `world.findByTag` (an array of
  handles);
- the `self` fields a `ScriptComponent` injects — `self.gameObject`, `self.script`
  and each sibling component (`self.transform`, `self.rigidbody`, `self.sprite`,
  `self.shape`, `self.anim`, …);
- the OTHER object delivered to `onContactBegin` / `onContactEnd`;
- `GameObject:getParent()`;
- `widget:getLayer()` — a screen-scoped `UiLayer` handle. A layer dies with its
  view, so an `.oui` hot-reload or a preview teardown makes a cached layer handle
  raise `layer handle is dead` (keyed on the view's liveness) rather than dangle.

Lua never owns the object; the engine does (the `GuiManager` owns its widgets, the
world owns its objects, a `GameObject` owns its components). Each method call
**locks** the handle for the duration of the call. If the object is already gone,
the call raises a normal, `pcall`-catchable Lua error at the line that made it —
e.g. `widget handle is dead (GuiLabel 'coinLabel')`, `handle is dead (GameObject
'Player')`, or `component handle is dead (TransformComponent 'Player')` (a
component names its OWNING object) — instead of a crash, a zombie that keeps the
object alive, or a silent no-op. A mistake is visible where it happened, and the
app keeps running. `self.id` stays a plain string, not a handle.

There is **one** widget handle type (`WidgetHandle`). It carries the whole widget
method surface and resolves each method against the object's **live type**, so
there are two complementary idioms:

- **Filter at acquisition** — the typed finders (`findLabel`, `findButton`, …)
  return `nil` for a wrong-kind or absent id, so `gui:findLabel("x")` hands back a
  handle only when `x` really is a label.
- **Gate at the call** — any handle exposes any widget method; the call succeeds
  when the live object actually is that kind and otherwise raises distinctly, e.g.
  `widget 'x' is a GuiButton, not a GuiLabel` (for a name several kinds share, the
  error names the accepted kinds: `… ; setText needs GuiLabel or GuiTextEntry`).
  So `gui:findWidget("x"):setText(...)` works when `x` is a label and errors
  honestly when it is not.

You never keep a handle alive to keep the widget alive — destroy it through the
manager (or let the manager tear down), and any handle to it simply raises on the
next touch.

The **GameObject and component** surface follows the same rule. `world.get(id)`,
`self.gameObject`, `GameObject:getParent()` and the contact-event OTHER object are
all one `GameObjectHandle` type carrying the read / hierarchy / tag / active
surface. Each component accessor (`world.getTransform`, `self.transform`, …) is
its own per-type handle carrying that component's methods — a `TransformComponent`
handle exposes `setPosition` / `teleport` / … plus the mobility flag
(`getStaticFlag`/`setStaticFlag` — declare an object's world transform immutable
so the renderer takes its immobile fast path; moving a static object still works
but warns once and pays a repair cost, and the flag refuses a dynamic parent —
`Docs/performance.md`), a `RigidBodyComponent` handle
exposes `applyImpulse` / `setLinearVelocity` / …, and so on. There is no wrong-type
gate for components (each type has its own accessor), so a component handle only
ever raises the dead-handle error, naming the object it belonged to. A cached
handle — `self.gameObject` stored across frames, a contact OTHER stashed for later
— is always safe: it locks per call and raises honestly once its object is
destroyed, never dereferencing freed engine state.

## Canonical snippets

**Screen from a `.oui` + wire it.**

```lua
function init(self)
    factory:loadLayout("screens/hud.oui")
    self.coins = gui:findLabel("coinLabel")   -- typed: label:setText works
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

**Drive a vector-animation rig (`self.anim`).**

```lua
function init(self)
    self.anim:play("idle")                        -- a looping clip
    events.subscribe("animation.ended", function(e)
        -- a `once` clip finished; e.object filters to THIS rig's owner
        if e.object == self.id and e.clip == "hop" then
            self.anim:crossFade("idle", 0.3)      -- blend back over 0.3s
        end
    end)
end
function onContactBegin(self, other)
    self.anim:crossFade("hop", 0.2)               -- one-shot, then the event
end
-- clip discovery at runtime: self.anim:getClipNames() -> "idle,hop";
-- an unknown name refuses (returns false) and warns once in the log,
-- naming the available clips. Scrub: self.anim:scrub(0.5) (seconds).
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

**Material accents — a hit flash on a mesh (`self.model`).**

```lua
-- RUNTIME-ONLY, per instance: the .omat asset stays the authored truth (a
-- scene save never records accents - the properties are transient), and the
-- OTHER instances of the same material keep their look. Tint multiplies the
-- albedo (white = off), the emissive boost ADDS glow (black = off) - a tint
-- can only darken, the boost is what makes a flash BRIGHT.
function onContactBegin(self, other)
    self.model:setTint(1.0, 0.3, 0.25)             -- red-shift the surface
    self.model:setEmissiveBoost(0.3, 0.05, 0.05)   -- and make it glow
    tween.to(0, 1, 0.15, "quadOut", nil, function()
        self.model:setTint(1, 1, 1)                -- restore EXACTLY
        self.model:setEmissiveBoost(0, 0, 0)
    end)
end
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

## Editor scripts (Tools menu)

A script whose file name ends in **`.editor.lua`** is not game behavior — it is an
**editor TOOL**: a one-shot command listed in the editor's **Tools** menu and run
on demand (also over MCP with `run_editor_script`). It is a *different surface*
from the `.component.lua` scripts above: it runs in the EDITOR, edits the open
scene, and never ticks. The two never mix — a `.component.lua` attaches to a
GameObject and sees `self`/`world`/`events`; a `.editor.lua` sees only the
`editor` table.

- **Discovery + the header.** Any `scripts/*.editor.lua` in the open project is
  discovered (rescanned on project open and on any `scripts/` write). The menu
  label is a title-cased file name unless the FIRST line is a
  `-- tool: <label>` comment, which overrides it.
- **One-shot, in a fresh sandbox.** A menu click (or `run_editor_script`) runs the
  file's top-level code ONCE in a fresh Lua sandbox. There is no `update`, no
  timer, no event delivery — the editor never ticks. The game-runtime tables
  (`world`/`events`/`tween`/…) are **absent** in an editor tool; referencing one
  is an honest nil error.
- **The `editor` table** routes through the SAME verb handler an MCP agent drives,
  so each function mirrors an MCP tool 1:1. Every function takes ONE table of
  named arguments and returns a result table (fields as strings, arrays as list
  tables — `tonumber` what you need). A refused verb RAISES a Lua error at the
  call site (guard with a read if you expect it). Available verbs:
  - reads: `list_hierarchy`, `get_object`, `get_component`,
    `list_addable_components`, `list_assets`, `list_paintable_assets`,
    `list_project_files`, `read_project_file`, `get_state`;
  - object CRUD: `create_object`, `delete_object`, `duplicate_object`,
    `rename_object`, `reparent_object`, `set_active`, `select`;
  - component CRUD (script components included): `add_component`,
    `remove_component`, `set_component`;
  - 2D grid painting: `paint_asset`, `erase_cell`;
  - scene + files + prefabs + levels: `save_scene`, `open_scene`, `new_scene`,
    `write_project_file`, `import_asset`, `create_prefab`, `instantiate_prefab`,
    `add_scene_to_levels`;
  - plus `editor.log(text)` to write a line to the editor Console.
- **One undo step.** The WHOLE run folds into a SINGLE undo step, so one Cmd/Ctrl+Z
  reverts everything the tool did. A tool that ERRORS is rolled back entirely —
  it leaves **no partial edits** — and the error (with its `file:line`) lands in
  the Console.
- **Noscript.** In `ORKIGE_SCRIPTING=OFF` builds the Tools menu shows a disabled
  note and running is an honest no-op; the project still loads.

**A tool (frame the level with wall tiles):**

```lua
-- tool: Paint Wall Border
-- reads the scene's LevelComponent, then paints wall tiles around its edge -
-- all ONE undo step. (projects/roller/scripts/border_walls.editor.lua)
local function findLevel()
    for _, id in ipairs(editor.list_hierarchy().ids or {}) do
        for _, c in ipairs(editor.get_object{ id = id }.components or {}) do
            if c == "LevelComponent" then return id end
        end
    end
end
local id = findLevel()
if not id then editor.log("no level here"); return end
local lvl = editor.get_component{ id = id, component = "LevelComponent" }
local cols, rows = math.floor(tonumber(lvl.cols)), math.floor(tonumber(lvl.rows))
for c = 0, cols - 1 do
    for r = 0, rows - 1 do
        if c == 0 or c == cols-1 or r == 0 or r == rows-1 then
            editor.paint_asset{ asset = "assets/wall.png", cell = { col = c, row = r } }
        end
    end
end
```

An AGENT authors such a tool once with `write_project_file`; a HUMAN then runs it
from the Tools menu forever after (or the agent triggers it with
`run_editor_script`). See `Docs/mcp-workflows.md` for that worked loop.

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

## TimerHandle
TimerHandle:cancel()  -- stop the timer now
TimerHandle:isActive() -> bool  -- is the timer still scheduled

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
TileComponent:getSourceAssetId(...)
TileComponent:setSourceAssetId(...)
TileComponent.openEdges
TileComponent.sourceAssetId

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
TransformComponent.static

## ModelComponent
ModelComponent:loadModel(...)
ModelComponent:getCurrentModelFileName(...)
ModelComponent:setMaterialReference(...)
ModelComponent:getMaterialFileName(...)
ModelComponent:setTint(...)
ModelComponent:setEmissiveBoost(...)
ModelComponent.mesh
ModelComponent.material
ModelComponent.castShadows
ModelComponent.receiveShadows
ModelComponent.tint
ModelComponent.emissiveBoost

## WaterComponent
WaterComponent:getSizeX(...)
WaterComponent:setSizeX(...)
WaterComponent:getSizeZ(...)
WaterComponent:setSizeZ(...)
WaterComponent:getOpacity(...)
WaterComponent:setOpacity(...)
WaterComponent:getWaveScale(...)
WaterComponent:setWaveScale(...)
WaterComponent:getWaveSpeed(...)
WaterComponent:setWaveSpeed(...)
WaterComponent:getFresnelPower(...)
WaterComponent:setFresnelPower(...)
WaterComponent:getNormalTexture(...)
WaterComponent.sizeX
WaterComponent.sizeZ
WaterComponent.deepColour
WaterComponent.shallowColour
WaterComponent.opacity
WaterComponent.waveScale
WaterComponent.waveSpeed
WaterComponent.fresnelPower
WaterComponent.normalTexture
WaterComponent.receiveShadows

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

## VectorAnimationComponent
VectorAnimationComponent:loadAnimation(...)
VectorAnimationComponent:removeAnimation(...)
VectorAnimationComponent:getAnimationName(...)
VectorAnimationComponent:hasAnimation(...)
VectorAnimationComponent:getTriangleCount(...)
VectorAnimationComponent:getVertexCount(...)
VectorAnimationComponent:getPoseSignature(...)
VectorAnimationComponent:play(...)
VectorAnimationComponent:stop(...)
VectorAnimationComponent:setClip(...)
VectorAnimationComponent:crossFade(...)
VectorAnimationComponent:scrub(...)
VectorAnimationComponent:isPlaying(...)
VectorAnimationComponent:currentClip(...)
VectorAnimationComponent:getClipCount(...)
VectorAnimationComponent:getClipNames(...)
VectorAnimationComponent:currentFrame(...)
VectorAnimationComponent:isAtEnd(...)
VectorAnimationComponent:setSpeed(...)
VectorAnimationComponent:getSpeed(...)
VectorAnimationComponent.clip
VectorAnimationComponent.speed
VectorAnimationComponent.playing
VectorAnimationComponent.transitionTime
VectorAnimationComponent.tint
VectorAnimationComponent.scale
VectorAnimationComponent.edgeSoftness
VectorAnimationComponent.zOrder
VectorAnimationComponent.visible
VectorAnimationComponent.animation

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
ParticleComponent.space3D
ParticleComponent.worldSpace
ParticleComponent.emissionVolume
ParticleComponent.volumeExtents
ParticleComponent.gravity3D
ParticleComponent.wind
ParticleComponent.direction3D
ParticleComponent.stretch
ParticleComponent.flutterAmplitude
ParticleComponent.flutterFrequency
ParticleComponent.additive
ParticleComponent.maxParticles

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
AnimationComponent:crossFadeTo(...)
AnimationComponent:isCrossFading(...)
AnimationComponent:getCrossFadeProgress(...)

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
CameraComponent:follow(...)
CameraComponent:stopFollow(...)
CameraComponent:setFollowTarget(...)
CameraComponent:getFollowTarget(...)
CameraComponent:setFollowDamping(...)
CameraComponent:getFollowDamping(...)
CameraComponent:setFollowOffset(...)
CameraComponent:getFollowOffset(...)
CameraComponent.projectionMode
CameraComponent.orthoSize
CameraComponent.fitMode
CameraComponent.designWidth
CameraComponent.designHeight
CameraComponent.followTarget
CameraComponent.followDamping
CameraComponent.followOffset
CameraComponent.ProjectionMode = { PM_PERSPECTIVE, PM_ORTHOGRAPHIC }
CameraComponent.FitMode = { FM_HEIGHT, FM_WIDTH, FM_EXPAND }

## LightComponent
LightComponent:hasLight(...)
LightComponent:setType(...)
LightComponent:getType(...)
LightComponent:setColour(...)
LightComponent:setIntensity(...)
LightComponent:getIntensity(...)
LightComponent:setRange(...)
LightComponent:getRange(...)
LightComponent:setInnerAngle(...)
LightComponent:getInnerAngle(...)
LightComponent:setOuterAngle(...)
LightComponent:getOuterAngle(...)
LightComponent:setCastsShadows(...)
LightComponent:getCastsShadows(...)
LightComponent.type
LightComponent.colour
LightComponent.intensity
LightComponent.range
LightComponent.innerAngle
LightComponent.outerAngle
LightComponent.castsShadows

## SoundComponent
SoundComponent:addSound(...)
SoundComponent:play(...)
SoundComponent:stop(...)
SoundComponent:stopAllSounds(...)
SoundComponent:setVolume(...)
SoundComponent:getVolume(...)
SoundComponent:setGroup(...)
SoundComponent:getGroup(...)
SoundComponent:setPitchVariation(...)
SoundComponent:setVolumeVariation(...)

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
Engine:setAtmosphere(enabled, skyRed, skyGreen, skyBlue, density, fogDensity) -> nil  -- set the scene sky/fog atmosphere; sun = the first directional light (next renders a sky dome, classic the flat sky + fog subset)
Engine:setAtmosphereBlend(...)
Engine:hasUISystem(...)
Engine:supports(capabilityName) -> bool  -- does the active render backend support a capability? (e.g. "skyDome"/"dynamicShadows"/"hemisphereAmbient") - degrade a script's look per flavor; an unknown name is false

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
GuiManager:findLabel(...)
GuiManager:findButton(...)
GuiManager:findCheckBox(...)
GuiManager:findSlider(...)
GuiManager:findSelectMenu(...)
GuiManager:findProgressBar(...)
GuiManager:findDecor(...)
GuiManager:findTextEntry(...)
GuiManager:findScrollView(...)
GuiManager:destroyWidget(...)
GuiManager:destroyAllWidgets(...)
GuiManager:hideAllViews(...)
GuiManager:showAllViews(...)
GuiManager:isPointOverWidget(...)
GuiManager:setDesignResolution(...)
GuiManager:setLayoutMatchMode(...)
GuiManager:setRootSpace(...)
GuiManager:getLayoutScale(...)
GuiManager:getUiScale(...)
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
