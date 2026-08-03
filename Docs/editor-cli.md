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
                     --platform macos|ios-simulator|ios|ios-ipa|android|
                                android-aab|web
                     [--output <dir>]
                     [--signing-identity <name>]
                     [--provisioning-profile <path>]
                     [--distribution-identity <name>]
                     [--distribution-profile <path>]
                     [--android-keystore <path>]
                     [--android-key-alias <name>]
                     [--bundletool <path>]
```

Output lands in `<project>/builds/<platform>/` unless `--output` says otherwise.
The platform vocabulary and every artifact shape are the exporter's — see
[textures.md](textures.md) for the export-time cook, [web-export.md](web-export.md)
for the browser payload and [device-payloads.md](device-payloads.md) for what a
phone build needs.

### The store platforms are here

`ios-ipa` and `android-aab` are reachable from this door and deliberately **not**
over MCP: they need machine-local secrets a remote agent does not hold. That
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

## What is deliberately NOT here

Anything that needs a **live game world**: opening a scene, editing objects,
running editor scripts, taking a screenshot. A `TransformComponent` stores its
transform *inside the render node*, so loading a scene reaches a `RenderWorld`,
which reaches `Engine::setup`, which reaches a window and a GPU. There is no
null render backend, so a headless scene operation cannot exist — and the usage
text promises none.

Those belong to a running editor and are reached over its MCP endpoint
([mcp.md](mcp.md)).

## Behaviour a script can rely on

A subcommand run is an **automated run** by the same definition a scripted test
is — nobody is watching it, so it touches no user state and opens no socket:

- no view settings, no recents, no imgui layout file,
- no MCP endpoint and no Claude-IDE lock,
- no update check and no credential vault,
- no `.mcp.json` written into any project.

The single stated exception is the credential-name read above.

Nothing here reaches the network **on its own**. `fetch-payload` obviously does —
but only because a person or a script named that download by id on the command
line, which is the opposite of the incidental background traffic the automated-run
rule exists to suppress. No other subcommand opens a connection.

## Platform notes

**Windows.** The editor is built as a **console-subsystem** executable
(`add_executable` with no `WIN32`), which is what makes stdout, stderr and the
exit code reach a caller. Flipping it to the GUI subsystem would silently kill
this whole surface: the process would detach from its console and every
subcommand would appear to print nothing.

**macOS.** The editor ships as `Orkige.app`; the executable to invoke is
`Orkige.app/Contents/MacOS/Orkige`. `open -a Orkige --args …` is *not*
equivalent — it detaches the process and discards the exit code.

## Tests

| Test | What it holds |
|------|---------------|
| `EditorCliTests` (unit) | the argument router: every subcommand, flag passthrough, and that an unknown word never reaches the window road |
| `editor_cli` (integration) | the real binary: the three exit codes, the refusals, an export that produces an artifact and prints its `OK` line — each within a deadline, because a subcommand that opened a window would hang rather than fail |
| `editor_bundle_native` (integration) | the distribution proof: a **copied** editor, in a clean room that denies the repository, the engine build tree and the vcpkg root, packages a project from a command line with no editor session running |
