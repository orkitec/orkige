# Nightly editor builds

Once a night, CI packages the Orkige editor for macOS, Linux and Windows and
publishes the archives as a rolling draft prerelease. The point is a download
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

| Platform | Runner | Preset | Archive |
| --- | --- | --- | --- |
| macOS (Apple silicon) | `macos-15` | `macos-release` | `Orkige-macos-<version>.zip` |
| Linux (x86_64) | `ubuntu-latest` | `linux-release-next` | `Orkige-linux-<version>.tar.gz` |
| Windows (x64) | `windows-latest` | `windows-release` | `Orkige-windows-<version>.zip` |

All three are the default Ogre-Next render flavor in Release, and `<version>` is
the ordered version in its filename rendering (below). Each archive has a
`.sha256` file beside it. The archive has one top-level directory and the same
shape everywhere:

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
  `KNOWN-LIMITATIONS.md` are repeated at the archive root so they are readable
  before installing. The bundle is zipped with `ditto`, which preserves its
  symlinks and executable bits, and is ad-hoc re-signed after staging so its
  resource seal covers everything the packaging added — ad-hoc, so it needs no
  certificate and confers no trust.
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
  the redistributable rather than leaving a silent launch failure.
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
- **the manifest** an updater polls (below).

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

## The published manifest

`nightly-manifest.json` is a release asset of its own: one small document naming
the current version and every platform's archive.

```json
{
  "schema": 1,
  "product": "orkige editor",
  "channel": "nightly",
  "version": "2.0.0-nightly.20260730+dea551f9e",
  "baseVersion": "2.0.0",
  "date": "2026-07-30",
  "commit": "dea551f9e0e0f1a2b3c4d5e6f708192a3b4c5d6e",
  "changelog": "## Changes since `bb9c73f81`\n\n- …",
  "platforms": {
    "macos": {
      "filename": "Orkige-macos-2.0.0-nightly.20260730_dea551f9e.zip",
      "size": 78123456,
      "sha256": "5f2b…",
      "url": "https://github.com/orkitec/orkige/releases/download/nightly/Orkige-macos-2.0.0-nightly.20260730_dea551f9e.zip"
    }
  }
}
```

| Field | Meaning |
| --- | --- |
| `schema` | the document version; a client refuses a number it does not know |
| `product` / `channel` | which artifact family and which stream this is |
| `version` | the ordered version — what a client compares |
| `baseVersion` | the engine version the build came from |
| `date` / `commit` | the build date (UTC) and the full source commit |
| `changelog` | the changelog section as markdown text |
| `platforms` | one entry per platform: `filename`, `size` in bytes, `sha256`, `url` |

A platform whose build failed is **absent** from `platforms` rather than present
and empty — a client asks "is there a build for me", and absence is the honest
answer (the release notes name the failed job). The digests are of the real bytes
and are cross-checked against the `.sha256` file that travelled beside each
archive; a disagreement fails the publish job rather than publishing a digest
that does not match its file.

**How a client consumes it** — the contract an updater implements against:

1. fetch `nightly-manifest.json` from the release, and refuse a `schema` it does
   not understand;
2. compare `version` with its own (`editorBuildVersion()`) through
   `VersionOrder::isUpdate` — never by string equality, never by date arithmetic
   of its own;
3. show `changelog` if it offers the update;
4. download the entry for its own platform, and **verify the SHA-256 before
   trusting a single byte of it**;
5. treat `VO_SAME` and `VO_INCOMPARABLE` as "no update" — a rebuild of today's
   tree and an unstamped local build are both "nothing to do".

The `nightly` release is a **draft** while the archives are unsigned, and a
draft's assets are only reachable with a token that has write access. The
manifest's URLs are the public download URLs the release will serve once it is
public; an updater cannot poll a draft anonymously, which is one more reason the
signing gap comes before an in-editor updater.

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
extraction and formatting over synthetic `git log` output, the manifest schema
and its digests over real bytes, the limitations table and its per-platform
rendering, the media staging over a synthetic build tree, the archive
round-trip, and every verdict the verifier can reach — including a stand-in
binary reporting the wrong commit or a version the packaging did not compose.

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

Each night's assets are the three archives, a `.sha256` file beside each of
them, and `nightly-manifest.json`.

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

- **The builds are unsigned.** macOS reports the app as unopenable until the
  download quarantine flag is removed (`xattr -dr com.apple.quarantine
  Orkige.app`, or right-click > Open); Windows SmartScreen warns until "More
  info" > "Run anyway". The limitations file spells out both. This is why the
  release stays a draft.
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

Then check the result the way CI does:

```sh
mkdir -p /tmp/smoke && ditto -x -k /tmp/nightly-out/Orkige-macos-*.zip /tmp/smoke
python3 Util/orkige_nightly_package.py --verify /tmp/smoke --platform macos \
        --commit $(git rev-parse HEAD)
```

Three more modes serve the pipeline, and each one works by hand:

```sh
# the ordered version and its filename rendering, as key=value lines
python3 Util/orkige_nightly_package.py --identity --commit <sha>
# the changelog section for a range
python3 Util/orkige_nightly_package.py --changelog --commit HEAD --since <sha>
# the manifest for a directory of release assets
python3 Util/orkige_nightly_package.py --manifest <dir> --commit <sha> \
        --asset-base-url https://example.invalid/releases/download/nightly
```

`--selftest` runs the headless self-checks without needing any build tree at all.
