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

    cmd = [args.player, "--project", str(repo / "projects/benchmark")]
    log("running: " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=str(repo), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=480)
    output = result.stdout.decode("utf-8", "replace")
    if result.returncode != 0:
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

    log("OK: Restart button present + safe-area-placed, restart fired, tour "
        "replayed from scene 1")


if __name__ == "__main__":
    main()
