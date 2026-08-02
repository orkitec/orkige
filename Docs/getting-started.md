# Getting started: your first game

This walkthrough assumes you have already built Orkige successfully (see the
[README](../README.md)). It takes you from an empty editor to a playable sprite
with physics and a Lua behavior, then out to a distributable build. It is
deliberately small — a real game is the same steps, repeated.

## 1. Launch the editor

Build and run the release editor — the debug build is roughly 19× slower and is
meant for engine development, not for working in the tool:

```sh
cmake --build --preset macos-release --target orkige_editor
open build/macos-release/tools/editor/Orkige.app
```

You land in a docked layout: the **Scene** viewport in the center, **Hierarchy**
and **Inspector** on the sides, and **Console** / **Stats** tabbed at the bottom.
The **View** menu toggles any panel and resets the layout.

## 2. Create a project

**File → New Project…** and pick a folder. The folder name becomes the project
name, and Orkige lays down the skeleton and opens it:

```
myproject/
  project.orkproj   the manifest (main scene, settings, config-asset references)
  assets/           textures, meshes, prefabs — everything with a stable asset id
  scenes/           .oscene files; a fresh empty main scene is created and saved
  scripts/          Lua behavior scripts
```

The new project opens on an empty, instantly-playable main scene.

## 3. Import a texture and make a sprite

Find the **Assets** panel (the asset browser). Drag a `.png` from Finder onto it —
the file is copied into `assets/` and gets a stable id via an `.orkmeta` sidecar,
so later renames won't break references.

Now drag that texture from the Assets panel **into the Scene viewport**. Orkige
creates a GameObject with a `SpriteComponent` (a textured, alpha-blended quad in
the XY plane) at the origin. Select it in the Hierarchy to see its components in
the Inspector.

## 4. Add physics

Sprites live in the XY plane, so switch the viewport to 2D: click the **2D/3D**
button in the toolbar (it reads "2D" when active) — the camera drops to a
top-down orthographic view locked to the XY plane, and the move/rotate/scale
gizmos constrain to that plane.

With the sprite selected, use **Add Component** at the bottom of the Inspector and
pick **RigidBodyComponent**. Enable its **planar** (2D) mode — planar mode locks
translation to the X/Y plane and rotation to the Z axis, so a body can only move
and spin the way a 2D game expects. The `TransformComponent` a rigid body needs is
added automatically.

The **shapeType** picks the collision geometry: `box` / `sphere` / `capsule`, or
**`shape`** — a collider derived from a flat-colour vector shape (`.oshape`) so an
organic outline collides without hand-approximating boxes. A `shape` body uses its
sibling `VectorShapeComponent`'s shape by default, or an explicit **shapeAsset**
reference. A **static** or **kinematic** `shape` body keeps the true CONCAVE outline
(an L or U cup collides on its inner edges); a **dynamic** one uses the outline's
**convex hull** (dynamic rigid bodies must be convex — a concave dynamic outline
degrades to its hull, logged once). The collider is planar-extruded to a thin prism
(its depth reuses the box `halfExtents.z`), built from the shape's authored units;
transform scale is not applied (matching the box/sphere/capsule shapes). A soft-body
`VectorShapeComponent`'s collider is built from the REST shape and stays rigid.

## 5. Attach a Lua script

In the Assets panel, right-click and choose **New Script** to create a `.lua` file
under `scripts/`. Open it in your editor and paste a minimal behavior:

```lua
-- spin.lua - a minimal ScriptComponent: move the sprite with the "move" action.

local actions   -- the named-input action map (built-in "move"/"jump" actions)
local MOVE_SPEED = 4.0

-- init(self) runs once after the script loads. `self` carries the owner
-- (self.id, self.gameObject) and its sibling components (self.transform,
-- self.rigidbody, ... nil when not attached). Cross-script state lives in the
-- global `shared` table; other objects are reached through the global `world`.
function init(self)
	actions = InputActions.getSingleton()
	print("spin.lua: attached to '" .. self.id .. "'")
end

-- update(self, dt) runs every frame while playing.
function update(self, dt)
	-- value2("move") returns the analog stick as a Vector2: .x is the
	-- left/right axis (A/D or arrows), .y the up/down axis.
	local move = actions:value2("move")
	local p = self.transform:getPosition()
	self.transform:setPosition(Vector3(
		p.x + move.x * MOVE_SPEED * dt,
		p.y + move.y * MOVE_SPEED * dt,
		p.z))
end

function shutdown(self)
	print("spin.lua: detached from '" .. self.id .. "'")
end
```

Back in the editor, select the sprite, **Add Component → ScriptComponent**, and set
its **script** property (a dropdown of the project's `.lua` assets) to your file.
Scripts are dormant in the editor — nothing ticks until you Play.

For a fuller example, read `projects/jumper-lua/scripts/player.lua` (velocity-driven
movement, buffered jumping, camera follow) and `projects/roller/scripts/ball.lua`
(tilt gravity via `InputManager:getTilt()`, a sensor `onContactBegin(self, other)`
win, `self.rigidbody:teleport(...)`).

Beyond `world`/`shared`/`InputActions`, scripts reach a handful of global tables:
`sound` and `music` (the mixer + streamed tracks), `tween` (animate values),
`screen` (`fadeOut`/`fadeIn`/`setFadeColor`/`isFading`, and `loadScene(path, out,
in)` to wipe over a scene switch), `haptics` (`play(strength, ms)` /
`pattern("light".."selection")` / `isAvailable` / `setEnabled` — phone-body
vibration, a no-op on desktop) and `loc(key, …)` (localisation). Tilt games can let
the player recalibrate the neutral pose with `InputManager.getSingleton():
calibrateTilt()` (and `clearTiltCalibration()`), persisted per-device.

## 6. Press Play

Hit **Play** in the toolbar or **Cmd/Ctrl+P**. Orkige launches the standalone
player as a **separate process** and talks to it over a debug protocol, so a crash
in your game can never take the editor down. While it runs you get a **live remote
Hierarchy and Inspector** — the tree and property values update from the running
game, and you can edit properties and cvars live.

Best of all: **edit your `.lua` and save while playing.** The editor watches
`scripts/` and hot-reloads the changed script into the running game (it compiles
before swapping, so a syntax error keeps the old code running and reports the
error). Press **Cmd/Ctrl+P** again, or **Stop**, to end the session.

## 7. Export

When you're ready to ship, use the **Build** menu:

- **Build for macOS** — a self-contained `.app` (player binary, its dylib closure,
  engine media, and your project payload; it boots your project with no arguments).
- **Build for iOS Simulator** — a simulator `.app`.
- **Build for Android APK** — a signed `.apk`.

Export runs asynchronously; progress streams into the **Console**. Output lands in
`<project>/builds/<platform>/`. Bundle and package ids come from the manifest.

## Scene view display options

The Scene viewport has a **Display** dropdown (the **eye** button) in its top-left
corner — its own little toolbar for what the view draws. Each choice is remembered
per machine (in `orkige_editor_view.ini`, like the other view state). The
**overlays** are drawn through the same facade line-mesh path as the reference
grid, so they render identically on both render backends and are masked out of the
Preview panel (overlays are editor chrome, never part of the game image); the
**View Mode** and **Lighting** choices below them re-style *only* the Scene view —
the Preview, the selected-camera inset and Play always show the real game
look.

- **Grid** — the ground-plane reference grid (on by default; hidden anyway in 2D
  mode, where it lies edge-on).
- **Colliders** — green wireframes of every `RigidBodyComponent`'s Jolt shape
  (box / sphere / capsule) at its world pose, or — for a `shape` collider — the
  actual `.oshape` contour outline (re-drawn when the shape is re-cooked). Planar
  2D bodies read correctly in the 2D top-down view. Off by default.
- **Bounding Boxes** — the cyan world-space axis-aligned bounding box of every
  renderable (mesh) object. Off by default.
- **Camera Frames** — the frustum of *every* `CameraComponent` (the selected
  camera always shows its frustum regardless of this toggle). When the **Game
  Preview** panel has a device preset selected, each camera also draws an **amber
  design-aspect rectangle** — the framing that device would render at — so you can
  see at a glance where the on-device frame differs from the editor viewport. Off
  by default.

Overlays rebuild only when something they depend on changes (selection, an object
move, a shape edit, the device preset or a toggle) — a plain camera orbit never
re-uploads them.

Below the overlays the dropdown carries two **Scene-view-only** looks:

- **View Mode** — a radio of **Shaded** (the default solid look), **Wireframe** and
  **Shaded + Wireframe**. Wireframe renders the Scene view's 3D geometry as line-fill
  for inspecting geometry, on **both** backends by different roads. On **classic** it
  flips the Scene view camera's polygon mode (per-camera, leak-free, since that camera
  only ever draws the Scene RTT). On **Ogre-Next**, polygon mode is baked into
  pipeline-state objects with no per-target override, so wireframe is a **global**
  state: the backend keeps the generated datablocks split into a 3D-scene set (mesh /
  material / water) and a 2D-UI set (sprites, vector shapes, dynamic lines, the
  editor's own ImGui chrome + gui), and wireframe flips **only** the 3D-scene set — so
  the geometry wireframes while the editor UI and 2D content stay solid. Because it is
  global, it rides the **one-game-view invariant** (below): it is armed only on a frame
  the Scene view owns, so it never leaks into the Preview or Play, and it composes
  with the Lighting toggle (flat unlit wireframe). **Shaded + Wireframe** keeps the
  solid shaded look and draws a thin wireframe **on top** of it (line edges over the lit
  surfaces) — also on **both** backends, by the SAME road: **overlay items**, never a
  mid-frame polygon flip (which Ogre-Next's baked pipeline-state cannot bracket
  per-target, and classic never had as a second pass). While armed, every scene mesh
  gets a second renderable sharing its mesh and node, drawn with one shared unlit
  near-black wireframe material that sits right on the shaded surface; the shaded pass
  renders untouched and the overlay items add the lines. The overlays carry the same
  editor-only visibility bit as the grid/gizmos, so they never leak into the Game
  Preview or Play, cast no shadows and are never clickable; the set rebuilds live as you
  add, delete or swap objects, and disarming destroys it (the shaded scene was never
  changed). It composes with the Lighting toggle (flat unlit + lines) and is
  radio-exclusive with Wireframe. Animated/skinned meshes are left out in this version —
  static scene geometry is the mode's habitat.
- **Lighting** — when off, the Scene view renders flat (**albedo + a bright flat
  ambient**, every analytic light suppressed) for inspecting materials without the
  light rig. It works on **both** backends, but as a **global per-frame** state, not a
  per-target one — a per-target route is impossible (Ogre-Next gathers directional
  lights from the scene's global light list, unfiltered by the per-pass mask; classic
  has no per-viewport lighting override). This is safe because of the render invariant
  below: the flat look is armed **only on a frame the Scene view is the one rendering**
  (the toggle is off and the Scene view is the render). The Preview, whenever it
  renders, is always the real lit look; Play is always lit. The selected-camera inset
  lives in the Scene panel and goes flat with it. Arming/releasing snapshots and
  restores the scene's lights + ambient exactly.

**The one-game-view-renders-at-a-time invariant.** The Scene view and the Game
Preview never render in the same frame — one renders, the other pauses. In the
default layout they are **tabs of the same center pane**, so only the visible tab
renders. If you split them into a side-by-side layout, only the **most recently
focused** of the two renders each frame; the other freezes its last image and shows
a small centered "Paused while … is active" note (no flicker — the frozen texture
persists, and clicking the paused view hands rendering back to it). This is what lets
the global lighting-off (and any future global render mode) apply cleanly: whatever is
rendering owns the frame.

Agents can flip the overlays, the view mode and the lighting over MCP with
`set_view_option` / `get_view_options` — the latter reports `view_mode`, `lighting`
and the `wireframe_supported` / `lighting_supported` capability flags, and an
unsupported value is refused with its reason (see [Docs/mcp.md](mcp.md)) — and
confirm the result with `screenshot`.

## Where to go next

- **[Docs/lua-api.md](lua-api.md)** — the Lua scripting API reference: a one-line
  signature index of every global table (`world`/`save`/`music`/`tween`/`screen`/
  `haptics`/…) and core type, then the conventions and canonical snippets.
- **[Docs/gui.md](gui.md)** — the runtime GUI: the `.oui` layout grammar, the
  widget set, and the author-load-find-wire recipe for building a screen.
- **`projects/roller`** — a complete 2D physics-puzzle game in pure Lua (tilt
  gravity + sliding world tiles, multi-level progression). Zero compiled game code.
- **`projects/jumper-lua`** — a textured jump-and-run with a gui HUD, also pure
  Lua. Its `game.lua` shows the title/playing/win state machine and UI.
- **[Docs/mcp.md](mcp.md)** — drive the editor from an AI agent over the Model
  Context Protocol: open projects, edit scenes, run Play, read back screenshots
  (incl. `get_lua_api` for the scripting surface).
