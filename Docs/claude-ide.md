# Claude IDE integration

The Orkige editor makes itself discoverable as an IDE to Anthropic's `claude`
CLI. When the integration is on, a `claude` session started from the editor's
embedded terminal auto-connects to the editor: the file/selection context flows
to claude, claude can open files in the embedded editor, and parse diagnostics
surface. This document is the protocol reference and the map from the protocol
to the editor implementation.

The protocol is **external and undocumented** — it is defined by the `claude`
CLI, not by Orkige, and may shift between CLI releases. The implementation is
pinned to the shape verified against the real binary (`claude` 2.x); the
`editor_ide_claude` ctest launches the actual CLI and asserts it still connects,
so a protocol change fails a test rather than silently breaking. Treat the field
and message shapes below as the current observed contract.

## Turning it on

Off by default — no lock file is written and no socket is opened unless asked:

* launch flag `--claude-ide`, or environment `ORKIGE_CLAUDE_IDE=1`.

It is never enabled by an automated/headless run (the same opt-in doctrine as
the `--mcp-port` control endpoint); the `editor_ide` selfcheck opts in
explicitly. With it on and running interactively, the editor:

1. opens a loopback (`127.0.0.1`) WebSocket endpoint on an ephemeral port;
2. writes the discovery lock `~/.claude/ide/<port>.lock`; and
3. seeds a `claude` spawned in the embedded terminal so it connects back.

## Discovery: the lock file

`claude` finds a running IDE by reading lock files under `~/.claude/ide/`. The
filename is the IDE's WebSocket **port**, and the JSON body carries:

```json
{
  "pid": 42480,
  "workspaceFolders": ["/path/to/project"],
  "ideName": "Orkige",
  "transport": "ws",
  "authToken": "39914d731f9a703d7d8bf46900bc713c"
}
```

* `pid` — the editor process id (used for stale-lock hygiene: a lock whose owner
  process is gone may be reclaimed; a live one is left alone).
* `workspaceFolders` — the open project root (empty in loose-scene mode). Kept
  current: the lock is rewritten when the open project changes.
* `ideName` — a display name (`Orkige`).
* `transport` — `ws` (WebSocket).
* `authToken` — a random 128-bit secret the WebSocket handshake must present.

The editor writes this file at owner-read/write permissions (the token is a
secret) and removes it on clean shutdown. The pure model + serializer/parser is
`OrkigeEditor::serializeIdeLock` / `parseIdeLock`
(`tools/editor/EditorIdeProtocol.h`).

## Connecting: env + WebSocket handshake

`claude` connects to a specific editor when it is launched with:

* `CLAUDE_CODE_SSE_PORT=<port>` — the IDE's WebSocket port; and
* `ENABLE_IDE_INTEGRATION=true`.

The embedded terminal seeds both into any shell it spawns (so `claude` started
there dials this editor). The IDE link is an **interactive-session** feature —
`claude -p` print mode does not open it; an interactive session (or `--ide`)
does.

The connection is a standard RFC 6455 WebSocket upgrade to the endpoint, with
one custom header carrying the lock's token:

```
x-claude-code-ide-authorization: <authToken>
```

A missing/wrong token is refused (`401`), so a local process that never read the
lock cannot reach the surface.

## The wire protocol: MCP over WebSocket

Once upgraded, the editor is an **MCP server** and `claude` is the MCP client.
Messages are JSON-RPC 2.0, one object per WebSocket text frame. The editor
handles:

* `initialize` → `{ protocolVersion, capabilities: { tools: {} }, serverInfo }`
* `tools/list` → the advertised tool descriptors
* `tools/call` → runs a tool, returns an MCP tool result
  (`{ content: [{ type: "text", text }], isError? }`)
* `prompts/list` → empty
* `notifications/*` and any id-less message → acknowledged, no reply

The editor pushes one notification to the client:

* `selection_changed` — `{ text, filePath, fileUrl, selection: { start, end,
  isEmpty } }` (line/character are 0-based) — sent whenever the focused
  document's caret/selection moves.

### Tools

Each tool's structured payload rides as a JSON string inside
`content[0].text` (the protocol's convention). Line/character values are 0-based
in ranges.

| Tool | Editor mapping |
|------|----------------|
| `getWorkspaceFolders` | the open project root(s) |
| `getOpenEditors` | the Script panel's open documents (path, active tab, dirty, languageId) |
| `getCurrentSelection` / `getLatestSelection` | the focused document's text selection |
| `getDiagnostics` | per-document parse diagnostics (`EditorTextDiagnostics`); optional `uri` filters to one file |
| `openFile` | opens the file in the embedded code editor (routes through the Script panel's open path) |
| `close_tab` | closes a matching clean document tab (a dirty tab is left alone) |
| `openDiff` | **not supported** — refuses honestly so claude applies the edit directly |

`openDiff` is the one gap. A proposed-change diff view (open before/after,
accept/reject) is a v2 feature; its natural backing is the editor's existing
per-line diff machinery (`EditorLineDiff` — the git gutter's hunk model). Until
then the tool returns an error result, which claude handles by applying the edit
to the file directly.

## Implementation map

* `tools/editor/EditorIdeProtocol.{h,cpp}` (in `orkige_editor_core`) — the PURE
  half: the lock model, the file-uri/languageId mappings, and every MCP result /
  notification builder. No sockets, no editor state; unit-tested
  (`EditorIdeProtocolTests`).
* `tools/editor/EditorIdeServer.{h,cpp}` (in `orkige_editor`) — the transport:
  the lock lifecycle, the loopback `HttpServer` WebSocket upgrade (auth-checked
  against the lock token), the per-frame client pump, and the tool dispatch onto
  live editor state.
* `core_debugnet/DebugSocket.{h,cpp}` — `WebSocketConnection`, the server-side
  message-framed WebSocket connection (the sibling of the debug link's
  line-framed `DebugLineConnection`), reusing the `WebSocketUtil` frame codec.
* `EditorState::ide` (`OrkigeEditor::IdeSharedState`) — the single-threaded
  bridge: the Script panel publishes the open documents / selection /
  diagnostics each frame and consumes the server's openFile / close_tab
  requests; the embedded terminal reads `ssePort` to seed `CLAUDE_CODE_SSE_PORT`.

The endpoint binds loopback only and is a separate instance from the MCP control
endpoint (`--mcp-port`) and the Play-in-Browser static server.

## Tests

* `EditorIdeProtocolTests` (unit) — the lock round-trip and stale-overwrite
  rule, uri/languageId mappings, the tool-result/notification JSON shapes, the
  selection diff, and a real captured lock body (interop).
* `editor_ide` (integration, both flavors) — a fake IDE client drives the real
  server over a loopback socket: the WebSocket handshake with the lock token
  (plus a bad-token rejection), then `initialize`, `getWorkspaceFolders`,
  `getOpenEditors`, `getDiagnostics`, and an `openFile` that must land its
  target in the Script panel.
* `editor_ide_claude` (integration, `device`-labeled — full-suite only) — the
  **drift alarm**: it launches the real `claude` CLI pointed at the editor's own
  lock/port and asserts it connects to the endpoint. It skips (exit 77) when
  `claude` is not on PATH or an interactive session cannot be established
  headlessly; a connection is the assertion that the external protocol still
  matches the live binary.
