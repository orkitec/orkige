# Script editing & debugging

The editor embeds a Lua script EDITOR and a breakpoint DEBUGGER, built for
humans in the code editor and Debug panel and for agents over MCP — one
machinery, two surfaces. Scripts open, highlight, complete and save inside the
editor; breakpoints pause the running game mid-statement with a call stack and
live locals; Continue/Step In/Step Over/Step Out walk the code like any debugger.

The API those scripts program against — `self`, `world`, `engine`, the global
tables — is [lua-api.md](lua-api.md); the completion popup here is generated
from the same registry that page documents.

## At a glance

- **A window per open file**: each script/text file opens as its OWN docked
  window (title = file name, dirty marker, stable id) so several open files
  read as sibling tabs in one dock node, like every other panel. A file opens
  on double-click in the Assets browser, from the Inspector's **Open in
  Internal Editor** button, and automatically on a debugger break-hit. Syntax
  highlight follows the file kind (Lua, JSON, Markdown, an XML definition for
  the engine's XMLArchive carriers — `.oscene`/`.oprefab`/`.orkproj`/
  `.orkmeta`/`.olevels`/`.oactions`/`.olayers` — plus XLIFF `.xlf` and a bare
  `.xml`, a config-text definition for the line-based `.oui`/`.ogui`/`.omat`/
  `.oshape` formats, plain text for anything else — sniffing the CONTENT of an
  unrecognized extension for an XML/JSON shape before giving up); completion
  and the breakpoint gutter are Lua-only. Live parse diagnostics (the gutter's
  red "!" badge) run for Lua, every XML-family kind and `.omat`/`.oui` (each
  wraps its own pure parser); `.ogui`/`.oshape`/JSON stay highlight-only —
  their parsers report no line-numbered error to show.
- **Debug panel** (View > Panels > Debug; closed by default): the debugger's
  transport (Continue / Step In / Over / Out), call-stack pane and
  locals/upvalues pane. Docks in the bottom group beside Console and
  auto-opens/focuses on a break-hit.
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
  frame-boundary Pause. The hit file's window is raised and scrolled to the
  line; the Debug panel shows the call stack and the locals/upvalues (tables
  expand on demand, bounded) and its transport drives Continue (`F5`), Step
  Over (`F10`), Step In (`F11`), Step Out (`Shift+F11`) — plus
  `Cmd/Ctrl+Alt+C/O/I/U` alternates where the F-row is awkward.
- **Break on Next Statement** (`Cmd/Ctrl+Alt+B`, the circle-pause glyph at the
  left of the Debug transport): a one-shot break with NO breakpoint needed —
  it pauses into the first script line the game runs next, wherever that is
  (the "where does code even run right now?" move). The one transport control
  enabled while the session is *running or frame-paused* rather than broken;
  arming it while frame-paused persists the arm until the sim resumes and the
  first line then catches. The hit is a normal break — stack, locals and the
  step/continue transport take over unchanged.
- **Break on Errors** (the persisted checkbox in the Debug transport row): when
  armed, an uncaught runtime Lua error PAUSES the game AT the error — the editor
  jumps to the erroring `file:line`, the Debug panel shows the crash's real stack
  + locals and the error message prominently (in the error colour, distinct from
  a breakpoint pause). On **Continue** the normal error semantics proceed
  unchanged (the instance disables itself + reports over the Console) — arming
  only DEFERS the honest failure, never replaces it. Off by default = the normal
  behavior. Pushed to a running player on connect + on toggle, like the
  breakpoint set.
- **MCP parity**: `set_breakpoint` / `clear_breakpoint` /
  `list_breakpoints`, `get_debug_state` (the break-hit poll; `break_error` on an
  error break), `debug_continue` / `debug_step_*`, `debug_break_next` (break on
  next statement — no breakpoint needed), `set_break_on_errors` (break on script
  errors), `get_locals` — the worked agent loop is
  in [mcp-workflows.md](mcp-workflows.md).
- **Honest refusals**: the browser player cannot block its main thread, so
  breakpoint sets refuse with one line there; `ORKIGE_SCRIPTING=OFF` builds
  refuse with the standard disabled error. A client that vanishes mid-break
  auto-resumes the game — never a wedged player.

## The code editor (human workflow)

Open a file by double-clicking it in the Assets browser (text formats —
`.lua`, `.ogui`, `.omat`, `.oshape`, `.oactions`, `.olayers`, `.olevels`,
`.xlf`, `.xml`, `.txt`, `.md`, `.json` — open in the embedded editor;
`.oscene` opens the scene, `.oprefab` its edit stage, `.oui` the GUI
Preview, images/audio their defaults), or from the Inspector's **Open in
Internal Editor** button (reachable for every file, including `.oui`'s text
source — the neighbouring **Open in External Editor** keeps the
heavyweight-IDE path, as does the Assets context menu). Each file becomes
its OWN docked window whose tab is the
file name; an unsaved window carries the dot marker, and closing one discards
its edits (logged to the Console — save first). Opening an already-open file
focuses its window. Multiple open files dock as sibling tabs in one node.

The editor renders in the system monospace font; syntax highlight follows the
file kind (Lua, JSON, Markdown, an XML definition for the engine's
XMLArchive/XLIFF formats, a config-text definition for `.oui`/`.ogui`/
`.omat`/`.oshape`, plain text otherwise — an unrecognized extension sniffs
its content for an XML/JSON shape first), and the palette follows the editor theme
(dark/light). Find/replace, multi-cursor editing, bracket matching and
auto-indent come with the widget. The mouse shows the text I-beam over the
code area.

**Saving**: Cmd/Ctrl+S with a document window focused saves THAT file (the
File menu's Save routes there too while a document has focus; otherwise it
saves the scene as always). During Play a save lands on disk and the scripts
watcher hot-reloads the player within its poll interval — a broken edit
keeps the old instance running and surfaces a `[remote]` error, which the
document also turns into a red line marker when it carries a `file:line`.

**Change markers**: the gutter's far-left edge carries per-line git change
bars versus the file's index baseline — a green bar for an added line, a blue
bar for a modified line, and a small red triangle where a run of lines was
deleted. The baseline is `git show :<path>` (the staged blob), fetched when the
file opens and on every save (never per keystroke); the live buffer is diffed
against it as you type, refreshed after a short idle pause. A header-row toggle
(the code-compare glyph) turns the markers on and off, persisted in the View
settings (`script_git_markers`, default on). The markers sit clear of the line
numbers, the breakpoint dot and the error `!`.

**Inspecting a hunk**: hover a change marker to see the hunk's index version —
its original lines on a red band and, for a modified hunk, the current lines on
a green band, a compact before/after in the editor font. A click PINS the popup
(Esc, its close box, or a click-away dismisses it); a long hunk clamps to the
first 20 lines with an honest `... N more`. The pinned popup carries a **Revert
Hunk** button that swaps the hunk's current lines back to the index version IN
THE BUFFER — one undoable step on the editor's own undo stack (Cmd/Ctrl+Z puts
the edit back), never touching disk (save as usual). After a revert the diff
recomputes and the marker disappears.

**Navigating changes**: Cmd/Ctrl+Alt+Down jumps to the next change hunk,
Cmd/Ctrl+Alt+Up to the previous one, wrapping around the ends and scrolling the
hit hunk to the middle of the view (bare Alt+arrows stay the editor's move-line).

Honest degradations: a file outside a git repository, an untracked file, or a
machine with no `git` on `PATH` (a distributed editor) shows no markers — an
untracked file's all-added spam would help nobody. A buffer past ~20k lines
skips the live per-edit diff and refreshes its markers only on save.

**Breakpoints** (Lua documents only): click the gutter left of a line number
to toggle; the red dot is a breakpoint. The set belongs to the PROJECT (not
the scene) and survives editor restarts. Breakpoints work on every play target
that shares the loopback debug link (desktop, simulators, adb devices) — the
browser target refuses honestly.

**While paused at a break** the hit file's window is raised and scrolled to
the line (amber marker + gutter arrow) and the **Debug panel** opens/focuses
with two panes:

- **Call Stack** — innermost frame first; a host-call frame reads `[host]`.
  Clicking a frame selects it (locals switch to that frame) and jumps the
  code editor to its line.
- **Locals** — the selected frame's locals and upvalues as name/value/scope
  rows. A table row expands on demand (each expansion is its own bounded
  request — the debugger never dumps whole object graphs).

The transport (Break on Next Statement / Continue / Step In / Over / Out,
FontAwesome glyphs) sits at the top of the Debug panel; its keyboard shortcuts
work editor-wide (they never type text, so they run even while the code editor
has keyboard focus). Break on Next Statement (`Cmd/Ctrl+Alt+B`) is the one
control enabled while the session is *running or frame-paused* rather than
broken — it arms a one-shot next-line break so you can pause into wherever the
scripts run without a breakpoint (see At a glance). A **Break on Errors**
checkbox sits beside the transport (persisted in `ViewSettings`); armed, a
runtime script error pauses the game at the fault with its message shown in the
error colour, and while broken-from-error the panel headlines the crash text
above the stack/locals.

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

**Break on Next Statement** (`ScriptRuntime::debugBreakNext`,
`MSG_DEBUG_BREAK_NEXT`) reuses the Step-In machinery from an UNBROKEN state:
it arms a `ScriptStepMode::In` (which breaks on the next line at any depth —
`ScriptDebugCore::breakNextStepMode`) and installs the hook if none was
running, so the next line in ANY instance is a normal break hit reported over
the same `MSG_DEBUG_BREAK` path. The arm carries no cost once it fires (the
hook drops itself when no breakpoint/step remains) or when `debugDetach`
cancels it. **Frame-pause interplay**: a frame-paused runtime is not ticking
scripts, so an arm placed then simply persists — the first line executed once
the sim resumes catches it (the pump is installed on the arm even when no
breakpoint was ever set, so the hit still reports).

**Break on Errors** (`ScriptRuntime::setDebugBreakOnErrors`,
`MSG_DEBUG_BREAK_ON_ERRORS`) breaks at an UNCAUGHT script error without a line
hook. By the time a sol2 protected call returns, the Lua stack is already
unwound — too late. So the entry point is the protected call's error/message
HANDLER, which Lua runs AT the error point with the stack intact: the seam
installs one C message handler as sol2's default error handler at boot
(`ScriptRuntime::installDebugErrorHandler`), so every script lifecycle call
(init/update/shutdown, event hooks — and any tween/event callback) adopts it.
The handler is a pure pass-through until armed (disarmed behavior is
byte-identical to no handler); armed, it enters the SAME break state a
breakpoint uses — captures the crash message + stack (from level 1, skipping the
handler frame, so the innermost reported frame is the code that faulted;
`ScriptDebugCore::errorBreakLocation` picks the first script frame for the
paused file/line) and BLOCKS in the same nested pump — then returns the error
object UNCHANGED so the lifecycle call still sees the failure and the normal
error path proceeds (the instance disables + reports). An error break never re-arms a
step (the stack unwinds as the error propagates). Only an UNCAUGHT error breaks:
a script's own `pcall` that catches an error keeps its own handler, so our
handler never fires. The break rides the same `MSG_DEBUG_BREAK` path with an
additive `error` field, and the arm installs the pump like a breakpoint. The
gate the handler applies (armed AND a pump exists AND not already broken) is the
pure `ScriptDebugCore::errorShouldBreak`, unit-tested.

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
would hang the tab — so `MSG_DEBUG_BREAKPOINTS`, break-next AND
`MSG_DEBUG_BREAK_ON_ERRORS` (arming) refuse there with one honest line each (the
`screenshot`/`record_trace` precedent on the web target). The wasm build
compiles the same code; only the block is refused — a script error on the web
player still flows its normal path (disabling break-on-errors is a no-op
success, and the error itself is never suppressed).

**Scripting off** (`ORKIGE_SCRIPTING=OFF`): everything compiles and every
debug operation reports the standard "scripting disabled" error (disarming
break-on-errors is a safe no-op).

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
- **Closing a dirty document discards silently** (one Console line, no
  confirm modal); the quit flow does not yet ask about unsaved documents.
- **Breakpoints pause on the line's first instruction** — there are no
  conditional breakpoints or hit counts.
- **iOS hardware** plays standalone (no debug link over USB —
  `Docs/ios-signing.md`), so breakpoints cannot reach it; simulators and
  adb devices work through the shared loopback link unchanged.
- **Change markers are display-only**: there is no hunk revert or
  next/previous-change navigation from the gutter, and a shrinking
  replace-block reads as one modified region (only a pure deletion draws the
  triangle).
