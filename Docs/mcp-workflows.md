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
  (a frame) / `record_trace` (a .jsonl of motion + events over time, read back)

All scalar fields cross the wire as STRINGS (`"1"`/`"0"` for flags), matching the
debug-protocol convention. The async tools (`play`, `run_tests`, `export_project`,
`screenshot_game`, `record_trace`) return an `accepted` acknowledgement and are POLLED — treat
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

// 5b. for behaviour that unfolds over time, record a TRACE - a .jsonl flight
//     recorder you READ back (agents can't watch video). record_trace is ASYNC
//     the same way (poll record_seq), auto-stops at the time budget (or
//     stop_recording), and samples the world every everyNth frame.
resume {}                                    // authed → let it move while we record
record_trace { "path":"/tmp/level1.jsonl", "seconds":3, "everyNth":2 }  // authed
//   → { "accepted":"1", "path":"/tmp/level1.jsonl", "prev_record_seq":"0" }
get_state {}
//   → poll until { "record_seq":"1", "record_ok":"1", "record_path":"/tmp/level1.jsonl" }
// then READ the file and ASSERT on it. Each sample line looks like:
//   {"t":0.28,"frame":34,"dt":0.0166,"objects":[
//      {"id":"Player","name":"Player","pos":[3.5,1.2,0],"vel":[2,4.1,0],"active":1,"visible":1}]}
// e.g. an agent proving a jump: read every sample's Player pos[1] (y) and assert
// the series rises above its start then falls back - the arc is in the numbers.
// Event lines interleave, e.g. {"t":0.5,"frame":60,"event":"contactBegin","a":"Player","b":"Ground"}.

// 5c. check the HUD respects the device safe area (notch / home indicator).
//     get_safe_area reports the window size + insets; get_ui_layout the widget
//     rects. Assert every VISIBLE widget lies inside the safe box.
get_safe_area {}
//   → { "window_w":"1179", "window_h":"2556", "safe_top":"120", "safe_left":"0", ... }
get_ui_layout {}
//   → { "ids":["hud.mode","hud.wins"], "rects":["16 120 180 28 1","959 120 200 28 1"] }
// for each rect "l t w h v" with v=1: assert l>=safe_left, t>=safe_top,
// l+w<=window_w-safe_right, t+h<=window_h-safe_bottom.

// 6. let it run again, then stop back to edit mode
resume {}                                    // authed → {}
stop {}                                      // authed → {}
```

Every `runtime_*` verb (and `pause`/`resume`/`step`/`screenshot_game`/
`record_trace`/`stop_recording`) returns `isError` with `"no live player - start
Play first"` when nothing is playing, so the edit-world / live-game boundary is
never ambiguous. `screenshot_game` and `record_trace` are desktop-play only (the
path lives on the player's filesystem, which the editor shares only on desktop).
Prefer `record_trace` when the evidence is motion or timing over a window — a
jump arc, a tween, a physics settle, a contact — and read the numbers back.
When you only need to confirm what's on screen right now, `screenshot_game`
gives the frame; and the zero-cost DETERMINISTIC alternative is a
`pause`/`step`/`screenshot_game` loop (step advances exactly one frame), which
needs no real-time capture at all.

---

## 4. Author levels

Build a tile-based level by stamping prefabs onto a snap grid, then wire the
finished scene into the game's level sequence and play it — the same grid-paint
loop the editor's paint tool drives, done over MCP. Assumes a project is open and
a scene is loaded (the grid coincides with the scene's slots when it carries a
level, otherwise it is the translate snap step at the world origin).

```jsonc
// 1. see the tile palette and the grid the paint verbs snap to
list_paint_prefabs {}
//   → { "paths":["assets/Wall.oprefab","assets/Floor.oprefab"],
//       "names":["Wall","Floor"], "count":"2",
//       "origin_x":"0", "origin_y":"0", "cell_size":"0.5" }

// 2. stamp a few cells. Give a cell by { col, row }, or a world "position" that
//    snaps to the nearest cell — either way the reply reports the SNAPPED cell.
//    Painting the same cell replaces its occupant; the identical tile again is a
//    no-op (painted:"0"), so a sweep never churns the undo stack.
paint_prefab { "prefab":"assets/Floor.oprefab", "cell": { "col":0, "row":0 } }  // authed
//   → { "id":"Floor", "painted":"1", "col":"0", "row":"0", "x":"0", "y":"0" }
paint_prefab { "prefab":"assets/Floor.oprefab", "cell": { "col":1, "row":0 } }  // authed
//   → { "id":"Floor 2", "painted":"1", "col":"1", "row":"0", "x":"0.5", "y":"0" }
paint_prefab { "prefab":"assets/Wall.oprefab", "position":"0 0.5" }             // authed
//   → { "id":"Wall", "painted":"1", "col":"0", "row":"1", "x":"0", "y":"0.5" }

// 3. evidence the tiles landed (the painted roots appear in the EDIT hierarchy)
list_hierarchy {}                           // → { "ids":[...,"Floor","Floor 2","Wall"], ... }

// 4. drop a wrong tile? erase that cell (undoable). undo/redo work too.
erase_cell { "cell": { "col":0, "row":1 } }  // authed → { "erased":"1", "col":"0", "row":"1", ... }
undo {}                                      // authed → the erased tile comes back

// 5. save, then append the scene to the project's level sequence (levels.olevels;
//    mints the manifest "levels" setting the first time). NOT undoable, and it
//    needs a SAVED scene inside the project — save_scene first.
save_scene { "scene":"scenes/level2.oscene" }   // authed → { "scene_path":".../level2.oscene" }
add_scene_to_levels {}                          // authed → { "scene_path":".../level2.oscene" }

// 6. play the level for proof (async; poll get_state), or run its selfcheck
play { "scene":"scenes/level2.oscene", "target":"desktop" }   // authed
get_state {}                                //   → poll until play_mode:"playing"
```

`add_scene_to_levels` refuses (isError) when no project is open, the scene is
unsaved or outside the project root, or the scene is already in the sequence —
read the reply back rather than assuming the append took.

## 5. Design a UI screen (the collaborative preview loop)

Author a `.oui` screen, see it rendered at real device sizes WITHOUT booting the
game, and iterate — while a human watching the editor's **GUI Preview** tab sees
every edit live (the tab shares the same offscreen stage and reloads the file on
change). `preview_ui` renders through the real gui stack into an offscreen target,
so it needs no running player; it is Ogre-Next only (the classic editor errors
honestly). Assumes a project carrying a `gui_default` atlas is open.

```jsonc
// 1. author a screen as plain text (jailed to the project root)
write_project_file {                                                 // authed
  "path":"screens/title.oui",
  "content":"[Layout]\natlas = gui_default\n\n[Button play]\nz = 2\nsprite = button\nfont = 9\ntext = Play\nposition = 40 40\nsize = 300 96\n"
}
//   → { "path":"screens/title.oui", "bytes":"..." }

// 2. preview it at a phone context; get a screenshot + the resolved rects
preview_ui {                                                         // authed
  "file":"screens/title.oui",
  "width":1179, "height":2556, "scale":3, "insets":"0 141 0 102"
}
//   → { "path":"/tmp/orkige_preview_ui.png", "width":"1179", "height":"2556",
//       "batch_count":"1",
//       "ids":["play"], "rects":["40 40 300 96 1 1 0"] }
//   read the png back to SEE it (rendered at the device size + content scale);
//   read the rects to CHECK the layout ("left top w h visible enabled modal").

// 3. a device-matrix sweep in ONE call (phone + tablet) - one png per context
preview_ui {                                                         // authed
  "file":"screens/title.oui",
  "contexts":"1179x2556@3/0,141,0,102; 2048x1536@2"
}
//   → { "paths":["/tmp/orkige_preview_ui_0.png","/tmp/orkige_preview_ui_1.png"],
//       "context_labels":["1179x2556@3","2048x1536@2"],
//       "ids":["play"], "rects":[...] }   // rects are the FIRST context

// 4. the button sits too high on the tablet? edit the file and preview again -
//    the human's GUI Preview tab updates live (it watches the file's mtime)
write_project_file { "path":"screens/title.oui", "content":"...position = 40 200..." }  // authed
preview_ui { "file":"screens/title.oui", "width":2048, "height":1536, "scale":2 }       // authed
//   → the returned rect for "play" moved - proof the edit took, no player booted
```

Evidence, not assumption: `preview_ui` returns `batch_count` (> 0 means the gui
actually submitted geometry) and the resolved `rects`; compare the rects across
edits/contexts to confirm the layout holds. A missing `file` returns `isError`
(an honest error, not a silent success). The whole loop is exercised end to end by
the `editor_control` self-test (write → preview → edit → preview → assert the rect
moved → a missing file errors).
