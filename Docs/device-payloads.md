# Device player payloads

A downloaded Orkige packages a game for the desktop and the browser out of what
it carries inside itself. A **phone** runs another architecture's binary, so
that player is not carried: it is published as its own release asset and
**fetched on demand**, once per build, into the editor's writable state
directory.

This matters more than the size argument suggests. Lua is the path that needs
no toolchain, and most games are Lua — so "write a Lua game, ship it to a
phone" is the majority use case, and it has to work from a released editor with
no repository, no build tree and no compiler on the machine.

## What a payload is

One payload is one self-contained directory:

    orkige_payload.txt      platform / flavor / version / commit
    OrkigePlayer.app        the prebuilt player (iOS Simulator)
    Media/                  the engine media that player renders through

It is published as a `.zip` whose **contents sit at the archive root**, so
unpacking it into a directory *is* the payload, plus the `.sha256` sidecar every
published asset carries. The name is one grammar:

    orkige-<payload id>-<flavor>-<version token>.zip
    orkige-player-ios-simulator-next-2.0.0-nightly.20260802_dea551f9e0.zip

`Util/orkige_nightly_package.py` composes that name and the editor
(`payloadAssetName` in `tools/editor/EditorPayloads.h`) composes it again to
look the asset up. The two libraries do not link each other, so a unit test on
each side pins the literal.

The `Media/` tree comes from the SAME preset build tree the player did, laid out
the way every runtime resolves it at boot — which is what lets an export from a
payload need no engine source tree at all.

## Paired on the release tag

A payload belongs to the build that fetches it: both came out of one night's
publish, from one commit. The pairing is therefore the **dated release tag** the
editor's own ordered version names —
`2.0.0-nightly.20260802+dea551f9e0` → `nightly-20260802`.

Two things follow from that choice, and both are the point:

- **Not the ABI stamp.** A payload records a stamp over its own surface while a
  source-built editor's stamp also hashes every implementation file. The two
  numbers differ by construction and could never match, so the stamp cannot do
  this job (it does a different one — see [SDK pack](sdk-pack.md)).
- **Not the rolling `nightly` tag.** That one is replaced every night, so an
  editor a day old would find its own assets gone. The dated release never
  moves.

The editor asserts the pairing rather than assuming it: the release document it
reads carries the ordered version in its marker, and a release that names a
different build is refused by name.

## What the editor does

`Settings > Build Targets` lists the platforms this installation packages for.
The default is **the host alone** — nothing is fetched for a platform nobody
switched on. Switching one on offers *Get Player*; switching it off gives the
download back.

A fetch is: read the dated release → find the asset and its `.sha256` → download
→ hash the bytes → unpack with the platform's own tool → check the payload is
complete → rename it into place. Every step refuses out loud:

| what happens | what the editor says |
| --- | --- |
| no asset for this build | names the asset it looked for |
| no `.sha256` beside it | refuses rather than trusting the bytes |
| the digest does not match | discards the download |
| it unpacks incomplete | names the missing piece |
| the release names another build | names both versions |
| no network | says so — there is no offline path |

Nothing lands inside the application. A distributed bundle is signed and
read-only, and an app that rewrites itself invalidates its own signature, so an
installed payload lives at
`<writable state>/payloads/<id>/<flavor>/<version token>` and nowhere else —
the same place an [installed SDK pack](sdk-pack.md) lives, for the same reason.

## Pruned, not accumulated

The published archive keeps every release forever; that is a server's job. A
client keeps exactly what its own build needs: **the current version of each
enabled payload, and nothing else.** A superseded version and a platform the
user switched off both go, at boot and after every successful fetch. The two
problems are different and the client only solves the small one.

## Packaging from one

The exporter is handed the directory and never learns how it got there
(`EngineSource::devicePayload`). It copies the player bundle, lays the payload's
`Media/` beside it, stages the project payload — cooking its textures for the
flavor the manifest records — and rewrites the bundle identity to the project's.
That is the same code an export from a preset build tree runs; only the two
inputs are sourced differently.

The CLI takes one too:

```sh
orkige_export --project projects/jumper-lua --platform ios-simulator \
              --engine-bundle <the app's Resources> \
              --device-payload <the installed payload directory>
```

A payload that was **not** downloaded — one composed from a source build, or one
a packaging test stands up in a clean room — is named through
`ORKIGE_EDITOR_PAYLOAD_DIR`, a directory holding one subdirectory per payload
id. It is checked for completeness exactly like a fetched one: the override is a
shortcut past the download, never past the contract.

## Publishing

`binaries-player-ios` builds the iOS-simulator player from its preset tree and
composes, packs and checksums the payload:

```sh
python3 Util/orkige_nightly_package.py \
    --stage-device-payload player-ios-simulator \
    --build-dir build/ios-simulator-debug \
    --commit <sha> --date <YYYY-MM-DD> --output device-payload
```

The publish job uploads it beside the editors, so it lands in both the rolling
and the dated release. It is **not** a precondition for shipping an editor: the
publish job waits for the payload job but proceeds without its artifact, and the
release notes then say that no mobile player was published for that build —
which is where somebody checks when an editor reports it cannot find one. See
[the nightly builds](nightly-builds.md).

## The second consumer

The mechanism is one fetch, not one download. A payload declares a **kind**:
`Player` today, `Sdk` for the relocatable engine a project's compiled C++ game
code builds against ([SDK pack](sdk-pack.md)) — declared beside it, and
deliberately unwired. Making a pack the second consumer is a catalogue entry
plus the paths that entry declares complete: the naming, the release pairing,
the install layout, the completeness check, the prune and the whole fetch
sequence are already keyed on the id. An installed pack even lives under the
same writable state root today, for the same read-only-bundle reason.

## What this covers today

- **iOS Simulator** — published, fetched and packaged.
- **Android** — deliberately NOT offered. Assembling an APK needs the Android
  SDK's own build tools on the machine (`aapt2`, `zipalign`, `apksigner`) as
  well as the player, and a download that turned into "install a toolchain"
  would be a switch that promises what it cannot deliver — we ship the engine,
  never a toolchain. The export refuses it by name and says to build Orkige
  from the engine repository. The naming and packing on the publishing side
  already understand the id, so publishing one is what remains once that
  question is answered.
- **iOS device** — a signed device build needs the arm64-iphoneos player and a
  signing identity; the published payload is the simulator one, and a signed
  export refuses with that difference named
  ([iOS signing](ios-signing.md)).
- The simulator payload comes from the `ios-simulator-debug` tree — the same
  player the editor's own Play-on-simulator uses — so it is a debug build and
  correspondingly large.

## Where it is proven

- `EditorPayloadsTests` — the catalogue, the asset name, the dated-release
  pairing, the install layout, what makes a payload complete, the prune plan,
  the settings codec and the refusal sentences.
- `editor_payload_fetch` — the whole download against a loopback release
  service with a real archive: the fetch that installs, a release with no such
  asset, one with no checksum, bytes that do not match theirs, an archive that
  unpacks incomplete, a release published for another build, the prune, and the
  automated-run veto.
- `editor_bundle_ios` — a COPIED editor in a clean room that denies the
  repository: with a payload installed it exports an iOS simulator app out of
  that payload alone, and without one it refuses with the way to get it.
- `orkige_nightly_package.py --selftest` — the asset name, the composition, the
  manifest a payload describes itself with, and the release notes' two states.
