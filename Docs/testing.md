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

### From an installed Orkige

`orkige_player` is a development-tree binary. On a machine carrying only a
released editor, the same run has a front door of its own
([editor-cli.md](editor-cli.md#test)):

```sh
orkige_editor test --project ~/games/roller
orkige_editor test --project ~/games/roller --test-filter clamp \
                   --report-dir ci-out
```

It resolves the player this installation has — the copy inside the application
— runs exactly the command above in it, and exits with the suite's verdict. The
runner is the same one; only the way it was reached differs. `--report-dir` is
the `ORKIGE_TEST_REPORT_DIR` seam below under a flag name, so the artifact
lands where a build server can collect it.

An `ORKIGE_SCRIPTING=OFF` build has no interpreter and says so, exiting
non-zero: it cannot answer the question that was asked, and reporting a pass
would be a lie.

### In the editor: the Tests panel

**View ▸ Tests** is the interactive door. It lists the project's test files,
runs them, and shows each verdict with its message and the `file:line` it came
from - click that button and the script editor opens there.

- **Run All** runs the suite. **Run Selected** runs whatever row is selected: a
  file row runs the file, a test row runs that one test. **Re-run Failed** runs
  only what failed last time. **Stop** ends the run in flight; what it already
  learned stands.
- The **filter** box is the runner's own grammar - the plain substring matched
  against `<file>::<test name>` - so the same text narrows the list and, through
  **Run Filtered**, narrows the run. There is no second grammar.
- A file lists **no tests until it has run**. The tests a file declares only
  exist once its chunk has executed, and the editor runs no game Lua; listing a
  guess would be worse than listing nothing.
- Verdicts appear **as they land**, because the runner flushes its artifact per
  record and the panel tails it.
- A **filtered run updates its own rows** and leaves the rest of the list
  standing; only a whole-suite run replaces it. Re-running one failure says
  nothing about the tests it did not run, and wiping their passes would leave a
  suite that looks like nothing but its failures.

The panel does not run tests in process. The editor never ticks game objects, so
`ScriptComponent` is dormant in edit mode and a test that declares a scene has no
world to run in. The panel starts the same `orkige_player --run-tests` the
command lines above do, resolving the player this installation has, and reads
back the same artifact. What it adds is a place to read it.

**A run that dies is not a failing suite**, and the panel says which happened. A
suite whose tests failed shows red rows and a tally. A runner that crashed or was
killed shows the sentence *the test runner exited (N) without finishing the run*,
naming the last test it reached, with the runner's own output foldable underneath
- and every verdict it did produce still listed, because the artifact was flushed
per record.

Only one run is in flight at a time. A run belongs to the project that started
it: opening another project stops it and drops its results.

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

The reader lives beside the writer (`ScriptTestReport::parseLine`), so the format
has one definition and the round trip is a unit test. The editor's Tests panel
and the `get_project_test_results` MCP verb both decode the artifact through it -
neither derives a verdict from log text.

## Over MCP

An agent reaches the same runner through three verbs
([mcp.md](mcp.md#the-projects-own-lua-suite)):

```
tools/call list_project_tests {}
tools/call run_project_tests { "filter": "movement" }
tools/call get_project_test_results {}
```

`run_project_tests` is asynchronous and `get_project_test_results` streams the
records as they land, so an agent sees a long suite progress rather than waiting
in the dark. They drive the **same session** the Tests panel does: a person
watching the panel while an agent polls is looking at one run, not two.

Do not confuse them with `run_tests` / `list_tests` / `get_test_results`, which
drive **ctest over the engine's own suite**. Two different suites, two different
names, no shared code.

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

### Driving input

A test presses what a player presses:

| Call | Does |
| --- | --- |
| `t.press(target)` | press and HOLD until released |
| `t.release(target)` | release |
| `t.tap(target [, frames])` | press, hold `frames` frames (default 1), release |

```lua
t.press("move+x")
t.wait(0.3)
t.release("move+x")
t.tap("jump")
```

A **target** is a named action, an action direction, or a raw key:

| Target | Means |
| --- | --- |
| `jump` | a DIGITAL action — its key binding is pressed |
| `move+x` / `move-x` / `move+y` / `move-y` | one DIRECTION of an axis action: the keys that push that component positive or negative |
| `SPACE`, `RETURN`, `RIGHT`, … | a raw KEY, for anything no action covers (the same key names the injected-input step grammar uses — case-insensitive, a leading `KC_` optional) |

**Named actions first.** They are what game code reads
(`actions:pressed("jump")`, `actions:value2("move")`), so a test written
against them keeps meaning what it meant when a binding is re-authored. A key
name is the escape hatch for input a game reads directly.

An action bound only to tilt or a controller axis has no key to press and is
**refused by name**, saying what it *is* bound to. A silent no-press would let
a test that proves nothing pass.

Every press goes through the engine's ONE input synthesis path
(`InputManager::injectKey` — the same road agent-driven input takes), so a
driven key is indistinguishable from a key the platform delivered: `isKeyDown`,
the action map, the gui hit test and every key listener see exactly what ships.
A test that drove input by a private road would stop exercising what ships.

**The frame it lands on.** `InputActionMap` takes ONE edge snapshot per frame
(`pressed` = down && !down-last-frame) in the tick order's input slot, before
the scripts of that frame run. A test body is resumed in the SCRIPT phase,
*after* that slot — so a press made there is the **next** frame's press, seen by
the input slot before that frame's game code. `t.press("jump")` followed by one
wait IS a press the game saw, and `t.tap("jump")` is exactly one press edge,
never zero and never two.

**Anything still held is released when the test ends**, before the next one
starts — the same boundary the fresh world draws. One test can never press a
key into the next.

This vocabulary is on `t` and deliberately **not** on the game-facing `input`
table. A game script that can fake input is a real capability with real
consequences — `isKeyDown` answering true for something nobody pressed muddies
the input model for every reader — so the ability to press is opened where it
belongs. The seam is installed only for a test run and bound into a test file's
own sandbox, never into the globals a game script reads.

Pointer and touch have no test verb: a finger is positional, and the number
space it lives in belongs to a window a headless assertion does not have. Drive
them from a `.oui` layout's own widgets, or over MCP `send_input` against a
running game.

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

## Running the suite inside the package

The runs above test a project **folder**. A game ships as a **package**, and
between the two lies the whole export: the texture cook, the sampler bake, the
media staging, the payload subdirectory vocabulary. A file the payload quietly
drops is invisible to a loose run and fatal to a player.

A **test build** closes that gap. It is an export, asked for by name:

```sh
orkige_export  --project projects/jumper-lua --platform ios-simulator \
               --engine-build build/ios-simulator-debug --with-tests
orkige_editor  export --project ~/games/roller --platform macos --with-tests
```

`--with-tests` changes exactly two things about the package, and nothing else:
the project's `tests/` tree is staged into the payload, and the artifact's
project marker gains the line `run-tests=1`. Launched, that app runs the suite
instead of the game and exits with the suite's verdict. `--test-filter` bakes
the runner's own filter in beside it.

**A shipping export is untouched.** `tests/` is still absent from
`payloadSubdirs()`, so a normal package cannot acquire a suite by accident — the
staging is a separate step, called from one guarded place. A marker written
without the flag is byte-for-byte the one line it always was. Diffing a plain
export against a test build of the same project shows those two differences and
no third.

**A test build is not shippable**, and the export says so while it works.

### Why a package, not a flag

A packaged app is launched with **no argv at all** — a phone taps an icon,
`simctl launch` passes nothing. An instruction that is a command-line flag on a
desktop run therefore has to ride *inside* the artifact, which is what makes
this a distinct KIND of export rather than a different way of starting one.

### macos and iOS only

| Platform | Test build |
| --- | --- |
| `macos`, `ios-simulator`, `ios`, `ios-ipa` | yes |
| `android`, `android-aab`, `web` | refused by name |

The line is drawn by how a suite is **discovered**. The runner enumerates
`tests/` with a directory walk, because there is no other way to learn which
files declare tests. A macOS or iOS bundle lays its payload out as loose files,
so the walk finds the suite exactly as it does in a source tree. An Android
package and a browser payload put the payload inside an **archive** the runtime
mounts in place, and a mounted entry is not a directory entry — the walk would
find nothing and the run would report a green verdict over zero tests. Refusing
by name is the honest answer until discovery has a second road there.

### The verdict travels in the artifact

There is no new channel. The run writes the same flush-per-record JSONL
described above, and `ORKIGE_TEST_REPORT_DIR` still chooses where. On a
simulator the app's own data container is a real host directory
(`xcrun simctl get_app_container <udid> <bundle id> data`), so the host names a
path inside it and reads the report back when the run settles.

The `summary` record carries the exit code. Its **absence** is the separate,
named fact that the run died — which is why a harness reads the artifact rather
than the process: an iOS app's process outlives its `main` inside UIKit's run
loop, and there is no exit code to read at all.

### Over MCP

`export_project` takes `withTests` (and `testFilter`), so an agent packages a
test build through the verb it already uses. The platform refusal above applies
there too, and `get_export_results` reports it verbatim.

### Tests

| Test | What it holds |
| --- | --- |
| `export_macos_tests` | the packaged run with no device in it: `projects/jumper-lua` is exported `--with-tests`, run from a neutral cwd, and its whole suite — the play-mode tests included — passes against the **payload's** content |
| `export_ios_simulator_tests` | the same suite on the device it ships to: installed, launched, and the report read back out of the app's data container. `device`-labeled, skipping 77 without a booted simulator |

Both refuse rather than pass on four separate facts: no report (the runner never
started), a report with no summary (the run died, naming the last test it
reached), a summary over **zero tests** (the package carried no suite), and a
nonzero verdict (the game's tests failed). A harness that reports success when
nothing ran is worth less than no harness.

## Making it a ctest

A project's suite is one `add_test` line:

```cmake
add_test(NAME player_project_lua_tests
    COMMAND orkige_player --project projects/jumper-lua --run-tests
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
```

`projects/jumper-lua` carries the shipped example, registered per render flavor:
`tests/movement.test.lua` and `tests/tuning.test.lua` need no world,
`tests/playthrough.test.lua` runs in `scenes/main.oscene` — and its last case
drives the game entirely through presses: it taps `RETURN` past the title
screen, holds `move+x` to walk the character and taps `jump` to lift it, then
reads what the game's own scripts made of that.

## What ships

Nothing here does. `tests/` is not an export payload subdirectory, so a game's
suite is out of every **shippable** package by construction rather than by a
later strip (a test build is the one artifact that carries it, and it says so);
editor tools (`*.editor.lua`) ride inside `scripts/`, which IS a payload
subdirectory, so those are stripped explicitly. The export suite asserts both
absences, so neither can regress quietly.
