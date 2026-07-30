# Logging

The engine's diagnostics run through one always-compiled, runtime-gated channel.
An honest error (a missing material, a failed parse, a resource miss) reaches
stderr by default in every build; verbose per-subsystem detail is off until you
raise it, live, per tag.

## At a glance

- **Macros** (`core_debug/DebugMacros.h`) — tagged, stream-style, always compiled:
  - `oDebugError("render", 0, "texture '" << name << "' not found");` — a failure.
  - `oDebugWarning(condition, "message " << detail);` — a warn, emitted only when
    the condition is false (tag `engine`).
  - `oDebugMsg("scene", 0, "loaded " << n << " objects");` — verbose detail.
  - The second argument (`0` above) is accepted for source compatibility and
    ignored; the severity is fixed by the macro.
- **Levels**: `error` < `warn` < `info` < `debug` (plus `off`). A tag emits a
  message only when its severity is at or above the tag's threshold.
- **Defaults**: `error` + `warn` on in every build config, `info` additionally on
  in a Debug build, `debug` off until raised.
- **Sinks**: `stderr` (the developer channel the tests grep) and the `LogManager`
  file log (dormant until `startFileLog` opens a file). An `oDebugError` also
  drops a `Breadcrumbs` entry so a hard-crash trail carries the last errors.
- **On a phone, stderr is the platform log.** A mobile app has no terminal, so
  anything written to stdout or stderr is discarded before anyone can read it.
  The app host calls `logAttachPlatformStdio()` at boot, which on Android
  re-points both streams at the system log: `adb logcat -s orkige` then shows
  every line this page describes — plus a script's `print` and any library's own
  output, which travel the same streams. It is a no-op on every platform whose
  stdio a developer can already read, so nothing about a desktop run changes.
- **Zero cost when off**: the macros gate on the tag's threshold *before* building
  the message stream, so a disabled call never evaluates its arguments (a single
  relaxed atomic load fast-rejects anything quieter than the loudest active tag).

## Line format

Each emitted line is `[<level>][<tag>] <message> (<file>:<line>)`, e.g.

```
[error][render] RenderSystem: texture 'hero.png' not found (RenderSystemNext.cpp:249)
```

## Raising verbosity live (cvars + MCP)

Per-tag thresholds are console variables named `log.<tag>` (plus `log.default`
for the base), registered at startup and reusing `CVarManager` — including its
manifest persistence (`CVAR_PERSIST`, so a raised level rides into the project
manifest as `cvar.log.<tag>`). The value is a level name; the empty string means
"inherit the default".

```
# in the console / over the debug protocol / via the Lua cvar table
log.render debug     # everything the 'render' tag emits
log.render off       # silence it entirely
log.render           # (empty) back to inheriting log.default
log.default warn     # move the base threshold
```

Because it is an ordinary cvar, an agent raises a tag over MCP with the existing
`set_cvar` verb — no new tool:

```json
{ "name": "set_cvar", "arguments": { "name": "log.render", "value": "debug" } }
```

The change takes effect immediately (the cvar's `onChange` writes the log table).

## Seeding a cvar from the environment (boot)

Any console variable can be seeded at boot from a per-variable environment
variable, without editing the project manifest or opening a socket. The name is
`ORKIGE_CVAR_<suffix>`, and the cvar it seeds is `<suffix>` with **every
underscore turned into a dot**:

```
ORKIGE_CVAR_r_planarReflection=0   # seeds the cvar  r.planarReflection = 0
ORKIGE_CVAR_r_shadowQuality=low    # seeds the cvar  r.shadowQuality = low
```

A cvar whose own name contains an underscore (e.g. `roller_gravity`) is still
reachable: the dotted form is tried first, then the literal suffix, so
`ORKIGE_CVAR_roller_gravity=30` finds `roller_gravity` when no `roller.gravity`
exists. The seed is **evaluated once at boot**, after the engine/render cvars
register (`AppHost::setupEngine` → `CVarManager::seedFromEnvironment`): the value
is type-validated like any `set`, the cvar's `onChange` fires, and a name that
matches no registered cvar or a value the type rejects emits one honest warning
and is ignored. It is a general dev/CI hook — not a live per-frame knob. The
distinct `ORKIGE_CVARS="name=value,name2=value2"` variable seeds a comma-list of
cvars through the manifest held-override path instead (order-independent with
script-registered cvars; used by the headless test drivers).

## Tags in use

`engine`, `render`, `sound`, `physic`, `scene`, `core`, `game`, `gameobject`,
`editor`, `serialize`, `script`, `resource`, `filesystem`, `eventmanager`, `loc`,
`gui`, `asset`, `platform`. Adding a tag is free — the macro accepts any string — but only the
tags above ship a pre-registered `log.<tag>` cvar; extend `kKnownLogTags` in
`core_debug/LogLevels.cpp` to make a new tag settable by cvar.

## Where things live

- `core_debug/DebugMacros.h` — the macros + the gating/emit function declarations.
- `core_debug/LogLevels.cpp` — the thread-safe per-tag threshold table, the emit
  path (stderr + file + breadcrumb), and the `log.<tag>` cvar installation.
- `core_debug/LogManager.{h,cpp}` — the file sink (`appendFileLine`), configurable
  from XML.
- `tests/core/LogLevelsTests.cpp` — gating, the disabled-path no-evaluation
  contract, the breadcrumb-on-error hook, and the cvar seam.

## Crash breadcrumbs & the crash marker

The breadcrumb trail (`core_debug/Breadcrumbs`) is an always-on, bounded ring of
engine events (scene loads, script errors, warnings, boot/shutdown) FLUSHED to
disk per entry so a hard crash leaves a readable tail. The player writes
`breadcrumbs.jsonl` to its writable app dir and rotates it to
`breadcrumbs.prev.jsonl` at boot; the editor reads the survived file over the MCP
`get_breadcrumbs` verb.

On top of that, a **fatal-signal crash marker** makes a crashed run
machine-detectable — including on phones, where there is no crash dialog:

- **The handler.** `Breadcrumbs::installCrashHandler()` (called once at boot,
  after `setFile()`/`rotate()`) arms handlers for `SIGSEGV`/`SIGBUS`/`SIGILL`/
  `SIGFPE`/`SIGABRT`. On a fatal signal the handler writes ONE final `"crash"`
  breadcrumb naming the signal, then restores the default disposition and
  re-raises so the OS still produces its crash report / core dump. The crash is
  **marked, never swallowed**.
- **Async-signal-safety** is the design constraint: the dedicated append fd and
  one fully-formatted breadcrumb line per signal are prepared at install time, so
  the handler only does `write(2)` + `raise(2)` — no `malloc`, no stdio, no JSON
  formatting. The marker's `"t"` is the install-time (boot) second; the accurate
  moment of death is the last ordinary crumb before it — the crash line only
  records THAT the run died and to which signal.
- **Sanitizer coexistence.** Under an AddressSanitizer build the marker stands
  down (`installCrashHandler()` returns `false`) — ASan owns the fatal handlers
  and its reports must stay intact.
- **Platform tolerances.** POSIX is first-class (`sigaction` + `write(2)`).
  Windows is best-effort over the four signals the MSVC CRT raises
  (`SIGSEGV`/`SIGABRT`/`SIGFPE`/`SIGILL`, no `SIGBUS`); there is no SEH machinery
  in this version.
- **Detection at next boot.** After `rotate()` the player reads the rotated file;
  when its LAST entry is a `"crash"` crumb it logs one honest warning
  (`oDebugWarn("breadcrumbs", …)`), e.g. *"the previous run crashed (SIGSEGV) -
  trail in breadcrumbs.prev.jsonl"*. The parse is the pure, headless-unit-tested
  `Breadcrumbs::lastEntryIsCrash()`.
- **MCP surfacing.** `get_breadcrumbs` gains `crashed` + `crashSignal`, derived
  from that same helper over the `previous` trail — no new verb.

Verified by `BreadcrumbsTests` (the parse + rotation flag) and the
`player_crash_marker_selfcheck` integration selfcheck per flavor
(`ORKIGE_CRASH_SELFCHECK`): a deliberate `raise(SIGSEGV)` at a known frame, the
expected signal death, then a clean reboot that warns + carries the crash crumb.

## Not the accidental channel

`SDL_Log` is *not* a diagnostic channel. It stays legitimate only for selfcheck /
demo output whose exact strings a test contract greps (the `samples/`,
`tools/player` selfcheck blocks, `tests/`) and for a handful of app-boot lines.
Engine diagnostics belong on the leveled macros above.
