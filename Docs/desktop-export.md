# Desktop export

A desktop package is the game as a person receives it: the runtime, the engine
media and the project payload arranged so the whole thing boots by being
double-clicked, with no arguments and nothing installed beside it.

Two shapes, one for each desktop system Orkige packages for:

| Platform | Artifact | Where it lands |
|----------|----------|----------------|
| `macos` | `<Name>.app`, a bundle | `<project>/builds/macos/` |
| `linux` | `<Exe>/`, a portable directory | `<project>/builds/linux/` |

Both come out of the same export: the same payload staging, the same
export-time [texture cook](textures.md), the same baked texture samplers, the
same `THIRD-PARTY-NOTICES.md`, and the same default-project marker. Only the
enclosing shape differs, because the two systems disagree about what an
application is.

## The desktop platform is the host's own

A desktop package is assembled **around a player binary**, and nothing in the
export pipeline cross-compiles one. So `macos` packages on a Mac and `linux` on
Linux, and each refuses the other by name rather than writing a directory that
cannot run:

```
$ orkige_export --project ~/games/roller --platform linux --engine-build build/macos-debug
orkige_export: ERROR: a Linux package is assembled around the Linux player, and
this exporter runs on macOS - nothing here cross-compiles a player for another
operating system. Export for macOS, or run the export on a Linux machine
```

The editor says the same thing in its own words, and its **Build** menu offers
only the host's desktop item — an entry that could never succeed is worse than
no entry. Over MCP, `export_project` takes `linux` like any other platform and
answers with the refusal where it does not apply.

Everything else about an export is unchanged by this: the mobile and browser
targets ship *another* platform's player, fetched or built, and are governed by
[device-payloads.md](device-payloads.md) instead.

## The Linux package

```
JumperLua/
    JumperLua               the game (the player binary, renamed)
    Media/                  the render flavor's engine media
    project/                manifest, scenes/, assets/, scripts/, data/
    orkige_project.txt      the default-project marker
    THIRD-PARTY-NOTICES.md  the linked libraries' license texts
```

A portable directory is the Linux distribution norm for a game: it is copied or
archived whole, it needs no installer, and it runs from wherever it is unpacked.

```sh
cd JumperLua
./JumperLua
```

The directory and the binary carry the **same** name, and it is the executable
name (the project name reduced to its alphanumerics) rather than the display
name — a path somebody types should not need quoting.

That the game boots with no arguments is the marker's doing, and it is the same
mechanism a macOS bundle uses: the runtime reads `orkige_project.txt` and the
`Media/` tree relative to `SDL_GetBasePath()`, which on Linux is the directory
the executable lives in. Nothing is baked into the binary, so moving the
directory changes nothing.

### No bundled libraries

A macOS bundle carries a `Contents/Frameworks` full of dylibs. The Linux package
carries none, and that is a property of the build rather than an omission: the
whole dependency closure is linked **statically** (`VCPKG_LIBRARY_LINKAGE
static` in `triplets/x64-linux.cmake`), so the binary the export copies is
already whole.

What stays dynamic is the machine's own: the C and C++ runtimes, and the
display, driver and audio libraries SDL and the render backends resolve through
the platform (X11 or Wayland, the GL or Vulkan driver, ALSA or PulseAudio). Those
belong to the system the game runs on and are never something an application
brings with it. The **Vulkan loader** (`libvulkan.so.1`) is one of them: it is
system-tier on Linux the way the Vulkan driver itself is - the distribution's
`libvulkan1` package provides it, every Vulkan game expects it, and the export
neither bundles nor rewrites it. The default flavor's package therefore needs a
machine with Vulkan installed; the classic flavor's needs GL, the way the
desktop has always worked.

The consequence worth knowing before shipping: **the glibc and libstdc++ of the
machine that built the game are its floor.** A package built on a recent
distribution will refuse to start on an older one, with a loader error naming
the version it wanted. Building the shipping package on the oldest distribution
you intend to support is the whole answer.

### What v1 does not do

Stated plainly, because each is a thing somebody will look for:

- **No desktop entry and no icon.** The package writes no `.desktop` file and no
  icon; where a menu entry and an icon come from is a question a Linux install
  answers, and the portable directory deliberately does not install anything.
  `export.icon` is read by the macOS, iOS and Android packages today.
- **No archive.** The artifact is a directory. Compressing it for distribution
  is one `tar` away and is not the exporter's business yet.
- **No compiled game code.** A project with a `native.target`
  ([native-modules.md](native-modules.md)) is refused by name: the module build
  the exporter drives is written against an Apple toolchain today.
- **No signing.** Linux has no code-signing tier to speak of, so unlike
  [iOS](ios-signing.md) and [Android](store-release.md) there is nothing to
  configure and nothing that can be half-signed.

## Test builds

`--with-tests` works on both desktop platforms: the project's own Lua suite
rides in the payload and the package runs it instead of the game, exiting with
the suite's verdict ([testing.md](testing.md)). The reason it works here and not
for Android or the browser is discovery — the runner finds a suite by walking a
directory, and a desktop package's payload is loose files either way.

## Verified by

- `export_linux` (Linux only) exports `projects/jumper-lua`, asserts the
  structure — the executable bit, the marker, the payload, the notices, the
  absence of `.orkmeta` sidecars and of the `tests/` tree — asserts that the
  package bundles **no** shared library and that no dependency resolves back
  into the build tree or vcpkg, and then **runs** the package from a neutral
  working directory with a scrubbed environment. A package that quietly depends
  on the machine it was built on fails there rather than at a player's.
- `export_linux_tests` (Linux only) packages the same project as a **test
  build** and runs the suite inside the package, so the payload's cooked
  textures, baked samplers and staged media are what the tests see.
- `export_macos_lua` / `export_macos_native` / `export_macos_tests` are the
  macOS siblings.
- The platform vocabulary, the artifact naming and every refusal sentence are
  pure functions asserted in `orkige_exporter_tests` and
  `orkige_editor_core_tests` — including that the editor's idea of this host's
  desktop platform and the exporter's are the same one.
