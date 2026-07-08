# Orkige

**The orkitec game engine** — a C++20 game engine for mobile games (iOS + Android)
with desktop (macOS/Windows) as first-class development targets. Originally written
2009–2012 and shipped on the App Store (*Pudding Panic*), revived and fully
modernized in 2026.

![Editor](https://img.shields.io/badge/editor-ImGui%20docked-blue)
![Renderer](https://img.shields.io/badge/renderer-OGRE%2014%20·%20GL3%2B%20·%20Vulkan%2FMoltenVK%20·%20GLES2-green)
![Physics](https://img.shields.io/badge/physics-Jolt%205-orange)
![Scripting](https://img.shields.io/badge/scripting-Lua%20(sol2)-purple)

## What's in the box

- **Engine** — OGRE 14.5 rendering (GL3+ on desktop, Vulkan-via-MoltenVK on macOS,
  GLES2 on iOS/Android), SDL3 windowing/input, Jolt Physics (with a planar "2D mode"),
  OpenAL Soft audio, Lua scripting on sol2, glTF asset loading (assimp), a
  GameObject/component model with XML scene serialization (`.oscene`), and the
  homegrown *fastgui* runtime UI with auto-generated texture atlases.
- **Editor** — Unity-style tool built on the engine itself: docked panels
  (Hierarchy, Inspector, Console, Scene viewport rendered to texture), transform
  gizmos with the classic Q/W/E/R shortcuts, undo/redo, multi-select, native macOS
  menu bar and file dialogs, mesh import via dialog or drag-&-drop, fly/orbit/pan
  camera navigation.
- **Play mode, the Godot way** — Play spawns the standalone player as a separate
  process and connects a debug protocol over TCP: live remote hierarchy and
  inspector, pause/step/stop, property editing into the running game. A crashing
  game can never take the editor down. The same protocol reaches **iOS simulators
  and Android devices** — press Play, pick a target, debug the game running there.
- **Projects** — a game is a folder with a `project.orkproj` manifest, scenes,
  assets and scripts; the editor opens projects and the player runs them.
- **Samples** — `hello_orkige` (feature demo) and `jumper`, a small textured
  jump-and-run with physics, gameplay and a fastgui HUD.

## Building

Requires CMake ≥ 3.28, Ninja, and [vcpkg](https://github.com/microsoft/vcpkg) at
`~/Development/vcpkg` (or set `VCPKG_ROOT`). All dependencies come from the vcpkg
manifest — nothing is vendored, nothing system-installed is used (exception:
MoltenVK acts as the Vulkan driver on macOS, `brew install molten-vk`).

```sh
cmake --preset macos-debug -DORKIGE_BUILD_ENGINE=ON   # configure (first run builds deps)
cmake --build --preset macos-debug                    # build
ctest --preset desktop                                # verify (~30s)

./build/macos-debug/tools/editor/orkige_editor       # the editor (use macos-release for speed)
./build/macos-debug/samples/jumper/jumper            # play the jumper
```

Presets: `macos-debug`, `macos-release` (use release when *working in* the editor —
it's ~19× faster), `ios-simulator-debug`, `android-debug`. Mobile deploy flows are
documented in CLAUDE.md.

## Testing

Everything is verified by a self-checking test suite (Catch2 units + app-level
integration runs that assert real behavior — rendered triangle counts, physics
positions measured over the debug protocol, screenshot dumps):

```sh
ctest --preset unit      # headless unit tests (~3s)
ctest --preset desktop   # + desktop integration (~30s)
ctest --preset all       # + simulator/emulator device tests
```

## Repository layout

```
orkige_core/      platform-independent core: meta/type system, events, game objects,
                  serialization, Lua scripting, debug-net protocol, projects
orkige_engine/    the OGRE-facing layer: graphics, input, sound, physics, fastgui
tools/editor/     the Orkige editor        tools/player/   the standalone runtime
samples/          hello_orkige, jumper     projects/       .orkproj game projects
tests/            unit + integration       Util/           asset generator scripts
ports/ triplets/  vcpkg overlays           Docs/           ports rationale, upstream PRs
```

`master` holds the historical 2012 state; modern work lives on `modernize`.
The old games survive in branches (`watermaze`, `ThinkBlue`, `CigaretteGame`,
tag `PuddingPanic-Appstore-version-1.1`).

## Upstream

Fixes discovered during the revival were contributed back to OGRE:
[#3667](https://github.com/OGRECave/ogre/pull/3667),
[#3668](https://github.com/OGRECave/ogre/pull/3668),
[#3669](https://github.com/OGRECave/ogre/pull/3669) (Vulkan-on-Apple via
`VK_EXT_metal_surface`).

---
© 2009–2026 orkitec / Steffen Römer
