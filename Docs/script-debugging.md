# Script editing & debugging

The editor embeds a Lua script EDITOR and a breakpoint DEBUGGER, built for
humans in the Script panel and for agents over MCP — one machinery, two
surfaces. Scripts open, highlight, complete and save inside the editor;
breakpoints pause the running game mid-statement with a call stack and live
locals; Continue/Step In/Step Over/Step Out walk the code like any debugger.

## At a glance

- **Script panel** (View > Panels > Script; closed by default): tabbed code
  editor over the open project's scripts. Opens on a script double-click in
  the Assets browser, from its own Open picker, and automatically on a
  debugger break-hit.
- **Editing is live**: Cmd/Ctrl+S writes the file; during Play the editor's
  scripts watcher hot-reloads the running game automatically (the same
  compile-before-swap reload a disk edit triggers — no second reload path).
- **Completion from the engine's own truth**: the generated Lua API index,
  the reflected scriptable-component registry (`self.*`, `world.*`,
  component properties), the live scripting state's globals (`engine:*` and
  friends) and the open document's identifiers. Triggered while typing and
  on Ctrl/Cmd+Space.
- **Breakpoints** are keyed (project-relative script path, 1-based line),
  persisted per project across sessions (`<project>/.orkige/breakpoints`,
  gitignored) and pushed to the running player live — set them before or
  during Play, from the gutter or over MCP.
- **A break pauses MID-STATEMENT** — a distinct state from the toolbar's
  frame-boundary Pause. The panel jumps to the hit line, shows the call
  stack and the locals/upvalues (tables expand on demand, bounded), and the
  debug toolbar drives Continue (`F5`), Step Over (`F10`), Step In (`F11`),
  Step Out (`Shift+F11`) — plus `Cmd/Ctrl+Alt+C/O/I/U` alternates where the
  F-row is awkward.
- **MCP parity**: `set_breakpoint` / `clear_breakpoint` /
  `list_breakpoints`, `get_debug_state` (the break-hit poll),
  `debug_continue` / `debug_step_*`, `get_locals` — the worked agent loop is
  in [mcp-workflows.md](mcp-workflows.md).
- **Honest refusals**: the browser player cannot block its main thread, so
  breakpoint sets refuse with one line there; `ORKIGE_SCRIPTING=OFF` builds
  refuse with the standard disabled error. A client that vanishes mid-break
  auto-resumes the game — never a wedged player.

## The Script panel (human workflow)

Open a script by double-clicking it in the Assets browser (the context menu
keeps "Open in External Editor" for the heavyweight-IDE path), or through the
panel's **Open** picker, which lists every project script. Each script is a
tab; an unsaved tab carries the dot marker, and closing one discards its
edits (logged to the Console — save first).

The editor renders in the system monospace font with Lua syntax highlight;
the palette follows the editor theme (dark/light). Find/replace, multi-cursor
editing, bracket matching and auto-indent come with the widget.

**Saving**: Cmd/Ctrl+S with the panel focused saves the ACTIVE SCRIPT (the
File menu's Save routes there too while the panel has focus; otherwise it
saves the scene as always). During Play a save lands on disk and the scripts
watcher hot-reloads the player within its poll interval — a broken edit
keeps the old instance running and surfaces a `[remote]` error, which the
panel also turns into a red line marker when it carries a `file:line`.

**Breakpoints**: click the gutter left of a line number to toggle; the red
dot is a breakpoint. The set belongs to the PROJECT (not the scene) and
survives editor restarts. Breakpoints work on every play target that shares
the loopback debug link (desktop, simulators, adb devices) — the browser
target refuses honestly.

**While paused at a break** the panel pulls itself up, focuses the hit file,
scrolls to the line (amber marker + gutter arrow) and shows two panes under
the code:

- **Call Stack** — innermost frame first; a host-call frame reads `[host]`.
  Clicking a frame selects it (locals switch to that frame) and jumps the
  editor to its line.
- **Locals** — the selected frame's locals and upvalues as name/value/scope
  rows. A table row expands on demand (each expansion is its own bounded
  request — the debugger never dumps whole object graphs).

The debug toolbar sits in the panel's header row while paused; the shortcuts
work editor-wide (they never type text, so they run even while the code
editor has keyboard focus).

## The runtime design

Everything above the scripting backend talks to
`core_script/ScriptRuntime`'s backend-neutral debug seam: a full-replace
breakpoint set, a break-state query (file/line/stack), resume/step commands
and a bounded frame/variable readback. The Lua machinery — the line hook,
call-stack walking, local/upvalue reads — lives behind it in the backend
implementation; the pure decision logic (chunk matching, the step state
machine) is `core_script/ScriptDebugCore.h`, unit-tested headlessly.

**Hook lifecycle** — zero overhead when idle. The script line hook is
installed only while at least one breakpoint is set or a step is pending;
clearing the last breakpoint removes it, so free-running scripts pay
nothing. Every script instance loads under its project-relative path as the
chunk name, so ONE breakpoint key matches every instance of a script, on
every machine.

**On a hit** the runtime blocks inside the hook and runs a bounded nested
pump: the player services the debug link (debugger commands and quit are
handled inline; every other editor command is DEFERRED to the next normal
frame drain, so nothing mutates the world mid-script), keeps the OS event
queue pumped so the window stays responsive, and paces itself. The editor is
notified once per break (`MSG_DEBUG_BREAK`, with the stack); resume/step
release the pump (`MSG_DEBUG_RESUMED`), and a landing step raises a fresh
break at its line.

**Steps** are call-depth based: Step In pauses at the very next executed
line wherever it is; Step Over pauses at the next line at the same or a
shallower depth (calls run through); Step Out pauses once the current
function returned.

**Pause semantics**: a break holds the player MID-FRAME, inside script
execution — the toolbar's Pause/Resume is a different, frame-boundary state
(`MSG_PAUSE`), and the two compose: a deferred pause applies at the frame
boundary after a resume. Streams freeze naturally while broken (the frame
never finishes); the debug link itself stays serviced.

**Session teardown is wedge-proof**: a client disconnect mid-break clears
the breakpoint set and auto-resumes; an editor Stop while broken quits
cleanly (the quit is handled inside the pump); the runtime with no pump
handler installed never blocks at all.

**Web player**: a browser page cannot block its main thread — a nested pump
would hang the tab — so `MSG_DEBUG_BREAKPOINTS` refuses there with one
honest line (the `screenshot`/`record_trace` precedent on the web target).
The wasm build compiles the same code; only the set is refused.

**Scripting off** (`ORKIGE_SCRIPTING=OFF`): everything compiles and every
debug operation reports the standard "scripting disabled" error.

## The MCP loop (agents)

Break-hit is asynchronous; `get_debug_state` is the poll:

1. `set_breakpoint {file: "scripts/player.lua", line: 42}` — before or
   during Play; `list_breakpoints` confirms the set.
2. `play {...}` (or keep the running session).
3. Poll `get_debug_state` until `paused_at_breakpoint` is `1`; read
   `file`/`line` and the `stack_*` lists.
4. `get_locals {frame: 0}` — a first call may answer `pending: 1`; call
   again. Expand one table with
   `get_locals {frame: 0, expand: ["self", "position"]}`.
5. `debug_step_over` (or `_in`/`_out`) — poll `get_debug_state` until
   `break_seq` advances past the reply's `prev_break_seq`.
6. `debug_continue` to run free; `clear_breakpoint {all: "1"}` +
   `debug_continue` to end the debugging pass.

The full worked walkthrough lives in
[mcp-workflows.md](mcp-workflows.md).

## Known v1 gaps

- **No eval-in-frame**: variables are read (locals/upvalues/table
  expansion), not written, and there is no expression evaluator at a paused
  frame. The live-tuning path remains `set_runtime_property`/`set_cvar`.
- **Main-thread scripts only**: the hook rides the one scripting state's
  main thread; a coroutine-driven script body would not pause (the engine's
  script surface runs no coroutines today).
- **Closing a dirty tab discards silently** (one Console line, no confirm
  modal); the quit flow does not yet ask about unsaved script tabs.
- **Breakpoints pause on the line's first instruction** — there are no
  conditional breakpoints or hit counts.
- **iOS hardware** plays standalone (no debug link over USB —
  `Docs/ios-signing.md`), so breakpoints cannot reach it; simulators and
  adb devices work through the shared loopback link unchanged.
