# Keeping the editor current

The editor updates itself the way desktop applications do: it looks once a day,
says when something newer exists, downloads it in the background if that is what
the setting asks for, and installs it when the editor next starts. Nothing is
replaced while the editor is running, and nothing older than what is installed
is ever offered.

There is deliberately nothing novel here. What follows is where each of those
sentences lives in the code, and what it refuses to do.

## The setting

Three states, in **View Settings > Software Updates**, persisted with the rest
of the editor's settings (`orkige_editor_view.ini`, key `update_policy`):

| Setting | What it does |
| --- | --- |
| `off` — *Never check* | no request is made on the editor's own initiative |
| `notify` — *Check and tell me* | checks, and says when a newer version exists |
| `download` — *Check and download in the background* | checks, and fetches it |

**`notify` is the default.** A published build is measured in hundreds of
megabytes and changes daily, which is not something to start pulling down a
connection that was never offered — a metered link, a phone hotspot, a
conference. The middle setting still puts the update in front of the user on the
first launch of the day; turning on the third one is a decision somebody makes
once.

**Automated runs are exempt from all three.** The `automatedRun` probe is an
absolute veto: no HTTP client is created at all, no request is submitted, no
stamp is written, no staging directory appears. A scripted run behaves
identically to one on a machine with no network. The veto outranks even an
explicit request, which is why it is checked first in `decideUpdateCheck`.

## The cadence

Once per launch, and at most once every 24 hours. The timestamp of the last
check lives in the editor's writable application-support directory
(`updates/update-state.ini`, beside the settings), never inside the app — a
distributed bundle is read-only, and writing into it would invalidate its
signature.

A published build changes at most once a day, so a shorter interval cannot find
anything a longer one misses. Two details the gate handles rather than ignores:

- **A stamp from the future** (a clock that moved backwards) counts as "long
  enough ago". A machine whose date is wrong must not be locked out of checking
  until it catches up.
- **The stamp records that a check RAN**, not that it found something. A check
  that failed — no network, a service that answered 503 — does not make the next
  launch retry in a loop.

**Check for Updates…** overrides both the `off` setting and the interval:
clicking it is consent. It lives in the application menu beside About on macOS
(where that platform keeps it) and in the Help menu everywhere else; both raise
the same request. It is the only path that produces a *"You are on the latest
version."* dialog — an automatic check that finds nothing says nothing at all,
and one that could not reach the service leaves one line in the Console.

## What it reads

One request against the rolling release tag, exactly as
[the nightly pipeline documents it](nightly-builds.md#what-an-updater-reads):

```
GET https://api.github.com/repos/orkitec/orkige/releases/tags/nightly
```

From the answer the client takes two things and nothing else:

1. **The ordered version**, out of the machine-readable marker in the release
   notes (`<!-- orkige-nightly-version: … -->`). A release body without one is
   not something this client understands, and "nothing to do" is the honest
   answer to that. The prose is never parsed, and a version is never guessed out
   of an asset filename.
2. **The portable archive for this platform**, out of the asset list, matched by
   its exact name: `Orkige-macos-<token>.zip`, `Orkige-linux-<token>.tar.gz`,
   `Orkige-windows-<token>.zip`, where `<token>` is the version's filename
   rendering. The installable shapes — the disk image, the installer — are
   *install* shapes rather than update payloads, and an exact-name match is what
   makes selecting one impossible rather than merely unlikely.

A platform whose build failed that night simply has no asset, and absence is the
answer. An archive with no `.sha256` sidecar beside it is refused before a byte
is downloaded: that sidecar is the download's only integrity story, so an
archive without one cannot be checked and is therefore not usable.

The release's `## Changes since …` section rides along and is what the update
dialog shows.

## Only newer, ever

The comparison is `core_util/VersionOrder.h` — the same ordering the packaging
composes versions with, so the two sides agree by construction. Four outcomes,
and exactly one of them leads anywhere:

| | |
| --- | --- |
| **newer** | offered |
| **same** | "You are on the latest version." A rebuild of one day's tree is the same version whatever its commit; treating it as an update would re-download forever. |
| **older** | **refused.** A downgrade is never offered and never installed. |
| **incomparable** | refused. An unstamped developer build (`2.0.0 (local build)`) has no ordered identity, and is told that rather than being offered an update it cannot justify — or told it is current, which would be equally untrue. |

The comparison happens **twice**: when the release is read, and again
immediately before the swap is handed over. A payload verified in an earlier
session could have been overtaken by a build installed another way, and a swap
that went backwards is precisely what the ordering exists to prevent.

## Downloading

Both requests go through the engine's async HTTP client (`core_http`), so
neither ever touches the frame loop: submissions return immediately and
completions are delivered from `update()` at the frame boundary, like every
other consumer. The archive is a **save-to-file** request, which streams through
the `FileWriter` funnel — bytes land in a sibling temp file and are renamed over
the target only on success, so a failed or cancelled transfer never leaves a
truncated file where a good one was.

While a check or a download runs, a small progress bar appears at the right of
the **status footer** with the stage beside it. The bar is indeterminate while
the server has announced no size; a bar inching along a made-up total would be a
lie about how far along the download is.

## Verifying

Three checks, in order, on a worker thread — hashing a hundred megabytes and
running the platform's unpacker are the only parts of this that genuinely take
time, and neither belongs on the frame loop. The main thread reads one small
status struct and nothing else.

1. **The digest, always.** The archive is streamed through `core_util/Sha256`
   a block at a time (it is never held in memory) and compared with the sidecar
   entry naming *that* file. A sidecar naming a different file yields nothing
   rather than its digest: matching bytes against a digest issued for something
   else is not a check. A mismatch discards the download and says so.
2. **Unpacking**, with the tool the platform itself provides — `ditto` on macOS
   (the same tool the archive was made with, because a bundle's symlinks and
   executable bits have to survive the round trip), `tar` elsewhere.
3. **The signature, where one exists.** On macOS the published app is Developer
   ID signed and notarized, so the staged copy is put through
   `codesign --verify --strict --deep` and then `spctl --assess --type exec` —
   the assessment the system itself performs. A payload either one rejects is
   discarded, not installed.

   **Windows and Linux builds carry no signature today**, so on those platforms
   the digest is the whole check and the updater reports that rather than
   implying the platforms are equal. Closing the Windows gap needs a code
   signing certificate from a certificate authority, which an Apple Developer
   membership does not cover
   ([the limitations list](nightly-builds.md#what-a-downloaded-build-cannot-do-yet)).

A verified payload is **staged**, not applied. It sits in the editor's writable
application-support directory and survives a quit: a later launch finds it in the
stamp, re-checks that it is still there and still newer, and offers it again
without re-downloading. A payload the running build has caught up with is
deleted.

## Applying it: the swap

A running application cannot replace itself. The established answer, and the one
used here, is a helper process that **outlives** the editor plus a swap that is
never an overwrite.

The editor writes a small script into its own writable directory, launches it
detached, and quits. The script:

1. **waits** for the editor's process id to be gone (with a timeout — a helper
   that waits forever is a stuck process nobody can see);
2. checks the staged copy is still there, and **changes nothing** if it is not;
3. moves the installed copy **aside** to a sibling backup path;
4. moves the staged copy **in**;
5. if step 4 failed, **puts the backup back** and exits non-zero;
6. removes the backup, relaunches if asked to, and deletes itself.

Both moves are renames **inside one directory** — the backup is a sibling of
what it replaces — so neither can half-copy or run out of space partway, and the
undo is exactly the first rename reversed. A half-swapped install is worse than
no update at all, which is why the rollback branch is asserted by tests rather
than trusted.

The helper **decides nothing**. The digest was checked, the signature was
checked and the install location was judged before the script existed; all it
does is wait, move, move, and undo. That is the whole reason it can be a few
lines of shell rather than a program that has to be trusted.

Two ways to get there, and no third:

- **Restart now** — quits the editor and asks the helper to launch it again
  afterwards.
- **Later** — leaves the staged copy alone. It installs at the next clean quit
  anyway, because that is when a swap is safe.

Never mid-session.

### When the install location is not ours

Before any of that, the editor asks whether the place it is installed may be
replaced at all, and refuses with a sentence naming what to do instead:

| Verdict | What it means |
| --- | --- |
| **Missing** | the editor's own location could not be resolved |
| **Read-only** | the containing directory cannot be written — a shared or managed install |
| **Translocated** | macOS gave the downloaded app a randomised read-only path because it was launched out of the folder it was unpacked into. Move it to Applications, or download the new version. |
| **Build tree** | this editor was built from source. Update the tree and rebuild. |

"Can this directory be written" is answered by *doing* it — a probe file created
and removed beside the install — because that is the only answer that accounts
for every layer that might refuse. "Was this built here" is answered by the
editor's own resource locator: an editor that resolved its resources from a
developer **tree** rather than from an app somebody copied is a build tree's
editor ([the locator](editor-distribution.md)), with a CMake cache found at or
just above the install as a cheap second opinion.

In every refusing case the editor does **not** rearrange a directory it does not
own. The releases page is the answer, and the message says so.

## No revert

There is no in-app way back to an older build. Somebody who wants the build from
before a regression finds it on the releases page: each night's dated
`nightly-YYYYMMDD` release keeps serving the bytes it served on the day, for as
long as it stays in the archive. That is the whole mechanism, and it is
deliberately outside the editor.

## Not reachable by an agent

The MCP endpoint gains **no verb** for any of this. An agent driving the editor
can author scenes, run tests and debug scripts; it cannot make the editor
download and install a new copy of itself. That is the same reasoning that keeps
git mutations off MCP: some actions belong to the person at the keyboard.

## Where it lives

| | |
| --- | --- |
| `tools/editor/EditorUpdate.{h,cpp}` | every DECISION, as pure functions over plain data: the cadence gate, the release-feed reading, the version verdict, the per-platform asset choice, the checksum sidecar, the install-location verdict, the swap plan, and the helper script itself |
| `tools/editor/EditorUpdater.{h,cpp}` | the moving parts: the requests, the staging, the worker thread, the subprocess seams |
| `orkige_core/core_util/Sha256.{h,cpp}` | the digest, incremental and filesystem-free |
| `orkige_core/core_util/VersionOrder.h` | the ordering, shared with the packaging |

The split is the one the rest of the tree uses: nothing in the first file reads
a clock, opens a socket, touches a file or spawns a process, which is what makes
the whole decision table unit-testable headlessly on any platform with no
network and no installed app. The second file's subprocess calls are injected as
callables, exactly like the git seam next door, so the whole sequence drives
against a loopback server and a sandbox directory.

## What is proved, and how

`EditorUpdateTests` (unit) drives every decision: the setting's round trip, the
cadence gate in all five of its outcomes (including a stamp from the future and
a manual request overriding both the setting and the interval), the version
comparison including the downgrade and the unstamped build, the release-feed
reading and its three refusals, the asset choice on each platform against a full
release listing carrying the disk image and the installer, the checksum
sidecar in five shapes, the install-location verdict, the swap plan and what it
refuses, shell quoting on both platforms, and the helper script — the wait, the
two moves in order, the rollback move after them, the relaunch, the
self-deletion, and the refusal to emit anything at all for a path that cannot be
quoted safely.

`Sha256Tests` (unit) drives the published vectors, the four lengths where the
padding boundary bites, chunk-size independence (the property a streamed
download rests on) and the rule that anything which is not a complete digest
compares equal to nothing.

`editor_update` (ctest) runs the **whole loop** headlessly against a clean room:
the tree's own `HttpServer` serves a fake release feed, a **real** archive packed
with the platform's own tool and a checksum sidecar on `127.0.0.1`, and the real
updater is pointed at it with a scratch directory standing in for an installed
editor. It asserts nothing-newer, a downgrade refused with nothing downloaded, a
notify-only check that downloads nothing, a good download that verifies and
stages while leaving the installed copy untouched, a download whose digest does
not match (refused, staging cleaned up, install untouched), a payload whose
signature is rejected (discarded), the refusal to rearrange a build tree, the
swap itself — running the real helper and checking the installed copy IS the new
version with no backup left behind — the **rollback**, where a shimmed `mv`
fails the second move and the previous version has to come back exactly where it
was, and the automated-run veto (no request, no state, no install).

**What only a real released build can prove:** that a signed, notarized download
passes `codesign` and `spctl` for real (the driver stubs the *verdict* — a
fixture directory cannot be Developer ID signed — while the commands themselves
are asserted in the unit tests), and that the relaunch after a swap comes back up
as the new version on a user's machine. Everything up to and including the swap
of a real directory tree by the real helper is covered above.
