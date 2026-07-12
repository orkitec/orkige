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
- `player_benchmark_selfcheck` (integration, next flavor) — runs a live project
  armed for ~90 frames, finalizes and asserts the artifact exists and parses
  with a meta line, a scene record carrying `frames > 0` and a measured
  `frameMs.avg`, and a clean summary.
