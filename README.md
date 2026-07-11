# Orkige

**The orkitec game engine** — a C++20 game engine for desktop and mobile games
(macOS, Windows, Linux, iOS, Android — desktop CI-verified on all three), with an AI-native
editor that lets agents create, run, test and debug games over MCP. Originally
written 2009–2012 and shipped on the App Store (*Pudding Panic*), revived and
fully modernized in 2026. Licensed under [Apache-2.0](LICENSE).

![Editor](https://img.shields.io/badge/editor-ImGui%20docked-blue)
![Renderer](https://img.shields.io/badge/renderer-Ogre--Next%20·%20OGRE%2014%20·%20Metal%20·%20Vulkan%20·%20GL3%2B%20·%20GLES2-green)
![Physics](https://img.shields.io/badge/physics-Jolt%205-orange)
![Scripting](https://img.shields.io/badge/scripting-Lua%20(sol2)-purple)
![CI](https://img.shields.io/badge/CI-Linux%20·%20macOS%20·%20Windows-brightgreen)

## What's in the box

A full 3D engine with a first-class 2D layer on top — not a 2D engine.

- **Rendering** — dual backends behind one facade: **Ogre-Next (the default —
  Metal on macOS/iOS, Vulkan on Android)** and classic OGRE 14.5 (GL3+ on desktop,
  Vulkan-via-MoltenVK on macOS, GLES2 on iOS/Android), pixel-identical output
  selected at build time and enforced by a parity test. SDL3 windowing/input, glTF asset loading (assimp). The homegrown
  *gui* runtime UI renders through the facade's `DrawLayer2D`, so it runs on
  **both** backends (as does the ImGui-based editor) — **display-scale aware**
  (text and touch targets keep their physical size on 2×–3× screens) and
  **safe-area aware** (HUDs anchor inside the notch/home-indicator box), with
  **runtime-baked TTF fonts** (lazy glyph paging for large charsets) and
  **vector-rasterized UI art** that stays crisp at any density, a **rect-anchor
  layout system** (anchors/pivots/stretch, layout groups, nine-slice and tiled
  panels, scroll views, text entry), and whole screens authored as declarative
  **`.oui` files** — one agent-writable text file per screen.
- **Scene model** — a GameObject/component system with a **parent/child hierarchy**
  and active/inactive state, **prefabs** (`.oprefab` assets with per-instance
  structural + property overrides, Apply/Revert), a **stable-ID asset database**
  (`.orkmeta` sidecars so references survive renames), and named-XML scene
  serialization (`.oscene`).
- **2D pipeline** — sprites, **flipbook animation**, a batched **2D particle
  system** (one draw call per emitter), sprite atlases + per-platform **texture
  import settings**, **flat-colour vector shapes** (`.oshape` path assets or
  imported SVG, tessellated with anti-aliased edges — resolution-independent
  organic art) that **squash, stretch, wobble and morph** as soft bodies
  (physics-driven deformation at ~4 µs per shape per frame, morph poses cooked
  from SVG sequences), and an ortho 2D camera with **aspect-fit policies**
  (deterministic framing from 4:3 tablets to 21:9 phones) and painter-order
  z-sorting.
- **Physics** — Jolt Physics (with a planar "2D mode"), a data-driven **collision
  layer matrix**, object **tags**, and **sensor/trigger contact events** delivered
  to script (`onContactBegin`/`onContactEnd`).
- **Gameplay systems** — **named input actions** (keys/tilt → actions, mobile +
  desktop unified, with **tilt calibration**), an **audio mixer** (groups +
  master) with **streamed OGG music** that survives level switches, a
  **tween/easing library**, **string-table localisation** (`loc()` in Lua),
  a typed **save API** for scripts (atomic, crash-aware), **screen shake, time
  scale and full-screen fades**, **haptics** (real phone vibration on iOS and
  Android), **console variables** for live tuning, **crash breadcrumbs** (a
  persisted event trail that survives a dead process), a defined **mobile
  lifecycle** (backgrounding pauses, flushes and suspends; scripts get
  `onAppPause`/`onAppResume`), and **Lua hot-reload during Play**
  (compile-before-swap: a broken save keeps the old code running).
- **Scripting** — Lua on sol2 behind a backend-neutral seam; game logic lives in
  per-object `ScriptComponent`s. `projects/roller` is a complete game in pure Lua,
  zero compiled code.
- **Editor** — a full scene-authoring tool built on the engine itself: docked Hierarchy /
  Inspector / Console / Stats / RTT Scene viewport, a two-pane **asset browser**
  (folder tree, texture thumbnails, create/import, two-way drag-&-drop), a **2D
  editor mode** (ortho, plane-locked gizmos), a **Tile Palette with grid
  painting** for tile-based levels (paint/erase prefab instances, one undo step
  per stroke), transform gizmos with Q/W/E/R, undo/redo, multi-select, native
  macOS menu + file dialogs. Ships as `Orkige.app`.
- **Play mode, out of process** — Play spawns the standalone player as a separate
  process over a TCP debug protocol: live remote hierarchy/inspector, pause/step/
  stop, live property + cvar editing, script hot-reload. A crashing game can never
  take the editor down. The same protocol reaches **iOS simulators and Android
  devices** — press Play, pick a target, debug the game running there.
- **AI-native** — the editor exposes a **Model Context Protocol (MCP)** server so
  an AI agent can open projects, edit scenes, add/reparent objects, drive Play, and
  read back state and viewport screenshots (see `Docs/mcp.md`).
- **Projects & mobile** — a game is a folder with a `project.orkproj` manifest,
  scenes, assets, scripts and optional project-config assets; the editor opens
  projects, the player runs them, and the Build menu exports a distributable macOS
  `.app`, iOS-simulator app, or Android APK — each with per-project **icons and
  launch screens**. With an Apple developer certificate configured, Play deploys
  straight to a **real iPhone** (signed export + install + launch).
- **Samples** — `samples/hello_orkige` (feature demo) and `samples/jumper` (a
  textured jump-and-run); `projects/` holds the real games, headlined by
  **`roller`** (a 2D physics puzzle: tilt-gravity ball + sliding world
  tiles, multi-level progression with mobile-persisted saves).

## Building

Requires CMake ≥ 3.28, Ninja, and [vcpkg](https://github.com/microsoft/vcpkg) at
`~/Development/vcpkg` (or set `VCPKG_ROOT`). All dependencies come from the vcpkg
manifest — nothing is vendored, nothing system-installed is used (exception:
MoltenVK acts as the Vulkan driver on macOS, `brew install molten-vk`).

```sh
cmake --preset macos-debug                           # configure (first run builds deps)
cmake --build --preset macos-debug                   # build
ctest --preset desktop                               # verify

open build/macos-debug/tools/editor/Orkige.app       # the editor (use macos-release for speed)
./build/macos-debug-classic/samples/jumper/jumper    # play the jumper (classic tree)
```

Presets: `macos-debug`, `macos-release` build the **default Ogre-Next render
backend**; `macos-debug-classic`, `macos-release-classic` build the fully
supported **classic OGRE compatibility flavor** — it still owns native game
modules and the Vulkan runtime pick, and both flavors must render
pixel-identical output (enforced by a parity test) and ship project exports.
Use a release preset when *working in* the editor — it's ~19× faster.
Mobile presets: `ios-simulator-debug`, `android-debug` are Ogre-Next (Metal on
iOS, Vulkan on Android — the default); `ios-simulator-debug-classic`,
`android-debug-classic` build the classic GLES2 mobile flavor. Deploy flows
are documented in CLAUDE.md.

## Testing

Everything is verified by a self-checking test suite (Catch2 units + app-level
integration runs that assert real behavior — rendered triangle counts, physics
positions measured over the debug protocol, screenshot dumps):

```sh
ctest --preset unit            # headless unit tests (~3s)
ctest --preset desktop         # the default (Ogre-Next) suite
ctest --preset desktop-classic # the classic-flavor suite (exports, Vulkan, native modules)
ctest --preset all             # classic + simulator/emulator device tests
```

Every push builds and tests on Linux (both render flavors plus the
scripting-off configuration, including the full windowed desktop suite under
a virtual display), macOS and Windows (`.github/workflows/ci.yml`).

## Repository layout

```
orkige_core/      platform-independent core: meta/type system, events, game objects,
                  serialization, Lua scripting, debug-net protocol, projects
orkige_engine/    the OGRE-facing layer: graphics, input, sound, physics, gui
tools/editor/     the Orkige editor        tools/player/   the standalone runtime
samples/          hello_orkige, jumper     projects/       .orkproj game projects
tests/            unit + integration       Util/           asset generator scripts
ports/ triplets/  vcpkg overlays           Docs/           ports rationale, upstream PRs
```

Modern work happens on `main`; the deep history carries the engine's
2009–2012 origins. The games shipped on the original engine live in a
private archive.

## Upstream

Fixes discovered during the revival were contributed back to OGRE:
[#3667](https://github.com/OGRECave/ogre/pull/3667),
[#3668](https://github.com/OGRECave/ogre/pull/3668),
[#3669](https://github.com/OGRECave/ogre/pull/3669) (Vulkan-on-Apple via
`VK_EXT_metal_surface`).

---


## License

Apache License 2.0 — see [LICENSE](LICENSE). © 2009–2026 orkitec / Steffen Römer and contributors.
