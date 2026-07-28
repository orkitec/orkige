# Embedded terminal

The editor hosts a real terminal in a dockable **Terminal** panel: terminal-based
agents (Claude Code and others) and plain shells run inside a dock tab, beside
Console/Assets/Source Control. It is desktop- and editor-only.

## What it is

- A pseudo-terminal running the user's **login shell** (`$SHELL -l` on POSIX,
  powershell on Windows). The login shell matters: a distributed macOS `.app`
  bundle inherits a skinny `PATH`, and `-l` restores the full one so `git`,
  `claude` and other user tools resolve. The working directory is the open
  project's root (the home directory when no project is open).
- The child renders into a mono-font character grid with per-cell foreground/
  background colour (indexed, 256-colour and 24-bit truecolour), a block cursor,
  bounded **scrollback** (5000 lines) with mouse-wheel scroll, drag selection
  with copy, and paste (bracketed when the app enabled it).
- Closed by default; open it from **View ▸ Terminal**. It docks as a tab in the
  bottom group. Fresh layouts and pre-existing `imgui.ini` users both get it
  docked there (a first-appearance dock anchored to the bottom group); re-dock
  freely afterwards.

## MCP auto-wiring

When the editor runs with its MCP endpoint enabled (`--mcp-port` / a token
file), the terminal is the fast path to an agent that controls the very editor
it lives in:

- the spawned shell's environment carries `ORKIGE_MCP_URL`
  (`http://127.0.0.1:<port>/mcp`) and, when a token file is set,
  `ORKIGE_MCP_TOKEN_FILE`;
- the panel shows a one-line, copyable connect command:
  `claude mcp add --transport http orkige $ORKIGE_MCP_URL --header
  "Authorization: Bearer $(cat "$ORKIGE_MCP_TOKEN_FILE")"`.

Start Claude Code in the terminal, paste the command, and it is registered
against the running editor. When the MCP endpoint is off there is no env and no
hint — honest silence.

The terminal itself is DELIBERATELY not exposed over MCP. A headless agent
spawning shells in the editor UI is out of scope, and an MCP tool for it would
launder that boundary; agents drive the editor through the MCP verbs directly.

## Input

While the panel holds keyboard focus every key goes to the child, and the
editor's own global shortcuts stand down for the frame (the same way a focused
code editor swallows them):

- printable text rides the platform IME/text-input path (UTF-8), so composed and
  non-ASCII input is correct;
- arrows, Home/End, Page Up/Down, Insert/Delete, function keys, Tab, Enter,
  Backspace and Escape are encoded to their xterm sequences by a pure key
  encoder (`EditorTerminalKeys`), honouring the app's DECCKM (application cursor
  keys) mode;
- `Ctrl`+letter sends the C0 control code (`Ctrl+C` → `0x03`, the interrupt).
  On macOS `Cmd` stays the editor's copy/paste modifier; elsewhere copy/paste
  are `Ctrl+Shift+C` / `Ctrl+Shift+V`, leaving `Ctrl+C`/`Ctrl+V` as control
  codes.

## Architecture

Three seams, each pure and testable, with libvterm confined to one file:

- `EditorTerminalScreen` — the VT screen model: bytes in → cell grid + cursor
  out. The header speaks only the editor's cell/grid vocabulary; the parsing is
  **libvterm**, quarantined in the `.cpp` (overlay port `ports/libvterm`, see
  `Docs/ports.md`). Keeping the VT core behind this seam makes it swappable.
  Unit-tested headlessly with scripted escape sequences
  (`EditorTerminalScreenTests`).
- `EditorTerminalKeys` — the pure key → VT byte-sequence encoder, unit-tested as
  a table (`EditorTerminalKeysTests`).
- `EditorTerminalPty` — the OS pty seam. POSIX uses `openpty` + `fork`/`exec`
  with the child in its own session, so closing the panel signals the whole
  process group. Windows uses **ConPTY** (`CreatePseudoConsole` +
  `CreateProcess` with the pseudoconsole attribute) inside a **Job Object** so
  closing the panel kills the child tree; UTF-8 throughout. Everything above
  this seam is OS-agnostic.

Output floods degrade gracefully: reads are bounded per frame (64 KB) so the UI
never stalls. Closing the panel or quitting the editor terminates the child.

## Verification

- `EditorTerminalScreenTests` / `EditorTerminalKeysTests` (unit).
- `editor_terminal` selfcheck (both flavors, and Windows CI via ConPTY): spawns
  a real pty running a scripted echo, asserts the grid seam (printed text + SGR
  colour reach the cells), types a known word through the input seam and asserts
  the child echoed it back, then closes and asserts the child died. Skips
  (exit 77) where no pty/shell is available.

## v1 limits

- Resizing the panel re-flows the live grid but NOT the retained scrollback
  (older pushed-off lines keep their old width).
- Mouse reporting to the app is not forwarded (the app's mouse-tracking request
  is detected but only affects nothing yet); selection is the mouse's job.
- Box-drawing and emoji width follow the mono font's coverage; a wide glyph
  spans two cells but complex grapheme widths are approximate.
- One session at a time.
