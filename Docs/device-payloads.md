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

One payload is one self-contained directory. For iOS:

    orkige_payload.txt      platform / flavor / version / commit
    OrkigePlayer.app        the prebuilt player (iOS Simulator)
    Media/                  the engine media that player renders through

The Android one carries more, because an APK is **assembled** around its player
rather than copied whole the way a `.app` is:

    orkige_payload.txt      platform / flavor / version / commit / abi
    libmain.so              the prebuilt player, already STRIPPED
    Media/                  the engine media that player renders through
    android/
        AndroidManifest.xml the manifest template the assembly substitutes
        res/                the network-security policy every package declares
        java/               the Java the package compiles

Everything in that list is an **engine** piece, which is exactly why it
travels: a machine with no repository has no other source for it.

- `libmain.so` is stripped when the payload is composed, on the machine that
  has the NDK. A debug library is hundreds of megabytes of DWARF; the client
  that unpacks this has no strip tool for the target anyway.
- `abi` is recorded rather than assumed. A package's `lib/` directory is named
  for it, and an emulator payload is not a phone's.
- `java/` holds **SDL3's own Java glue** (zlib licensed, so redistributable)
  beside Orkige's activity and HTTP transport. It is taken from the exact SDL
  source the player's `libmain.so` was built against: a Java glue and a native
  library that disagree crash at the JNI boundary, so the pair is composed
  together and travels together. From a build tree the same script takes that
  glue out of vcpkg — a downloaded editor has no vcpkg, and shipping the
  sources is what closes that gap.

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

## What you need, per platform

Three different kinds of prerequisite, and the editor keeps them apart because
they are fixed in three different ways.

| Tier | What it is | How you get it |
| --- | --- | --- |
| **Player payload** | that platform's prebuilt player | the editor fetches it — switch the platform on under Settings > Build Targets |
| **Platform toolchain** | programs that belong to the platform's own SDK | you install them; the editor names each missing one |
| **Engine SDK pack** | an engine to COMPILE C++ game code against | only for a project that carries compiled C++ ([SDK pack](sdk-pack.md)) |

**A project with no C++ never needs an SDK pack — for any platform, at debug,
release or signed.** A Lua game has nothing to compile, so nothing consults a
pack and no message mentions one.

For **iOS Simulator** the payload is the whole story: the `.app` is copied into
place and packaging runs no platform tools at all.

For **Android** you also need the Android SDK's own build tools, because every
APK is assembled and signed by them. The assembly itself runs in process — the
editor stages the trees, writes the archive and spawns each SDK program
directly, so there is no interpreter, no script and no shell anywhere in the
path:

| Program | Where it comes from |
| --- | --- |
| `aapt2`, `zipalign`, `apksigner`, `d8` | the SDK build tools — `sdkmanager "build-tools;35.0.0"`, or SDK Manager > SDK Tools |
| an Android platform, API 28 or newer | `sdkmanager "platforms;android-35"` |
| a JDK | your platform's OpenJDK package (`brew install openjdk` on macOS) |

The SDK is **found rather than configured**: `ANDROID_HOME`, then
`ANDROID_SDK_ROOT`, then the place your platform's own installer puts one. The
newest installed build-tools and platform are the ones used, so whatever your
SDK manager gave you is what runs. A JDK is resolved the same way — `JAVA_HOME`,
what `javac` on the PATH belongs to, then the usual install locations.

Anything missing is reported **one program at a time**, with the command that
installs it. That is deliberate: somebody who has the SDK and no JDK should read
about the JDK, not about the SDK.

**Signing is not an extra step for a debug APK.** Android installs no unsigned
package at all, so every APK is signed — but the debug key is created for you on
first use (`~/.android/debug.keystore`), and nothing is asked of you. A *release*
App Bundle for a store is the different case: it needs your own release keystore
and `bundletool`, it is built from an `android-release` tree rather than from a
fetched payload, and it refuses rather than emitting a half-signed artifact —
[store release](store-release.md).

We ship the engine, never a toolchain. That is the same line the
[native modules](native-modules.md) tier draws when it reports a missing
`cmake` differently from a missing SDK pack.

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

orkige_export --project projects/jumper-lua --platform android \
              --engine-bundle <the app's Resources> \
              --device-payload <the installed payload directory>
```

The Android one additionally runs the machine's SDK tools, so it refuses first
with the list of any that are missing, and hands the ones it found down to the
packaging script — the programs it checked for are exactly the ones that run.

A payload that was **not** downloaded — one composed from a source build, or one
a packaging test stands up in a clean room — is named through
`ORKIGE_EDITOR_PAYLOAD_DIR`, a directory holding one subdirectory per payload
id. It is checked for completeness exactly like a fetched one: the override is a
shortcut past the download, never past the contract.

## Publishing

`binaries-player-ios` and `binaries-player-android` each build their platform's
player from its preset tree and compose, pack and checksum the payload:

```sh
python3 Util/orkige_nightly_package.py \
    --stage-device-payload player-ios-simulator \
    --build-dir build/ios-simulator-debug \
    --commit <sha> --date <YYYY-MM-DD> --output device-payload

python3 Util/orkige_nightly_package.py \
    --stage-device-payload player-android \
    --build-dir build/android-debug \
    --commit <sha> --date <YYYY-MM-DD> --output device-payload
```

The publish job uploads them beside the editors, so they land in both the rolling
and the dated release. Neither is **a precondition for shipping an editor**: the
publish job waits for the payload jobs but proceeds without their artifacts, and
the release notes then say per platform that no player was published for that
build — which is where somebody checks when an editor reports it cannot find
one. See [the nightly builds](nightly-builds.md).

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
- **Android** — published, fetched and packaged, as an **APK for arm64-v8a**
  (the artifact people ship to phones; the CI emulator's x86_64 is a test
  shape, not a published one). The Android SDK build tools and a JDK are a
  stated prerequisite, reported program by program — see the tier table above.
  A release App Bundle is not packaged from a payload: it is built from an
  `android-release` tree and needs credentials the payload cannot carry
  ([store release](store-release.md)).
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
  that payload alone, and without one it refuses with the way to get it. Its
  verdict is byte-level: the player it shipped is the payload's own.
- `editor_bundle_android` — the same clean room, and the place the three
  prerequisite tiers are held apart. It asserts three different answers: no
  player installed (a download), a player but no Android SDK (each missing
  program named, and no download offered for something no download provides),
  and both (the APK, carrying the payload's own library byte for byte). The
  project is pure Lua and no SDK pack exists anywhere in the room, so neither
  refusal is allowed to mention one.
- `export_android_run` / `export_android_payload_run` — the APK INSTALLED ON AN
  EMULATOR AND LAUNCHED, from a build tree and from a payload. Bytes are not a
  game: a package whose player, media, dex and Java all arrived through a
  payload installs perfectly and can still boot into nothing.
- `doc_link_lint` — every doc this code points a person at exists. A refusal
  that names a doc, and a help link the editor composes from a page name, are
  both links somebody follows; the portal's own build cannot see either, so a
  renamed page fails here instead of shipping dead.
- `export_ios_simulator_payload_run` — the same package INSTALLED ON A
  SIMULATOR AND LAUNCHED: it boots its bundled project and renders its frames.
  Bytes are not a game — a package whose engine media arrived from the payload
  rather than from a source tree installs perfectly and can still boot into
  nothing, so the payload path carries the same install-and-launch verdict its
  build-tree sibling `export_ios_simulator_run` does.
- `orkige_nightly_package.py --selftest` — the asset name, the composition, the
  manifest a payload describes itself with, and the release notes' two states.
