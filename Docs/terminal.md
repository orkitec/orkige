# Embedded terminal

The editor hosts real terminals as **dockable windows**: terminal-based agents
(Claude Code and others) and plain shells each run in their own window, sibling
tabs in the bottom dock group beside Console/Assets/Source Control. It is
desktop- and editor-only. Each window is one **session**, titled with what is
running inside it. This is the code editor's one-window-per-open-file pattern.

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
- Closed by default; open a terminal from **View ▸ Terminal** (or **View ▸ New
  Terminal**). Each session window first-appearance-docks into the bottom group;
  re-dock freely afterwards.

## Multiple windows

Every session is its own top-level dockable window, so several agents sit as
sibling tabs a click apart:

- **New Terminal** — **View ▸ New Terminal**, the **`+`** button in any terminal
  window's header, or a terminal tab's right-click **New Terminal** — spawns
  another session: the same login shell, in the same project working directory,
  inheriting the same MCP environment. Every session drains its pty every frame
  (bounded), so a backgrounded agent keeps running while another window is on top.
- Opening **View ▸ Terminal** with no sessions spawns one; the menu item mirrors
  "at least one terminal window is open", and the last window closing clears it.
- Each window's **close (×)** (or the tab's right-click **Close**) closes the
  session. Closing one whose child is still running asks for confirmation first,
  then terminates the whole child process tree; a window whose shell already
  exited closes without a prompt.
- The session count and order are **not persisted** across editor runs (v1): a
  fresh launch opens with a single shell.

## App-aware window titles

A window's tab shows **what is running** inside it, from two signals — the title
wins:

- the **terminal title** the running program sets over an OSC sequence
  (`ESC ] 0 ; text BEL` / `ESC ] 2 ; text ST`) — shells (a login `fish`) and
  TUIs (`claude`, `vim`) announce themselves this way. It is surfaced through
  `EditorTerminalScreen::getTitle()` (a plain string; the VT library type never
  leaves the `.cpp`);
- the **foreground process name** as a fallback, polled at ~1 Hz (never per
  frame): the pty's `tcgetpgrp` foreground group leader, named via libproc on
  macOS and `/proc/<pid>/comm` on Linux. Windows returns nothing here (the title
  covers the agent TUIs); the shell's own path is the floor.

The title behind a stable `###terminal<id>` (so a relabel keeps the window +
docking identity while the visible part updates live) is a **cleaned** title: a
path or a path-prefixed command line is trimmed to its leading app word
(`/Users/me/dev/orkige` → `orkige`, `/opt/homebrew/bin/fish -l` → `fish`), a
plain title passes through verbatim, and with neither signal the tab reads
`Terminal N`. Because a dock-tab title renders in the default UI font (which
carries the icon/badge glyphs), it identifies its tenant crisply.

### Agent badges

A recognised **agent CLI** (`claude`, `codex`, `opencode`, `aider`, `gemini` — a
case-insensitive prefix match on the detected name) draws a small **generated
badge** in front of the title; every other session draws the plain terminal
glyph. The badge is a runtime-rasterised mark baked into the UI-font atlas under
a private-use codepoint (one per agent): Claude a coral radiating-asterisk, Codex
a monochrome interlocking-ring, and the rest a signature-tinted rounded square
carrying the program's initial. The marks are generated procedurally (parametric
strokes, never traced artwork) and render **exclusively to identify the
third-party program** running in that session — the OS dock-icon precedent; they
are not product logos and appear nowhere else. The match list names programs the
user runs — the badge and label the user sees are always runtime data from the
session, never a hardcoded product string. A later runtime vendor-icon discovery
could replace a badge's pixels in the same atlas rect with no other change.

## MCP auto-wiring

When the editor runs with its MCP endpoint enabled (`--mcp-port` / a token
file), the terminal is the fast path to an agent that controls the very editor
it lives in:

- the spawned shell's environment carries `ORKIGE_MCP_URL`
  (`http://127.0.0.1:<port>/mcp`) and, when a token file is set,
  `ORKIGE_MCP_TOKEN_FILE`;
- each session's header carries a compact **Connect** button that copies the
  ready-made connect command
  (`claude mcp add --transport http orkige $ORKIGE_MCP_URL --header
  "Authorization: Bearer $(cat "$ORKIGE_MCP_TOKEN_FILE")"`) to the clipboard; the
  full text is in its tooltip.

Start Claude Code in the terminal, paste the command, and it is registered
against the running editor. When the MCP endpoint is off there is no env and no
hint — honest silence.

The terminal itself is DELIBERATELY not exposed over MCP. A headless agent
spawning shells in the editor UI is out of scope, and an MCP tool for it would
launder that boundary; agents drive the editor through the MCP verbs directly.

## Input

While a terminal window holds keyboard focus every key goes to the child, and
the editor's own global shortcuts stand down for the frame (the same way a
focused code editor swallows them):

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

## Copy and paste

Copy/paste go through the **OS pasteboard** (via SDL), so text moves between the
terminal and any other application:

- **Copy** (`Cmd+C` on macOS, `Ctrl+Shift+C` elsewhere) writes the drag-selection
  to the clipboard. With no selection the copy chord is a no-op — on macOS
  `Ctrl+C` remains the interrupt (SIGINT), and `Cmd+C` copies.
- **Paste** (`Cmd+V` / `Ctrl+Shift+V`) writes the clipboard to the child. It
  works with plain `Cmd/Ctrl+V` regardless of the app; the bracketed-paste
  framing (`ESC [ 200~ … ESC [ 201~`) is added only when the app enabled DEC
  mode 2004.

The editor wires ImGui's clipboard to SDL globally, so every panel's and text
field's copy/paste reaches the OS pasteboard too (not just an in-process buffer).

## Query replies

A conforming terminal answers the queries apps send it — Primary Device
Attributes (`ESC [ c`), device-status / cursor-position reports (`ESC [ 5n` /
`ESC [ 6n`) and the like — on the input channel. The VT core generates those
replies; the panel forwards the emitted bytes back into the pty's input
(`EditorTerminalScreen::setResponder`). Without this a shell that probes the
terminal at startup (e.g. `fish`) stalls a couple of seconds waiting for a
Primary DA answer and then disables features, and query-driven TUI renderers
degrade.

## Fonts and TUI glyphs

The grid renders in the mono font, whose atlas bakes the terminal glyph blocks —
Box Drawing, Block Elements, Geometric Shapes, General Punctuation (ellipsis,
dashes), Arrows, Braille Patterns (spinner frames), Dingbats and Miscellaneous
Symbols — so box UIs, progress bars and braille spinners render instead of `?`.
Because the editor uploads one static atlas, the blocks are requested up front.
The system mono fonts cover only part of these (no macOS mono ships Braille), so
the bundled **DejaVu Sans** is merged as a symbols fallback: merged glyphs never
override the primary font's, so box/block art stays cell-crisp and the fallback
only fills the holes (braille above all). A codepoint neither font carries (a
handful of emoji-tier dingbats, e.g. `U+2728 SPARKLES`) draws as **blank**, not a
`?`.

## Architecture

Three seams, each pure and testable, with libvterm confined to one file:

- `EditorTerminalScreen` — the VT screen model: bytes in → cell grid + cursor
  out, plus a reply channel out (`setResponder`, the query-answer bytes) and the
  app-announced **title** out (`getTitle` / `setTitleChanged`). The header speaks
  only the editor's cell/grid vocabulary, a plain byte-sink responder and a
  string title; the parsing is **libvterm**, quarantined in the `.cpp` (overlay
  port `ports/libvterm`, see `Docs/ports.md`). Keeping the VT core behind this
  seam makes it swappable. Unit-tested headlessly with scripted escape sequences
  (`EditorTerminalScreenTests`, including the DA / cursor-position replies and
  the OSC title).
- `EditorTerminalKeys` — the pure key → VT byte-sequence encoder, unit-tested as
  a table (`EditorTerminalKeysTests`).
- `EditorTerminalSession` — the pure, UI-free session bookkeeping: title
  cleaning, the agent classifier, tab-label composition (title vs process-name,
  agent detection), the post-close active index, the pure mouse→cell hit test
  (`terminalCellAtPoint`, which the drag-selection shares so a drag past the
  visible rows still extends deterministically) and the **agent-badge maker**
  (per-agent private-use codepoint + signature tint + the deterministic
  `terminalAgentBadgePixels` generator). Unit-tested headlessly
  (`EditorTerminalSessionTests`). The atlas wiring —
  `EditorTheme::bakeTerminalAgentBadges` reserving one UI-font custom-rect glyph
  per agent and filling it from the generator — is the only ImGui-side piece.
- `EditorTerminalKeys` — the pure key → VT byte-sequence encoder, unit-tested as
  a table (`EditorTerminalKeysTests`).
- `EditorTerminalPty` — the OS pty seam. POSIX uses `openpty` + `fork`/`exec`
  with the child in its own session, so closing a window signals the whole
  process group; it also names its **foreground process** (`tcgetpgrp` +
  libproc / `/proc`) for the window title. Windows uses **ConPTY**
  (`CreatePseudoConsole` + `CreateProcess` with the pseudoconsole attribute)
  inside a **Job Object** so closing a window kills the child tree; UTF-8
  throughout. Everything above this seam is OS-agnostic.

Output floods degrade gracefully: reads are bounded per frame (64 KB) so the UI
never stalls. Closing a window or quitting the editor terminates the child.

## Verification

- `EditorTerminalScreenTests` / `EditorTerminalKeysTests` /
  `EditorTerminalSessionTests` (unit): the last covers title cleaning, the agent
  classifier, tab-label composition, the post-close active index, the mouse→cell
  hit test, the glyph codepoints falling in the icon atlas ranges, and the agent
  badges (distinct private-use codepoints, non-black signature tints, the mark
  stroke counts, and badge pixels that are non-empty, tinted and deterministic).
- `editor_terminal` selfcheck (both flavors, and Windows CI via ConPTY): spawns
  a real pty running a scripted echo, asserts the grid seam (printed text + SGR
  colour reach the cells), types a known word through the input seam and asserts
  the child echoed it back, drives the **paste** seam (encoding + the pasted
  bytes reach the pty and echo), the **copy** seam (selection text + the OS
  clipboard round trip via SDL, with the user's clipboard saved and restored so a
  test run leaves nothing behind), the **reply** channel (DA / cursor-position
  answered), the **font coverage** (the mono atlas bakes box drawing, block,
  arrows, geometric shapes, ellipsis, the merged braille spinner and the six
  agent-badge private-use glyphs) and the **multiple-session** seam (two
  independent grids, an OSC title on session B detected as the Claude agent with
  its badge codepoint, and a close that kills one child and shrinks the list),
  then closes and asserts the child died. Skips (exit 77) where no pty/shell is
  available; individual legs skip honestly where SDL video, a system mono font or
  a second pty is absent.

## v1 limits

- Resizing the panel re-flows the live grid but NOT the retained scrollback
  (older pushed-off lines keep their old width).
- Mouse reporting to the app is not forwarded (the app's mouse-tracking request
  is detected but only affects nothing yet); selection is the mouse's job.
- Glyph width follows the mono font's coverage; a wide glyph spans two cells but
  complex grapheme widths are approximate. Colour emoji and CJK render only where
  the merged fonts carry the glyph; a codepoint neither carries draws blank.
- The session count and order are not persisted across editor runs.
- The foreground-process-name fallback is POSIX-only; on Windows a session with
  no OSC title reads by its numbered fallback (the agent TUIs set a title).
