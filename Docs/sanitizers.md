# Dynamic-analysis gates: ASan/UBSan/LSan, ThreadSanitizer and Valgrind

Instrumented CI builds catch faults a plain build hides. The compile-time
sanitizer gates run each in their own dedicated build tree (the sanitizer
runtimes are mutually exclusive, so they cannot share one instrumented
closure):

- **AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer**
  (`linux-debug-sanitize`, CI job `linux-sanitizer`) — memory-safety, UB and
  memory leaks across the full non-device suite (unit + desktop integration).
- **ThreadSanitizer** (`linux-debug-tsan`, CI job `linux-tsan`) — data-race
  detection across the engine's threaded seams.

Both run on **libstdc++** in the Linux CI environment (and the local container
rig), where race/lifetime bugs surface that libc++ on macOS can mask. Both run
with **zero retries** so a finding is never hidden by a re-run.

Two more layers sit beside the compile-time gates:

- **Standard-library hardening** — the cheap always-on STL bounds / iterator /
  precondition checks, compiled into every Debug and sanitizer build (see
  [below](#standard-library-hardening)).
- **Valgrind Memcheck** (`valgrind.yml`, nightly) — the uninitialized-read
  watch the compile-time sanitizers do not cover, on an uninstrumented binary
  (see [below](#valgrind-memcheck-nightly--the-uninitialized-read-watch)).

## LeakSanitizer on the ASan gate

LeakSanitizer ships inside the AddressSanitizer runtime, so the ASan gate turns
it **on** at run time — `ASAN_OPTIONS=detect_leaks=1` — for no extra build. A
memory leak in Orkige's own code then fails the gate exactly like a
use-after-free would.

LSan reports only allocations that are **unreachable at process exit**
(definitely / indirectly lost); it never flags blocks still reachable from a
global or static pointer. This is why the third-party engine singletons a game
holds for its whole lifetime — the Ogre root, the SDL subsystems, Jolt's
factory, the OpenAL device — do **not** appear: they stay reachable through
their owning globals at exit. The engine's own teardown is leak-disciplined
(the `GameObjectManager::clear` hook, the event/manager destructors), so the
full unit + desktop integration suite runs **clean** under `detect_leaks=1`.

`Util/lsan_suppressions.txt` (wired via
`LSAN_OPTIONS=suppressions=…`) exists for genuinely third-party / intentional
process-lifetime leaks only, each entry naming its non-Orkige source. **A leak
in Orkige's own code is a bug, not a suppression** — it is fixed, never added
to the file. The file currently carries no entries (the suite is leak-clean);
the goal is LSan catching FUTURE Orkige leaks, not a wall of suppressions.

Turning the gate on surfaced exactly one leak, and it was **fixed rather than
suppressed** — the pattern the policy exists to enforce. The editor's async
workers (the `run_tests` / export capture and the `adb`/`simctl` device probes
in `EditorControlServer`) are raw `std::thread` / `std::async` tasks that call
SDL functions. SDL stores its error message in per-thread storage it can only
reclaim for threads IT created, so each of those non-SDL workers left a small
error buffer behind when it exited (SDL's `SDL_CleanupTLS` was never run;
`SDL_Quit` does not cover it — the documented mechanism is to call
`SDL_CleanupTLS` before a non-SDL thread that touched SDL exits). The fix is an
RAII guard at the top of every such worker; no suppression was needed.

### UBSan check set

The UBSan gate runs the default undefined-behaviour check set (`undefined`,
minus `vptr` — RTTI-less static dependencies provide no typeinfo to link
against). The optional `integer` and `nullability` groups are **deliberately
left off**: `integer` flags legal unsigned wrap-around, which the engine relies
on in hashing and RNG (`sha1`, the fast-hash containers), so enabling it would
turn the gate red on well-defined code rather than surface a bug. Keep them off
unless a specific, clean sub-check is worth arming.

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

## Standard-library hardening

The C++ standard library can insert cheap always-on precondition checks into
its own containers — an out-of-range `operator[]`, an invalidated iterator, a
bad range become a **clean abort** instead of silent undefined behaviour. Orkige
compiles these into every **Debug** build and both sanitizer trees (the root
`CMakeLists.txt`, next to the sanitizer flags). It is a floor *under* the
sanitizers that also fires in a plain Debug run, at negligible cost.

The macro differs per standard library, so a configure-time compile probe picks
exactly the one for the library in use:

- **libstdc++** (the Linux clang/gcc toolchain): `_GLIBCXX_ASSERTIONS`. This is
  the ABI-neutral switch — unlike `_GLIBCXX_DEBUG` it does not change container
  layout, so hardened Orkige code links cleanly against the unhardened vcpkg
  dependencies.
- **libc++** (the macOS / Apple-clang toolchain): the graduated
  `_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE` on recent libc++
  (LLVM ≥ 18), with a probe-driven fallback to `_LIBCPP_ENABLE_ASSERTIONS=1` on
  older libc++. The define is applied across **all** C++ translation units
  (`.cpp` and `.mm` alike) on purpose: libc++ folds the hardening level into its
  ODR signature, so every TU must agree.

Both configs were rebuilt and the full unit suite re-run under hardening
(libc++ on macOS, libstdc++ in the container) with no new assertion trips.

## Valgrind Memcheck nightly — the uninitialized-read watch

ASan/UBSan and TSan cover memory safety, undefined behaviour and data races,
but **not reads of uninitialized memory** — the one class the compile-time
instrumented sanitizers miss (MemorySanitizer would need an MSan-instrumented
libc++/vcpkg closure the tree does not build). Valgrind's **Memcheck** fills
that gap: it shadows every byte's definedness on an **uninstrumented** binary,
so it needs no special build and no dependency rebuild. That is precisely why
this gate uses Valgrind, not MSan.

`.github/workflows/valgrind.yml` is a **scheduled** workflow (nightly cron +
`workflow_dispatch`, never on push — Memcheck's ~20–50× slowdown has no place in
the push gate). On an x86_64 Linux runner it builds the plain (non-sanitizer)
`linux-debug-next` tree and runs the **headless core unit binary**
(`orkige_core_tests`) — the same Catch2 suite the push gate runs — under:

```sh
valgrind --tool=memcheck --error-exitcode=1 --leak-check=no \
  --track-origins=yes --suppressions=Util/valgrind.supp \
  build/linux-debug-next/tests/core/orkige_core_tests "~[perf]"
```

`--error-exitcode=1` turns any Memcheck finding (an uninitialized read, an
invalid access) into a job failure; `--track-origins=yes` names where an
uninitialized value was born; leak-checking is off because LSan already owns
leaks. The `[perf]` scope test (hundreds of thousands of iterations) is excluded
so the run stays bounded without losing coverage of any distinct code path. Two
**may-not-skip** guards protect the gate: the binary must exist, and Memcheck
must reach its `ERROR SUMMARY` line (a crash before the summary must not read as
success).

`Util/valgrind.supp` suppresses only third-party Memcheck false positives, each
naming its non-Orkige source; Valgrind also loads the platform's built-in
`default.supp`, which already covers the glibc dynamic linker and C-runtime
startup. **A Memcheck error in Orkige's own code is a bug, not a suppression.**
The core binary currently runs with **zero** Memcheck errors and needs no
project suppressions.

