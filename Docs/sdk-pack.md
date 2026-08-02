# The SDK pack

The SDK pack is the engine in RELOCATABLE form: one self-contained directory
that compiles, links and runs a project's native C++ game code on a machine
with no engine checkout and no engine build tree. It is what lets compiled game
components work from a downloaded editor exactly as they do from a source
build — the build tree a native module normally resolves
(`Docs/native-modules.md`) spells absolute paths and keeps its dependency
closure inside itself, so a downloaded editor cannot hand one out.

Build it from any desktop engine tree:

```sh
cmake --build --preset macos-release
cmake --install build/macos-release --prefix ~/orkige-sdk --component sdk
```

`--component sdk` is the pack; the engine graph installs nothing else.

## Layout

The one thing a consumer must know is the pack root.

| Path | What |
| --- | --- |
| `include/` | every engine header, LAYER-ROOTED exactly as an include line spells it: `core_util/String.h`, `engine_graphic/Engine.h`. Both layers merge into one root because their module directories are disjoint (`core_*` vs `engine_*`), so one include path serves both and consumer include lines are unchanged. |
| `lib/` | `liborkige_core` + `liborkige_engine` |
| `media/` | the engine's runtime media (fonts, the shared water plane) |
| `cmake/` | `OrkigeConfig.cmake` + `OrkigeConfigVersion.cmake` (the ABI stamp), `OrkigeGameModule.cmake`, `OrkigeAbiStamp.cmake`, `OrkigeWriteVersion.cmake` and `OrkigeSdkPack.cmake` |
| `vcpkg/` | the dependency closure — the exact binaries the engine archives were compiled and linked against |

Size follows the configuration, and a distribution pack is a **Release** one:
about **250 MB**, dominated by the dependency closure (~181 MB: OGRE and its
render systems, the image and mesh codecs, SDL3, OpenAL, Jolt, Lua), with the
two engine archives ~59 MB and the headers ~2 MB. A Debug pack — a development
and test artifact, not something to ship — is around **3.2 GB**, because both
the engine archives (~590 MB) and every dependency archive carry debug info.

`cmake/OrkigeSdk.cmake` writes the pack. `cmake/OrkigeSdkPack.cmake`, which
ships inside it, is the pack's own description of itself and the ONE place the
layout above is written down.

## Building a project against it

A native module includes the pack's own copy of the game-module helper and
passes nothing else:

```sh
cmake -G Ninja -S <project>/native -B <project>/native/build-sdk \
    -DCMAKE_BUILD_TYPE=Release -DORKIGE_ROOT=<pack>
```

…where `CMAKE_BUILD_TYPE` matches the configuration the pack records. That is
the same `include(${ORKIGE_ROOT}/cmake/OrkigeGameModule.cmake)` line a module
uses against an engine checkout — `cmake/OrkigeGameModule.cmake` is ONE file
serving both forms. It detects a pack by the `OrkigeSdkPack.cmake` sitting
beside it (a build tree carries no such file) and then takes the package
directory, the dependency closure prefix, the render flavor, the scripting
backend, the compile contract and the engine include roots from the pack
instead of from an engine source tree and a `CMakeCache.txt`. Everything after
that — the flavor's OGRE closure, the archive link order — is the same code.

Game code needs no change to build against a pack: it spells only facade types,
and its include lines are identical. One runtime habit does matter — **state
your media roots**. The engine archive carries a compile-time default for the
OGRE media (Hlms shader templates / the classic RTSS library) that names the
tree the archive was built in, which is not there for a module built against a
pack. `orkige_game_module()` bakes the correct answer in as
`ORKIGE_MODULE_MEDIA_DIR`, so a module sets both `AppHostConfig` media fields
from it unconditionally (`projects/jumper-native/native/main.cpp` is the
reference; against a build tree the value IS the baked default, so a dev run is
unchanged).

## The compile contract

A consumer's own translation units must be compiled with the same ABI-relevant
definitions the engine archives were, or its objects disagree with the archive
about struct layout and inline behaviour. The set is not small and it is not
static: `ORKIGE_STATIC`, the render-flavor macro, `USE_RTSHADER_SYSTEM`,
`ORKIGE_OPENAL_SOUND`, `ORKIGE_ENGINE_HAS_GOCOMPONENT`, `ORKIGE_HTTP`, the
scripting-backend define, `ORKIGE_HAVE_VULKAN`, the standard-library hardening
switch, and on Windows `NOMINMAX`/`WIN32_LEAN_AND_MEAN`.

So it is **captured, never restated**. `cmake/OrkigePackage.cmake` reads it off
the engine itself — the root directory's `COMPILE_DEFINITIONS` plus each
archive's `INTERFACE_COMPILE_DEFINITIONS`, i.e. exactly what the engine
declares PUBLIC — and the package records it as
`ORKIGE_CORE_COMPILE_DEFINITIONS` / `ORKIGE_ENGINE_COMPILE_DEFINITIONS`. The
imported targets carry it, and `orkige_game_module()` applies it verbatim. A
hand-kept copy in a config and another in a link helper is a drift trap: the
engine grows a define, the copies do not, and nothing says so until a
consumer's binary behaves oddly.

The engine's own build-tree-absolute paths are declared PRIVATE and are
therefore correctly absent — they are implementation, not contract, and a pack
must not carry them.

The dependencies' contract (Jolt's, OGRE's, SDL's) is not copied: it rides the
imported targets a consumer links, which carry it themselves. That is only
sound while the closure is the same configuration as the archives, which is the
rule above.

## Relocatable, and how that is known

Every path the pack's `OrkigeConfig.cmake` hands out is derived from the
config's own directory, so unpacking anywhere answers correctly. The
dependency closure relocates too: vcpkg's generated cmake configs and
pkg-config files resolve against `_IMPORT_PREFIX`/`${pcfiledir}` and bake no
build-machine path at all, which is what makes copying the closure into the
pack sufficient rather than a re-resolve.

The `sdk_pack` ctest (per flavor) proves it, in seven legs:

1. **install + relocate** — `cmake --install` into a scratch prefix, then
   RENAME the result. Every later leg reads only the renamed copy, so a path
   baked at install time fails in the suite rather than on a user's machine.
2. **self-contained** — no file in the pack's cmake surface, or in the
   closure's cmake/pkg-config files, may name the engine source tree, the
   engine build tree or the machine's vcpkg root.
3. **configuration** — the engine archives and the dependency closure must be
   the same configuration, with the other half absent entirely and its
   per-config target files pruned.
4. **surface** — every engine header the source tree carries must be IN the
   pack at the same layer-rooted path, and a single translation unit that
   includes all of them must compile and link against it. Both lists come from
   the SOURCE tree, never from the pack, so the check cannot go circular.
5. **acceptance** — `projects/jumper-native` configures, builds and RUNS
   against the pack inside a clean room (a macOS sandbox profile) where the
   engine source tree and the engine build tree are DENIED. Denial is the
   point: a build tree at its usual absolute path would silently satisfy a
   configure that should have failed.
6. **compile contract** — every definition the package records must appear on
   the module's actual compiler command line, read back from its
   `compile_commands.json`. Leg 5 only notices a missing definition when
   something happens to reference the symbol it changes; this notices the
   layout and inline-behaviour cases that would otherwise link and then
   misbehave.
7. **ABI guard** — an installed header edited in a throwaway copy of the pack
   makes the configure refuse with the ABI-mismatch diagnostic.

The pack is removed when all legs pass and kept when one fails, which is when
the several gigabytes of a Debug pack are worth the disk.

On platforms without a path sandbox the acceptance leg still runs from staged
copies outside the repository and leg 2 still audits the pack, but the engine
tree is not made unreachable there; macOS carries the strict form.

## The public header surface

A pack installs the COMPLETE header set of both engine layers. "Public" is not
a distinction this tree draws: includes are layer-rooted with no public/private
split, and the umbrella, Meta and template headers cross-reference each other
freely, so a carve would be a guess that fails at a consumer's first unusual
include. The whole set is ~2 MB against a pack dominated by the dependency
closure, so shipping it all costs nothing measurable and the surface probe
keeps the install set complete.

What that costs in honesty: the two render backends' own headers
(`engine_render_classic/`, `engine_render_next/`) are reachable in a pack.
Reachable is not sanctioned — they are the engine's private implementation of
the `engine_render` facade and include their OGRE flavor's umbrella. Game code
above the facade spells only facade types, exactly as in-tree code does
(`Docs/render-abstraction.md`). The surface probe leaves out the backend the
pack's flavor does not build, together with anything that reaches it through an
unconditional include; that set is derived from the include graph rather than
listed by hand, so it cannot rot.

## The ABI stamp in a pack

The stale-library guard keeps its shape and its teeth. A build tree compares
the fingerprint of the engine sources on disk against the one its archives were
built from (`Docs/native-modules.md`). A pack has no `.cpp` files, but it
carries the surface that decides object layout for a consumer — the installed
headers plus the cmake files defining the compile and link — and
`OrkigeSdkPack.cmake` names exactly that surface for both sides.
`cmake/OrkigeSdk.cmake` records the stamp of it at install time through the
same writer the build-tree package uses, and the game-module helper recomputes
it over the pack it is handed. So a half-unpacked, truncated or hand-edited
pack is a hard configure error naming both fingerprints, never a skewed object
layout at run time. A module that pins an explicit
`ORKIGE_EXPECTED_ABI_VERSION` still gets the EXACT match it asks for, so
pinning one engine version and being handed another is refused as well.

## One configuration, all the way through

A pack carries exactly ONE configuration, and it is the one its engine archives
were built in: `cmake/OrkigeSdk.cmake` ships the matching half of the
dependency closure and nothing of the other.

This is correctness, not tidiness. A dependency's headers compile differently
per configuration — Jolt enables its asserts wherever `NDEBUG` is absent, and
says so through its own per-configuration interface definitions — so a Debug
engine archive contains calls into code that only the debug build of that
dependency defines. Ship the other half and the mismatch surfaces as an
undefined symbol at a consumer's link, which is the LUCKY outcome; where the
difference is struct layout or an inline body instead of a referenced symbol,
the module links and then misbehaves. On MSVC the constraint is sharper still:
the tree pins `x64-windows-static-md`, so the C runtime itself differs and
`/MD` and `/MDd` objects cannot live in one image at all.

For the same reason the game-module helper REFUSES a module configured in a
different build type than the pack records, naming both. And because a port's
main `*Targets.cmake` globs its siblings and includes them all, the per-config
imported-target files of the absent half are pruned as the pack is assembled —
a leftover would name archives that are not there and turn every consumer
configure into an import-check failure.

A **distribution** pack is therefore installed from a Release engine tree. A
Debug pack is legitimate and correct, just large, and exists for development
and for the suite.

## Scope

Desktop host packs. iOS, Android and wasm packs are not built — mobile and
browser game code goes through the export pipeline
(`Docs/web-export.md`, `Docs/ios-signing.md`), and a native module is a desktop
build. The editor does not yet consume a pack; it builds compile-on-Play
against its own engine tree (`Docs/native-modules.md`).
