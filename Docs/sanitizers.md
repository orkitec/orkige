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
through. Every entry names its non-Orkige source and the reason. The current
entries are all Ogre-Next's own scene-update / frustum-culling / clustered-
forward worker threads and the threading primitives that drive them.

**A race in Orkige's own code is a bug, not a suppression.** It is fixed, never
added to the file. Because the CI gate is headless, none of the suppressed
third-party threads even start there — the suppressions matter only for local
windowed runs under TSan.

The file is wired in through `TSAN_OPTIONS`:

```sh
export TSAN_OPTIONS="halt_on_error=1:history_size=4:suppressions=$PWD/Util/tsan_suppressions.txt"
```

## Running it locally

The macOS Apple-clang toolchain carries TSan. There is no committed macOS TSan
preset (like ASan, the instrumented presets are Linux-only); configure a tree
by hand off the `macos-debug` settings with `-DORKIGE_TSAN=ON`:

```sh
export PATH=/opt/homebrew/bin:$PATH
VCPKG_ROOT=$HOME/Development/vcpkg cmake -S . -B build/macos-debug-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_SYSROOT=macosx \
  -DCMAKE_IGNORE_PREFIX_PATH=/usr/local \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DPKG_CONFIG_EXECUTABLE=/opt/homebrew/bin/pkg-config \
  -DVCPKG_OVERLAY_TRIPLETS=$PWD/triplets -DVCPKG_OVERLAY_PORTS=$PWD/ports \
  -DORKIGE_RENDER_BACKEND=next -DVCPKG_MANIFEST_FEATURES=render-next \
  -DORKIGE_TSAN=ON
cmake --build build/macos-debug-tsan --target orkige_core_tests
ALSOFT_DRIVERS=null \
  TSAN_OPTIONS="suppressions=$PWD/Util/tsan_suppressions.txt" \
  ./build/macos-debug-tsan/tests/core/orkige_core_tests
```

A macOS TSan run proves the configuration builds, links and runs clean, but its
libc++ runtime does not match CI exactly. The **container rig is the gold
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
