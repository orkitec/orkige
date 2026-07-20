# Native game modules

A native module is the compiled C++ game code of a `.orkproj` project (the
manifest carries `native.target` / `native.cmakeDir` / `native.buildDir`;
`projects/jumper-native/` is the reference). It is a small standalone CMake
project that links the engine out of an engine BUILD TREE — the editor's
compile-on-Play builds it, and `Util/orkige_export.py` builds it for a
distributable app. The full build contract lives in
`cmake/OrkigeGameModule.cmake`; game code is flavor-neutral by construction (it
spells only facade types, never `Ogre::`), so one module builds against either
render flavor's engine tree.

## The engine as a find_package(Orkige) package

The engine is consumed as ONE `find_package(Orkige)` package that resolves
against the engine build tree with NO install step (the config is written
straight into the build dir, keeping the dev loop and CI fast). It exports TWO
imported targets that share ONE version stamp:

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
realized by `cmake/OrkigeGameModule.cmake` against the engine tree's own
`vcpkg_installed/` — the module builds without the vcpkg toolchain, so the
closure is resolved there rather than pinned into the imported-target
interfaces.

A module therefore does exactly:

```cmake
find_package(Orkige <abi-stamp> EXACT REQUIRED
             CONFIG PATHS <engine-build-dir> NO_DEFAULT_PATH)
target_link_libraries(my_game PRIVATE Orkige::Engine)
```

`orkige_game_module(<target>)` wraps this and adds the dependency closure.

## The ABI-stamp version guard

The package VERSION is not a marketing semver — it is an ABI stamp derived from
the engine SOURCES (`cmake/OrkigeAbiStamp.cmake`: the committed HEAD plus the
tracked working-tree diff, so it moves the instant a header changes, even
uncommitted). The engine records the stamp of the sources its archives were
built from — written at configure time and refreshed on every engine build so it
stays in lock-step with the libraries. A module computes the CURRENT source
stamp and requires the package match it EXACTLY.

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
The `module_abi_mismatch` ctest (per flavor) is the regression proof — it
configures the module against a version-mismatched package and asserts the
refusal.

Honest limits: a non-git source drop collapses to one constant stamp (no
source-change tracking); untracked new header files are not folded into the
stamp. The package resolves against the build tree only — a relocatable
installed SDK, and migrating the editor/player/tests (which build in the engine
graph itself and never drift, so they are deliberately left as-is) onto
`find_package(Orkige)`, are the next bricks of a fuller SDK.
