# Nightly editor builds

Once a night, CI packages the Orkige editor for macOS, Linux and Windows and
publishes the results as a rolling draft prerelease. The point is a download
instead of a build: a C++ toolchain is needed only to write native game code, not
to open the editor and make a game in Lua.

The pipeline lives in `.github/workflows/nightly.yml` beside the other scheduled
work (the soak, Valgrind and fuzz watches — one Actions run a night). The
packaging itself is `Util/orkige_nightly_package.py`, which reuses the project
exporter's build-tree plumbing (`Util/orkige_export.py`: media resolution, the
macOS dylib closure) rather than restating it.

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

Each platform ships the artifact a person on it expects, and the two desktop
platforms with an installable convention ship a second, portable one beside it:

| Platform | Runner | Preset | Install | Portable |
| --- | --- | --- | --- | --- |
| macOS (Apple silicon) | `macos-15` | `macos-release` | `Orkige-macos-<version>.dmg` | `Orkige-macos-<version>.zip` |
| Linux (x86_64) | `ubuntu-latest` | `linux-release-next` | — | `Orkige-linux-<version>.tar.gz` |
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
  Installed apps lists it like any other program.
- The **portable** artifact is the `.zip` / `.tar.gz`: nothing to mount, nothing
  to run, unpack anywhere. It is also the shape an updater consumes, because
  replacing files in place needs neither a mounted image nor an installer run.
  Linux ships this alone — a distribution's own package formats are not
  interchangeable, and a tarball is what all of them can unpack.

The archive has one top-level directory and the same shape everywhere:

```
Orkige-<platform>-<version>/
    VERSION                 the build identity, one `key: value` per line
    CHANGELOG.md            what landed since the previous nightly
    KNOWN-LIMITATIONS.md    what this build cannot do yet
    <the editor>            Orkige.app, or orkige_editor[.exe]
    <the player>            beside the editor, for Play
    <the texture cook tool> beside the player, for the export cook
    <resources>             the engine media, at the path the editor resolves
```

The resources sit exactly where the editor's own resource locator looks — that
locator resolves bundle-first, so the archive layout is not a convention of the
packaging but the editor's own contract (`Docs/editor-distribution.md`):

- macOS: inside the bundle — `Contents/MacOS` for the executables,
  `Contents/Resources/Media` for the media. `VERSION`, `CHANGELOG.md` and
  `KNOWN-LIMITATIONS.md` are repeated at the archive root (and so appear on the
  mounted disk image) so they are readable before installing. The bundle is
  zipped with `ditto`, which preserves its symlinks and executable bits, and is
  ad-hoc re-signed after staging so its resource seal covers everything the
  packaging added — ad-hoc, so it needs no certificate and confers no trust. The
  disk image is then built with `hdiutil` from that same sealed staging, with
  the `Applications` symlink added for the duration of the call: the app is well
  over a hundred megabytes, and a second copy of it would be both slow and a
  chance for the two artifacts to diverge.
- Linux and Windows: the executables at the top level and the resources under
  `share/orkige/` beside them (`share/orkige/Media/…` plus the editor's icon and
  mono fonts), which is what the locator reads relative to `SDL_GetBasePath`.

The build stages that same payload into the build tree, so the two are one
layout: the packaging targets `orkige_editor_bundle` (which depends on the
editor, the player and the texture cook tool) rather than the bare editor,
because a scoped `--target orkige_editor` leaves the staging unrun.

Platform-specific handling worth knowing:

- **Linux** links the whole engine statically but still loads the
  distribution's X11/Wayland, OpenGL/Vulkan, ALSA/PulseAudio and D-Bus
  libraries. The archive names the packages. A single-file bundle that carries
  those too is the eventual answer and is deliberately not attempted here.
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
  updater polls (below).

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

## What an updater reads

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
   section of `body`, the same text the artifact's `CHANGELOG.md` carries.
5. Pick its platform's asset by name. An **updater takes the portable one** —
   `Orkige-macos-<token>.zip`, `Orkige-linux-<token>.tar.gz`,
   `Orkige-windows-<token>.zip`, where `<token>` is the version's filename
   rendering — because swapping files in place needs neither a mounted image nor
   an installer run. The installable assets (`Orkige-macos-<token>.dmg`,
   `Orkige-windows-<token>-setup.exe`) are what a **person** downloads from the
   release page. A platform whose build failed has **no asset**, and the notes
   table names it with that job's result: a client asks "is there a build for
   me", and absence is the answer.
6. Fetch the `<archive>.sha256` asset beside it and **verify the digest before
   trusting a single byte** of the archive. That sidecar is the download's only
   integrity story; the publish job checks every archive against the sidecar
   that travelled with it, so the two agree at the moment they are served.

Two caveats, stated plainly:

- **A draft release's assets are not anonymously readable.** The `nightly`
  release is a draft while the archives are unsigned, and a draft is reachable
  only with a token that has write access — so nothing can poll it until the
  release goes public. That is one more reason the signing gap comes before an
  in-editor updater.
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

The installable artifacts are checked the same way — by using them, not by
reading them:

- The **disk image is mounted**
  (`--verify-dmg <image>`, `hdiutil attach`) and has to carry the complete app
  under the same layout check the unpacked archive gets, plus the
  `/Applications` symlink without which the drag is a copy into the download
  folder rather than an install. The binary is not run from the read-only mount:
  the archive's smoke test already proved it starts.
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

It also drives the installable artifacts as far as a platform-neutral test can:
the asset names, the volume name against the 27-character cap a disk image's
filesystem enforces, the numeric `a.b.c.d` the Windows VERSIONINFO resource
accepts, the `makensis` argv, and the installer script's own properties — the
per-user install root, `RequestExecutionLevel user`, the absence of any `HKLM`
write (which would demand elevation), the Start-menu shortcut, the uninstall
registry record, and the fact that every `/D` define the packager passes is one
the script requires and vice versa. `makensis` runs on no machine this suite
runs on, so compiling the installer is the pipeline's job; the disk image, whose
tool exists on exactly one platform, gets its own
`orkige_nightly_dmg_selftest` ctest (label `unit`) that builds and mounts a real
image over a synthetic app and **skips with 77** where `hdiutil` does not exist,
rather than passing without having checked anything.

It also drives the **release notes** an updater reads: the publish job's own
shell block is lifted out of the workflow and run against stubbed job outputs,
asserting that both markers carry the right values, that an archive which
arrived is named while a failed platform is called out with its job result, and
that the notes point at the `.sha256` sidecar. A marker exists in exactly one
place, and this is the check that it exists where a client looks.

## Where the artifacts go

Two places, both conservative:

- **Workflow-run artifacts** (`binaries-macos`, `binaries-linux`,
  `binaries-windows`), kept 14 days. This is how to get a build today.
- **A rolling release tagged `nightly`**, kept a **draft prerelease**. A draft is
  visible only to accounts with write access and creates no git tag, so nothing
  half-working is presented as ready. Each night replaces the previous draft
  wholesale rather than accumulating releases nobody prunes.

The publish job runs whenever the gate was green, even if a platform's build
failed: the release notes list every platform with its archive or the words "not
produced" plus that job's result, so a partial night is legible instead of
looking complete. Zero archives is a failure — there is nothing to publish.

Each night's assets are the two macOS ones (`.dmg`, `.zip`), the Linux
`.tar.gz`, the two Windows ones (`-setup.exe`, `.zip`) and a `.sha256` file
beside each. Before they are attached, every one of them is checked against the
sidecar that travelled with it (`--verify-checksums`), so bytes that changed on
the way fail the publish rather than being served under a digest that fits
neither.

Making the release public is one change (dropping `--draft` from the
`gh release create` call in the publish job). What has to be true first is the
list below — above all, the archives have to be signed, because every platform
currently warns the user that they are not.

## What a downloaded build cannot do yet

Every archive carries a generated `KNOWN-LIMITATIONS.md` listing exactly the gaps
that apply to its platform. The list is a table of records in
`Util/orkige_nightly_package.py`, so closing a gap is deleting one record — the
selftest asserts the rendered document lists exactly the records that apply,
whatever they are, which keeps it honest in both directions.

Today the list is headed by the one that matters most:

- **The builds are unsigned**, and the installable containers are unsigned with
  them. macOS reports the app as unopenable until the download quarantine flag
  is removed (install first, then `xattr -dr com.apple.quarantine
  /Applications/Orkige.app`, or right-click > Open); an unsigned `.dmg` is
  blocked exactly like an unsigned `.zip`, because the image is an install
  shape, not a trust shape — what changes that is a notarization ticket, which
  staples onto the `.dmg` directly. On Windows the installer draws SmartScreen's
  loudest prompt, the full-screen "Windows protected your PC" whose default
  button is *Don't run*, because it is a program asking to install software
  rather than a file being unpacked; "More info" > "Run anyway" gets past it,
  and the portable `.zip` beside it needs no such confirmation at all. The
  limitations file spells out every step. This is why the release stays a draft.
- **Exporting a game needs the engine repository** — Build > Export copies out of
  a build tree and runs `Util/orkige_export.py`, so it needs that tree and
  python3.
- **Importing a Lottie animation needs python3 and the repository** — that cook
  runs `Util/cook_vector_anim.py`. Importing an `.svg` needs neither; that cook
  runs inside the editor.
- **Compiled C++ game code needs a toolchain** — CMake, Ninja, a C++20 compiler
  and an engine build tree. Game behaviour written in Lua needs none of that,
  which is the whole point of the distinction.

Per platform: Linux needs the distribution's X/Wayland, GL/Vulkan, audio and
D-Bus libraries present (the file names the packages), and Windows needs the
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

The build target is `orkige_editor_bundle`, which stages the payload a copied app
resolves its media from; a scoped editor build would package an app with none of
it, and the verification below would refuse it. With no `--version` the packager
composes one from `--commit` and `--date` (today by default) — the same value the
pipeline passes explicitly; with no `--since` the changelog falls back to a
bounded window and says so.

That one run writes both of the platform's assets. The `.dmg` needs `hdiutil`,
which is part of macOS; the Windows installer needs `makensis` on `PATH`, and
without it the packager says so and produces the `.zip` alone.

Then check the result the way CI does:

```sh
mkdir -p /tmp/smoke && ditto -x -k /tmp/nightly-out/Orkige-macos-*.zip /tmp/smoke
python3 Util/orkige_nightly_package.py --verify /tmp/smoke --platform macos \
        --commit $(git rev-parse HEAD)
# and the disk image, by mounting it
python3 Util/orkige_nightly_package.py --verify-dmg /tmp/nightly-out/Orkige-macos-*.dmg
```

Three more modes serve the pipeline, and each one works by hand:

```sh
# the ordered version and its filename rendering, as key=value lines
python3 Util/orkige_nightly_package.py --identity --commit <sha>
# the changelog section for a range
python3 Util/orkige_nightly_package.py --changelog --commit HEAD --since <sha>
# every archive in a directory against its .sha256 file
python3 Util/orkige_nightly_package.py --verify-checksums <dir>
```

`--selftest` runs the headless self-checks without needing any build tree at all,
and `--selftest-dmg` builds and mounts a real disk image over a synthetic app
(exiting 77 where `hdiutil` does not exist).
