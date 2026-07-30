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

A **manual** `workflow_dispatch` always builds, even from an unchanged commit:
asking for a build by hand is itself the override.

`workflow_dispatch` runs the same pipeline on demand. Its `run_binaries` input
(default on) skips it, and `ignore_gate` builds the dispatched ref even when
`main` is red — the log and the job summary both name the override, so an
artifact produced that way is never mistaken for a gated one.

## What each platform ships

| Platform | Runner | Preset | Archive |
| --- | --- | --- | --- |
| macOS (Apple silicon) | `macos-15` | `macos-release` | `Orkige-macos-<sha>.zip` |
| Linux (x86_64) | `ubuntu-latest` | `linux-release-next` | `Orkige-linux-<sha>.tar.gz` |
| Windows (x64) | `windows-latest` | `windows-release` | `Orkige-windows-<sha>.zip` |

All three are the default Ogre-Next render flavor in Release. The archive has one
top-level directory and the same shape everywhere:

```
Orkige-<platform>-<sha>/
    VERSION                 the build identity, one `key: value` per line
    KNOWN-LIMITATIONS.md    what this build cannot do yet
    <the editor>            Orkige.app, or orkige_editor[.exe]
    <the player>            beside the editor, for Play
    Media/                  the engine shader, font, water and decal media
```

macOS puts the payload inside the bundle (`Contents/MacOS` for the two binaries,
`Contents/Resources/Media` for the media, which is where the runtime's own bundle
lookup resolves) and repeats `VERSION` + `KNOWN-LIMITATIONS.md` at the archive
root so they are readable before installing. The bundle is zipped with `ditto`,
which preserves its symlinks and executable bits, and is ad-hoc re-signed after
staging so its resource seal covers everything the packaging added — ad-hoc, so
it needs no certificate and confers no trust.

Linux and Windows keep the tree flat beside the executable, which is where
`SDL_GetBasePath` resolves: the editor's icon and mono fonts are shipped there,
and the editor finds them without any repository.

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

## Version stamping

Each build is stamped with the commit it came from and the date it was made, and
the stamp is visible in three places that cannot disagree because they come from
one value:

- **the archive filename**, `Orkige-<platform>-<short sha>.<ext>`;
- **the `VERSION` file** inside the archive, which also records the render
  flavor, the build type and the engine ABI stamp (what a native game module
  must match — `cmake/OrkigeAbiStamp.cmake`);
- **the binary itself**: `orkige_editor --version` prints
  `orkige_editor 2.0.0 (a16c0227a, 2026-07-30) [next, Release]`, and the Help >
  About box shows the same identity.

The stamp reaches the binary as two configure-time cache variables the pipeline
passes:

```sh
cmake --preset macos-release -DORKIGE_BUILD_COMMIT=a16c0227a \
                             -DORKIGE_BUILD_DATE=2026-07-30
```

They are compile definitions on ONE translation unit
(`tools/editor/EditorBuildInfo.cpp`), so a re-stamp recompiles one small file
rather than the whole editor. An ordinary developer build leaves them unset and
reports `2.0.0 (local build)` — never a commit nobody supplied.

The engine ABI stamp is a content fingerprint of the engine sources, which
answers "does this module match this library" and not "which commit is this", so
it rides in `VERSION` as extra information rather than serving as the version.

## The smoke test

Packaging is not the end of a build job. Each one then unpacks its own archive
into a clean directory that has no build tree to lean on and runs

```sh
python3 Util/orkige_nightly_package.py --verify <unpacked dir> --platform <p> \
        --commit <sha>
```

which asserts, and fails the job on any of them:

- every file the layout promises is present — the editor, the player, `VERSION`,
  `KNOWN-LIMITATIONS.md`, `Media/` with its shader library and the font, water
  and decal dirs;
- no editor settings file rode along from the build machine (window layout and
  recent projects are the developer's, not the download's);
- the binary **starts and reports its version**, and the commit it reports is
  the commit the job stamped it with. A wrong stamp is a failure, not a note.

What that proves: the executable and everything it dynamically links load on a
machine that is not the build tree, and the artifact is structurally complete.
What it does not prove: that the editor renders. The `--version` path returns
before any window is created, deliberately, so the check needs no display; a
render-level smoke test would need per-platform virtual GPU setup (xvfb and
lavapipe on Linux, a registered software ICD on Windows) and is a separate,
larger step. Until the media-resolution gap below closes, a render smoke test
would fail by design anyway.

The same verifier logic is unit-tested headlessly by the
`orkige_nightly_package_selftest` ctest (label `unit`), which drives the identity
strings, the limitations table and its per-platform rendering, the media staging
over a synthetic build tree, the archive round-trip, and every verdict the
verifier can reach — including a stand-in binary reporting the wrong commit.

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

Making the release public is one change (dropping `--draft` from the
`gh release create` call in the publish job). What has to be true first is the
list below: a downloaded editor has to render, and a user has to be able to
export a game from it.

## What a downloaded build cannot do yet

Every archive carries a generated `KNOWN-LIMITATIONS.md` listing exactly the gaps
that apply to its platform. The list is a table of records in
`Util/orkige_nightly_package.py`, so closing a gap is deleting one record — the
selftest asserts the rendered document lists exactly the records that apply,
whatever they are, which keeps it honest in both directions.

Today the list is headed by the one that matters most:

- **The editor does not render in a copied build.** It resolves the engine's
  shader, font, water and decal media from the absolute path of the build tree it
  was compiled in, so on any other machine it finds none of it: the window opens
  and `--version` answers, but the material system has no shader templates and
  nothing draws. The archives already ship that media under `Media/`; the missing
  piece is the editor asking for it next to its own executable first. This is why
  the release stays a draft.
- **Exporting a game needs the engine repository** — Build > Export copies out of
  a build tree and runs `Util/orkige_export.py`.
- **Play looks for the runtime at its build-time path.** The matching player
  ships beside the editor, ready for the lookup to prefer it.
- **SVG and Lottie import needs python3 and the repository** — those imports cook
  through `Util/cook_shapes.py` / `Util/cook_vector_anim.py`.
- **Compiled C++ game code needs a toolchain** — CMake, Ninja, a C++20 compiler
  and an engine build tree. Game behaviour written in Lua needs none of that,
  which is the whole point of the distinction.
- **Settings are written next to the executable**, so the editor must live
  somewhere the user can write.
- **The builds are unsigned.** macOS reports the app as unopenable until the
  download quarantine flag is removed (`xattr -dr com.apple.quarantine
  Orkige.app`, or right-click > Open); Windows SmartScreen warns until "More
  info" > "Run anyway". The limitations file spells out both.

## Running the packaging by hand

The packager takes a build tree that already exists; it never builds.

```sh
cmake --preset macos-release -DORKIGE_BUILD_COMMIT=$(git rev-parse --short=9 HEAD) \
                             -DORKIGE_BUILD_DATE=$(date -u +%F)
cmake --build --preset macos-release --target orkige_editor orkige_player
python3 Util/orkige_nightly_package.py --platform macos \
        --build-dir build/macos-release --commit $(git rev-parse HEAD) \
        --output /tmp/nightly-out
```

Then check the result the way CI does:

```sh
mkdir -p /tmp/smoke && ditto -x -k /tmp/nightly-out/Orkige-macos-*.zip /tmp/smoke
python3 Util/orkige_nightly_package.py --verify /tmp/smoke --platform macos \
        --commit $(git rev-parse HEAD)
```

`--selftest` runs the headless self-checks without needing any build tree at all.
