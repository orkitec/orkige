# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Orkige is a custom C++ game engine ("orkitec game engine", ~2009–2012) being revived and
modernized to build mobile games (iOS + Android) with desktop (macOS/Windows) as dev targets.
Original author: Steffen Römer. The modernization happens on the `modernize` branch; `master`
holds the historical state. Old game projects live in archived branches (`watermaze`,
`ThinkBlue`, `CigaretteGame`, tag `PuddingPanic-Appstore-version-1.1`).

Anything that seems missing (vendored `Dependencies/`, OgreLite forks, Ogitor, `engine_swf`,
`engine_video`, the CMake 2.6 build) was deliberately deleted during modernization and is
recoverable from git history — do not reintroduce it.

## Build

Requires: CMake ≥ 3.28, Ninja, vcpkg at `~/Development/vcpkg` (or set `VCPKG_ROOT`).

```sh
VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset macos-debug   # configure (runs vcpkg installs)
cmake --build --preset macos-debug                              # build
```

Presets: `macos-debug`, `macos-release`. Output in `build/<preset>/`.
Dependencies come exclusively from `vcpkg.json` (manifest mode) — never vendor libraries
into the tree and never rely on system-installed libraries.

Hermeticity: this machine hosts a second (Intel-layout) Homebrew at `/usr/local`, plus
loose orphaned headers from ~2016 (e.g. an ancient zlib.h), and clang searches
`/usr/local/include` by default. The presets force `CMAKE_OSX_SYSROOT` +
`CMAKE_IGNORE_PREFIX_PATH=/usr/local`, and `triplets/arm64-osx.cmake` (via
`VCPKG_OVERLAY_TRIPLETS`) does the same for vcpkg port builds. If a build ever reports
headers/symbols from `/usr/local`, that isolation has regressed — fix it, don't work
around it.

## Testing

```sh
ctest --preset unit    # headless Catch2 unit tests (~3s) — safe to run anytime
ctest --preset all     # + integration runs (open real windows on this machine)
```

Layout: `tests/core/` is the Catch2 unit suite (`orkige_core_tests`, label `unit`,
boots the app singleton set headlessly via `CoreTestEnvironment`). The integration
tests (label `integration`, registered in `tests/CMakeLists.txt`) reuse the
self-checking apps — hello_orkige demos, editor self-check/resize, player — which
verify themselves and exit non-zero on failure; that exit code is the contract.

The rule: every change ships with tests that verify it — unit tests for core
logic, a self-check hook wired into ctest for app/runtime behavior. `ctest` must
pass before committing.

## Modernization ground rules

- C++20, no boost. Old code being touched gets moved to std equivalents
  (`std::shared_ptr`, `<type_traits>`, range-for, `std::function`, `std::mutex`).
- Renderer target is OGRE 14.x from vcpkg (port from the historical OGRE 1.7 API).
  Window/input target is SDL3 (replaces the abandoned OIS).
- Scripting is Lua-first and currently DISABLED via the global `ORKIGE_NOSCRIPT` define:
  the old Lua meta backend (`core_base/Meta_Lua.h`) depends on the dead luabind library
  and the Python backend (`Meta_Python.h`, `core_python/`) on boost::python. Neither is
  compiled; the plan is to rebuild Lua bindings on sol2. Don't "fix" these files in passing.
- Everything builds statically during the revival (`ORKIGE_STATIC` is defined globally);
  the old `__declspec` DLL export macros in the prerequisites headers are inert.
- Keep the existing code style when editing old files: tabs, `m`-prefixed members,
  Doxygen-style comments, `#ifndef` include guards with date suffixes.
- Most legacy files use CRLF line endings — preserve them when editing (a flip to LF
  turns the whole file into one unreviewable diff). New files use LF.
- Commit messages: no `Co-Authored-By` trailers.
- Renderer containment (decided 2026-07: classic OGRE stays, Ogre-Next is the pre-planned
  migration target if mobile forces it): new code above `engine_graphic` must not take
  `Ogre::` types in public signatures — route through engine-owned wrappers (see how
  `engine_physic/PhysicsWorld` hides Jolt). Reduce existing `Ogre::` leakage
  opportunistically when touching a file. Don't add reliance on features Ogre-Next
  dropped (OGRE material scripts especially — keep materials simple/generated).

## Architecture

Two layers, each split into small modules with a flat `<module>/<File>.{h,cpp}` layout.
Include paths are rooted at the layer directory (e.g. `#include "core_util/String.h"`).

**`orkige_core/`** — platform- and renderer-independent. Builds today as the static lib
`orkige_core` (alias `Orkige::Core`). Key ideas that span multiple modules:

- **Meta/type system** (`core_base`): every engine class registers a `TypeInfo` via the
  `OTYPE_INFO*` macros in `Meta.h`; `TypeManager` is the registry. The `Meta_*.h` backends
  additionally expose registered types to a scripting language — selected by
  `ORKIGE_NOSCRIPT` / `ORKIGE_LUA` defines at build level.
- **`optr`** (`core_util/optr.h`): the engine-wide smart-pointer alias — a `#define` for
  `std::shared_ptr` (`woptr` = `weak_ptr`). Old code uses it pervasively; keep using it.
- **Events** (`core_event`): global pub/sub via `GlobalEventManager` singleton;
  handlers are FastDelegate-based (`EventListener.h`).
- **Game objects** (`core_game`): `GameObject` = id + component container built on the
  generic `core_util/ComponentHolder`/`AttributeHolder` templates (SFINAE-heavy).
- **Serialization** (`core_serialization`): `ISerializeable` + archive pattern;
  `XMLArchive` is the tinyxml2-backed implementation.
- **Memory/debug** (`core_debug`): custom `MemoryManager` with allocation tracking,
  `LogManager` configured from XML.
- Umbrella header: `core_module/OrkigePrerequisites.h` (forward decls, export macros).

**`orkige_engine/`** — the OGRE-facing layer (NOT yet building; Phase 1 of the revival
ports it, gated behind the `ORKIGE_BUILD_ENGINE` CMake option). Modules follow the same
pattern with `engine_module/EnginePrerequisites.h` as umbrella. `engine_graphic/Engine.h`
is the central engine object; `engine_gocomponent` bridges core game objects to rendered
scene (`TransformComponent` etc.); `engine_fastgui` is the homegrown UI system;
`engine_mygui`, `engine_sound` (OpenAL), `engine_input`, `engine_physic` wrap subsystems.
All of it currently targets the OGRE 1.7 API and old platforms — expect every file to
need porting work when it gets re-enabled.

**Tools** (`orkige_fontconverter`, `orkige_menuviewer`, `orkige_sceneoptimizer`): legacy
utility sources, not built (`ORKIGE_BUILD_TOOLS` option exists but nothing is wired yet).
`Util/` holds legacy asset-pipeline scripts/binaries from the 2012 era.

**Docs/** contains the historical generated API docs (`OrkigeAPI`, `LuaAPI`) — useful as
reference for how the Lua-facing API looked when rebuilding bindings.

## Roadmap context

Phased revival (decided 2026-07): Phase 0 core foundation (done — core builds), Phase 1
OGRE 14 + SDL3 port of `orkige_engine` with a minimal demo on desktop, Phase 2 subsystems
(sol2 Lua bindings, UI decision, Jolt physics), Phase 3 mobile (Android arm64 + iOS/Metal),
Phase 4 asset pipeline (glTF) and a custom in-engine editor (Ogitor is gone for good).
