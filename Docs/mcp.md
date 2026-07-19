# MCP endpoint — AI-agent editor control

The Orkige **editor hosts an MCP server itself**, over
[Model Context Protocol](https://modelcontextprotocol.io) **Streamable HTTP**
(the stable remote transport, spec 2025-03-26): a single `POST /mcp` endpoint
speaking JSON-RPC 2.0. An AI agent (Claude Code, Claude Desktop, …) connects to
the running editor as a *remote* MCP server and drives it: open projects, edit
and save scenes, inspect and mutate the GameObject hierarchy, read and write
component properties, control play mode, take screenshots and list assets.

This document is the **reference** (endpoint, auth, transport, the tool table and
per-tool semantics). For worked agent walkthroughs — authoring a feature, the
edit→test loop, debugging a live game — see the tutorial companion
[`Docs/mcp-workflows.md`](mcp-workflows.md).

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
local dev session; with no token the port is fully open, and with a token EVERY
request needs it — reads included, see the security posture below.)

Claude Desktop: add an entry under `mcpServers` with `"type": "http"` and
`"url": "http://127.0.0.1:9010/mcp"` plus the `Authorization` header.

## Security posture

The MCP endpoint grants a remote client **full editor control** — scene
authoring, project-file writes, arbitrary Lua/editor-script execution, and Play.
The player's debug link (`core_debugnet/DebugServer`, the play-mode
editor↔player protocol) is a second socket surface. **Threat model:** in an
AI-agent development setting neither surface may be reachable by any process
other than the intended local client, and the token that gates access must not
leak through a timing side channel. Three properties enforce that;
[security.md](security.md) is the engine-wide umbrella (endpoints, tokens, jails)
this section details.

- **Loopback-only by default.** Both the MCP `HttpServer` and the player
  `DebugServer` bind `127.0.0.1` **only** — they are not reachable off the
  machine. Binding a non-loopback interface is an **explicit opt-in**:
  `--mcp-bind 0.0.0.0` / `ORKIGE_MCP_BIND=0.0.0.0` (aliases `--control-bind` /
  `ORKIGE_CONTROL_BIND`) for the editor endpoint and `--debug-bind 0.0.0.0` for
  the player debug port (`any`/`all`/`*` are accepted spellings; anything
  unrecognized stays loopback so a typo cannot silently expose the surface). It
  **exposes the control/debug surface to the network — only do it behind a
  trusted boundary** (a private lab network, an SSH tunnel's far end), and the
  editor logs a warning when it does. The regression is pinned by
  `ServerBindTests` (both servers assert loopback by default via the
  `isLoopbackOnly()` seam plus a live loopback connect, and the opt-in flips it).

- **Token required on reads, not just mutations.** When a token file is
  configured, **every** verb needs the `Authorization: Bearer <token>` header —
  reads (`get_state`, `list_hierarchy`, `read_project_file`, …) included, so an
  unauthenticated peer cannot exfiltrate the project structure or source over the
  network. Only the handshake/liveness verbs (`hello`, which itself carries and
  checks the token, and `ping`) are reachable pre-auth; the MCP `initialize` /
  `tools/list` discovery stays open (it exposes only the static tool schema). The
  **no-token dev mode is unchanged** — with no token file the port is fully open
  for a hand-started local session. The policy is the pure
  `core_debugnet/ControlAuth::verbAllowed` (unit-tested by `ControlAuthTests`),
  and the `editor_control` self-test drives a read WITHOUT the token and asserts
  it is refused while a token is configured.

- **Constant-time token comparison.** The bearer token is compared with
  `core_util/constantTimeEquals` (unit-tested by `ConstantTimeCompareTests`),
  which folds every byte into one accumulator with no early exit, so the reply
  latency never reveals how many leading bytes of a guess matched — closing the
  byte-at-a-time timing oracle a plain `==`/`strcmp` leaks. The public `Bearer `
  scheme prefix is still matched normally (it is not secret).

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
`isError` for a refused/failed verb). The image-capturing verbs
(`screenshot`, `preview_ui`, `preview_animation`) additionally inline the
just-written PNG as an MCP **image content block**
(`{"type":"image","data":"<base64>","mimeType":"image/png"}`) so a remote client
sees the render directly — the path stays in `structuredContent` for pixel
diffing. A PNG over 4 MiB is skipped (with `"inline_skipped":"too_large"` in
`structuredContent`), and `inline:false` opts out; either way the path is
returned. `screenshot_game` is async (the player writes the file after the
accepted reply), so it stays path-only — read the confirmed path off the shared
filesystem.

- **JSON-RPC surface**: `initialize` (→ protocol version + `tools` capability +
  server info), `notifications/initialized` (→ 202 Accepted, no body), `ping`,
  `tools/list` (→ the tools with schemas), `tools/call`. `id` is JSON-RPC's
  native correlation.
- **Transport**: POST-only. The optional Streamable-HTTP GET-SSE stream is not
  implemented — the tool surface is request/response; long ops (play boot)
  return an *accepted* result and are polled via `get_state`.
- **Auth**: with a token file configured, EVERY verb needs the
  `Authorization: Bearer <token>` header (the token from the token file) — reads
  included (see "Security posture" above). No token file ⇒ auth off (dev
  convenience). The bearer is compared in constant time.
- **Path jail**: the file-authoring verbs (`write_project_file`,
  `read_project_file`, `list_project_files`, and `import_asset`'s `targetDir`)
  are **confined to the open project's root**. The requested path is normalized
  and canonicalized and its containment verified before any I/O — an absolute
  path, a `..` traversal (even one that only escapes after normalization) and a
  symlink component that resolves out of root are all **refused with an honest
  error**, nothing written. Legitimate nested paths (`assets/textures/foo.png`)
  work. The shared containment primitive is `core_util/PathJail`; the same guard
  hardens pak/zip mounting against zip-slip (see
  [filesystem.md](filesystem.md#security-zip-slip--the-path-jail)).
- **Play control** (`play`/`stop`/`pause`/`resume`/`step`), the **run tools**
  (`list_play_targets`, `play`'s `scene`/`target`, the `build_*` fields of
  `get_state`) and the **runtime debug tools** (`runtime_hierarchy`/
  `runtime_select`/`runtime_state`/`set_runtime_property`/`set_cvar`/
  `reload_script`/`screenshot_game` — see "Debugging a running game") are
  translated into the ONE existing player debug protocol — the MCP endpoint is
  editor-side, never a second player port.

## Tools

The endpoint advertises 79 tools (the `toolSpecs` table in
`EditorControlServer.cpp`). Each maps onto an existing `EditorCore` method or an
`EditorDocument` free function — nothing bypasses the verb handler.

| Tool | Maps to |
|------|---------|
| `get_state` | project/scene/dirty/selection/play-mode snapshot (+ the prefab-edit stage: `edit_context` (`scene`/`prefab`), and while a prefab is staged `prefab_path`/`prefab_root`/`prefab_dirty` plus `scene_path`/`scene_dirty` re-reported from the STASHED scene the close restores; + `build_status`/`build_target`/`build_errors` for compile-on-Play; while a play session runs, the streamed-music snapshot: parallel `music_ids`/`music_files` arrays plus a `music_info` string per track — `"playing positionSec durationSec baseGain groupVolume effectiveGain loop"`, streamed on `MSG_STATS`, empty when nothing plays; plus the runtime perf summary: `frame_ms`, the engine-level allocation counters `alloc_per_frame`/`alloc_peak` with the per-subsystem breakdown in parallel `alloc_tags`/`alloc_counts`, and `profile_seq` — see `get_profile`; plus `transaction_open` — `1` while a `begin_transaction` atomic-edit bracket is open) |
| `open_project(path)` / `new_project(path)` / `close_project(force)` | `openProjectFromPath` / `newProjectAtPath` / `closeProject` (dirty-state policy) |
| `new_scene(force)` / `open_scene(scene, force)` / `save_scene(scene)` | `newScene` / `openSceneFromPath` / `saveSceneToPath` |
| `list_hierarchy()` / `get_object(id)` | `GameObjectManager::getGameObjects` (+ parent/active) |
| `get_component(id, component)` | a component's reflected properties (generic over the property registry) + the discovery lists `kinds`/`hints`/`readonly`/`transient` |
| `set_component(id, component, properties)` | the undoable reflected setter (`EditorCore::applyPropertyChange`, validated then merged into one undo step) |
| `create_object(id, mesh, position)` / `delete_object(id)` / `duplicate_object(id)` | `CreateObjectCommand` / `DeleteObjectCommand` / `DuplicateObjectCommand` |
| `rename_object(id, new_id)` / `reparent_object(id, parent)` / `set_active(id, value)` | `EditorCore::renameObject` / `reparentObject` / `setObjectActive` |
| `add_component(id, component)` / `remove_component(id, component)` / `list_addable_components()` | `addComponentToObject` / `removeComponentFromObject` / `getAddableComponentTypes` |
| `select(id)` / `undo()` / `redo()` | `EditorCore::selectObject` / `undo` / `redo` |
| `begin_transaction()` / `end_transaction(commit)` | **auth** — one atomic-edit bracket for a remote client, over `EditorCore::begin`/`endScriptTransaction` (the SAME one-undo primitive `.editor.lua` tools use). Everything run between them folds into ONE undo step on `commit=true`, or unexecutes wholesale on `commit=false`. A double begin / an end with no begin is an honest error. `get_state` reports `transaction_open`. The bracket spans many HTTP requests, so it AUTO-ABORTS (rolls back, one Console line) if the editor switches scene/project/prefab, starts Play, or shuts down under it — keep it short-lived (a manual editor edit the owner makes in between is folded in too; the fold is origin-blind) |
| `play(scene?, target?, force?)` / `stop()` / `pause()` / `resume()` / `step()` | `startPlay` / `requestStopPlay` / pause·resume·step over the player protocol; `scene` opens+plays a scene (jailed), `target` picks the device (`applyPlayTarget`) |
| `list_play_targets()` | the Play target picker's enumeration (`listSimulators`/`listIosHardwareDevices`/`listAdbDevices`) → `target_kinds`/`target_ids`/`target_names`/`target_states` |
| `screenshot(path, window, inline?)` | the EDITOR: `RenderTexture::writeContentsToFile` (chrome-free viewport) / `RenderSystem::saveWindowContents` (whole window) → returns the written path AND inlines the PNG as an image content block (`inline:false` or >4 MiB opts out) |
| `runtime_hierarchy()` | the RUNNING game's live hierarchy (ids/parents/active), streamed from the player |
| `runtime_select(id)` | choose which running object streams its component state (`MSG_SELECT`) |
| `runtime_state()` | the streamed component state of the selected running object (values + kinds/hints/readonly) |
| `set_runtime_property(id, component, property, value)` | write one reflected property on the RUNNING game live (`MSG_SET_PROPERTY`) |
| `set_cvar(name, value)` | change a console variable on the RUNNING game live (`MSG_SET_CVAR`) |
| `reload_script(id?)` | hot-reload Lua on the RUNNING game — one object or all (`MSG_RELOAD_SCRIPT`) |
| `reload_ui(file)` | hot-reload one declarative `.oui` screen on the RUNNING game — destroy-and-rebuild its widgets from the fresh file (`MSG_RELOAD_UI`); a parse failure keeps the OLD screen and surfaces a `[remote]` error, a rebuild emits the `ui.reloaded` script event. The editor's `.oui` watcher fires this on a file save too |
| `reload_anim(file)` | hot-reload one vector-animation rig (`.oanim`) on the RUNNING game (`MSG_RELOAD_ANIM`): the player parses the fresh file FIRST, then rebuilds every `VectorAnimationComponent` playing it (clean cutover — playback restarts at each component's reflected `clip`); a parse failure keeps every OLD rig and surfaces a `[remote]` error naming the line, a rebuild emits the `animation.reloaded` script event. The editor's animation watcher fires this on a file save too (re-cooking a changed Lottie source first); after a `reimport_asset` during Play, call it yourself |
| `screenshot_game(path)` | screenshot the RUNNING game's frame (`MSG_SCREENSHOT`, desktop play) → poll `get_state` (async: the file is written after the accepted reply, so this verb stays path-only — no inline image) |
| `record_trace(path, seconds?, everyNth?, objects?)` | record a temporal TRACE of the RUNNING game to a `.jsonl` flight recorder (`MSG_RECORD_START`, desktop play) — per-frame object samples (pos/vel/active/visible + dt + `mem` process footprint) with contact/scene/error/warning events → poll `get_state` for `record_seq` |
| `stop_recording()` | end an in-progress `record_trace` early (`MSG_RECORD_STOP`); the player writes what it captured → poll `get_state` |
| `list_assets()` | `AssetDatabase::listAssets` + `Project::listScenes` |
| `write_project_file(path, content)` | write a text file under the open project's root (jailed; LF endings; parent dirs created) |
| `read_project_file(path)` | read a text file under the project root (jailed; 1 MiB cap) |
| `list_project_files(dir?, glob?)` | list one directory level under the project root (jailed) → `names`/`paths`/`types` |
| `preview_ui(file, language?, width?, height?, scale?, insets?, contexts?, path?, inline?)` | **auth** — render a project `.oui` screen at a SIMULATED device context into an offscreen target and return a screenshot + resolved widget rects, **no running player needed** (the centrepiece of the collaborative UI loop). Renders through the REAL gui stack, isolated from any running game. Single context from `width`/`height` (pixels) + `scale` (1/2/3) + `insets` (`"l t r b"`); OR `contexts` for a device-matrix sweep (`;`-separated `WxH[@scale][/l,t,r,b]`). Optional `language` resolves the screen's `@key` captions in that target language (from the project's `loc/` directory); omit for the source language — preview a screen in German without a play session. Returns `path` (single) or `paths`+`context_labels` (sweep), `width`/`height`, `batch_count`, parallel `ids`/`rects` (each rect `"left top width height visible enabled modal"`, first context for a sweep), the applied `language` and the available `languages` (a project with no `loc/` directory ignores `language` with a `language_note`). The screenshot is also inlined as an image content block (the FIRST context for a sweep, to bound payload; `inline:false` or >4 MiB opts out). **Ogre-Next only** (classic reports an honest error); does not disturb the human's GUI Preview tab |
| `preview_animation(asset, clip?, time?, blendClip?, blendWeight?, size?, path?, inline?)` | **auth** — render a vector-animation rig (`.oanim`) at a chosen `clip`/`time` into a PNG + return the pose readback, **no running player needed** (the animation twin of `preview_ui`). The rig is evaluated on the editor's own clock (`core_util/VectorAnimEval`) and CPU-rasterized (the same raster that draws `.oanim` thumbnails), so an agent can scrub a cycle (t=0 / mid / end) and try a same-rig blend (`blendClip` + `blendWeight` 0..1, mixed at the same time) without a play session. Returns `path` (the PNG), the resolved `clip`/`frame`/`time`, the rig `duration` (frames)/`fps`, `layer_count`/`shape_count`/`vertex_count`, `visible_pixel_count`/`coloured_pixel_count`, `at_end`, and the available `clips`. The pixel counts make blank or all-white regressions machine-detectable. The PNG is also inlined as an image content block (`inline:false` or >4 MiB opts out). **Both flavors** (pure CPU raster — no offscreen target); does not disturb the human's Inspector animation preview |
| `import_asset(sourcePath, targetDir?, clips?, extent?, tolerance?)` | copy an OUTSIDE file into the project via `importAssetFile` (sidecar minted, id returned; optional relocate via `AssetDatabase::moveAsset`). An `.svg` source is cooked to a native `.oshape` on the way in (`Util/cook_shapes.py`); a Lottie `.json` is cooked to a native `.oanim` (`Util/cook_vector_anim.py`) with the SOURCE `.json` KEPT beside it (both id-tracked, re-cooked on re-import) — the returned `path`/`assetId` point at the cooked `.oanim` (or `.oshape` for a document where nothing animates), and a `.oanim` reply also carries the rig's `clips` (names in rig order — clip discovery lands with the import). The optional cook params (`clips` = `name:start:end[:loop\|once],...` marker override, `extent`, `tolerance`) are applied to the Lottie cook AND recorded on the kept source's sidecar as the per-asset intent (`Docs/vector-animation.md#import-records-and-automatic-re-cooks`) |
| `reimport_asset(asset, clips?, extent?, tolerance?)` | **auth** — re-cook an imported animation pair IN PLACE via `recookAnimationPair` (the MCP face of the Asset browser's "Reimport"): run the Lottie cook on the project's kept `.json` source — the re-cook trigger a source edited via `write_project_file` needs, since a plain file write never cooks. `asset` is the project-relative `.json` (or its cooked `.oanim`/`.oshape` — the pair resolves either way); optional cook params update the sidecar's recorded settings first, omitted ones keep the recorded values. Returns the produced `path`, the `source`, the recorded settings (`clips_setting`/`extent_setting`/`tolerance_setting`), the recorded input hashes (`source_hash`/`tool_hash`/`settings_hash`) and a fresh rig's `clips`. During Play, follow with `reload_anim` |
| `create_prefab(objectId, path)` | `PrefabSerializer::savePrefab` + `AssetDatabase::importAsset` + `EditorCore::makePrefabInstance` (write a subtree as a `.oprefab`, convert to an instance) |
| `instantiate_prefab(path, parent?)` | `CreatePrefabInstanceCommand` (a fresh instance of a `.oprefab`, optionally reparented) |
| `open_prefab(path\|asset)` | **auth** — `openPrefabForEdit` (swap the live scene aside into a temp snapshot and load the `.oprefab` subtree into the ONE edit world). Give the prefab by project-relative/absolute `path` OR stable `asset` id. Returns the stage `root_id` (the file stem) + `prefab_path`; `get_state` reports `edit_context=prefab`. While staged, EVERY editing verb (hierarchy CRUD, `get`/`set_component`, `undo`/`redo`, `screenshot`, `run_editor_script`) operates on the prefab subtree UNCHANGED, and the scene/project lifecycle, `play`, `add_scene_to_levels` and the paint/instance verbs are refused with the prefab-mode error |
| `save_prefab()` | **auth** — `savePrefabEdit` (write the open stage back to its `.oprefab`; the open scene's instances refresh from the rewritten file with their per-instance overrides re-applied at close). Refused (honest error) when the stage root was deleted or objects exist OUTSIDE the single root. Returns `prefab_path` |
| `close_prefab(policy)` | **auth** — `closePrefabEdit` (restore the snapshotted scene and pop the stage). `policy` is REQUIRED — `save` (write the prefab first; a refused save cancels the close) or `discard` (drop unsaved stage edits). Returns the restored `scene_path`; `get_state` reports `edit_context=scene` again |
| `list_paintable_assets()` | `searchAssets` (the project's paintable palette: prefabs + textures + `.oshape`) + `EditorCore::resolvePaintGrid` → parallel `paths`/`names`/`kinds` (`prefab`/`texture`/`shape`), `count` and the grid `origin_x`/`origin_y`/`cell_size` |
| `list_paint_prefabs()` | back-compat alias of `list_paintable_assets` (lists textures + shapes too, not prefabs only; same result shape incl. `kinds`) |
| `paint_asset(asset, cell?, position?, suppressed?)` | **auth** — `EditorCore::paintTileAtCell` (paint a tile into one grid cell as one undo step; same cell replaces its occupant of ANY kind). A `.oprefab` `asset` instantiates the prefab; a texture or `.oshape` paints a BARE tile (a grid-cell sprite/shape object carrying a `TileComponent` source id — no prefab file) → the painted-root `id`, `kind`, snapped `col`/`row`/`x`/`y`, `painted` |
| `paint_prefab(prefab\|asset, cell?, position?, suppressed?)` | back-compat alias of `paint_asset` (accepts the source as `prefab` or `asset`; paints a texture/`.oshape` as a bare tile too) |
| `erase_cell(cell?, position?)` | **auth** — `EditorCore::eraseTileAtCell` (erase the tile in one grid cell as one undo step, any kind — prefab instance or bare tile) → snapped `col`/`row`/`x`/`y`, `erased` |
| `add_scene_to_levels()` | **auth** — `addCurrentSceneToLevels` (append the current saved scene to `levels.olevels`, minting the manifest `levels` setting the first time; NOT undoable) |
| `get_project_setting(key?)` | a project manifest Setting from the OPEN project (the free-form `.orkproj` key/values — `export.orientation`, `export.ios.bundleId`, …): with `key` returns `value` + `has`; omit `key` for every setting as parallel `keys`/`values`. Read-only |
| `set_project_setting(key, value)` | **auth** — write one manifest Setting and persist the `.orkproj`. The authoritative path for `export.*` config (`export.orientation` = `portrait`/`landscape`/`auto`, bundle ids, versions): it goes through the editor's IN-MEMORY project so a following Build/export sees it — unlike a raw `write_project_file` of the `.orkproj`, which the editor would not pick up. Refused with no project open |
| `get_safe_area()` | the RUNNING game's window size + safe-area insets (notch/rounded corners/home indicator), pixels: `window_w`/`window_h` + `safe_left`/`safe_top`/`safe_right`/`safe_bottom` (streamed on `MSG_STATS`; `-1` until reported, desktop insets 0) |
| `get_ui_layout()` | the RUNNING game's gui widget rects: parallel `ids`/`rects` (each rect `"left top width height visible enabled modal"`, pixels; the three flags are `1`/`0` — `enabled`=interactive, `modal`=part of an active modal dialog; streamed on `MSG_UI_LAYOUT`) — combine with `get_safe_area` to assert every visible HUD widget lies inside the safe box, or read `modal` to assert a dialog is up |
| `gui_press(id)` | **auth** — synthesize a press on a gui widget by id in the RUNNING game, routed through the REAL input path so modal/disabled semantics apply (a button under a modal scrim does NOT fire; a disabled widget stays inert) (`MSG_GUI_PRESS`) |
| `dismiss_modal(id?)` | **auth** — close a modal dialog in the RUNNING game by id, or the topmost one when omitted (`MSG_GUI_DISMISS_MODAL`) |
| `get_breadcrumbs()` | the player's on-disk crash trail (pure file I/O — the player may be dead): `live` (this/most-recent session's `breadcrumbs.jsonl` text) and `previous` (the prior session's, rotated aside at boot — the one to read after a crash), one JSON object per line, plus the resolved `dir`. Two derived fields answer "did the last run die?" straight off the `previous` trail: `crashed` (`"true"`/`"false"` — true when its LAST entry is a fatal-signal `"crash"` marker) and `crashSignal` (the signal name, e.g. `"SIGSEGV"`, empty when not crashed) — the machine-detectable crash verdict a phone can't show as a dialog. Mobile app-lifecycle transitions ride this same trail — `"background"`/`"foreground"`/`"terminating"`/`"low_memory"` kinds — so no new readback verb was needed to observe backgrounding on device |
| `get_benchmark_results(file?)` | the per-scene performance artifact the player captured when armed for a benchmark run (`ORKIGE_BENCHMARK`): pure file I/O from the player's writable app dir (its project jail can't reach it, like `get_breadcrumbs`). Picks the newest `benchmark-*.jsonl`, or the named `file`. Returns the raw `text`, the parsed `meta`/`summary` lines, a `scenes` array (one JSON object per scene — frame-ms min/avg/p50/p95/p99/max, per-phase means, alloc mean+peak, RSS peak, triangle/batch/texture means), `scene_count`, `aborted` and the resolved `dir`/`file`. Empty when no artifact exists. Schema: `Docs/benchmark.md`. Starting a run is the existing `play` verb with `ORKIGE_BENCHMARK` armed; on-device artifacts are pulled by the CLI harness (`adb pull` / `simctl get_app_container`), not MCP |
| `get_profile()` | the RUNNING game's hierarchical CPU frame profile: parallel `names`/`info` lists (each info `"depth calls milliseconds maxMilliseconds"`; depth-0 rows are the canonical tick phases — `input scripts events tweens physics load audio present debug render`), plus `frame_ms` and `profile_seq` (poll until it advances for a fresh frame). A Debug player streams snapshots unprompted (`MSG_PROFILE_DATA` on the stats cadence); on a Release player the first call arms the profiler (`MSG_PROFILE`), so call again shortly after. Pair with `get_state`'s `alloc_per_frame`/`alloc_tags` to answer "where does the frame go, and what allocates?" |
| `get_lua_api()` | the generated Lua scripting API signature index (`inventory` text + `doc` path) — the global tables (`world`/`screen`/`sound`/`music`/`tween`/`guitween`/`haptics`/`cvar`/`save` + the `loc` global) and core value types, one line per symbol; read-only, needs no project/Play. Embedded from `Docs/lua-api.md`'s generated block (`GeneratedLuaApi.h`), so an MCP-only agent learns the scripting surface self-contained; see `Docs/lua-api.md` for conventions and the full type reference |
| `run_editor_script(name)` | **auth** — run a project EDITOR TOOL (`scripts/<name>.editor.lua`) once through the editor-tool host — the same tool a human runs from the editor's Tools menu. The tool's `editor.*` calls route back through this SAME verb handler and its whole run folds into ONE undo step; a tool script error is reported (with its `file:line`) and leaves NO partial edits. Author a tool with `write_project_file`, then trigger it here. Returns `name` and `command_count`. See `Docs/lua-api.md` (Editor scripts) for the `editor.*` surface |
| `console_tail(count)` | the editor `EditorConsole` line store (includes the player's `[remote]` lines + script errors during Play) |
| `list_tests(preset, filter, label)` | `ctest -N` in a build tree → the test names (discovery) |
| `run_tests(filter, label, preset, build, targets)` | async build + `ctest` → a jobId; poll `get_test_results` |
| `get_test_results(jobId)` | the structured verdict of a `run_tests` job |
| `export_project(platform)` | async `Util/orkige_export.py` (macos/ios-simulator/android) → a jobId; poll `get_export_results` (classic-flavor tree required) |
| `get_export_results(jobId)` | the structured verdict of an `export_project` job (`ok`/`artifactPath`/`error`) |

### Component properties (reflected)

`get_component` / `set_component` are GENERIC over the property registry — there
is no fixed per-component field list. `get_component` reports every reflected
property of the named component as a `name`->`value` field plus the parallel
discovery lists `properties` (names), `kinds` (`int`/`float`/`bool`/`string`/
`enum`/`vec3`/`quat`/`color`/`asset`/`object`), `hints` (enum options
`label=value,...`, or an asset/object kind), `readonly` and `transient`
(`1`/`0`), so an agent discovers the field set with no hardcoded allowlist — a
`ScriptComponent`'s exported script properties surface here too. Values are
canonical strings: vectors space-separated (`vec3` `x y z`, `quat` `w x y z`,
`color` `r g b a`), bool `1`/`0`, enum the integer value. `set_component` writes
by property NAME (an unknown, read-only or unparseable value is refused without
touching the object) and accepts the changed fields either at the top level or
inside a `properties` object, merged into one undo step.

**Script component kinds.** A behavior script whose file ends in `.component.lua`
is a first-class component kind named after the file (`player.component.lua` →
`player`); several attach to one object. It flows through the SAME generic verbs
with zero kind-specific handling: `write_project_file scripts/foo.component.lua`
makes `foo` appear in `list_addable_components`; `add_component(id, "foo")`
attaches it (binding its script file); its declared `properties` are reflected,
so `get_component`/`set_component` read and write them by name. Verified end to
end by the `editor_control` self-test.

### Flat-colour vector shapes — no new verb

`VectorShapeComponent` (the flat-colour organic-shape 2D content) needs NO
dedicated MCP verb: it rides the generic surfaces above. Author the asset by
writing an `.oshape` directly with `write_project_file` — it is plain text (a
`fill r g b a` colour + `contour N` / `v x y` polylines, holes via `hole M`),
the strong agent path — or `import_asset` an `.svg` (the editor cooks it to
`.oshape` via `Util/cook_shapes.py`). Create and place with `create_object` +
`add_component` (`VectorShapeComponent` is a reflected type, so it appears in the
component registry MCP already exposes) + `set_component` on `transform`.
Configure `shape` (the AssetRef, set by resource name like a sprite `texture`),
`tint`, `scale`, `edgeSoftness`, `zOrder` and `visible` through
`get_component` / `set_component` — they are reflected properties, so the single
`OOBJECT_IMPL` registration feeds the inspector, serialization AND MCP at once
with no per-surface wiring. Verify with `screenshot_game` / the scene RTT and
run it with `play`.

**Soft, deformable organic shapes** need NO new verb either. The soft-body
tunables — `softBody`, `wobbleStiffness`/`wobbleDamping`/`wobbleAmount`,
`squashAmount`, `morphClip`/`morphSpeed`/`morphLoop` — are reflected properties
on the SAME `VectorShapeComponent`, so `get_component` / `set_component` (edit
mode) and `set_runtime_property` (a running game) tune them with zero extra
plumbing. The scripted drive (`impulse`, `playMorph`, `stopMorph`) is exposed to
Lua on `self.shape` (the component's `OFUNC` exports), so an agent authors the
behaviour by writing the project script with `write_project_file`. Author a
morph set by writing an `.oshape` with `morph NAME` blocks directly, or cook one
from pose SVGs with `Util/cook_shapes.py --targets`. Observe the deform on the
running game: a `record_trace` captures per-frame object positions (the deformed
silhouette rides its node) AND `contact` events (the impacts that squash it), and
`get_component` reads the live squash/wobble state back through the reflected
introspection getters.

### Game UI (gui) — authoring vs. readback

Game UI is authored in Lua and project files — screens are built by
`ScriptComponent` scripts (see `projects/jumper-lua`, `projects/roller`) or loaded
from a declarative `.oui` layout file (`gui:loadLayout`, see below). Agents
therefore create and edit UI through the existing project-file verbs
(`write_project_file` / `read_project_file` / `list_project_files`) and iterate
live: overwrite an `.oui` and call `reload_ui(file)` — the RUNNING game destroys
and rebuilds that screen from the fresh file (clean cutover; a parse failure
keeps the OLD screen and surfaces a `[remote]` error, so a half-broken screen
never renders). The editor's `.oui` watcher fires the SAME reload on a plain file
save during Play, exactly like `reload_script` for `.lua`. Verify the result with
`get_ui_layout` (the widget rects move); a script re-acquires its now-stale
widget handles by subscribing to the `ui.reloaded` bus event. What agents
cannot otherwise observe — the platform safe area and the resolved on-screen
widget rects on a real device — is exposed as runtime readback: `get_safe_area`
(the notch/home-bar insets) and `get_ui_layout` (per-widget pixel rects, plus an
`enabled` and a `modal` flag per widget), both read-only. Two authoring-adjacent
verbs let an agent *drive* the live UI: `gui_press(id)` synthesizes a real press
on a widget — routed through the actual input path, so a press on a button
UNDER a modal scrim does not fire and a disabled widget stays inert — and
`dismiss_modal(id?)` closes a dialog. Together with the `modal`/`enabled` flags
an agent can assert "the dialog is up, it eats input below, and row X is
disabled", then drive the dialog to completion. The full grammar (including the
`.oui` `[Modal]` / `[ToggleGroup]` sections and `enabled` / `modal` keys) and the
widget recipes live in `Docs/gui.md`.

Real fonts and vector UI sprites need **no new verb** either: a runtime font/
sprite atlas is a plain `.ogui` text asset plus its `.ttf`/`.svg` sources under
`assets/`. An agent authors the atlas by writing the `.ogui` with
`write_project_file` (a `[Font.N]` section with `ttf <asset>`/`size <designPx>`,
or a `[Sprites]` `name svg <asset> <designWidth>` entry) and brings the source
files in with `import_asset` (or `write_project_file` for an inline `.svg`); the
runtime bakes them at load. The engine-default font ships with every build, so a
project can reference `Nunito-Regular.ttf` by name with no import at all. Text
laid out with these fonts appears in `get_ui_layout` like any other widget.

Nine-slice sprites, the rect-anchor layout model, layout groups, content-size-fit
and the scroll container likewise need **no new verb**: all are plain-data widget
properties an agent sets from the same Lua the game already uses (nine-slice = a
4-int inset suffix on a `[Sprites]` line + `setNineSlice(true)`; layout =
`setParent`/`setAnchorPreset`/`setOffsets`/`setAnchoredPosition`/`setSizeDelta` on
a widget plus `setDesignResolution`/`setRootSpace` on the manager; groups =
`setLayoutGroup`/`setGroupPadding`/`setGroupSpacing`/`setContentSizeFit`; scroll =
`createScrollView` + `setScroll`).

The **declarative `.oui` file** makes this even more agent-native, and it is why
`read_ui_layout` / `write_ui_layout` verbs are **deliberately NOT added**: an
`.oui` is a plain text project file, so an agent authors a whole screen — widgets,
anchors, pivots, offsets, groups, nine-slice, scroll — by writing it with the
existing `write_project_file` (jailed to the project root), and the game loads it
with `gui:loadLayout(path)`. Unlike the level-paint verbs (which mutate live
editor scene state that has no file the agent can otherwise write), the `.oui`
path already has the file as its source of truth, so `write_project_file` +
`read_project_file` cover authoring completely and a dedicated verb would only
duplicate them. The `core_util/UiLayout` descriptor is pure plain data that
serialises 1:1 into the `.oui`, so the file and the Lua setters express exactly
the same thing. The resolved absolute rects — the two-pass resolver's output —
read back through the existing `get_ui_layout` (no change needed: the resolve runs
before the screens rebuild, so `get_ui_layout`'s per-widget pixel rects already
reflect the anchors/groups/scroll offset), so an agent confirms the `.oui` took
effect the same way it verifies any widget (and combines it with `get_safe_area`
for the notch check).

The **one** UI verb that does earn its place is `preview_ui` — because it fills a
gap the readback verbs cannot: seeing a screen at a chosen device context WITHOUT
booting the game. `get_ui_layout` reports the running game's rects at the running
window's size; `preview_ui` renders a project `.oui` through the same real gui
stack into an offscreen target at a SIMULATED device (resolution + content scale +
safe-area notch), and returns a screenshot plus the resolved rects for that
context (or a matrix of contexts in one call). It is the agent's half of the
collaborative UI loop: author a screen with `write_project_file`, `preview_ui` it
across a phone and a tablet, read the rects to check the layout holds on both,
iterate — while a human watching the editor's **GUI Preview** tab (which shares
the same offscreen stage) sees each edit live, because the tab watches the `.oui`
file's mtime. The `editor_control` self-test drives exactly this loop
(write → preview → edit → preview → assert the rects moved; a missing file errors).
It is Ogre-Next only (the offscreen 2D composition the preview needs is a
Next-flavor capability — see `Docs/render-abstraction.md`); the classic editor
reports an honest error and disables the tab.

The machine-checkable "HUD respects the notch" assertion is: for every
visible widget from `get_ui_layout`, its rect lies inside
`[safe_left, window_w-safe_right] × [safe_top, window_h-safe_bottom]` from
`get_safe_area` (the `player_safearea_device` ctest runs exactly this against a
booted iPhone 16 simulator).

### GUI animation — Lua/`.oui`-authored, no new verb

Widget animation (the `guitween` table, `transition` in the `.oui`, cascading
group alpha, button press feedback, scroll momentum) is authored in Lua and the
declarative layout, so an agent drives it with the existing
`write_project_file` / script-authoring verbs — no dedicated animation verb.
Read-back needs none either: `get_ui_layout` reports the RESOLVED rects, so a
poll mid-animation shows a widget's interpolating position/size (a `move`/`size`
tween drives the layout inputs the resolver already reflects), and
`screenshot_game` captures the scaled/rotated/faded frame. The pure animation
logic is unit-tested headlessly (`GuiAnimationTests`, `TweenTests` loops) and
the `demo_gui_matrix` selfcheck exercises the whole surface on both flavors.

### Haptics, tilt calibration, screen fades — Lua-authored, no new verb

Three device/presentation features are reachable through the SAME
authoring-in-Lua + readback path as game UI, so they need no dedicated MCP verb:

- **Haptics** (`haptics.play(strength, ms)` / `haptics.pattern(name)` /
  `haptics.isAvailable()` / `haptics.setEnabled()`) — phone-body vibration, a
  device-only effect (desktop is an honest no-op). An agent authors the calls in
  a `ScriptComponent` via `write_project_file` and iterates with `reload_script`;
  there is no headless "did it buzz" to read back.
- **Tilt calibration** (`input:calibrateTilt()` / `input:clearTiltCalibration()`
  / `input:getTiltCalibration()`) — captures the current pose as neutral. Authored
  in Lua (a settings "Calibrate" button); the resulting offset is a reflected-ish
  input value the game reads through `input:getTilt()`.
- **Screen fades** (`screen.fadeOut(secs)` / `screen.fadeIn(secs)` /
  `screen.loadScene(path, out, in)` / `screen.setFadeColor` / `screen.isFading`) —
  a full-screen wipe over a scene switch, authored in Lua and observable via
  `screenshot_game` (the overlay renders into the captured frame).

All three are driven by writing/reloading project scripts and confirmed with the
existing readback verbs (`screenshot_game`, `console_tail`, `get_breadcrumbs`),
so the standing "every feature reachable or justified" rule is met without a new
tool.

### Game-support pack (save / camera fit / screen shake / time scale / text entry) — no new verb

The game-support features follow the same authored-in-Lua + reflected-property
pattern, so none needs a dedicated verb:

- **Persistence** (`save.set/getNumber/getBool/getString/has/remove/flush`) —
  authored in a `ScriptComponent` via `write_project_file`, iterated with
  `reload_script`. The on-disk store is a plain file in the writable app dir; an
  agent confirms a write with `get_breadcrumbs`/`console_tail` (the store logs
  its flush) or reads a value back through the game's own HUD via
  `screenshot_game`.
- **2D camera fit** — the `CameraComponent` `fitMode` / `designWidth` /
  `designHeight` are REFLECTED properties, so they are already authored and read
  back through the generic `get_component` / `set_component` (edit mode) and
  `set_runtime_property` (live) — no bespoke verb, exactly like every other
  component property. The script-driven `engine:setCameraOrthographicFit` is a
  Lua call authored the same way as `setCameraOrthographic`.
- **Screen shake** (`screen.shake` / `stopShake` / `isShaking`) and **time
  scale** (`world.setTimeScale` / `getTimeScale`) — runtime Lua calls, authored +
  `reload_script`-iterated; the shake is observable in `screenshot_game`, and
  time scale is confirmed by watching `runtime_state` values advance at the
  scaled rate (or not, at 0 = hitstop).
- **Text entry** (`createTextEntry`, `getText/setText/setPlaceholder/
  setMaxLength/wasSubmitted`) — a gui widget, authored in Lua like every
  other widget and read back with `get_ui_layout` / `screenshot_game` (the
  "authoring vs. readback" rule above already covers game UI).

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

## Authoring a project over MCP

`get_component`/`set_component` mutate objects already in a scene, but a scene is
built out of files an agent also has to author: game logic lives in Lua scripts,
behaviour in prefabs, tuning in config assets. `write_project_file`,
`read_project_file`, `list_project_files`, `import_asset` and the two prefab
verbs close that gap so an MCP-only agent never needs a separate filesystem tool.

**Path safety.** Every file verb is JAILED to the open project's root: the path
is project-root-relative, absolute paths are refused, and any `..` or symlink
escape is rejected (the same lexical containment `AssetDatabase::resolveInsideRoot`
uses for asset imports, plus an absolute-path refusal and a
weakly-canonical symlink-escape re-check). `import_asset` is the one exception —
its *source* is by nature outside the project (it copies a file IN) — so it
needs auth, exactly like a human dragging a file into the editor.

**Config assets** (`input.oactions` / `physics.olayers` / `levels.olevels`) are
project-root-relative files referenced from the manifest `Settings`, so they are
authored with plain `write_project_file` — there is no dedicated verb. The editor
does not cache them in edit mode (it re-reads them at Play/export time and the
Inspector reloads the physics layers on `open_project`), so no reload hook is
needed; a running player picks up a rewritten config on its next Play.

**Scripts + hot-reload.** Writing a `scripts/*.lua` file during a live Play
session needs no explicit reload verb: the editor already polls `scripts/` (~4 Hz,
`watchProjectScripts`) and fires the hot-reload on any change, so an MCP write is
picked up like an editor-side edit. `reload_script` (see below) is still there to
force a reload on demand.

The develop → verify loop:

```
// 1. write the logic
tools/call write_project_file { "path":"scripts/player.lua",
    "content":"function update(dt)\n  self.y = self.y + dt\nend\n" }
//   → { "path":"scripts/player.lua", "bytes":"41" }

// 2. discover / read files back
tools/call list_project_files { "dir":"scripts", "glob":"*.lua" }
//   → { "names":["player.lua"], "paths":["scripts/player.lua"], "types":["file"] }
tools/call read_project_file { "path":"scripts/player.lua" }   // → { "content":"..." }

// 3. bring an outside asset in (sidecar minted, stable id returned)
tools/call import_asset { "sourcePath":"/tmp/hero.png" }
//   → { "path":"assets/hero.png", "assetId":"5f2c..." }

// 4. capture a subtree as a reusable prefab, then stamp instances of it
tools/call create_prefab { "objectId":"Enemy", "path":"assets/Enemy.oprefab" }
//   → { "id":"Enemy", "path":"assets/Enemy.oprefab", "assetId":"a1b2..." }
tools/call instantiate_prefab { "path":"assets/Enemy.oprefab", "parent":"Spawner" }
//   → { "id":"Enemy 2" }

// 5. run the project's selfcheck for evidence (the loop above, run_tests)
tools/call run_tests { "filter":"player_jumper_lua_selfcheck" }
```

Then verify with `run_tests` + a `screenshot` (or `screenshot_game` during Play).

## Debugging a running game

During Play the editor spawns the player as a separate process and talks to it
over the ONE player debug protocol (`core_debugnet`). A distinct family of tools
bridges that link so an agent can drive and inspect the RUNNING game — the
editor-side MCP endpoint never opens a second player port. This is the DEBUG half
of the develop→run→test→debug loop: change something, watch it live, verify with
a screenshot.

**Edit world vs. running game.** `list_hierarchy` / `get_object` / `get_component`
/ `set_component` ALWAYS operate on the EDITOR's world, even while Play is running
(editing the scene mid-play stages changes for the next Play; it does not touch
the live game). The `runtime_*` tools serve the LIVE player instead. Each returns
`isError` (`"no live player - start Play first"`) when nothing is playing, so the
mode is never ambiguous.

- `runtime_hierarchy` reads the running game's live tree (`ids` / `parents` /
  `active`, parallel lists; plus `scene`, `selected`, `play_mode`). The player
  streams it on change; this returns the latest snapshot.
- `runtime_select(id)` picks which running object streams its component state
  (the debug protocol streams the SELECTED object only), then `runtime_state`
  returns that stream: `object` (the streamed id), `ready` (`"1"` once it matches
  the selection), and the parallel lists `properties` (the
  `"<Component>.<property>"` keys), `values`, `kinds`, `hints`, `readonly`. The
  stream is asynchronous (~15 Hz), so select, then poll `runtime_state` until
  `ready="1"`.
- `pause` / `resume` / `step` gate the running game's world stepping (rendering
  and the protocol stay live); `step` advances exactly one fixed physics tick
  while paused.
- `set_runtime_property(id, component, property, value)` writes ONE reflected
  property on the running game through the player's reflected setter — it takes
  effect immediately and is NOT undoable (contrast `set_component`, which is the
  undoable edit-world write). A bad name/value is reported as a `[remote]` error
  line rather than failing the call, matching the live protocol's one-way write.
- `set_cvar(name, value)` tunes a console variable on the running game
  (`CVarManager`, fires its `onChange`). `reload_script(id?)` hot-reloads Lua
  (compile-before-swap; one object or all) — a broken edit keeps the old instance
  and surfaces a `[remote] SCRIPT ERROR`.
- `console_tail` already includes the running player's forwarded log (the
  `[remote]` lines) and its script errors — the player forwards its engine log
  over the debug protocol and the editor folds it into the same Console store, so
  no separate remote-log verb is needed.
- `screenshot_game(path)` captures the RUNNING game's next rendered frame (the
  most-cited verification primitive). Desktop play only — the path lives on the
  player's filesystem, which the editor shares on desktop. It is ASYNC: the tool
  returns `{ accepted:"1", path, prev_screenshot_seq }`; the player captures the
  next frame and confirms, and `get_state` then carries `screenshot_seq` (bumped
  on each confirmation), `screenshot_ok` and `screenshot_path`. Poll `get_state`
  until `screenshot_seq` exceeds the returned `prev_screenshot_seq`.
- `record_trace(path, seconds?, everyNth?, objects?)` is the TEMPORAL sibling of
  `screenshot_game` — evidence for behaviour that unfolds over time, in the form
  an agent can actually READ: a `.jsonl` flight recorder (one JSON object per
  line), NOT pixels. Every `everyNth` rendered frame (default 2) it writes a
  sample line — `{"t", "frame", "dt", "objects":[…]}` — carrying, per named
  object, its world `pos`, `vel` (only when a rigid body exists), `active` and
  `visible` (in the window camera's view). Event lines interleave as they occur:
  `contactBegin`/`contactEnd` (both object names), `sceneLoad`, `scriptError`,
  `warning` (warning-and-above log lines) AND the message-bus events emitted that
  frame — the ones a script raised with `events.emit(name, payload)` plus the
  engine mirrors (`gui.clicked`, `physics.contactBegin`, `app.pause`, …), each a
  line named for the event with its payload's top-level scalar fields (bus events
  are Lua-authored, so the trace is their readback). Records for up to `seconds`
  wall-clock (default 5, capped at 60); `objects` narrows to a comma-separated
  id/name allowlist. The trace is byte-capped (~2MB) with an honest
  `{"truncated":1}` marker line if hit. Desktop play only, and ASYNC like
  `screenshot_game`: it returns `{ accepted:"1", path, prev_record_seq }`;
  `get_state` then carries `recording` (in progress), `record_seq` (bumped on
  each confirmation), `record_ok` and `record_path`. Poll until `record_seq`
  exceeds the returned `prev_record_seq`, or call `stop_recording()` to end it
  early. The dt field lets an agent assert on performance; the position series
  lets it assert on movement — e.g. "the player's y rose then fell" (a jump).

`get_state` is the poll target for all of the above: besides the editor snapshot
it carries `remote_connected`, `remote_scene`, `remote_selected`,
`remote_object_count`, the `screenshot_*` fields and the `recording`/`record_*`
fields while a play session is up. It also carries the running game's **memory
footprint** — `mem_rss` (the process resident set size in bytes) and
`mem_rss_peak` (the session high-water mark) — streamed by the player a few
times a second (`MSG_STATS`). Both read `"-1"` until the first reading arrives
(or on a platform without a memory query); read `mem_rss` against `mem_rss_peak`,
or the `mem` field on trace sample lines (below), to spot unbounded growth.

Example loop — inspect and poke the running game, then capture evidence:

```jsonc
tools/call play {}                                 // → { accepted:"1", play_mode:"launching" }
tools/call get_state {}                            // poll → play_mode:"playing", remote_connected:"1"
tools/call runtime_hierarchy {}                    // → { ids:["Player", ...], ... }
tools/call runtime_select { "id": "Player" }       // authed
tools/call runtime_state {}                        // poll → ready:"1", values:["1 0 0", ...]
tools/call pause {}                                // authed → get_state play_mode:"paused"
tools/call set_runtime_property {                  // authed; live write
    "id":"Player","component":"TransformComponent","property":"position","value":"2 3 4" }
tools/call screenshot_game { "path":"/tmp/frame.png" }  // → { accepted:"1", prev_screenshot_seq:"0" }
tools/call get_state {}                             // poll until screenshot_seq > 0, screenshot_ok:"1"
tools/call resume {}                               // authed; let it run while we record
tools/call record_trace { "path":"/tmp/run.jsonl", "seconds":3, "everyNth":2 }  // → { accepted:"1", prev_record_seq:"0" }
tools/call get_state {}                             // poll until record_seq > 0, record_ok:"1"; then READ /tmp/run.jsonl
tools/call stop {}                                 // authed → back to edit mode
```

## Running: what plays, and where

`play` is the RUN half of the loop. Two optional arguments control WHAT runs and
WHERE:

- `scene` opens a scene into the editor first, then plays it. It is
  project-relative and **jailed** inside the open project (an absolute path or a
  `..`/symlink escape is refused); with no project open the raw path is used, as
  for `open_scene`. Opening a different scene discards the current unsaved edit
  world, so it honors the dirty-state policy — pass `force:"1"` to override.
- `target` picks the device. `""`/`"desktop"` runs the local player;
  otherwise pass an id from `list_play_targets` — an iOS simulator UDID, an adb
  serial or `"browser"`. A shutdown simulator boots asynchronously (the
  toolbar's boot flow), so `play` returns `{ accepted:"1" }` and you poll
  `get_state` (`play_mode` walks `launching`→`playing`). Native-module projects
  are desktop-only.
- `"browser"` is an export-serve-open that becomes a LIVE play session once
  the page loads: the editor runs a `web` export, serves the artifact
  directory on a loopback port, opens the default browser at a URL carrying
  `?env.ORKIGE_DEBUG_CONNECT=127.0.0.1:<servePort>` — and waits. The page's
  runtime dials that endpoint (a page cannot listen, so the direction
  reverses; its socket emulation rides a WebSocket the serve port upgrades)
  and the session becomes a desktop-like live session: `runtime_hierarchy`,
  `runtime_state`, `pause`/`resume`/`step`, `set_runtime_property`,
  `set_cvar` and the `[remote]` Console lines all work over the ONE debug
  protocol. Poll `get_state` — `browser_play_status` walks
  `exporting`→`serving`→`connected` (or `failed`) and `browser_play_url`
  carries the page URL; a headless agent drives its own browser at that URL
  (the shell page maps `?env.NAME=VALUE` query params onto the module
  environment, so the native automation hooks work — `Docs/web-export.md`).
  A page that never connects degrades honestly: the waiting session times
  out back to edit mode, the status returns to `serving` and the tab runs
  standalone. `stop` sends quit over the link — the page's game loop exits
  (the editor cannot close a tab; the finished page stays). What does NOT
  work over a browser link, each refusing with an honest error:
  `screenshot_game` and `record_trace` (the page's in-memory filesystem
  never reaches the editor's disk) and `reload_script`/`reload_ui`/
  `reload_anim` (the page runs its packaged export snapshot — stop, re-play,
  and the fresh export picks the edit up). Gated (`target_states:"gated"`) until the
  web-release preset built the wasm player.

`list_play_targets` (a read) enumerates exactly what the editor's target picker
shows: `target_count` plus the parallel lists `target_kinds`
(`desktop`/`browser`/`ios-simulator`/`ios-device`/`android`), `target_ids`
(what you pass to `target`), `target_names` and `target_states`
(`ready`/`booted`/`shutdown`/`gated`/`device`).

```jsonc
tools/call list_play_targets {}                    // → target_ids:["desktop", "<udid>", ...]
tools/call play { "scene":"scenes/level1.oscene", "target":"desktop" }  // authed → { accepted:"1", target:"desktop" }
tools/call get_state {}                            // poll → play_mode:"playing"
```

**Reading build errors (compile-on-Play).** For a native-module project, Play
first compiles the module; a failed build stays in edit mode and launches
nothing. The `[build]` lines stream into the Console (read them with
`console_tail`), and `get_state` carries the STRUCTURED signal an agent acts on:
`build_status` (`none`/`building`/`ok`/`failed`), `build_target`, and — on a
failure — `build_errors` (the compiler-diagnostic tail, kept after the session
reverts to edit mode so you can read it post-hoc). Poll `get_state` after `play`:
`build_status:"failed"` with a non-empty `build_errors` is the "fix the compile"
signal; `build_status:"ok"` then `play_mode:"playing"` is the "it launched".

## Exporting a project

`export_project(platform)` packages the open project as a distributable through
the same pipeline as the editor's Build menu (`Util/orkige_export.py`).
`platform` is `macos`, `ios-simulator` or `android`. It is **async** (a
multi-minute job): it returns `{ accepted:"1", jobId }`; poll
`get_export_results(jobId)` — `status:"running"` until it finishes, then
`status:"done"` with `ok` (`"1"`/`"0"`), the `artifactPath` (the built `.app`/
`.apk`) on success or the `error` on failure, plus the exporter's `outputTail`.

The export pipeline is **pinned to the classic render flavor** (it bundles the
classic player/media set). `export_project` checks the target engine tree up
front and returns an honest structured error — WITHOUT running the exporter —
when that tree is missing or next-flavored (build the matching classic preset,
e.g. `macos-debug-classic`, first). Native-module projects export desktop only.

```jsonc
tools/call export_project { "platform":"macos" }   // authed → { accepted:"1", jobId:"..." }
tools/call get_export_results { "jobId":"..." }     // poll → status:"done", ok:"1", artifactPath:".../MyGame.app"
```

**Icon / launch-screen / signing config — no new verb.** The exporter now
generates a per-project app icon (from the manifest `export.icon`, or a neutral
engine default) and launch screen, and gates a signed `--platform ios` device
build on a resolvable identity + provisioning profile. Every new failure mode —
a missing/too-small `export.icon`, an `iconutil`/`aapt2` failure, an absent
signing identity — is emitted by the exporter to stdout and flows through the
**existing** `get_export_results` `error` + `outputTail` fields unchanged. No new
tool and no new schema field are needed; the export verb already surfaces these
honestly. (A signed `ios` device platform is not yet an `export_project` enum
value — it lands as a one-line enum addition once a device player build exists.)

**Store packaging stays CLI-only, deliberately.** The store-submittable
platforms (`android-aab` — a release-signed Android App Bundle; `ios-ipa` — a
distribution-signed `.ipa`) are NOT `export_project` enum values. They require
machine-local secrets — a release keystore + its passwords, an Apple
distribution certificate + App Store profile — that a remote agent does not (and
should not) hold, so a store verb over MCP could never do more than gate. The
gating logic itself (version-code/keystore config validation, the honest refusal
when a credential is missing) lives in `Util/orkige_export.py` and is exercised
cert-free by its `--selftest`; run store packaging from the shell, where the
secrets live. See `Docs/store-release.md`.

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
  It also proves the runtime debug tools' NEGATIVE paths headlessly: `tools/list`
  advertises them, the runtime READS (`runtime_hierarchy` / `runtime_state`)
  error cleanly with no player, and every runtime MUTATION is rejected without a
  bearer token. Finally it drives the AUTHORING tools against a throwaway project
  it opens with `new_project`: a `write_project_file` → `read_project_file`
  round-trip (asserting CRLF is normalized to LF), two jail violations REFUSED (an
  absolute path and a `..` escape, verifying nothing was written outside the
  root), an auth-rejected `write_project_file`, `list_project_files` (the written
  script appears under a `*.lua` glob), `import_asset` from a temp file (a stable
  id is minted, an unauthenticated import rejected), a `create_prefab` →
  `instantiate_prefab` round-trip (the `.oprefab` lands on disk and the fresh
  instance appears in `list_hierarchy`), and the GRID-PAINT loop over that same
  prefab (`list_paint_prefabs` lists it and reports a grid, an auth-rejected
  `paint_prefab`, then `paint_prefab` into a cell — the painted root appears in
  `list_hierarchy` — `erase_cell` removes it and `undo` brings it back) PLUS the
  bare-tile path (a probe texture is written, `list_paintable_assets` surfaces it
  as a `texture`, `paint_asset` paints it as a bare tile, `erase_cell` removes it).
  It also drives `run_editor_script` end to end: `write_project_file`s a
  `scripts/*.editor.lua` tool, an AUTH-REJECTED `run_editor_script`, then the
  authed run — asserting the object the tool authored appears in `list_hierarchy`.
  It then exercises PREFAB EDIT MODE over MCP: an auth-rejected `open_prefab`,
  the authed `open_prefab` (the stage roots at the file stem, `get_state` reports
  `edit_context=prefab`, and the swapped-out scene object is gone from
  `list_hierarchy`), an ordinary editing chain against the stage
  (`create_object`→`reparent_object`→`set_component` on a child, read back), a
  `save_scene` REFUSED with the prefab-mode error, `save_prefab`, a
  policy-less `close_prefab` refused, then `close_prefab {discard}` — asserting
  `get_state` returns to `edit_context=scene` and the swapped-out scene object is
  back in `list_hierarchy`.
  It also covers the RUN tools:
  `list_play_targets` reports the desktop target, and `export_project` refuses an
  unauthenticated request, a no-project request, an unknown platform and (on a
  next-flavored editor tree) the classic-pinned flavor check — all as fast
  structured refusals, leaving real exports to the `export_*` ctests. Proves the
  whole C++ MCP endpoint with no Python.
- `editor_control_debug` (ctest, integration; needs the built player): the same
  socket-driven MCP client, but the RUNTIME DEBUG loop end to end against the
  live player — `play { scene, target }` (saves a fixture scene, clears the edit
  world, then plays that scene on the desktop target — a running `Cube1` proves
  the scene path took effect), then
  `runtime_hierarchy` / `runtime_select` / `runtime_state` (live inspection),
  `pause` + `step`, `set_runtime_property` (a live Transform write read back
  through `runtime_state`), `screenshot_game` (the running-game frame, verified
  non-empty on disk via `get_state`'s `screenshot_seq`/`screenshot_ok`),
  `record_trace` (records the running game while nudging its Transform, then
  parses the written `.jsonl` line by line and asserts the moving object's
  sampled position changes across samples and every sample carries a positive
  `dt`, via `get_state`'s `record_seq`/`record_ok`), `reload_script`, an
  auth-rejected mutation on the LIVE player, then `stop` and a
  clean revert to edit mode. Every step runs through the MCP tools, exactly as an
  agent would drive them.
- `JsonTests` / `HttpServerTests` (ctest, unit): the nested-JSON codec
  round-trip + malformed-input safety, and the hand-rolled HTTP/1.1 framing
  (request parsing, keep-alive pipelining, case-insensitive headers, junk
  tolerance) — both headless, in the unit and desktop presets.
