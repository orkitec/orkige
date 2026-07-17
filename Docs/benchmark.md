# Benchmark recorder

Per-scene performance capture to a machine-readable results artifact. The
recorder (`core_debug/BenchmarkRecorder`) is the benchmark counterpart to the
crash breadcrumbs: JSON Lines, written to the player's writable app dir, flushed
per record so a crash mid-run still leaves the scenes recorded so far. It is
**opt-in and dormant** — it records only once *armed*, and the editor never arms
it (every entry point is an honest no-op there, like `SaveStore`).

It samples per frame from the landed instruments — `ProfileManager`
(whole-frame ms + the depth-0 tick-phase times), `MemoryManager` (tagged
per-frame allocation events), `MemorySampler` (process RSS) — plus the render
facade's `FrameStats` (triangles / batches / texture memory), and aggregates
them **per scene**. A scene boundary is a level switch (the player opens a scene
boundary at boot and on each deferred scene load) OR an explicit Lua marker,
which composes: an explicit `benchmark.begin(name)` renames/restarts the current
aggregation so a director script can label a benchmark act.

## Arming

The recorder is armed by the **player**, from the environment (the player CLI
contract is strict, so arming rides an env like the other automation hooks):

| Env | Effect |
|-----|--------|
| `ORKIGE_BENCHMARK` | present ⇒ arm; the player opens `benchmark-<utcstamp>.jsonl` and gathers the compiled-in identity |
| `ORKIGE_BENCHMARK_DIR` | override the output directory (default: the writable app dir — `getSupportDirectory` on desktop, `getDocumentsDirectory` on mobile — the breadcrumbs precedent). Used for test isolation |
| `ORKIGE_BENCHMARK_MODE` | the `scenario` field (`full` / `smoke` / …); default `full` |
| `ORKIGE_BUILD_SHA` | the artifact's `engineSha`. There is **no compiled-in sha define** in the tree, so the recorder takes the sha from this env (the runner/harness sets it); absent ⇒ `"unknown"` |

Starting a run over MCP is the existing `play` verb with `ORKIGE_BENCHMARK` in
the player's environment — no new "start" verb.

## Lua surface

Registered like the other engine tables (`engine_gocomponent/ScriptComponent`),
all honest no-ops when disarmed or in the editor:

```lua
benchmark.begin("vista")   -- start / restart / rename the current scene aggregation
benchmark.endScene()       -- close the current scene, write its record
benchmark.isArmed()        -- is a run being recorded
```

`end` is a Lua keyword, so the close method is spelled `endScene()`.

## Artifact schema (JSON Lines, one object per line)

Line kinds: one `meta` line first, one `scene` line per recorded scene, one
closing `summary` line.

```jsonl
{"type":"meta","schema":1,"utc":"2026-07-12T10:00:00Z","engineSha":"5d2e1f6","flavor":"next","renderSystem":"Metal","build":"Release","platform":"ios","device":{"model":"unknown","os":"iOS","gpu":"unknown"},"scenario":"full","project":"vista"}
{"type":"scene","name":"vista","seconds":88.900,"frames":5332,"fpsAvg":59.910,"frameMs":{"min":15.100,"avg":16.694,"p50":16.600,"p95":17.400,"p99":21.000,"max":34.200},"subsystemsMs":{"input":0.100,"physics":1.200,"render":9.800,"scripts":0.800,"tweens":0.100},"allocs":{"perFrameAvg":3.100,"peakFrame":41,"rssPeakBytes":123456789},"gpu":{"trisAvg":412000.000,"batchesAvg":143.000,"texMemMB":186.000}}
{"type":"summary","scenes":6,"totalSeconds":412.000,"aborted":false}
```

Field notes:

- **`frameMs`** — min / avg / p50 / p95 / p99 / max of the whole-frame wall time
  (ms). Percentiles use nearest-rank with integer indexing
  (`index = floor(p·n/100)`, clamped) so each is a real observed sample.
- **`subsystemsMs`** — mean ms per depth-0 profiler tick phase, keyed by phase
  name (sorted). These are the `input scripts events tweens physics load audio
  present debug render` phases the profiler already records.
- **`allocs`** — `perFrameAvg` and `peakFrame` are the `MemoryManager` tracked
  **allocation-event** counts (not bytes; that is the currency the perf
  instrument trades in). `rssPeakBytes` is the process resident set peak from
  `MemorySampler` (0 where the platform offers no query).
- **`gpu`** — per-frame means of `FrameStats` triangles / batches / texture
  memory (MB).
- **`engineSha`** — from `ORKIGE_BUILD_SHA` (see Arming); `"unknown"` when unset.
- **`device`** — `os` from SDL; `model`/`gpu` are `"unknown"` where the platform
  is not queried.
- **`aborted`** — `true` when the run was torn down without a clean `finish`
  (a crash-safe partial artifact still parses).

## Reading the results

- **Desktop / same machine**: the MCP verb `get_benchmark_results(file?)` reads
  the newest (or named) artifact from the writable app dir and returns the raw
  text plus the parsed `meta` / `scenes[]` / `summary` (see `Docs/mcp.md`). Pure
  file I/O, no live player needed.
- **On device**: the artifact lives in the app container; a harness pulls it via
  `adb pull` / `simctl get_app_container` / `devicectl` — CLI, not MCP (the
  device-path precedent from the store-release flow).

## Tests

- `BenchmarkRecorderTests` (unit) — the pure `BenchmarkSceneStats` aggregation
  math (min/avg/max/p50/p95/p99 exactness on a known distribution, phase/alloc/
  gpu means) and the JSONL artifact shape (every line valid JSON with the
  documented keys, meta + scene + summary).
- `player_benchmark_selfcheck` (integration, both flavors) — runs a live project
  armed for ~90 frames, finalizes and asserts the artifact exists and parses
  with a meta line, a scene record carrying `frames > 0` and a measured
  `frameMs.avg`, and a clean summary.
- `player_benchmark_vista` (integration, both flavors) — runs the whole benchmark
  showcase project (below) as one autonomous tour and asserts EVERY scene lands
  in the artifact plus a clean summary (`tests/integration_driver/run_benchmark_test.py`).

## The benchmark project

`projects/benchmark/` is a stock `.orkproj` that IS the benchmark: a
`LevelManager` sequence of self-running vignette scenes, each a feature demo
scored by the recorder. It is generated end to end by
`Util/make_benchmark_assets.py` (terrain, PBS-prop, particle and GUI-atlas
assets come from the tree's shared generators it invokes; the scenes,
localisation, `.oui` screens, `.oshape`/`.omat`/`.oanim` content, `levels.olevels`,
`physics.olayers` and the manifest are written by it). Nothing is downloaded.

A single shared director script (`scripts/director.component.lua`, one
`director` component kind whose `mode` export picks the vignette) runs each
scene with **no input**: it sets up the camera + atmosphere, drives the scene's
motion or stress ramp, draws a small HUD, and after a frame budget wipes to the
next scene — looping on the results card. The nine scenes are a terrain vista
with a day→night sun arc + PSSM shadows + weather, a water lake, a night point-
light ramp, a 3D-particle swarm, an instance field (one mesh + one material,
Hlms auto-batch), a flat-colour 2D showcase (vector soft-bodies, morph, sprite
parallax, vector animation), a `.oui`/localisation GUI screen, a physics body
cascade with a time-scale hitstop, and a GUI results card.

Timing is **frame-count based, scaled by the `benchmark.sceneScale` cvar**
(default 1): a scene lasts `seconds * 60 * scale` frames, so a normal vsync-paced
run reads as seconds while an automated run shrinks scale to a handful of
deterministic frames per scene. `benchmark.wipe` (0 skips the fade wipe) and
`benchmark.lightCeiling` (the night-lights ramp ceiling) are the other knobs.
The stress scenes self-limit: a ramp stops adding load once a frame exceeds the
budget, so the recorded scene tells you where the device stalled. Two more
cvars exist purely as automation seams: `benchmark.rampBudgetMs` overrides the
self-limit budget (a probe forces the ramp to its ceiling regardless of the
machine's frame cost) and `benchmark.cameraOrbit` (0 freezes the wall-time
showcase orbit at the init framing, so a captured frame is machine-independent).

The tour must also LOOK right per flavor: beside the artifact tests, the
per-vignette pixel probes (`tests/integration_driver/run_benchmark_scene_probe.py`,
ctests `player_benchmark_lumens_probe` / `_field_probe` / `_hud2x_probe`) boot
single scenes deterministically and assert measured discriminators — the
moonlit-night corridor + visible lamp pools (lumens), lit instance cubes
(field), and vertically disjoint HUD text rows at a simulated 2x display
density (hud2x).

### Running it

- **Editor**: open the project and press Play (or pick an iOS-simulator / Android
  target) — the tour runs itself; arm the run by launching the player with
  `ORKIGE_BENCHMARK` in its environment.
- **Player CLI, armed**: `ORKIGE_BENCHMARK=1 ORKIGE_BUILD_SHA=$(git rev-parse --short HEAD) \
  orkige_player --project projects/benchmark` — writes `benchmark-<stamp>.jsonl`
  to the writable app dir (override with `ORKIGE_BENCHMARK_DIR`). A capped run
  ends via `ORKIGE_DEMO_FRAMES=N`.
- **MCP agent** (the editor's endpoint): open/scan the project, `play {scene:
  "scenes/vista.oscene"}` with `ORKIGE_BENCHMARK` in the player's environment
  (the existing `play` verb — no new verb), poll `get_state`, then read the run
  with `get_benchmark_results`. Per-scene labels are the recorder line names
  ("Terrace Vista", "Night Lumens", …); each also leaves a 1-frame boundary
  fragment named by the scene path (the level-switch open before the director's
  `benchmark.begin` renames it) — filter to records with `frames >= 2`.

### Backend note

Each scene runs on BOTH render flavors in isolation, and the whole tour now runs
cleanly on both. The classic GL/RTSS backend previously faulted over the long
sequence: its 2D-overlay (HUD) materials share the RTSS scheme, so when the
scene's dynamic light count changes (two-plus dynamic point lights over an
`.omat`-lit mesh mark every scheme technique for rebuild) or a scene teardown
churns materials, the RTSS transiently drops those materials' generated
technique — and the `DrawLayer2D` composite dereferenced a null best technique.
The classic composite now recompiles-or-skips on that transient
(`engine_render_classic/DrawLayer2DClassic.cpp`), so the `player_benchmark_vista`
and `player_benchmark_selfcheck` ctests run on both flavors. The
`benchmark.lightCeiling` cvar bounds the night-lights ramp for a tighter budget.
