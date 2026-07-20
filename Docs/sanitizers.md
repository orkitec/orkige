# Sanitizer gates: ASan/UBSan and ThreadSanitizer

Instrumented CI builds catch faults a plain build hides. Two gates run, each in
its own dedicated build tree (the sanitizer runtimes are mutually exclusive, so
they cannot share one instrumented closure):

- **AddressSanitizer + UndefinedBehaviorSanitizer** (`linux-debug-sanitize`,
  CI job `linux-sanitizer`) — memory-safety and UB across the full non-device
  suite (unit + desktop integration).
- **ThreadSanitizer** (`linux-debug-tsan`, CI job `linux-tsan`) — data-race
  detection across the engine's threaded seams.

Both run on **libstdc++** in the Linux CI environment (and the local container
rig), where race/lifetime bugs surface that libc++ on macOS can mask. Both run
with **zero retries** so a finding is never hidden by a re-run.

## What the TSan gate covers

The gate runs the **headless unit suite** (`ctest --preset unit-linux-tsan`).
That suite exercises the engine's threaded seams without a display:

- the physics **contact-event path** — Jolt worker-thread callbacks pushed onto
  a mutex queue and drained on the main thread (`engine_physic`);
- the **debug-protocol sockets** — the non-blocking socket layer under
  `core_debugnet` (the editor↔player link, the HTTP/JSON-RPC MCP transport);
- the **relaxed-atomic tables** in `core_debug` — `MemoryManager`'s per-frame
  allocation counters and the `LogLevels` per-tag threshold table (these use
  `std::atomic` deliberately; TSan understands atomics, so correct relaxed
  atomics are NOT flagged — a flag here would mean a genuine non-atomic race);
- **`Breadcrumbs`** — the always-on flushed ring.

The **windowed integration suite is deliberately excluded** from the TSan gate.
Under TSan the render backend's worker threads and the GL/Vulkan driver produce
races the engine cannot fix (only suppress), which would drown a genuine Orkige
race in noise. The unit gate is the meaningful, low-noise slice; the boot/
teardown and soak instruments in [soak.md](soak.md) cover the full-stack
lifecycle separately.

## Suppressions policy

`Util/tsan_suppressions.txt` suppresses **only races the engine cannot fix** —
third-party worker threads whose internal synchronisation TSan cannot see
through. Every entry names its non-Orkige source and the reason. Two families:
Ogre-Next's own scene-update / frustum-culling / clustered-forward worker
threads (plus the threading primitives that drive them), and **Jolt** (`JPH::`),
which runs the physics simulation across its own job-system worker pool over
lock-free atomics. The Jolt entry is namespace-wide (`race:JPH::`) because a race
sited anywhere in the `JPH::` namespace is Jolt-internal by construction — the
engine drives Jolt only from the main thread (`engine_physic/PhysicsWorld`) and
drains contact events through an owned mutex queue, which is `Orkige::` code and
stays unsuppressed.

**A race in Orkige's own code is a bug, not a suppression.** It is fixed, never
added to the file. The Ogre-Next entries bite only local windowed runs (the
render backend threads); the **Jolt entries bite the headless UNIT gate too** —
the physics tests drive Jolt's worker pool, so the gate does exercise
third-party threads (this is why the suppression file is wired into
`unit-linux-tsan`, not just the windowed presets).

**Suppress vs exclude.** The physics tests are kept in the gate (with Jolt
suppressed) because they also exercise Orkige's own contact-event queue — real
TSan value. The one AL-device test (`SoundManagerDestructorTearsDownAL`) is
instead **excluded** from the TSan presets outright: it spins OpenAL Soft's
internal mixer thread but carries no Orkige-owned threading (the engine drives
OpenAL entirely from the main thread), so under TSan it is pure third-party
noise — and `halt_on_error=1` stops at the first of OpenAL's several internal
race sites, so a suppression would be an open-ended chase. Its teardown
memory-safety stays covered by the ASan gate. Exclusion lives in the
`unit-*-tsan` test-preset `filter.exclude.name`.

The file is wired in through `TSAN_OPTIONS`:

```sh
export TSAN_OPTIONS="halt_on_error=1:history_size=4:suppressions=$PWD/Util/tsan_suppressions.txt"
```

## Running it locally

The macOS Apple-clang toolchain carries TSan, and the `macos-debug-tsan` preset
(a sibling of `macos-debug` with `-DORKIGE_TSAN=ON`) drives it. The
`unit-macos-tsan` test preset runs the same headless unit gate as CI's
`unit-linux-tsan`:

```sh
export PATH=/opt/homebrew/bin:$PATH
VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset macos-debug-tsan \
  -DPKG_CONFIG_EXECUTABLE=/opt/homebrew/bin/pkg-config
cmake --build --preset macos-debug-tsan
ALSOFT_DRIVERS=null \
  TSAN_OPTIONS="halt_on_error=1:suppressions=$PWD/Util/tsan_suppressions.txt" \
  ctest --preset unit-macos-tsan
```

The physics tests reproduce Jolt's worker-thread races here (the `JPH::` symbols
are platform-independent, so the suppression entries validate on macOS). A macOS
TSan run proves the configuration builds, links and runs clean, but its libc++
runtime does not match CI exactly. The **container rig is the gold
proof**: `Util/linux_rig/run_container.sh` reproduces the CI libstdc++
environment, where every `linux-*` preset works. Inside the container:

```sh
bash Util/ci_configure.sh --preset linux-debug-tsan \
  -DVCPKG_INSTALL_OPTIONS="--clean-after-build;--allow-unsupported"
cmake --build --preset linux-debug-tsan
TSAN_OPTIONS="halt_on_error=1:suppressions=$PWD/Util/tsan_suppressions.txt" \
  ALSOFT_DRIVERS=null ctest --preset unit-linux-tsan
```

`ORKIGE_TSAN` and `ORKIGE_ENABLE_SANITIZERS` are mutually exclusive — enabling
both FATAL_ERRORs at configure. Keep one instrumented tree per sanitizer.
