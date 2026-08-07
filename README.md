# Orkige

![Orkige](Util/media/orkige_icon.png)

**The orkitec game engine** — a C++20 game engine for desktop, mobile and web
games (macOS, Windows, Linux, iOS, Android and the browser via WebAssembly —
CI-verified on all six), with an AI-native editor that lets agents create, run,
test and debug games over MCP. Originally written 2009–2012 and shipped on the
App Store (*Pudding Panic*), revived and fully modernized in 2026. Licensed
under [Apache-2.0](LICENSE).

[![CI](https://github.com/orkitec/orkige/actions/workflows/ci.yml/badge.svg?branch=development)](https://github.com/orkitec/orkige/actions/workflows/ci.yml?query=branch%3Adevelopment)
[![Platforms](https://img.shields.io/badge/platforms-macOS%20·%20Windows%20·%20Linux%20·%20iOS%20·%20Android%20·%20Web-blue)](https://orkige.orkitec.com/help/getting-started.html)
[![Renderer](https://img.shields.io/badge/renderer-Ogre--Next%20·%20OGRE%2014%20·%20Metal%20·%20Vulkan%20·%20GL3%2B%20·%20GLES2-green)](https://orkige.orkitec.com/help/render-abstraction.html)
[![Physics](https://img.shields.io/badge/physics-Jolt%205-orange)](https://github.com/jrouwe/JoltPhysics)
[![Scripting](https://img.shields.io/badge/scripting-Lua%20(sol2)-purple)](https://orkige.orkitec.com/help/lua-api.html)
[![Editor](https://img.shields.io/badge/editor-code%20editor%20·%20breakpoint%20debugger-8a2be2)](https://orkige.orkitec.com/help/script-debugging.html)
[![License](https://img.shields.io/badge/license-Apache--2.0-lightgrey)](LICENSE)

**Website & documentation:** [orkige.orkitec.com](https://orkige.orkitec.com)
— this documentation as a searchable site, plus the generated C++ API
reference.

## Download the editor

Ready-made builds are published every night the tree changes, from a commit CI
has already proven green:
**[github.com/orkitec/orkige/releases](https://github.com/orkitec/orkige/releases)**

| Platform | Install | Portable |
|---|---|---|
| macOS (Apple silicon) | `.dmg` — signed, notarized, opens with no security prompt | `.zip` |
| Windows (x64) | `-setup.exe` — per-user, no administrator rights | `.zip` |
| Linux (x86_64) | `.AppImage` — one file, `chmod +x` and run | `.tar.gz` |

Every asset carries a `.sha256` beside it, and each build ships a
`CHANGELOG.md` and a `KNOWN-LIMITATIONS.md` saying what it cannot do yet — the
Windows builds are not code-signed, so that platform warns about an unknown
publisher. The rolling `nightly` tag is the newest build; the dated
`nightly-YYYYMMDD` releases are the archive. The editor checks for updates
itself and installs them on restart. Building from source is only needed to
write C++ game code; Lua games need nothing but the download.

## What's in the box

A full 3D engine that treats 2D as a first-class citizen: sprites, vector
shapes and the UI all render through the same facade as the 3D scene, so a
game can be purely 2D, purely 3D, or mix both freely.

- **Rendering** — dual backends behind one facade: **Ogre-Next (the default —
  Metal on macOS/iOS, Vulkan on Android)** and classic OGRE 14.6 (GL3+ on desktop,
  Vulkan-via-MoltenVK on macOS, GLES2 on iOS/Android), pixel-identical output
  selected at build time and enforced by a parity test. SDL3 windowing/input, glTF asset loading (assimp). The homegrown
  *gui* runtime UI renders through the facade's `DrawLayer2D`, so it runs on
  **both** backends (as does the ImGui-based editor) — **display-scale aware**
  (text and touch targets keep their physical size on 2×–3× screens) and
  **safe-area aware** (HUDs anchor inside the notch/home-indicator box), with
  **runtime-baked TTF fonts** (lazy glyph paging for large charsets) and
  **vector-rasterized UI art** that stays crisp at any density, a **rect-anchor
  layout system** (anchors/pivots/stretch, layout groups, nine-slice and tiled
  panels, scroll views, tab bars, dropdowns, modals and toasts, wrapped
  labels, single- and **multi-line text entry**, and **virtualized list
  views** that serve a thousand rows with a screenful of widgets), and whole
  screens authored as declarative **`.oui` files** — one agent-writable text
  file per screen, editable visually in the editor's **UI editor** (canvas
  drag/resize, anchor gizmo, widget tree with drag-drop reparenting).
  `projects/gallery` shows every widget in one tabbed showroom.
- **3D feature set** — text-asset **PBS materials** (metal-rough, normal +
  emissive maps, cutout/two-sided), **cascaded shadow maps**, sky-sourced
  **image-based lighting**, a **day-night atmosphere** (procedural sky, fog,
  sun linkage) with rain/snow **3D particles**, **animated water** with planar
  reflection, **baked-mesh terrain**, **skinned glTF characters** with
  cross-fading animation, world-space text, dynamic debug lines, and an
  authored **output grade** — every feature rendering on both backends with
  parity-tested output.
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
  `onAppPause`/`onAppResume`), and **Lua and `.oui` hot-reload during Play**
  (compile/parse-before-swap: a broken save keeps the old version running).
- **Networking** — an async **HTTP client** for games (`http` in Lua: GET, POST,
  download-to-file, progress, cancel), on each platform's OWN stack so
  certificates verify against the trust store that machine maintains:
  NSURLSession on Apple, the platform HTTP stack over JNI on Android (so a
  device's enterprise anchors, network security config and proxy settings all
  apply), WinHTTP on Windows, the page's `fetch` in the browser, and libcurl on
  Linux — the one platform with no system HTTP client to ask. https by default,
  no downgrade on redirect, and a size cap and timeout on every request.
- **Monetization** — one seam with pluggable providers for **in-app purchases**
  (`store` in Lua: product query with the storefront's own localised prices,
  purchase, restore, entitlements) and **adverts** (`ads`: banner, interstitial,
  rewarded, app-open, with consent as an ordering constraint and a reported
  banner strip a HUD can lay out around). Products are a text `.ocatalog` mapping
  one logical id to each storefront's own identifier. The store side runs against
  Apple's StoreKit on macOS and iOS; the ad side is **mediation only** — no
  network ships in the tree, and one is a provider a project brings with it. A
  deterministic simulated provider makes every unhappy path (no-fill, dismissal,
  deferred approval, already-owned) reachable on demand.
- **Scripting** — Lua on sol2 behind a backend-neutral seam; game logic lives in
  per-object `ScriptComponent`s. `projects/roller` is a complete game in pure Lua,
  zero compiled code.
- **Editor** — a full scene-authoring tool built on the engine itself: docked Hierarchy /
  Inspector / Console / Stats / RTT Scene viewport, a two-pane **asset browser**
  (folder tree, texture thumbnails, create/import, two-way drag-&-drop), a
  **Game Preview at real device presets** (iPhones/iPads/Pixels/foldables with
  genuinely occluding notches and safe areas), a **2D
  editor mode** (ortho, plane-locked gizmos), a **Tile Palette with grid
  painting** for tile-based levels (paint/erase prefab instances, one undo step
  per stroke), transform gizmos with Q/W/E/R, undo/redo, multi-select,
  **hierarchy drag-drop** (reparent and true sibling reordering — scene files
  persist child order), **built-in git tooling** (a Source Control panel,
  per-line diff gutters with undoable hunk revert, dirty-state dots in the
  asset browser), an **embedded terminal** (real pty shells in dockable tabs
  on macOS, Linux and Windows — tabs recognize a running agent CLI and show
  its name and mark), native
  macOS menu + file dialogs, and Help > Orkige Help opening the engine's
  **published documentation site** at
  [orkige.orkitec.com](https://orkige.orkitec.com). Ships as `Orkige.app`.
- **Code editor & Lua debugger, built in** — every text asset opens in an
  embedded code editor (a docked window per file, Lua/C/C++/JSON/XML
  highlighting, completion generated from the engine's own reflected API, live
  syntax checking with error badges as you type, save wired into hot-reload).
  The **script debugger** is the real thing: click breakpoints into the gutter,
  the game pauses mid-statement, step in/over/out, walk the call stack and
  inspect locals in a docked Debug panel — and the whole loop is equally
  drivable by an AI agent over MCP (see `Docs/script-debugging.md`).
- **Play mode, out of process** — Play spawns the standalone player as a separate
  process over a TCP debug protocol: live remote hierarchy/inspector, pause/step/
  stop, live property + cvar editing, script + UI hot-reload, and the Scene view
  **mirrors the running game live** — object motion, runtime-spawned objects
  (lightweight view-only stand-ins), even a mid-play scene switch swaps the view
  to the running scene — restoring the authored scene
  exactly on Stop. A crashing game can never
  take the editor down. The same protocol reaches **iOS simulators, Android
  devices and the browser** — press Play, pick a target, debug the game running
  there (a browser tab dials the same protocol back in over a WebSocket).
- **AI-native** — the editor exposes a **Model Context Protocol (MCP)** server so
  an AI agent can open projects, edit scenes, add/reparent/reorder objects,
  drive Play, set breakpoints and inspect a paused game's locals, and
  read back state and viewport screenshots (see `Docs/mcp.md`) — and the whole
  loop is **zero-setup**: an interactive editor auto-publishes its endpoint
  into the open project, so an agent started in the project directory (or in
  the editor's own embedded terminal) finds it with no configuration. The
  editor also speaks the **IDE discovery protocol** agent CLIs use, so the
  file you have open and the text you have selected flow to the agent as
  context, and the agent can open files in the editor
  (see `Docs/terminal.md`, `Docs/claude-ide.md`).
- **Projects & mobile** — a game is a folder with a `project.orkproj` manifest,
  scenes, assets, scripts and optional project-config assets; the editor opens
  projects, the player runs them, and the Build menu exports a distributable macOS
  `.app`, iOS-simulator app, Android APK, or a **self-contained browser build**
  (WebAssembly + WebGL, servable from any static host) — each with per-project
  **icons and launch screens**; store-submittable Android App Bundles and signed
  iOS `.ipa`s are a CLI flag away. With an Apple developer certificate configured,
  Play deploys straight to a **real iPhone** (signed export + install + launch).
- **Samples** — `samples/hello_orkige` (feature demo) and `samples/jumper` (a
  textured jump-and-run); `projects/` holds the real games, headlined by
  **`roller`** (a 2D physics puzzle: tilt-gravity ball + sliding world
  tiles, multi-level progression with mobile-persisted saves), plus
  **`benchmark`** (a self-running 3D+2D feature tour that doubles as a
  machine benchmark), **`gallery`** (the UI widget showroom) and
  **`vectorshapes`** (soft-body vector art + cutout animation).

## Building

Requires CMake ≥ 3.28, Ninja, and [vcpkg](https://github.com/microsoft/vcpkg) at
`~/Development/vcpkg` (or set `VCPKG_ROOT`), plus python3 ≥ 3.10 for the dev-time
asset/build tooling (stdlib only — the shipped game never needs Python). All
dependencies come from the vcpkg manifest — nothing is vendored, nothing
system-installed is used (exception: MoltenVK acts as the Vulkan driver on macOS,
`brew install molten-vk`).

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

Every push runs a **fifteen-verdict CI matrix** — roughly one job per platform ×
flavor, so a failure names itself (`.github/workflows/ci.yml`). Both Linux
render flavors run their full windowed suites under a virtual display, and a
**cross-flavor parity job** then compares the two flavors' screenshots pixel by
pixel — with a diff image and the largest disagreeing region on every run, so a
divergence is readable rather than a number. Ogre-Next additionally owns
scripting-off, an ASan + UBSan pass and a ThreadSanitizer pass. macOS (Metal)
and Windows (Mesa lavapipe software Vulkan) run the complete desktop suites. The
Android player runs in a hardware-accelerated x86_64 emulator, the iOS player in
an arm64 iPhone Simulator — and the **browser player builds to WebAssembly and
boots in headless Chrome with a pixel check** on every push.

## Repository layout

```
orkige_core/       platform-independent core: meta/type system, events,
                   game objects, serialization, Lua scripting, debug-net
                   protocol, projects
orkige_engine/     the OGRE-facing layer: graphics, input, sound, physics, gui
tools/editor/      the Orkige editor
tools/player/      the standalone runtime (desktop, mobile, web)
samples/           hello_orkige, jumper
projects/          .orkproj game projects (roller, benchmark, jumper-lua, ...)
tests/             unit + integration suites
Util/              asset generators + build/export tooling (python, stdlib-only)
ports/ triplets/   vcpkg overlay ports and triplets
Docs/              the documentation corpus (published at orkige.orkitec.com)
```

Work lands on `development`, and `main` is the last commit the whole matrix
turned green — CI fast-forwards it there itself, so every commit on `main`
carries its own verdicts. The deep history carries the engine's 2009–2012
origins; the games shipped on the original engine live in a private archive.

## Upstream

Fixes discovered during the revival are contributed back to OGRE and
Ogre-Next; until a fix ships in a pinned release, it is carried as a
documented overlay-port patch. The running record — every PR, its status,
and the matching patch lifecycle — lives in [Docs/ports.md](Docs/ports.md).

---


## License

Apache License 2.0 — see [LICENSE](LICENSE). © 2009–2026 orkitec / Steffen Römer and contributors.

Orkige links a large third-party closure statically and bundles third-party
media. Their notices and license texts are
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), which is packaged into the
editor, the player and every exported game — the licenses require the text to
travel with the binary. See
[Docs/vendored-libs.md](Docs/vendored-libs.md#redistribution-the-third-party-notices)
for what ships where and what the copyleft entries oblige.
