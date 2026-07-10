# Contributing to Orkige

Thanks for your interest in Orkige.

## Project posture

Orkige is source-open, not a community project. It is built first and foremost for
its author's own games, and development is driven by what those games need. That
shapes how contributions are handled:

- **Issues are welcome.** Bug reports, reproductions and clear feature ideas are
  genuinely useful, even if they don't get acted on right away.
- **Pull requests may sit.** There is no support promise and no response-time
  commitment. A PR might be merged quickly, might wait a long time, or might be
  declined if it pulls the engine in a direction that doesn't fit the games it
  serves. That's not a judgement on the work — it's the reality of a one-author
  engine. Opening an issue to discuss a larger change before writing it is the
  surest way to avoid wasted effort.

None of this is meant to discourage you. It's just honest about what to expect.

## Building

See the [README](README.md) for the full build instructions (CMake + Ninja +
vcpkg). The short version:

```sh
cmake --preset macos-debug          # configure (first run builds dependencies)
cmake --build --preset macos-debug  # build
```

## Testing

Every change ships with tests that verify it — unit tests for core logic, and a
self-check hook wired into ctest for app and runtime behavior. Run the tests
before opening a PR:

```sh
ctest --preset unit       # headless unit suite (~3s) — the quick check
ctest --preset desktop    # the real suite (rendering, physics, app-level runs)
```

A change with no test is a change that will silently break later. If you're
unsure how to test something, say so in the PR — a pointer to the right harness
is easy to give.

## Code style

The umbrella rule is **match the file you are editing** — consistency within a
file matters more than any global rule, and the tree spans a decade, so styles
vary between the old core/engine code and the newer tooling. New standalone code
should be clean, modern C++20.

The house conventions, which most of the tree already follows:

- **Indentation is tabs**, four columns wide; wrapped lines target 80 columns.
- **Braces go on their own line** (namespaces, types, functions, control
  blocks), and a `namespace` body is indented.
- **Members of a class with encapsulation carry an `m` prefix over a CamelCase
  tail** — `mRootDirectory`, `mTextureName`, `mServer`. This holds in old and
  new classes alike (`AssetDatabase`, `EditorControlServer`).
- **Fields of a passive data struct are plain camelCase, no prefix** — the
  aggregates that are just grouped state, such as the editor's `PlaySession`
  and `AssetBrowserState` (`mode`, `process`, `currentDir`, `thumbnailSize`).
- **Types are CamelCase; functions and methods are camelCase.**
- **Ownership is spelled with the engine's alias family** from
  `core_util/optr.h`: `optr<T>` for shared ownership, `woptr<T>` for a weak
  observer, `uptr<T>` for a single owner. Use them rather than writing the
  `std::` names directly — the family keeps ownership intent readable and
  uniform. A translation unit outside `namespace Orkige` imports what it uses
  with `using Orkige::optr;` after its includes (see `tools/player/main.cpp`);
  headers qualify (`Orkige::uptr<Impl>`) instead of importing.
  Review-enforced (no lint can choose between equivalent types).
- **Comments are Doxygen `//!` / `//!<`.** They are wrapped by hand — describe
  behavior, not the mechanics of the surrounding line.
- **Include guards are `#ifndef` with a date suffix** (core/engine headers).
- **Line endings are LF everywhere**, enforced by `.gitattributes`.

`.clang-format` and `.clang-tidy` at the repository root encode these rules for
tools. The tree is deliberately **not** reformatted wholesale (that would bury
real changes in history), so run them per hunk on what you touched rather than
across whole files:

```sh
git clang-format               # format only your staged/changed lines
clang-tidy --quiet -p build/macos-debug <file.cpp>   # advisory checks
```

Some of the house style — the tab-aligned declaration columns and the
hand-wrapped Doxygen prose — cannot be expressed in `.clang-format`; those it
leaves to you. `.clang-tidy` is advisory (naming rules plus a small curated set
of correctness/performance checks), never a build gate.

## Two hard rules for contributions

These are about content, and apply on top of the mechanical style above:

1. **Comments describe the code, never the development process.** No references to
   tasks, milestones, phases, tickets, or who wrote what. A comment explains what
   the code does and why — nothing about how it came to be.
2. **No third-party product names in code, comments, or docs.** Don't name other
   game engines, tools, or products to draw comparisons. Describe the behavior or
   mechanic directly instead.

Both rules apply to code, comments, user-facing strings, and documentation. Commit
messages are the one place where development history is fine.

## License

Orkige is licensed under Apache-2.0. By contributing, you agree that your
contributions are licensed under the same terms. There is no separate CLA to sign.
