# Desktop export

A desktop package is the game as a person receives it: the runtime, the engine
media and the project payload arranged so the whole thing boots by being
double-clicked, with no arguments and nothing installed beside it.

Three shapes, one for each desktop system Orkige packages for:

| Platform | Artifact | Where it lands |
|----------|----------|----------------|
| `macos` | `<Name>.app`, a bundle | `<project>/builds/macos/` |
| `linux` | `<Exe>/`, a portable directory | `<project>/builds/linux/` |
| `windows` | `<Exe>/`, a portable directory | `<project>/builds/windows/` |

All three come out of the same export: the same payload staging, the same
export-time [texture cook](textures.md), the same baked texture samplers, the
same `THIRD-PARTY-NOTICES.md`, and the same default-project marker. Only the
enclosing shape differs, because the systems disagree about what an application
is — macOS has a bundle format and the other two do not.

Three doors, one implementation: `orkige_export` inside this repository,
**Build ▸ Export** in the editor, and `orkige_editor export` on a machine that
carries only an installed Orkige ([editor-cli.md](editor-cli.md)).

## The desktop platform is the host's own

A desktop package is assembled **around a player binary**, and nothing in the
export pipeline cross-compiles one. So `macos` packages on a Mac, `linux` on
Linux and `windows` on Windows, and each refuses the others by name rather than
writing a directory that cannot run:

```
$ orkige_export --project ~/games/roller --platform linux --engine-build build/macos-debug
orkige_export: ERROR: a Linux package is assembled around the Linux player, and
this exporter runs on macOS - nothing here cross-compiles a player for another
operating system. Export for macOS, or run the export on a Linux machine
```

The editor says the same thing in its own words, and its **Build** menu offers
only the host's desktop item — an entry that could never succeed is worse than
no entry. That item is derived from the one host pairing rather than written out
per system, so the menu and the refusal are the same decision. Over MCP,
`export_project` takes any desktop platform like any other platform and answers
with the refusal where it does not apply.

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

## The Windows package

```
JumperLua/
    JumperLua.exe           the game (the player binary, renamed)
    Media/                  the render flavor's engine media
    project/                manifest, scenes/, assets/, scripts/, data/
    orkige_project.txt      the default-project marker
    THIRD-PARTY-NOTICES.md  the linked libraries' license texts
```

The same portable directory as the Linux package, for the same reason: Windows
has no bundle format either, so the directory is the artifact. It is copied or
archived whole and the program is run from inside it.

```
cd JumperLua
JumperLua.exe
```

The directory and the executable carry the **same** name — the project name
reduced to its alphanumerics — and the `.exe` is not decoration. Windows takes
executability from the extension, so there is no permission bit to set the way
the other two desktops have one.

Two names Windows will not accept, handled rather than discovered: a project
whose name reduces to `CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9` or `LPT1`–`LPT9`
gets a `Game` suffix. Those name character devices at *every* directory, and the
extension does not save them — `CON.exe` is still the console — so the artifact
could not otherwise be written at all.

The marker does the rest, exactly as it does elsewhere: the runtime reads
`orkige_project.txt` and the `Media/` tree relative to `SDL_GetBasePath()`, which
on Windows is the directory the executable lives in. Nothing is baked into the
binary, so moving the directory changes nothing.

### What the machine supplies

The package carries no libraries, and that is a property of the build rather
than an omission: the dependency closure is linked **statically** (the
`x64-windows-static-md` triplet), so the binary the export copies is already
whole. What rides beside it is *enumerated* rather than assumed — the export
copies the DLLs that sat beside the player it packaged, which is none today, and
would be the right answer rather than a broken package if a dependency ever
built as one.

What stays dynamic is the machine's own, and on Windows that is one tier worth
naming precisely:

- **The Microsoft Visual C++ runtime** (`vcruntime140.dll`, `msvcp140.dll`) and
  the Universal CRT. The triplet's `-md` half is exactly this: every dependency
  static, the C and C++ runtimes dynamic. They are **system tier**, the way
  glibc and libstdc++ are on Linux — supplied by the *Visual C++
  Redistributable*, which virtually every Windows machine already carries and
  which Microsoft distributes for the ones that do not. The export neither
  bundles them nor installs them, because that is an installer's job and this
  artifact deliberately installs nothing.
- **The graphics and audio stack**: the Vulkan loader (`vulkan-1.dll`, placed by
  the GPU driver, the same system-tier position it holds on Linux) for the
  default flavor, `opengl32.dll` for the classic one, and the audio devices SDL
  resolves through the platform.

One consequence is sharp enough to state on its own: **a shipping package must
be built from a release tree.** A debug build links the *debug* C runtime
(`vcruntime140d.dll`), which is not redistributable and exists only on machines
with Visual Studio installed — a package built from a debug tree starts on the
machine that built it and nowhere else. The export prefers a release player
automatically when one is beside the tree it was given, and says so in the log
when it has to fall back.

## Signing a Windows package: two states, and nothing in between

Windows has a code-signing tier of its own — Authenticode, and the SmartScreen
reputation that hangs off it — so a package meant for **other people's PCs**
names a publisher or it does not.

| State | What it is | What it needs |
|---|---|---|
| **unsigned** (default) | a plain copy of the player binary, naming nobody | nothing |
| **signed** (`--sign`) | an Authenticode signature over each shipped binary, countersigned through RFC 3161 | a code-signing certificate |

An export that asks for nothing produces the unsigned package, byte for byte,
and consults no credential. Signing is opt-in and always explicit, and a signed
export whose certificate is missing **refuses and packages nothing** — a
half-signed artifact is worse than an honestly unsigned one.

There is no third state, and that is the honest difference from the macOS tier
above. **Nothing here corresponds to notarization.** No service is asked for a
verdict, so there is nothing to poll, no wait to bound and nothing to staple —
`--notarize` on a Windows package refuses by name. The timestamp below is a
*countersignature over the signature*, not an opinion about the program.
Reputation is earned by the certificate over time rather than granted per
build, which means a brand-new certificate still meets a SmartScreen warning
for a while; that is a property of the ecosystem, not of the export.

### Signing from the command line

```sh
# a certificate already in this machine's store, named by its thumbprint
orkige_export --project projects/mygame --platform windows ^
    --engine-build build/windows-release ^
    --sign --windows-thumbprint A1B2C3D4E5F60718293A4B5C6D7E8F9012345678

# ...or a certificate file, whose password comes from the environment alone
set ORKIGE_WINDOWS_SIGNING_PASSWORD=...
orkige_export --project projects/mygame --platform windows ^
    --engine-build build/windows-release ^
    --sign --windows-certificate C:\keys\publisher.pfx
```

`orkige_editor export` takes the identical flags.

### Credentials

| What | Flag | Environment | Settings key |
|---|---|---|---|
| machine-store certificate thumbprint | `--windows-thumbprint` | `ORKIGE_WINDOWS_SIGNING_THUMBPRINT` | `windows.distribution.thumbprint` |
| certificate file (`.pfx`) | `--windows-certificate` | `ORKIGE_WINDOWS_SIGNING_CERTIFICATE` | `windows.distribution.certificate` |
| ...its password | **none** | `ORKIGE_WINDOWS_SIGNING_PASSWORD` | **none** (the credential store) |
| RFC 3161 timestamp authority | `--windows-timestamp-url` | `ORKIGE_WINDOWS_TIMESTAMP_URL` | `windows.distribution.timestampUrl` |
| `signtool.exe`, named outright | `--signtool` | `ORKIGE_SIGNTOOL` | — |

The precedence is the one rule everywhere: **an explicit value wins, then the
environment.** The names follow the vocabulary the other platforms use —
`ORKIGE_<PLATFORM>_<WHAT>`, as `ORKIGE_MACOS_SIGNING_IDENTITY` and
`ORKIGE_ANDROID_KEYSTORE` do — rather than naming the signature format, because
nothing else in the export environment is named after a technology and somebody
looking for "the Windows signing variables" should find them by the platform
they are packaging for. `ORKIGE_SIGNTOOL` is spelled like `ORKIGE_BUNDLETOOL`,
which is the other variable that names a **program** rather than a credential.

Two routes, and **the machine store wins when both are configured**: the
private key never leaves the certificate store — the shape a hardware token or
an HSM has — so a run taking that route holds no secret at all, and one that
holds no secret cannot leak one. A thumbprint is a public hash of a public
certificate, which is why it may travel on a command line and is *not* redacted
out of the echoed log; hiding it would only make the log useless for telling
which certificate signed.

The `.pfx` password is the one credential with no flag and no settings key — it
is a secret, so it lives in the OS credential store and reaches the signing step
through the environment. A certificate file named with **no** password refuses
rather than proceeding: `signtool` would stop and *ask*, which on a build server
is a job that hangs instead of a job that fails.

### Finding signtool

There is no `xcrun` here. `signtool.exe` ships inside the Windows SDK (the
*Windows SDK Signing Tools* component), under a per-SDK-version directory, and
is on no machine's `PATH` by default — so it is searched for rather than
assumed, in this order:

1. `ORKIGE_SIGNTOOL` (or `--signtool`), if named. A named tool that is not there
   **refuses rather than falling back**: somebody who names a tool means that
   tool, and quietly signing with a different one is exactly the silent
   substitution the search exists to prevent.
2. every Windows Kits root the environment points at — `WindowsSdkDir` first,
   then the two Program Files directories — **newest SDK version first**. The
   ordering compares version components as numbers, not as text: sorted as text
   `10.0.9000.0` outranks `10.0.22621.0`, and the search would take a
   decade-old tool off a machine that has a current one.
3. the entries of `PATH`, split and probed explicitly. A bare name handed to
   the process launcher fails as "could not run 'signtool'", which names
   neither what is missing nor how to get it.

A machine with none of these is told to install the Windows SDK, and it is told
that **before a single file is copied** — the tool is located in the same gate
that resolves the credentials.

### What a signed export runs

```
signtool sign /fd SHA256 /tr <timestamp url> /td SHA256 \
         /sha1 <thumbprint>                              <Name>.exe
signtool verify /pa                                      <Name>.exe
```

...or, on the certificate-file route, `/f <pfx> /p <password>` in place of
`/sha1`. Every DLL that rode into the package is signed and verified the same
way first; there is no seal over a directory here, so each file stands alone.

- `/fd SHA256` is the digest of the signature and `/td SHA256` the digest of the
  countersignature. SHA-1 is not accepted for either any more, and defaulting is
  not the same as choosing, so both are stated.
- `/tr` is the RFC 3161 form. The older `/t` protocol no longer produces a
  timestamp an operating system accepts.
- `/pa` selects the **Authenticode** policy — what an operating system applies
  to a program. Without it `signtool` verifies against the *driver* policy,
  which a perfectly good application signature fails.

The sequence is decided up front by a pure planner
(`tools/exporter/ExportWindowsSign.h`) and each command is spawned directly as
an argv — no shell, nothing handed to a command interpreter. The password is
replaced with `<redacted>` before any line is echoed, for the reason the
notarization credentials are: `signtool` takes it on an argv and offers no
alternative.

### The timestamp is not optional

An Authenticode signature with no countersignature stops verifying the day the
certificate expires, which turns every copy already in people's hands into an
unsigned one. So the timestamp URL has a default and can be pointed elsewhere
with `--windows-timestamp-url`, but there is no way to ask for a signature
without one.

### Where signing is available

Signed distribution is a **command-line** operation, exactly as it is on macOS
and for the same reason: it needs machine-local secrets. `--sign` exists on
`orkige_export` and on `orkige_editor export`; the editor's **Build ▸ Export**
menu and the MCP `export_project` verb both package the unsigned directory, and
neither has a way to ask for anything else. What the editor contributes is the
credential surface — **Build ▸ Project Settings ▸ Signing**, Windows /
Distribution, which keeps the names in a per-project file outside every project
tree and the password in this machine's Credential Manager
([store-release.md](store-release.md) has the full model).

`--sign` is **one ask on both desktops**; which platform's rules it means is
decided by `--platform`. A credential aimed at the other platform's gate is
refused by name rather than ignored.

### What the Windows package does not do

Stated plainly, because each is a thing somebody will look for:

- **No icon on the executable.** A Windows program carries its icon as a
  *resource inside the binary*, so setting one means rewriting the executable
  rather than copying a file beside it. `export.icon` is read by the macOS, iOS
  and Android packages today; the Windows executable shows the default.
- **No shortcut and no Start-menu entry.** The package writes neither, for the
  reason the Linux one writes no `.desktop` file: those belong to an install,
  and a portable directory deliberately installs nothing.
- **No archive and no installer.** The artifact is a directory. Zipping it is
  one command, and an MSI or an installer executable is a different kind of
  artifact that is not the exporter's business yet.
- **No compiled game code.** A project with a `native.target`
  ([native-modules.md](native-modules.md)) is refused by name, as it is for
  Linux: the module build the exporter drives is written against an Apple
  toolchain today.
- **A console window.** The player is a console-subsystem program, which is what
  makes its output and its exit code reach a caller — the property every test
  leg depends on — so a packaged game currently opens a console window beside
  it. Changing that is a change to the player's own target shape, not to
  packaging.

## Test builds

`--with-tests` works on every desktop platform: the project's own Lua suite
rides in the payload and the package runs it instead of the game, exiting with
the suite's verdict ([testing.md](testing.md)). The reason it works here and not
for Android or the browser is discovery — the runner finds a suite by walking a
directory, and a desktop package's payload is loose files whichever shape
encloses it.

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
- `export_windows` / `export_windows_tests` (Windows only) are the same pair for
  the Windows package. The structure leg additionally holds the package's
  libraries against the ones that sat beside the player it was built from — so
  a dependency that starts building as a DLL either travels or fails the test,
  and neither outcome is silent — and asserts that no link or debug artifact
  (`.pdb`, `.ilk`, `.lib`, `.exp`) and no second executable came along. The run
  leg's environment is a **keep-list** rather than an empty one: a Windows
  process with no `SystemRoot` cannot load a system DLL at all, so the scrub
  removes the developer `PATH` and everything else that could stand in for a
  resource the package forgot, while leaving the variables the operating system
  itself needs.
- `export_macos_lua` / `export_macos_native` / `export_macos_tests` are the
  macOS siblings.
- `tests/exporter/ExportMacosSignTests.cpp` — the pure signing decisions: the
  command sequence and its order, the hardened-runtime and timestamp flags,
  every refusal (each naming its missing credential), the redaction, and the
  verdict parse where "we could not tell" and "Apple said yes" must never be
  the same answer. No certificate, no account, no network.
- `tests/exporter/ExportWindowsSignTests.cpp` — the pure Authenticode
  decisions: the whole `signtool` search (the numeric version ordering where
  sorting as text picks a decade-old tool, the candidate paths, the `PATH`
  split, and the named-tool override that refuses instead of falling back),
  both command shapes, every refusal naming its missing credential, and the
  redaction — together with the deliberate *non*-redaction of a thumbprint,
  which is public. No certificate, no Windows SDK, no Windows.
- `export_windows_signed` (Windows only) — the four refusal shapes run
  credential-free on every Windows machine: no credential at all, a certificate
  file with no password, a credential named without `--sign`, and `--notarize`
  on a platform that has no such thing. Each must fail, name what is missing,
  and leave no executable behind. The signature leg signs for real and puts the
  result to `signtool verify /pa`, with the tool the export itself reported
  rather than a second search that could disagree; on a machine that names no
  certificate it SKIPS (77) rather than passing over nothing.
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
