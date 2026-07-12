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
  - The historical second argument (`0` above) is accepted for source
    compatibility and ignored; the severity is fixed by the macro.
- **Levels**: `error` < `warn` < `info` < `debug` (plus `off`). A tag emits a
  message only when its severity is at or above the tag's threshold.
- **Defaults**: `error` + `warn` on in every build config, `info` additionally on
  in a Debug build, `debug` off until raised.
- **Sinks**: `stderr` (the developer channel the tests grep) and the `LogManager`
  file log (dormant until `startFileLog` opens a file). An `oDebugError` also
  drops a `Breadcrumbs` entry so a hard-crash trail carries the last errors.
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

## Tags in use

`engine`, `render`, `sound`, `physic`, `scene`, `core`, `game`, `gameobject`,
`editor`, `serialize`, `script`, `resource`, `filesystem`, `eventmanager`, `loc`,
`gui`, `asset`. Adding a tag is free — the macro accepts any string — but only the
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

## Not the accidental channel

`SDL_Log` is *not* a diagnostic channel. It stays legitimate only for selfcheck /
demo output whose exact strings a test contract greps (the `samples/`,
`tools/player` selfcheck blocks, `tests/`) and for a handful of app-boot lines.
Engine diagnostics belong on the leveled macros above.
