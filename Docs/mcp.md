# MCP endpoint — AI-agent editor control

The Orkige **editor hosts an MCP server itself**, over
[Model Context Protocol](https://modelcontextprotocol.io) **Streamable HTTP**
(the stable remote transport, spec 2025-03-26): a single `POST /mcp` endpoint
speaking JSON-RPC 2.0. An AI agent (Claude Code, Claude Desktop, …) connects to
the running editor as a *remote* MCP server and drives it: open projects, edit
and save scenes, inspect and mutate the GameObject hierarchy, read and write
component properties, control play mode, take screenshots and list assets.

There is **no Python sidecar and no extra pip dependency** — the editor now
hosts the endpoint in-process, retiring the old `Util/orkige_mcp.py` stdio
bridge (and its `mcp` SDK requirement). The HTTP
server is hand-rolled on the engine's own non-blocking socket layer
(`core_debugnet/HttpServer`), with a minimal nested-JSON value type
(`core_debugnet/Json`) for the JSON-RPC surface.

## Launch the editor with the MCP endpoint

The endpoint is **OFF by default** — no normal run or test opens a socket. Turn
it on with a port and (for mutations) a token file:

```sh
# explicit port + published token (the client reads both from the token file)
orkige_editor --mcp-port 9010 --mcp-token-file /tmp/orkige.token

# env equivalents (both the new ORKIGE_MCP_* and the historical
# ORKIGE_CONTROL_* names are honored)
ORKIGE_MCP_PORT=9010 ORKIGE_MCP_TOKEN_FILE=/tmp/orkige.token orkige_editor
```

`--control-port` / `--control-token-file` remain accepted aliases. With a token
file the editor writes `<port>\n<token>\n` to it (so a launcher that passed port
`0` can discover the ephemeral port), and the token is required on mutations.

## Register with Claude

Point the MCP client at the editor's URL (no command to spawn — it's a remote
server):

```sh
# read the published token, then register the HTTP endpoint
TOKEN=$(sed -n 2p /tmp/orkige.token)
claude mcp add --transport http orkige http://127.0.0.1:9010/mcp \
  --header "Authorization: Bearer ${TOKEN}"
```

(If your `claude` CLI version uses a different flag for headers, pass the
`Authorization: Bearer <token>` header however it accepts custom headers, or run
the editor without a token file — `--mcp-port 9010` alone — to disable auth for a
local dev session; a loopback reader is harmless, only mutations are gated.)

Claude Desktop: add an entry under `mcpServers` with `"type": "http"` and
`"url": "http://127.0.0.1:9010/mcp"` plus the `Authorization` header.

## Architecture

```
   Claude ──HTTP POST /mcp (JSON-RPC 2.0)──▶ editor MCP endpoint
                                             (EditorControlServer → HttpServer)
```

`EditorControlServer` is an HTTP + JSON-RPC transport in front of the existing
verb handler, which is REUSED wholesale: a thin adapter over `EditorCore`
(deliberately UI-independent) and the `EditorDocument` free functions. Each
editor verb is surfaced as an MCP **tool** with a JSON `inputSchema`; a
`tools/call` converts the tool arguments into the handler's internal request and
the reply back into MCP tool content (a text block + `structuredContent`, or
`isError` for a refused/failed verb).

- **JSON-RPC surface**: `initialize` (→ protocol version + `tools` capability +
  server info), `notifications/initialized` (→ 202 Accepted, no body), `ping`,
  `tools/list` (→ the tools with schemas), `tools/call`. `id` is JSON-RPC's
  native correlation.
- **Transport**: POST-only. The optional Streamable-HTTP GET-SSE stream is not
  implemented — the tool surface is request/response; long ops (play boot)
  return an *accepted* result and are polled via `get_state`.
- **Auth**: a mutation needs `Authorization: Bearer <token>` (the token from the
  token file). Read verbs are open. No token file ⇒ auth off (dev convenience).
- **Play control** (`play`/`stop`/`pause`/`resume`/`step`) is translated into
  the ONE existing player debug protocol — the MCP endpoint is editor-side,
  never a second player port.

## Tools

| Tool | Maps to |
|------|---------|
| `get_state` | project/scene/dirty/selection/play-mode snapshot |
| `open_project(path)` / `new_project(path)` / `close_project(force)` | `openProjectFromPath` / `newProjectAtPath` / `closeProject` (dirty-state policy) |
| `new_scene(force)` / `open_scene(scene, force)` / `save_scene(scene)` | `newScene` / `openSceneFromPath` / `saveSceneToPath` |
| `list_hierarchy()` / `get_object(id)` | `GameObjectManager::getGameObjects` (+ parent/active) |
| `get_component(id, component)` | the six typed bundles `EditorCore` exposes |
| `set_component(id, component, properties)` | the typed undoable setters (`applyTransformChange`, `changeObjectMesh`, `changeObjectScript`, `applyRigidBodyChange`, `applyCameraChange`, `applySpriteChange`) |
| `create_object(id, mesh, position)` / `delete_object(id)` / `duplicate_object(id)` | `CreateObjectCommand` / `DeleteObjectCommand` / `DuplicateObjectCommand` |
| `rename_object(id, new_id)` / `reparent_object(id, parent)` / `set_active(id, value)` | `EditorCore::renameObject` / `reparentObject` / `setObjectActive` |
| `add_component(id, component)` / `remove_component(id, component)` / `list_addable_components()` | `addComponentToObject` / `removeComponentFromObject` / `getAddableComponentTypes` |
| `select(id)` / `undo()` / `redo()` | `EditorCore::selectObject` / `undo` / `redo` |
| `play()` / `stop()` / `pause()` / `resume()` / `step()` | `startPlay` / `requestStopPlay` (translated to the player protocol) |
| `screenshot(path, window)` | `RenderTexture::writeContentsToFile` (chrome-free viewport) / `RenderSystem::saveWindowContents` (whole window) → returns the written path |
| `list_assets()` | `AssetDatabase::listAssets` + `Project::listScenes` |
| `console_tail(count)` | the editor `EditorConsole` line store |
| `list_tests(preset, filter, label)` | `ctest -N` in a build tree → the test names (discovery) |
| `run_tests(filter, label, preset, build, targets)` | async build + `ctest` → a jobId; poll `get_test_results` |
| `get_test_results(jobId)` | the structured verdict of a `run_tests` job |

## Test runner (the evidence loop)

`run_tests` is the close-the-loop primitive: edit, run the relevant test, read
a STRUCTURED verdict, iterate. Everything in Orkige is already self-checking
(Catch2 units + self-checking apps that exit non-zero on failure), so this is
cheap, deterministic evidence.

- `list_tests(filter?, label?, preset?)` runs `ctest -N` and returns `tests`
  (the names) so an agent can find, e.g., the selfcheck for the project it is
  editing. `filter` is a ctest `-R` name regex, `label` a `-L` label
  (`unit` / `integration`). `device`-labelled (simulator/emulator) tests are
  never listed. Synchronous (fast).
- `run_tests(filter?, label?, preset?, build?, targets?)` is **async**: it
  returns `{ accepted:"1", jobId }` immediately and does the work on a worker
  thread. With `build` unset/true it first `cmake --build`s the tree
  (`targets` scopes it); a **build failure short-circuits** — no ctest runs and
  the result is `buildFailed:"1"` + `buildErrors` (the compiler output tail), so
  an agent's first question ("did it compile?") is answered directly. Then
  `ctest` runs, filtered by `filter` (`-R`) and/or `label` (`-L`);
  `device`-labelled tests are ALWAYS excluded (a run never boots a simulator or
  emulator). Pass `build:"0"` to test an already-built tree as-is (fast).
- `get_test_results(jobId)` returns `status:"running"` until the run finishes,
  then `status:"done"` with `total` / `passed` / `failed` counts and, for each
  failure, the index-aligned lists `failed_names`, `failed_durations`,
  `failed_logtails` (each logtail is the last ~40 lines of that test's captured
  output — the "why did it fail" material). All counts cross as strings, per the
  DebugMessage convention.
- `preset` selects the build tree: empty / `desktop` / `unit` / `next` = this
  editor's own build; `desktop-classic` (or a `build/` dir name, or an absolute
  path) = another tree. Build/ctest paths are this editor build's own baked
  constants (`CMAKE_COMMAND` / `CMAKE_CTEST_COMMAND`).

Example loop — edit a project script, then verify with its selfcheck:

```jsonc
// 1. discover the acceptance gate for the project you are editing
tools/call list_tests { "filter": "jumper_lua" }
//   → { "tests": ["player_jumper_lua_selfcheck_next"], "count": "1" }

// 2. after editing scripts, run it (authed; build the tree first)
tools/call run_tests { "filter": "player_jumper_lua_selfcheck" }
//   → { "accepted": "1", "jobId": "a1b2..." }

// 3. poll for the verdict
tools/call get_test_results { "jobId": "a1b2..." }
//   → { "status": "running" }               // keep polling
//   → { "status": "done", "total": "1", "passed": "0", "failed": "1",
//       "failed_names": ["player_jumper_lua_selfcheck_next"],
//       "failed_logtails": ["...assertion: player never left the ground..."] }
//   read the logtail, fix the script, go back to step 2.

// a build that does not compile short-circuits with the compiler errors:
//   → { "status": "done", "buildFailed": "1",
//       "buildErrors": "player.lua:12: ... error: ..." }
```

The six typed component bundles (v1 — no generic reflection):
`TransformComponent` (position/orientation/scale, space-separated float strings;
orientation is `w x y z`), `ModelComponent` (mesh), `ScriptComponent`
(script/enabled), `RigidBodyComponent` (body_type/shape_type/mass/friction/
restitution/planar/radius/half_height/half_extents), `CameraComponent`
(projection_mode/ortho_size), `SpriteComponent` (texture/width/height/tint/
flip_x/flip_y/z_order/visible). `set_component` accepts the fields either at the
top level or inside a `properties` object.

## Dirty-state policy

Destructive verbs (`new_scene`, `open_scene`, `open_project`, `new_project`,
`close_project`) refuse to clobber an unsaved scene: they return `isError` unless
the scene is clean or the call passes `force="1"`. Automated runs never touch the
user's recents (the editor's `gRecordRecents`/`automatedRun` suppression).

## Tests

- `editor_control` (ctest, integration): the editor hosts its MCP endpoint on an
  ephemeral in-process port and, on a worker thread, drives a raw TCP socket
  through a real MCP conversation — `initialize`, `notifications/initialized`,
  `tools/list`, then `tools/call`s: `create_object` (authed, + verify it appears
  in `list_hierarchy`), an AUTH-REJECTED `create_object` (a mutation with no
  bearer token), and a `screenshot` to a temp path (+ verify the file was
  written) — asserting MCP-compliant JSON-RPC responses (id echo, result shape)
  at every step. It also drives the test-runner tools: `list_tests` (a known
  core unit test must appear), `run_tests` + `get_test_results` on ONE
  already-built unit test (`build:"0"`, asserting the structured pass tally),
  and a throwaway failing CTest tree (built on the fly) so the `failed_*` lists +
  logtail parse are exercised on a real failure without touching the real suite.
  Proves the whole C++ MCP endpoint with no Python.
- `JsonTests` / `HttpServerTests` (ctest, unit): the nested-JSON codec
  round-trip + malformed-input safety, and the hand-rolled HTTP/1.1 framing
  (request parsing, keep-alive pipelining, case-insensitive headers, junk
  tolerance) — both headless, in the unit and desktop presets.
