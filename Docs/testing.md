# Testing a game in Lua

A project tests its own game code, in the language the game is written in. Test
files live in `<project>/tests/`, are written against an engine-owned
vocabulary, and run against the **live runtime** — the same sandbox, the same
content mounts, the same libraries the game loads.

This is the game-side tier. The engine's own C++ suites (`ctest --preset unit`,
the integration selfchecks) are a different thing and are described in
[CLAUDE.md](../CLAUDE.md#testing).

## Writing a test

A file under `<project>/tests/` whose name ends in `.test.lua` is a test file.
Suffix-marks-kind, like `.component.lua` (a component kind) and `.editor.lua`
(an editor tool).

```lua
-- projects/jumper-lua/tests/movement.test.lua
local jumper = script.require("scripts/jumperlib.lua")

test("the goal is a sphere, not a box", function(t)
    t.truthy(jumper.withinGoal(0, 0, 0, 1.5, 0, 0, 1.5))
    -- the corner of the enclosing box is OUTSIDE the sphere
    t.falsy(jumper.withinGoal(0, 0, 0, 1.4, 1.4, 0, 1.5))
end)
```

`test(name, fn)` declares one test. Running the file IS the declaration pass:
nothing executes until every `test` call has been seen, then each body runs in
turn under `pcall`, so one failing test never stops the rest.

The body takes one argument — the assertion table:

| Assertion | Refuses when |
| --- | --- |
| `t.eq(actual, expected [, message])` | the values differ. Tables compare by CONTENT, recursively (metatables are not consulted); the message spells both sides out |
| `t.near(actual, expected [, tolerance [, message]])` | `\|actual - expected\|` exceeds `tolerance` (default `1e-6`) — the float comparison |
| `t.truthy(value [, message])` | the value is `false` or `nil` |
| `t.falsy(value [, message])` | the value is anything else |
| `t.isnil(value [, message])` | the value is not `nil` |
| `t.errors(fn [, contains [, message]])` | `fn` returns normally — or raises without `contains` in its message. Returns the raised message |
| `t.fail(message)` | always — the "we got here and should not have" assertion |

A refusal reads `tests/movement.test.lua:12: expected -1, got 3`. The
`file:line` is the line in **your test body**, and it costs nothing: script
chunks load under their project-relative names, so Lua's own `error(message,
level)` prepends it. (The `debug` library stays denied — see
[lua-api.md](lua-api.md#sandbox--security).)

## Running

```sh
orkige_player --project projects/jumper-lua --run-tests
orkige_player --project projects/jumper-lua --run-tests --test-filter clamp
```

The **exit code is the verdict**: `0` when everything passed, non-zero
otherwise. That is the same contract every player selfcheck ctest uses, so a
project's suite registers as a ctest with no wrapper.

`--test-filter <substring>` is matched against `<file>::<test name>`, so
`--test-filter movement` runs a whole file and `--test-filter "is symmetric"`
runs one case across files.

`--run-tests` needs `--project`: a suite belongs to a project (its `tests/`
directory and its `scripts/` libraries), not to a loose scene.

Discovery walks `<project>/tests/` recursively. Two files whose base names
collide (`tests/a/loot.test.lua` and `tests/b/loot.test.lua`) are a name clash:
the first in sorted order wins and both are logged.

**The runner ships.** It is compiled into every player — the one inside a
released editor and the one on a device payload — so testing a project needs no
repository, no build tree and no interpreter beyond the engine's own. The
vocabulary above is a string constant inside the binary, not a file to install.

An `ORKIGE_SCRIPTING=OFF` build has no interpreter and says so, exiting
non-zero: it cannot answer the question that was asked, and reporting a pass
would be a lie.

## The run artifact

Every run writes a JSONL file — one JSON object per line, flushed as it is
produced, in the shape the breadcrumb trail and the benchmark results use. The
exit code says *whether*; this says *which* and *why*, without scraping a log.

```json
{"record":"meta","project":"Jumper Lua","utc":"2026-08-03T15:30:39Z","filter":"","files":2}
{"record":"test","file":"tests/movement.test.lua","name":"the goal is a sphere, not a box","status":"pass","message":"","ms":0.002}
{"record":"summary","files":2,"total":14,"passed":14,"failed":0,"errors":0,"filtered":0,"ms":2.2,"exitCode":0}
```

`status` is one of exactly three words:

- `pass` — the body returned without raising.
- `fail` — an **assertion** refused. `message` is the `file:line:` refusal.
- `error` — anything else raised (a nil index, a typo, a library that would not
  load). A different fact from a failure, and worth reading differently.

Because each line is flushed as it is written, a run that **crashes** still
leaves the file naming the test that was live: the last line is the last thing
that happened. A file with no `summary` line is a run that died.

The artifact lands beside the breadcrumb trail in the writable app directory;
`ORKIGE_TEST_REPORT_DIR` overrides the directory (the isolation seam a ctest
uses), and the file is named `tests-<utcstamp>.jsonl`.

## What a test can reach

A test file runs in an ordinary script sandbox with the runtime up, so it has
the whole permitted surface: `math`, `string`, `table`, the pruned `os`,
`script.require` for the project's libraries, and `data.read` / `data.readJson`
for its content. `projects/jumper-lua/tests/tuning.test.lua` uses that to assert
the **shipped** `data/tuning.json` against the same validation the game boots
with — the file that actually ships, not a copy of its numbers.

A test that also needs a **world** — objects, physics, the game's own scripts
running — declares the scene it wants and gets one; see below.

## Play-mode tests: a test with a world

Declaring a `scene` makes a test a **play-mode test**: it runs in a live world,
with physics stepping, scripts updating and frames actually rendering.

```lua
test("the level holds the player up", { scene = "scenes/main.oscene" },
    function(t)
        t.waitUntil(function() return shared.jumper ~= nil end, 300)
        t.wait(1.0)
        t.truthy(shared.jumper.y > -10, "the player fell out of the level")
    end)
```

The body runs as a **script task** ([lua-api.md](lua-api.md#tasks-code-that-spans-frames-scriptasync)),
so it suspends on the same three waits a game script uses, reached through the
assertion table:

| Wait | Comes back |
| --- | --- |
| `t.wait(seconds)` | after that many seconds of gameplay time |
| `t.waitFrames(n)` | after `n` frames |
| `t.waitUntil(fn [, limitFrames])` | the first frame `fn()` returns true; with a limit, giving up is a named failure |

While the test is suspended the GAME runs. It is resumed once per frame, in the
script phase of the tick order and nowhere else, so a test observes the world
only at frame boundaries — never halfway through a physics step.

The assertion vocabulary is **identical** in both tiers. That is the point: a
test is a test, and only its declaration says whether it needs a world.

### Isolation

Every play-mode test gets its **own** world: the runner tears the current one
down whole through `GameObjectManager::clear` and loads the scene fresh — for
every test, even two in a row on the same scene.

The teardown is the full clear, *not* the persistence-preserving one the level
system's mid-play switch uses. A test run is a boundary: an object marked
persistent surviving into the next test would couple the two, and a suite whose
tests can influence each other is worth less than no suite. Persistence is a
feature of a *play session*, and a test declares the world it wants.

### The frame budget

**Every play-mode test carries a frame budget** (600 frames — about ten seconds
of gameplay — unless a wait was given a shorter one of its own). A test that
runs out of frames is recorded as an `error` reading *timed out after 600 frames
without finishing*, and the run fails.

This is not a nicety. A `waitUntil` whose condition never comes true would
otherwise hang the runner until the CI job's own timeout killed it — burning the
whole job's budget and reporting nothing about which test was stuck. A named
failure costs ten seconds and says exactly which test wedged.

### Ordering, and one player boot

A run is one player process from start to finish. Inside it:

1. every test that needs no scene runs first — they are fast, they cannot be
   disturbed by a world, and their verdicts land before anything is loaded;
2. then the play-mode tests, grouped by scene, each with its own fresh world.

The frameless entry point (`ScriptRuntime::runTestFile`, which the engine's own
unit tests use) has no world and advances no frames, so it refuses a play-mode
test per test — honestly, as an `error` naming the frame-driven runner — rather
than passing it silently.

## Making it a ctest

A project's suite is one `add_test` line:

```cmake
add_test(NAME player_project_lua_tests
    COMMAND orkige_player --project projects/jumper-lua --run-tests
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
```

`projects/jumper-lua` carries the shipped example, registered per render flavor:
`tests/movement.test.lua` and `tests/tuning.test.lua` need no world,
`tests/playthrough.test.lua` runs in `scenes/main.oscene`.

## What ships

Nothing here does. `tests/` is not an export payload subdirectory, so a game's
suite is out of every package **by construction** rather than by a later strip;
editor tools (`*.editor.lua`) ride inside `scripts/`, which IS a payload
subdirectory, so those are stripped explicitly. The export suite asserts both
absences, so neither can regress quietly.
