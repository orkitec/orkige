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
otherwise). Play on it is an export-serve-open, not a live play session: the
editor runs the `web` export through its async export job (the `[export]`
Console lines), serves the artifact directory on a loopback port through a
second instance of the core_debugnet `HttpServer` (127.0.0.1 only; the wasm
module serves as `application/wasm` — streaming compilation requires it) and
opens the default browser at the served URL. The game runs standalone in the
tab — a page cannot host the debug socket, so the remote hierarchy/inspector
stay unavailable, like an iOS hardware session. A later Play re-exports and
re-points the one server's doc root; a previous tab's fetches answer 404 from
then on (those artifacts no longer exist). Agents reach the same flow over
MCP: `play { target:"browser" }`, then poll `get_state` for
`browser_play_status`/`browser_play_url` (`Docs/mcp.md`). Verified by the
`editor_play_browser` ctest (SKIPs 77 without the wasm player).

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
- **No debug link.** The BSD-socket API compiles but cannot listen in a page;
  `--debug-port` fails honestly and the game runs standalone — like an iOS
  device session.
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
