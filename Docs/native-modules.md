# Native game modules

A native module is the compiled C++ game code of a `.orkproj` project (the
manifest carries `native.target` / `native.cmakeDir` / `native.buildDir`;
`projects/jumper-native/` is the reference). It is a small standalone CMake
project that links the engine out of an engine BUILD TREE — the editor's
compile-on-Play builds it, and the exporter (`tools/exporter`) builds it for a
distributable app. The full build contract lives in
`cmake/OrkigeGameModule.cmake`; game code is flavor-neutral by construction (it
spells only facade types, never `Ogre::`), so one module builds against either
render flavor's engine tree.

The same module builds against an installed SDK pack instead — one relocatable
directory carrying headers, archives, the dependency closure and the cmake
surface, for a machine with no engine checkout. That form, and the one helper
serving both, is `Docs/sdk-pack.md`.

## The engine as a find_package(Orkige) package

The engine is consumed as ONE `find_package(Orkige)` package. In the developer
loop it resolves against the engine build tree with NO install step (the config
is written straight into the build dir, keeping the dev loop and CI fast); an
installed SDK pack is the same package with every path spelled against the pack
root (`Docs/sdk-pack.md`). Either way it exports TWO imported targets that share
ONE version stamp:

- `Orkige::Core` — the platform- and renderer-independent core (`orkige_core`),
  its own OGRE-free surface. A core-only consumer links just this and gets a
  link error the moment it reaches an engine symbol, so the layering boundary is
  enforced at the target level.
- `Orkige::Engine` — the OGRE-facing engine (`orkige_engine`); it pulls
  `Orkige::Core` transitively, so a game module links only `Orkige::Engine`.

Both targets carry the engine include roots and the ABI compile definitions
(`ORKIGE_STATIC`, the render-flavor macro `ORKIGE_RENDER_NEXT` /
`ORKIGE_RENDER_CLASSIC`, the scripting-backend define), so a consumer inherits
the matching ABI straight from the package. The vcpkg dependency closure the
archives link (SDL3, OpenAL, Jolt, tinyxml2, NanoSVG, the flavor's OGRE +
codecs, Lua/sol2) is DECLARED by the package (`ORKIGE_TRANSITIVE_PACKAGES`) and
realized by `cmake/OrkigeGameModule.cmake` against the prefix the package points
at — the engine tree's own `vcpkg_installed/<triplet>`, or a pack's bundled
`vcpkg/`. The module builds without the vcpkg toolchain, so the closure is
resolved there rather than pinned into the imported-target interfaces; either
way those are the exact binaries the engine archives were linked against.

A module therefore does exactly:

```cmake
find_package(Orkige <abi-stamp> EXACT REQUIRED
             CONFIG PATHS <package-dir> NO_DEFAULT_PATH)
target_link_libraries(my_game PRIVATE Orkige::Engine)
```

`orkige_game_module(<target>)` wraps this and adds the dependency closure, and
`orkige_add_game_module(<target> <sources...>)` is the entry point a project
uses: it creates the target in the shape the target platform requires — a
desktop executable, the shared library an Android activity loads, an Apple
mobile bundle — and then wires it. The shape belongs to the platform, so a
project file never spells `add_executable()` and never has to be edited to
follow the engine onto another target. Where the artifact landed is written
down beside the build (`orkige_module_artifact.txt`) and read by the editor and
the exporter, because `<buildDir>/<target>` is only the desktop answer. See
`Docs/sdk-pack.md` for the full vocabulary.

## The ABI-stamp version guard

The package VERSION is not a marketing semver — it is a **content fingerprint**
of the engine's SOURCE SURFACE (`cmake/OrkigeAbiStamp.cmake`), git-INDEPENDENT:
it hashes the actual on-disk bytes (and relative paths) of every engine source
file. The surface is SCOPED to exactly what defines the engine ABI: the compiled
source of the two engine layers `orkige_core/` and `orkige_engine/` (a recursive
glob of `.h`/`.hpp`/`.inc`/`.cpp`/`.mm`/… — NOT `orkige_engine/media/`, which is
runtime assets, not object layout), plus the cmake files that define how a module
compiles and links against them (each engine layer's `CMakeLists.txt` and the
package/link helpers `OrkigeGameModule.cmake`, `OrkigeConfig.cmake.in`,
`OrkigePackage.cmake`, `OrkigeSdk.cmake`, `OrkigeSdkPack.cmake.in`,
`OrkigeTargetShape.cmake`, `OrkigeAbiStamp.cmake`, `OrkigeWriteVersion.cmake`).

Because it reads files rather than VCS state, it covers EVERY case uniformly —
a committed change, an uncommitted edit, a brand-new UNTRACKED header the module
includes, or a plain tarball drop with no git at all — all move the stamp. A
change ANYWHERE ELSE — a game script, an asset, a doc, a test, anything under
`projects/`, `samples/`, `tests/`, `Docs/`, `Util/`, `.github/` — does NOT touch
it. That keeps the guard silent through the ordinary edit-your-game-and-replay
loop (which would otherwise fire on every edit and train you to ignore it) while
still catching a genuinely stale engine library.

The engine records the stamp of the sources its archives were built from —
written at configure time and refreshed on every engine build so it stays in
lock-step with the libraries. A module computes the CURRENT source-surface stamp
and requires the package match it EXACTLY.

This closes a crash class: a module compiled against the current engine headers
but linking a STALE `liborkige_engine.a` (e.g. a struct grew a member in
`AppHost.h` but the library was never rebuilt) gets a garbage object layout and
crashes at runtime (the shipped `JumperNative` `setWindowBackgroundColour →
RenderTarget::getViewport` null-deref). With the guard, the mismatch is a HARD
CONFIGURE ERROR instead:

```
Orkige engine ABI mismatch: the package at '<engine-build-dir>' is version
2.0.<A>.<B> (ABI stamp <hash>), but this module's engine headers expect version
2.0.<C>.<D>. The engine library is stale relative to the sources at
'<ORKIGE_ROOT>' - rebuild the engine tree (cmake --build ...) so its archives
match the current headers, then reconfigure this module.
```

Both the editor's compile-on-Play and the exporter flow through this same path,
so a stale engine tree refuses at configure rather than shipping a crashing app.
The `module_abi_mismatch` ctest (per flavor) is the regression proof — it asserts
the guard fires on a mismatch AND that the fingerprint is correctly scoped: an
engine-source edit moves it, a brand-new untracked engine file moves it, and a
non-engine edit (a game file, a doc) does NOT trip the guard.

An installed SDK pack runs the same guard over the surface it actually carries —
the installed headers plus the cmake files defining the compile and link — so a
tampered or half-unpacked pack refuses the same way (`Docs/sdk-pack.md`).

Honest limits: the fingerprint tracks the engine SOURCE surface, not the exact
compiler/flags/vcpkg toolchain (a different toolchain producing a different ABI
from identical sources is out of scope — the flavor + ABI defines the package
already carries cover the intended axes). The editor/player/tests build in the
engine graph itself and never drift, so they are deliberately not migrated onto
`find_package(Orkige)`.
