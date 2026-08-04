# The SDK pack

The SDK pack is the engine in RELOCATABLE form: one self-contained directory
that compiles, links and runs a project's native C++ game code on a machine
with no engine checkout and no engine build tree. It is what lets compiled game
components work from a downloaded editor exactly as they do from a source
build — the build tree a native module normally resolves
(`Docs/native-modules.md`) spells absolute paths and keeps its dependency
closure inside itself, so a downloaded editor cannot hand one out.

Build it from any engine tree whose target the game-module helper derives a link
closure for — the desktop presets and `ios-simulator-*`:

```sh
cmake --build --preset macos-release
cmake --install build/macos-release --prefix ~/orkige-sdk --component sdk

cmake --build --preset ios-simulator-debug
cmake --install build/ios-simulator-debug --prefix ~/orkige-sdk-ios --component sdk
```

`--component sdk` is the pack; the engine graph installs nothing else.

A pack is bound to ONE target (see [The target contract](#the-target-contract)),
so a machine holds one per target it builds for, and the helper refuses a pack
whose target is not the one being configured.

## What you actually need, per project

The prerequisites are **separate tiers**, and none of them ever stands in for
another. This is the part that confuses, so it is worth stating plainly:

| Your project | To export for a phone you need |
| --- | --- |
| behaviour is **Lua** (no C++) | the platform's **player** — carried by a source build, fetched by a downloaded editor (`Docs/device-payloads.md`). Nothing else. |
| behaviour is **compiled C++** (`native.target`) | the **SDK pack for that target**, plus a C++ toolchain on the machine (on Apple targets, Xcode). There is no player to fetch: your module IS the app. |
| either, onto a **physical device or a store** | additionally the **signing credentials** — an identity and a provisioning profile, machine-local and never committed (`Docs/ios-signing.md`). |

**A project with no C++ never needs a pack — for any platform, in any
configuration, release and signed builds included.** A Lua game has nothing to
compile, so the SDK is not part of its story at all; it is neither required nor
mentioned. Equally, a project whose app is its own module has no use for a
prebuilt player. Each refusal names exactly the tier that is missing and links
to the page that explains it.

## Layout

The one thing a consumer must know is the pack root.

| Path | What |
| --- | --- |
| `include/` | every engine header, LAYER-ROOTED exactly as an include line spells it: `core_util/String.h`, `engine_graphic/Engine.h`. Both layers merge into one root because their module directories are disjoint (`core_*` vs `engine_*`), so one include path serves both and consumer include lines are unchanged. |
| `lib/` | `liborkige_core` + `liborkige_engine` |
| `media/` | the engine's runtime media (fonts, the shared water plane) |
| `cmake/` | `OrkigeConfig.cmake` + `OrkigeConfigVersion.cmake` (the ABI stamp), `OrkigeGameModule.cmake`, `OrkigeAbiStamp.cmake`, `OrkigeWriteVersion.cmake`, `OrkigeTargetShape.cmake` with what its recipes need (`OrkigeModuleEntry.cpp`, `apple/Info.plist.in`), a cross pack's `OrkigeSdkToolchain.cmake`, and `OrkigeSdkPack.cmake` |
| `vcpkg/` | the dependency closure — the exact binaries the engine archives were compiled and linked against |

`cmake/OrkigeSdk.cmake` writes the pack. `cmake/OrkigeSdkPack.cmake`, which
ships inside it, is the pack's own description of itself and the ONE place the
layout above is written down.

### What a pack does not carry

**No host executables.** The closure's `tools/` and `bin/` directories hold the
compressors, validators and uninstall scripts a port installs beside its
library. A module build configures and compiles; it invokes none of them. They
are pure size and license surface — and on macOS an executable inside a
downloaded archive carries the quarantine attribute, so shipping them would put
a Gatekeeper encounter on a user's machine that no development machine ever
sees. A pack that carries nothing runnable cannot have that problem.

**No editor or test dependencies, and no other flavor's renderer.** The closure
is the engine BUILD's closure, which also builds the editor and the suite:
their libraries would otherwise ride into every download. And a tree that builds
the Ogre-Next backend still installs classic OGRE beside it (it is a base
dependency), together with the windowing library only classic uses and the
immediate-mode UI classic's overlay is built against — a renderer that pack can
never load. Both are pruned, exactly rather than by pattern: vcpkg records which
files each port installed, and those manifests decide what goes, so a shared
dependency is never touched.

The set is not the same for both flavors, and it is not a guess. A classic pack
KEEPS the immediate-mode UI library, because classic OGRE's own package config
requires it and a pack without it fails a consumer's configure — which is
exactly how that entry was settled. An over-eager entry is never a quiet
mistake: the surface probe and the acceptance run below build and run a real
game from the pruned pack, on both flavors.

### Size

A distribution pack is a **Release** one: about **200 MB** for the default
flavor, dominated by the dependency closure (~139 MB: OGRE-Next and its render
systems, the image and mesh codecs, SDL3, Jolt, Lua), with the two
engine archives ~59 MB and the headers ~2 MB. Trimming host tools and the ports
above takes ~42 MB off the closure a Release next-flavor pack would otherwise
carry.

A Debug pack — a development and test artifact, not something to ship — is
**1.9 GB** on the default flavor and **2.5 GB** on classic, because both the
engine archives (~600–680 MB) and every dependency archive carry debug info.
The trim is worth ~1.2 GB on a Debug next pack, which matters directly: the
`sdk_pack` test installs one on every desktop-suite run.

## Building a project against it

A native module includes the pack's own copy of the game-module helper and
passes nothing else:

```sh
cmake -G Ninja -S <project>/native -B <project>/native/build-sdk \
    -DCMAKE_BUILD_TYPE=Release -DORKIGE_ROOT=<pack>
```

…where `CMAKE_BUILD_TYPE` matches the configuration the pack records.

### The configure vocabulary

These three are the whole interface, and they are final — a project's own
`CMakeLists.txt` is written against them, and those files belong to their
authors:

| Variable | Meaning |
| --- | --- |
| `ORKIGE_ROOT` | **the pack root** in pack mode, **the engine source root** against a checkout. One name, because a consumer says "where Orkige is" and the helper works out which form it was handed (see below). |
| `ORKIGE_ENGINE_BUILD_DIR` | the engine build tree to link. Required against a checkout, and meaningless — never passed — against a pack, which has no build tree. |
| `CMAKE_BUILD_TYPE` | must equal the configuration the pack records; a mismatch is refused by name. Against a checkout, either. |
| `CMAKE_TOOLCHAIN_FILE` | a **cross** pack's own `cmake/OrkigeSdkToolchain.cmake` (see below). Not passed for a host pack, which has none. |

`ORKIGE_EXPECTED_ABI_VERSION` stays available as an explicit pin for a project
that wants to name the engine version it was written against rather than accept
whatever it is handed.

### Writing the module

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_game LANGUAGES CXX)
include("${ORKIGE_ROOT}/cmake/OrkigeGameModule.cmake")
orkige_add_game_module(my_game main.cpp)
```

`orkige_add_game_module()` creates the target AND wires it, and that is what
keeps the file above portable. The **shape** a module takes is a property of the
target platform, not of the game: a desktop module is an executable the editor
runs as the play process, an Android module is the shared library the activity
loads by a fixed name, an Apple mobile module is a bundle. A project that spelled
`add_executable()` would be frozen to whichever shape it was written on, and
would have to be edited — by its author, in a file this engine does not own —
the day it targets a phone. So the helper owns the shape and the pack declares
which one applies.

For the same reason nothing guesses **where the artifact landed**:
`<buildDir>/<target>` is only the desktop answer. The helper writes
`orkige_module_artifact.txt` beside the build with the path the generator
resolved exactly, and the tools that launch or package a module read it
(`core_project/NativeModule.h`). A build too old to write one falls back to the
desktop guess, which is what it produced anyway.

`orkige_game_module(<existing target>)` remains available for a project that
must configure its own target first; it is the same wiring without the creation,
and such a project owns the shape question itself.

The include line is the same
`include(${ORKIGE_ROOT}/cmake/OrkigeGameModule.cmake)` a module uses against an
engine checkout — `cmake/OrkigeGameModule.cmake` is ONE file
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

## The target contract

A pack is bound to ONE target, and it says which: platform
(`macos`/`linux`/`windows`/`ios`/`ios-simulator`/`android`/`web`), architectures,
vcpkg triplet, module shape and output name, the OS floor its binaries were
built for, the toolchain kind/version/file/options a consumer must configure
with, and the compiler and standard library the archives came from.
`cmake/OrkigeSdkPack.cmake` inside the pack declares all of it.

Those field names are a public contract, for the same reason the configure
vocabulary is: packs are built per target and projects are written against the
words. Adding a field later is fine; changing what an existing one MEANS is
not. So every field is declared whether or not a given pack has an answer — a
slot with no answer is **empty, never absent**, and empty reads as "this
target needs nothing here". A host pack fills the platform, triplet, shape,
floor, compiler and standard library, and leaves the cross-toolchain slots
empty.

The helper derives the platform it is actually configuring for from the
toolchain in force — through `cmake/OrkigeTargetShape.cmake`, which ships in the
pack so both sides compute the same word for the same thing — and refuses a
pack whose recorded platform disagrees.

### A cross pack's toolchain file

We ship the engine, never a toolchain: the compiler and the platform SDK come
from the machine (Xcode, an NDK, an emsdk). What a **cross** pack owes a
consumer is the SETTINGS that make the machine's toolchain produce objects which
link with the archives in the pack — the system, the SDK, the architectures and
the OS floor. CMake reads those before it probes a compiler at all, which is the
one thing a cache variable cannot do, so they travel as a cmake toolchain file:
`cmake/OrkigeSdkToolchain.cmake`, realized at install time from what the engine
build actually cross-compiled with and named by `ORKIGE_SDK_TOOLCHAIN_FILE`
relative to the pack root, so the pack still relocates.

```sh
cmake -G Ninja -S <project>/native -B <project>/native/build-sdk-ios \
    -DCMAKE_TOOLCHAIN_FILE=<pack>/cmake/OrkigeSdkToolchain.cmake \
    -DCMAKE_BUILD_TYPE=<the pack's> -DORKIGE_ROOT=<pack>
```

`NativeModule::configureCommand` passes it, so the editor and the exporter do
this without a per-platform branch of their own; a host pack records an empty
field and nothing is passed.

Two details in that file earn their comments. The Apple SDK is recorded by its
**short name** (`iphonesimulator`), because CMake expands the setting to the
absolute `.sdk` path of whichever Xcode built the pack — a fact about the build
machine that names a directory a consumer does not have. And the file sets the
find-root modes to `BOTH`: a cross build defaults to re-rooting every
`find_package`/`find_library` into the sysroot ONLY, which is right for a
distribution whose libraries all live there and wrong for a pack the user
unpacked wherever they liked, where it makes the engine archives and the whole
bundled closure invisible. The pack's own closure is still what gets found,
because the game-module helper puts that prefix at the front of the search path.

### The OS floor

Without a pinned deployment target every object records the SDK's own minimum,
so a build on a current machine produces binaries that will not launch on
anything older. For a distributed editor and for the packs it hands out, that
is not a detail: it decides who can run the result at all. The Apple targets
therefore pin **14.0** — the presets for the engine, the vcpkg triplets for the
closure. Pinning only one side would be worse than pinning neither, because
linking newer-minimum dependency objects into an older-minimum binary produces
a build that warns and a binary claiming a floor it cannot honour. The pack
records the floor, so a readiness check on the consumer side has something to
compare against.

Linux states its floor differently: there is no compiler switch for it, only
the base system a pack was built on, whose glibc and standard-library versions
are what a consumer's machine must be no older than. The field is recorded as
the build host, and the intent for Linux packs is to build them on the oldest
base the project supports rather than on the newest available — a floor set by
the build environment, and stated rather than discovered.

## The compile contract

A consumer's own translation units must be compiled the same way the engine
archives were, or its objects disagree with the archive about struct layout and
inline behaviour. Two channels carry that.

**Definitions.** The set is not small and it is not static: `ORKIGE_STATIC`, the
render-flavor macro, `USE_RTSHADER_SYSTEM`,
`ORKIGE_ENGINE_HAS_GOCOMPONENT`, `ORKIGE_HTTP`, the scripting-backend define,
`ORKIGE_HAVE_VULKAN`, the standard-library hardening switch, and on Windows
`NOMINMAX`/`WIN32_LEAN_AND_MEAN`.

**Options.** A define is not the only thing that changes what the shared headers
mean. An exception model decides how every translation unit unwinds; sanitizer
instrumentation emits references only an equally instrumented compile and link
resolve; an object-section capacity flag is needed by the consumer for exactly
the reason the engine needed it, because it compiles the same fat headers. Link
options travel with the compile options, since instrumentation compiled in must
be linked in.

Both are **captured, never restated**. `cmake/OrkigePackage.cmake` reads them off
the engine itself — the root directory's `COMPILE_DEFINITIONS`,
`COMPILE_OPTIONS` and `LINK_OPTIONS` plus each archive's `INTERFACE_*`
equivalents, i.e. exactly what the engine declares PUBLIC — and the package
records them as `ORKIGE_{CORE,ENGINE}_COMPILE_DEFINITIONS`,
`ORKIGE_{CORE,ENGINE}_COMPILE_OPTIONS` and `ORKIGE_LINK_OPTIONS`. The imported
targets carry them, and `orkige_game_module()` applies them verbatim. A
hand-kept copy in a config and another in a link helper is a drift trap: the
engine grows a switch, the copies do not, and nothing says so until a
consumer's binary behaves oddly.

The engine's own build-tree-absolute paths are declared PRIVATE and are
therefore correctly absent — they are implementation, not contract, and a pack
must not carry them.

### The floor under the capture

Capture is only as complete as the engine's discipline of declaring an
ABI-relevant define PUBLIC. A define added PRIVATE that a HEADER then reads
would change what that header means for a consumer while escaping the contract
in silence — no link error, no diagnostic, just objects that disagree.

So the package records the private set as well
(`ORKIGE_PRIVATE_COMPILE_DEFINITIONS`), and the `sdk_pack` test asserts that no
installed header mentions any of those names. The day one does, the suite says
so, and the fix is to declare it PUBLIC. That is a check rather than a
convention, which is what the rest of this contract is built on.

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

The `sdk_pack` ctest (per flavor) proves it:

1. **install + relocate** — `cmake --install` into a scratch prefix, then
   RENAME the result. Every later leg reads only the renamed copy, so a path
   baked at install time fails in the suite rather than on a user's machine.
2. **self-contained** — no file in the pack's cmake surface, or in the
   closure's cmake/pkg-config files, may name the engine source tree, the
   engine build tree or the machine's vcpkg root.
3. **configuration** — the engine archives and the dependency closure must be
   the same configuration, with the other half absent entirely and its
   per-config target files pruned. And the pack must carry no host executable
   directories at all.
4. **target contract** — every field of the pack's own description must be
   present, and the ones a host pack can answer must be right: the platform it
   was built on, an executable module shape, a triplet, a `host` toolchain kind
   with NO toolchain file (handing a host consumer settings for a cross build
   would redirect a build that was correct), a compiler, a standard library,
   and on Apple a recorded OS floor.
5. **private definitions** — no installed header may read a definition the
   engine keeps PRIVATE.
6. **surface** — every engine header the source tree carries must be IN the
   pack at the same layer-rooted path, and a single translation unit that
   includes all of them must compile and link against it. Both lists come from
   the SOURCE tree, never from the pack, so the check cannot go circular.
7. **acceptance** — `projects/jumper-native` configures, builds and RUNS
   against the pack inside a clean room (a macOS sandbox profile) where the
   engine source tree and the engine build tree are DENIED. Denial is the
   point: a build tree at its usual absolute path would silently satisfy a
   configure that should have failed. Where the artifact landed is read from
   the manifest the build wrote, not guessed. And the frame the module renders
   is examined for several distinct colours: the module's own self-check is a
   gui widget-state assertion that a blank window passes, so it is the pixels
   that have to say a scene was there — the failure mode a wrong media
   directory produced.
8. **closure** — every library on the module's real link line must resolve
   INSIDE the pack. The clean room denies our own trees, but it cannot deny the
   platform and it does not deny a package manager's prefix, so a
   host-installed copy of a library the pack is meant to carry could satisfy
   the link and leave the pack looking self-sufficient here and broken on a
   user's machine — a link resolving to the wrong thing rather than to nothing.
   The exception list is the platform's OWN runtime, named explicitly: Apple
   frameworks and `/usr/lib` (the dyld shared cache — there is no static
   libSystem to bundle), the SDK roots, glibc and its siblings on Linux, and
   the Windows SDK import libraries. Package-manager prefixes are deliberately
   NOT on it, and a bare `-l<name>` outside a short list of platform runtime
   libraries fails too, since a bare name finds whatever the host has.
9. **compile contract** — every definition AND compile option the package
   records must appear on the module's actual compiler command line, read back
   from its `compile_commands.json`. The acceptance leg only notices a missing
   one when something happens to reference the symbol it changes; this notices
   the layout and inline-behaviour cases that would otherwise link and then
   misbehave.
10. **ABI guard** — an installed header edited in a throwaway copy of the pack
   makes the configure refuse with the ABI-mismatch diagnostic.

The pack is removed when all legs pass and kept when one fails, which is when
the several gigabytes of a Debug pack are worth the disk.

On platforms without a path sandbox the acceptance leg still runs from staged
copies outside the repository and leg 2 still audits the pack, but the engine
tree is not made unreachable there; macOS carries the strict form.

### A cross pack's acceptance

`sdk_pack` proves the HOST pack, where the machine can run what it builds.
A cross pack's equivalent question is answered on the target itself, by
`export_ios_simulator_native_run`: an iOS pack is installed off the simulator
tree and RELOCATED, `projects/jumper-native` is exported with that pack as the
export's ONLY engine source (no build tree, no player payload on the command
line), and the resulting app is installed on a booted simulator and watched
until its own console says it booted its bundled project and rendered its
frames. A module linked against the wrong architecture's archives, a bundle
without its platform entry point, or one missing its shader media is a package
that installs perfectly and never draws — which is why the verdict is the app's
own output rather than a file listing.

Self-containment is asserted STRUCTURALLY there rather than inferred from the
run: a simulator sees the host filesystem, so a build-machine path baked into a
binary still resolves on it. What the pack contains and what the link line
resolves to are the host pack's legs 2 and 8, and they cover the same files.

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

## How the editor uses one

A pack is what a DOWNLOADED editor hands a project whose game code is compiled
C++. The resolution is one seam — `core_project/NativeModule.h`, consumed by
compile-on-Play and by export alike, so both build against the same engine and
refuse for the same reasons:

1. **A build tree first.** An editor running out of an engine checkout resolves
   its own configured build tree, exactly as it always has. That order is
   deliberate: a developer keeps building against the very engine their editor
   runs on, whatever else is installed on the machine.
2. **An installed pack second**, at `<writable state>/sdk/<flavor>` — the
   platform's per-user application-support directory
   (`tools/editor/EditorResourcePaths.h`). A signed app bundle is read-only and
   self-writes invalidate its signature, so that is the only place a pack can
   live. Per flavor, because a pack is flavor-bound like the engine inside it.

A pack build gets its own module tree, `native/build-sdk-<flavor>` (and
`native/build-export-sdk-<flavor>` for an export), because a module tree is
bound to the ENGINE it was configured against — its cache holds that engine's
`ORKIGE_ROOT` and build type, and a configured tree is only ever rebuilt
incrementally.

### Two prerequisites, reported as two

A missing pack and a missing toolchain are different problems with different
fixes, so the editor never merges them into one message:

- **No SDK** — "…needs the Orkige SDK for this build, and it is not installed",
  naming the directory it belongs in. That is something to install through
  Orkige.
- **No build programs** — the machine has no `cmake` / `ninja` on its PATH.
  That is something to install on the machine: **we ship the engine, never a
  toolchain**, and the message says which program is missing and points at the
  platform's compiler (on macOS `xcode-select --install`).
- **The other flavor's pack** — its archives are the other render backend's, so
  it is refused for what it is rather than failed at link.

Both surfaces carry them: the Console line a person reads, and the same sentence
over MCP (the `play` refusal, and `get_state`'s build verdict, so a headless
agent reads what a person reads). A project whose behaviour is Lua scripts needs
none of it and is unaffected.

The `editor_bundle_native` ctest is the acceptance proof, per flavor: a COPIED
editor plus a pack installed into its own state directory builds, plays and
packages `projects/jumper-native` inside a clean room where the repository, the
engine build tree and the machine's vcpkg root are denied — with cmake and ninja
handed back as individual files, since a native build genuinely needs a
toolchain. It asserts all three outcomes, and that the two refusals differ.

## Scope

Desktop host packs are the ones built today. The contract above is written for
all seven targets — the field schema, the module shape, the artifact manifest
and the configure vocabulary are the parts a per-target pack must not have to
change, because they reach into project files this engine does not own. Until
those packs exist, mobile and browser game code goes through the export pipeline
(`Docs/web-export.md`, `Docs/ios-signing.md`) — a downloaded editor packages
compiled game code for the DESKTOP host, and says so for anything else.

Installing a pack is a manual step today: unpack it at
`<writable state>/sdk/<flavor>`. Fetching one from inside the editor is a
separate piece of work; nothing above depends on how the directory got there.
