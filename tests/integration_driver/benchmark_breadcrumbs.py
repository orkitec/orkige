#!/usr/bin/env python3
"""Shared crash-breadcrumb plumbing for the benchmark ctest drivers.

The benchmark tour has twice aborted on a CI runner (Windows Debug, exit code
3) mid scene-switch, with nothing in the captured stdout. Breadcrumbs are
flushed to disk per entry, so the trail survives a hard abort even when the
last buffered stdout lines are lost. Every benchmark driver therefore points
the player's breadcrumb trail at its own scratch dir, dumps that trail on ANY
nonzero exit (naming the last scene reached), and asserts the trail is present
on a clean run - so the plumbing a future abort depends on is proven live by
every green run.

The trail lives in a dedicated <scratch>/breadcrumbs subdir so it never
collides with a driver that globs the scratch dir for *.jsonl artifacts (the
benchmark results card).

Pure stdlib. Imported by run_benchmark_test.py, run_benchmark_restart_test.py
and run_benchmark_scene_probe.py.
"""

import os
from pathlib import Path

BREADCRUMB_NAME = "breadcrumbs.jsonl"


def _crumb_dir(scratch):
    return Path(scratch) / "breadcrumbs"


def crumb_path(scratch):
    """the live breadcrumb file under the scratch dir's breadcrumbs subdir."""
    return _crumb_dir(scratch) / BREADCRUMB_NAME


def arm(env, scratch):
    """point the player's breadcrumb trail at <scratch>/breadcrumbs and clear
    any stale file from a previous run. Mutates and returns env."""
    directory = _crumb_dir(scratch)
    directory.mkdir(parents=True, exist_ok=True)
    env["ORKIGE_BREADCRUMB_DIR"] = str(directory)
    try:
        crumb_path(scratch).unlink()
    except OSError:
        pass
    return env


def read_trail(scratch):
    """the flushed trail text, or "" when absent/unreadable."""
    try:
        return crumb_path(scratch).read_text("utf-8", "replace")
    except OSError:
        return ""


def dump_on_failure(scratch, log):
    """print the flushed trail (tail) via the driver's log() so a nonzero-exit
    run names the last scenes reached even when stdout was lost to the abort."""
    trail = read_trail(scratch)
    if not trail.strip():
        trail = "(no %s written)" % BREADCRUMB_NAME
    log("breadcrumb trail before exit:\n" + trail[-1500:])


def assert_present(scratch, fail, expect_scene=None):
    """a clean run must have written a trail with at least a boot marker (and,
    when given, an entry mentioning expect_scene) - the plumbing a future
    abort relies on, proven live. fail(msg) ends the driver with exit 1."""
    trail = read_trail(scratch)
    if not trail.strip():
        fail("no %s written - the breadcrumb plumbing a future abort relies on "
             "is not live (ORKIGE_BREADCRUMB_DIR wiring)" % BREADCRUMB_NAME)
    if expect_scene is not None and expect_scene not in trail:
        fail("the breadcrumb trail has no '%s' entry - a future exit 3 would "
             "name no scenes:\n%s" % (expect_scene, trail[-1500:]))
