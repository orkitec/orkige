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

Output lands in `build/<preset>/`. Two render flavors implement the same
`engine_render` facade (`engine_render_next/` vs `engine_render_classic/`, one
source tree): **Ogre-Next is the DEFAULT** (`ORKIGE_RENDER_BACKEND=next`, vcpkg
feature `render-next`), classic OGRE the fully supported compatibility flavor.

| Preset family | Target |
|---------------|--------|
| `macos-debug` / `macos-release` (+ `-classic`) | desktop macOS |
| `linux-debug-next` / `linux-release-next` / `linux-debug-classic` | desktop Linux |
| `windows-debug` / `windows-release` / `windows-debug-classic` | desktop Windows (MSVC) |
| `ios-simulator-debug` (+ `-classic`) | arm64 iOS Simulator, `triplets/arm64-ios-simulator.cmake` |
| `ios-device-debug` / `ios-device-release` (+ `-classic`) | arm64 iPhoneOS, `triplets/arm64-ios-device.cmake` |
| `android-debug` / `android-release` (+ `-classic`) | arm64 Android, `triplets/arm64-android.cmake`, NDK 27 via `ANDROID_NDK_HOME`, API 28+ |
| `web-release` | wasm32 via Emscripten, the classic flavor through WebGL |
| `macos-debug-noscript` / `linux-debug-noscript` | `ORKIGE_SCRIPTING=OFF` builds |
| `linux-debug-sanitize` / `linux-debug-tsan` / `macos-debug-tsan` | sanitizer trees |

On mobile, next is the default too: iOS boots Metal, Android boots Vulkan; the
`-classic` mobile presets are the GLES2 flavor. The full per-flavor capability
matrix is `Docs/render-abstraction.md`.

Rules and hazards:

- **Dependencies come exclusively from `vcpkg.json` (manifest mode)** — never
  vendor a library into the tree, never rely on a system-installed one.
- **Build trees are flavor-bound.** Reconfiguring a tree with the other backend
  would silently poison its CMake/vcpkg caches, so the root CMakeLists
  FATAL_ERRORs instead (guard `ORKIGE_RENDER_BACKEND_CONFIGURED`) — delete the
  build dir or use the matching preset.
- **Both flavors must render the SAME image** (WYSIWYG). Games, gui and the
  editor (ImGui on `DrawLayer2D`) all run on both; the `render_backend_parity`
  pixel test enforces it. Classic stays first-class — it owns the runtime
  render-system pick (`ORKIGE_RENDERSYSTEM`), the GLES2 mobile presets, the
  WebGL/web path and the `samples/jumper` C++ sample (gui HUD; the only
  classic-gated `add_subdirectory` in the root CMakeLists).
- **Signing never happens at build time.** Ninja runs no codesign; iOS device
  builds compile and link with no certificate. Real signing happens at export
  (`export.ios.teamId` in the manifest + the
  `ORKIGE_IOS_SIGNING_IDENTITY`/`_PROVISIONING_PROFILE` env seam) —
  `Docs/ios-signing.md`.
- **BUILDING the wasm player needs the user-local emsdk**
  (`triplets/wasm32-emscripten.cmake`); the chainload wrapper
  `cmake/wasm32-emscripten-toolchain.cmake` carries `-fwasm-exceptions` for the
  WHOLE closure because vcpkg silently drops triplet compiler flags on
  toolchain-less platforms. **EXPORTING needs none of it** — a web export
  compiles nothing, so `orkige_export --platform web` runs on a machine with no
  emsdk. Details, incl. the WebGL2/GLES3 tier the player requests:
  `Docs/web-export.md`.
- Pak mounting is backend-neutral: `RenderSystem::mountPak` mounts a zip's
  contents (optionally a prefix-stripped sub-tree — the APK `assets/` case), so
  scenes/textures/sounds resolve like loose files on both flavors, over the
  shared `engine_filesystem/MiniZip` reader — `Docs/filesystem.md`.

Deploying: iOS builds the runtime as `tools/player/OrkigePlayer.app` (SDL3 UIKit
main, media bundled in) — `xcrun simctl boot/install/launch` for the simulator,
`xcrun devicectl device install app` + `... process launch` for hardware.
Android builds `tools/player/libmain.so` (everything incl. SDL3 statically
linked); `tools/exporter/ExportAndroidAssemble.h` assembles + signs the APK
IN PROCESS, spawning javac/d8/aapt2/zipalign/apksigner directly as argv (**no
shell, no Gradle** — the whole command set is decided up front by a pure
planner, so "nothing is handed to a command interpreter" is a unit-tested
property; SDL3's Java glue comes from the vcpkg SDL source, or - packaging
from a fetched payload, where there is no vcpkg - from the Java sources that
payload carries). `orkige_export android-player --engine-build <tree>`
packages the dev player's own APK. Deploy with
`adb install`; emulator AVD `orkige_test` (android-35, arm64) exists. The
manifest Setting `export.android.assets` picks how media rides in the APK
(`stored`, the default: assets stay UNCOMPRESSED so the player mounts its own
APK and reads bulk media in place; `compressed`: deflated, extracted on first
launch).

The editor's Play toolbar has a target picker (desktop / iOS simulators — a
shutdown one is booted via simctl and auto-installed / adb devices + emulators /
Play in Browser). **Play on an iOS device is a deploy-and-run, not a live
session**: USB has no dependency-free debug-port TCP tunnel (unlike the
simulator's shared loopback and Android's `adb forward`), so hardware runs
standalone. The `editor_play_*` ctests cover the flows and skip (exit 77) when
no prepared device is available. Outside the editor,
`python3 Util/orkige_device.py doctor|android|ios` is the one-command
deploy-and-run front door (readiness report, build-if-stale, package via
`orkige_export`, install, launch, stream logcat) — `Docs/device-session.md`.

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

Layout: the `unit` label covers four headless Catch2 executables —
`tests/core/` (`orkige_core_tests`, boots the app singleton set via
`CoreTestEnvironment`), `tests/engine/`, `tests/editor_core/` and
`tests/exporter/` — plus the stdlib-only lint/selftest ctests. The
`integration` tests (registered in `tests/CMakeLists.txt`) reuse the
self-checking apps — hello_orkige demos, editor self-check/resize, player —
which verify themselves and exit non-zero on failure; that exit code is the
contract. Integration tests are registered per flavor with a `_next` /
`_classic` suffix (`editor_control_next`, …), so grep for the base name.

The rule: every change ships with tests that verify it — unit tests for core
logic, a self-check hook wired into ctest for app/runtime behavior. `ctest` must
pass before committing.

The noscript tree is preset-encoded: `cmake --preset macos-debug-noscript`,
`ctest --preset unit-noscript`.

**Local Linux rig** (`Util/linux_rig/`): `run_container.sh` builds the
`orkige-ci-linux` image (ubuntu 24.04, clang, xvfb + Mesa lavapipe/llvmpipe,
the CI-pinned vcpkg) and starts the long-lived `orkige-ci` container with the
repo bind-mounted and build trees/caches in named volumes — a local twin of the
CI Linux jobs for anything macOS can't reproduce. Every linux-* preset works
inside.

- **HAZARD: the repo is bind-mounted, so git inside the container moves the HOST
  HEAD.** The image installs a git GUARD that refuses it; override only
  deliberately with `ORKIGE_CONTAINER_GIT=1`.
- `linux-debug-sanitize` is the one that earns the rig: the ASan/UBSan gate runs
  on **libstdc++**, which exposes memory bugs libc++ (macOS) masks even under a
  local macOS ASan build. Reproduce CI memory-safety findings here, and pass a
  container sanitizer-unit run before pushing any core lifecycle/teardown change.
- On an arm64 host, configure with
  `-DVCPKG_INSTALL_OPTIONS="--clean-after-build;--allow-unsupported"` (ogre-next's
  supports-list has no linux&arm64 entry; `triplets/arm64-linux.cmake` covers the
  triplet).
- Windowed tests run the CI way:
  `xvfb-run -a -s "-screen 0 1280x1024x24 +extension RANDR" ctest ...` with
  `VK_DRIVER_FILES` pointed at the lavapipe ICD.

CI is a hard gate on every PULL REQUEST, and `main` is protected: it moves only
when the whole matrix is green — see the **CI** section at the end of this file.

## Modernization ground rules

- C++20, no boost. Old code being touched gets moved to std equivalents
  (`std::shared_ptr`, `<type_traits>`, range-for, `std::function`, `std::mutex`).
- Renderer target is OGRE 14.x from vcpkg (port from the historical OGRE 1.7 API).
  Window/input target is SDL3 (replaces the abandoned OIS).
- Scripting is Lua-first and LIVE, behind a backend-neutral seam: application code
  talks ONLY to `core_script/ScriptRuntime` (always compiled; `available()` is
  false and errors are honest in `ORKIGE_SCRIPTING=OFF` builds — **both configs
  must keep building**). The sol2 backend (`ScriptManager` + `Meta_Lua.h`) is an
  implementation detail selected in `Meta.h`. **NEVER write raw
  `#ifdef ORKIGE_LUA` outside Meta.h/Meta_Lua.h/Meta_None.h and the ScriptRuntime
  implementation** — the meta macro vocabulary (incl. `OUSERTYPE*`) is complete in
  both backends by design. Lua is the ONLY embedded language; the historical
  Python backend (`Meta_Python.h`, `core_python/`) is deleted — don't reintroduce
  it, and don't embed another interpreter.
- Game behavior lives in project scripts via `engine_gocomponent/ScriptComponent`
  (per-instance sandbox, init/update/shutdown, `self` + the global
  `world`/`shared` tables); `projects/jumper-lua/scripts/player.lua` is the
  reference script. A script is a **named component KIND** when its file ends in
  `.component.lua` (`player.component.lua` → the component `player`;
  `ScriptComponentRegistry` registers a factory alias per kind on project scan),
  so several different scripts attach to one object, each with its own container
  key + sandbox, addable in the editor / over MCP / in scenes by kind name. A
  top-level `properties` table auto-exposes designer-tunable fields through the
  ONE reflection registry. Plain `.lua` files are LIBRARIES, loaded by another
  script with **`script.require("scripts/lib.lua")`** — jailed by `PathJail` to
  a project-relative `.lua` name (exactly the file set a path-bound
  `ScriptComponent` could already run, so it is the same capability, not a new
  one; `load`/`loadfile`/`dofile`/`require` stay denied), read through
  `ResourceAccess` so a library resolves out of a pak/APK in place, cached PER
  SANDBOX (a process-wide module registry would be a second, undeclared sharing
  channel beside `shared`), with a named cycle refusal instead of a blown C
  stack. The low-level path-bound `ScriptComponent` kind still works —
  `Docs/lua-api.md#script-components`.
  A project TESTS its own Lua in Lua: `<project>/tests/*.test.lua` run by
  `orkige_player --project <p> --run-tests [--test-filter <substr>]` against
  the live runtime, over an engine-owned vocabulary that is a C++ string
  constant (so a released player carries it — no file, no repo, no Python),
  exit code as the verdict plus a flush-per-record JSONL artifact
  (`ORKIGE_TEST_REPORT_DIR`). `tests/` is NOT a payload subdirectory — a
  suite never ships — and `*.editor.lua` is stripped from `scripts/` at
  export; both absences are asserted. `Docs/testing.md`.
- Everything builds statically (`ORKIGE_STATIC` is defined globally); the old
  `__declspec` DLL export macros in the prerequisites headers are inert.
- Keep the existing code style when editing old files: tabs, `m`-prefixed members,
  Doxygen-style comments, `#ifndef` include guards with date suffixes.
- File copyright headers read `copyright:	(c) 2009-2026 orkitec` — one range
  across the whole tree; new files use the standard header block verbatim
  (created/filename/author/notice/copyright, see any engine header).
- Line endings are LF everywhere, enforced by `.gitattributes`.
- Commit messages: no `Co-Authored-By` trailers. Enforced by the `commit-msg`
  hook `Util/install_git_hooks.sh` installs (skip once with
  `ORKIGE_NO_COMMIT_MSG_LINT=1`).
- **Renderer containment**: code above the render backend goes through the
  `engine_render` facade — no `Ogre::` outside `engine_graphic/`,
  `engine_render_classic/`, `engine_render_next/` and
  `engine_render/RenderMath.h`. ENFORCED MECHANICALLY by
  `render_containment_lint` (`Util/check_ogre_containment.py`, unit + desktop
  presets); the sanctioned files/blocks live in `Util/ogre_containment.json` — a
  new exception needs an entry there, with a reason, in the same change. Don't
  add reliance on features Ogre-Next dropped (OGRE material scripts especially —
  keep materials simple/generated).
- Two sibling lints enforce more of this file mechanically:
  - `portability_lint` (`Util/check_portability.py`) fails on Windows-macro
    identifiers (near/far/small/interface) and on curated std symbols used
    without their own include (libc++ leaks headers transitively, MSVC does not
    — add the direct `#include`; suppress a false hit with
    `// portability-ok: <reason>`).
  - `doctrine_lint` (`Util/check_doctrine.py` + `Util/doctrine_lint.json`) locks
    the ORKIGE_LUA/NOSCRIPT meta-macro seam, bans competing-product names in
    comments/strings/Docs, gates core+engine raw filesystem access (the
    pre-funnel backlog lives as shrink-only `legacy` entries in the json) and
    requires the standard copyright block across core/engine/tools/tests/samples
    (vendored + generated files sanctioned by name).
- Open-source hygiene: comments and user-facing strings describe the CODE, not the
  development process — no phase, work-package, or task references in comments,
  strings, or docs. Never name competing game engines or other third-party products
  in comments, strings, or docs; describe the behavior and mechanics directly. Commit
  messages MAY reference dev history, but code, comments, strings, and docs must not.
  Docs are PRESENT-TENSE reference: describe how things are and how they work —
  never change-log narration ("now supports", "the old X was retired", "formerly").
  When history encodes a constraint, state the constraint plainly and drop the
  archaeology. This holds for every doc, including `render-abstraction.md`. The
  only sanctioned record-genre exceptions are `Docs/upstream/` and the `ports.md`
  provenance notes (both document external artifacts, not our own current code).

## MCP endpoint (AI-agent editor control)

The editor HOSTS the MCP server itself over Streamable HTTP: one `POST /mcp`
endpoint speaking JSON-RPC 2.0 (`initialize`, `tools/list`, `tools/call`,
notifications). A remote MCP client connects to the running editor's URL — no
command to spawn, no vcpkg/pip dependency (the HTTP/1.1 server and the
nested-JSON codec are hand-rolled in `core_debugnet` on the existing
non-blocking socket layer: `HttpServer` + `Json`). Register manually with
`claude mcp add --transport http orkige http://127.0.0.1:<port>/mcp --header
"Authorization: Bearer <token>"`.

`tools/editor/EditorControlServer.{h,cpp}` is the transport in front of the
existing command handler — a thin adapter over `EditorCore` + the
`EditorDocument` free functions. Each verb is an MCP tool with a JSON
`inputSchema` (the `toolSpecs` table); `tools/call` runs the verb through the
handler's internal DebugMessage request/reply and returns the reply as tool
content (text + `structuredContent`, or `isError`). The tools cover the whole
agent dev-loop — scene and project-file authoring, prefabs, UI/animation
preview, play, export, tests, live debugging and the script-breakpoint loop.
`Docs/mcp.md` is the reference; `Docs/mcp-workflows.md` the worked walkthroughs.

Rules that hold here:

- **Nothing bypasses the verb handler.** A new tool maps onto an existing
  `EditorCore` method or `EditorDocument` free function; it never reaches into
  editor state directly.
- **Play control is translated into the ONE existing player debug protocol** —
  never a second player port.
- POST-only (no SSE). Long ops (play boot, export) return an accepted result and
  are polled via `get_state`. Correlation is JSON-RPC's native `id`.
- AUTH: mutations need the `Authorization: Bearer <token>` header; reads are
  open; no token file ⇒ auth off for dev.
- DEFAULT-ON for an interactive session, OFF for automated runs — no normal test
  opens a socket. An interactive editor takes an EPHEMERAL loopback port and
  writes its token to the writable app dir (`mcp-endpoint.token`). The token is
  minted from the platform's entropy source (`core_util/SecretToken.h`), and
  **every file that carries it is written OWNER-ONLY through the one sink**
  `core_filesystem/FileWriter::beginOwnerOnly` — created empty, restricted while
  empty, written, then renamed, so the secret never sits in a readable file
  (`0600` on macOS/Linux, a protected DACL on Windows; a volume that holds
  neither gets one warn, never a refusal). `Docs/security.md`.
  Pin explicitly with `--mcp-port <N> --mcp-token-file <path>` (aliases
  `--control-port`/`--control-token-file`; env `ORKIGE_MCP_PORT` /
  `ORKIGE_MCP_TOKEN_FILE`, `ORKIGE_CONTROL_*` also honored);
  `ORKIGE_MCP_PORT=0` (or `off`) opts out.
- While a project is open the editor writes the project-scope discovery file
  `<projectRoot>/.mcp.json`, so an agent in the embedded terminal or the project
  cwd finds the editor with no manual registration. **We only ever manage our
  own entry** (marked `x-orkige-managed`) — a foreign or user-authored
  `.mcp.json` / `orkige` server is left untouched (pure merge-or-skip in
  `tools/editor/EditorMcpConfig.{h,cpp}`, reconciled by `EditorMcpConfigFile`).
  Removed on clean shutdown, gitignored, listing-hidden, never exported.

Verified headlessly by the `editor_control` ctest (a worker thread drives a raw
socket through the whole conversation incl. auth rejection) plus the
`core_debugnet` JSON/HTTP unit cases.

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
  on / Release off until armed). Both stream to the editor and read back over
  MCP `get_state`/`get_profile`. `MemorySampler` stays the process-RSS number.
  `CVarManager` holds the typed, live-tunable cvars (`cvar.`-prefixed manifest
  persistence).
- **Logging** (`core_debug`): the always-compiled, runtime-gated diagnostic
  channel — `oDebugError`/`oDebugWarning`/`oDebugMsg` (`DebugMacros.h`) route
  through a per-tag threshold table (`LogLevels.cpp`), gate-before-format so a
  disabled call never evaluates its stream. Per-tag level is a live cvar
  `log.<tag>` (+ `log.default`), so MCP `set_cvar` raises verbosity at runtime
  with no new verb; an `oDebugError` also drops a `Breadcrumbs` entry.
  **`SDL_Log` is NOT a diagnostic channel** — it stays only for selfcheck/demo
  output whose exact strings a test greps. `Docs/logging.md`.
- **Filesystem** (`core_filesystem`): the ONE runtime filesystem facade. Core and
  engine code does not open files directly — `doctrine_lint` gates raw access and
  the pre-funnel backlog is shrink-only. `Docs/filesystem.md`. `DataResource` is
  the read behind the Lua **`data` table**: authored data files (a level table,
  an item list, a dialogue tree, a tuning table) live in `data/` and are read by
  project-relative name through `ResourceAccess` — **never `fopen`**, so one call
  serves a loose file, a mounted pak and an APK entry — jailed by `PathJail` and
  size-capped. `data/` is a `payloadSubdirs()` entry, so it ships with every
  export. Reading is not executing: the sandbox keeps denying
  `load`/`loadfile`/`dofile`/`require`.
- **Other core modules**: `core_project` (project/manifest, `AssetDatabase`,
  `ProjectPaths`, `TextureSamplerTable`), `core_script` (`ScriptRuntime`),
  `core_tween`, `core_http` (`Docs/http.md`), `core_debugnet` (the
  editor↔player debug protocol, plus the hand-rolled `HttpServer`/`Json`/
  `WebSocketConnection` the MCP endpoint rides on).
- Umbrella header: `core_module/OrkigePrerequisites.h` (forward decls, export macros).

**`orkige_engine/`** — the OGRE-facing layer, ported to OGRE 14.x + SDL3 (gated
behind `ORKIGE_BUILD_ENGINE`, ON for all app work).

- **Umbrella headers**: `engine_module/EnginePrerequisites.h` is
  backend-NEUTRAL (core prerequisites + Meta + the `RenderMath.h` alias
  vocabulary); **classic-only TUs use `EnginePrerequisitesClassic.h`**, which
  adds the `<Ogre.h>` umbrella. Never pull the classic umbrella into neutral code.
- **`engine_graphic/Engine.h`** is the central engine object on both flavors; it
  dispatches to the classic bootstrapper or the facade-only `EngineNext.h`
  sibling. `engine_runtime/AppHost` owns the shared app boot. Classic picks its
  render system from the `ORKIGE_RENDERSYSTEM` env var (GL3Plus default; Vulkan
  via MoltenVK on macOS; GLES2 on the mobile/web classic presets — see
  `Docs/ports.md`); next boots Ogre-Next's Metal RS on macOS/iOS and its Vulkan
  RS on Android.
- **`engine_render`** is the facade both backends implement
  (`engine_render_classic/`, `engine_render_next/`) — `RenderSystem`,
  `RenderWorld`, `RenderNode`, `MeshInstance`, `RenderLight`, `RenderTexture`,
  `SpriteBatch`, `VectorMesh`, `LineMesh`, `DrawLayer2D`, `SkinnedRig`.
  `Docs/render-abstraction.md` is the class map + capability matrix;
  `Docs/materials.md` covers the whole look tier authored through it (PBS
  `.omat`, normal/emissive maps, `alphaTest`/`twoSided`, PSSM shadows,
  image-based lighting, output grade, water, sky/fog atmosphere, decals) with
  the per-flavor honesty notes and the `r.*` quality cvars
  (`r.shadowQuality`, `r.iblQuality`, `r.planarReflection`, `r.staticScene`,
  `r.spriteBatching`).
- **`engine_gocomponent`** bridges core game objects to the scene:
  `TransformComponent`, `ModelComponent`, `SpriteComponent`,
  `SpriteAnimationComponent`, `ParticleComponent`, `VectorShapeComponent`,
  `VectorAnimationComponent`, `WorldTextComponent`, `LineComponent`,
  `DecalComponent`, `WaterComponent`, `CameraComponent`, `LightComponent`,
  `AtmosphereComponent`, `RigidBodyComponent`, `SoundComponent`,
  `AnimationComponent`, `BoneAttachComponent`, `ScriptComponent`.
- **`engine_physic/PhysicsWorld`** wraps Jolt behind a backend-agnostic seam
  (planar 2D mode; `teleport` moves body AND transform even while the sim is
  `setPaused` — the tile-slide / "move world" API).
- **`engine_sound`**: `SoundComponent` on OpenAL Soft (fully-buffered WAV/CAF
  sfx), streamed OGG Vorbis music on `MusicStream` (queued-buffer ring, main-thread
  refill in `SoundManager::update`, owned by the `SoundManager` music registry so
  tracks survive scene switches), mixer groups + master.
- **`engine_input`** is SDL3-based (KC_* keycodes preserved). `isKeyDown`, the
  gamepad state and the touch/pointer snapshot all read the injectEvent-fed
  state, so synthetic SDL events work. `getTilt()` is a
  normalized gravity direction (accelerometer where one exists, LEFT/RIGHT-key
  simulated on desktop) — it is WALL-CLOCK paced, so selfchecks must poll it
  condition-driven, never frame-count. Input reaches a game at two levels:
  named INTENT through `InputActionMap` (keys/tilt/gamepad bindings, max-magnitude
  combine) and raw POSITION through the `input` script table (touch, pointer,
  raw keys, pad reads) — `Docs/lua-api.md`. Both take ONE edge snapshot per
  frame, in the tick order's input slot before scripts.
- **`engine_gui`** is the runtime UI system on both flavors (widgets + the
  engine-owned `UiAtlas`/`UiRenderer` 2D renderer on `DrawLayer2D`); atlases come
  from `Util/make_gui_atlas.py`. `Docs/gui.md`.
- **`engine_filesystem`** carries the pak mount (`MiniZip`/`PakArchive`) behind
  `RenderSystem::mountPak` — `Docs/filesystem.md`.

Hazards worth knowing before touching the engine:

- **`ScriptComponent` is dormant unless a runtime ticks GameObjects — the editor
  NEVER runs game scripts.** Anything that must work in edit mode cannot depend
  on a script tick.
- **The editor renders ALL its targets inside ONE `renderOneFrame`** via auto
  compositor workspaces, so a per-target view effect must be
  visibility/render-queue based (e.g. `RenderTexture::setSkyVisible`,
  `MeshInstance::setVisibilityFlags`), **never global-state bracketing**.
- **The next backend forbids mapping a dynamic vertex buffer twice in one
  frame.** Every dynamic-geometry consumer (`VectorMesh`, `LineMesh`, world text,
  3D particles) therefore defers its FIRST upload one tick after a `setMesh`, and
  keeps exactly ONE per-frame upload site.

**Tools & apps**

`tools/editor` — the Orkige editor: docked ImGui UI, RTT scene panel, gizmos,
undo/redo in the UI-agnostic `orkige_editor_core` library, native macOS menu +
file dialogs, play/pause/step/stop spawning `tools/player` with a live remote
hierarchy/inspector over the debug protocol. Panels and workflows are documented
in `Docs/editor.md` (Source Control, asset badges), `Docs/getting-started.md`
(the Scene view **Display dropdown** — overlays, View Mode, Lighting — and the
one-game-view invariant) and `Docs/gui.md` (the Preview panel's Edit UI mode and
the UI Editor panel). Things to know when working on it:

- The **Preview panel** shows the scene through its own `CameraComponent` at real
  device presets (`core_util/DevicePreset.h`), with an `.oui` overlay picker, an
  animate-materials clock (water look-dev without Play), a selected-camera PiP
  inset in the Scene view and the `preview_game` MCP verb. With no camera in the
  scene it falls back to the player's EXACT default camera.
- **Editor-only visuals (grid, gizmos, overlays) are masked out of the game
  image** via the facade visibility-mask route
  (`MeshInstance::setVisibilityFlags` + `RenderTexture::setVisibilityMask`,
  editor bit `0x00400000`). Anything editor chrome must carry that bit.
- **Reserved output dirs** (`builds/`, `.orkige/`, `native/build*`, VCS dirs) are
  excluded from every project walker via `core_project/ProjectPaths.h` — use it
  rather than walking a project tree by hand.
- On macOS the editor builds as an `Orkige.app` bundle (Dock icon from
  `Util/make_editor_icon.py` + iconutil at build time; settings inis live in the
  bundle's `Resources/` via `SDL_GetBasePath`); Linux keeps the bare
  `orkige_editor` executable — ctest reaches both through the target name.
- View state (shortcuts, snap steps, last project, display options) persists in
  `orkige_editor_view.ini`. **Automated runs are exempt** via the `automatedRun`
  env probe: they start blank, render vsync-free and never touch the user's
  recents. Keep that exemption intact for any new persisted state.
- Distribution, updates and nightly packaging: `Docs/editor-distribution.md`,
  `Docs/editor-updates.md`, `Docs/nightly-builds.md`.

`tools/player` — the standalone runtime (scene/project loader, debug server). The
player CLI contract (`[scene.oscene] [--project <dir>] [--debug-port N]`) and the
runtime side of the debug protocol live in `engine_runtime/PlayerRuntime.h`
(`PlayerArguments` + `PlayerDebugLink`) — the player and native game modules
share them.

Other tools: `tools/exporter` (the `orkige_exporter` library + `orkige_export`
CLI), `tools/texcook` (the export-time GPU texture encoder), `tools/shapecook`
(`.svg` → `.oshape`), `tools/animcook` (vector clip cooking).

`samples/`: hello_orkige (feature demo with env-hooked self-checks, both
flavors), jumper (textured jump-and-run with gui HUD — **classic-flavor only**).
`projects/` holds `.orkproj` project
folders, each verified by its own player selfcheck ctest:

| Project | What it proves |
|---------|----------------|
| `jumper-lua/` | the jumper in pure Lua — zero compiled game code |
| `roller/` | the 2D tier end to end: tilt-gravity ball + sliding-tile "move world" (assets and the `.oscene` generated by `Util/make_roller_assets.py`) |
| `benchmark/` | the autonomous 3D+2D feature tour that doubles as a machine benchmark — a `LevelManager` vignette sequence driven with NO input by one shared `director.component.lua`, scored by `BenchmarkRecorder`, generated by `Util/make_benchmark_assets.py`, run over MCP via `play` + `get_benchmark_results` — `Docs/benchmark.md` |
| `gallery/` | the UI widget showroom: every widget kind across tabbed `.oui` screens |
| `vectorshapes/` | flat-colour vector shapes, soft bodies, vector clip animation, cutout rigs |
| `jumper-native/` | the native (compiled C++) game module — see below |
| `example/`, `watertest/` | small fixture projects the editor/player ctests drive |

**Native game modules** (compiled C++ game code; `Docs/native-modules.md`,
`Docs/sdk-pack.md`). Manifest Settings
`native.target`/`native.cmakeDir`/`native.buildDir` (`core_project/NativeModule.h`)
mark a project as carrying C++ under `native/`, built as a standalone CMake
project via `cmake/OrkigeGameModule.cmake`. The engine is consumed as ONE
`find_package(Orkige)` package exporting `Orkige::Core` (OGRE-free) and
`Orkige::Engine`, in two forms the same helper serves: the **build tree**
(`cmake/OrkigePackage.cmake`, no install step — the developer case, tried first)
and the relocatable **SDK pack** (`cmake/OrkigeSdk.cmake`,
`cmake --install <build> --prefix <dir> --component sdk`) — one self-contained
directory (layer-rooted `include/`, the two archives, engine `media/`, the cmake
surface and the `vcpkg/` closure), so game code builds on a machine with no
engine checkout. `NativeModule::resolveEngineSdk` (bound to this build's
constants by `tools/editor/EditorEngineSdk.h`) is the ONE seam compile-on-Play
and export share.

The contracts that make it refuse instead of crash:

- **ABI stamp.** Both targets share one version — a git-INDEPENDENT content
  fingerprint of the engine source surface (`orkige_core/`, `orkige_engine/` and
  the cmake files defining how a module compiles and links), hashed from the
  bytes on disk (`cmake/OrkigeAbiStamp.cmake`). A module does
  `find_package(Orkige <stamp> EXACT REQUIRED)`, so compiling against newer
  headers than the library it links is a HARD CONFIGURE ERROR, never a runtime
  null-deref. Editor and exporter both flow through it. The editor/player/tests
  build inside the engine graph and never drift, so they stay off find_package.
- **ONE CONFIGURATION all the way through** — a module in another build type is
  REFUSED by name (a dependency's headers compile differently per config; on
  MSVC `/MD` vs `/MDd` cannot share an image at all).
- **The compile contract is CAPTURED off the engine targets, never restated** —
  the compile definitions and the compile + link options, both. The package also
  records the PRIVATE definitions and the suite asserts no installed header
  reads one.
- **The target shape belongs to the platform**: a project says
  `orkige_add_game_module(<name> <sources...>)`, **never `add_executable`**
  (`cmake/OrkigeTargetShape.cmake` is the one derivation both pack writer and
  consumer read; a pack refuses a target it was not built for). Where the
  artifact landed is written down in `orkige_module_artifact.txt` (read by
  `NativeModule::executablePath` and the exporter) instead of guessed.
- **The OS floor is pinned** (macOS 14.0 in the presets AND
  `triplets/arm64-osx.cmake`, so engine and closure agree) and inherited by every
  module from the package.
- **We ship the engine, never a toolchain.** The two prerequisites are reported
  as two: a missing pack is "the SDK for this build is not installed"; a missing
  `cmake`/`ninja` names programs to install on the machine.
- A module tree is flavor-bound like any build tree (a flavor flip in place
  FATAL_ERRORs), so each form gets its own tree — `native/build-<flavor>`,
  `native/build-sdk-<flavor>` and the `-export` siblings. Game code is
  flavor-neutral by construction (facade types only, no `Ogre::`); the helper
  takes the flavor from the package and defines its ABI macro
  (`ORKIGE_RENDER_NEXT` / `ORKIGE_RENDER_CLASSIC`).

In the editor, Play on such a project is **compile-on-Play**: an async
incremental cmake build against this editor's own flavor tree, `[build]` lines
streamed into the Console (Stop cancels; a failed build stays in edit mode and
launches nothing), then the project's own executable runs as the play process
(desktop target only — a phone gets the module through the export pipeline).
Packs are built PER TARGET: the desktop hosts and the iOS simulator emit one,
and a CROSS pack additionally ships the cmake toolchain file that tells the
machine's own compiler what to produce (we ship the engine, never a toolchain)
plus what the target's shape needs — the platform entry TU and the Apple plist
template, both owned by `cmake/OrkigeTargetShape.cmake`, so a game's `main.cpp`
stays platform-neutral. Acceptance proofs: `module_abi_mismatch`, `sdk_pack`,
`editor_bundle_native` (a COPIED editor plus a pack builds, plays and packages
`projects/jumper-native` in a clean room denying the repository, the engine build
tree and the vcpkg root) — each per flavor — and
`export_ios_simulator_native_run` (a relocated iOS pack is the export's ONLY
engine source, and the app it produces installs and renders on a simulator).

**Project export** (`tools/exporter/`): the `orkige_exporter` library the editor
LINKS plus the `orkige_export` CLI every export ctest drives. **Build > Export
runs IN PROCESS on a worker thread — no interpreter, no spawned tool.** It
packages a project as a distributable macOS `.app` (self-contained:
player/module binary + dylib closure + engine media + project payload; a marker
file makes the app boot its bundled project with no arguments — `PlayerBundle`
in `engine_runtime/PlayerRuntime.h`), an iOS-simulator `.app`, an Android APK
(assembled in process — `ExportAndroidAssemble.h`) or a web payload. Output
lands in `<project>/builds/<platform>/`; ids come from the manifest Settings
`export.macos.bundleId` / `export.android.package` / `export.ios.bundleId`.
Every export gets a per-project app icon (`export.icon` source PNG resized by
`ExportIcons` → macOS `.icns` / iOS `CFBundleIconFiles` / Android launcher
mipmaps; a neutral engine default `Util/media/orkige_default_icon.png` when
unset) and a launch screen. A native-module project ships the MODULE's own app
instead of the player, so its iOS-simulator export BUILDS the module against an
iOS SDK pack (`--sdk-pack`, the only engine source it needs; Android and the
browser are not there yet).

- **The SDK pack is never a prerequisite for a project with no C++.** A Lua
  game has nothing to compile: it needs the platform's player (fetched, for a
  distributed editor) and — for a device or store build — the signing
  credentials. Three separate tiers, and none ever stands in for another.

- **Signing credentials NEVER live in the manifest** — only `export.ios.teamId`
  is committed. They come from the CLI, the environment, or the editor's
  **Build > Project Settings** window, whose `tools/editor/EditorBuildSettings.h`
  owns the split: the committed `export.*` group in the manifest, the
  machine-local credential group in a PER-PROJECT file under the editor's
  writable state directory (never inside a project tree), and **passwords in
  the OS credential store, never in a file** —
  `tools/editor/EditorSecretStore.h` (macOS Keychain / Windows Credential
  Manager; Linux refuses honestly rather than pull in libsecret, and NO
  platform gets a plaintext fallback), keyed per project and per slot, with
  the order **environment wins, then the vault, then not set** so CI and
  scripted builds never depend on a desktop keyring. The platform backend is
  installed ONLY by an interactive editor launch (`installPlatformSecretVault`
  refuses on the `automatedRun` probe), so no test run can prompt for keychain
  access or read the user's credentials. The credential model is a platform x
  purpose matrix (development and distribution take different identities) with
  three honest cell states: automatic, applied, and shown-but-not-wired
  (`Docs/store-release.md`, `Docs/ios-signing.md`).
- **A phone's player is FETCHED, not carried** (`Docs/device-payloads.md`): a
  released editor packages for iOS Simulator and Android out of a downloaded
  payload, with no repository and no build tree. Three prerequisite tiers stay
  three answers and must not be folded together — the **payload** (a download
  the editor performs), the **platform toolchain** (Android's SDK build tools
  and a JDK, each missing program named with what installs it), and the
  **engine SDK pack**, which belongs to compiled C++ game code ALONE. **A
  project with no C++ never needs a pack, at debug, release or signed**, and no
  message may mention one.
- The store-submittable platforms `android-aab` (the same in-process assembly
  in protobuf form + `bundletool` + `jarsigner`, off an `android-release` tree,
  `bundletool` resolved via `ORKIGE_BUNDLETOOL`) and `ios-ipa` **refuse rather
  than emit a half-signed artifact** when credentials are absent, and stay
  CLI-only (a headless MCP agent lacks the secrets) — `Docs/store-release.md`.
- **The CLI that ships is the EDITOR's** (`Docs/editor-cli.md`). `orkige_export`
  is a development-tree tool and is NOT part of a release, so on a machine
  carrying only a distributed Orkige the export capability lives inside the
  editor process alone. `orkige_editor <subcommand>` is that door:
  `tools/editor/EditorCli.{h,cpp}` is the PURE argv→decision router (in
  editor_core, unit-tested) and `EditorCliRun.cpp` carries it out, before SDL
  video and the render backend exist. Rules:
  - **Two entry points, ONE implementation.** `export` goes through the SAME
    `planExport` → `runPlannedExport` pair the Build menu and MCP
    `export_project` use — never a second export path, and never a spawned
    `orkige_export` (which also keeps the cheap `host-exporter` CI job alive).
    What the editor's door adds is the engine-source resolution only THIS
    installation knows (its build tree / bundled payload / fetched device
    player / installed SDK pack) and the three-tier refusals.
  - **An unrecognised first-word argument EXITS 2, never falls through to the
    GUI** — a typo on a build server used to open a window and hang the job.
    Flags keep their historical harmless-if-unknown behaviour.
  - **A subcommand run IS an `automatedRun`** (the same boolean, not a third
    mode): no view settings, recents, imgui ini, MCP endpoint, IDE lock or
    credential vault. ONE stated exception: headless export READS the
    machine-local per-project build settings for the identity NAMES, so it
    signs identically to Build ▸ Export on that machine (read only; passwords
    stay environment-only).
  - **The editor stays CONSOLE-subsystem on Windows** (`add_executable` with no
    `WIN32`) — that is what makes stdout and the exit code reach a caller.
  - v1 covers `export`, `fetch-payload`, `version`, `changelog`, `help` and
    promises NOTHING that needs a live game world (a scene load reaches
    `RenderWorld` → a window → a GPU, and there is no null render backend).

Covered by the `export_*` integration ctests (the macOS ones RUN the exported app
from a neutral cwd; `export_android_aab` asserts the unsigned bundle-module
structure) plus the `orkige_exporter_tests` unit executable (manifest facts,
setting vocabularies, credential resolvers, the fixed Apple declarations, icon
and texture-cook image arithmetic, payload + zip round-trips) and
`make_default_icon_selftest`.

**Docs/** — the depth tier. This file stays the compact map; anything longer
lives in a doc and is pointed at from here. The full index:

| Doc | Covers |
|-----|--------|
| `getting-started.md` | the first-game walkthrough |
| `render-abstraction.md` | the `engine_render` facade, containment, per-flavor capability matrix |
| `materials.md` | `.omat`, PBS, shadows, IBL, water, atmosphere/sky |
| `meshes.md` | `.omesh` procedural meshes |
| `particles.md` | 2D + 3D particles, weather |
| `vector-animation.md` | `.oshape`/`.oanim` grammar, Lottie import, soft bodies |
| `character-animation.md` | skinned glTF rigs, the 2D character taxonomy |
| `performance.md` | static mobility, sprite-run batching, instancing verdict, budgets |
| `gui.md` | the whole game-UI tier: widgets, `.oui`, layout, styling, visual editor, world text |
| `localisation.md` | XLIFF 1.2, `orkige_loc.py` |
| `sound.md` | procedural sfx (`.sfs`/`.osfx`), the synth model |
| `filesystem.md` | pak mounting, `MiniZip`, the filesystem funnel |
| `textures.md` | import settings, the export-time GPU cook |
| `logging.md` | tags, levels, sinks, the `log.*` cvars |
| `lua-api.md` | the generated Lua reference (script components, libraries, editor scripts) |
| `testing.md` | the project Lua test tier (`tests/*.test.lua`, `--run-tests`) |
| `mcp.md` / `mcp-workflows.md` | the MCP endpoint reference / worked agent workflows |
| `script-debugging.md` | script editor, breakpoints, the debug loop |
| `editor.md` | editor panels (Source Control, asset badges), scene view + level authoring |
| `terminal.md` / `claude-ide.md` | embedded terminal / the IDE protocol |
| `native-modules.md` / `sdk-pack.md` | compiled C++ game modules / the relocatable SDK pack |
| `editor-cli.md` | the editor's headless subcommands (`export`, `fetch-payload`, …) |
| `editor-distribution.md` / `editor-updates.md` / `nightly-builds.md` | shipping and updating the editor |
| `device-payloads.md` | the fetched device players, and the prerequisite tiers per platform |
| `ios-signing.md` / `store-release.md` / `device-session.md` | signing, store artifacts, phone runs |
| `web-export.md` | the wasm player and browser export |
| `http.md` | the engine HTTP client |
| `monetization.md` | the store/ads seam, the provider contract, the simulated provider |
| `benchmark.md` | the feature-tour benchmark project |
| `sanitizers.md` / `fuzzing.md` / `soak.md` / `security.md` | the stability + safety instruments |
| `ports.md` / `vendored-libs.md` | overlay ports, third-party provenance + pinning |
| `help-portal.md` | the published site generator |
| `upstream/` | the OGRE PR package (OGRECave/ogre #3667-3669) |
| `api/`, `legal/` | the site's class-reference config, imprint + privacy |

`Docs/lua-api.md` and `Docs/gui.md` carry GENERATED blocks — never hand-edit
inside a `<!-- GENERATED:... -->` fence. Add a Lua binding or a gui widget and
run `python3 Util/update_docs.py --write`; the `docs_currency` unit ctest fails
on drift.

## Feature systems

Each is verified on both render flavors. This is the map — where the code lives,
what rule it carries, which doc has the depth.

- **Scene model** (`core_game`): GameObject parent/child hierarchy + active
  state (`TransformComponent` composes world transforms through the render node
  graph; `GameObjectManager` keeps `ChildIdMap`/`tagIds` indexes), multi-tag
  objects (`world.findByTag`), and **prefabs** via `core_game/PrefabSerializer`
  (`.oprefab` subtree assets; an instance stores `prefabRef` +
  `suppressedChildren` + per-property overrides, with Apply/Revert in the
  editor). Serialization rides `core_serialization` (`ISerializeable` +
  `XMLArchive`).
  - **The scene format is v7 and the loader accepts ONLY the current version**,
    erroring honestly otherwise — clean-cutover policy, no compat shims. Fields
    are reflection-driven and NAMED: no positional readers, no per-version field
    gates. `SceneSerializer::SCENE_FORMAT_VERSION` is the constant.
  - **Sibling order is scene state**: the child index's `""` entry is the ordered
    ROOT sequence and the serializer emits depth-first, so the `.oscene` DOCUMENT
    ORDER *is* the sibling order — no extra field. The loader resolves parent
    links after the object loop and `setParent` appends, so file order rebuilds
    the arrangement. `GameObjectManager::reorderChild` + the undoable
    `ReorderObjectCommand` drive the Hierarchy drop; MCP `reorder_object` and the
    ordered `list_hierarchy` make it verifiable.
- **Asset pipeline** (`Docs/textures.md`): `core_project/AssetDatabase` = stable
  IDs via `.orkmeta` sidecars, so references survive renames; sidecars also carry
  per-platform **texture import settings** (base + android/ios/web override
  slots). The editor asset browser adds folder tree, thumbnails and drag-&-drop
  import/instantiate. The dev loop always renders raw PNGs; the **export cook**
  block-compresses the payload per sidecar settings, encoding in `tools/texcook`
  (over vcpkg `ktx`), and a cooked texture replaces its `.png` (every reference
  reaches it through the backends' `.png`→`.dds`/`.oitd`/`.ktx` fallback).
  Generated atlases and normal maps stamp `format="none"`
  (`Util/orkige_sidecar.py`).
  **An EXPORTED payload carries no `.orkmeta`** — sidecars are editor
  bookkeeping. The one setting a runtime reads out of them (a texture's sampler)
  is resolved ONCE at export, for the packaged platform, into the payload
  manifest's baked `<TextureSamplers>` block, and components ask the ONE
  `core_project/TextureSamplerTable` (keyed by a texture's bare stem, so the
  cook's rename cannot break it) — one lookup, two sources. Proven by the
  exporter unit suite plus `player_cooked_textures` /
  `player_pak_sampler_selfcheck` and the `export_*` payload assertions.
- **2D**: `SpriteComponent`, `SpriteAnimationComponent` (flipbook),
  `ParticleComponent` + the facade `SpriteBatch` (one draw per emitter), an ortho
  **2D editor mode**. The same `ParticleComponent`/`ParticleSim` carries the
  reflected `space3D` mode for **3D particles + weather** (default OFF, so 2D
  content stays byte-identical): Vec3 gravity/wind, point/sphere/box emission
  volumes, world-vs-local space, velocity stretch, CPU-billboarded camera-facing
  quads through `SpriteBatch`. Mobile-budgeted: a hard `maxParticles` cap and an
  allocation-free tick — `Docs/particles.md`.
- **Flat-colour vector shapes** (`Docs/vector-animation.md`):
  `VectorShapeComponent` renders a tessellated `.oshape` — an agent-authorable
  text asset, or SVG-cooked — through the facade `VectorMesh` (SpriteBatch's
  arbitrary-triangle sibling; flat regions share one unlit vertex-colour
  datablock, a `texture` region is a textured cutout part drawn per texture
  through the generated sprite material). The pure geometry core is
  `core_util/VectorTessellator` (bezier flatten + earcut + a baked alpha-feather
  edge for portable AA — FSAA is 0). **Importing an `.svg` cooks it to `.oshape`
  IN PROCESS** (`engine_gui/SvgShapeCook` over the same nanosvg parser the gui
  atlas uses; the emission tail is the pure `core_util/VectorShapeCook` +
  `VectorShapeAsset::serialize`), so importing a drawing needs NO interpreter and
  a distributed editor works on a machine with no python3. The source `.svg` is
  not kept. `tools/shapecook` is the CLI face (`--targets` cooks a morph set).
  **nanosvg is confined to two TUs** — `engine_gui/SvgShapeCookImpl.cpp` and
  `engine_gui/SvgRasterImpl.cpp`; don't add a third.
  **Soft bodies** (`softBody` on the same component) skin the tessellated mesh to
  contour control points driven by wobble springs, physics squash/stretch and
  morph targets, over the pure `core_util/SoftBodyDeform` — same doc.
- **Vector clip animation** (`.oanim`, `Docs/vector-animation.md`): a Lottie
  `.json` cooks to the native rig on import (`core_util/VectorAnimCook`, in
  process; the source `.json` is KEPT beside it and re-cooks on re-import). The
  pure rig is `core_util/VectorAnimAsset` (parser) + `VectorAnimEval`
  (preallocated, allocation-free tick, clip blending);
  `engine_gocomponent/VectorAnimationComponent` plays it through the `VectorMesh`
  dynamic path — **`onUpdateComponent` is the SINGLE per-frame upload site**;
  playback setters only mutate the evaluator. Editor: thumbnails, the Inspector
  animation preview, and the `preview_animation` MCP verb.
- **Procedural meshes** (`Docs/meshes.md`): `.omesh` is a text list of placed
  parametric shapes with per-shape modifiers; shapes sharing a material merge
  into one draw section. The pure core is
  `core_util/MeshBuilder`+`MeshShapes`+`MeshExtrude`+`MeshAsset` (deterministic,
  honest line-numbered refusals). The 2D operators consume the collider's own
  contour vocabulary (`ShapeCollider::isSolidRegion`/`openLoop`), so a shape
  collides and extrudes over the same outlines. `RenderWorld::createMeshFromData`
  registers a named LIT mesh RESOURCE, so **everything downstream is the
  loaded-`.glb` road** — PBS `.omat`, shadows, static flag, visibility mask,
  instancing, with no procedural special case above the facade; `ensureMeshAsset`
  (flavor-neutral, called from both backends' `createMeshInstance`) is the
  text→resource road, so `ModelComponent.mesh` takes a `.omesh` exactly like a
  `.glb`. Play hot-reload via `MSG_RELOAD_MESH` / MCP `reload_mesh` (parse → drop
  instances → retire resource → rebuild; a broken edit changes nothing).
- **Game UI** (`engine_gui`, both flavors — `Docs/gui.md` is the full
  reference): the retained widget set (label / button / checkbox / slider /
  select-menu / progressbar / decor / text-entry / toggle group / dropdown /
  modal + toast + dialog / tab bar / virtualized list view / scroll view) is
  authorable from Lua via `GuiFactory` or declaratively from a `.oui` file
  (`GuiFactory::loadLayout` — noscript-safe; the doc model is
  `engine_gui/GuiLayout.{h,cpp}`, round-trippable). Pieces worth knowing where
  they live:
  - **Layout** is the pure resolver `core_util/UiLayout.{h,cpp}` — rect anchors
    (anchorMin/Max + pivot + offsets), layout groups, content-size-fit, and a
    `LayoutScalePolicy` design resolution. `GuiManager` runs it in
    `onFrameStarted`, only when a layout property changed or the window resized.
    The legacy absolute-pixel path stays byte-identical, so widgets that never
    touch a layout setter are unchanged.
  - **UI scale vs design scale are DISTINCT and compose**: `UiGlyph::scale` is
    glyph/pixel density (from `Engine::getContentScale()`), the layout policy is
    geometry ("design units → window pixels"). Don't conflate them.
  - **Safe areas**: `Engine::getSafeAreaInsets()` (`SDL_GetWindowSafeArea` via
    `engine_util/PlatformWindow`) + the pure `core_util/SafeArea.h`; a screen can
    resolve against the safe rect with `setRootSpace("SafeArea")`.
  - **Fonts and sprites** bake into ONE GPU page at boot at the display's integer
    content scale (`engine_gui/FontAtlas`): TrueType fonts from a `.ogui`
    `[Font.N]` section, rasterised SVG sprites, nine-slice insets, and lazy glyph
    paging for codepoints beyond Latin-1 (the CJK/Cyrillic `loc()` unblocker).
    **The single-file libs stay confined to one TU each** —
    `FontBakeImpl.cpp` (stb_truetype), `SvgRasterImpl.cpp` +
    `SvgShapeCookImpl.cpp` (nanosvg), `StbVorbisImpl.cpp` (stb_vorbis). Engine-default font:
    `orkige_engine/media/fonts/Nunito-Regular.ttf` (SIL OFL, `OFL.txt` beside
    it), bundled to exports under `Media/fonts/`.
  - **Text style** is ONE vocabulary on `GuiWidget`
    (`setFontIndex`/`setTextColour`/`setTextScale`, `.oui` keys
    `font`/`textColor`/`textScale`), pushed into owned text elements by the
    single `onTextStyleChanged` hook, so every surface reads the SAME resolved
    state. A `[Style NAME]` section is a bundle applied STYLE-FIRST with the
    widget's own keys overriding (the merge is the pure `engine_gui/GuiStyle`).
  - **Localisation**: `core_util/StringTable` (XLIFF 1.2 `.xlf`, config-asset
    `Settings "localisation"`) with the Lua `loc(key[, args…])` accessor — the
    SOLE localisation path. `Docs/localisation.md`, tooling `Util/orkige_loc.py`.
  - **World-space text**: `engine_gocomponent/WorldTextComponent` renders 3D
    billboard labels through the SAME baked font page, laid out by the pure
    `engine_gui/WorldTextLayout` into camera-facing `SpriteBatch` quads.
  - Agents author `.oui` over MCP `write_project_file` and verify the resolve via
    `get_ui_layout` — no UI-specific MCP verb is needed.
  `projects/gallery/` is the widget showroom that gates the tier.
- **Visual `.oui` editor** (`Docs/gui.md`): the Preview panel's **Edit UI mode**
  IS the canvas (GuiManager is a singleton, so the one preview render path is the
  canvas) — click-select, anchor-preserving drag/resize pinned to the ONE
  UiLayout resolver, selection outlines and grips clipped to the canvas image
  rect. The tool surface (widget tree, properties, anchor gizmo,
  align/distribute, add/delete, undo/redo/save) is the dockable **UI Editor**
  panel, fed the one edit session through `UiEditorPanelLink`. Saves flow through
  `GuiLayout::serialize` (a no-op save is byte-identical), so **the file stays
  the interface** — MCP needs no new verb and Play hot-reload fires like any text
  save.
- **Procedural sound** (`Docs/sound.md`): an sfx can be a PARAMETER file instead
  of a recording — the sfxr standard model, read from the binary `.sfs` the free
  authoring tools of that family write, or from the line-based text twin `.osfx`
  an agent writes with `write_project_file`. ONE `SfxDesc`, two codecs — never
  two models; `preset` seeds an archetype and explicit directives override under
  a two-pass parse, so line order cannot change meaning. Synthesis is the pure
  `core_util/SfxSynth` plugged into `SoundUtil::loadSoundData` — **the ONE place
  a `.wav` is decoded** — so a parameter file IS a sound file to
  `SoundComponent`, Lua, mixer groups and positional audio with no API change,
  and a malformed one leaves a registered-but-SILENT source instead of throwing
  into game code. Implemented from the format documentation with no code taken
  from any implementation (provenance in `Docs/sound.md`). Editor: Audition /
  Stop / Export WAV (`core_util/WavWriter`, convenience only — the runtime plays
  the parameters) / Generate / per-parameter rows / Save-as-`.osfx`, plus
  Create > New Sound.
- **Level authoring** (`Docs/editor.md`): the **Tile Palette** panel arms a
  paintable asset and the **Paint** tool (`B`) paints/erases grid-snapped tiles in
  2D mode. Two occupant kinds — a prefab tile or a bare texture/`.oshape` tile
  with no prefab file — go through ONE seam
  (`EditorCore::paintTileAtCell`/`findTileAtCell`/`eraseTileAtCell`), each tile
  stamped with a `TileComponent`. A stroke is ONE undo step; erase/replace is
  subtree-safe across kinds. Agents reach it with `list_paintable_assets` /
  `paint_asset` / `erase_cell` / `add_scene_to_levels`.
- **Physics** (`engine_physic/PhysicsWorld`, Jolt): a data-driven **collision
  layer matrix** (`physics.olayers`), `RigidBodyComponent` layer + **sensor**
  flag, and **contact events** (worker-thread callbacks → mutex queue →
  main-thread drain → `ScriptComponent` `onContactBegin/End` + C++ events — the
  world is never mutated from a physics worker). **Shape colliders**
  (`ST_SHAPE`) take collision from a tessellated `.oshape`'s outer contours (pure
  `core_util/ShapeCollider`), defaulting to the sibling `VectorShapeComponent`'s
  shape with a reflected `shapeAsset` override: static/kinematic get the true
  concave mesh prism, dynamic the convex hull (a concave dynamic body degrades
  with one warn). Holes and scale sit out v1; soft bodies collide as their rest
  shape; the Scene Colliders overlay draws the real contour.
- **Gameplay**: `engine_input/InputActionMap` (named actions over
  keys/tilt/gamepad, `input.oactions`); `engine_sound` mixer groups + master; `core_tween`
  (`TweenManager` + `EaseLibrary`); `core_debug/CVarManager` (typed cvars,
  live-tunable over the debug protocol, `cvar.`-prefixed manifest persistence).
  The Lua surface for all of it is `Docs/lua-api.md`.
- **Persistent objects** (`core_game`): a `persistent` flag on GameObject
  (serialized as an optional side attribute, so untouched scenes stay
  byte-identical) carries an object through a level switch with its WHOLE live
  state — components, render node, physics body, script sandbox —
  via `GameObjectManager::clearExceptPersistent`, the fenced teardown hook's
  persistence-aware sibling, which never destroys a survivor. A persistent parent
  keeps its subtree; a persistent child of a dying parent re-roots; an arriving
  duplicate id loses to the survivor (one warn line). Running tweens/timers still
  die with the outgoing scene (v1 limit).
- **Dynamic lines** (`engine_render/LineMesh`): the facade's 3D dynamic hairline
  primitive (strip/segments, depth-test toggle, the `VectorMesh` beginUpdate fast
  path) — hairline-only; thickness would need quad expansion.
  `engine_gocomponent/LineComponent` is the authored polyline; the Lua **`draw`
  table** is immediate-mode debug lines/boxes/spheres with TTLs (one engine-owned
  allocation-free collector → one mesh per frame, player-ticked, editor-inert,
  zero cost when unused).
- **Persistence**: per-project save via `core_game/SaveStore` — a flat typed
  key→value store (Number/Bool/String, no nesting), atomic temp+rename write to
  the writable app dir, loaded at boot and autosaved at clean shutdown and on
  `flush`; it coexists with the LevelManager progression save. Lua `save` table.
  **Crash semantics: only a flush reaches disk** (breadcrumbs cover the unflushed
  window). The editor never makes one — honest no-op in edit mode.
- **2D camera fit** (`core_util/CameraFit.h`, pure math): `CameraComponent`'s
  reflected `fitMode` (FM_HEIGHT default / FM_WIDTH / FM_EXPAND) plus
  `designWidth`/`designHeight` derive `orthoSize` from the live viewport aspect,
  re-applied on resize; `Engine::setCameraOrthographicFit` is the script-driven
  window-camera counterpart. Letterbox bars are pure math only
  (`CameraFit::letterboxRect`) — the facade exposes no viewport-rect control, so
  drawing bars is the game's job.
- **Juice**: **screen shake** (`engine_graphic/ScreenShake`) applies a decaying
  camera-space wobble POST-transform to the window-camera rig node and restores
  it EXACTLY on finish (recover-then-reapply, so it never fights a follow rig or
  accumulates); ticked last in the loop. **Time scale**
  (`core_game/TimeControl`) scales the delta the player loop feeds
  scripts/tweens/physics (0 = hitstop, still renders); input, render and debug
  stay real-time. Both are editor-inert.
- **Iteration**:
  - **Lua hot-reload during Play** — `ScriptComponent::hotReload`,
    compile-before-swap; the editor watches `scripts/` and sends
    `MSG_RELOAD_SCRIPT`.
  - **`.oui` hot-reload during Play** — the editor's project-tree watcher and the
    MCP `reload_ui` verb both send `MSG_RELOAD_UI`; the player destroys that
    screen's widgets and rebuilds from the fresh file at the frame boundary. A
    parse failure keeps the OLD screen and reports a `[remote]` error; a rebuild
    emits the `ui.reloaded` bus event so scripts re-acquire handles
    (`Docs/gui.md`). `.oanim` and `.omesh` reload the same way.
  - **Level system** (`core_game/Level*`): a deferred mid-play scene switch via
    the `LevelManager` pending-load applied at the player-loop frame boundary;
    `levels.olevels`; progression save in the documents directory.
  - **Live scene mirror during Play**: `MSG_SCENE_TRANSFORMS` streams a ~15Hz
    whole-scene local-transform delta over the ONE debug link, and
    `tools/editor/PlayMirror` (pure, unit-tested) snapshots the authored poses
    once, drives the matching render nodes and restores them EXACTLY on
    Stop/crash/mid-play-save. **The edit document is never dirtied.**
    Runtime-spawned objects are the documented v1 skip.
- **Script editor + Lua debugger** (`Docs/script-debugging.md`, editor + MCP): an
  embedded code editor on the `ports/imgui-color-text-edit` overlay port
  (commit-pinned like imgui — **bump the two ports as a COUPLED PAIR**, it
  includes `imgui_internal.h`). Per-kind highlighting and **live parse
  diagnostics through each format's own parser** (`ScriptRuntime::checkSyntax`
  compiles without running; tinyxml2 for the XML/XLIFF kinds; `.omat`/`.oui` wrap
  their own parsers) — `EditorTextDiagnostics` in editor_core is the one seam.
  **Completion comes from the ONE reflection/script-surface registry**
  (TypeManager + PropertySchema + `OSCRIPT_HANDLE` + live
  `ScriptRuntime::globalNames`/`globalMemberNames` introspection) — never a
  hand-kept list.
  The debugger: gutter breakpoints persisted per project in
  `<project>/.orkige/breakpoints` (`ScriptBreakpointStore` is the one truth for
  gutter, session push and MCP), continue/step in/over/out in the docked Debug
  panel, call stack and frame-scoped locals/upvalues. Runtime side is seam-clean
  behind `ScriptRuntime` (**ALL `lua_sethook`/C-API stays inside the sol2
  backend**; pure decisions in `core_script/ScriptDebugCore.h`): the line hook
  installs ONLY while breakpoints or a step exist (zero cost otherwise); a hit
  BLOCKS in-hook while `PlayerDebugLink::serviceBreakPump` services debug
  commands and quit, and **DEFERS every other message to the frame boundary so
  the world never mutates mid-script**; script chunks load under their
  project-relative names; client loss mid-break auto-resumes (wedge-proof). All
  additive messages on the ONE debug protocol. The wasm player refuses
  breakpoints honestly (its main thread cannot block); noscript refuses honestly
  and keeps building.
- **Embedded git tooling** (editor-only, git CLI, no libgit2; `Docs/editor.md`):
  `tools/editor/EditorGit.{h,cpp}` is the ONE seam — a pure porcelain-v2 status
  parser + badge model over repo ops issued as `git -C` argv through an
  injectable runner (`runProcessCaptured` merges stdout+stderr so hook rejections
  and push errors surface verbatim). Consumers off the ONE shared status
  snapshot: the script editor's diff gutter, the Source Control panel and the
  asset browser's dirty dots. Three deliberately distinct severities: document
  **Cancel** (reload disk) / gutter **Revert Hunk** (baseline, undoable) / panel
  **Discard Changes** (committed, destructive, confirmed). Honest silence outside
  a repo and in automated runs. **DELIBERATELY no MCP mutation verbs — agents
  never commit, and a tool would launder that prohibition.**
- **Embedded terminal** (`Docs/terminal.md`; editor-only): real shell sessions in
  the editor — a POSIX pty and Windows ConPTY behind the ONE `EditorTerminalPty`
  seam, the VT screen on the `ports/libvterm` overlay port (**libvterm confined
  to `EditorTerminalScreen.cpp`**; a `setResponder` seam answers DA/CPR queries
  so query-driven shells never stall). Pure decisions (key encoder, follow-tail,
  grid hit-test, selection) live in `EditorTerminalSession`, unit-tested
  headlessly. Sessions inherit the MCP discovery env, so an agent launched inside
  finds the editor with zero setup. Hazards this code exists to avoid:
  - **Input is QUEUED, never truncated.** A tty accepts only ~1 KB of pending
    input, so `TerminalPty::write` hands bytes to the pure FIFO
    `TerminalInputQueue` and the frame boundary offers the remainder
    (`flushPendingWrites`). Dropping a tail strands the app mid-sequence, and a
    bracketed paste missing its closing `ESC[201~` makes the shell swallow every
    later keystroke — the interrupt included.
  - macOS un-swaps ImGui's Cmd↔Ctrl at the event layer so **Cmd+C never reaches
    the shell as SIGINT**.
  - On Linux a process-wide X error guard (`SDLNativeWindowLinux.cpp`) keeps the
    inherently racy clipboard-answer BadWindow from killing any SDL-hosted app.
- **Claude IDE protocol** (`Docs/claude-ide.md`; editor-only, interactive
  sessions): the editor announces itself as an IDE — it serves MCP over a
  WebSocket UPGRADE on its OWN ephemeral loopback port (a second
  `core_debugnet` `HttpServer` + `WebSocketConnection`, separate from the MCP
  control endpoint and its token) and writes the owner-only discovery lock
  `~/.claude/ide/<port>.lock` carrying that port's own auth token.
  The active document is STICKY (focusing another panel never blanks the agent's
  context) and paths travel with forward slashes on every platform. Terminal
  children get `CLAUDE_CODE_SSE_PORT`/`ENABLE_IDE_INTEGRATION`, so `/ide` inside
  the embedded terminal connects to THIS editor. Default-on for interactive
  launches, off for automated runs; `ORKIGE_CLAUDE_IDE=0` opts out.
- **Device polish**: **haptics** (`engine_input/HapticManager` — iOS
  `HapticBridgeApple.mm`, Android `Vibrator`/`VibrationEffect` over JNI, desktop
  honest no-op; SDL3 has no device-body vibration API, so this is a platform
  shim); **tilt calibration** (`InputManager::calibrateTilt` captures the current
  pose as neutral, pure math in `core_util/TiltCalibration.h`, persisted
  per-device); **screen fades** (`engine_graphic/ScreenFade` — a facade-only
  full-window `DrawLayer2D` overlay animated through `EaseLibrary`, ticked last,
  with `screen.loadScene` wiping over a deferred scene switch).
- **Performance architecture** (`Docs/performance.md`; the native-fast-path rule
  lives in `Docs/render-abstraction.md`): the reflected **`static` mobility
  flag** on TransformComponent (facade `RenderNode::setStatic`; gate cvar
  `r.staticScene`, which the editor boots OFF) and **sprite-run batching** (the
  pure `core_util/SpriteRunPlanner` owns the painter's contract, realized by
  `engine_gocomponent/SpriteBatcher`; gate cvar `r.spriteBatching`). Classic 3D
  instancing is GATED OUT by verdict. Two rules:
  - **THE MOBILITY CONTRACT**: moving a static node at runtime warns once per
    node and repairs (dirty-notify on next, demote-out-of-region on classic); the
    hierarchy rule is static-parent-required and validated.
  - **Budgets are edited in the same commit as the change that moves them** —
    `tests/integration_driver/benchmark_budgets.json`, guarded per flavor by the
    `benchmark_budget` gate (draw-batch corridors + tri ceilings).
- **Character animation** (`Docs/character-animation.md` — both capability tables
  and the 2D taxonomy doctrine): **skinned glTF characters play on BOTH flavors**
  (classic via OGRE's assimp codec, next via `MeshLoaderNext`'s skinned road);
  the backend-neutral `engine_render/SkinnedRig{,Extract}` extraction is the
  shared semantics both consume, and `player_character_rig_selfcheck` is the
  two-importer drift alarm over the generated mannequin
  (`Util/make_character_rig.py`). `AnimationComponent` carries
  `crossFadeTo`/weights/animated bounds. **For 2D characters the house answer is
  flipbook + `.oanim` cutout rigs + morph/soft-body — weighted 2D skinning is
  rejected as doctrine.**
- **Light budget capability**: `RenderSystem::lightBudget()` /
  `engine:getLightBudget()` — classic 30 (the forward per-pass headroom), next 96
  (the clustered-forward per-cell bound), each from the constant its own backend
  boots with. The benchmark's lamp ramps cap at the queried budget, so each
  flavor climbs to its real ceiling.
- **Crash breadcrumbs**: `core_debug/Breadcrumbs` — an always-on bounded ring of
  engine events (scene loads, script errors, warnings, boot/shutdown) FLUSHED to
  disk per entry, so a hard crash leaves a readable trail; rotated on boot
  (`breadcrumbs.jsonl` → `.prev.jsonl`). The player writes it to the writable app
  dir; the editor reads the survived file over MCP `get_breadcrumbs`.
- **Mobile app lifecycle** (`core_game/AppLifecycle` — the backgrounding contract
  as a pure, headless-unit-tested state machine; the player owns the wiring in
  its poll loop). SDL raises these events on iOS/Android only — **desktop
  minimizing is NOT a background**, and desktop behavior is unchanged.
  - On `WILL_ENTER_BACKGROUND`: FLUSH the save store (a backgrounded mobile app
    may be killed silently — this is the crash-safe autosave point), deliver
    `onAppPause(self)` to scripts, pause the sim, suspend audio
    (`SoundManager::onInterruptBegin`), drop a breadcrumb.
  - On `DID_ENTER_BACKGROUND`: **STOP rendering** — mobile GPU work in the
    background is an OS kill, so the loop skips `renderOneFrame` until foreground.
  - `WILL_ENTER_FOREGROUND` resumes rendering + audio; on `DID_ENTER_FOREGROUND`
    the sim resumes RUNNING by default and `onAppResume(self)` fires so the GAME
    decides whether to re-pause behind an overlay.
  - `TERMINATING`/`LOW_MEMORY` do a final/cheap save flush + crumb.
  - The **Android back button is TRAPPED** (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`)
    and delivered as a `KC_WEBBACK` key event — the game handles it; the default
    is deliver, never exit.
  - Transient audio-focus loss without a background (a phone call) is not
    separately surfaced by SDL; it is handled at the background boundaries.
- **AI control**: the editor hosts the MCP server — see the MCP section above and
  `Docs/mcp.md`. Reachability over MCP is part of shipping a feature.
- **Public site + help portal** (`Util/make_help_portal.py`;
  https://orkige.orkitec.com): a generated landing page, the searchable docs
  portal under `/help/` (a stdlib-only markdown-subset renderer over the
  committed docs corpus + README, with a hard-failing file:line broken-link
  gate), the C++ class reference under `/api/` (rendered from the engine headers
  by the CI-only `Docs/api/Doxyfile` tooling — `/api/` is the ONE allowlisted
  link target the generator takes on faith) and footer-linked legal pages. The
  per-push deploy is the `site` job in `ci.yml`; `pages.yml` is a manual
  redeploy lever. Help > Orkige Help just opens the published `/help/` URL
  (`HELP_PORTAL_URL`) — **the editor never generates or serves the site**, since
  a distributed editor has no repo and no python. `make_help_portal_selftest`
  renders the REAL corpus at zero broken links, so **docs rot is a test
  failure**; `check_doxyfile` validates the API config against the real tree.
  **The portal PRESENTS docs, never rewrites them** — `Docs/help-portal.md`.
- **Editor scripts** (`tools/editor/EditorScriptHost`, discovery in editor_core's
  `EditorScriptTools`): a project `scripts/<name>.editor.lua` is an EDITOR TOOL —
  a one-shot command in the editor's **Tools** menu (and MCP `run_editor_script`),
  run in a fresh editor-side sandbox whose `editor.*` table routes through the
  SAME verb handler the MCP endpoint uses
  (`EditorControlServer::dispatchLocalVerb`). The whole run folds into ONE undo
  step (`EditorCore::begin/endScriptTransaction`) and **a tool that errors is
  rolled back** — no partial edits — reporting `file:line`. The editor never
  ticks and never installs the game-runtime Lua tables, so the `events` bus is
  ABSENT from an editor-script sandbox by construction. Under noscript the menu
  shows a disabled note and the project still loads.
  `projects/roller/scripts/border_walls.editor.lua` is the shipped sample;
  `Docs/lua-api.md`.
- **CONVENTIONS to preserve** — the load-bearing patterns. Break one and
  something goes silently wrong:
  - **The property registry is the single source of property truth.**
    `core_base/PropertyReflect.h`/`PropertySchema.h`; the `OPROPERTY*` macros in
    the Meta backends register schema + Lua binding in one line. Inspector,
    scene/prefab serialization, the debug protocol and MCP all consume that ONE
    schema — **never hand-wire a per-surface property list.** The registry
    supports SCHEMA INHERITANCE (TypeManager records the OParent chain and
    composes base-first with by-name shadowing).
  - **The scriptable-component access registry**: a component declares its script
    surface (`self.<name>`, `world.<accessor>(id)`, `getComponent("name")`) in ONE
    `OSCRIPT_HANDLE` line at its meta-export site.
    `ScriptComponent::populateSelfTable` + `ensureScriptApi` drive every surface
    off that ONE `ScriptRuntime` registry, so a new scriptable component is never
    silently script-unreachable.
  - **Component `enabled` is a base-system feature**, not a per-component bool.
    The ONE base-declared `enabled` reaches every component, and disabling means
    the honest per-kind thing (renderables hide, lights go dark, rigid bodies
    leave the sim and re-enter at rest, sounds stop, particles stop emitting
    while live ones drain, animations pause). Object-active and
    component-enabled funnel through the SINGLE `applyEffectiveEnabled` suspend
    path. Components that cannot honestly disable opt out via `supportsDisable`
    (no lying checkboxes). `true` serializes as silence, so untouched scenes stay
    byte-identical. **Don't add an ad-hoc `visible` bool** — `enabled` is the one
    vocabulary (the Lua `setXVisible` aliases are kept for compatibility).
  - **The config-asset pattern**: project-config files (`input.oactions`,
    `physics.olayers`, `levels.olevels`) are referenced from the manifest
    `Settings`, live OUTSIDE `assets/` and are NOT id-tracked. They reach an
    export through `configSettingKeys()` in `tools/exporter/ExportSettings.cpp` —
    add a new config asset there or it will not ship.
  - **The canonical game-loop tick order** is a FENCED block in
    `advanceGameWorld` (`engine_runtime/GameHost.cpp`): input → scripts →
    tweens → physics → deferred-load. It lives in the engine rather than the
    player because compiled game code gets it instead of re-deriving it.
  - **`engine_runtime/GameHost`** owns the packaging prologue (APK/browser
    payload, media and writable directories, orientation and back-button
    policy, abort diagnostics) and the CALLBACK-shaped frame loop every plain
    loop is expressed in terms of — a browser page owns the frame cadence, so
    a blocking `while` inside `main` cannot exist there. The player is its
    first consumer; a desktop module may still write its own main, on mobile
    and web the harness is structural.
  - **The scene teardown hook** (`GameObjectManager::clear`) is fenced too;
    `clearExceptPersistent` is its persistence-aware sibling.

## CI

GitHub Actions (`.github/workflows/ci.yml`) builds + tests as **fifteen jobs**,
mostly parallel, so a failure names itself and every verdict lands as early as
its own build allows. **A change is verified on its BRANCH, through a pull
request, and `main` only moves once all fourteen gating jobs are green** —
`main` is a protected branch requiring them. `pull_request` verifies a branch;
`push` is restricted to `main` and verifies the merge, so a branch with an open
PR never runs the matrix twice. The `site` job is pinned to a push on `main`:
every other job renders a VERDICT on a change, that one PUBLISHES, and it must
never fire for a branch under review.

Protection deliberately does NOT require a branch to be up to date before
merging — with a matrix this slow, strict mode makes every merge invalidate
every other open PR's run. Admins are not bound by the checks, so a genuine
emergency still has a door.

Throughput, not correctness, is the usual constraint: public-repo runners are
free, but the ACCOUNT's concurrent-job ceiling is what actually paces a queue of
open PRs (macOS is separately capped at 5). A pile of branches can starve the
one PR that unblocks the others - cancelling runs for branches that must be
rebased anyway is the cheap lever. The jobs:

| Job | What it gates |
|-----|---------------|
| `linux-classic` / `linux-next` | the full windowed desktop suites under xvfb (llvmpipe / lavapipe); `linux-next` adds the `ORKIGE_SCRIPTING=OFF` build + unit gate |
| `linux-sanitizer` | CI-only ASan + UBSan tree, complete unit + desktop suite |
| `linux-tsan` | ThreadSanitizer tree, headless unit gate only (windowed sets are too noisy under TSan) |
| `host-exporter` | builds `orkige_export` on Linux and uploads it — the browser export needs a host exporter the wasm tree cannot build |
| `web` (needs `host-exporter`) | cross-builds the wasm player + core test module (pinned emsdk) and runs the full web suite: core units under node, export structure + pixel-boot through headless Chrome, may-not-skip guard on the boot test |
| `site` (needs `web`) | the per-push site deploy (see the help-portal bullet) |
| `android-emulator-next` / `-classic` | build the x86_64 emulator player FIRST (the fail-fast the job exists for), then the host editor, then the adb Play test |
| `macos-next` / `macos-classic` | the complete non-device desktop suites on Apple hardware (classic includes the MoltenVK Vulkan runs — brew molten-vk in the job) |
| `ios-simulator-next` | Simulator player, then host editor, then the export/Play/boot/safe-area device tests against a prepared iPhone simulator plus a PRE-WARMED shutdown device |
| `ios-simulator-classic` | classic Simulator player (fail-fast) + the export structure test with a may-not-skip guard |
| `windows-next` | MSVC build + complete desktop suite through a Mesa lavapipe software Vulkan ICD with Win32 presentation (preset `windows-debug`, `x64-windows-static-md`, NOMINMAX/WIN32_LEAN_AND_MEAN globally) |
| `windows-classic` | build + headless-unit gate (no software GL on hosted Windows) |

Standing facts and hazards:

- A hosted runner boots even a warm iOS simulator in 4-6 minutes — the Play
  session/phase/ctest budgets are spaced for that (see `EditorApp.h`).
- Editor-session device tests stay on `ios-simulator-next` + local hardware: the
  HOST classic editor is unreliable on hosted virtual GPUs. Run the classic
  device suite locally with `ctest --preset all` on the classic tree.
- Shipping Android is arm64-v8a; the emulator jobs are x86_64 only.
- Windowed CI suites retry a failing test once (`--repeat until-pass:2`); local
  runs and both sanitizer jobs stay strict so flakes remain visible.
  `Util/tsan_suppressions.txt` covers only non-Orkige worker-thread races —
  `Docs/sanitizers.md`.
- Linux builds with **clang** (`CC/CXX` in the workflow env) and needs system dev
  packages the cold vcpkg build surfaced (autoconf-archive, libltdl-dev,
  libxtst/libxinerama; SDL's builtin iconv via the `triplets/x64-linux.cmake`
  overlay).
- `.github/workflows/nightly.yml` packages and publishes the editor —
  `Docs/nightly-builds.md`. `pages.yml` is a manual site redeploy lever only.
- A `pre-push` hook (install once per clone: `Util/install_git_hooks.sh`) spawns
  `Util/watch_ci.sh` detached, which polls the push's runs and reports via macOS
  notification + `~/.orkige/ci-watch-<sha>.log` (a failure includes the failing
  steps' log tail). Skip once with `ORKIGE_NO_CI_WATCH=1 git push`. When a CI
  failure lands, fix it promptly — a red required job blocks everyone's
  confidence in the suite.
