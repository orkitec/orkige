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
