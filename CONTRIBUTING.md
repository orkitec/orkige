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

The codebase carries a decade of history, and the older files follow an older
house style: tabs for indentation, `m`-prefixed member names, Doxygen-style
`//!` comments, and `#ifndef` include guards with a date suffix. **Match the file
you are editing** — consistency within a file matters more than any global rule.
New standalone code should be clean, modern C++20.

## Two hard rules for contributions

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
