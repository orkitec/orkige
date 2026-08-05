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

Three doors, one implementation: `orkige_export` inside this repository,
**Build ▸ Export** in the editor, and `orkige_editor export` on a machine that
carries only an installed Orkige ([editor-cli.md](editor-cli.md)).

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

## The macOS bundle

```
<Name>.app/Contents/
    MacOS/<Exe>            the player or the project's own module binary
    Frameworks/            the executable's non-system dylib closure, with
                           rpaths rewritten to @executable_path/../Frameworks
    Resources/
        Media/             the render flavor's engine media
        project/           manifest, scenes/, assets/, scripts/, data/
        orkige_project.txt the marker the runtime reads at boot
        AppIcon.icns       from export.icon, or the neutral engine icon
        THIRD-PARTY-NOTICES.md
    Info.plist             CFBundleIdentifier from export.macos.bundleId
```

The bundle stands alone: nothing in it resolves against a build tree, which is
what the `export_macos_lua` test proves by running the exported app from a
neutral working directory.

## Signing a macOS package: three states, and nothing in between

macOS treats a downloaded app far more strictly than a locally built one, so a
package meant for **other people's Macs** needs more than the bundle above.

| State | What it is | What it needs |
|---|---|---|
| **ad-hoc** (default) | the bundle is internally consistent and names no developer | nothing |
| **Developer ID** (`--sign`) | signed with your certificate, hardened runtime, secure timestamp | a Developer ID Application identity |
| **notarized** (`--notarize`) | the same, submitted to Apple and stapled | that identity plus notarization credentials |

An export that asks for nothing produces the ad-hoc package, byte for byte, and
consults no credential. Signing is opt-in and always explicit.

An ad-hoc app runs on the machine that built it. Copied or downloaded, macOS
refuses it — a person can still open it through the Finder's context menu, but
that is a workaround, not a distribution. A **Developer ID** signature names
you; a **notarized** one carries Apple's own verdict, and the stapled ticket
means the Mac opening it needs no network to see that verdict.

Nothing in between ships: a signed export whose certificate is missing, or a
notarized one whose credentials are half configured, **refuses and packages
nothing**. A half-signed artifact is worse than an honestly ad-hoc one.

### Signing from the command line

```sh
# Developer ID, no notarization
orkige_export --project projects/mygame --platform macos \
    --engine-build build/macos-release \
    --sign --macos-identity "Developer ID Application: You (TEAM123456)"

# ...and Apple's verdict, stapled into the app
export ORKIGE_NOTARY_KEY=~/keys/AuthKey_ABCDE12345.p8
export ORKIGE_NOTARY_KEY_ID=ABCDE12345
export ORKIGE_NOTARY_ISSUER_ID=69a6de70-....
orkige_export --project projects/mygame --platform macos \
    --engine-build build/macos-release --notarize
```

`--notarize` implies `--sign`: there is nothing to submit without a signature.
`orkige_editor export` takes the identical flags.

### Credentials

The identity is a certificate's public name and may travel on a command line.
Everything else comes from the environment — or, in the editor, from **Build ▸
Project Settings ▸ Signing**, macOS / Distribution, which keeps the names in a
per-project file outside every project tree and the password in this machine's
credential store ([store-release.md](store-release.md) has the full model).

| What | Flag | Environment | Settings key |
|---|---|---|---|
| Developer ID Application identity | `--macos-identity` | `ORKIGE_MACOS_SIGNING_IDENTITY` | `macos.distribution.identity` |
| a non-default keychain to search | — | `ORKIGE_MACOS_KEYCHAIN` | — |
| App Store Connect key file (`.p8`) | `--notary-key` | `ORKIGE_NOTARY_KEY` | `macos.distribution.notaryKey` |
| ...its key id | `--notary-key-id` | `ORKIGE_NOTARY_KEY_ID` | `macos.distribution.notaryKeyId` |
| ...its issuer id | `--notary-issuer` | `ORKIGE_NOTARY_ISSUER_ID` | `macos.distribution.notaryIssuer` |
| Apple ID (the alternative route) | `--notary-apple-id` | `ORKIGE_NOTARY_APPLE_ID` | `macos.distribution.notaryAppleId` |
| ...its team id | `--notary-team-id` | `ORKIGE_NOTARY_TEAM_ID` | `macos.distribution.notaryTeamId` |
| ...its app-specific password | **none** | `ORKIGE_NOTARY_APP_PASSWORD` | **none** (the credential store) |

The precedence is one rule everywhere: **an explicit value wins, then the
environment.** The app-specific password is the one credential with no flag and
no settings key at all — it is a secret, so it lives in the OS credential store
and reaches the signing step through the environment, never through a file and
never on a command line anyone can name.

Apple takes **either** route. The API key wins when both are complete: it is
revocable on its own, without touching an Apple ID. A route that is *half*
configured is never silently ignored — the refusal names each value that is not
set, by variable name and never by value.

### What a signed export runs

Nested code first, because a bundle signature seals what it contains and a
later nested signature would invalidate it:

```
codesign --force --sign <identity> --timestamp --options runtime  Frameworks/*
codesign --force --sign <identity> --timestamp --options runtime  <Name>.app
codesign --verify --strict --verbose=2                            <Name>.app
                                          # --notarize continues:
ditto -c -k --sequesterRsrc --keepParent  <Name>.app  <Name>-notarize.zip
xcrun notarytool submit <zip> <credentials> --wait --timeout 30m \
      --output-format json
xcrun stapler staple    <Name>.app
xcrun stapler validate  <Name>.app
spctl --assess --type exec --verbose=2 <Name>.app
```

The whole sequence is decided up front by a pure planner
(`tools/exporter/ExportMacosSign.h`) and each command is spawned directly as an
argv — no shell, nothing handed to a command interpreter. `ditto` rather than a
zip writer: the bundle's symlinks and executable bits have to survive the trip
or Apple assesses something that is not the app.

**The verdict is read from Apple's JSON payload, never inferred from an exit
code** — a submission that came back `Invalid` exits 0. Anything but `Accepted`
fails the export, and the notarization **log** is fetched and printed first,
because it is the only thing that names the binary Apple objected to. The
ticket is stapled only after an acceptance.

**No entitlements.** The hardened runtime's default restrictions are all things
a game the engine runs does not do: the scripting runtime is an interpreter and
not a JIT, so no executable-memory exception is needed, and every dylib inside
the bundle is signed by the same identity in the seal above, so library
validation holds. An entitlement that is not needed is signed-in permission
nobody asked for. A game that genuinely requires one gets a reviewed
entitlements file and a line here beside the reason.

### The wait

`notarytool submit --wait` blocks until Apple answers. The wait is **bounded**:
30 minutes by default, or whatever `ORKIGE_NOTARY_TIMEOUT` names. Apple's
service usually answers in minutes and occasionally takes far longer, so a wait
that runs out is not a rejection — the export says so, names the submission id,
and nothing is stapled. `xcrun notarytool log <id>` collects the verdict
afterwards.

In the editor, an export runs on a worker thread and every command line above is
echoed into the Console as it happens, so a long submission is visibly waiting
rather than apparently hung. Credential values are replaced with `<redacted>`
before a line is printed: `notarytool` takes its credentials on an argv and
offers no alternative, so the values that must never appear travel with each
planned command and are removed from what is shown.

### Where signing is available

Signed distribution is a **command-line** operation, for the same reason store
packaging is ([store-release.md](store-release.md)): it needs machine-local
secrets. `--sign` and `--notarize` exist on `orkige_export` and on
`orkige_editor export`; the editor's **Build ▸ Export** menu and the MCP
`export_project` verb both package the ad-hoc app, and neither has a way to ask
for anything else. What the editor contributes is the credential surface — the
Signing tab holds the names so the command line finds them without being told
twice.

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

### What the Linux package does not do

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
  [macOS above](#signing-a-macos-package-three-states-and-nothing-in-between)
  there is nothing to configure and nothing that can be half-signed.

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
- `tests/exporter/ExportMacosSignTests.cpp` — the pure signing decisions: the
  command sequence and its order, the hardened-runtime and timestamp flags,
  every refusal (each naming its missing credential), the redaction, and the
  verdict parse where "we could not tell" and "Apple said yes" must never be
  the same answer. No certificate, no account, no network.
- `export_macos_signed` (ctest) — the refusal leg runs everywhere: a signed
  export with no identity must fail, name the variable, and leave no artifact.
  The signature leg signs for real and puts the result to
  `codesign --verify --strict`; on a machine with no Developer ID identity it
  SKIPS (77) rather than passing over nothing. Notarization itself is not a
  ctest — it is a network round trip against an Apple account, so what is
  testable about it, the decisions, is tested, and the round trip is exercised
  by running a real signed export.
- The platform vocabulary, the artifact naming and every refusal sentence are
  pure functions asserted in `orkige_exporter_tests` and
  `orkige_editor_core_tests` — including that the editor's idea of this host's
  desktop platform and the exporter's are the same one.
