# Orkige in the browser (WebAssembly / WebGL)

The classic render flavor compiles to WebAssembly through Emscripten and
renders through WebGL on a page canvas — effectively a **GLES3/WebGL2** target.
Two facts settle the tier from the code:

- OGRE's EGL context creation (`RenderSystems/GLSupport/src/EGL/OgreEGLSupport.cpp`,
  `EGLSupport::createNewContext`) requests **GLES 3.2** first
  (`EGL_CONTEXT_MAJOR_VERSION=3, MINOR=2`) and only walks the major version DOWN
  if `eglCreateContext` fails ("find maximal supported context version"). The ES
  profile — which Emscripten uses (`EmscriptenEGLSupport` is `CONTEXT_ES`) —
  keeps that `=3`, so it asks for an ES3 context.
- The player links `-sMAX_WEBGL_VERSION=2` (`tools/player/CMakeLists.txt`), so an
  ES3 request maps to a **WebGL2** context on any WebGL2-capable browser — which
  is every current browser. WebGL1/GLES2 is only the fallback where WebGL2 is
  genuinely absent, and the engine gates GLES3-level features on a `glsl300es`
  probe for that floor.

So depth textures, MRT and GLSL ES 3.00 are available on web wherever WebGL2
runs (the norm), not just desktop. The GLES3-level facade features gate on the
`glsl300es` probe, so they light up on the WebGL2 context: **IBL reflections**
(the same gate) and **advanced water** — the screen-space refraction
grab-pass and the planar mirror reflection carry a GLSL ES 3.00 program variant
(`RenderSystemClassic.cpp` `waterGlslProfile`) alongside the desktop GL-core
one, so a WebGL2 context renders the full refraction + geometric swell + fresnel
sky rather than the byte-stable flat-shimmer fallback (the GLES2/WebGL1 floor
keeps that fallback). This is asserted on web: `export_web_water` boots a refractive
water fixture headless and requires `screenSpaceRefraction` to answer supported
on the live WebGL2 context with the ES-300 programs building (no fallback
refusal) and the surface rendering through to the orderly shutdown. **LDR
bloom** reaches the WebGL2 context on the same `glsl300es` gate: the classic
bloom compositor's `OgreUnifiedShader.h` quad passes (bright-pass → separable
blur → additive combine) run in the GLSL ES 3.0 profile, so an
`engine:setBloom` scene glows in the browser rather than degrading to the
honest no-op the GLES2/WebGL1 floor still logs (see
`RenderBackend::bloomSupported`; the bloom compositor media
`orkige_engine/media/bloom/classic/` rides the web payload too). This is
asserted on web: `export_web_bloom` boots an emissive-cube fixture headless,
requires the `bloom` cap to answer supported on the live WebGL2 context with no
fallback refusal and a clean shutdown, and pixel-proves the glow — the same
static scene is measurably brighter with bloom on than off (the additive halo
around the emissive cube).

**Dynamic shadows are conditional on a GPU-backed context.** The RTSS
integrated-PSSM shadow pass renders correctly on real GPU-backed WebGL2, but a
**software WebGL rasterizer** — the fallback a GPU-less or GPU-blocklisted
browser silently hands back (Chrome's SwiftShader, Firefox's llvmpipe) — DROPS
the WebGL context the moment the shadow receiver samples the depth shadow map,
which takes the whole game down (a lost context renders no further frame). The
classic backend therefore detects the software rasterizer and refuses the
shadow pass there, with one honest log line, while GPU-backed WebGL2 keeps
shadows. The detection lives in `RenderBackend::dynamicShadowsSupported`
(`ClassicBackend.cpp`): Chrome MASKS the plain `GL_RENDERER` string to
`WebKit WebGL`, so the real driver is read through the
`WEBGL_debug_renderer_info` extension's UNMASKED renderer (e.g.
`ANGLE (Google, Vulkan … SwiftShader driver)`) via a tiny `EM_JS` probe,
matched against `swiftshader`/`llvmpipe`/`softpipe`/`software`. The
`dynamicShadows` capability bit answers false on such a context, so
`engine:supports("dynamicShadows")` and the MCP caps read report it honestly.
The gate is `#ifdef __EMSCRIPTEN__`, so desktop and the classic GLES2 mobile
presets (real device GPUs) are untouched. This is what the CI `export_web_embed_click`
test relies on: CI's headless Chrome has no GPU and always falls back to
SwiftShader, and the benchmark tour there runs its scenes with shadows at the
default quality and completes because the pass is refused rather than crashing
(a GPU-backed browser runs the same tour with shadows visible). A latent RTSS
GLSL-ES-3.0 shadow-shader fix that would make the pass strict-driver-safe (and
so let SwiftShader render shadows too) is an OGRE-port change tracked separately.

The *requested* GL tier itself is code-confirmed; a runtime **tier-assertion
test is still a TODO** (read the GL context version from the boot log or the
caps bitset over the debug protocol) to confirm delivery and catch a
regression. What the suite does hold the tier to meanwhile is its OUTPUT: the
boot test checks boot + clean shutdown + a non-uniform screenshot, the water
and bloom tests require their GLES3-gated features to answer supported and
render, and the parity gate below compares the browser's frames against the
desktop classic player's frames of the same scenes.

One preset builds the whole runtime; one exporter platform packages any
Lua/scene project as a static directory every web server can host as-is.

    cmake --preset web-release                 # configure (vcpkg wasm ports)
    cmake --build --preset web-release         # player + core test binary
    ctest --preset web                         # units under node + export tests
    orkige_export --project projects/jumper-lua \
        --platform web --engine-build build/web-release
    python3 -m http.server -d projects/jumper-lua/builds/web

## Prerequisites

- **emsdk**, user-local (never system-wide), at `~/Development/emsdk` or
  wherever `EMSDK` points:

      git clone https://github.com/emscripten-core/emsdk.git ~/Development/emsdk
      ~/Development/emsdk/emsdk install latest
      ~/Development/emsdk/emsdk activate latest

  Nothing needs to be on `PATH`: the triplet and the preset resolve the
  toolchain through `EMSDK` (defaulted to the path above). Only BUILDING the
  wasm player needs it — packaging a project for the browser does not, so an
  editor that carries a prebuilt browser player exports one on a machine with
  no emsdk at all.
- vcpkg as for every other preset. The wasm dependency set is the classic set
  minus the editor-only ports; `catch2` stays in so the unit suite runs on the
  target.

## How the pieces fit

| piece | role |
| --- | --- |
| `triplets/wasm32-emscripten.cmake` | overlay triplet: static wasm libs, hermetic `/usr/local` isolation, chainload below |
| `cmake/wasm32-emscripten-toolchain.cmake` | the ONE chainload toolchain (ports AND engine): seeds `-fwasm-exceptions`, then includes the emsdk platform file. vcpkg has no emscripten toolchain of its own, so `VCPKG_C(XX)_FLAGS` set in a triplet never reach a compiler here — the wrapper is where ABI-relevant flags live. |
| preset `web-release` | classic backend, Release, tests ON |
| `tools/player/CMakeLists.txt` (Emscripten branch) | player link flags: WebGL2 ceiling, forced FS |
| `tools/player/PlayerContext.h` + `playerIterate` (main.cpp) | the player's world on ONE heap context and the loop body as an iterate callback: the desktop loop calls it in a plain `while`, the browser hands the context to the page's frame callback (`emscripten_set_main_loop_arg`, requestAnimationFrame-paced) — same frame body, same orderly teardown, no stack-suspension instrumentation |
| `engine_util/SDLNativeWindowWeb.cpp` | the native-handle bridge returns null: the page's one canvas is both SDL's window (input) and the GLES2 render surface (OGRE binds it through Emscripten's EGL) |
| `orkige_export --platform web` (@see `tools/exporter/ExportWeb.h`) | packages `<project>/builds/web/`: `index.html` (title/launch background/icon from the manifest), `orkige_player.{js,wasm}`, `icon.png`, `game.pak` + `game.js`. COMPILES NOTHING — the wasm player is a build artifact like every other platform's player and the rest is bytes the exporter arranges, so a browser build packages on a machine with no Emscripten toolchain. |
| `tools/player/web/index.html.in` | the shell page template the exporter fills in |
| `tools/player/web/pak_loader.js` | the data loader, shipped verbatim as `game.js` |

## The payload: one game pak

Everything an exported page needs rides in ONE `game.pak` — the engine's own
zip, the archive `RenderSystem::mountPak` reads. It holds the tree a desktop
bundle keeps in `Resources/`: `Media/` (the classic shader library plus the
engine fonts/water/decals and the bloom/grade compositor media), `project/`
(the shippable project subset, textures cooked for the `web` platform) and the
`orkige_project.txt` marker.

The page's data loader (`game.js`) fetches it and hands the bytes to the
module's filesystem as `/game.pak`, through the runtime methods a
`-sFORCE_FILESYSTEM` build exports for exactly this — `FS_createDataFile` plus
the run-dependency pair, which hold the runtime at `preRun` until the payload
is in place. Nothing reaches into the module's internals.

The player then splits the archive the way the Android player splits an
uncompressed APK:

- **written out as real files** — the small tree read through `fopen`: the
  marker, the project manifest, scenes, scripts, config assets, and the engine
  shader/font media the RTSS and font loaders want as directories;
- **mounted in place** — the bulk game media under the payload's `assets/`,
  each directory its own flat pak mount so files resolve by BARE resource
  name, exactly like the loose-file registration a desktop run does.

One split, one mechanism, two packages: `PlayerBundle::isMountedMediaPath`
(`engine_runtime/PlayerRuntime.h`) is the shared rule and the packaging
prologue that applies it — unpacking the payload, remembering the mounts — is
`GamePlatform::boot` in `engine_runtime/GameHost.cpp`, so every runtime built
on the game host gets it. A page whose module filesystem carries no
`game.pak` boots as a dev module.

## The browser payload inside a packaged editor

A downloaded Orkige packages a browser build. It needs no Emscripten toolchain
to do it — a web export compiles nothing — but it does need the prebuilt player,
so a packaged editor CARRIES one, in a single self-contained directory at its
resource root (`Orkige.app/Contents/Resources/web/`, `share/orkige/web/`
elsewhere):

| in `web/` | from |
| --- | --- |
| `orkige_player.js`, `orkige_player.wasm` | a `web-release` build tree |
| `index.html.in`, `pak_loader.js` | `tools/player/web/`, verbatim |
| `Media/` | the CLASSIC engine media: the shader library, fonts, water, decals and the bloom/grade compositor media the browser player renders through |

**The browser target is flavor-independent.** The browser player IS the classic
flavor (GLES2/WebGL), whatever flavor the editor itself is, so a next-flavored
editor packages a web export out of this classic payload: `exportWeb` takes the
payload's ready-made `Media/` tree as one piece instead of reading a build
tree's. That is why one `web-release` preset serves every editor.

Who stages it: the **packaging** pipeline
(`Util/orkige_nightly_package.py`), never the build — the wasm player is
cross-built by its own toolchain, and no desktop packaging machine has one. So
the payload is composed once on a machine that does and handed over:

    # on a machine with the wasm toolchain, after building the web-release player
    orkige_nightly_package.py --stage-web-payload web-payload \
                              --web-build build/web-release

    # on each desktop packaging machine, which needs no toolchain
    orkige_nightly_package.py --platform macos --build-dir build/macos-release \
                              --web-payload web-payload ...

`--web-build` composes it in place instead, for a machine that has both.
[The nightly](nightly-builds.md#the-browser-player-payload) runs exactly this
handover.

What the editor does with it: `EditorResourceLocator::webPlayer()` probes the
wasm module as the payload's marker, and `planProjectExport` allows a
bundle-sourced `web` export when it is there (`tools/editor/EditorExportPlan.h`
— the one export decision the Build menu and the MCP `export_project` verb both
go through). Without it, a web request is refused in one sentence naming what
is missing. An archive's `VERSION` file records which of the two it is, as
`web-export: bundled` or `web-export: absent`, and its `KNOWN-LIMITATIONS.md`
carries the matching record — a build with no browser player never promises a
browser package.

Proven end to end by `editor_bundle_web` (both flavors, `tests/CMakeLists.txt`):
it stages the payload into a copied app through the packaging tool's own
function, then drives that copy in a clean room — no repository, no engine
tree, a scrubbed `PATH` with no interpreter — to export for the web, and
asserts the artifact set plus a shipped wasm player byte-identical to the one
the app carried. It skips honestly on a machine with no `build/web-release`
tree to stage from.

## Exception handling (the wasm ABI rule)

Everything wasm — every vcpkg port and every engine object — compiles with
`-fwasm-exceptions` (native WebAssembly exception handling; for C objects the
same flag lowers `setjmp`/`longjmp`, Lua's error path, onto wasm unwinding).
A throw crossing a frame compiled without EH support aborts the module, and
mixed SJLJ modes fail at link with an unresolved `emscripten_longjmp` /
`__wasm_longjmp`. The single chainload wrapper enforces the rule; never add a
second EH mode.

## Automation hooks in the browser

The shell page maps query parameters `?env.NAME=VALUE` onto the module's
environment, so every environment probe the native player reads works
unchanged in a browser session, e.g.

    index.html?env.ORKIGE_DEMO_FRAMES=90&env.ORKIGE_DEMO_FPS_LOG=1

frame-limits the run and prints the frame stats at the orderly shutdown. The
runtime's exit code lands in `document.title` (`ORKIGE_EXIT_<code>`) and in
the page's status line. `tests/web/run_export_web.py` uses exactly these:
`export_web_structure` asserts the artifact set, `export_web_boot` drives a
headless Chrome/Chromium (boot marker → clean shutdown → a mid-run screenshot
that must contain an actual scene, pixel-checked). The boot test SKIPs (77)
on machines without a headless browser (`ORKIGE_CHROME` overrides discovery).

## Render parity with the desktop

The browser must render what the desktop renders. That is one gate, on one
seam: **the browser's frames against the DESKTOP CLASSIC player's frames** of
the same scene. The browser IS the classic flavor, so both sides run the same
backend and a divergence can only come from the WebGL2/GLES3/Emscripten tier.

Desktop-**next** versus the browser is the pair a RELEASE is judged on — the
browser ships, the desktop classic flavor does not — and it is the COMPOSITION
of two gates: this one and the cross-flavor gate
(`run_crossflavor_parity_test.py`, next vs classic on the desktop). It is
**measured and pictured on every commit, and deliberately not gated**:

- measured, because the pair matters. `--report-only` prints every band's
  numbers and `--pair-image` writes the two frames as ONE side-by-side picture
  (browser left, desktop right); the `web-parity` job runs both on every commit
  and uploads them, `if: always()`, so the pictures exist even when the gate
  above went red.
- not gated, because its deltas ARE the flavor seam's. The platform seam
  contributes about nothing (below), so on the lake vignette the direct pair
  measures sky 7, terrain 5, water 26 — and 26 is exactly what the cross-flavor
  gate measures for that band between the two desktop flavors. A gate here
  would either restate that at a corridor four times looser than the one on
  this page, or block on a difference the cross-flavor gate already adjudicates
  band by band. A corridor wide enough to hold both seams at once passes a real
  regression in either of them and cannot say which side moved.

The driver is `tests/integration_driver/run_web_parity_test.py`, the sibling of
the cross-flavor one, sharing its diff/clustering diagnosis (`parity_diff`) and
its refusals. What it pins, and why each pin exists:

| pinned | why |
| --- | --- |
| the scene (`lake`, `mirrorlake` benchmark vignettes) | the same content the cross-flavor gate compares, so the two gates' numbers compose. A page has no argv, so the browser side exports a copy of the benchmark project whose `MainScene` is the vignette under test; the desktop side boots that scene file directly |
| 1280x720 on both sides | the browser viewport, its device scale factor and the player's `ORKIGE_WINDOW_SIZE` are one rectangle, so the canvas backing store, its CSS box and the captured page agree and nothing is scaled between the render and the comparison |
| `benchmark.cameraOrbit=0`, `benchmark.rampBudgetMs=100000` | the deterministic capture recipe: no wall-clock camera orbit, no early vignette advance |
| `r.shadowQuality=off` | the classic backend refuses the shadow pass on a software WebGL rasterizer (above), which is what a GPU-less CI browser always gets and a developer's browser never does. Unpinned, the compared image would depend on the machine rather than on the code |

The textures need no pin: the export cook's auto table ships the SOURCE image
on `web` (no compressed format is guaranteed in a browser), so both sides
sample the same bytes and no lossy cook sits inside the comparison.

**Simulated time is already pinned by the engine**: an automated run advances
the world by the fixed `AppHost::AUTOMATED_FRAME_DELTA` tick, so frame N is the
same instant on a fast host and a slow one. The desktop capture is frame-pinned
(the frame-60 framebuffer dump). The browser capture is not — a page carries no
frame counter to wait on, and the canvas composites black the moment the run
exits, so the frame is taken from a live page after a settle. What that costs
is measured rather than assumed: two browser frames of the same run, one settle
apart, differ by at most 1.1 levels per band on the lake vignette and 7.9 on the
mirror-ripple band, and the corridors carry that share.

Both roads to the verdict exist, as for every parity gate:

    # one machine holding a wasm tree, the host classic tree and a browser
    python3 tests/integration_driver/run_web_parity_test.py \
        --repo . --engine-build build/web-release \
        --player-classic build/macos-debug-classic/tools/player/orkige_player \
        --dir /tmp/webparity --scene scenes/lake.oscene

    # or capture each side where it can be captured, and compare the frames
    ... --capture web --engine-build build/web-release --dir shots/web
    ... --capture desktop --player-classic <player> --dir shots/desktop
    ... --compare-shots --shot-web shots/web/web.png \
                        --shot-desktop shots/desktop/desktop.png

The second road is what makes it a CI gate. The `web` job's ctests
(`web_parity_capture_lake`, `web_parity_capture_mirrorlake`) take the browser
frames and assert each is a real render at the pinned rectangle; the
`linux-classic` job captures the desktop frames with the same recipe — and
`linux-next` captures its own with that recipe too, for the reported release
pair; the `web-parity` job downloads all three and compares. **A missing or
empty capture FAILS** — a parity gate that compared nothing must never report
parity, and `--report-only` withholds the corridor verdict alone, never those
refusals. The artifact `web-parity-captures` carries every frame, the diff
images and `pairs/` — the side-by-side pictures, `<scene>_browser_vs_classic`
and `<scene>_browser_vs_next`.

Corridors are measured, and tight because the measurement allowed it: the
browser and the desktop agree to 0-3 levels per band, so the corridors sit at
12 (20 on the mirror-ripple band) rather than the 20-55 the cross-flavor gate
needs for genuinely different backends. The browser's own rasterizer turns out
not to matter at band level — the same page captured through a GPU-backed
WebGL context and through forced SwiftShader agrees to 0 — which is why this
gate pins the viewport and the cvars but not the rasterizer. Every number the
comparison uses is printed on every run, green ones included, so a corridor is
only ever moved with a fresh measurement written beside it.

What the gate does NOT cover, by construction: dynamic shadows (pinned off,
because CI's browser cannot render them at all), and any scene the browser
cannot boot in the time a capture allows.

## Play in Browser (the editor)

The Play toolbar's target picker carries a **Browser (WebGL)** entry once the
web-release preset built the wasm player (greyed with the build hint
otherwise). Play on it is an export-serve-open that becomes a **live debug
session** once the page loads: the editor runs the `web` export through its
async export job (the `[export]` Console lines), serves the artifact
directory on a loopback port through a second instance of the core_debugnet
`HttpServer` (127.0.0.1 only; the wasm module serves as `application/wasm` —
streaming compilation requires it), opens the default browser at the served
URL — and waits for the page to dial the debug link back in.

The link is the ONE editor↔player debug protocol with its direction
reversed: a page cannot listen and cannot speak raw TCP, so the URL carries
`?env.ORKIGE_DEBUG_CONNECT=127.0.0.1:<servePort>` (the shell's `?env.*`
mapping), the wasm runtime dials that endpoint through its plain
`DebugClient` — Emscripten's POSIX-socket emulation wraps the byte stream in
a WebSocket (`binary` subprotocol) — and the serve port answers the upgrade
(RFC 6455 handshake in `core_debugnet/WebSocket.{h,cpp}`, the generic
`HttpServer` connection takeover) and hands the socket to the waiting play
session's `DebugClient` (`adoptWebSocket`). From there it is a desktop-like
session: `[remote]` Console lines, remote hierarchy/inspector, pause/step,
live property writes and cvars, all unchanged. One page per session: a
second tab during the session is refused the upgrade (409) and runs
standalone; once the session ends the serve ends with it, so any later
page gets the honest 404.

Honest boundaries of the browser link:

- **Stop** sends quit over the link; the page's game loop exits cleanly
  (`ORKIGE_EXIT_<code>` in the title) and the closing socket confirms the
  stop. The editor cannot close a tab — the finished page stays open, but
  the **serve ends with the session**: a reload of that tab answers the
  honest 404 instead of restarting the game.
- A page that **never connects** (no browser, tab closed early) times out
  (`BROWSER_PAGE_CONNECT_TIMEOUT_SECONDS`) back to edit mode, and the serve
  ends with the session there too. Closing/refreshing the tab mid-session
  ends the session like a vanished player — and with it the serve, so the
  reloaded page gets the 404, not a standalone restart.
- `screenshot_game` and `record_trace` refuse: the page writes to its
  in-memory filesystem, which never reaches the editor's disk.
- Lua/`.oui` **hot-reload** refuses (and the editor's file watchers stay
  dark): the page runs its packaged export snapshot — stop, re-play, and
  the fresh export picks the edit up.

A later Play re-exports and re-points the one server's doc root; a previous
tab's fetches answer 404 from then on (those artifacts no longer exist).
Agents reach the same flow over MCP: `play { target:"browser" }`, poll
`get_state` for `browser_play_status` (`exporting`→`serving`→`connected`) /
`browser_play_url`, open their own browser at the URL and use the
`runtime_*` verbs (`Docs/mcp.md`). Verified by the `editor_play_browser`
ctest (served artifacts, the no-page degradation and the late-upgrade 409;
SKIPs 77 without the wasm player) and the `editor_play_browser_session`
ctest (a real headless Chrome dials the session in: remote logs, hierarchy,
pause/resume, the honest refusals, stop; SKIPs 77 without the wasm player or
a headless browser). The transport itself is unit-tested browser-free
(`WebSocketCodecTests`, `DebugWebSocketLinkTests`).

## What is different in the browser (v1)

- **Single-threaded.** No pthreads: worker threads would demand
  SharedArrayBuffer and cross-origin-isolation headers from every host page.
  Jolt runs its single-threaded job system; the four host-only test TUs
  (sockets/threads) are excluded from the wasm unit binary with the reason in
  `tests/core/CMakeLists.txt`.
- **The page paces the frames.** The frame loop runs as a per-frame callback
  on the page's requestAnimationFrame cadence (`playerIterate` over the heap
  `PlayerContext`); when the run ends, the callback performs the same orderly
  shutdown the desktop path runs, then the runtime exits with the game's code
  (the `ORKIGE_EXIT_<code>` title contract is unchanged).
- **The debug link dials OUT.** The BSD-socket API compiles but cannot
  listen in a page, so `--debug-port` fails honestly; instead
  `ORKIGE_DEBUG_CONNECT=host:port` (a `?env.*` query param) makes the
  runtime dial the editor — the browser Play session's live link (see
  below). Without it, or when nobody answers the dial, the game runs
  standalone.
- **Saves are in-memory.** The module filesystem is MEMFS: the save store
  works within a session but does not survive a reload (persistent browser
  storage is a future knob).
- **Mesh import only.** The wasm assimp builds with its exporters off (a
  per-port option in the triplet): the runtime never writes meshes. Disabling
  them also drops assimp's 3MF exporter — the one consumer of the standalone
  zip library OgreMain also embeds (OgreZip) — so the module carries exactly
  ONE zip/miniz implementation with no duplicate-symbol clash. Import formats
  (glb/gltf/obj/3mf/...) are unchanged.
