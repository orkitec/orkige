# Nightly editor builds

Once a night, CI packages the Orkige editor for macOS, Linux and Windows and
publishes the results as two prereleases: a rolling one whose download URLs
never move, and a dated one that stays. The point is a download
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
— the axis a daily channel makes obvious. Each group is headed by the ordered
version identity a build of that day carries, composed by the same
`nightly_version` the pipeline names its artifacts with, from the day and that
day's newest commit; so a heading here and the version a binary reports are the
same string by construction. A day whose date composes no version is headed by
its date.

The document says what it is missing, rather than presenting whatever it found
as the whole record:

- a **shallow checkout** lists only the commits that clone carries and says so,
  naming the count. Both jobs that render it check out with `fetch-depth: 0` for
  exactly this reason;
- **no history at all** says nothing is listed and where the document normally
  comes from.

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
   `Orkige-windows-<token>-setup.exe`) are what a **person** downloads from the
   release page. A platform whose build failed has **no asset**, and the notes
   table names it with that job's result: a client asks "is there a build for
   me", and absence is the answer.
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
  served on the day, for as long as it stays in the fourteen-day archive.
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

It drives the **dated archive's selection rule** as pure data — which tags are
candidates at all, over a realistic listing carrying the rolling `nightly`, a
stable `v2.0.0`, `nightly-2026`, `nightly-20260731-rc1`, a date that names no
real day and tags a person made; which survive a keep count of 14 and in what
order the rest are deleted; and that tonight's own tag survives a keep count of
zero, because it is protected explicitly rather than by being newest.

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

The two steps that **create and prune the releases** are driven the same way,
because a mistake in either deletes something on a real repository: they are
lifted out of the workflow and run against a `gh` that records the exact
argument vector it was handed instead of talking to GitHub. What is asserted is
the sequence — that the rolling release is replaced, that the dated one is
added carrying the identical asset list, that a build with no composed date
still publishes the rolling one and says it left no archive entry, and that
**no tag outside the dated shape is ever passed to a delete**, tonight's own
included. The failure paths are asserted as paths, not as prose: a listing that
cannot be fetched and a deletion GitHub refuses both leave the step at exit 0
with an annotation naming what happened.

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
  is the whole mechanism.

Both carry the **same assets**, uploaded twice from one list, and the same
notes. GitHub has no way to share an uploaded asset between two releases, and a
dated release that merely pointed at the rolling one would break the moment the
rolling one is replaced; release storage on a public repository is free, so the
bytes are simply uploaded again.

The date is the one the build's ordered version is composed from, so the tag and
the title name one day by construction — `--identity` prints the version, its
filename token and the tag together, and the gate hands all three to the publish
job.

**Rerunning a day replaces that day's entry.** The dated release is deleted and
recreated exactly like the rolling one, so a second run never leaves two entries
for one date.

### Pruning the archive

Only the newest **14** dated releases are kept; the older ones are deleted, tag
and all. Two weeks is what the workflow-run artifacts keep too, so "how far back
can I go" has one answer on both surfaces.

Which tags are even candidates is a decision the shell does not make. The
packager's `prune_dated_releases` makes it and only ever names a tag matching
exactly `nightly-YYYYMMDD` on a real calendar day. The rolling `nightly`, a
stable release tag like `v2.0.0`, a tag that merely begins like ours
(`nightly-2026`, `nightly-20260731-rc1`) and anything a person made are not
candidates and no keep count can turn them into one. Tonight's own tag is passed
in as protected as well, so it survives independently of the ordering.

Pruning **never fails the night**. The artifacts are the deliverable and they
are already published by the time it runs, so a listing that cannot be fetched
or a deletion GitHub refuses is a loud annotation and an exit 0 — and every
deletion is named in the job log and the summary, so the output says exactly
what was removed.

The publish job runs whenever the gate was green, even if a platform's build
failed: the release notes list every platform with its archive or the words "not
produced" plus that job's result, so a partial night is legible instead of
looking complete. Zero archives is a failure — there is nothing to publish.

Each night's assets are the two macOS ones (`.dmg`, `.zip`), the Linux
`.tar.gz`, the two Windows ones (`-setup.exe`, `.zip`), the full-history
`CHANGELOG.md`, and a `.sha256` file beside each. Before they are attached,
every one of them is checked against the
sidecar that travelled with it (`--verify-checksums`), so bytes that changed on
the way fail the publish rather than being served under a digest that fits
neither.

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

Five more modes serve the pipeline, and each one works by hand:

```sh
# the ordered version, its filename rendering and the dated release's tag,
# as key=value lines
python3 Util/orkige_nightly_package.py --identity --commit <sha>
# the changelog section for a range
python3 Util/orkige_nightly_package.py --changelog --commit HEAD --since <sha>
# the FULL history, every commit grouped by the day it landed
python3 Util/orkige_nightly_package.py --history --history-out CHANGELOG.md
# every archive in a directory against its .sha256 file
python3 Util/orkige_nightly_package.py --verify-checksums <dir>
# which dated releases are past the keep count, read from a list of tags
gh release list --limit 200 --json tagName --jq '.[].tagName' \
  | python3 Util/orkige_nightly_package.py --prune-tags - --protect nightly-$(date -u +%Y%m%d)
```

That last one only ever *prints* tags; deleting them is the workflow's step, so
running it by hand is a dry run of the decision by construction.

`--selftest` runs the headless self-checks without needing any build tree at all,
and `--selftest-dmg` builds and mounts a real disk image over a synthetic app
(exiting 77 where `hdiutil` does not exist).
