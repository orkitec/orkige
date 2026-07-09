# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Orkige is a custom C++ game engine ("orkitec game engine", ~2009–2012) being revived and
modernized to build mobile games (iOS + Android) with desktop (macOS/Windows) as dev targets.
Original author: Steffen Römer. The modernization happens on the `modernize` branch; `master`
holds the historical state. Old game projects live in archived branches (`watermaze`,
`ThinkBlue`, `CigaretteGame`, tag `PuddingPanic-Appstore-version-1.1`).

Anything that seems missing (vendored `Dependencies/`, OgreLite forks, Ogitor, `engine_swf`,
`engine_video`, the CMake 2.6 build) was deliberately deleted during modernization and is
recoverable from git history — do not reintroduce it.

## Build

Requires: CMake ≥ 3.28, Ninja, vcpkg at `~/Development/vcpkg` (or set `VCPKG_ROOT`).

```sh
VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset macos-debug   # configure (runs vcpkg installs)
cmake --build --preset macos-debug                              # build
```

Presets: `macos-debug`, `macos-release` (**Ogre-Next — the DEFAULT render
backend** since 2026-07-08: `ORKIGE_RENDER_BACKEND=next`, vcpkg feature
`render-next`), `macos-debug-classic`, `macos-release-classic` (**classic
OGRE — the compatibility flavor**), `ios-simulator-debug`, `android-debug`
(classic-flavored — mobile runs the classic GLES2 path; mobile-on-Next is
future work). Output in `build/<preset>/`.
The two flavors implement the same `engine_render` facade
(`engine_render_next/` vs `engine_render_classic/`, same source tree). Games
(player, hello_orkige, projects/), fastgui AND the editor (ImGui on
DrawLayer2D) run on both flavors and must render the SAME image (WYSIWYG —
enforced by the `render_backend_parity` pixel test). Classic remains fully
supported because it OWNS what next doesn't do yet: the mobile GLES2 path,
the export pipeline (pinned to the classic presets; next-flavor export needs
Hlms media bundling — future work), native game modules, BigZip, the
Vulkan/GL runtime RS pick and the jumper C++ sample. See the flavor
capability matrix in `Docs/render-abstraction.md` ("B phase status").
Build trees are flavor-bound: reconfiguring a tree with the other backend
would silently poison its CMake/vcpkg caches, so the root CMakeLists
FATAL_ERRORs instead — delete the build dir or use the matching preset
(guard `ORKIGE_RENDER_BACKEND_CONFIGURED`).
The iOS preset cross-builds the runtime as `tools/player/OrkigePlayer.app`
(GLES2 render system, SDL3 UIKit main, media bundled in) for the arm64
simulator via `triplets/arm64-ios-simulator.cmake`; deploy with
`xcrun simctl boot/install/launch`.
The Android preset (`triplets/arm64-android.cmake`, NDK 27 via
`ANDROID_NDK_HOME`, API 28+) builds the player as `tools/player/libmain.so`
(GLES2 render system, everything incl. SDL3 statically linked);
`tools/player/android/package_apk.sh` assembles + signs
`build/android-debug/apk/OrkigePlayer.apk` directly with javac/d8/aapt2/
apksigner (no Gradle - the machine's JDK 26 predates Gradle support; SDL3's
Java glue is taken from the vcpkg SDL source). Media rides in APK assets/
and is extracted to the app files dir on first launch. Deploy with
`adb install`; emulator AVD `orkige_test` (android-35, arm64) exists.
The editor's Play toolbar has a target picker (Desktop / iOS simulators -
booted or shutdown, Play boots a shutdown one via simctl + Simulator.app
and auto-installs the built player app / adb devices+emulators - physical
Android phones use the same adb flow; iOS hardware is enumerated but gated
until signed deploys land). `editor_play_simulator` /
`editor_play_simulator_boot` / `editor_play_android` ctests cover the
flows, skipping when no prepared device is available.
Dependencies come exclusively from `vcpkg.json` (manifest mode) — never vendor libraries
into the tree and never rely on system-installed libraries.

Hermeticity: this machine hosts a second (Intel-layout) Homebrew at `/usr/local`, plus
loose orphaned headers from ~2016 (e.g. an ancient zlib.h), and clang searches
`/usr/local/include` by default. The presets force `CMAKE_OSX_SYSROOT` +
`CMAKE_IGNORE_PREFIX_PATH=/usr/local`, and `triplets/arm64-osx.cmake` (via
`VCPKG_OVERLAY_TRIPLETS`) does the same for vcpkg port builds. If a build ever reports
headers/symbols from `/usr/local`, that isolation has regressed — fix it, don't work
around it. One deliberate exception: MoltenVK is treated as the platform's Vulkan
*driver* (system-tier, like GPU drivers on Windows/Linux) and comes from Apple-Silicon
Homebrew (`brew install molten-vk`, found via its ICD manifest under `/opt/homebrew`);
the Vulkan *loader* and headers stay vcpkg-provided.

## Build speed / iteration discipline

- Scope builds to what you're working on: `cmake --build --preset macos-debug
  --target orkige_engine_tests` (or `orkige_editor`, `jumper`, ...) instead of
  the full preset build.
- During development run `ctest --preset unit` (~3s, headless); use
  `ctest --preset desktop` (the default next-flavor suite, excludes the
  `device`-labeled simulator/emulator tests) as the standard verification
  pass, add `ctest --preset desktop-classic` when the change touches the
  classic backend/flavor-shared code, and the full `ctest --preset all`
  (classic tree incl. device tests) when deploy/device code changed or
  before handing over.
- USING the editor or playing samples (as opposed to developing them): build
  and run the `macos-release` preset — the Debug editor runs ~19x slower
  (measured 237 vs ~4500 fps) because of -O0 plus assert-heavy debug
  OGRE/Jolt. Debug is for development and tests; Release is for actually
  working in the tool.
- ccache is wired in automatically (root CMakeLists `find_program`). PCH
  targets add `-Xclang -fno-pch-timestamp` via `orkige_pch_ccache_compat()`;
  the machine's ccache carries the matching one-time setting
  `sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime`
  — without both, PCH-using TUs never hit the cache.
- Port dirs are hashed byte-for-byte into the vcpkg ABI hash: ANY edit under
  `ports/<name>/` (even a README typo) forces that port to rebuild on all
  three triplets (macOS, iOS, Android). Batch port edits, keep in-port
  READMEs to the single pointer line, and put all prose in `Docs/ports.md`.
- New fat targets (many TUs including Ogre.h / sol2 / imgui) get
  `target_precompile_headers` with every entry wrapped in
  `$<$<COMPILE_LANGUAGE:CXX>:...>` (the targets contain .mm files, and PCHs
  must not leak across languages) plus a `orkige_pch_ccache_compat()` call.
  Tiny targets (one or two TUs) aren't worth a PCH.

## Testing

```sh
ctest --preset unit            # headless Catch2 unit tests (~3s) — safe to run anytime
ctest --preset desktop         # the default (Ogre-Next) suite (no simulator/emulator boots)
ctest --preset desktop-classic # the classic-flavor suite: exports, Vulkan runs,
                               # native-module tests (build macos-debug-classic first)
ctest --preset all             # classic tree incl. device tests (boots simulators/emulators)
```

Layout: `tests/core/` is the Catch2 unit suite (`orkige_core_tests`, label `unit`,
boots the app singleton set headlessly via `CoreTestEnvironment`). The integration
tests (label `integration`, registered in `tests/CMakeLists.txt`) reuse the
self-checking apps — hello_orkige demos, editor self-check/resize, player — which
verify themselves and exit non-zero on failure; that exit code is the contract.

The rule: every change ships with tests that verify it — unit tests for core
logic, a self-check hook wired into ctest for app/runtime behavior. `ctest` must
pass before committing.

CI (GitHub Actions, `.github/workflows/ci.yml`): every push builds + tests the
Linux classic flavor (required) and Linux next (experimental). A `pre-push` git
hook (install once per clone: `Util/install_git_hooks.sh`) spawns
`Util/watch_ci.sh` detached, which polls the push's runs and reports via macOS
notification + `~/.orkige/ci-watch-<sha>.log` (failure = failing steps' log
tail included). Skip once with `ORKIGE_NO_CI_WATCH=1 git push`. When a CI
failure lands, look into fixing it promptly — a red required job blocks
everyone's confidence in the suite.

## Modernization ground rules

- C++20, no boost. Old code being touched gets moved to std equivalents
  (`std::shared_ptr`, `<type_traits>`, range-for, `std::function`, `std::mutex`).
- Renderer target is OGRE 14.x from vcpkg (port from the historical OGRE 1.7 API).
  Window/input target is SDL3 (replaces the abandoned OIS).
- Scripting is Lua-first and LIVE, behind a backend-neutral seam: application code talks
  ONLY to `core_script/ScriptRuntime` (always compiled; `available()` is false and errors
  are honest in `ORKIGE_SCRIPTING=OFF` builds — both configs must keep building). The sol2
  backend (`ScriptManager` + `Meta_Lua.h`) is an implementation detail selected in
  `Meta.h`. NEVER write raw `#ifdef ORKIGE_LUA` outside Meta.h/Meta_Lua.h/Meta_None.h and
  the ScriptRuntime implementation — the meta macro vocabulary (incl. `OUSERTYPE*`) is
  complete in both backends by design. Game behavior lives in project scripts via
  `engine_gocomponent/ScriptComponent` (per-instance sandbox, init/update/shutdown, `self`
  + the global `world`/`shared` tables); `projects/jumper-lua/scripts/player.lua` is the
  reference script. The dead Python backend (`Meta_Python.h`, `core_python/`) stays
  uncompiled — don't "fix" it in passing.
- Everything builds statically during the revival (`ORKIGE_STATIC` is defined globally);
  the old `__declspec` DLL export macros in the prerequisites headers are inert.
- Keep the existing code style when editing old files: tabs, `m`-prefixed members,
  Doxygen-style comments, `#ifndef` include guards with date suffixes.
- Line endings are LF everywhere, enforced by `.gitattributes` (the tree was normalized
  in a dedicated commit on 2026-07-08; the old preserve-CRLF rule is obsolete).
- Commit messages: no `Co-Authored-By` trailers.
- Renderer containment (decided 2026-07; since 2026-07-08 **Ogre-Next is the default
  backend** and classic OGRE the fully supported compatibility flavor): code above the
  render backend goes through the `engine_render` facade — no `Ogre::` outside
  `engine_graphic/`, `engine_render_classic/`, `engine_render_next/`
  and `engine_render/RenderMath.h`. The rule is ENFORCED MECHANICALLY since WP-A1.5:
  `render_containment_lint` (a unit-labeled ctest running `Util/check_ogre_containment.py`,
  part of the unit and desktop presets) fails on any unsanctioned `Ogre::` spelling in
  code; the sanctioned files/blocks (classic-only zones, math-alias residue, the marked
  app boot blocks) live in `Util/ogre_containment.json` — a new exception needs an entry
  there, with a reason, in the same change. Don't add reliance on features Ogre-Next
  dropped (OGRE material scripts especially — keep materials simple/generated).

## MCP endpoint (AI-agent editor control)

The editor HOSTS an MCP server itself over Streamable HTTP (WP #90, retired the
#80 `Util/orkige_mcp.py` Python stdio sidecar and its `mcp` pip dependency): one
`POST /mcp` endpoint speaking JSON-RPC 2.0 (`initialize`, `tools/list`,
`tools/call`, notifications). A remote MCP client (Claude Code/Desktop) connects
to the running editor's URL — NO command to spawn, NO new vcpkg/pip dependency
(the HTTP/1.1 server and the nested-JSON codec are hand-rolled in `core_debugnet`
on the existing non-blocking socket layer: `HttpServer` + `Json`). Register with
`claude mcp add --transport http orkige http://127.0.0.1:<port>/mcp --header
"Authorization: Bearer <token>"`.

OPT-IN and OFF by default: launch with `--mcp-port <N> --mcp-token-file <path>`
(aliases `--control-port` / `--control-token-file`; env `ORKIGE_MCP_PORT` /
`ORKIGE_MCP_TOKEN_FILE`, historical `ORKIGE_CONTROL_*` still honored) — no normal
run/test opens a socket. `tools/editor/EditorControlServer.{h,cpp}` is the HTTP +
JSON-RPC transport in front of the #80 command handler, REUSED wholesale: a thin
adapter over `EditorCore` + the `EditorDocument` free functions. Each verb is an
MCP tool with a JSON `inputSchema`; a `tools/call` runs the verb on the handler's
internal DebugMessage request/reply and returns the reply as MCP tool content
(text + `structuredContent`, or `isError`). AUTH: mutations need the
`Authorization: Bearer <token>` header (the editor writes the secret to the token
file; reads are open; no token file ⇒ auth off for dev). Correlation is JSON-RPC's
native `id`. POST-only (no SSE); long ops (play boot) return an accepted result
and are polled via `get_state`. Play control is translated into the ONE existing
player debug protocol — never a second player port. The ~17 tools (open_project,
open/save/new scene, list_hierarchy, get/set_component over the six typed bundles,
create/delete/reparent object, add/remove component, play/stop, screenshot → file
path, list_assets, console_tail, …) map onto existing `EditorCore` methods + the
`EditorDocument` free functions. Verified headlessly by the `editor_control` ctest
(a worker thread drives a raw socket through the whole MCP conversation incl. auth
rejection) plus the `JsonTests`/`HttpServerTests` unit tests. Full reference:
`Docs/mcp.md`.

## Architecture

Two layers, each split into small modules with a flat `<module>/<File>.{h,cpp}` layout.
Include paths are rooted at the layer directory (e.g. `#include "core_util/String.h"`).

**`orkige_core/`** — platform- and renderer-independent. Builds today as the static lib
`orkige_core` (alias `Orkige::Core`). Key ideas that span multiple modules:

- **Meta/type system** (`core_base`): every engine class registers a `TypeInfo` via the
  `OTYPE_INFO*` macros in `Meta.h`; `TypeManager` is the registry. The `Meta_*.h` backends
  additionally expose registered types to a scripting language — selected by
  `ORKIGE_NOSCRIPT` / `ORKIGE_LUA` defines at build level.
- **`optr`** (`core_util/optr.h`): the engine-wide smart-pointer alias — a `#define` for
  `std::shared_ptr` (`woptr` = `weak_ptr`). Old code uses it pervasively; keep using it.
- **Events** (`core_event`): global pub/sub via `GlobalEventManager` singleton;
  handlers are FastDelegate-based (`EventListener.h`).
- **Game objects** (`core_game`): `GameObject` = id + component container built on the
  generic `core_util/ComponentHolder`/`AttributeHolder` templates (SFINAE-heavy).
- **Serialization** (`core_serialization`): `ISerializeable` + archive pattern;
  `XMLArchive` is the tinyxml2-backed implementation.
- **Memory/debug** (`core_debug`): custom `MemoryManager` with allocation tracking,
  `LogManager` configured from XML.
- Umbrella header: `core_module/OrkigePrerequisites.h` (forward decls, export macros).

**`orkige_engine/`** — the OGRE-facing layer, fully ported to OGRE 14.5 + SDL3 (gated
behind `ORKIGE_BUILD_ENGINE`, ON for all app work). Umbrella: `engine_module/
EnginePrerequisites.h` — backend-NEUTRAL since B3 (core prerequisites + Meta + the
`RenderMath.h` alias vocabulary); classic-only TUs use
`engine_module/EnginePrerequisitesClassic.h` (adds the `<Ogre.h>` umbrella).
`engine_graphic/Engine.h` is the central engine object on BOTH render flavors (it
dispatches: classic bootstrapper vs. the facade-only `EngineNext.h` sibling;
`engine:hasUISystem()` returns true on BOTH flavors — fastgui renders through the
`DrawLayer2D` facade, so it and the ImGui editor run on next as well; the probe
stays only so a hypothetical future UI-less flavor could answer honestly). Classic render
system selection via `ORKIGE_RENDERSYSTEM` env: GL3Plus default; Vulkan renders via
MoltenVK on macOS; GLES2 on iOS/Android; Metal builds but can't render RTSS content —
see Docs/ports.md. The next flavor boots Ogre-Next's Metal RS. `engine_gocomponent` bridges core game objects to the scene
(`TransformComponent`, `ModelComponent`, `SpriteComponent` — the 2D building block: a
textured alpha-blended quad in the XY plane, per-texture generated `Sprite/<tex>`
material, zOrder → render-queue painter's sorting (its header documents the
alpha/sorting rules), `RigidBodyComponent` on Jolt via the
backend-agnostic `engine_physic/PhysicsWorld` (planar 2D mode, `teleport` moves body +
transform even while the sim is `setPaused` — the tile-slide/"move world" API),
`SoundComponent` on OpenAL Soft,
`AnimationComponent`, `CameraComponent` (projection mode + orthoSize serialize; ortho =
the 2D camera, also reachable via `Engine::setCameraOrthographic`), the Lua
`ScriptComponent` — dormant unless a
runtime ticks GameObjects, so the editor never runs scripts); `engine_input` is SDL3-based
(KC_* keycodes preserved; `isKeyDown` reads the injectEvent-fed state, so synthetic
SDL events work; `getTilt()` = normalized gravity direction, accelerometer-backed via
SDL sensors where one exists, LEFT/RIGHT-key simulated on desktops — wall-clock paced,
so selfchecks poll it condition-driven instead of frame-counting); `engine_fastgui` (widgets + the engine-owned `UiAtlas`/`UiRenderer` 2D renderer on the `DrawLayer2D` facade; the vendored Gorilla fork is gone) is the runtime UI system, rendering on BOTH flavors, with
atlases generated by `Util/make_fastgui_atlas.py`; `engine_filesystem` wraps archives;
`core_debugnet` (in core) carries the editor<->player debug protocol.

**Tools & apps**: `tools/editor` — the Orkige editor (docked ImGui UI, RTT scene panel,
gizmos, undo/redo in the UI-agnostic `orkige_editor_core`, native macOS menu + file
dialogs, play/pause/step/stop spawning `tools/player` with live remote
hierarchy/inspector over the debug protocol; Play targets: desktop, iOS simulators,
adb devices). On macOS the editor builds as a proper `Orkige.app` bundle (Dock
icon generated by `Util/make_editor_icon.py` + iconutil at build time; the
settings inis live in the bundle's `Resources/` via SDL_GetBasePath; Linux keeps
the bare `orkige_editor` executable — ctest reaches both through the target name). Unity muscle memory: Cmd/Ctrl+P toggles Play/Stop, the Hierarchy has a
filter box, F2/Delete/Cmd+D work from Scene panel AND focused Hierarchy, snap steps are
editable via the toolbar popover, and an interactive launch reopens the last project
(all persisted in `orkige_editor_view.ini`; automated runs are exempt via the
`automatedRun` env probe — they start blank, render vsync-free and never touch the
user's recents). `tools/player` — the standalone runtime (scene/project loader, debug
server). The player CLI contract (`[scene.oscene] [--project <dir>] [--debug-port N]`)
and the runtime side of the debug protocol live in `engine_runtime/PlayerRuntime.h`
(`PlayerArguments` + `PlayerDebugLink`) — the player and native game modules share them.
`samples/`: hello_orkige (feature demo with env-hooked self-checks), jumper
(textured jump-and-run with fastgui HUD). `projects/` holds .orkproj project folders —
`projects/jumper-lua/` is the jumper reimplemented in pure Lua (ScriptComponent, zero
compiled game code; verified by the player_jumper_lua_selfcheck ctest);
`projects/roller/` is the 2D tier proven end to end — the Continuity×Rolando prototype
(tilt-gravity ball + sliding-tile "move world" mode; assets AND the .oscene generated by
`Util/make_roller_assets.py`, HUD atlas by `make_fastgui_atlas.py`; verified by the
player_roller_selfcheck ctest, which probes that a tile slide moves sprites AND
collision bodies while the sim is paused);
`projects/jumper-native/` is the jumper as a **native project module**: manifest
Settings `native.target`/`native.cmakeDir`/`native.buildDir`
(`core_project/NativeModule.h`) mark a project as carrying compiled C++ game code
under `native/`, built as a standalone CMake project against the engine build tree
via `cmake/OrkigeGameModule.cmake` (no installed SDK yet — the helper file IS the
interim contract). In the editor, Play on such a project becomes compile-on-Play:
async incremental cmake build with `[build]` lines streamed into the Console (Stop
cancels, a failed build stays in edit mode and launches nothing), then the project's
own executable runs as the play process (desktop target only; covered by the
editor_project_native_play / _break ctests — their build tree persists under
`projects/jumper-native/native/build`, gitignored, so re-runs build incrementally).
**Project export** (`Util/orkige_export.py`, editor Build menu): packages a project as a
distributable macOS .app (self-contained: player/module binary + dylib closure + engine
media + project payload; a marker file makes the app boot its bundled project with no
arguments — `PlayerBundle` in `engine_runtime/PlayerRuntime.h`), an iOS-simulator .app or
an Android APK (via `package_apk.sh`; native-module projects are desktop-only). Output:
`<project>/builds/<platform>/`; bundle/package ids come from the manifest Settings
`export.macos.bundleId` / `export.android.package`. Covered by the `export_*` ctests
(the macOS ones RUN the exported app from a neutral cwd).
Legacy tool sources (`orkige_fontconverter` etc.) and `Util/`'s 2012 binaries remain
unbuilt reference material; `Util/*.py` are the live asset generators.

**Docs/**: historical API docs (`OrkigeAPI`, `LuaAPI`), `Docs/ports.md` (overlay-port
rationale), `Docs/upstream/` (OGRE PR package — submitted as OGRECave/ogre #3667-3669),
`Docs/mcp.md` (the MCP endpoint), `Docs/render-abstraction.md` (the facade design/audit).

## Feature systems (the 2026 build)

Landed on `modernize` on top of the revival, each verified on both flavors. Where to
look when touching one:

- **Scene model**: GameObject **parent/child hierarchy + active state** (`core_game`;
  `TransformComponent` composes world transforms through the render node graph;
  `GameObjectManager` `ChildIdMap`/`tagIds` indexes). **Prefabs** =
  `core_game/PrefabSerializer` (`.oprefab` subtree assets; instances store `prefabRef`
  + `suppressedChildren` + per-child property overrides; Apply/Revert in the editor).
  **Scene format is v5** — serialization is currently POSITIONAL/version-gated (the
  reflection work #94 replaces it with named fields). **Object tags** (multi-tag,
  `world.findByTag`). Serialization: `core_serialization` (`ISerializeable` + `XMLArchive`).
- **Asset pipeline**: `core_project/AssetDatabase` = stable IDs via `.orkmeta` sidecars
  (references survive renames; v2 sidecars carry per-platform **texture import
  settings**); the editor **asset browser** (folder tree, thumbnails, drag-&-drop
  import/instantiate); `Util/make_sprite_atlas.py` + `cook_textures.py`.
- **2D**: `SpriteComponent`, `SpriteAnimationComponent` (flipbook), `ParticleComponent`
  + the facade `SpriteBatch` (one draw per emitter), an ortho **2D editor mode**.
- **Physics** (`engine_physic/PhysicsWorld`, Jolt): a data-driven **collision layer
  matrix** (`physics.olayers`), `RigidBodyComponent` layer + **sensor** flag, and
  **contact events** (worker-thread callbacks → mutex queue → main-thread drain →
  `ScriptComponent` `onContactBegin/End` + C++ events).
- **Gameplay**: `engine_input/InputActionMap` (named actions over keys/tilt,
  `input.oactions`); `engine_sound` mixer groups + master; `core_tween`
  (`TweenManager` + `EaseLibrary`); `core_debug/CVarManager` (typed cvars, live-tunable
  over the debug protocol, `cvar.`-prefixed manifest persistence).
- **Iteration**: **Lua hot-reload during Play** (`ScriptComponent::hotReload`,
  compile-before-swap; editor watches `scripts/` and sends `MSG_RELOAD_SCRIPT`);
  **level system** (`core_game/Level*`: deferred mid-play scene switch via the
  `LevelManager` pending-load applied at the player-loop frame boundary;
  `levels.olevels`; progression save in `getDocumentsDirectory`).
- **AI control**: the editor hosts an **MCP server over Streamable HTTP** — see the
  MCP section above + `Docs/mcp.md`.
- **CONVENTIONS to preserve**: the **config-asset** pattern (project-config files —
  `input.oactions`/`physics.olayers`/`levels.olevels` — are manifest-`Settings`-referenced,
  NOT under `assets/`, NOT id-tracked; bundled to exports via `CONFIG_SETTING_KEYS` in
  `orkige_export.py`); the **canonical player-loop tick order** (fenced block in
  `tools/player/main.cpp`: input → scripts → tweens → physics → deferred-load); the
  **scene teardown hook** (`GameObjectManager::clear`, fenced); the **world-table
  accessor convention** (fenced block in `ScriptComponent`). One backend-neutral
  property registry driven off Meta (#94, in design) is meant to retire the four
  hand-kept property allowlists (debug read/write, inspector, MCP).

## CI

GitHub Actions (`.github/workflows/ci.yml`) builds + tests on every push. The required
**Linux-classic** job (GL3Plus) builds engine/editor/player/samples and runs the
headless unit suite — GREEN. It builds with **clang** (`CC/CXX` in the workflow env;
matches the clang-oriented codebase), and needs a few system dev packages the cold
vcpkg build surfaced (autoconf-archive, libltdl-dev, libxtst/libxinerama; SDL's builtin
iconv via the `triplets/x64-linux.cmake` overlay). **Linux-next** (Vulkan) is
experimental/`continue-on-error` — currently red on an ogre-next Vulkan RS packaging
gap. A `pre-push` hook (`Util/install_git_hooks.sh`) spawns `Util/watch_ci.sh` to report
each push's result.

