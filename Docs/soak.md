# Stability instruments: boot/teardown cycling + the nightly soak

Two classes of fault never show in a 3-second unit run:

- **teardown-order and lifetime bugs** surface only when the WHOLE stack comes
  up and goes down — a dangling handle freed in the wrong order, a re-attach of
  released memory at shutdown. A single-boot selfcheck exits before the second
  cycle would expose them.
- **slow leaks and long-run faults** need the game running for minutes, cycling
  its scenes — a per-frame or per-scene-switch allocation that never comes back.

These instruments cover those gaps. Neither replaces the fast suites; they sit
alongside them, one as a ctest and one as a scheduled job.

## The boot/teardown cycling test (`boot_cycle_selfcheck`)

`tests/lifecycle/boot_cycle_main.cpp` boots the FULL runtime spine through
`AppHost` (SDL window + the per-flavor `Engine` + a live render system +
`GameObjectManager`), fills a small scene that reaches every risky teardown
edge, unloads it, and tears the whole stack down — **six times in one process**.
Each cycle:

1. `AppHost::boot` brings the stack up (a real render-system boot, not just the
   core singletons — the render backend is where the teardown bugs lived).
2. The scene enables a **live skybox cubemap** (`sky_day.dds`) + image-based
   lighting sourced from it, and creates `GameObject`s whose `ModelComponent`s
   hang mesh instances off the world graph while their siblings ride the
   per-frame update list.
3. `GameObjectManager::clear` (the scene teardown hook) unloads the scene while
   the render system is still live — the mid-session scene switch — then the
   scene is re-populated to prove `clear()` left the world reusable.
4. The `AppHost` destructor tears everything down in mirrored order, running
   `Engine::~Engine` → `RenderBackend::destroyRenderSystem` **with the skybox and
   IBL still armed**.

It exists because two shipped teardown bugs would each have failed here:

- a **skybox re-attach on `clearScene`** that dereferenced freed memory at
  shutdown. `SceneManager::clearScene` (run from `Root::shutdown`) re-attaches
  its cached sky quad unconditionally; a still-live skybox left that pointer
  dangling. The fix detaches the sky in `destroyRenderSystem` BEFORE the root
  tears the scene manager down — and this test keeps a skybox live at every
  teardown, so a regression trips it. (Confirmed live: each cycle logs
  `Parsing script SkyCubemap.material` / the sky shader compile.)
- a **heap-use-after-free in the `GameObjectManager` update-list teardown**
  (the update vector freed before the objects map, whose destructors re-enter
  it). The pure path has its own unit test (`GameObjectManagerTeardownTests`);
  this test exercises it under a live render system, where the object
  destructors also unwind real render nodes.

Both are memory-safety faults, so the test carries its weight under the CI
**AddressSanitizer** gate: it is labelled `integration` (not `device`), so the
instrumented Linux suite (`desktop-linux-sanitize`) runs it — a plain build
often masks a use-after-free a sanitizer build reports.

Run it (both flavors):

```sh
ALSOFT_DRIVERS=null ctest --test-dir build/macos-debug         -R boot_cycle_selfcheck
ALSOFT_DRIVERS=null ctest --test-dir build/macos-debug-classic -R boot_cycle_selfcheck
```

The cycle count is `ORKIGE_BOOT_CYCLES` (default 6); raise it for a longer
in-process soak of the boot/teardown path itself.

## The nightly soak (`soak.yml` + `Util/orkige_soak.py`)

`Util/orkige_soak.py` runs the standalone player over `projects/benchmark` in
**attract-loop mode** (the exported `benchmark.loop=1` cvar keeps the scene tour
cycling instead of holding on the results card) for a bounded frame budget,
samples the player's resident set size (RSS) across the run, and FAILS if the
player crashes / exits non-zero OR if RSS trends upward past a documented slope
ceiling.

It is **reuse, not new telemetry**: the run harness is the same env the
benchmark ctest driver uses (`ORKIGE_BENCHMARK` + `benchmark.sceneScale`, here
with a small scale so many scene load/unload cycles pack into the budget — the
churn a leak hides in), and RSS is the operating system's own accounting, the
SAME number `core_debug/MemorySampler` queries in-engine, read here from the
child process with `ps` so the driver needs nothing but the standard library.

The verdict discards a warmup fraction (the one-time boot + first-scene
allocations — shader compile, texture uploads — that are not a leak), then takes
a least-squares slope of RSS over the steady-state samples. A flat floor across
the loop is the pass; a rising floor is a leak suspect. Ceiling: **8 MB/min**
(`--max-slope-mb-per-min`); an optional absolute peak ceiling is
`--max-peak-mb`.

Flavor: **next** (Ogre-Next) only — the default backend the shipping player uses
on every platform, so it is the footprint a nightly watch must protect.

`soak.yml` runs on a nightly `schedule:` cron and on manual `workflow_dispatch`
(never per push — a minutes-long soak has no place in the push gate). It builds
the `orkige_player` target from the `linux-debug-next` tree and runs the soak
under xvfb + Mesa lavapipe, exactly like the linux-next desktop suite. Its
`uses:` refs are plain tags; the repo's action SHA-pins are applied centrally.

Validate the trend math locally without a player (the stdlib-only self-check):

```sh
python3 Util/orkige_soak.py --selftest
```

Run a real short soak against a built player (raise `--frames` for a longer
watch):

```sh
ALSOFT_DRIVERS=null python3 Util/orkige_soak.py \
  --repo "$PWD" \
  --player build/macos-debug/tools/player/orkige_player \
  --frames 1500 --interval 1
```
