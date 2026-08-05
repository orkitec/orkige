# The editor's command line

The Orkige editor answers to `argv` as well as to a mouse. A first argument that
is a **subcommand word** runs that operation headlessly — no window, no render
backend, no GPU — and exits with a code a script can branch on:

```sh
orkige_editor export --project ~/games/roller --platform macos
# orkige_editor: packaging 'Roller' for macos (engine: …)
# …
# orkige_editor: OK /Users/you/games/roller/builds/macos/Roller.app
```

With no subcommand the editor opens its window exactly as it always has, and
every existing flag (`--version`, `--mcp-port`, `--claude-ide`, …) keeps its
meaning.

## Why the editor carries this

An installed Orkige is **the editor application plus the players it fetches**.
`orkige_export` is a development-tree tool and is not part of a release, so on a
machine that has only a distributed Orkige, the ability to package a game exists
**inside this one process and nowhere else**. Without a command line, the only
door into it is the MCP endpoint of a running windowed editor — which is not a
thing a build server can reasonably drive.

So the subcommands are that door. They are not a second exporter: `export` plans
through the same decision the **Build ▸ Export** menu uses and runs through the
same in-process exporter, with the same refusals. What the command line adds is
the one thing a standalone tool cannot know — **which engine source this
installation has**: its own build tree, the payload it carries inside itself, a
fetched device player, or an installed SDK pack.

`orkige_export` still exists for work inside the engine repository, and the two
share one implementation. Neither spawns the other.

## The subcommands

| Command | What it does |
|---------|--------------|
| `export` | packages a project |
| `test` | runs a project's Lua test suite |
| `fetch-payload` | downloads and installs a platform's player |
| `version` | this build's identity (the `--version` flag's twin) |
| `changelog` | what this build shipped with |
| `help` (also `--help`, `-h`) | the usage text |

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | it worked |
| `1` | it ran and failed — the reason is on stdout as `orkige_editor: ERROR: …` |
| `2` | the arguments were not usable (unknown subcommand, missing option) |

Human lines carry the `orkige_editor:` prefix, and a successful run's **last
line** is `orkige_editor: OK <artifact>` — the same contract `orkige_export`
prints, so a script greps one pattern whichever door produced the artifact.

### A mistyped subcommand exits, it never launches

A first argument that is a word must resolve to a known subcommand:

```sh
$ orkige_editor exprot --project ~/games/roller --platform macos
orkige_editor: unknown subcommand 'exprot'
usage: …
$ echo $?
2
```

This is deliberate and it is load-bearing. Unrecognised arguments used to be
ignored, so a typo on a build server opened a graphical application on a machine
with nobody in front of it and the job hung until its timeout, with nothing in
the log to explain it. **Flags** keep the old behaviour — the windowed editor's
own options are flags and an unknown one must stay harmless there.

## `export`

```
orkige_editor export --project <dir>
                     --platform macos|linux|ios-simulator|ios|ios-ipa|android|
                                android-aab|web
                     [--output <dir>]
                     [--signing-identity <name>]
                     [--provisioning-profile <path>]
                     [--distribution-identity <name>]
                     [--distribution-profile <path>]
                     [--android-keystore <path>]
                     [--android-key-alias <name>]
                     [--bundletool <path>]
                     [--sign | --notarize]
                     [--macos-identity <name>]
                     [--notary-key <path>] [--notary-key-id <id>]
                     [--notary-issuer <id>]
                     [--notary-apple-id <id>] [--notary-team-id <id>]
                     [--with-tests [--test-filter <substring>]]
```

Output lands in `<project>/builds/<platform>/` unless `--output` says otherwise.
The platform vocabulary and every artifact shape are the exporter's — see
[desktop-export.md](desktop-export.md) for the two desktop packages,
[textures.md](textures.md) for the export-time cook,
[web-export.md](web-export.md) for the browser payload and
[device-payloads.md](device-payloads.md) for what a phone build needs.

The **desktop** platform is the one this machine runs: a package is assembled
around a player binary and nothing cross-compiles one, so `macos` packages on a
Mac and `linux` on Linux, each refusing the other by name.

A phone platform packages out of the installed device player, read from the same
directory `fetch-payload` writes into — so the two subcommands are one workflow
and neither needs to be told where the other put anything:

```sh
orkige_editor fetch-payload player-android
orkige_editor export --project ~/games/roller --platform android
```

With no player installed the refusal names the command that installs one, rather
than the settings window a build server does not have. That is the only thing
about an export that differs between the two doors: the three prerequisite tiers
are the same three tiers, told with an instruction the caller can follow.

### `--with-tests`: a package that runs the game's own suite

`--with-tests` packages a **test build** instead of a shippable one: the
project's `tests/*.test.lua` suite rides in the payload and the artifact runs it
instead of the game, exiting with the suite's verdict
([testing.md](testing.md#running-the-suite-inside-the-package)). It is what
`test` is to a project folder, for the thing that actually ships — the payload's
cooked textures, baked samplers and staged media are what the tests see.

The desktop platforms and the iOS ones only. An Android or web payload lives
inside an archive the runner cannot walk, and is refused by name rather than
passing over zero tests. `--test-filter` without `--with-tests` is a usage error, because an
option that silently did nothing would be worse.

### `--sign` / `--notarize`: a macOS package for other people's Macs

A `macos` export is signed **ad-hoc** unless asked otherwise, which runs on this
machine and is refused on anyone else's. `--sign` seals it with a Developer ID
identity and the hardened runtime; `--notarize` additionally submits it to Apple
and staples the ticket, so a downloaded copy opens with no network round trip.
Missing credentials refuse by name and package nothing —
[desktop-export.md](desktop-export.md) has the credential table and the exact
command sequence.

### The store platforms are here

`ios-ipa` and `android-aab` are reachable from this door and deliberately **not**
over MCP — as is a signed macOS package: they need machine-local secrets a
remote agent does not hold. That
distinction is the whole reason [store-release.md](store-release.md) calls store
packaging a command-line operation — and on an installed Orkige, *this* is that
command line.

### Credentials: three sources, one precedence

An identity or a keystore can arrive three ways, and the first non-empty one
wins:

1. **a flag above** — what a build script passes,
2. **the machine-local per-project build settings** the editor's
   **Build ▸ Project Settings…** window writes,
3. **the environment** (`ORKIGE_IOS_SIGNING_IDENTITY`, `ORKIGE_ANDROID_KEYSTORE`,
   … — see [ios-signing.md](ios-signing.md) and [store-release.md](store-release.md)).

Reading the machine-local settings file is **the one deliberate exception** to
"a headless run touches no user state". Skipping it would make
`orkige_editor export` produce a differently-signed artifact than
**Build ▸ Export** does on the same machine — exactly the drift a shared front
door exists to remove. It is a **read** and nothing else: no user state is
written, no recents move, no view settings are loaded.

**Passwords are not part of it.** A headless run installs no credential vault, so
a keystore password comes from the environment, which is where a build server
keeps one anyway.

## `test`

```
orkige_editor test --project <dir-or-.orkproj>
                   [--test-filter <substring>]
                   [--report-dir <dir>]
```

Runs `<project>/tests/*.test.lua` — the project's own Lua suite
([testing.md](testing.md)) — and exits with the suite's verdict: `0` when
everything passed.

```sh
orkige_editor test --project ~/games/roller --report-dir ci-out
# orkige_editor: running the tests of 'Roller' (player: …/Orkige.app/Contents/MacOS/orkige_player)
# orkige_player: tests - 18 tests, 18 passed …
# orkige_editor: OK /…/ci-out/tests-20260804T101500Z.jsonl
```

`--test-filter` is matched against `<file>::<test name>`, exactly as the
runner's own flag is. `--report-dir` chooses where the run's JSONL artifact
lands; it is the existing `ORKIGE_TEST_REPORT_DIR` seam under a flag name, not
a second report format. Without it the runner writes its artifact where it
always does, beside the breadcrumb trail, and the `OK` line says the suite
passed rather than naming a path this process would have had to guess.

### This one runs the player, and that is not the rule above being bent

`export` may never spawn `orkige_export`, because a second exporter would be a
second **copy of a decision** — two places that could disagree about what a
package contains. Handing a test run to the player copies nothing, because the
editor holds no test runner to copy. The player is the engine's runtime: the
only part of an installation that owns a game world.

That matters because of what a test may declare. A test with a `scene` is a
**play-mode test** — physics stepping, scripts updating, frames advancing — and
those are the tests worth putting on a build server. The editor boots exactly
one render backend, the graphics one, straight into a window, so it has no
world to lend before its own is up. A door that ran only the worldless half of
a suite would report a green verdict on a game whose gameplay was never
exercised.

So this subcommand contributes the one thing the runtime cannot know for
itself: **which player this installation has** — the copy inside the
application for a distributed editor, the build tree's binary for a developer
one ([editor-distribution.md](editor-distribution.md)). The runner's verdict
travels back untouched.

Standard streams are inherited rather than captured, so the run's output
reaches the caller as it happens. A wedged suite shows which test it was on
instead of sitting silent until something kills it.

### What it refuses, and why each is a separate answer

| Refusal | What fixes it |
|---------|---------------|
| no `--project` | name one — a suite belongs to a project (its `tests/` and its `scripts/` libraries), never to a loose scene |
| no manifest at that path | point at a project directory or a `.orkproj` file |
| the project has no `tests/` directory | write one — the sentence names the directory and the `.test.lua` suffix |
| this build has no scripting backend | use a build with scripting on; an `ORKIGE_SCRIPTING=OFF` build has no interpreter and reporting a pass would be a lie |
| no player beside the editor and no build tree | there is nothing to run the suite in |

The directory check is deliberately the only thing this door decides about the
suite's contents. **What counts as a test inside `tests/`, and what an empty
run is worth, stay the runner's** — one question answered in one place. What
the door adds is that a misspelled or missing `tests/` fails loudly here
instead of booting an engine to be told nothing ran.

## `fetch-payload`

A phone's player is another architecture's binary, so a released editor
*downloads* it rather than carrying it ([device-payloads.md](device-payloads.md)).
The same download, from a shell:

```sh
orkige_editor fetch-payload --list          # what this build can install
orkige_editor fetch-payload player-android  # install one
```

It prints the install directory on success and one honest line on failure (an
unstamped developer build can pair with no published release and says so).

`--list` names the directory either way — where an installed payload is, and
where a missing one would land. The path carries this editor's render flavor and
its ordered version, so it is not a path anybody can spell from outside, and
"not installed" on its own leaves the follow-up question unanswered.

## What is deliberately NOT here

Anything that needs a live game world **in this process**: opening a scene,
editing objects, running editor scripts, taking a screenshot. A
`TransformComponent` stores its transform *inside the render node*, so loading
a scene reaches a `RenderWorld`, which reaches `Engine::setup` — and the editor
boots exactly one render backend, the graphics one, straight into a window.

The engine does carry a **deviceless render system** (`ORKIGE_RENDERSYSTEM=NULL`,
`engine_render/RenderSystemSelection.h`), which is how the player holds a live
scene on a machine with no display, and the `player_deviceless` test proves it.
That is a **runtime** capability. The editor is a window application: its scene
view, its preview, its gizmos and its whole interface *are* render targets, so
a deviceless editor would be an MCP endpoint with no pixels — and an automated
run opens no socket. A windowed launch under that variable therefore **refuses
by name** before any render system is installed:

```sh
$ ORKIGE_RENDERSYSTEM=null orkige_editor
orkige_editor: ERROR: ORKIGE_RENDERSYSTEM=null selects the deviceless render
system, and the editor is a window application: … Headless work goes through
the subcommands (orkige_editor help); a live scene with no display is
orkige_player.
$ echo $?
1
```

**Subcommands are exempt.** They install no render system at all, so a build
server that sets the variable machine-wide for its player runs still exports,
tests and fetches with it set.

Where a live world is what the caller actually wants, the honest answer is the
part of the installation that already has one — which is what `test` does.
Everything else belongs to a running editor and is reached over its MCP
endpoint ([mcp.md](mcp.md)).

## Behaviour a script can rely on

A subcommand run is an **automated run** by the same definition a scripted test
is — nobody is watching it, so it touches no user state and opens no socket:

- no view settings, no recents, no imgui layout file,
- no MCP endpoint and no Claude-IDE lock,
- no update check and no credential vault,
- no `.mcp.json` written into any project.

The single stated exception is the credential-name read above.

`test` is the one subcommand that runs a second process, and the same rule
carries across the boundary rather than stopping at it. The player inherits the
environment, so a run isolated by `ORKIGE_EDITOR_STATE_DIR` stays isolated; the
two automation hooks that would cut a run short (`ORKIGE_DEMO_FRAMES`,
`ORKIGE_DEMO_SCREENSHOT`) are **unset** for the child, because a player that
exits after N frames would end a suite early and call it a pass. No debug port
is passed, so the run opens no socket. What it does touch is the runtime's own
writable directory: a play-mode test loads scenes and the engine writes its
breadcrumb trail, exactly as any player run does.

The network rule is about **incidental** traffic: no subcommand reaches the
network as a side effect of doing something else. Update checks, telemetry and
discovery all fall under that, and none of them run.

`fetch-payload` is not an exception to the rule, because a download is not a
side effect there — it is the whole command. The caller named the payload by id
on the command line, and the connection is the work that was asked for. A rule
phrased as "an automated run never opens a connection" would have to carve this
out; phrased as "an automated run never opens a connection the caller did not
ask for", it covers both cases without one. No other subcommand opens a
connection at all.

## Platform notes

**Windows.** The editor is built as a **console-subsystem** executable
(`add_executable` with no `WIN32`), which is what makes stdout, stderr and the
exit code reach a caller. Flipping it to the GUI subsystem would silently kill
this whole surface: the process would detach from its console and every
subcommand would appear to print nothing.

**macOS.** The editor ships as `Orkige.app`, so its executable is buried at
`Orkige.app/Contents/MacOS/Orkige`. `open -a Orkige --args …` is *not*
equivalent — it detaches the process and discards the exit code, which is the
one thing a subcommand exists to produce.

So the downloadable archive publishes the command line under **the same
`orkige_editor` name on every platform**, at the archive root: on Linux and
Windows that is the editor executable itself, on macOS a small wrapper beside
the bundle that `exec`s the buried executable, passing arguments through and
returning its exit code unchanged.

```
Orkige-macos-<version>/
  Orkige.app/            the editor
  orkige_editor          the command line  ->  Orkige.app/Contents/MacOS/Orkige
  VERSION  CHANGELOG.md  KNOWN-LIMITATIONS.md  THIRD-PARTY-NOTICES.md
```

The wrapper runs **the bundle beside it and no other**. It does not search
`/Applications`, because an unpacked archive that ran some other installation's
editor would be a wrong answer that looks right — on a build server pinning a
version, silently the wrong build. The cost of that choice is where it bites:
move `Orkige.app` out of the unpacked directory and the wrapper stops working.
It says so, and names the path to invoke directly. **A machine that installs
Orkige by dragging the app to `/Applications` invokes
`/Applications/Orkige.app/Contents/MacOS/Orkige`** — which is why the disk
image carries no wrapper, and the portable archive does.

`Util/orkige_nightly_package.py` writes it, and its `verify_layout` refuses an
archive that lacks the entry point or whose wrapper no longer reaches the
executable, so a layout change cannot drop the command line quietly.

**The installed forms need no wrapper.** A Linux AppImage takes arguments
directly (`./Orkige-linux-<version>.AppImage export --project …`), and the
Windows installer puts `orkige_editor.exe` in the install directory. Only the
macOS bundle buries its executable, which is the one case this exists for.

## Tests

| Test | What it holds |
|------|---------------|
| `EditorCliTests` (unit) | the argument router: every subcommand, flag passthrough, that an unknown word never reaches the window road, and the deviceless refusal — which fires for a windowed launch, never for a graphics render system, never for a subcommand |
| `orkige_nightly_package_selftest` (unit) | the archive's entry point: the macOS wrapper is *run* over a stand-in bundle (arguments arrive one for one, the exit code comes back, an absent bundle refuses), and `verify_layout` rejects a tree whose command line is missing or no longer reaches the executable |
| `editor_cli` (integration) | the real binary: the three exit codes, the refusals, a deviceless launch that exits instead of booting, an export that produces an artifact and prints its `OK` line — each within a deadline, because a subcommand that opened a window would hang rather than fail |
| `editor_cli_test` (integration) | the real binary running a real suite: a project with tests passes and names its JSONL artifact, a filter that matches nothing still passes, a project with no `tests/` is refused by name, and a suite whose tests fail comes back non-zero |
| `editor_bundle_native` (integration) | the distribution proof: a **copied** editor, in a clean room that denies the repository, the engine build tree and the vcpkg root, packages a project from a command line with no editor session running |
