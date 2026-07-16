# Orkige in the browser (WebAssembly / WebGL)

The classic (GLES2) render flavor compiles to WebAssembly through Emscripten
and renders through WebGL on a page canvas. One preset builds the whole
runtime; one exporter platform packages any Lua/scene project as a static
directory every web server can host as-is.

    cmake --preset web-release                 # configure (vcpkg wasm ports)
    cmake --build --preset web-release         # player + core test binary
    ctest --preset web                         # units under node + export tests
    python3 Util/orkige_export.py --project projects/jumper-lua \
        --platform web --engine-build build/web-release
    python3 -m http.server -d projects/jumper-lua/builds/web

## Prerequisites

- **emsdk**, user-local (never system-wide), at `~/Development/emsdk` or
  wherever `EMSDK` points:

      git clone https://github.com/emscripten-core/emsdk.git ~/Development/emsdk
      ~/Development/emsdk/emsdk install latest
      ~/Development/emsdk/emsdk activate latest

  Nothing needs to be on `PATH`: the triplet, the preset and the exporter
  resolve the toolchain through `EMSDK` (defaulted to the path above).
- vcpkg as for every other preset. The wasm dependency set is the classic
  set minus openal-soft (the browser provides OpenAL), minus the editor-only
  ports; `catch2` stays in so the unit suite runs on the target.

## How the pieces fit

| piece | role |
| --- | --- |
| `triplets/wasm32-emscripten.cmake` | overlay triplet: static wasm libs, hermetic `/usr/local` isolation, chainload below |
| `cmake/wasm32-emscripten-toolchain.cmake` | the ONE chainload toolchain (ports AND engine): seeds `-fwasm-exceptions`, then includes the emsdk platform file. vcpkg has no emscripten toolchain of its own, so `VCPKG_C(XX)_FLAGS` set in a triplet never reach a compiler here — the wrapper is where ABI-relevant flags live. |
| preset `web-release` | classic backend, Release, tests ON |
| `tools/player/CMakeLists.txt` (Emscripten branch) | player link flags: WebGL2 ceiling, forced FS, Emscripten's OpenAL |
| `tools/player/PlayerContext.h` + `playerIterate` (main.cpp) | the player's world on ONE heap context and the loop body as an iterate callback: the desktop loop calls it in a plain `while`, the browser hands the context to the page's frame callback (`emscripten_set_main_loop_arg`, requestAnimationFrame-paced) — same frame body, same orderly teardown, no stack-suspension instrumentation |
| `engine_util/SDLNativeWindowWeb.cpp` | the native-handle bridge returns null: the page's one canvas is both SDL's window (input) and the GLES2 render surface (OGRE binds it through Emscripten's EGL) |
| `Util/orkige_export.py --platform web` | packages `<project>/builds/web/`: `index.html` (title/launch background/icon from the manifest), `orkige_player.{js,wasm}`, `game.data` + `game.js` (the payload image — engine media, project payload, `orkige_project.txt` marker — packed by emsdk's `file_packager`, mounted before `main()` runs, so `PlayerBundle` boots the project with no arguments, the same mechanism as every exported app) |
| `tools/player/web/index.html.in` | the shell page template the exporter fills in |

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
live property writes and cvars, all unchanged. One page per session; a
second tab or a page arriving after the session ended is refused (409) and
runs standalone.

Honest boundaries of the browser link:

- **Stop** sends quit over the link; the page's game loop exits cleanly
  (`ORKIGE_EXIT_<code>` in the title) and the closing socket confirms the
  stop. The editor cannot close a tab — the finished page stays open.
- A page that **never connects** (no browser, tab closed early) times out
  (`BROWSER_PAGE_CONNECT_TIMEOUT_SECONDS`) back to edit mode; serving
  continues and the tab runs standalone. Closing/refreshing the tab
  mid-session ends the session like a vanished player; the reloaded page is
  refused and runs standalone.
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
- **Mesh import only, and OgreZip owns zip.** The wasm assimp builds with
  its exporters off (a per-port option in the triplet): the runtime never
  writes meshes, and the 3MF exporter was the one consumer of the standalone
  zip library whose full source OgreMain also embeds (OgreZip) — with it
  gone, the module carries exactly ONE zip/miniz implementation and the
  linker's duplicate-symbol warning is gone. Import formats (glb/gltf/obj/
  3mf/...) are unchanged.
