#!/usr/bin/env python3
"""ctest driver for the benchmark results-card RESTART button
(projects/benchmark, the "tally" vignette).

Runs the standalone player over the whole scene sequence, ARMED
(ORKIGE_BENCHMARK) and driven fast + deterministically (a tiny
benchmark.sceneScale shrinks every scene to a handful of frames, the wipe is
off for an immediate switch), until the tour reaches its results card. The
card's Restart button replays the tour from its first scene; the director's
`benchmark.autoRestart` seam fires the SAME restartTour() path the button's
click calls, a few frames into the card, so this headless run proves the
button's wiring without a synthetic mouse event.

The assertions, all off the director's own log lines:
  * the Restart button EXISTS on the card and its rect lands INSIDE the safe
    area and clear of the results panel (touch-friendly, notch-safe placement);
  * the restart path fired (restartTour -> loadLevel 0);
  * the sequence actually restarted from scene 1 (a fresh Terrace Vista
    director init appears AFTER the results card).

    run_benchmark_restart_test.py --repo <root> --player <path> --dir <scratch>

Exit codes: 0 pass, 1 fail.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchmark_breadcrumbs  # noqa: E402
from benchmark_skip import TOUR_ORDER, resolve_skip_scenes  # noqa: E402

# the director's per-scene "ready" line: director[<mode>]: '<label>' ready (...)
READY_RE = re.compile(r"director\[(\w+)\]: '([^']*)' ready")
# the button-existence readback the director logs when it builds the card
BUTTON_RE = re.compile(
    r"director\[tally\]: restart button ready "
    r"rect=\((-?\d+),(-?\d+),(\d+),(\d+)\) "
    r"panel=\((-?\d+),(-?\d+),(\d+),(\d+)\) "
    r"safe=\((\d+),(\d+),(\d+),(\d+)\) "
    r"window=\((\d+),(\d+)\)")
RESTART_LINE = "director[tally]: restart -> loadLevel 0"


def log(msg):
    print("run_benchmark_restart_test: " + msg, flush=True)


def fail(msg, tail=None):
    if tail:
        log(tail)
    print("run_benchmark_restart_test: FAILED - " + msg, flush=True)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--frames", type=int, default=400)
    args = parser.parse_args()

    repo = Path(args.repo)
    out = Path(args.dir)
    out.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.update({
        "ORKIGE_BENCHMARK": "1",
        "ORKIGE_BENCHMARK_DIR": str(out),
        "ORKIGE_BENCHMARK_MODE": "smoke",
        # tiny scene scale => the whole tour traverses in a handful of frames;
        # wipe off => the restart switches immediately (deterministic); the
        # autoRestart seam fires 3 frames into the results card, driving the
        # SAME restart path the button's click uses
        "ORKIGE_CVARS": "benchmark.sceneScale=0.02,benchmark.wipe=0,"
                        "benchmark.autoRestart=3",
        "ORKIGE_DEMO_FRAMES": str(args.frames),
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": str(out),
    })
    # the per-host scene-skip quarantine (win32 default + any manual env). On
    # win32 the tour walks straight past the mirror lake vignette: the CI host's
    # software-Vulkan driver faults inside the cold shader-variant compile of
    # that scene's material/shader mix (the crash-breadcrumb trail dead-ends
    # within ~0.5s of the mirrorlake scene load, with planar reflection already
    # off - a driver-compiler fault, not an engine one). Linux/lavapipe and
    # macOS/Metal never reproduce it; the mirror FEATURE stays fully tested
    # everywhere through the dedicated gates (water_mirror_wobble,
    # benchmark_crossflavor_parity_mirror), which keep planar reflection ON.
    skip_scenes = resolve_skip_scenes(env)
    if skip_scenes:
        log("benchmark.skipScenes = %s (tour walks straight past these)"
            % ",".join(sorted(skip_scenes)))
    # the crash-breadcrumb proof scene: mirrorlake by default; when it is
    # quarantined, the scene loaded in its place (lumens) is the proof, falling
    # back to the first surviving mid-tour scene for a wider custom skip set
    crumb_scene = "mirrorlake"
    if crumb_scene in skip_scenes:
        crumb_scene = next((b for b, _ in TOUR_ORDER[1:]
                            if b not in skip_scenes), TOUR_ORDER[0][0])
    # write the crash-breadcrumb trail beside the run: flushed to disk per
    # entry, so it survives a hard abort even when the last buffered stdout
    # lines are lost - the reliable scene-load trail after an exit 3
    benchmark_breadcrumbs.arm(env, out)

    cmd = [args.player, "--project", str(repo / "projects/benchmark")]
    log("running: " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=str(repo), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=480)
    output = result.stdout.decode("utf-8", "replace")
    if result.returncode != 0:
        # a non-zero exit is the crash path (exit 3 = a Debug abort/assert on
        # Windows) - print the flushed breadcrumb trail alongside the log tail so
        # the last scenes reached before death are named even if stdout was lost
        benchmark_breadcrumbs.dump_on_failure(out, log)
        fail("player exited %d" % result.returncode, output[-1500:])

    lines = output.splitlines()

    # (1) the button exists, with a safe-area-aware, non-overlapping rect
    button = None
    button_at = None
    for i, line in enumerate(lines):
        m = BUTTON_RE.search(line)
        if m:
            button = [int(g) for g in m.groups()]
            button_at = i
            break
    if button is None:
        fail("the results card never logged a Restart button - the button was "
             "not built on the tally screen", output[-1500:])
    (bx, by, bw, bh, px, py, pw, ph,
     sl, st, sr, sb, ww, wh) = button
    log("button rect=(%d,%d,%d,%d) panel=(%d,%d,%d,%d) safe=(%d,%d,%d,%d) "
        "window=(%d,%d)" % (bx, by, bw, bh, px, py, pw, ph, sl, st, sr, sb,
                            ww, wh))
    if bw < 44 or bh < 44:
        fail("the Restart button is not touch-friendly (%dx%d, want >= 44 on "
             "each axis)" % (bw, bh))
    # inside the safe rect on every edge
    if bx < sl or by < st or bx + bw > ww - sr or by + bh > wh - sb:
        fail("the Restart button escapes the safe area: rect=(%d,%d,%d,%d) "
             "safe insets l/t/r/b=%d/%d/%d/%d window=%dx%d"
             % (bx, by, bw, bh, sl, st, sr, sb, ww, wh))
    # clear of the results panel (placed below it, no overlap with the content)
    if by < py + ph:
        fail("the Restart button overlaps the results panel (button top %d < "
             "panel bottom %d)" % (by, py + ph))

    # (2) the restart path fired
    if RESTART_LINE not in output:
        fail("the restart path never fired (no '%s') - the button/autoRestart "
             "wiring did not reach restartTour" % RESTART_LINE, output[-1500:])

    # (3) the sequence actually restarted from scene 1: a fresh Terrace Vista
    # director init must appear AFTER the results card was built
    first_tally = None
    for i, line in enumerate(lines):
        m = READY_RE.search(line)
        if m and m.group(1) == "tally":
            first_tally = i
            break
    if first_tally is None:
        fail("the tour never reached its results card (no tally 'ready' line)",
             output[-1500:])
    restarted_vista = False
    for line in lines[first_tally + 1:]:
        m = READY_RE.search(line)
        if m and m.group(1) == "vista" and m.group(2) == "Terrace Vista":
            restarted_vista = True
            break
    if not restarted_vista:
        fail("the tour did not restart from scene 1 - no fresh Terrace Vista "
             "init after the results card (loadLevel 0 did not take effect)",
             output[-1500:])

    # (4) the crash-breadcrumb trail was written and names a mid-tour scene load
    # (mirrorlake, or its stand-in when quarantined): this is the plumbing the
    # failure path above dumps, so a passing run proves it is present for the
    # NEXT crash
    benchmark_breadcrumbs.assert_present(out, fail, expect_scene=crumb_scene)

    log("OK: Restart button present + safe-area-placed, restart fired, tour "
        "replayed from scene 1, breadcrumb trail present%s"
        % ("" if not skip_scenes
           else "; skipped " + ",".join(sorted(skip_scenes))))


if __name__ == "__main__":
    main()
