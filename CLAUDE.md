# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Orkige is a custom C++ game engine ("orkitec game engine", ~2009–2012) being revived and
modernized to build mobile games (iOS + Android) with desktop (macOS/Windows) as dev targets.
Original author: Steffen Römer. This public repository carries the modernized engine on
`main` (its history reaches back to 2009). The pre-modernization state, the old game
projects and the removed vendored dependencies live in the PRIVATE archive repository
`orkitec/orkige-archive` (remote `archive` on dev machines).

Anything that seems missing (vendored `Dependencies/`, OgreLite forks, Ogitor, `engine_swf`,
`engine_video`, the CMake 2.6 build) was deliberately deleted during modernization and is
recoverable from history (the private archive holds everything) — do not reintroduce it.

## Build

Requires: CMake ≥ 3.28, Ninja, vcpkg at `~/Development/vcpkg` (or set `VCPKG_ROOT`),
python3 >= 3.10 (stdlib only by policy — enforced by python_stdlib_lint; the shipped
player never needs Python).

```sh
VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset macos-debug   # configure (runs vcpkg installs)
cmake --build --preset macos-debug                              # build
```

Presets: `macos-debug`, `macos-release` (**Ogre-Next — the DEFAULT render
backend** since 2026-07-08: `ORKIGE_RENDER_BACKEND=next`, vcpkg feature
`render-next`), `macos-debug-classic`, `macos-release-classic` (**classic
OGRE — the compatibility flavor**), `ios-simulator-debug`, `android-debug`
(**Ogre-Next too — the DEFAULT on mobile since the mobile flip: iOS boots
Metal, Android boots Vulkan**), `ios-simulator-debug-classic`,
`android-debug-classic` (the classic GLES2 mobile flavor),
`ios-device-debug`/`ios-device-release` (**arm64 iPhoneOS — the
physical-device player, Ogre-Next/Metal**),
`ios-device-debug-classic`/`ios-device-release-classic` (the classic
GLES2 device flavor) and `web-release` (**wasm32 via Emscripten — the
browser player, classic GLES2→WebGL**; needs the user-local emsdk, see
`triplets/wasm32-emscripten.cmake`; the chainload wrapper
`cmake/wasm32-emscripten-toolchain.cmake` carries `-fwasm-exceptions` for
the WHOLE closure because vcpkg silently drops triplet compiler flags on
toolchain-less platforms; export via `orkige_export.py --platform web`,
Play in Browser from the editor's target picker serves it on a loopback
HttpServer instance AND is a live debug session: the page dials the debug
link back in over a WebSocket the serve port upgrades — the ONE protocol,
reversed direction — so remote logs/hierarchy/pause work; screenshot/trace/
hot-reload refuse honestly — `Docs/web-export.md`). Output in `build/<preset>/`.
The two flavors implement the same `engine_render` facade
(`engine_render_next/` vs `engine_render_classic/`, same source tree). Games
(player, hello_orkige, projects/), gui AND the editor (ImGui on
DrawLayer2D) run on both flavors and must render the SAME image (WYSIWYG —
enforced by the `render_backend_parity` pixel test). Classic remains fully
supported because it OWNS what next doesn't do yet: the Vulkan/GL
runtime RS pick and the jumper C++ sample. **Pak mounting is now
backend-neutral** (the former classic-only BigZip): `RenderSystem::mountPak`
mounts a zip's contents — optionally a prefix-stripped sub-tree, the APK
`assets/` case — so scenes/textures/sounds resolve like loose files on BOTH
flavors; because the Ogre-Next build ships no zip support, a small shared
`engine_filesystem/MiniZip` reader (STORED + DEFLATE over the zlib already in
the closure) backs `PakArchive` on both — `Docs/filesystem.md`. **Native game
modules (compiled C++ game code, `projects/jumper-native/`) now build, play and
export on BOTH flavors** — the module links the flavor's engine closure resolved
from the engine build tree's cache (`cmake/OrkigeGameModule.cmake`) and is
flavor-neutral game code by construction. Project
export ships on BOTH flavors now (the exporter bundles the tree's engine
media — classic RTSS media or the Ogre-Next Hlms shader templates —
resolved at boot via `PlayerBundle`); the classic GLES2 mobile path lives
on in the `-classic` mobile presets. See the flavor capability matrix in
`Docs/render-abstraction.md`.
Build trees are flavor-bound: reconfiguring a tree with the other backend
would silently poison its CMake/vcpkg caches, so the root CMakeLists
FATAL_ERRORs instead — delete the build dir or use the matching preset
(guard `ORKIGE_RENDER_BACKEND_CONFIGURED`).
The iOS preset cross-builds the runtime as `tools/player/OrkigePlayer.app`
(Ogre-Next Metal by default / GLES2 on `-classic`, SDL3 UIKit main, media
bundled in) for the arm64 simulator via `triplets/arm64-ios-simulator.cmake`;
deploy with `xcrun simctl boot/install/launch`. The `ios-device-*` presets
(`triplets/arm64-ios-device.cmake`, `iphoneos` sysroot) build the SAME `.app`
for arm64 physical hardware — compiling/linking need NO certificate (Ninja
never runs codesign at build; real signing happens at export time via the
`export.ios.teamId` manifest + `ORKIGE_IOS_SIGNING_IDENTITY`/`_PROVISIONING_PROFILE`
seam in `Util/orkige_export.py --platform ios`). Deploy the signed `.app` with
`xcrun devicectl device install app` + `... process launch`. Live debug over
USB is NOT wired: a device has no dependency-free debug-port TCP tunnel (unlike
the simulator's shared loopback / Android's `adb forward`), so the game runs
standalone on hardware — see `Docs/ios-signing.md`.
The Android preset (`triplets/arm64-android.cmake`, NDK 27 via
`ANDROID_NDK_HOME`, API 28+) builds the player as `tools/player/libmain.so`
(Ogre-Next Vulkan by default / GLES2 on `-classic`, everything incl. SDL3
statically linked);
`tools/player/android/package_apk.sh` assembles + signs
`build/android-debug/apk/OrkigePlayer.apk` directly with javac/d8/aapt2/
apksigner (no Gradle - the machine's JDK 26 predates Gradle support; SDL3's
Java glue is taken from the vcpkg SDL source). Media rides in APK assets/;
the manifest Setting `export.android.assets` chooses how (`stored`, the
DEFAULT: assets stay UNCOMPRESSED so the player MOUNTS its own APK — path via
JNI `getPackageCodePath` — and reads the bulk game media in place, no
first-launch extraction; only the small fopen tree (manifest/scenes/scripts/
config + shader/font media) still extracts. `compressed`: assets deflated for a
smaller APK, extracted on first launch — the older path). The AAB path keeps
the assets uncompressed via a bundletool BundleConfig `uncompressedGlob`. Deploy
with `adb install`; emulator AVD `orkige_test` (android-35, arm64) exists.
The editor's Play toolbar has a target picker (Desktop / iOS simulators -
booted or shutdown, Play boots a shutdown one via simctl + Simulator.app
and auto-installs the built player app / adb devices+emulators - physical
Android phones use the same adb flow; iOS hardware becomes selectable once iOS
signing is configured — Play on a device is a deploy-and-run: an `ios` export
+ `devicectl` install/launch, NOT a live session, since USB has no debug-port
tunnel, see `Docs/ios-signing.md`). `editor_play_simulator` /
`editor_play_simulator_boot` / `editor_play_android` / `editor_play_ios_device`
ctests cover the flows, skipping (exit 77) when no prepared device is
available (the `ios_device` one is a signing/hardware GATE probe — it skips on
every machine without a cert + connected iPhone + built device player).
For a physical-phone session outside the editor, `python3 Util/orkige_device.py
doctor|android|ios` is the one-command deploy-and-run front door (readiness
report + build-if-stale, package via `orkige_export.py`, install, launch, stream
logcat; default project `projects/benchmark`) — the owner runbook is
`Docs/device-session.md`, covered by the `orkige_device_selftest` unit ctest.
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
ctest --preset desktop         # the default (Ogre-Next) suite (no simulator/emulator boots;
                               # incl. the native-module play/export tests, next flavor)
ctest --preset desktop-classic # the classic-flavor suite: exports, Vulkan runs,
                               # native-module tests (build macos-debug-classic first)
ctest --preset all             # classic tree incl. device tests (boots simulators/emulators)
ctest --preset web             # the web tree: wasm core units under node + the
                               # export structure/boot tests (Chrome headless,
                               # skip-77 without a browser) — build web-release first
```

Layout: `tests/core/` is the Catch2 unit suite (`orkige_core_tests`, label `unit`,
boots the app singleton set headlessly via `CoreTestEnvironment`). The integration
tests (label `integration`, registered in `tests/CMakeLists.txt`) reuse the
self-checking apps — hello_orkige demos, editor self-check/resize, player — which
verify themselves and exit non-zero on failure; that exit code is the contract.

The rule: every change ships with tests that verify it — unit tests for core
logic, a self-check hook wired into ctest for app/runtime behavior. `ctest` must
pass before committing.

**Local Linux rig** (`Util/linux_rig/`): `run_container.sh` builds the
`orkige-ci-linux` image (ubuntu 24.04, clang, xvfb + Mesa lavapipe/llvmpipe,
the CI-pinned vcpkg) and starts the long-lived `orkige-ci` container with the
repo bind-mounted and build trees/caches in named volumes — a local twin of
the CI Linux jobs for anything macOS can't reproduce. Every linux-* preset
works inside; the important one is `linux-debug-sanitize`: the ASan/UBSan
gate runs on **libstdc++**, which exposes memory bugs libc++ (macOS) masks
even under a local macOS ASan build (destroyed-container internals differ) —
memory-safety findings from CI are reproduced and fixes proven in the
container, and any core lifecycle/teardown change should pass a container
sanitizer-unit run before pushing. On an arm64 host, configure with
`-DVCPKG_INSTALL_OPTIONS="--clean-after-build;--allow-unsupported"`
(ogre-next's supports-list has no linux&arm64 entry;
`triplets/arm64-linux.cmake` covers the triplet). Windowed tests run the CI
way: `xvfb-run -a -s "-screen 0 1280x1024x24 +extension RANDR" ctest ...`
with `VK_DRIVER_FILES` pointed at the lavapipe ICD.

CI (GitHub Actions, `.github/workflows/ci.yml`): every push builds and runs the
unit + full desktop compatibility suites for Linux classic. Linux next (Vulkan
— the default backend) also runs the unit gate and full windowed desktop suite
under xvfb/lavapipe, plus the
`ORKIGE_SCRIPTING=OFF` build + unit gate (preset `linux-debug-noscript`), and a
CI-only ASan + UBSan build running the complete unit + desktop integration
suite. All tests are hard gates on the next flavor. The next job also builds the Android player
for an accelerated x86_64 emulator and runs the adb Play test; macOS next builds
and runs the complete non-device host suite, builds the arm64 iOS Simulator
player, then runs the simulator export/Play/cold-boot/safe-area tests; Windows
next (MSVC) runs its units and complete non-device desktop suite through a
Mesa lavapipe software Vulkan ICD with Win32 presentation support. The local
noscript tree is preset-encoded too: `cmake --preset macos-debug-noscript`,
`ctest --preset unit-noscript`. A `pre-push` git
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
  reference script. A script is a **named component KIND** when its file ends in
  `.component.lua` (`player.component.lua` → the component `player`,
  `engine_gocomponent/ScriptComponentRegistry` registers a factory alias per kind on
  project scan): several DIFFERENT scripts attach to one object, each its own container
  key + sandbox, addable in the editor / over MCP / in scenes by kind name; a top-level
  `properties` table auto-exposes designer-tunable fields through the ONE reflection
  registry (Inspector, scene overrides, `self.<name>`, debug protocol, MCP —
  `Docs/lua-api.md#script-components`). Plain `.lua` files are libraries. The low-level
  path-bound `ScriptComponent` kind still works. The historical Python backend
  (`Meta_Python.h`, `core_python/`) has been deleted — recoverable from history and
  the private archive if a real consumer ever appears; don't reintroduce it.
- Everything builds statically during the revival (`ORKIGE_STATIC` is defined globally);
  the old `__declspec` DLL export macros in the prerequisites headers are inert.
- Keep the existing code style when editing old files: tabs, `m`-prefixed members,
  Doxygen-style comments, `#ifndef` include guards with date suffixes.
- File copyright headers read `copyright:	(c) 2009-2026 orkitec` — one range
  spanning the orkige heritage; new files use the standard header block
  verbatim (created/filename/author/notice/copyright, see any engine header).
  The owner consolidated the old kunst-stoff-era files under orkitec on
  2026-07-15, so the whole tree carries the ONE notice.
- Line endings are LF everywhere, enforced by `.gitattributes` (the tree was normalized
  in a dedicated commit on 2026-07-08; the old preserve-CRLF rule is obsolete).
- Commit messages: no `Co-Authored-By` trailers.
- Renderer containment (decided 2026-07; since 2026-07-08 **Ogre-Next is the default
  backend** and classic OGRE the fully supported compatibility flavor): code above the
  render backend goes through the `engine_render` facade — no `Ogre::` outside
  `engine_graphic/`, `engine_render_classic/`, `engine_render_next/`
  and `engine_render/RenderMath.h`. The rule is ENFORCED MECHANICALLY:
  `render_containment_lint` (a unit-labeled ctest running `Util/check_ogre_containment.py`,
  part of the unit and desktop presets) fails on any unsanctioned `Ogre::` spelling in
  code; the sanctioned files/blocks (classic-only zones, math-alias residue, the marked
  app boot blocks) live in `Util/ogre_containment.json` — a new exception needs an entry
  there, with a reason, in the same change. Don't add reliance on features Ogre-Next
  dropped (OGRE material scripts especially — keep materials simple/generated).
- Open-source hygiene: comments and user-facing strings describe the CODE, not the
  development process — no phase, work-package, or task references in comments,
  strings, or docs. Never name competing game engines or other third-party products
  in comments, strings, or docs; describe the behavior and mechanics directly. Commit
  messages MAY reference dev history, but code, comments, strings, and docs must not.

## MCP endpoint (AI-agent editor control)

The editor HOSTS an MCP server itself over Streamable HTTP (which retired the
earlier `Util/orkige_mcp.py` Python stdio sidecar and its `mcp` pip dependency): one
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
JSON-RPC transport in front of the existing command handler, REUSED wholesale: a thin
adapter over `EditorCore` + the `EditorDocument` free functions. Each verb is an
MCP tool with a JSON `inputSchema`; a `tools/call` runs the verb on the handler's
internal DebugMessage request/reply and returns the reply as MCP tool content
(text + `structuredContent`, or `isError`). AUTH: mutations need the
`Authorization: Bearer <token>` header (the editor writes the secret to the token
file; reads are open; no token file ⇒ auth off for dev). Correlation is JSON-RPC's
native `id`. POST-only (no SSE); long ops (play boot) return an accepted result
and are polled via `get_state`. Play control is translated into the ONE existing
player debug protocol — never a second player port. The 79 tools cover the whole
agent dev-loop: scene authoring (project/scene lifecycle, hierarchy CRUD,
get/set_component generically over the reflected property registry, prefabs),
project-file authoring (write/read/list jailed to the project root, import_asset),
UI/animation preview (preview_ui renders a `.oui`; preview_animation renders a
`.oanim` pose + blend on the editor's own clock, no play session),
running (play {scene, target}, list_play_targets, async export_project + build
status in get_state), testing (run_tests/list_tests/get_test_results with
build-errors-first short-circuit) and live debugging (runtime_* state, pause/step,
set_runtime_property/set_cvar/reload_script, reload_ui, screenshot_game) — all mapped onto
existing `EditorCore` methods + the `EditorDocument` free functions. Verified headlessly by the `editor_control` ctest
(a worker thread drives a raw socket through the whole MCP conversation incl. auth
rejection) plus the `JsonTests`/`HttpServerTests` unit tests. Full reference:
`Docs/mcp.md`; `Docs/mcp-workflows.md` is the agent-workflow guide (worked
develop/test/debug walkthroughs with real call sequences).

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
- **Memory/debug** (`core_debug`): the performance instruments — `MemoryManager`
  is the tagged per-frame allocation-counter layer (opt-in seams at the engine's
  own allocation points, relaxed atomics, folded at the player's frame boundary;
  NOT a global new/delete hook) and `ProfileManager` the hierarchical CPU frame
  profiler behind the `OPROFILE`/`OPROFILEFUNC` scope macros (`Profile.h`;
  static-string names, thread-local trees, allocation-free steady state, Debug
  on / Release off until armed). Both stream to the editor over MSG_STATS /
  MSG_PROFILE_DATA and read back over MCP `get_state`/`get_profile`; the trace
  carries per-frame `alloc` + `phases`. `MemorySampler` stays the process-RSS
  number. **Logging** is the always-compiled, runtime-gated diagnostic channel:
  the tagged stream-style macros `oDebugError`/`oDebugWarning`/`oDebugMsg`
  (`DebugMacros.h`) route through a thread-safe per-tag threshold table
  (`LogLevels.cpp`) — levels error/warn/info/debug, gate-before-format so a
  disabled call never evaluates its stream (a relaxed-atomic fast-reject).
  Default error+warn on in every config, info on in Debug, debug off until
  raised; sinks are stderr (what tests grep) + the `LogManager` file (configured
  from XML), and an `oDebugError` also drops a `Breadcrumbs` entry. Per-tag level
  is a live cvar `log.<tag>` (+ `log.default`), so MCP `set_cvar` raises verbosity
  at runtime with no new verb. `SDL_Log` is NOT a diagnostic channel — it stays
  only for selfcheck/demo output whose exact strings a test greps. See
  `Docs/logging.md`.
- Umbrella header: `core_module/OrkigePrerequisites.h` (forward decls, export macros).

**`orkige_engine/`** — the OGRE-facing layer, fully ported to OGRE 14.5 + SDL3 (gated
behind `ORKIGE_BUILD_ENGINE`, ON for all app work). Umbrella: `engine_module/
EnginePrerequisites.h` — backend-NEUTRAL (core prerequisites + Meta + the
`RenderMath.h` alias vocabulary); classic-only TUs use
`engine_module/EnginePrerequisitesClassic.h` (adds the `<Ogre.h>` umbrella).
`engine_graphic/Engine.h` is the central engine object on BOTH render flavors (it
dispatches: classic bootstrapper vs. the facade-only `EngineNext.h` sibling;
`engine:hasUISystem()` returns true on BOTH flavors — gui renders through the
`DrawLayer2D` facade, so it and the ImGui editor run on next as well; the probe
stays only so a hypothetical future UI-less flavor could answer honestly). Classic render
system selection via `ORKIGE_RENDERSYSTEM` env: GL3Plus default; Vulkan renders via
MoltenVK on macOS; GLES2 on iOS/Android (the `-classic` mobile presets); Metal builds but
can't render RTSS content — see Docs/ports.md. The next flavor boots Ogre-Next's Metal RS
on macOS/iOS and its Vulkan RS on Android. `engine_gocomponent` bridges core game objects to the scene
(`TransformComponent`, `ModelComponent`, `SpriteComponent` — the 2D building block: a
textured alpha-blended quad in the XY plane, per-texture generated `Sprite/<tex>`
material, zOrder → render-queue painter's sorting (its header documents the
alpha/sorting rules), `RigidBodyComponent` on Jolt via the
backend-agnostic `engine_physic/PhysicsWorld` (planar 2D mode, `teleport` moves body +
transform even while the sim is `setPaused` — the tile-slide/"move world" API),
`SoundComponent` on OpenAL Soft (fully-buffered WAV/CAF sfx; streamed OGG Vorbis
music rides `engine_sound/MusicStream` — a queued-buffer ring decoded a little at a
time via stb_vorbis, main-thread refill in `SoundManager::update`, owned by the
`SoundManager` music registry so tracks survive scene switches; the Lua `music` table
= play/stop/stopAll/isPlaying/setVolume/getPosition, in the "music" mixer group),
`AnimationComponent`, `CameraComponent` (projection mode + orthoSize + 2D
aspect fit policy `fitMode`/`designWidth`/`designHeight` serialize; ortho =
the 2D camera, also reachable via `Engine::setCameraOrthographic`/`…Fit`),
`LightComponent` (directional/point/spot over the `engine_render` `RenderLight`
facade — reflected `type`/`colour`/`intensity`/`range`/`innerAngle`/`outerAngle`/
`castsShadows`, serialized + Lua + MCP through the ONE property registry, follows
the transform node, both flavors; `RenderWorld::setAmbientHemisphere` adds
two-colour sky/ground ambient — native on next, averaged-flat on classic;
**PBS materials**: a `.omat` text asset — `core_util/MaterialAsset`, pure parser —
feeds `RenderSystem::createMaterial` + `MeshInstance::setMaterial` via
`ModelComponent`'s reflected `material` AssetRef; next = native HlmsPbs metal-rough,
classic = RTSS Cook-Torrance metal-rough — BOTH honor normal + emissive maps
(tolerance parity), plus `alphaTest`/`twoSided` (cutout casters shadow as
cutouts on both flavors) and runtime per-instance accents
(`MeshInstance::setTint`/`setEmissiveBoost`, PROP_TRANSIENT — never
serialized) — `Docs/materials.md`,
`demo_material` + the `material_*_right` probes per flavor;
**image-based lighting** (opt-in, sky-sourced, both flavors):
`RenderWorld::setImageLighting` / `engine:setImageLighting(enabled,
intensity)` adds cubemap specular reflections + a diffuse fill to the
generated PBS materials ON TOP of the analytic lights (never replacing
them; default OFF and byte-stable), sourced from the SKY the enabled
atmosphere shows — ONE path, TWO sources selected automatically: a **skybox**
atmosphere feeds the offline-baked prefiltered chain (`make_sky_assets.py`),
a **procedural** atmosphere feeds a runtime CPU capture of the sky (the pure
`core_util/SkyEnvMap` — a small cubemap synthesized from the atmosphere +
sun with a box-downsampled roughness chain, recaptured on-demand only on a
material sun swing / colour change, never per frame; a tolerance-parity
approximation of the AtmosphereNpr dome on next, exact for the classic
gradient sky); colour/disabled skies have no environment and refuse with one
honest line; the `r.iblQuality` cvar (off/low/medium/high, live re-arm,
pure tier table `core_util/IblPreset.h` capping the chain resolution);
next = the native HlmsPbs reflection map + diffuse-GI env feature, classic
= the RTSS image-based-lighting stage over the same cubemap (GLES2/WebGL1
runtime-gates the `iblReflections` capability bit) — the
`render_facade_selfcheck` image-lighting leg + `IblPresetTests`;
**shadows**: integrated-PSSM texture shadows on BOTH flavors (next native,
classic via the RTSS receiver injected into the generated-material scheme —
`ClassicBackend::applyShadowConfig`); the `r.shadowQuality` cvar
(off/low/medium/high, live re-arm, shared tier table
`core_util/ShadowPreset.h`), per-object `castShadows`/`receiveShadows` on
ModelComponent (+receive on Water), arm/disarm restore-exactly, the
night-dimmed sun skips the pass, GLES2 gates on a runtime depth-texture
capability probe; **baked-mesh terrain** is content, not a facade feature:
`Util/make_terrain_mesh.py` bakes a seeded fBm heightfield into a chunked glTF
`.glb` (per-chunk sub-meshes, 16-bit-index budget, normals + tiling UVs) plus a
tiling ground `.omat`, rendered through this same ModelComponent path —
`make_terrain_mesh_selftest` + `demo_terrain` per flavor;
**animated water**: `WaterComponent` renders the shared engine water plane
(`Util/make_water_mesh.py` → `orkige_engine/media/water/` water_plane.glb +
tiling water_normal.png, registered like the engine font dir + bundled to
exports) with a per-instance scrolling material through the material-facade
siblings `RenderSystem::createWaterMaterial(RenderWaterDesc)` + a cheap per-frame
`setWaterTime` (next = HlmsPbs two scrolling detail normals + fresnel
transparency + deep/shallow colour; classic = transparent Blinn-Phong subset:
one flat tint + scrolling shimmer, logged once); reflected deepColour/
shallowColour/opacity/waveScale/waveSpeed/fresnelPower/normalTexture + sizeX/
sizeZ (material-param animation only, no vertex work, dormant in the editor); NO
screen-space refraction/depth-graded transmission yet (a future desktop knob
gated on a compositor refraction pass — `Docs/materials.md`,
`Docs/render-abstraction.md`) — `make_water_mesh_selftest` + `demo_water` per
flavor;
**sky/fog/day-night atmosphere**: `RenderWorld::setAtmosphere(AtmosphereDesc)`
(pure `core_util/AtmosphereDesc.h`) + `skyDomeSupported()` capability probe +
Lua `engine:setAtmosphere(enabled, r,g,b, density, fog)` — next = native
`AtmosphereNpr` (atmospheric sky dome + HlmsPbs object fog + sun linkage: the
sun is the FIRST directional `RenderLight`, its direction drives the sky, the
atmosphere drives its colour/power; sky material media ships from the
`ports/ogre-next` `Media/Atmosphere`), classic = fixed-function fog + flat sky
clear-colour subset, no sky dome (one log line) — `render_facade_selfcheck`
atmosphere leg, `AtmosphereDescTests`), the Lua
`ScriptComponent` — dormant unless a
runtime ticks GameObjects, so the editor never runs scripts); `engine_input` is SDL3-based
(KC_* keycodes preserved; `isKeyDown` reads the injectEvent-fed state, so synthetic
SDL events work; `getTilt()` = normalized gravity direction, accelerometer-backed via
SDL sensors where one exists, LEFT/RIGHT-key simulated on desktops — wall-clock paced,
so selfchecks poll it condition-driven instead of frame-counting); `engine_gui` (widgets + the engine-owned `UiAtlas`/`UiRenderer` 2D renderer on the `DrawLayer2D` facade; the vendored Gorilla fork is gone) is the runtime UI system, rendering on BOTH flavors, with
atlases generated by `Util/make_gui_atlas.py`; `engine_filesystem` is the
backend-neutral pak-mount (`RenderSystem::mountPak` over `MiniZip`/`PakArchive`,
both flavors — `Docs/filesystem.md`);
`core_debugnet` (in core) carries the editor<->player debug protocol.

**Tools & apps**: `tools/editor` — the Orkige editor (docked ImGui UI, RTT scene panel,
gizmos, undo/redo in the UI-agnostic `orkige_editor_core`, native macOS menu + file
dialogs, play/pause/step/stop spawning `tools/player` with live remote
hierarchy/inspector over the debug protocol; Play targets: desktop, iOS simulators,
adb devices). On macOS the editor builds as a proper `Orkige.app` bundle (Dock
icon generated by `Util/make_editor_icon.py` + iconutil at build time; the
settings inis live in the bundle's `Resources/` via SDL_GetBasePath; Linux keeps
the bare `orkige_editor` executable — ctest reaches both through the target name). Familiar shortcuts: Cmd/Ctrl+P toggles Play/Stop, the Hierarchy has a
filter box, F2/Delete/Cmd+D work from Scene panel AND focused Hierarchy, snap steps are
editable via the toolbar popover, and an interactive launch reopens the last project
(all persisted in `orkige_editor_view.ini`; automated runs are exempt via the
`automatedRun` env probe — they start blank, render vsync-free and never touch the
user's recents). `tools/player` — the standalone runtime (scene/project loader, debug
server). The player CLI contract (`[scene.oscene] [--project <dir>] [--debug-port N]`)
and the runtime side of the debug protocol live in `engine_runtime/PlayerRuntime.h`
(`PlayerArguments` + `PlayerDebugLink`) — the player and native game modules share them.
`samples/`: hello_orkige (feature demo with env-hooked self-checks), jumper
(textured jump-and-run with gui HUD). `projects/` holds .orkproj project folders —
`projects/jumper-lua/` is the jumper reimplemented in pure Lua (ScriptComponent, zero
compiled game code; verified by the player_jumper_lua_selfcheck ctest);
`projects/roller/` is the 2D tier proven end to end — a physics-puzzle prototype
(tilt-gravity ball + sliding-tile "move world" mode; assets AND the .oscene generated by
`Util/make_roller_assets.py`, HUD atlas by `make_gui_atlas.py`; verified by the
player_roller_selfcheck ctest, which probes that a tile slide moves sprites AND
collision bodies while the sim is paused);
`projects/benchmark/` is the **autonomous 3D+2D feature-tour showcase that doubles as a
machine benchmark** — a `LevelManager` sequence of self-running vignette scenes (terrain
vista with a day→night sun arc + PSSM shadows + weather, water lake, night point-light
ramp, 3D-particle swarm, instance field, flat-colour 2D showcase, `.oui`/localisation GUI,
physics cascade with a time-scale hitstop, results card) driven with NO input by one
shared `director.component.lua` (frame-count pacing scaled by the `benchmark.sceneScale`
cvar), each scored by the `BenchmarkRecorder`; everything is generated by
`Util/make_benchmark_assets.py`, run over MCP via `play` + `get_benchmark_results`,
verified by the player_benchmark_vista ctest (both flavors — the classic 2D-composite
RTSS-transient fix cleared the long-sequence fault, see `Docs/benchmark.md`);
`projects/jumper-native/` is the jumper as a **native project module**: manifest
Settings `native.target`/`native.cmakeDir`/`native.buildDir`
(`core_project/NativeModule.h`) mark a project as carrying compiled C++ game code
under `native/`, built as a standalone CMake project against the engine build tree
via `cmake/OrkigeGameModule.cmake` (no installed SDK yet — the helper file IS the
interim contract). **Runs on BOTH render flavors**: the helper reads the engine
tree's flavor from its cache and links THAT flavor's engine closure + defines its
ABI macro (`ORKIGE_RENDER_NEXT` / `ORKIGE_RENDER_CLASSIC`); game code is
flavor-neutral by construction (only facade types, no `Ogre::`). A module tree is
flavor-bound like any build tree (a flavor flip in place FATAL_ERRORs), so the
editor builds into the per-flavor `native/build-<flavor>` and the exporter into
`native/build-export-<flavor>`. In the editor, Play on such a project becomes
compile-on-Play: async incremental cmake build (against this editor's OWN flavor
tree) with `[build]` lines streamed into the Console (Stop cancels, a failed build
stays in edit mode and launches nothing), then the project's own executable runs as
the play process (desktop target only; covered by the
editor_project_native_play / _break ctests, per flavor — their build tree persists
under `projects/jumper-native/native/build-<flavor>`, gitignored, so re-runs build
incrementally).
**Project export** (`Util/orkige_export.py`, editor Build menu): packages a project as a
distributable macOS .app (self-contained: player/module binary + dylib closure + engine
media + project payload; a marker file makes the app boot its bundled project with no
arguments — `PlayerBundle` in `engine_runtime/PlayerRuntime.h`), an iOS-simulator .app or
an Android APK (via `package_apk.sh`; native-module projects are desktop-only). Output:
`<project>/builds/<platform>/`; bundle/package ids come from the manifest Settings
`export.macos.bundleId` / `export.android.package` / `export.ios.bundleId`. Every export
gets a **per-project app icon** (`export.icon` source PNG resized by `Util/orkige_icons.py`
→ macOS `.icns` / iOS loose `CFBundleIconFiles` / Android launcher mipmaps; a neutral engine
default — `Util/make_default_icon.py` → `Util/media/orkige_default_icon.png` — when unset) and
a **launch screen** (iOS `UILaunchScreen` for native resolution, Android `windowBackground`
from `export.launch.background`). Signed **iOS device** builds (`--platform ios`) are gated on
an identity + provisioning profile resolved from CLI/env (NEVER the manifest — only
`export.ios.teamId` is committed; see `Docs/ios-signing.md`). The **store-submittable**
layer adds two more platforms: `android-aab` (a release-signed Android App Bundle via
`tools/player/android/build_aab.sh` — `aapt2 --proto-format` → bundle module →
`bundletool build-bundle` → `jarsigner`, off an `android-release` tree, version from
`export.android.versionCode`/`versionName`) and `ios-ipa` (a distribution-signed `.ipa`
under `Payload/`). Both gate + degrade honestly on this machine's absent developer
credentials (release keystore + passwords / Apple distribution cert + profile, all
machine-local env, NEVER committed; bundletool is a separate download resolved via
`ORKIGE_BUNDLETOOL`) — like the iOS device path, they refuse rather than emit a
half-signed artifact, and stay CLI-only (a headless MCP agent lacks the secrets). See
`Docs/store-release.md`. Covered by the `export_*` ctests
(the macOS ones RUN the exported app from a neutral cwd; `export_android_aab` asserts the
unsigned bundle-module structure) plus the `orkige_icons` /
`make_default_icon` / `orkige_export` (`--selftest`) unit ctests.
The 2012 legacy tools and prebuilt binaries were removed from the tree (recoverable
from history); `Util/*.py` are the live asset generators.

**Docs/**: `Docs/ports.md` (overlay-port rationale), `Docs/upstream/` (OGRE PR
package — submitted as OGRECave/ogre #3667-3669), `Docs/mcp.md` (the MCP
endpoint), `Docs/render-abstraction.md` (the facade design/audit), `Docs/api/`
(the public site's `/api/` class-reference config + footer), `Docs/legal/`
(the site's imprint + privacy notice).

## Feature systems (the 2026 build)

Landed on top of the revival, each verified on both flavors. Where to
look when touching one:

- **Scene model**: GameObject **parent/child hierarchy + active state** (`core_game`;
  `TransformComponent` composes world transforms through the render node graph;
  `GameObjectManager` `ChildIdMap`/`tagIds` indexes). **Prefabs** =
  `core_game/PrefabSerializer` (`.oprefab` subtree assets; instances store `prefabRef`
  + `suppressedChildren` + per-child property overrides; Apply/Revert in the editor).
  **Scene format is v7** — reflection-driven NAMED component fields since v6
  (no positional readers, no per-version field gates; the loader accepts only
  the current version and errors honestly otherwise — clean-cutover policy);
  v7 added per-property prefab overrides. **Object tags** (multi-tag,
  `world.findByTag`). Serialization: `core_serialization` (`ISerializeable` + `XMLArchive`).
- **Asset pipeline**: `core_project/AssetDatabase` = stable IDs via `.orkmeta` sidecars
  (references survive renames; v3 sidecars carry per-platform **texture import
  settings** — base + android/ios/web override slots incl. `format`/`quality`);
  the editor **asset browser** (folder tree, thumbnails, drag-&-drop
  import/instantiate); `Util/make_sprite_atlas.py` + `cook_textures.py`.
  **Export-time GPU texture compression** (`Docs/textures.md`): the dev loop
  always renders raw PNGs; the export cook block-compresses the payload per
  sidecar settings (`format` defaults to `auto` — desktop BCn in `.dds`, iOS
  ASTC / Android ETC2 in `.oitd` on next, PNG on web and the classic GLES2
  mobile flavor; explicit formats validate per platform x flavor and refuse
  impossible pairs). Encoding runs in `tools/texcook` (host CLI over vcpkg
  `ktx` — ASTC encoder + universal-transcoder ETC2/BCn); a cooked texture
  replaces its `.png` (sidecar renamed along, ids keep resolving; the
  backends fall back `.png`→`.dds`/`.oitd`/`.ktx` for bare-name refs).
  Generated glyph/sprite atlases and normal maps stamp `format="none"`
  (`Util/orkige_sidecar.py`). Verified by `texcook_selftest`,
  `cook_textures_selftest`, `player_cooked_textures` (both flavors) and the
  `export_*` payload assertions.
- **2D**: `SpriteComponent`, `SpriteAnimationComponent` (flipbook), `ParticleComponent`
  + the facade `SpriteBatch` (one draw per emitter), an ortho **2D editor mode**.
  **3D particles + weather**: the SAME `ParticleComponent`/`ParticleSim` grows a
  reflected `space3D` mode (default OFF — 2D content stays byte-identical) with
  Vec3 gravity/wind, point/sphere/box emission volumes, world-vs-local space
  (world = weather, particles don't ride a moving emitter) and a velocity-stretch
  factor; rendering is CPU-billboarded camera-facing quads (billboard corners
  from the window camera's view-matrix axes) reusing `SpriteBatch` (world-space
  Vec3 quads in the 3D scene pass), one draw per emitter. Rain/snow are content
  presets over the reflected tunables (`Util/make_particle_textures.py` soft
  dot/streak), mobile-budgeted (hard `maxParticles` cap, allocation-free tick) —
  `Docs/particles.md`, `demo_particles3d` per flavor.
  **Flat-colour organic vector shapes**: `VectorShapeComponent` renders a
  tessellated `.oshape` (agent-authorable text asset, or SVG-cooked via
  `Util/cook_shapes.py`) through the facade `VectorMesh` (SpriteBatch's
  arbitrary-triangle sibling: flat regions share one "VectorFill" unlit
  vertex-colour datablock, and a v3 `texture NAME x y w h [uv]` region is a
  TEXTURED CUTOUT PART — parse-time per-vertex UV projection through the
  authored rect, per-texture tessellator draw runs rendered through the SAME
  generated sprite material/datablock per texture (no feather on cutout art —
  the texture's alpha is the edge), one draw per texture, tint = the fill
  colour; same zOrder painter window as sprites, both flavors; all-flat
  content still renders byte-identically through the one untextured run).
  The pure geometry core is `core_util/VectorTessellator` (bezier flatten +
  earcut triangulation + baked alpha-feather edge for portable AA — FSAA is 0),
  headless-unit-tested; `Util/make_vectorshape_demo.py` writes the
  `projects/vectorshapes/` sample. Editor integration: dropping/importing an
  `.svg` (browser or MCP `import_asset`) cooks it to `.oshape` in-place via
  `cook_shapes.py` (subprocess; the source `.svg` is not kept), and `.oshape`
  assets show a real thumbnail — the tessellated fill CPU-rasterized by the pure
  `core_util/VectorShapeRaster` and uploaded via `createTexture2D`.
  **Soft, deformable organic shapes** (`softBody` on `VectorShapeComponent`,
  both flavors): the rest mesh is tessellated ONCE and skinned to a few contour
  CONTROL POINTS (translation-only linear-blend skinning — the exact formulation
  a future vertex-shader path would consume); per gameplay tick only the control
  points move and the deformed vertices upload through the DYNAMIC
  `VectorMesh::updateVertices` fast path (ManualObject `beginUpdate` on both
  backends — the classic v1 object and the next SCENE_DYNAMIC v2 object; the next
  backend forbids mapping a buffer twice per frame, so the component defers the
  first upload one tick after any `setMesh`). Three drivers, all moving the same
  control points so they compose: per-control-point WOBBLE springs
  (`core_util/WobbleSpring`, snap-to-exact-rest so the shape returns with no
  drift), a physics-driven volume-preserving SQUASH/STRETCH (a sibling
  `RigidBodyComponent` contact squashes along the impact + kicks the wobble; the
  body's velocity stretches along the motion — the physics body stays a rigid
  circle), and same-topology MORPH targets (`.oshape` `morph` blocks;
  `cook_shapes.py --targets` cooks a multi-SVG morph set with a clear
  structure-mismatch error). The deform math is the pure, headless-unit-tested
  `core_util/SoftBodyDeform` (allocation-free per frame; the player selfcheck
  logs a measured per-frame cost — ~4 µs/blob at 72 verts / 16 control points).
  Lua drive via `self.shape` (`impulse`/`playMorph`/`stopMorph`); the wobble/
  squash/morph tunables are reflected properties (inspector/serialization/MCP).
  `projects/vectorshapes/scenes/softbody.oscene` is the sample (falling blob +
  Lua-morphed blob), verified by the `player_softbody_selfcheck` ctest on both
  flavors.
  **Vector clip animation** (`.oanim`, both flavors): a Lottie `.json` cooks to
  the native `.oanim` rig on import (`Util/cook_vector_anim.py`; the source
  `.json` is KEPT beside it and re-cooks on re-import; a document where nothing
  animates cooks to a plain `.oshape`). The pure rig lives in
  `core_util/VectorAnimAsset` (parser) + `VectorAnimEval` (preallocated,
  allocation-free tick; `evaluateAt`/`blendPose`/`composeRegions`;
  `setClip`/`crossFadeTo` clip blending);
  `engine_gocomponent/VectorAnimationComponent` plays it through the facade
  `VectorMesh` dynamic path (playback setters only mutate the evaluator —
  `onUpdateComponent` is the SINGLE per-frame upload site), reflected
  clip/speed/playing/transitionTime props, a `once` clip's end raises a
  VectorAnimationEndedEvent + the `animation.ended` bus event, Lua drive via
  `self.anim` (`play`/`setClip`/`crossFade`/`scrub`/…). Editor: `.oanim`
  thumbnails, the Inspector's animation preview (shown when a `.oanim` or its
  kept `.json` is selected; own clock, CPU raster) and
  the `preview_animation` MCP verb (clip/time/blend → PNG + pose readback).
  Sample: `projects/vectorshapes/scenes/vectoranim.oscene` (idle → one-shot
  hop crossfade, ended-event into Lua), `player_vectoranim_selfcheck` on both
  flavors; grammar + design in `Docs/vector-animation.md`.
- **Game UI** (`engine_gui`, both flavors): the retained widget set (label/
  button/checkbox/slider/select-menu/progressbar/decor/**text-entry**) is
  Lua-authored via `GuiFactory` (`createCheckBox`/`createSlider`/
  `createSelectMenu`/`createDecorWidget`/`createTextEntry` bound — a spriteless
  DecorWidget is a solid scrim for pause overlays). **TextEntry**
  (`GuiTextEntry`): a single-line field on SDL text input — `InputManager`
  routes `SDL_EVENT_TEXT_INPUT`→`TextInputEvent` and owns the
  `startTextInput`/`stopTextInput` session (raises the mobile keyboard);
  `GuiManager` coordinates single-field focus (tap to focus / tap-away or
  Return to blur). Blinking caret, backspace/delete/left/right/home/end, max
  length, dimmed placeholder; the pure UTF-8 edit model is `GuiTextEdit.h`.
  Lua `getText/setText/setPlaceholder/setMaxLength/wasSubmitted` (poll idiom).
  **UI scale**: `Engine::getContentScale()` (SDL display scale) drives
  the dormant `UiGlyph::scale` at gui boot (integer-snapped) and scales
  authored widget sizes in `GuiFactory`, so pixel text/touch targets keep a
  physical size on 2×–3× screens (larger integer font atlas entries in
  `make_gui_atlas.py`). **Safe areas**: `Engine::getSafeAreaInsets()`
  (`SDL_GetWindowSafeArea`, via `engine_util/PlatformWindow`; the app registers
  its SDL window) + the pure `core_util/SafeArea.h` `UiAnchor::place`; scripts read
  `engine:getSafeAreaInsets()` to keep the HUD off the notch/home bar.
  **Localisation**: `core_util/StringTable` (backend-neutral, XLIFF 1.2 (.xlf)
  files, `%%0%%` formatting, config-asset `Settings "localisation"`) with the Lua
  `loc(key[, args…])` accessor (the sole localisation path; the earlier
  Ogre-tied classic localisation service has been removed). MCP readback: `get_safe_area` /
  `get_ui_layout` over `MSG_STATS` safe-area fields + `MSG_UI_LAYOUT`.
  **Real fonts + vector sprites** (`engine_gui/FontAtlas`, both flavors): a
  `.ogui` `[Font.N]` section carrying `ttf <asset>`/`size <designPx>` is a
  runtime TrueType font (vs. `glyph_*` bitmap rects — one format, two builders);
  a `[Sprites]` entry `name svg <asset> <designWidth>` is a vector sprite.
  FontAtlas bakes glyph pages + rasterised SVGs into ONE GPU page at boot at the
  display's integer content scale (crisp, point-filtered, no facade change:
  `RenderSystem::createTexture2D`), exposed through the UNCHANGED
  `UiAtlas`/`UiFont`/`UiGlyph` surface so every widget renders verbatim. Metrics
  are design px (`UiGlyph::scale` multiplies to device px 1:1 with the texels);
  kerning from the font tables. **Lazy glyph paging**: ASCII + Latin-1 bake
  eagerly, any codepoint beyond that bakes on demand into free page space
  (`UiGlyphProvider` + `UiFont::mSparseGlyphs`) — the CJK/Cyrillic `loc()`
  unblocker. The stb_truetype / nanosvg single-file libs are confined to
  `FontBakeImpl.cpp` / `SvgRasterImpl.cpp` (the `StbVorbisImpl.cpp` precedent);
  the pure shelf packer is `FontPacker`. Engine-default font: `orkige_engine/
  media/fonts/Nunito-Regular.ttf` (SIL OFL, verbatim `OFL.txt` beside it),
  registered by the player/editor and bundled to exports under `Media/fonts/`;
  per-project fonts are id-tracked `assets/` referenced by name from a `.ogui`.
  Existing bitmap atlases (jumper/roller/hello) are unchanged. Reference:
  `samples/hello_orkige/media/gui_ttf_demo.ogui` (the `demo_ttf` selfcheck).
  **Nine-slice + tiling**: a `[Sprites]` entry may carry an optional 4-int
  suffix `name x y w h  l r t b` — nine-slice border insets in sprite pixels
  (`UiSprite::sliceLeft/Right/Top/Bottom`); `UiRect` gains a draw mode
  (`Stretch`/`NineSlice`/`Tiled`) whose nine-slice path emits fixed corner
  bands + stretched edges/centre (the pure `UiNineSlice::buildNineSlice`/
  `buildTiled` quad emitters, unit-tested `NineSliceLayoutTests`), staying ONE
  element in the per-screen batch. `GuiDecorWidget`/`GuiButton`
  `setNineSlice(true)`/`setTiled(true)` (Lua) so a panel/button resizes without
  corner distortion; the generator emits insets for panel/button/field sprites.
  **Rect-anchor layout** (opt-in, both flavors): the pure resolver
  `core_util/UiLayout.{h,cpp}` (`LayoutNode` = anchorMin/Max fractions + pivot +
  offsetMin/Max, friendly `anchoredPosition`/`sizeDelta`) turns a parent-rect
  into a child pixel rect; `GuiWidget` gains a `layout` node + `layoutParent`
  and Lua setters (`setParent`/`setAnchors`/`setAnchorPreset`/`setPivot`/
  `setOffsets`/`setAnchoredPosition`/`setSizeDelta`/`setUseSafeArea`).
  `GuiManager` runs an O(n) resolve pass in `onFrameStarted` (before the
  screens rebuild, only when a layout prop changed or the window resized) that
  resolves each opted-in widget against its parent — or the screen ROOT (full
  window or, via `setRootSpace("SafeArea")`, the safe rect, which subsumes the
  manual `+ safe.mLeft` HUD math and folds `UiAnchor::place` in as the
  point-anchor-against-safe-root case). A `LayoutScalePolicy`
  (`setDesignResolution(w,h,match)` + match/shrink/expand) owns the geometry
  reference scale ("design units → window pixels"), kept DISTINCT from
  `UiGlyph::scale` (glyph/pixel density) so the two compose instead of fighting;
  the legacy absolute-pixel path (`scaleAuthoredSize`) is byte-identical, so
  widgets that never touch a layout setter are unchanged (`demo_ui_scale` still
  asserts it). Pure tests `UiLayoutResolverTests`/`ReferenceScaleTests`; the
  `demo_layout` selfcheck exercises an anchored nine-slice panel + a
  parent-relative child + a safe-area re-layout on both flavors.
  **Layout groups + content-size-fit** (opt-in): a widget becomes a group
  (`setLayoutGroup("horizontal"/"vertical"/"grid")` + padding/spacing/childAlign/
  childExpand/grid cell+constraint) that auto-arranges its layout children, and a
  `setContentSizeFit(h,v)` `preferred` axis sizes a node to its content (a group
  to its children, a label to its measured text). Both live in the pure
  `core_util/UiLayout` two-pass resolver (`measurePreferred` bottom-up →
  `assignRects` top-down; nesting anchored rects inside groups inside groups
  works); `GuiManager::resolveLayouts` builds a transient `LayoutItem` forest
  and runs it. **Scroll container**: `GuiScrollView` (`createScrollView`) is a
  clipping viewport whose taller layout child scrolls by drag / mouse wheel — the
  content is offset through the resolver (children stay hit-testable at their
  shifted rects, scroll-offset-aware) and clipped by a per-`UiLayer` `ScissorRect`
  (`clampScroll` is pure/unit-tested; the clip is analytic + backend-identical,
  batch count grows by one per scroll region). Author scroll content widgets with
  the scroll view's z (the clip layer). Pure tests `LayoutGroupTests`/
  `ScrollClipTests`. **Declarative `.oui`** (`GuiFactory::loadLayout` /
  `gui:loadLayout`, both flavors, noscript-safe): a whole screen — widgets
  (incl. checkbox/slider/scrollview), anchors/pivots/offsets, groups, nine-slice,
  scroll — authored as one text file (the backend-neutral, `StringTable`-localised
  successor to the classic `load()`; `@key` text routes through `loc`). The doc
  model `engine_gui/GuiLayout.{h,cpp}` parses/serialises round-trippably
  (`GuiLayoutIoTests`); `samples/hello_orkige/media/settings_screen.oui` + the
  `demo_oui` selfcheck are the acceptance proof (a scrolling settings screen
  loaded from one file, scroll shifts the rects, a scrolled checkbox still
  hit-tests). Agents author `.oui` over MCP `write_project_file` and verify the
  resolve via `get_ui_layout` — no new MCP verb (see `Docs/mcp.md`).
- **Level authoring**: the editor's **Tile Palette** panel arms a paintable
  asset and the **grid-paint tool** (Paint tool, `B`) paints/erases tiles snapped
  to a grid in 2D mode. Two occupant kinds through ONE seam
  (`EditorCore::paintTileAtCell`/`findTileAtCell`/`eraseTileAtCell`,
  `EditorPaintDesc::kind`): a **prefab** tile (instantiates its `.oprefab`
  subtree — open edges become suppressed prefab wall children + a
  `TileComponent.openEdges` stamp, wall-local convention in
  `TileComponent::EDGE_WALL_LOCAL_IDS`) OR a **bare-asset** tile painted straight
  from a **texture** (a grid-cell `SpriteComponent` quad) or an **`.oshape`** (a
  `VectorShapeComponent`), with NO prefab file generated — the tile carries a
  `TileComponent` stamping the source-asset id (`TileComponent.sourceAssetId`).
  The palette lists all three kinds (`wall_block (prefab)` vs bare `grass`);
  bare-tile LOOK propagation is the shared asset itself (edit the texture/shape,
  every painted tile updates — no per-tile prefab to re-apply). The cell size
  comes from a scene `LevelComponent` else the translate snap step, a stroke is
  ONE undo step (`CompositeCommand::mergeWith`), erase/replace is subtree-safe and
  works across kinds (`DeleteSubtreeCommand`). File > **Add Scene to Level
  Sequence** appends the scene to `levels.olevels`. Reachable by agents via the
  MCP verbs `list_paintable_assets` (alias `list_paint_prefabs`) / `paint_asset`
  (alias `paint_prefab`, accepts a texture/shape too) / `erase_cell` /
  `add_scene_to_levels`. Verified by `editor_level_paint` (prefab AND bare
  sprite/shape tiles, mixed grid, paint → save → reload → PLAYS, both flavors) +
  the `editor_control` MCP bare-tile leg. (Future, not built: a bulk "convert
  painted bare tiles to prefab instances" op if bare tiles later need structure.)
- **Physics** (`engine_physic/PhysicsWorld`, Jolt): a data-driven **collision layer
  matrix** (`physics.olayers`), `RigidBodyComponent` layer + **sensor** flag, and
  **contact events** (worker-thread callbacks → mutex queue → main-thread drain →
  `ScriptComponent` `onContactBegin/End` + C++ events).
- **Gameplay**: `engine_input/InputActionMap` (named actions over keys/tilt,
  `input.oactions`); `engine_sound` mixer groups + master; `core_tween`
  (`TweenManager` + `EaseLibrary`); `core_debug/CVarManager` (typed cvars, live-tunable
  over the debug protocol, `cvar.`-prefixed manifest persistence).
- **Persistence**: general per-project save via `core_game/SaveStore` (flat typed
  key→value store — Number/Bool/String, no nesting; atomic temp+rename write to
  the writable app dir under a per-project file name, coexisting with the
  LevelManager progression save; loaded at boot, autosaved at clean shutdown +
  on `flush`). Lua `save` table (`set`/`getNumber`/`getBool`/`getString`/`has`/
  `remove`/`flush` — `set` dispatches on the Lua value type). Crash semantics:
  only a flush reaches disk (breadcrumbs cover the unflushed window). Editor
  never makes one → honest no-op in edit mode.
- **2D camera fit** (`core_util/CameraFit.h`, pure math): `CameraComponent`
  reflected `fitMode` (FM_HEIGHT default / FM_WIDTH / FM_EXPAND) + `designWidth`/
  `designHeight` derive `orthoSize` from the live viewport aspect (re-applied on
  resize); `Engine::setCameraOrthographicFit(mode,w,h)` is the script-driven
  window-camera counterpart on both flavors. Letterbox bars are pure-math only
  (`CameraFit::letterboxRect`) — the facade exposes no viewport-rect control, so
  drawn bars are descoped to games.
- **Juice**: **screen shake** (`engine_graphic/ScreenShake`, both flavors, engine-
  owned like `ScreenFade`): decaying camera-space wobble applied POST-transform to
  the window-camera rig node and restored EXACTLY on finish (recover-then-reapply,
  never fights a follow rig / accumulates); Lua `screen.shake`/`stopShake`/
  `isShaking`, ticked last in the loop. **Time scale** (`core_game/TimeControl`):
  Lua `world.setTimeScale`/`getTimeScale` scales the delta the player loop feeds
  scripts/tweens/physics (0 = hitstop, still renders; input/render/debug stay
  real-time). Both editor-inert (no singleton → no-op).
- **Iteration**: **Lua hot-reload during Play** (`ScriptComponent::hotReload`,
  compile-before-swap; editor watches `scripts/` and sends `MSG_RELOAD_SCRIPT`);
  **`.oui` hot-reload during Play** (`GuiFactory::reloadLayout`/
  `GuiManager::reloadLayout`, both flavors: the editor's project-tree `*.oui`
  watcher — same cadence/lifecycle as the scripts watcher — and the MCP
  `reload_ui` verb both send `MSG_RELOAD_UI`; the player DESTROYS that screen's
  widgets and rebuilds from the fresh file at the frame boundary — clean cutover,
  a parse failure keeps the OLD screen + reports a `[remote]` error, a rebuild
  emits the `ui.reloaded` bus event so scripts re-acquire handles;
  `Docs/gui.md#hot-reload-during-play-oui-iteration`, verified by
  `editor_ui_hotreload` both flavors);
  **level system** (`core_game/Level*`: deferred mid-play scene switch via the
  `LevelManager` pending-load applied at the player-loop frame boundary;
  `levels.olevels`; progression save in `getDocumentsDirectory`).
- **Device polish**: **haptics** (`engine_input/HapticManager`: phone-body
  vibration — iOS UIFeedbackGenerator in `HapticBridgeApple.mm`, Android
  `Vibrator`/`VibrationEffect` over JNI, desktop honest no-op; Lua `haptics`
  table — `play`/`pattern`/`isAvailable`/`setEnabled`; SDL3 has NO device-body
  vibration API, so this is a platform shim); **tilt calibration**
  (`InputManager::calibrateTilt` captures the current pose as neutral — a Z-offset
  applied in `getTilt`, pure math in `core_util/TiltCalibration.h`, persisted
  per-device; Lua `input:calibrateTilt`/`clearTiltCalibration`/`getTiltCalibration`);
  **screen fades** (`engine_graphic/ScreenFade`: a facade-only full-window
  `DrawLayer2D` overlay animated through `EaseLibrary`, both flavors, ticked last;
  Lua `screen` table — `fadeOut`/`fadeIn`/`setFadeColor`/`isFading`/`loadScene`
  which wipes over a deferred scene switch).
- **Performance architecture** (`Docs/performance.md` — the native-fast-path
  rule lives in `Docs/render-abstraction.md`): the reflected **`static`
  mobility flag** on TransformComponent (facade `RenderNode::setStatic`;
  next = SCENE_STATIC memory managers, classic = StaticGeometry region bake
  in `StaticBakeClassic.cpp`; THE MOBILITY CONTRACT: a runtime move warns
  once per node and repairs — dirty-notify on next, demote-out-of-region on
  classic; hierarchy rule static-parent-required, validated; gate cvar
  `r.staticScene`, editor boots it OFF); **sprite-run batching** (pure
  `core_util/SpriteRunPlanner` owns the painter's contract — stable zOrder
  sort, contiguous same-(texture,sampler) runs, dirty-tracked;
  `engine_gocomponent/SpriteBatcher` realizes runs as facade SpriteBatches,
  player-ticked pre-render, editor-inert; gate cvar `r.spriteBatching`);
  classic 3D instancing GATED OUT by verdict (RTSS derives no instanced
  vertex path; next auto-instances natively). Guarded by the per-scene
  **structural budget gate** (`benchmark_budget` per flavor over
  `tests/integration_driver/benchmark_budgets.json` — draw-batch corridors
  + tri ceilings, budgets edited in the same commit as the change that
  moves them), the `player_static_contract`/`player_spritebatch` fixture
  ctests (pixel identity under the toggles, exact counts, the mobility
  probe) and the `SpriteRunPlannerTests`/`StaticFlagTests` units.
- **Character animation** (`Docs/character-animation.md` — both capability
  tables + the 2D taxonomy doctrine): **skinned glTF characters play on BOTH
  flavors** — classic via OGRE's assimp codec, next via `MeshLoaderNext`'s
  skinned road (static imports byte-identical by deferred post-processing;
  the backend-neutral `engine_render/SkinnedRig{,Extract}` extraction is the
  shared semantics both flavors can consume — the two-importer drift alarm
  is the `player_character_rig_selfcheck` per flavor over the GENERATED
  mannequin `Util/make_character_rig.py`); `AnimationComponent` grew
  `crossFadeTo`/weights/animated bounds (reflected, Lua + MCP). 2D
  characters: flipbook + `.oanim` cutout rigs + morph/soft-body ARE the
  house answer (weighted 2D skinning rejected as doctrine), and textured
  cutout parts SHIP on `.oanim`/`.oshape` rigs (the v3 texture-region
  grammar, Lottie image layers cook to textured regions with the images
  materialized beside the `.oanim`, soft-body/morph compose with UVs
  pinned to vertices — `player_cutout_selfcheck` per flavor over the
  generated `projects/vectorshapes` cutout hero).
- **Light budget capability**: `RenderSystem::lightBudget()` +
  `engine:getLightBudget()` — classic 30 (RTSS forward headroom), next 96
  (derived from the clustered-forward `lightsPerCell` bound, constants
  shared with the boot call); the benchmark's lamp ramps cap at the queried
  budget, so each flavor climbs to its real ceiling (`LightBudgetTests`).
- **Crash breadcrumbs**: `core_debug/Breadcrumbs` — an always-on, bounded ring of
  engine events (scene loads, script errors, warnings, boot/shutdown) FLUSHED to
  disk per entry so a hard crash leaves a readable trail; rotated on boot
  (`breadcrumbs.jsonl` → `.prev.jsonl`); the player writes it to the writable app
  dir, the editor reads the survived file over the MCP `get_breadcrumbs` verb.
- **Mobile app lifecycle** (`core_game/AppLifecycle` — the backgrounding contract
  as a pure, headless-unit-tested state machine; the player owns the wiring in the
  poll loop). SDL raises the lifecycle events on iOS/Android only (desktop
  minimizing is NOT a background — desktop behavior is unchanged). On **background**
  (`WILL_ENTER_BACKGROUND`): FLUSH the save store (a backgrounded mobile app may be
  killed silently — the crash-safe autosave point), deliver `onAppPause(self)` to
  scripts, pause the sim (an `advanceWorld` gate like the editor's pause), suspend
  audio (`SoundManager::onInterruptBegin` — tears the AL device down), drop a
  "background" breadcrumb; on `DID_ENTER_BACKGROUND` STOP rendering (mobile GPU work
  in the background = an OS kill — the loop skips `renderOneFrame` until foreground).
  On **foreground** (`WILL_ENTER_FOREGROUND` resumes rendering + audio;
  `DID_ENTER_FOREGROUND`) the sim resumes RUNNING by default and `onAppResume(self)`
  fires so the GAME decides whether to re-pause behind an overlay; "foreground"
  breadcrumb. `TERMINATING`/`LOW_MEMORY` do a final/cheap save flush + crumb. The
  **Android back button** is TRAPPED (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`) and
  delivered as a `KC_WEBBACK` key event (game handles it — default is deliver, never
  exit); the APK activity's `configChanges` already keeps rotation from recreating
  the activity. Transient audio-focus loss WITHOUT a background (a phone call, another
  app grabbing audio) is not separately surfaced by SDL — it is handled at the
  background boundaries. Verified by `AppLifecycleTests` (unit) + the
  `player_lifecycle_selfcheck` ctest (synthetic SDL events through the real loop,
  both flavors).
- **AI control**: the editor hosts an **MCP server over Streamable HTTP** — see the
  MCP section above + `Docs/mcp.md`.
- **Public site + help portal** (`Util/make_help_portal.py` +
  `.github/workflows/pages.yml`): https://orkige.orkitec.com — a generated
  landing page, the searchable docs portal under `/help/` (stdlib-only
  markdown-subset renderer over the committed docs corpus + README,
  hard-failing file:line broken-link gate = the deploy gate), the C++ class
  reference under `/api/` (rendered from the engine headers by the CI-only
  `Docs/api/Doxyfile` tooling — `/api/` is the ONE allowlisted link target the
  generator takes on faith) and footer-linked legal pages (`Docs/legal/`,
  imprint + privacy, out of nav and search by convention). CI redeploys on
  every main push; Help > Orkige Help just opens the published `/help/` URL
  (`HELP_PORTAL_URL` — network required; the editor never generates or serves
  the site, a distributed editor has no repo or python).
  `make_help_portal_selftest` renders the REAL corpus at zero broken links —
  docs rot is a test failure; `check_doxyfile` validates the API config
  against the real tree (skip-77 without the CI-only tool). The portal
  PRESENTS docs, never rewrites them (`Docs/help-portal.md`).
- **Editor scripts** (`tools/editor/EditorScriptHost`, discovery in the
  editor_core lib's `EditorScriptTools`): a project `scripts/<name>.editor.lua`
  is an EDITOR TOOL — a one-shot command in the editor's **Tools** menu (and MCP
  `run_editor_script`), run once in a fresh editor-side sandbox whose `editor.*`
  table routes through the SAME verb handler the MCP endpoint uses
  (`EditorControlServer::dispatchLocalVerb` — the reused internal dispatch seam).
  The whole run folds into ONE undo step (`EditorCore::begin/endScriptTransaction`);
  a tool that errors is rolled back (no partial edits) and reports `file:line`.
  The editor never ticks and never installs the game-runtime Lua tables, so the
  `events` bus is ABSENT from an editor-script sandbox by construction. Noscript:
  the menu shows a disabled note, the project still loads. `border_walls.editor.lua`
  in `projects/roller` is the shipped sample; see `Docs/lua-api.md` (Editor
  scripts). Covered by the `editor_scripts` selfcheck, the `EditorScriptTools`
  units and the `editor_control` `run_editor_script` leg.
- **CONVENTIONS to preserve**: the **config-asset** pattern (project-config files —
  `input.oactions`/`physics.olayers`/`levels.olevels` — are manifest-`Settings`-referenced,
  NOT under `assets/`, NOT id-tracked; bundled to exports via `CONFIG_SETTING_KEYS` in
  `orkige_export.py`); the **canonical player-loop tick order** (fenced block in
  `tools/player/main.cpp`: input → scripts → tweens → physics → deferred-load); the
  **scene teardown hook** (`GameObjectManager::clear`, fenced); the **scriptable-
  component access registry** (a component declares its script surface — `self.<name>`,
  `world.<accessor>(id)`, `getComponent("name")` — in ONE `OSCRIPT_HANDLE` line at its
  meta-export site; `ScriptComponent::populateSelfTable` + `ensureScriptApi` drive
  every surface off that ONE `ScriptRuntime` registry instead of a hand-wired per-type
  block, so a new scriptable component is never silently script-unreachable). The
  backend-neutral
  **property registry** (`core_base/PropertyReflect.h`/`PropertySchema.h`; the
  `OPROPERTY*` macros in the Meta backends register schema + Lua binding in one
  line) IS the single source of property truth — Inspector, scene/prefab
  serialization, the debug protocol and MCP all consume the one schema; never
  hand-wire a per-surface property list.

## CI

GitHub Actions (`.github/workflows/ci.yml`) builds + tests on every push —
**twelve parallel jobs, one per platform x flavor**, so a failure names
itself and every verdict lands as early as its own build allows (public-repo
runners are free; the only cap is 5 concurrent macOS jobs):
**web** cross-builds the wasm player + core test module on Ubuntu (pinned
emsdk, cached; vcpkg binary cache keyed on the chainload wrapper too) and
runs the full web suite — core units under the emsdk's node plus the export
structure/pixel-boot tests through the image's headless Chrome, with a
may-not-skip guard on the boot test;
**linux-next**/**linux-classic** run the full windowed desktop suites under
xvfb (lavapipe / llvmpipe); linux-next adds the **`ORKIGE_SCRIPTING=OFF`**
build + unit gate and the CI-only **ASan + UBSan** tree with the complete
unit + desktop suite (the sanitizer suite runs with ZERO retries).
**android-emulator-next**/**-classic** build the x86_64 emulator player
FIRST (the fail-fast the job exists for), then the host editor, then run
the adb Play test (shipping Android remains arm64-v8a).
**macos-next**/**macos-classic** run the complete non-device desktop suites
on Apple hardware (classic includes the MoltenVK Vulkan runs — brew
molten-vk in the job, the documented driver-tier setup).
**ios-simulator-next** builds the Simulator player first, then
the host editor, then runs the export/Play/boot/safe-area device tests
against a prepared iPhone simulator plus a PRE-WARMED shutdown device (a
hosted runner boots even a warm simulator in 4-6 minutes — the Play
session/phase/ctest budgets are spaced for that, see EditorApp.h);
**ios-simulator-classic** builds the classic Simulator player (fail-fast)
and runs the export structure test with a may-not-skip guard — the
editor-session device tests stay on the next job + local hardware, because
the HOST classic editor is unreliable on hosted virtual GPUs (the
macos-classic finding; the classic device suite runs locally via
`ctest --preset all` on the classic tree).
**windows-next** builds on MSVC and runs the complete desktop suite through
a Mesa lavapipe software Vulkan ICD registered in the REGISTRY (elevated
processes ignore the loader's env overrides) with Win32 presentation
(preset `windows-debug`, x64-windows-static-md triplet,
NOMINMAX/WIN32_LEAN_AND_MEAN globally); **windows-classic** is the build +
headless-unit gate (no software GL on hosted Windows for the windowed set —
the classic windowed gate runs on Linux and macOS). The windowed CI suites
retry a failing test once (`--repeat until-pass:2`); local runs and the
sanitizer suite stay strict so flakes remain visible.
Linux builds with **clang** (`CC/CXX` in the workflow env;
matches the clang-oriented codebase), and needs a few system dev packages the cold
vcpkg build surfaced (autoconf-archive, libltdl-dev, libxtst/libxinerama; SDL's builtin
iconv via the `triplets/x64-linux.cmake` overlay). A `pre-push` hook (`Util/install_git_hooks.sh`) spawns `Util/watch_ci.sh` to report
each push's result.
