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
loading data. Detail:
[lua-api.md § Sandbox / security](lua-api.md#sandbox--security).

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
memory and data-race faults in CI.
