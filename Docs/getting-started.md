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
