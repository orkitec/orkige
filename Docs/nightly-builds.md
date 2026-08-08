# Nightly editor builds

Once a night, CI packages the Orkige editor for macOS, Linux and Windows and
publishes the results as two prereleases: a rolling one whose download URLs
never move, and a dated one that stays. The point is a download
instead of a build: a C++ toolchain is needed only to write native game code, not
to open the editor and make a game in Lua.

This channel is how the engine ships. There is no second, slower track behind
it: every build comes from a commit the full test matrix proved green, and the
day's build is the release of that day.

The pipeline lives in `.github/workflows/nightly.yml` beside the other scheduled
work (the soak, Valgrind and fuzz watches — one Actions run a night). The
packaging itself is `Util/orkige_nightly_package.py`, which shares the
build-tree lookups with the phone-session front door through
`Util/orkige_buildtree.py` and drives the engine's own `orkige_export` binary
for the macOS dylib closure rather than restating either.

Where the artifacts go, and what they can and cannot do, is below. Read
[what a downloaded build cannot do yet](#what-a-downloaded-build-cannot-do-yet)
before expecting one to replace a build tree.

## The green gate

Binaries are built ONLY from a commit the full test matrix proved green. The
`binaries-gate` job asks the API for the most recent **completed** `ci.yml` run
on `main` and reads its conclusion:

- conclusion `success` → the build jobs check out **that run's commit** (not
  whatever `main` is at now, which may carry untested pushes) and proceed.
- any other conclusion, or no completed run at all → every later job is skipped
  and the log carries an annotation naming the run and its conclusion. A red
  tree is never published, and the failure is not reported twice: `ci.yml`
  already said so.

The gate is an API query rather than a `workflow_run` trigger because
`workflow_run` fires once per CI completion — that is once per push, which would
rebuild and republish desktop binaries many times a day, and it cannot be put on
a schedule. One scheduled query gives exactly one build a day from the last
proven-green commit.

The schedule is the shared nightly cron, 23:07 UTC: shortly after 01:00 in the
owner's timezone, and off the round minute where GitHub's cron backlog collects.
A push made minutes before that has not finished CI yet, so the gate finds the
previous green run and builds that — which is the intended behaviour, not a
miss.

## Unchanged trees are not rebuilt

A scheduled run also compares tonight's green commit against the one the standing
nightly was built from: identical means **skip**, with a notice saying so. The
predecessor's commit comes from a machine-readable marker the publish job writes
into the release notes (`<!-- orkige-nightly-commit: … -->`), so the check needs
no state outside the release itself; a missing marker or a missing release — the
first night, or one deleted by hand — counts as "build". The job summary
distinguishes the two skip reasons, so a quiet night reads as *nothing new*
rather than as a red tree.

That marker does double duty: the predecessor's commit is also the lower bound of
the changelog every artifact and the release notes carry, so it is read once and
handed to every job that needs it.

A **manual** `workflow_dispatch` always builds, even from an unchanged commit:
asking for a build by hand is itself the override.

`workflow_dispatch` runs the same pipeline on demand. Its `run_binaries` input
(default on) skips it, and `ignore_gate` builds the dispatched ref even when
`main` is red — the log and the job summary both name the override, so an
artifact produced that way is never mistaken for a gated one.

## What each platform ships

Each platform ships the artifact a person on it expects, and a second, portable
one beside it:

| Platform | Runner | Preset | Install | Portable |
| --- | --- | --- | --- | --- |
| macOS (Apple silicon) | `macos-15` | `macos-release` | `Orkige-macos-<version>.dmg` | `Orkige-macos-<version>.zip` |
| Linux (x86_64) | `ubuntu-latest` | `linux-release-next` | `Orkige-linux-<version>.AppImage` | `Orkige-linux-<version>.tar.gz` |
| Windows (x64) | `windows-latest` | `windows-release` | `Orkige-windows-<version>-setup.exe` | `Orkige-windows-<version>.zip` |

All are the default Ogre-Next render flavor in Release, and `<version>` is the
ordered version in its filename rendering (below). **Every asset has a `.sha256`
file beside it** — installers included — and the publish job re-checks each one
against the sidecar that travelled with it.

Where a platform has both, the two containers hold the same build: they are made
from ONE staged directory, so the installed editor and the unpacked archive are
the same bytes.

- The **install** artifact is what a person downloads. On macOS a `.dmg` whose
  root is the app plus an `/Applications` symlink: dragging it there is the
  install, and it is what avoids app translocation (macOS gives a downloaded app
  launched out of the folder it was unpacked into a read-only randomized path
  instead of its own). It is also the container a notarization ticket staples
  onto directly, so the shape that fixes translocation today is the shape that
  carries a signature when there is one. On Windows an NSIS installer
  (`Util/orkige_installer.nsi`, compiled by `makensis`): per-user, into
  `%LOCALAPPDATA%\Programs\Orkige`, **no administrator elevation**, a Start-menu
  shortcut, a working uninstaller, and the ordered version recorded under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Orkige` so Settings >
  Installed apps lists it like any other program. On Linux an **AppImage**:
  distributions share no package format, but they all run a single executable
  file, and that file carries the libraries a given distribution may not have
  installed — which is the difference between it and the tarball beside it
  ([the Linux single-file bundle](#the-linux-single-file-bundle)).
- The **portable** artifact is the `.zip` / `.tar.gz`: nothing to mount, nothing
  to run, unpack anywhere. It is also the shape an updater consumes, because
  replacing files in place needs neither a mounted image nor an installer run.

The archive has one top-level directory and the same shape everywhere:

```
Orkige-<platform>-<version>/
    VERSION                 the build identity, one `key: value` per line
    CHANGELOG.md            what landed since the previous nightly
    KNOWN-LIMITATIONS.md    what this build cannot do yet
    <the editor>            Orkige.app, or orkige_editor[.exe]
    orkige_editor[.exe]     the command line (Docs/editor-cli.md)
    <the player>            beside the editor, for Play
    <the texture cook tool> beside the player, for the export cook
    <resources>             the engine media, at the path the editor resolves
    <resources>/web/        the browser payload a web export ships
```

The resources sit exactly where the editor's own resource locator looks — that
locator resolves bundle-first, so the archive layout is not a convention of the
packaging but the editor's own contract (`Docs/editor-distribution.md`):

- macOS: inside the bundle — `Contents/MacOS` for the executables,
  `Contents/Resources/Media` for the media. `VERSION`, `CHANGELOG.md` and
  `KNOWN-LIMITATIONS.md` sit at the resource root (`Contents/Resources`) — where
  the editor reads its own changelog back for the About box — and are repeated
  at the archive root (and so appear on the mounted disk image) so they are
  readable before installing. The bundle is
  re-signed after staging so its resource seal covers everything the packaging
  added ([macOS signing](#macos-signing-notarization-and-stapling)), and zipped
  with `ditto`, which preserves its symlinks and executable bits. The disk image
  is built with `hdiutil` from that same sealed staging, with the `Applications`
  symlink added for the duration of the call: the app is well over a hundred
  megabytes, and a second copy of it would be both slow and a chance for the two
  artifacts to diverge.
- Linux and Windows: the executables at the top level and the resources under
  `share/orkige/` beside them (`share/orkige/Media/…` plus the editor's icon and
  mono fonts, and the same three text files), which is what the locator reads
  relative to `SDL_GetBasePath`.

The archive root always carries one invokable `orkige_editor[.exe]`, which is
what a build server runs to reach the [subcommands](editor-cli.md). On Linux and
Windows it is the editor executable itself. On macOS the executable is inside
the bundle, so the archive root carries a small `/bin/sh` wrapper of that name
which `exec`s `Orkige.app/Contents/MacOS/Orkige` beside it. The wrapper is
written **after** the disk image is built, so it belongs to the portable archive
alone: the image is the drag-to-`/Applications` road, where the app leaves the
volume and a wrapper would not follow it. `verify_layout` refuses an archive
without the entry point, or one whose wrapper no longer names the executable.

The build stages that same payload into the build tree, so the two are one
layout: the packaging targets `orkige_editor_bundle` (which depends on the
editor, the player and the texture cook tool) rather than the bare editor,
because a scoped `--target orkige_editor` leaves the staging unrun.

Platform-specific handling worth knowing:

- **Linux** links the whole engine statically but still loads the
  distribution's own libraries. The tarball names the packages it needs; the
  AppImage carries the ones a distribution may not have — see below.
- **Windows** builds on the `x64-windows-static-md` triplet: every dependency is
  static, but the Visual C++ runtime stays SHARED. The packager copies
  `VCRUNTIME140.dll` / `MSVCP140.dll` app-local from the build machine's
  redistributable (the supported deployment) and records in `VERSION` whether it
  managed to; when it could not, the limitations file tells the user to install
  the redistributable rather than leaving a silent launch failure. The installer
  installs the staged directory verbatim, those app-local copies included, so it
  is never less correct about the runtime than the zip and pulls in no
  redistributable bootstrapper of its own.
- **macOS** needs no dylib closure today — the built bundle depends on nothing
  outside the system frameworks — but the packaging runs the closure step
  anyway, so a future dependency rides along instead of breaking a download.

### The browser player payload

Every published editor can package a game for the browser, on any of the three
platforms. A web export compiles nothing — the wasm player is a prebuilt
artifact and the rest is bytes the exporter arranges — but it does need that
player, and no desktop packaging runner can cross-build wasm. So a fourth job,
`binaries-web-player`, builds the `web-release` player once (the same pinned
emsdk and cache keys the push CI's web job uses) and composes the whole payload:

    orkige_nightly_package.py --stage-web-payload web-payload \
                              --web-build build/web-release

The three packaging jobs download that as an artifact and stage it into the
editor with `--web-payload` (the `ORKIGE_WEB_PAYLOAD` environment the workflow
sets). It carries the player pair, the shell page and the CLASSIC engine media
the browser player renders through — the browser target is flavor-independent,
so the same payload rides inside every editor whatever its own flavor
([the browser payload](web-export.md#the-browser-payload-inside-a-packaged-editor)).

It is an added capability, not a precondition for shipping an editor. The
packaging jobs WAIT for the payload job but publish without its artifact if it
fails, so a broken wasm build costs the night its browser target and nothing
else. What is never allowed is a half-staged one: a payload handed to the
packaging that is missing a file is REFUSED rather than copied, because it would
fail inside a user's export instead of before it. The artifact records which of
the two states it is in:

    web-export: bundled     # VERSION - this editor packages for the browser
    web-export: absent      # ... and this one says so, in KNOWN-LIMITATIONS.md too

### The device player payloads

A phone runs another architecture's binary, so unlike the browser player this
one cannot ride inside every editor: `binaries-player-ios` builds the
iOS-simulator player from its preset tree and publishes the whole payload as its
own release asset,

    orkige_nightly_package.py --stage-device-payload player-ios-simulator \
                              --build-dir build/ios-simulator-debug \
                              --commit <sha> --output device-payload

which the publish job uploads beside the editors. A released editor FETCHES it
on demand, once, when someone switches that platform on under Settings > Build
Targets — paired on the dated release tag its own version names, so the player
and the editor come from one night's publish
([device payloads](device-payloads.md)).

Like the browser payload it is an added capability, not a precondition: the
publish job waits for it but ships the editors without it, and the release notes
then say that no mobile player was published for that build. That line is where
somebody checks when an editor reports it cannot find one.

## The Linux single-file bundle

The tarball carries the editor but not the libraries it links, and that list is
longer than a user expects. Beside the X11 and GL/Vulkan libraries every
graphical program needs, the editor pulls in `libXaw`, `libXmu`, `libXpm`,
`libXt`, `libICE` and `libSM` — the Xt/Athena family nothing on a modern desktop
installs on its own — plus `libbsd`, `libmd`, `libuuid` and `libatomic`.
Unpacking the tarball on a clean distribution therefore ends in a loader error
naming a library its user has never heard of. The `.AppImage` is one file that
carries them: `chmod +x`, run.

### What it bundles, and what has to come from the host

One rule, and it is the decision the whole artifact turns on:

> Every library the loader resolves for the editor is bundled EXCEPT the ones
> whose correct version is a property of the **machine** rather than of our
> build.

Four families qualify, each for a reason that is not a preference:

| Family | Left to the host | Why |
| --- | --- | --- |
| driver | `libvulkan`, `libGL`, `libEGL`, `libGLX*`, `libGLdispatch`, `libOpenGL`, `libGLESv*`, `libglapi`, `libdrm`, `libgbm` | These are the front doors into the machine's own GPU driver, which is matched to its kernel and its hardware. A bundled copy either shadows that driver's entry point or is substituted into the driver's own dependency chain — turning a working GPU into a software fallback or a crash. |
| libc | `libc`, `libm`, `libdl`, `libpthread`, `librt`, `libresolv`, `libutil`, `libnss_*`, `ld-linux*` | The process is started by the HOST's loader and resolves users and hosts through the HOST's NSS modules. A second glibc inside the image is a mismatch, not a fix — which is what makes the glibc floor a property of the build image. |
| toolchain | `libstdc++`, `libgcc_s` | Because glibc is not bundled, a machine that can run the image is already at least as new as the machine that built it, so its C++ runtime can never be too old — while a bundled copy OLDER than the host's Mesa driver (which resolves its own `libstdc++` through our search path) breaks that driver. Bundling would carry all of the risk and none of the benefit. |
| server-client | `libX11`, `libxcb*`, `libwayland-*`, `libxshmfence`, `libasound`, `libpulse`, `libjack`, `libdbus-1`, `libudev` | These talk to a server or daemon that is part of the running system and load the host's own modules (X11 locale and input-method data, ALSA plugins) by absolute path. They are also on every machine that has a display at all. |

Everything else is bundled. `libatomic` is a deliberate near-miss worth stating:
it comes from the same compiler as `libstdc++`, but a distribution installs it
only when something asks for it, so **presence** rather than version is what
decides — and presence is exactly what a download cannot assume. The clean-room
check below is what caught it.

The bundled copies win because the `AppRun` puts the image's own `usr/lib` on
`LD_LIBRARY_PATH`, which the loader searches before every system directory. So
for a bundled name the host's copy is never consulted, which is what makes "the
host does not have it" a non-event. That variable also reaches the processes
the editor spawns, which is a second reason the bundled set stays leaf
libraries no other program's behaviour hinges on.

### The glibc floor

glibc is the one family the image cannot carry, so the oldest distribution it
runs on is decided by the machine that built it. The packaging does not assume
that number, it **measures** it: the highest `GLIBC_x.y` symbol version the
binary references, read out of the binary with `objdump -p`, recorded as the
`glibc-floor:` line in `VERSION` and pointed at by the artifact's
`KNOWN-LIMITATIONS.md`. The nightly's Linux job runs on `ubuntu-latest`, so the
floor can never exceed that image's glibc (2.39 on Ubuntu 24.04) and in practice
sits just under it, because a binary references only the symbol versions it
uses — so the image runs on that generation of distributions and every newer
one. The recorded number is the authority: reading it out of the binary means
the floor follows the runner image instead of following a sentence in this
document.

### FUSE, and how to run one without it

An AppImage mounts itself through libfuse at run time, and some current
distributions no longer ship FUSE. `--appimage-extract-and-run` (or the
`APPIMAGE_EXTRACT_AND_RUN` environment variable) unpacks to a temporary
directory and runs from there instead, needing nothing:

```sh
chmod +x Orkige-linux-<version>.AppImage
./Orkige-linux-<version>.AppImage                            # with FUSE
./Orkige-linux-<version>.AppImage --appimage-extract-and-run # without
```

Nothing in the pipeline depends on FUSE either: `appimagetool` is an AppImage
too and is run that way, and every check runs the produced image that way.

### Desktop integration

The image carries an `orkige.desktop` entry (name, generic name, comment,
`Development;IDE;` categories and the `StartupWMClass` that pairs a window with
its launcher) and a 256×256 icon drawn by the same generator the macOS `.icns`
comes from — at the AppDir root where the runtime looks, as `.DirIcon` where a
file manager reads it, and under `usr/share/applications` and
`usr/share/icons/hicolor` where a desktop that installs the file expects them.
So an integrated image gets a name and an icon rather than a path.

### The tool

`appimagetool` is a separate download — not a dependency this repository
declares, and on no runner — so it is resolved the way the Android bundle's
`bundletool` is (`Docs/store-release.md`): an explicit `--appimagetool`, else
`ORKIGE_APPIMAGETOOL`, else one on `PATH`. The nightly's Linux job downloads a
**pinned release**, checks it against its SHA-256 and exports the path; that
step **fails the job** with a sentence naming what is missing, before the build,
rather than letting a night ship without the artifact. A hand run without the
tool still produces the tarball and says why there is no image.

## macOS signing, notarization and stapling

A downloaded macOS app is held to rules a locally built one is not, and the
whole point of a nightly is that it is downloaded. The pipeline therefore signs
the bundle with a **Developer ID Application** certificate, submits both
artifacts to Apple for **notarization**, and **staples** the resulting tickets
into them — so the editor opens with no security prompt and needs no network to
prove it may.

### One seal, two identities

The bundle is sealed **inside-out** — nested code first, then the bundle, because
a bundle's seal records the signatures beneath it — by one function, whatever it
signs with. Only the identity differs:

| | ad-hoc | Developer ID |
| --- | --- | --- |
| certificate | none | Developer ID Application |
| `codesign` flags | `--force --sign -` | `--force --sign <identity> --timestamp --options runtime` |
| verification | `codesign --verify` | `codesign --verify --strict` |
| what it proves | the bundle is internally consistent | who built it, that it was not altered since, and when |

The hardened runtime (`--options runtime`) and the secure timestamp
(`--timestamp`) are not preferences: **Apple rejects a notarization submission
missing either**, so a real signature always carries both, on every binary and
not just the outer bundle.

**No entitlements.** Every hardened-runtime restriction that could bite is one
this editor does not need: the scripting runtime is an interpreter and not a JIT
(so no executable-memory exception), every dylib inside the bundle carries the
same signature from the seal above (so library validation holds), and the tools
the editor spawns — the player, the texture cook tool, `cmake`, `git` — are
separate processes under their own policy rather than code loaded into this one.
An entitlement that is not needed is signed-in permission nobody asked for. If
something ever genuinely refuses to run without one, it belongs in a reviewed
`.entitlements` file, passed through `codesign --entitlements` (which the
packager's argv already supports), with the reason recorded here. The one
foreseeable case is a flavor that loads a Vulkan driver from outside the bundle,
which library validation would refuse — the shipped nightly is the Ogre-Next
flavor on Metal and loads nothing of the sort.

### The credentials, and where they may appear

The certificate and the notarization credentials are repository secrets, and the
rules around them are the same ones the iOS signing seam follows
(`Docs/ios-signing.md`): **never committed, never on a command line this
pipeline writes, never in a path an artifact upload can reach.**

The packager reads them from the **environment**:

| variable | what it is |
| --- | --- |
| `ORKIGE_MACOS_SIGNING_IDENTITY` | the Developer ID identity — a name or a SHA-1 (also `--signing-identity`; not a secret) |
| `ORKIGE_MACOS_KEYCHAIN` | the keychain to search for it |
| `ORKIGE_NOTARY_KEY` / `ORKIGE_NOTARY_KEY_ID` / `ORKIGE_NOTARY_ISSUER_ID` | an App Store Connect API key: the `.p8` file's path plus its two identifiers |
| `ORKIGE_NOTARY_APPLE_ID` / `ORKIGE_NOTARY_APP_PASSWORD` / `ORKIGE_NOTARY_TEAM_ID` | an Apple ID, an app-specific password and the team id |

Either notarization route works and the pipeline does not care which is
configured. When both are, the **API key wins**: it is revocable on its own,
without touching the Apple ID a person signs in with. A **half-configured** set
is not used at all, and the log names exactly which values were missing — a typo
in one secret must not read as "notarization is switched off".

`notarytool` takes its credentials on an argv and offers no alternative, so the
packager echoes those command lines with every credential value **redacted**. The
key file's *path* stays readable: it names a file, it is not the key.

### The CI keychain

The signing key lives in a keychain created for the job and destroyed with it —
never the runner's login keychain, which a later step could still reach. The
workflow's step does, in order: create the keychain under a password generated
on the spot, stop it auto-relocking mid-build (`codesign` cannot answer a
prompt), decode the `.p12` from the secret into `RUNNER_TEMP` and import it,
delete the decoded file immediately, **add the keychain to the user search
list** — `codesign` does not find a key in a keychain that is merely named with
`--keychain`, it has to be searched — and run `security set-key-partition-list`
so the private key is usable without the UI prompt no runner can answer. The
only value that leaves the step is the certificate's SHA-1: a public
fingerprint, and deliberately not the identity's common name, which carries the
team id. A final `always()` step deletes the keychain and every decoded
credential file on **every** path out of the job, including a failed build.

### Order: the ticket has to be inside the .zip

A notarization ticket is issued for what was **submitted**, so an artifact only
ends up with one of its own if it was submitted on its own. The portable `.zip`
is the updater's payload, and a download that needs a network round trip to open
is a second-class one — so the app is notarized *first*, and the sequence is:

1. seal the staged bundle (hardened runtime, secure timestamp);
2. submit the **app** (in a throwaway `ditto` zip, the shape Apple's service
   takes an app in), wait for the verdict, staple the ticket into the app **in
   the staging**;
3. build the `.dmg` from that staging — so the app inside it is already
   stapled — sign the image (a container, so no hardened runtime: that flag
   describes code), submit it, wait, staple it;
4. build the `.zip` from the same staging, which now holds the stapled app.

Both downloads therefore carry a ticket of their own. That is the reason the
macOS disk image is built **before** the portable archive, where every other
platform builds the archive first.

### Verdicts, and what a rejection prints

`notarytool submit --wait --output-format json` is asked for a verdict and the
verdict is read out of the payload, never inferred from an exit code: anything
that is not `"status": "Accepted"` — including output that cannot be parsed at
all — is **not** accepted, because "we could not tell" and "Apple said yes" must
never be the same answer. The wait is generous (Apple's service usually answers
in minutes and occasionally takes far longer); a nightly can afford it.

On a rejection the packager fetches and prints `notarytool log` for that
submission before failing. That log is the only thing that names the offending
binary, and a rejection without it is undiagnosable.

Then the tickets are proved rather than assumed: `stapler validate` on both
artifacts, and the assessment Gatekeeper itself performs — `spctl --assess
--type exec` on the app, and `--type open --context context:primary-signature`
on the disk image, which is the assessment Apple documents for that container
(`--type install` is the assessment for an installer package, which this
pipeline does not produce). The build job repeats both on the app it **unpacked
from the archive**, which is what proves the ticket survived the `.zip`.

### When there is no certificate

A fork, a pull request (secrets are not exposed to those) and a hand run on a
machine with nothing configured all still produce a complete, working nightly.
They produce an **ad-hoc** one, and every surface says so rather than implying
otherwise:

- the packaging log warns, naming the variable that was not set;
- the `VERSION` file carries `signing: ad-hoc`, `signing: developer-id` or
  `signing: developer-id-notarized` — one vocabulary, used by everything;
- the artifact's `KNOWN-LIMITATIONS.md` carries the record that describes *that*
  build: what an ad-hoc download does at first launch, or what a signed but
  un-notarized one does, or neither record when there is nothing to warn about;
- the release notes say the same thing, composed from the value read back out of
  the artifact rather than from an assumption;
- and the build job cross-checks the two: the signature the artifact records has
  to equal the one the job set up, or the night does not publish.

Nothing in between ships. A certificate that cannot sign, or a submission Apple
does not accept, **fails the build** — a half-signed artifact is worse than an
honestly ad-hoc one, and an artifact claiming a notarization it never got is
worse than both. `--ad-hoc-sign` forces the ad-hoc path on a machine that does
have a certificate, for a local packaging run that must not reach Apple.

## The ordered version

A commit sha names a tree but has no order, so it cannot answer "is that download
newer than what I run". The ordered version can:

```
2.0.0-nightly.20260730+dea551f9e
^^^^^ ^^^^^^^ ^^^^^^^^ ^^^^^^^^^
base  channel  date     commit (build metadata)
```

It is semantic versioning 2.0.0, so its precedence rules are the standard ones:
the base version first, then the date as a numeric prerelease identifier, with
the build metadata carried along but **never** ordered. Three consequences worth
stating plainly:

- **Two builds of one day are the same version.** A rebuild of today's tree is
  not an update, and a client that treated it as one would re-download forever.
  The commit is still recoverable from the string — that is what the metadata is
  for.
- **A release outranks every nightly of its base.** `2.0.0` follows
  `2.0.0-nightly.20261231`, which is how a channel switch works with no extra
  rule.
- **A base bump outranks any date.** `2.1.0-nightly.20260101` follows
  `2.0.0-nightly.20261231`.

`2.0.0-nightly.20260730_dea551f9e` is the same version rendered for a
**filename**: download paths and asset stores rewrite characters outside
`[A-Za-z0-9._-]`, which would leave a client unable to match a file against the
version it polled. `_` is not a semantic-versioning character at all, so the
token reads back to the same version unambiguously.

### One value, every surface

The version is composed **once**, by the gate job, and every later job consumes
that one value:

- **the archive filename**, `Orkige-<platform>-<filename token>.<ext>`;
- **the `VERSION` file** inside the archive, whose `version:` line is the ordered
  version (with `base-version:`, `commit:`, `built:`, the render flavor, the
  build type and the engine ABI stamp — what a native game module must match,
  `cmake/OrkigeAbiStamp.cmake`);
- **the binary itself**: `orkige_editor --version` prints
  `orkige_editor 2.0.0-nightly.20260730+dea551f9e [next, Release]`, and the
  Help > About box shows the same identity;
- **the release notes**, whose `orkige-nightly-version` marker is the value an
  updater polls (below);
- **the dated release's tag**, `nightly-YYYYMMDD`, composed from the same build
  date the version orders by — so a build's archive entry and its title can
  never name different days.

The binary composes its own copy from the two values the pipeline stamps it with:

```sh
cmake --preset macos-release -DORKIGE_BUILD_COMMIT=dea551f9e \
                             -DORKIGE_BUILD_DATE=2026-07-30
```

They are compile definitions on ONE translation unit
(`tools/editor/EditorBuildInfo.cpp`), so a re-stamp recompiles one small file
rather than the whole editor. An ordinary developer build leaves them unset and
reports `2.0.0 (local build)` — never a commit nobody supplied, and never an
ordered version it cannot justify.

That grammar therefore exists twice: in `orkige_core/core_util/VersionOrder.h`
(which the binary uses, and which is also where the comparison lives) and in the
packaging tooling. Both are unit-tested against the same literals, and the smoke
test matches the packaged version against what the binary reports — so a change
to one that the other does not follow fails the night's build instead of
publishing an artifact whose name and self-report disagree.

The engine ABI stamp is a content fingerprint of the engine sources, which
answers "does this module match this library" and not "which build is this", so
it rides in `VERSION` as extra information rather than serving as the version.

### Comparing two versions

`core_util/VersionOrder.h` is the comparison an updater depends on — pure string
and number work, no filesystem, unit-tested headlessly
(`tests/core/VersionOrderTests.cpp`):

```cpp
using namespace Orkige::VersionOrder;
if (isUpdate(publishedVersion, editorBuildVersion())) { /* offer it */ }
switch (compare(left, right)) { /* VO_OLDER / VO_SAME / VO_NEWER / VO_INCOMPARABLE */ }
```

`compose(base, date, commit)` builds an identity, `commitOf` recovers the commit
from one and `filenameToken` renders it for a filename. Anything that is not a
version — an empty string, `2.0.0 (local build)`, a truncated or mistyped
string — is `VO_INCOMPARABLE`, and `isUpdate` is false for it. An unstamped
developer build is therefore never offered an update it cannot verify, and never
told it is current either.

## The changelog

Every build carries `CHANGELOG.md`, and the release notes carry the same text.
The range is the previous nightly's commit (exclusive) to this one (inclusive):
that lower bound is free, because the marker the gate reads to skip an unchanged
tree is also the changelog's lower bound.

The entry text is a commit **subject reduced to its headline**. This repository's
subjects are one dense narrative line whose first clause is a genuine headline,
separated from the rest by `": "` — so that is where the split happens, with
honest fallbacks: no `": "` takes the first sentence, and a subject with neither
is cut at a hard character cap with an ellipsis. Entries are newest first, each
carrying its short sha, and the list is capped at 20 with a `+N more commits`
line rather than rendering an unbounded wall after a busy fortnight.

The degradations say what they are, in the output:

- **no previous marker** (the first night, or a release deleted by hand): the
  most recent 20 commits, under a line saying there was nothing to compare
  against;
- **a marker this history cannot reach** (a rewritten history): the same window,
  naming the commit it could not find;
- **no history at all** (packaging outside a repository): a changelog saying so
  rather than an empty section implying nothing landed;
- **an empty range** (a manual dispatch of an unchanged tree): "No commits since
  the previous nightly."

Generation is split at a seam — the `git log` invocation on one side, every
decision about what the output says on the other — so the formatting is
unit-tested against synthetic log text instead of whatever history a machine
happens to have.

The changelog is visible in three places, all rendered from the same git
history and none of them committed back into the repository — a generated file
in the tree would be a commit, which would build, which would make the next
night's gate see a moved branch and rebuild for nobody:

- the **release notes** and each archive's `CHANGELOG.md` carry the section
  above: what landed since the previous nightly;
- the **release carries the full history as its own asset**, a `CHANGELOG.md`
  beside the archives (with its own `.sha256`), so "what has ever landed" is one
  fetch rather than a hundred-megabyte download and an unpack;
- the **editor's About box** shows the changelog the running build shipped with,
  read once from the packaged file at its resource root through the one resource
  locator (`Docs/editor-distribution.md`). A build from a source tree carries
  none and says so in one line. `orkige_editor --changelog` prints the same
  text without opening a window, which is how the bundle self-check reads both
  states back off a real staged copy.

### The full history

`--history` renders every commit, newest first, grouped by the **day** it landed
— the axis a daily channel makes obvious. A day is headed by one of two things:

- a day that **published a nightly** is headed by the ordered version identity
  that build carries, composed by the same `nightly_version` the pipeline names
  its artifacts with, from the commit the build was made from; so a heading here
  and the version that binary reports are the same string by construction. What
  proves a day published is its `nightly-YYYYMMDD` release tag, which points at
  exactly that commit — read by `git_release_tags` and turned into the
  {day → commit} map by the pure `published_days`. A day the tags do not name
  gets no version, so no heading can name a build that never existed;
- every **other** day is headed by its date, carrying the retroactive version
  era it falls in (`HISTORY_ERAS`: `0.1.0`, `0.2.0 — Watermaze`,
  `0.3.0 — Think Blue`, `1.0.0 — Pudding Panic`, `2.0.0-pre — Editor`, then the
  nightly period, which carries no era because its days name real builds). Those
  labels are applied in hindsight — nothing here carried a version number before
  the channel began — and the boundary dates are constants with their provenance
  written beside the table, because the branches and the tag that mark them live
  in the private archive repository.

The document says what it is missing, rather than presenting whatever it found
as the whole record:

- a **shallow checkout** lists only the commits that clone carries and says so,
  naming the count. Both jobs that render it check out with `fetch-depth: 0`,
  which brings the tags along, for exactly this reason;
- **tags it could not read** leave every day unmarked and say so, rather than
  reading as "nothing was ever published";
- **no history at all** says nothing is listed and where the document normally
  comes from.

Every dated release is kept, tag and all, so the evidence reaches as far back as
the channel does. It still has to be read: a checkout fetched without tags, or a
git that cannot answer, marks no day at all — which is why an absent marker
records nothing either way, never that the day published nothing.

Tonight's own tag is the one case the tags cannot answer: this document is an
**asset of** tonight's release, so it is written before that release and its tag
exist. The publish job hands the tag in with `--published-tag`, which is why a
build's own changelog marks the night that built it.

The site's changelog page (`Util/make_help_portal.py`) renders this same text —
it imports the composition rather than reading the commit log a second way.

## What an updater reads

**`nightly` is the tag a client polls.** It is the moving one: it always names
the newest build, so one request against it answers "is there something newer
than what I run". The dated `nightly-YYYYMMDD` releases are the **archive** — a
person browses them on the releases page to fetch a specific older build, and a
client has no reason to enumerate them.

The release IS the description of the build. One request returns everything a
client needs:

```
GET https://api.github.com/repos/orkitec/orkige/releases/tags/nightly
Accept: application/vnd.github+json
```

The JSON carries `body` (the release notes), `assets[]` (each with `name`,
`size` and `browser_download_url`) and `published_at`. A token is optional —
sending one only raises the rate limit.

Two **machine-readable markers** sit at the end of the notes, so a client never
parses prose and never guesses a version out of an asset filename:

```
<!-- orkige-nightly-commit: dea551f9e0e0f1a2b3c4d5e6f708192a3b4c5d6e -->
<!-- orkige-nightly-version: 2.0.0-nightly.20260730+dea551f9e -->
```

The version is the ordered identity; the commit is the full source commit (the
gate reads it back to skip an unchanged tree, and it bounds the next changelog).
Both are fixed strings one regex finds in one pass over `body`.

**The contract an updater implements:**

1. `GET /repos/<owner>/<repo>/releases/tags/nightly` — one call, authentication
   optional.
2. Read `<!-- orkige-nightly-version: … -->` out of `body`. A body without it is
   not a release this client understands, and "no update" is the honest answer
   to that.
3. Compare it with its own `editorBuildVersion()` through
   `VersionOrder::isUpdate` — never by string equality, never by date arithmetic
   of its own. `VO_SAME` (a rebuild of today's tree) and `VO_INCOMPARABLE` (an
   unstamped local build) are both "nothing to do".
4. Show the changelog if it offers the update: it is the `## Changes since …`
   section of `body`, the same text the artifact's `CHANGELOG.md` carries. For
   everything before that, the release's own `CHANGELOG.md` asset is the full
   history, fetchable without unpacking an archive.
5. Pick its platform's asset by name. An **updater takes the portable one** —
   `Orkige-macos-<token>.zip`, `Orkige-linux-<token>.tar.gz`,
   `Orkige-windows-<token>.zip`, where `<token>` is the version's filename
   rendering — because swapping files in place needs neither a mounted image nor
   an installer run. The installable assets (`Orkige-macos-<token>.dmg`,
   `Orkige-linux-<token>.AppImage`, `Orkige-windows-<token>-setup.exe`) are what
   a **person** downloads from the release page. A platform whose build failed
   has **no asset under this version's token** — the release may still carry
   that platform's previous build, under the token *that* version renders, and
   the notes table names it as kept. Either way a client asks "is there a build
   for me at this version", and absence is the answer: what is kept is not an
   update.
6. Fetch the `<archive>.sha256` asset beside it and **verify the digest before
   trusting a single byte** of the archive. That sidecar is the download's only
   integrity story; the publish job checks every archive against the sidecar
   that travelled with it, so the two agree at the moment they are served.

Two caveats, stated plainly:

- **The `nightly` tag moves every night.** The download URLs stay the same
  from one night to the next, which is what makes them quotable in a document
  and fetchable by an updater — but it also means a URL saved today serves
  different bytes tomorrow. A client that wants a specific build records the
  version and the `.sha256`, not the URL alone; a **dated**
  `nightly-YYYYMMDD` release is the one whose URLs keep serving the bytes they
  served on the day, and every one of them is kept.
- **Unauthenticated API calls are rate-limited per IP:** 60 requests an hour
  (5000 with a token). A check once a day, or once per launch, sits far inside
  that; a client that polls in a loop is answered `403` with a reset time, and
  has to honour it.

And the trade: reading a release this way couples the client to the GitHub REST
API's shape — the release-by-tag endpoint, `body`, `assets[]`. That coupling is
deliberate. The release is the ONE description of a build, so there is nothing
for a client to read that can disagree with what was published.

## The smoke test

Packaging is not the end of a build job. Each one then unpacks its own archive
into a clean directory that has no build tree to lean on and runs

```sh
python3 Util/orkige_nightly_package.py --verify <unpacked dir> --platform <p> \
        --commit <sha> --version <ordered version>
```

which asserts, and fails the job on any of them:

- every file the layout promises is present — the editor, the player, the texture
  cook tool, `VERSION`, `CHANGELOG.md`, `KNOWN-LIMITATIONS.md`, and the media
  **at the path the editor resolves** (`Contents/Resources/Media` or
  `share/orkige/Media`) carrying its flavor's shader library plus the font, water
  and decal dirs. That shader library is also the marker the editor requires
  before believing a media root at all, so a build whose payload staging never
  ran fails here rather than shipping an editor that opens a window and draws
  nothing;
- no editor settings file rode along from the build machine (window layout and
  recent projects are the developer's, not the download's);
- the binary **starts and reports its version**: the commit it reports is the
  commit the job stamped it with, and its ordered version is exactly the one the
  packaging composed. A wrong stamp, or a version the two sides disagree on, is a
  failure and not a note.

On macOS the job then reads the `signing:` line back out of the unpacked
`VERSION` and **matches it against what the job itself set up**. The packager
resolves the same credentials independently and writes that verdict; if the two
ever disagree, one of them is telling a user something untrue, and the night does
not publish. That same read-back value is what the release notes are composed
from. A notarized build additionally has `stapler validate` and `spctl` run
against the app **that came out of the archive**, which is what proves the ticket
survived the `.zip`.

The installable artifacts are checked the same way — by using them, not by
reading them:

- The **disk image is mounted**
  (`--verify-dmg <image>`, `hdiutil attach`) and has to carry the complete app
  under the same layout check the unpacked archive gets, plus the
  `/Applications` symlink without which the drag is a copy into the download
  folder rather than an install. The binary is not run from the read-only mount:
  the archive's smoke test already proved it starts.
- The **AppImage is run**, three times over, because building one proves
  nothing:
  - `--verify-appimage` executes it and holds its self-report to the commit and
    the ordered version this packaging composed, extracts it and puts it
    through the same layout check the unpacked tarball gets (plus `AppRun`, the
    desktop entry and the icon it names), and then asks the editor **inside**
    the extracted image where it resolves each of its libraries from — with the
    image's own lib directory in front of the loader's path exactly as the
    `AppRun` puts it. Every bundled library has to resolve inside the image, and
    no driver or libc may.
  - It is then started in **two clean rooms** — bare `ubuntu:24.04` and
    `debian:13-slim` containers given exactly the families the rule declares
    host-owned (`libx11-6 libx11-xcb1 libxcb1 libxcb-randr0 libvulkan1
    libstdc++6 libgcc-s1`) and nothing else. Anything the editor needs that is
    neither bundled nor on that list is a loader error there, before `main`
    runs — which is how `libatomic` was found. Each room is first asked whether
    it really lacks the bundled family, because a check run in a room that is
    not clean proves nothing; the second distribution also carries a newer
    glibc than the build image, which is the forward half of the floor.
  - And it **boots**: `xvfb` plus Mesa's lavapipe software Vulkan driver, the
    same setup the windowed desktop suites run on, driving the editor through
    90 frames and a framebuffer dump. The identity checks return before any
    window is created, so this is the step that proves the download opens
    rather than merely loading.
- The **installer is installed**. `makensis` is resolved in a step of its own
  before packaging, which fails the job with a sentence naming the missing tool
  rather than letting a night ship without an installer. After packaging, the
  installer runs silently (`/S`), and the job asserts the editor, the player,
  `VERSION` and `share/orkige/Media` landed under
  `%LOCALAPPDATA%\Programs\Orkige` and that Windows lists the build under its
  ordered version. Then the uninstaller runs silently too, and the job waits for
  the install directory and the registry entry to be gone — an installer nobody
  can remove is worse than a zip.

What that proves: the executable and everything it dynamically links load on a
machine that is not the build tree, the artifact is structurally complete, and
its identity is one value. What it does not prove: that the editor renders. The
`--version` path returns before any window is created, deliberately, so the
check needs no display. That a copied app renders is proven separately by the
`editor_bundle` ctest (`Docs/editor-distribution.md`), which detaches a copy from
the tree and drives it through boot, open, a screenshot and a Play session.

The same verifier logic is unit-tested headlessly by the
`orkige_nightly_package_selftest` ctest (label `unit`), which drives the ordered
version and its filename rendering, the identity strings, the changelog
extraction and formatting over synthetic `git log` output, the checksum sidecar
over real bytes (its `sha256sum -c` format, and the refusal when a file and its
sidecar disagree), the limitations table and its per-platform rendering, the
media staging over a synthetic build tree, the archive round-trip, and every
verdict the verifier can reach — including a stand-in binary reporting the wrong
commit or a version the packaging did not compose.

It drives the whole **signing** decision layer too, on a machine with no
certificate at all, because every one of those decisions is pure: the credential
resolution and its precedence (API key over Apple ID; a half-configured set
refused with the missing names; credentials without a certificate falling all the
way back to ad-hoc), the `codesign` / `notarytool` / `stapler` / `spctl` argv,
the redaction that keeps a credential out of an echoed command line, the strict
reading of Apple's verdict (anything unparseable is *not* accepted), a rejection
fetching the notarization log before failing, the inside-out seal order over a
synthetic bundle with the signing tool injected — including the invariant that a
run with no certificate issues exactly the four-word ad-hoc command it always
did — and which limitations record each signature state carries. What only a run
with the real credentials can prove is the rest: that the certificate imports,
that Apple accepts the submission, and that the ticket staples.

It also drives the installable artifacts as far as a platform-neutral test can:
the asset names, the volume name against the 27-character cap a disk image's
filesystem enforces, the numeric `a.b.c.d` the Windows VERSIONINFO resource
accepts, the `makensis` argv, the Linux bundle's whole decision layer — which
library is bundled and which family keeps it out (asserted library by library,
in both directions), the three shapes of `ldd` output and the plan that comes
out of them including a dependency nothing resolves, the glibc floor ordered as
versions rather than strings, the tool's resolution precedence on a machine
that has no `appimagetool` at all, the `appimagetool` argv, the `AppRun` and the
desktop entry, the icon actually rendering, the honest refusal when the tool is
absent and the two refusals a check pointed at a missing or non-executable
image gives — and the installer script's own properties — the
per-user install root, `RequestExecutionLevel user`, the absence of any `HKLM`
write (which would demand elevation), the Start-menu shortcut, the uninstall
registry record, and the fact that every `/D` define the packager passes is one
the script requires and vice versa. `makensis` runs on no machine this suite
runs on, so compiling the installer is the pipeline's job; the two artifacts
whose tools are platform-bound get a ctest each (label `unit`), and both **skip
with 77** where the tool is absent rather than passing without having checked
anything:

- `orkige_nightly_dmg_selftest` builds and mounts a real disk image over a
  synthetic app. It needs `hdiutil`, which exists on exactly one platform.
- `orkige_nightly_appimage_selftest` assembles and packs a real AppImage over a
  synthetic AppDir whose stand-in editor is a genuine dynamic executable off the
  machine — so the inclusion rule runs against a closure the loader really
  resolved rather than a fixture — then unpacks what was packed, through the
  FUSE-free path, and asserts where the editor inside finds each library: the
  bundled names from inside the image, the driver/libc/toolchain families from
  the host. It needs Linux and `appimagetool`.

It drives the **dated tag rule** as pure data — which tags are this channel's at
all, over a realistic listing carrying the rolling `nightly`, a stable `v2.0.0`,
`nightly-2026`, `nightly-20260731-rc1`, a date that names no real day and tags a
person made. The shape is the whole test, so a look-alike somebody else tagged is
never read as a night that published.

It also drives the **release notes** an updater reads: the publish job's own
shell block is lifted out of the workflow and run against stubbed job outputs,
asserting that both markers carry the right values, that an archive which
arrived is named while a failed platform is called out with its job result, and
that the notes point at the `.sha256` sidecar. A marker exists in exactly one
place, and this is the check that it exists where a client looks. The same block
is run once per macOS signature state, because the sentence a reader is given
has to be the one that applies: a notarized download described as unsigned sends
people through steps they do not need, an unsigned one described as notarized
leaves them stuck, and a macOS job that never reported reads as the unsigned
wording rather than as a claim nobody made.

The **retention decision** is driven as pure data first — which rows keep what,
over asset names composed by the very functions that name a real artifact, so
the reader and the writer of the grammar cannot drift: a platform that built
keeps nothing, a platform that did not keeps its whole last build with its
sidecars, an asset published without one travels alone, a predecessor carrying
two builds of one platform yields the newest, and neither a device payload nor
the full-history document is ever claimed for a platform. The **step** that
carries it out is then lifted out of the workflow and run against a `gh` that
serves a fixture release: it must fetch exactly the planned files, end with a
plan describing what arrived, and — when a download fails or there is no
predecessor at all — keep nothing rather than name a file that is not there.

The step that **creates the releases** is driven the same way, because a mistake
in it deletes something on a real repository: it is lifted out of the workflow
and run against a `gh` that records the exact argument vector it was handed
instead of talking to GitHub. What is asserted is the sequence — that the
rolling release is replaced, that the dated one is added, that with nothing kept
the two carry the identical asset list while a kept asset reaches **the rolling
release alone**, that each is described by its own notes file, that a build with
no composed date still publishes the rolling one and says it left no archive
entry, and that **the only tags a delete is ever handed are the two this night
republishes**, so no earlier dated release can be reached from here.

## Where the artifacts go

Three places, all conservative:

- **Workflow-run artifacts** (`binaries-macos`, `binaries-linux`,
  `binaries-windows`), kept 14 days. This is how to get a build today.
- **A rolling release tagged `nightly`**, published as a **prerelease** —
  which is what it is: the tip of `main`, built tonight, superseded tomorrow.
  Each night replaces it wholesale and the `nightly` tag moves with it, so the
  asset URLs are identical from one night to the next. That stability is the
  contract: it is what makes a URL quotable in a document and fetchable by an
  updater.
- **A dated release tagged `nightly-YYYYMMDD`**, the same prerelease under a
  tag that never moves and that no later night deletes. This is the archive:
  somebody who wants the build from before a regression finds it on the
  releases page and downloads it. There is no in-app revert — the releases page
  is the whole mechanism. **Every entry is kept.** A night publishes only where
  the tree actually moved, so the archive grows with real work rather than with
  the calendar, and release storage on a public repository costs nothing.

Both carry **tonight's produce**, uploaded twice, and describe one build.
GitHub has no way to share an uploaded asset between two releases, and a dated
release that merely pointed at the rolling one would break the moment the
rolling one is replaced; release storage on a public repository is free, so the
bytes are simply uploaded again. They differ in exactly one thing, and it is
the subject of the next section: the rolling release additionally carries what
a platform that failed tonight keeps from its predecessor.

The date is the one the build's ordered version is composed from, so the tag and
the title name one day by construction — `--identity` prints the version, its
filename token and the tag together, and the gate hands all three to the publish
job.

**Rerunning a day replaces that day's entry.** The dated release is deleted and
recreated exactly like the rolling one, so a second run never leaves two entries
for one date.

The only releases the publish job deletes are the two it is about to recreate:
the rolling `nightly` and this day's own dated entry. Nothing else is ever
named, so an earlier night's download cannot be removed by a later one.

The publish job runs whenever the gate was green, even if a platform's build
failed, so a partial night is legible instead of looking complete. Zero archives
is a failure — there is nothing to publish.

Each night's assets are the two macOS ones (`.dmg`, `.zip`), the two Linux ones
(`.AppImage`, `.tar.gz`), the two Windows ones (`-setup.exe`, `.zip`), the full-history
`CHANGELOG.md`, and a `.sha256` file beside each. Before they are attached,
every one of them is checked against the
sidecar that travelled with it (`--verify-checksums`), so bytes that changed on
the way fail the publish rather than being served under a digest that fits
neither.

## A platform that fails keeps its last good assets

The rolling release is replaced **wholesale**, which is what keeps its download
URLs identical from one night to the next. Wholesale also means whatever it does
not carry is gone: one lost runner and that platform has nothing to download at
all until the next green night.

So the rolling release **retains**. A platform that produced no fresh artifact
tonight keeps the assets it published last — both of them, with their `.sha256`
sidecars, fetched back out of the release those bytes are still attached to and
uploaded unmodified. Its row in the notes table then names that build's files
and **the version they carry**, beside tonight's job result:

```
| Windows (x64) | `Orkige-windows-2.0.0-nightly.20260730_aaaaaaaaa-setup.exe` |
`Orkige-windows-2.0.0-nightly.20260730_aaaaaaaaa.zip` |
kept `2.0.0-nightly.20260730+aaaaaaaaa` - tonight's build failure |
```

Four rules bound it:

- **Only the rolling release retains.** The dated `nightly-YYYYMMDD` release is
  the archive of one night, so it carries tonight's produce alone and its own
  notes say "not produced" wherever tonight's build did — an asset from another
  night inside it would claim that night built something it did not. The two
  releases are the only place the notes differ, and each is composed from the
  same block with and without the retention plan.
- **The `orkige-nightly-commit:` marker stays tonight's commit.** The gate reads
  it to decide whether `main` moved since the last published nightly, so a kept
  asset must never freeze it; what was kept is an asset decision and never an
  identity.
- **A kept asset travels with the sidecar it was published with**, byte for
  byte. It is verified on arrival exactly like a fresh one, so bytes that
  changed on the way back fail the publish rather than being served again under
  a digest that fits neither.
- **The desktop editors are what is retained.** A device player payload is
  resolved by a released editor off the *dated* release its own version names
  (`EditorPayloads::payloadReleaseTag`), which no later night replaces, so the
  payload of the night that built it is still exactly where that editor looks. A
  copy in the rolling release would be a file nothing resolves, and "no player
  was published for this build" stays both true and what the editor itself says.

An **updater sees no change**: it picks its platform's asset by the token of the
version in the release body, and a kept asset carries its own older token — so a
retained row is not offered as an update, which is right, because it is not one.
Retention is for the download page and the person reading it.

Which platform keeps which files, under which version, is decided by
`orkige_nightly_package.py --plan-retention` — a pure function of two lists of
asset names (what arrived tonight, what the predecessor carried) that prints the
plan as `key=value` lines. The workflow carries it out and then asks again over
what actually **arrived**, so a download that failed leaves the row reading "not
produced" instead of naming a file the release does not carry.

What each platform's download is worth — and where it still warns its user —
is the list below.

## What a downloaded build cannot do yet

Every archive carries a generated `KNOWN-LIMITATIONS.md` listing exactly the gaps
that apply to its platform — and, on macOS, to what its signature is actually
worth. The list is a table of records in `Util/orkige_nightly_package.py`, so
closing a gap is deleting one record — the selftest asserts the rendered document
lists exactly the records that apply, whatever they are, which keeps it honest in
both directions.

The trust gaps, per platform:

- **Windows builds are unsigned**, and the installer is unsigned with them. It
  draws SmartScreen's loudest prompt, the full-screen "Windows protected your
  PC" whose default button is *Don't run*, because it is a program asking to
  install software rather than a file being unpacked; "More info" > "Run anyway"
  gets past it, and the portable `.zip` beside it needs no such confirmation at
  all. Closing this needs a Windows code signing certificate from a certificate
  authority, which an Apple Developer membership does not cover.
- **macOS builds are Developer ID signed, notarized and stapled** when the
  signing credentials are reachable — the app and the disk image each carry
  their own ticket, so both open with no security prompt and no quarantine flag
  to clear. A build without those credentials (a fork, a pull request, a clone
  with nothing configured) is ad-hoc signed instead and carries the record
  saying so, with the steps its user needs; one with a certificate but no
  notarization credentials carries the record for *that*. See
  [macOS signing](#macos-signing-notarization-and-stapling).
- **A download packages for the desktop and the browser out of the box** —
  Build > Export packages a game with the engine payload the app carries inside
  itself, which is this platform's player and the browser one. A PHONE runs
  another architecture's binary, so that player is a download of its own:
  switching the platform on under Settings > Build Targets fetches the player
  published for this exact build ([device payloads](device-payloads.md)).
  Android is not among them yet, and a signed iOS DEVICE build needs the device
  player, which only a build from the engine source tree produces. A build whose
  browser payload did not arrive carries the desktop-only record instead, and
  its `VERSION` reads `web-export: absent`
  ([the browser player payload](#the-browser-player-payload)).
- **Compiled C++ game code needs a toolchain** — CMake, Ninja, a C++20 compiler
  and an engine build tree. Game behaviour written in Lua needs none of that,
  which is the whole point of the distinction.

Per platform: on Linux the `.tar.gz` needs the distribution's own X/Wayland,
GL/Vulkan, audio and D-Bus libraries present, the Xt/Athena family included (the
file names the packages), while the `.AppImage` carries those and needs only the
machine's GPU driver and a glibc at least as new as the recorded floor
([the Linux single-file bundle](#the-linux-single-file-bundle)); Windows needs the
Visual C++ runtime resolvable (the packaging ships it app-local when the build
machine had it, and `VERSION` records whether it did).

## Running the packaging by hand

The packager takes a build tree that already exists; it never builds.

```sh
cmake --preset macos-release -DORKIGE_BUILD_COMMIT=$(git rev-parse --short=9 HEAD) \
                             -DORKIGE_BUILD_DATE=$(date -u +%F)
cmake --build --preset macos-release --target orkige_editor_bundle
python3 Util/orkige_nightly_package.py --platform macos \
        --build-dir build/macos-release --commit $(git rev-parse HEAD) \
        --since <previous nightly's commit> \
        --output /tmp/nightly-out
```

A hand run with a `build/web-release` tree beside it stages the browser payload
from there without being asked; point `--web-build` (or `ORKIGE_WEB_BUILD`) at
another tree, or `--web-payload` at a directory `--stage-web-payload` composed
elsewhere. With neither, the packaging warns once and the artifact records
`web-export: absent`.

The build target is `orkige_editor_bundle`, which stages the payload a copied app
resolves its media from; a scoped editor build would package an app with none of
it, and the verification below would refuse it. With no `--version` the packager
composes one from `--commit` and `--date` (today by default) — the same value the
pipeline passes explicitly; with no `--since` the changelog falls back to a
bounded window and says so.

That one run writes both of the platform's assets. The `.dmg` needs `hdiutil`,
which is part of macOS; the Windows installer needs `makensis` on `PATH`, and
the Linux AppImage needs `appimagetool`. Without either the packager says so and
produces the portable archive alone:

```sh
python3 Util/orkige_nightly_package.py --platform linux \
        --build-dir build/linux-release-next --commit $(git rev-parse HEAD) \
        --appimagetool ~/bin/appimagetool --output /tmp/nightly-out
# ... and check it by using it: run, extract, and resolve its libraries
python3 Util/orkige_nightly_package.py \
        --verify-appimage /tmp/nightly-out/Orkige-linux-*.AppImage \
        --commit $(git rev-parse HEAD)
```

A hand run on macOS is **ad-hoc signed** unless a certificate is pointed at, and
says so. To sign one for real, name the identity and let the notarization
credentials come from the environment (never from a command line):

```sh
export ORKIGE_MACOS_SIGNING_IDENTITY="Developer ID Application: … (TEAMID)"
export ORKIGE_NOTARY_KEY=~/private/AuthKey_XXXXXXXX.p8
export ORKIGE_NOTARY_KEY_ID=XXXXXXXX ORKIGE_NOTARY_ISSUER_ID=…
python3 Util/orkige_nightly_package.py --platform macos …
```

`--ad-hoc-sign` forces the ad-hoc path on a machine that does have a certificate,
for a packaging run that must not reach Apple.

Then check the result the way CI does:

```sh
mkdir -p /tmp/smoke && ditto -x -k /tmp/nightly-out/Orkige-macos-*.zip /tmp/smoke
python3 Util/orkige_nightly_package.py --verify /tmp/smoke --platform macos \
        --commit $(git rev-parse HEAD)
# and the disk image, by mounting it
python3 Util/orkige_nightly_package.py --verify-dmg /tmp/nightly-out/Orkige-macos-*.dmg
```

Four more modes serve the pipeline, and each one works by hand:

```sh
# the ordered version, its filename rendering and the dated release's tag,
# as key=value lines
python3 Util/orkige_nightly_package.py --identity --commit <sha>
# the changelog section for a range
python3 Util/orkige_nightly_package.py --changelog --commit HEAD --since <sha>
# the FULL history, every commit grouped by the day it landed. --published-tag
# counts one more day as published (the publish job hands in tonight's, whose
# tag does not exist yet when this asset is written)
python3 Util/orkige_nightly_package.py --history --history-out CHANGELOG.md
python3 Util/orkige_nightly_package.py --history --commit <sha> \
  --published-tag nightly-$(date -u +%Y%m%d)
# every archive in a directory against its .sha256 file
python3 Util/orkige_nightly_package.py --verify-checksums <dir>
```

`--selftest` runs the headless self-checks without needing any build tree at all;
`--selftest-dmg` builds and mounts a real disk image over a synthetic app
(exiting 77 where `hdiutil` does not exist), and `--selftest-appimage` packs and
unpacks a real AppImage over a synthetic AppDir (exiting 77 off Linux or without
`appimagetool`).
