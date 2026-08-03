# Security posture

Orkige is developed and driven by AI agents as much as by people: an agent
controls the editor over the MCP endpoint, writes project files, imports assets,
and runs scripts. This document is the engine-wide security posture that setting
creates — the threat model and the guarantees that answer it. Each guarantee is
enforced in code with tests; this page is the map, the linked docs are the
detail.

## Threat model

The adversary is not (only) a remote attacker — it is **untrusted input reaching
a trusted, automated operator**:

- **An agent with control.** The MCP endpoint grants its client the full editor:
  scene authoring, project-file read/write, asset import, script execution, play.
  Whatever the agent can be told to do, the endpoint can be made to do.
- **Untrusted content.** A scene, script, asset, or pak/zip an agent handles may
  come from anywhere (fetched off the web, copied from a sample). Opening it must
  not become code execution or a filesystem escape.
- **A shared machine.** The control and debug ports live on a developer or CI
  machine alongside other processes and, potentially, other users on the network.

The three surfaces below each close one leg of that model. None assumes the agent
is malicious — they assume the agent is *powerful* and the content it touches is
*unvetted*, which is the same defensive requirement.

## 1. The control + debug ports are local and authenticated

The editor's MCP server and the player debug link **bind loopback (127.0.0.1)
by default** — no process off the machine can reach them. Exposing a non-loopback
interface is an explicit, logged opt-in (`--mcp-bind` / `--debug-bind` and their
env aliases), never the default. When a token file is configured **every** verb —
reads included, not just mutations — requires the `Authorization: Bearer` token,
so an unauthenticated peer cannot even enumerate project structure or read source
over the socket; the no-token path stays open only as a local dev convenience.
The token is compared in **constant time** (`core_util/ConstantTimeCompare.h`), so
a match cannot be recovered byte-by-byte through reply latency. Detail:
[mcp.md § Security posture](mcp.md#security-posture).

The token is minted from the platform's entropy source
(`core_util/SecretToken.h`) and never from a seeded engine whose other outputs
are published, and every file that carries it is written **owner-only**.

#### Files that carry a secret

Three files quote the live endpoint's token — the MCP token file
(`mcp-endpoint.token`), the per-project discovery file `<projectRoot>/.mcp.json`,
and the Claude-IDE lock `~/.claude/ide/<port>.lock` — and a fourth, the
per-project build-credential file, carries signing settings. All four go through
one sink, `core_filesystem/FileWriter`'s owner-only road, and the **sequence** is
the guarantee: the file is created empty and exclusively, restricted while it
still holds nothing, written, and then renamed onto its target. So the secret
never exists in a file another account can open — not even for the instant
between a write and a restriction applied afterwards.

| Platform | The restriction |
|---|---|
| **macOS**, **Linux** | POSIX mode bits, enforced by the kernel: the file is `0600` |
| **Windows** | a **protected** (non-inheriting) DACL granting the current token's owner and SYSTEM, and naming nobody else — written through the platform's own security API, since access control there is an ACL the standard filesystem library cannot express |

A volume that can hold neither (FAT/exFAT, many network mounts) gets the file
plus **one honest warning**: refusing would trade a defence-in-depth control for
a broken feature, and on the token file it would degrade to auth-off, which is
strictly worse than a permissive file.

What this buys is bounded, and the bound is worth stating: **a file ACL stops
another account, never code already running as this user.** Malware with the
user's own identity reads the token exactly as the editor does. What limits that
exposure is the token's lifetime — it is minted per session, on an ephemeral
port, and the file is removed on shutdown — not the mode bits.

## 2. Every path boundary is jailed

File authoring over MCP (`write_project_file`, `read_project_file`,
`list_project_files`, `import_asset`) is **confined to the open project's root**,
and pak/zip mounting is hardened against **zip-slip**. Both go through one pure
guard, `core_util/PathJail`: a requested or archive-entry path is normalized and
canonicalized, and its containment under the root is verified before any I/O — an
absolute path, a `..` traversal (even one that only escapes after normalization),
and a symlink component that resolves out of root are all refused with an honest
error, nothing written or resolved. Legitimate nested paths still work. Detail:
[filesystem.md § Security: zip-slip + the path jail](filesystem.md#security-zip-slip--the-path-jail).

## 3. A scene is content, not code

Game scripts (`ScriptComponent`) and editor scripts run in a Lua sandbox over an
**allowlist**: the pure-computation stdlib (`math`, `string`, `table`, and the
base helpers) and the sanctioned engine tables are exposed; the capability
globals are denied — `require`/`package` (module loading), `load`/`loadstring`/
`loadfile`/`dofile` (compile-and-run arbitrary source or read-and-run a file),
`io` (raw files), `debug` (the reflection/hook library), and all of `os` beyond a
read-only `time`/`clock`/`date` subset. So a scene attached from an untrusted
source cannot read the filesystem, spawn a process, or load code — loading it is
loading data. What a script does get is `data`: reading authored content files by
project-relative name (jailed by the same `PathJail` predicate, size-capped,
resolved only through the mounted content and never through `fopen`). It grants
strictly less than `io` — no writes, no handles, no path outside the project —
and reading a file is not running it, so the code-loading globals stay denied.

`coroutine` is the one library that is opened and then **replaced** rather than
denied, and the distinction is worth stating: it carries no capability at all -
no file, no process, no code loading - so nothing about coroutines is unsafe to
compute with. What a script-created coroutine does carry is the **resume point**,
and a resume landing inside a physics contact callback or an event dispatch
re-enters the world in the middle of an update. That is a correctness boundary,
not a security one, and the engine holds it the same way it holds the tick order:
`coroutine.yield` is captured into the engine's three wait functions, the raw
table is dropped, and the only way to suspend game code is a task the engine owns
and resumes at exactly one point in the frame. A script therefore gets the
expressive power and none of the reentrancy.
Detail: [lua-api.md § Sandbox / security](lua-api.md#sandbox--security).

## 4. Outbound requests are https-first, and not an agent's tool

The HTTP client is the one path out of the process. Its defaults are the safe
ones and every relaxation is explicit at the call site: certificates verified
against the PLATFORM's trust store (never a CA bundle the engine ships and lets
rot), https unless the caller opts into a plain-http URL, no https-to-http
redirect ever, http/https only, no credentials in URLs, no header injection, no
ambient cookie jar or credential store, and a per-request timeout and response
cap that always apply. A refusal reports a reason; a failure logs the URL and
that reason and never a body or a header, because a request may carry a token.
The rules are pure code (`core_http/HttpPolicy`) so every platform backend
applies the identical decision.

The MCP endpoint exposes **no** HTTP verb, deliberately. It gives an agent
control of the editor, not a general network egress path with the editor's
credentials inside whatever network the editor can reach — the same line that
keeps git mutations off MCP. An agent that needs a game to call a server writes
that into the game's Lua, where it is visible in the project and in a diff.
Detail: [http.md](http.md).

## What this does *not* claim

Honesty is part of the posture — these are the known limits, not hidden gaps:

- **Script isolation is capability-based, not memory-based.** The sandboxes share
  one Lua state, so a script can still write the shared globals table (`_G` /
  `rawset`) and grief a sibling script. It cannot *reconstruct* a denied
  capability — `load`/`require`/`io`/full `os` are gone — so this is an isolation
  weakness, not a filesystem/process/RCE escape.
- **No-token dev mode is unauthenticated by design.** Running the control port
  with no token file leaves reads and mutations open; that is a local-only
  convenience and the loopback bind is what protects it. A shared or exposed host
  should configure a token.
- **A sandboxed script CAN reach the network.** The `http` table is a sanctioned
  engine capability, so a scene from an untrusted source can make outbound
  requests — it cannot read the filesystem or load code, but it can talk to a
  server the machine can reach. That is a deliberate feature (games need
  leaderboards and remote config), not a hole in the sandbox; a host that must
  not allow it builds with `ORKIGE_HTTP=OFF`, where the table refuses honestly.
- **The engine trusts its own compiled game modules.** Native project modules
  (`projects/*-native/`) are compiled C++ linked into the player — they are code,
  not sandboxed content, and are outside this model by construction.

## Related hardening

Not part of the input-trust model above, but part of staying safe under an
autonomous operator: the fatal-signal **crash marker** + boot detection and the
always-on **breadcrumbs** trail (a hard crash leaves a readable cause), the
**boot/teardown cycling test** and **nightly soak** that catch lifetime and leak
faults before a user does ([soak.md](soak.md)), and the **sanitizer gates** —
ASan/UBSan and ThreadSanitizer ([sanitizers.md](sanitizers.md)) — that catch
memory and data-race faults in CI, and the **supply-chain provenance** of the
third-party code that parses that untrusted content — every dependency pinned,
the single-file asset parsers tracked, the CI actions SHA-pinned
([vendored-libs.md](vendored-libs.md)).
