# Editor distribution

The editor is meant to be copied. Someone takes the app to a machine that has
no clone of this repository, no engine build tree and no Python, double-clicks
it, and it renders, opens a project and plays. This document describes what the
app carries, how it finds those resources at runtime, and where it writes.

## What the app carries

The build stages the runtime payload into the app on every build, under the same
layout a project export writes — one convention for the editor, the player and
every exported game.

| Payload | Why the app needs it |
|---------|----------------------|
| `Media/Hlms` (+ `Media/Atmosphere` when the port ships it) — Ogre-Next flavor | The shader templates every material compiles from. The editor's own ImGui interface draws through the same path, so without these there is no window worth showing. |
| `Media/Main`, `Media/RTShaderLib` — classic flavor | The shader library the classic backend generates from; the engine's own metal-rough library is merged into the staged `RTShaderLib`, the one location the runtime registers. |
| `Media/fonts` | The engine-default font, so a project `.ogui` can name it. |
| `Media/water`, `Media/decals` | The shared water plane + normal map and the default decal textures, so a scene's water and decals show their editor preview. |
| `Media/bloom/<flavor>`, `Media/grade/<flavor>` | The compositor media a play session's `engine:setBloom` / `engine:setGrade` compiles from. |
| The player executable | Play spawns it. Without it in the app, Play has nothing to run. |
| The texture-cook tool | The export cook's encoder, beside the player where the tools belong. |
| The UI fonts (icon font, mono symbols) + their licenses | The asset browser's kind icons and the terminal's block/braille glyphs. |
| `Util/` — the engine's Python tools | The project exporter (`orkige_export.py` and the helpers it imports: `cook_textures.py`, `macos_self_contain.py`, `orkige_icons.py`, `orkige_png.py`) plus the animation cook `cook_vector_anim.py` and the neutral default app icon under `Util/media/`. They keep the source tree's directory name and sibling layout, because that is how they import each other. See [Exporting a game from a copied editor](#exporting-a-game-from-a-copied-editor). |
| The non-system dylib closure (macOS `Contents/Frameworks`) | The classic flavor links the Vulkan loader as a dylib, so a copy dies in dyld before `main` without it. |
| `CHANGELOG.md`, `VERSION`, `KNOWN-LIMITATIONS.md` (packaged builds only) | What the release shipped with. The Help > About box shows the changelog, read once from the resource root through this same locator. A build from a source tree carries none, resolves `Missing`, and the box says so in one line — the repository's working history is not what a binary shipped with. `Util/orkige_nightly_package.py` writes these; nothing in the build tree does. |

Mobile and web players are deliberately **not** staged: they are separate build
trees, and a copied editor cannot deploy to a device anyway.

## Layout per platform

`SDL_GetBasePath()` is the one probe both roots derive from.

- **macOS** — `Orkige.app/Contents/Resources/Media/…` for content,
  `Orkige.app/Contents/MacOS/` for the sibling executables. Nested executables
  belong beside the main one so a single signature covers the bundle, and
  `SDL_GetBasePath()` resolves to `Contents/Resources`.
- **Linux / Windows** — `<executable dir>/share/orkige/Media/…` for content and
  `<executable dir>/` for the sibling executables, where `SDL_GetBasePath()` is
  the executable's own directory. The build stages this layout beside the built
  executable, so a distributable archive is that directory.

## Linked libraries

Most of the closure is static, but not all of it: the classic flavor links
vcpkg's Vulkan loader as a dylib. On macOS the build therefore copies the
non-system dylib closure into `Contents/Frameworks`, points the editor and the
staged executables at it (`@executable_path/../Frameworks`) and **removes every
build-tree rpath**, so a missing library fails on the build machine rather than
on a user's. `Util/macos_self_contain.py` is that operation — the same one a
project export runs, so an app the build stages and an app the exporter writes
are self-contained the same way.

MoltenVK is the deliberate exception, and it is not an app concern: it is the
platform's Vulkan *driver*, system-tier like a GPU driver anywhere else, found
through its ICD manifest under the machine's Homebrew prefix. A machine without
it has no Vulkan; the classic flavor's default render system is unaffected.

## How resolution works

`tools/editor/EditorResourcePaths.h` is the ONE seam. Every consumer — the
engine media registration, the fonts, the compositor media, the player Play
spawns — asks it, and it answers **bundle first, developer tree second**:

1. the path inside the app, when it exists;
2. otherwise the developer-tree path CMake baked in, when THAT exists;
3. otherwise a `Missing` answer with an empty path, which each consumer degrades
   honestly on (a skipped resource location, or a refusal naming what is
   absent).

Two details make the answer trustworthy. The engine-media probe looks for the
flavor's own **marker** subdirectory (`Hlms` on Ogre-Next, `Main` on classic), so
a half-staged `Media/` never shadows a working developer tree. And the whole
decision table runs over an injectable existence predicate, which is what makes
it headless-unit-testable (`EditorResourcePathsTests`).

The editor logs one line at boot naming the root it chose:

```
orkige_editor: resources: bundled app (/Applications/Orkige.app/Contents/Resources/), engine media '…/Media', player '…/MacOS/orkige_player' (bundled)
```

The baked developer-tree paths are bound in exactly one place —
`tools/editor/EditorResourceBinding.cpp` — so nothing else in the editor reads
an absolute path from the machine that built it.

### Proving self-sufficiency: `ORKIGE_EDITOR_BUNDLE_ONLY`

Set `ORKIGE_EDITOR_BUNDLE_ONLY=1` and the editor refuses **every**
developer-tree fallback: only what the app carries can answer. That is what a
packaged build effectively is, and it turns "is this app complete?" into a
question with a visible answer — anything the staging missed fails loudly
instead of silently borrowing from the build machine.

## Exporting a game from a copied editor

Packaging a game needs three things from the engine: a player binary, the engine
media and the texture encoder. A copied editor has all three — they are what it
renders and plays with — so an export from one packages *the app itself*.

`tools/editor/EditorExportPlan.h` is the one decision. It reads the resolved
paths (through the locator above, never a baked constant) and answers with an
exporter command line or one sentence:

- **Built from the source tree** — the tree's `Util/orkige_export.py` packages a
  preset build tree: this editor's own for the desktop app, the platform's
  preset tree for a device or browser target
  (`--engine-build <tree>`).
- **A copied app** — the staged `Util/orkige_export.py` packages the app's own
  payload: `--engine-bundle <resource root> --engine-tools <tool root>`, the two
  roots the locator answered with. The exporter reads the render flavor out of
  the staged media (the shader tree names it), copies that `Media/` tree across
  as one piece (it is already the export layout), takes the player and the
  encoder from the tool root and resolves the dylib closure against the app's
  own `Contents/Frameworks`. Everything after the sourcing is the same code, so
  both shapes produce the same bundle.

Both the Build menu and the MCP `export_project` verb go through the plan, so
the command line and the refusals have one definition.

A copy refuses, specifically, what it genuinely cannot do:

| Request | What it says |
|---------|--------------|
| iOS, Android or browser | Packaging for that platform needs that platform's player, which only an Orkige built from the engine source tree produces; export the desktop app, or build from source. |
| A project with a native module | Compiled C++ game code needs the engine source tree and a C++ toolchain, which a downloaded app carries neither of. Projects whose behaviour is Lua scripts export as they are. |
| Anything, on a host with no packaging target | The exporter writes a macOS app today; on another host it says so instead of producing nothing. |
| Anything, with the tools missing | The app carries no exporter and no source tree is reachable — reinstall. |

**Python is required for export** (and for importing an animation document):
the exporter is a python3 script, and porting it is a separate job. The editor
preflights the interpreter before spawning, so a missing or too-old python3 is
one honest sentence naming what was probed, what was found and what is needed —
`ORKIGE_PYTHON` points at a specific interpreter. Everything else a copied
editor does, including importing an `.svg`, needs no interpreter at all.

## Where the editor writes

A distributed app bundle is read-only in spirit (and on macOS a self-write
invalidates its signature), and a Finder-launched app has a working directory of
`/`. So nothing the editor persists lives next to the executable or relative to
the cwd. The settings inis (panel layout, recents, camera feel, snap steps) and
the engine log go to the per-user application-support directory:

- macOS: `~/Library/Application Support/Orkige/`

An ini an earlier build left inside the app is **moved** there once, so an
in-place upgrade keeps the layout and recents.

`ORKIGE_EDITOR_STATE_DIR` redirects the whole writable set. Scripted runs need
it: the application-support directory follows the user account rather than
`HOME`, so a test cannot isolate itself by moving `HOME` and would otherwise
scribble into the real user's editor state. (This is the same test-isolation
seam `ORKIGE_BREADCRUMB_DIR` is for the crash trail.)

Automated runs keep their historical cwd-relative log name and persist no
settings at all, so one test never inherits or rewrites another's — or the
user's — state.

## The test

`editor_bundle` (per flavor, `tests/integration_driver/run_editor_bundle_test.py`)
is the standing gate on all of the above. It stages the built app plus a copy of
a project into the build tree, detaches the copy from the tree, and drives it
over its own MCP endpoint: it must boot with a rendering window, open the copied
project, render a scene screenshot and PLAY, with the player inside the app.

Detachment uses two mechanisms so the test means something on every platform:
`ORKIGE_EDITOR_BUNDLE_ONLY=1` everywhere, and on macOS additionally a sandbox
that denies reads of the repository, the vcpkg tree and Homebrew — which also
catches a resource reached through a baked path *outside* the resolver. The
environment is scrubbed: a `PATH` of `/usr/bin:/bin` (no Python), a scratch
`HOME`, a scratch writable state directory, and a working directory outside the
tree.

A second leg asks the copy to **package** the project it has open, and accepts
exactly two answers: a `.app` exported out of the payload the copy carries —
which the leg then runs, from a neutral working directory inside the same clean
room — or a refusal naming what is missing. A missing-file error naming a
directory from the machine that built the binary fails the leg, because that is
what a baked exporter path produces on a user's machine. The same leg asks for
an iOS package and asserts the refusal says *why*. The interpreter is handed to
the copy explicitly (by its real path, which is not in the denied tool
directories): python is a machine tool for this job, like the graphics driver
the windowed legs use, and the clean room denies the repository, not the
machine. Where no interpreter meets the floor, the leg asserts the editor's own
preflight message instead.

A third leg makes the staged shader media **unreadable** and asserts the render
backend logs its honest warning and keeps going, rather than aborting the boot:
a missing directory and an unreadable one must reach the same graceful branch.
That leg skips when run as root, where a mode of `000` denies nothing.

A fourth leg reads back the changelog the About box shows, in both of its states.
`orkige_editor --changelog` prints exactly that text and exits before any window
— display-free like `--version` — so a staged copy with no `CHANGELOG.md` at its
resource root must say it carries none, and the same copy with the file the
packaging pipeline writes there must show *that*.

## Not yet portable

- **Writable state on Linux and Windows** still resolves to the historical
  location: `PlatformUtil::getSupportDirectory` has no XDG (`$XDG_DATA_HOME`,
  `~/.local/share/<app>`) or `%APPDATA%` implementation yet, and its Linux
  branch answers with the working directory. The editor routes through the one
  helper already, so implementing those two platform branches is all that is
  left.
- **A signed, notarized macOS app** and the archive/installer shapes for Linux
  and Windows are packaging concerns beyond the app's own self-sufficiency.
- **Project export needs a python3 on the machine** (>= 3.10). The exporter and
  its helpers travel with the app, but they are Python; a C++ exporter is a
  separate, much larger job. The preflight makes the requirement visible rather
  than letting a spawn fail opaquely.
- **Export on Linux and Windows** has no packaging target yet — the exporter
  writes a macOS app, an iOS bundle, an Android package or a browser build, and
  a copy on another desktop says so plainly.
