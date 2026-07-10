# MCP agent workflows

Worked walkthroughs for an AI agent driving the Orkige editor over its
[MCP endpoint](mcp.md). This is the **tutorial** companion to `Docs/mcp.md` —
that document is the reference (endpoint, auth, transport, the full tool table
and per-tool semantics); this one shows the tools composed into the loops an
agent actually runs.

Every call below is a JSON-RPC `tools/call` against `POST /mcp`. The wire form is

```jsonc
// request
{ "jsonrpc":"2.0", "id":7, "method":"tools/call",
  "params": { "name":"<tool>", "arguments": { ... } } }
// result content (structuredContent shown; a refused verb comes back isError)
{ "jsonrpc":"2.0", "id":7, "result": { "structuredContent": { ... } } }
```

The walkthroughs elide the envelope and show just `<tool> { arguments } → { reply }`.

## Evidence-first: never claim success you did not read back

The editor tools are built so an agent can *observe*, not guess. A mutation
returning without `isError` means the verb was accepted — it does NOT mean the
game now behaves the way you intended. Before reporting a step done, read the
result back through the matching read tool:

- authored a file → `read_project_file` / `list_project_files`
- changed a component → `get_component` (edit world) or `runtime_state` (live game)
- ran the game → `get_state` (play mode), `runtime_hierarchy`, `console_tail`
- verified behaviour → `run_tests` + `get_test_results`, or `screenshot_game`

All scalar fields cross the wire as STRINGS (`"1"`/`"0"` for flags), matching the
debug-protocol convention. The async tools (`play`, `run_tests`, `export_project`,
`screenshot_game`) return an `accepted` acknowledgement and are POLLED — treat
"accepted" as "started", and keep polling the stated field until it settles.
Mutations need the `Authorization: Bearer <token>` header; pure reads are open.

---

## 1. Author a feature

Write a script, attach it to an object, run the game, confirm it does what you
meant, then iterate on the script with hot-reload — all without leaving the MCP
session. Assumes a project is already open (`open_project`).

```jsonc
// 1. write the game logic as a project file (jailed to the project root, LF)
write_project_file {
    "path":"scripts/spinner.lua",
    "content":"function update(dt)\n  self.rotationY = self.rotationY + dt\nend\n" }
//   → { "path":"scripts/spinner.lua", "bytes":"57" }

// 2. read it back — the evidence the write landed as intended
read_project_file { "path":"scripts/spinner.lua" }
//   → { "path":"scripts/spinner.lua", "bytes":"57", "content":"function update..." }

// 3. attach a ScriptComponent to the object, then point it at the script.
//    set_component writes reflected properties by NAME; run get_component first
//    if you are unsure of the exact names (here: 'script').
add_component { "id":"Cube1", "component":"ScriptComponent" }        // authed → {}
set_component { "id":"Cube1", "component":"ScriptComponent",
    "properties": { "script":"scripts/spinner.lua", "enabled":"1" } } // authed → {}
get_component { "id":"Cube1", "component":"ScriptComponent" }         // verify:
//   → { "script":"scripts/spinner.lua", "enabled":"1",
//       "properties":["script","enabled",...], "kinds":["asset","bool",...] }

// 4. run it. play is async — poll get_state until it settles.
play {}                                     // authed → { "accepted":"1", "play_mode":"launching", "target":"desktop" }
get_state {}                                // poll → { "play_mode":"playing", "remote_connected":"1", ... }

// 5. evidence the script is live on the running object
runtime_hierarchy {}                        // → { "ids":["Cube1",...], "play_mode":"playing", ... }
runtime_select { "id":"Cube1" }             // authed → { "selected":"Cube1" }
runtime_state {}                            // poll until ready="1":
//   → { "object":"Cube1", "ready":"1",
//       "properties":["TransformComponent.orientation",...], "values":["1 0 0 0",...] }
console_tail { "count":50 }                 // → { "lines":[...,"[remote] ..."], "levels":[...] }

// 6. edit the script — a write to scripts/*.lua during a live Play is picked up
//    by the editor's scripts/ watcher and hot-reloaded automatically (or force
//    it with reload_script).
write_project_file {
    "path":"scripts/spinner.lua",
    "content":"function update(dt)\n  self.rotationY = self.rotationY + dt * 3\nend\n" }
//   → { "path":"scripts/spinner.lua", "bytes":"61" }
reload_script { "id":"Cube1" }              // authed (optional; the watcher also fires) → {}

// 7. confirm the new behaviour, then stop
runtime_state {}                            // poll → orientation now advancing faster
stop {}                                     // authed → {}
```

A reload that fails to compile keeps the OLD instance running and surfaces a
`[remote] SCRIPT ERROR` — so always read `console_tail` (or `runtime_state`)
back after a hot-reload rather than assuming the swap took.

---

## 2. The test loop

Close the loop with structured evidence: make a change, run the relevant test,
read a machine-parseable verdict, fix, rerun until green. This is the cheapest
form of proof because every Orkige test self-checks and exits non-zero on failure.

```jsonc
// 1. discover the acceptance gate for what you are editing (synchronous)
list_tests { "filter":"jumper_lua" }
//   → { "tests":["player_jumper_lua_selfcheck_next"], "count":"1", "buildDir":"..." }

// 2. after editing, run it. run_tests is ASYNC and (by default) builds the tree
//    first; it returns a jobId to poll.
run_tests { "filter":"player_jumper_lua_selfcheck" }   // authed
//   → { "accepted":"1", "jobId":"a1b2c3...", "build":"1", "buildDir":"..." }

// 3. poll for the verdict
get_test_results { "jobId":"a1b2c3..." }
//   → { "jobId":"a1b2c3...", "status":"running" }     // keep polling
//   → { "status":"done", "total":"1", "passed":"0", "failed":"1",
//       "buildFailed":"0",
//       "failed_names":["player_jumper_lua_selfcheck_next"],
//       "failed_durations":["2.13"],
//       "failed_logtails":["...assertion failed: player never left the ground..."] }

// 4. the logtail is the "why" — read it, fix the script/code, rerun step 2.
//    A build that does not COMPILE short-circuits (no ctest runs):
//   → { "status":"done", "buildFailed":"1",
//       "buildErrors":"player.lua:12: ... error: ..." }
//    Fix the compile error first, then rerun.

// 5. iterate until green
get_test_results { "jobId":"<new job>" }
//   → { "status":"done", "total":"1", "passed":"1", "failed":"0",
//       "failed_names":[], "failed_logtails":[] }        // green
```

Notes that keep the loop honest:

- `device`-labelled tests (simulator/emulator) are ALWAYS excluded from both
  `list_tests` and `run_tests` — a run never boots a device.
- Pass `build:"0"` to `run_tests` to test an already-built tree as-is (fast) when
  you only changed a project script, not engine C++.
- `targets` scopes the incremental build (e.g. just `orkige_editor_tests`);
  `preset` selects a different tree (`"desktop-classic"`, a `build/` dir name or
  an absolute path) — default is the editor's own build.

---

## 3. Debug a live game

Drive and inspect the RUNNING game: watch its live hierarchy, pause and step,
poke a property or a cvar, capture a frame as proof, resume, stop. The `runtime_*`
tools serve the LIVE player; `list_hierarchy`/`get_component` always read the
EDIT world even during Play, so the two never blur together.

```jsonc
// 1. play a specific scene on the desktop target (async; poll get_state)
play { "scene":"scenes/level1.oscene", "target":"desktop" }   // authed
//   → { "accepted":"1", "play_mode":"launching", "target":"desktop" }
get_state {}
//   → { "play_mode":"playing", "remote_connected":"1", "remote_object_count":"12", ... }

// 2. inspect the live tree, then stream one object's component state
runtime_hierarchy {}                        // → { "ids":["Player","Ground",...], "selected":"", ... }
runtime_select { "id":"Player" }            // authed → { "selected":"Player" }
runtime_state {}                            // poll until ready="1":
//   → { "object":"Player", "ready":"1",
//       "properties":["TransformComponent.position","RigidBodyComponent.mass",...],
//       "values":["0 1 0","1",...], "kinds":["vec3","float",...], "readonly":["0","0",...] }

// 3. freeze time to examine a frame, advance exactly one physics tick
pause {}                                     // authed → {}   (get_state → play_mode:"paused")
step {}                                      // authed → {}   (one fixed tick while paused)

// 4. poke the running game live (NOT undoable — this is the live player, not the
//    edit world). A bad name/value comes back as a [remote] line, not an error.
set_runtime_property {
    "id":"Player", "component":"TransformComponent",
    "property":"position", "value":"2 3 4" }  // authed → {}
set_cvar { "name":"player_speed", "value":"8" } // authed → {}
runtime_state {}                             // read back → values reflect the writes
console_tail { "count":30 }                  // check for any [remote] rejection lines

// 5. capture the running frame as proof. screenshot_game is ASYNC: it returns a
//    baseline sequence; poll get_state until screenshot_seq exceeds it.
screenshot_game { "path":"/tmp/level1.png" }  // authed
//   → { "accepted":"1", "path":"/tmp/level1.png", "prev_screenshot_seq":"0" }
get_state {}
//   → poll until { "screenshot_seq":"1", "screenshot_ok":"1", "screenshot_path":"/tmp/level1.png" }

// 6. let it run again, then stop back to edit mode
resume {}                                    // authed → {}
stop {}                                      // authed → {}
```

Every `runtime_*` verb (and `pause`/`resume`/`step`/`screenshot_game`) returns
`isError` with `"no live player - start Play first"` when nothing is playing, so
the edit-world / live-game boundary is never ambiguous. `screenshot_game` is
desktop-play only (the path lives on the player's filesystem, which the editor
shares only on desktop).
