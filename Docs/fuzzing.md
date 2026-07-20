# Fuzzing the pure parsers

Orkige loads several plain-text and binary asset formats by hand-rolled
parser. Some of those bytes are **untrusted**: a downloaded pak or an APK
central directory is read on every mobile boot, and a `.omat` / `.oanim` /
`.oshape` / JSON payload can arrive over MCP or from a project an agent
authored. This package puts those parsers under a
[libFuzzer](https://llvm.org/docs/LibFuzzer.html) harness with AddressSanitizer
and UndefinedBehaviorSanitizer, so a malformed or hostile input surfaces as a
test failure rather than a crash / out-of-memory / memory-corruption on a
player.

The harnesses live in `tests/fuzz/`, one `LLVMFuzzerTestOneInput` per target,
each linking only the minimal library the parser needs. They are pure and
headless - no renderer, no window, no engine boot.

## Targets

| Target             | Parser                                   | What it eats |
|--------------------|------------------------------------------|--------------|
| `fuzz_minizip`     | `engine_filesystem/MiniZip`              | a zip central directory + STORED/DEFLATE entries (an untrusted pak / APK) |
| `fuzz_material`    | `core_util/MaterialAsset`                | a `.omat` PBS material text asset |
| `fuzz_vectoranim`  | `core_util/VectorAnimAsset`              | a `.oanim` keyframed vector-animation rig |
| `fuzz_vectorshape` | `core_util/VectorShapeAsset`             | a `.oshape` tessellated-shape asset |
| `fuzz_json`        | `core_debugnet/Json`                     | the hand-rolled JSON codec behind the MCP / JSON-RPC endpoint |

`fuzz_material` and `fuzz_json` also exercise the matching `serialize()` and
re-parse a well-formed value, so the writer is fuzzed alongside the reader.

Parsers deliberately **not** fuzzed here:

- `engine_gui/GuiLayout` (`.oui`) - the parse logic is a simple line-based
  `key = value` reader, but its header pulls `engine_render/RenderMath.h`
  (Ogre math types), so the TU cannot compile in a core-only fuzz build without
  the whole OGRE include closure. Left out until a fuzz build worth the engine
  dependency is justified.
- `core_util/StringTable` (XLIFF) - the actual XML parsing is delegated to
  **tinyxml2**, a mature, separately-fuzzed library; StringTable reads a file
  path and does thin bookkeeping on top of the parsed DOM, so there is little
  hand-rolled byte-handling to fuzz.

## Build

The fuzz targets are gated behind the `ORKIGE_FUZZ` CMake option and are a
**host-only, core-only** build (no engine / OGRE). They need a Clang or
Apple-clang toolchain (libFuzzer ships as `-fsanitize=fuzzer`). Configure the
tree with the sanitizers on so the linked `orkige_core` parser code is
instrumented too:

```sh
cmake -S . -B build/fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DORKIGE_BUILD_ENGINE=OFF -DORKIGE_BUILD_TOOLS=OFF -DORKIGE_BUILD_TESTS=OFF \
  -DORKIGE_FUZZ=ON -DORKIGE_ENABLE_SANITIZERS=ON
cmake --build build/fuzz --target \
  fuzz_material fuzz_json fuzz_vectoranim fuzz_vectorshape fuzz_minizip
```

The `tests/fuzz/CMakeLists.txt` block is self-contained: each executable adds
its own `-fsanitize=fuzzer,address,undefined`; it never touches the global
sanitizer options. Without `-DORKIGE_ENABLE_SANITIZERS=ON` the harness still
links and runs, but only the harness / directly-compiled `MiniZip.cpp` TUs are
instrumented - always pair the two flags.

> Apple-clang does **not** ship the libFuzzer runtime
> (`libclang_rt.fuzzer_osx.a`), only the ASan/UBSan ones. On such a macOS host,
> build and run the targets inside the Linux rig (`Util/linux_rig/`, clang-18
> with libFuzzer) - the documented local twin of the Linux CI jobs.

## Run locally

Each executable is a standard libFuzzer binary. Point it at its seed corpus and
give it a time budget:

```sh
# a quick smoke: replay the seeds + a few seconds of mutation
build/fuzz/tests/fuzz/fuzz_minizip -max_total_time=30 tests/fuzz/corpus/fuzz_minizip

# a longer local hunt into a growing corpus dir (keep the coverage between runs)
mkdir -p /tmp/mz
build/fuzz/tests/fuzz/fuzz_minizip -max_total_time=600 \
  -rss_limit_mb=4096 -malloc_limit_mb=2048 \
  /tmp/mz tests/fuzz/corpus/fuzz_minizip

# reproduce one crashing input
build/fuzz/tests/fuzz/fuzz_minizip path/to/crash-or-oom-file
```

`-malloc_limit_mb` is worth setting: it turns a single oversized allocation
(the classic "declared size is huge, actual data is tiny" DoS) into an
immediate, reproducible finding instead of a slow swap-death.

A bounded `ctest` smoke leg is registered per target under the label `fuzz`
(only in a tree configured with `-DORKIGE_FUZZ=ON`):

```sh
ctest --test-dir build/fuzz -L fuzz
```

## Corpus and regression seeds

Each target has a seed corpus under `tests/fuzz/corpus/<target>/`:

- **Seeds from real structure** - real `.omat` / `.oanim` / `.oshape` assets
  copied from the repo, real JSON-RPC replies, and small valid zips - so the
  fuzzer starts from valid grammar instead of discovering it byte by byte. The
  binary seeds (zips, JSON) are (re)generated by the stdlib-only
  `tests/fuzz/make_seeds.py`; the text seeds are committed copies.
- **Regression seeds** - a `regression_*` file is a once-crashing input,
  committed so the fuzzer (and the PR smoke leg) replays it forever and fails
  if a fix is ever undone. When a fuzzer finds a bug, minimize the artifact,
  fix the parser, and drop the crashing input in as `regression_<what>.<ext>`.

**The rule: a fuzzer-found crash / OOM / UAF / UB / leak is a real bug.** Fix
the parser (a bounds check, an honest `false` return - never a silent
truncation), add the input as a regression seed, and note the finding.

## CI cadence

`.github/workflows/fuzz.yml`:

- **nightly cron** - each target fuzzed for the full per-target budget;
- **workflow_dispatch** - manual run with a tunable `seconds` per target;
- **per-pull-request smoke** - a short leg (triggered only when a fuzzed
  parser / harness / the fuzz plumbing changes) that mainly replays the
  regression seeds and briefly mutates, so a reopened crash is caught fast.

Every leg runs with `-error_exitcode=1`, so a finding fails the job; the
crashing artifacts are uploaded for triage.

## Findings on record

The initial sweep found two unbounded-allocation denial-of-service bugs, both
where an **untrusted declared size / count drove an up-front allocation** with
no bound against the actual input - fixed by rejecting sizes that exceed the
physical input (MiniZip) and by treating declared counts as capacity hints,
never allocation sizes (VectorAnimAsset):

- **MiniZip** - a forged End-Of-Central-Directory could declare a
  `centralDirSize` / entry `compressedSize` / `uncompressedSize` of up to 4 GB
  on a tiny file, driving a multi-gigabyte allocation on mount (a downloaded
  pak / an APK is untrusted on every mobile boot). Fixed by bounding the
  directory and each entry's compressed span against the real file size, and by
  inflating DEFLATE incrementally (the declared uncompressed size is verified as
  a ceiling, never pre-allocated). Regression seed:
  `corpus/fuzz_minizip/regression_oom_centraldir_size.zip`.
- **VectorAnimAsset** - a `shape k <N>` / channel / `contour` / `hole` / `mask`
  / gradient count reserved `N` elements up front; `shape k 45000250` in a
  115-byte file reserved ~12.6 GB. Fixed by capping the reservation (the run is
  validated element by element as it is read anyway). Regression seed:
  `corpus/fuzz_vectoranim/regression_oom_shape_count.oanim`.
