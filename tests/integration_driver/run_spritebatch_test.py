#!/usr/bin/env python3
"""Sprite-run batching end to end, per flavor.

Boots projects/benchmark scenes/fixture_sprites.oscene (11 sprites whose
exact painter's grouping is known: 3 runs + 1 solo, see the generator) twice
- batching ON (the default) and OFF - with ORKIGE_SPRITEBATCH_SELFCHECK
driving both runs (it moves the batched member "A4" at frame 20, so the
frame-60 screenshots show a mid-run re-upload; the ON run additionally
exercises the live r.spriteBatching escape hatch).

Assertions (driver-independent - see EXPECTED below):
  * the realized run count is EXACTLY 3 (structural, a silent grouping
    regression shifts it); the batching-off minus batching-on draw-batch
    DELTA matches the flavor table; the live-toggle-off reading equals
    the batching-off run (measured on the same host and driver).
  * PIXEL IDENTITY: the ON and OFF screenshots must match up to a 1-LSB
    rounding fringe on sprite edges, BOTH flavors (the merged path bakes
    the world transform on the CPU, the per-quad path applies it in the
    vertex shader - the sums round differently at float32, and no GL/
    Vulkan implementation guarantees invariance across different vertex
    data; measured: next 10 px of 921600 at delta 1 on Metal, classic 0 px
    on macOS GL but 5 px at delta 1 on llvmpipe).

Pure stdlib. Exit codes: 0 pass, 1 fail.
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_benchmark_pixel_test import decode_png, pixel  # noqa: E402

# per-flavor DRIVER-INDEPENDENT expectations over the fixture. Absolute
# frame-stats batch counts are driver-dependent (a software Vulkan stack
# counts one more steady batch than Metal for the same scene), so the
# assertions pin what every driver preserves: the batcher's realized run
# count (structural) and the batching-off minus batching-on draw-batch
# DELTA. Classic counts real draw calls: 3 merged runs + 1 solo = 4 vs 11
# individual quads (delta 7); the next flavor's frame-stats metric
# compresses the same structure (delta 1).
EXPECTED = {
    "classic": {"delta": 7, "runs": 3},
    "next": {"delta": 1, "runs": 3},
}

MAX_DELTA = 1                 # per-channel LSB fringe, both flavors
OUTLIER_FRACTION = 0.0001

ON_PATTERN = re.compile(
    r"spritebatch selfcheck complete - batching on, batches=(\d+) "
    r"runs=(\d+) liveOffBatches=(\d+) restored")
OFF_PATTERN = re.compile(
    r"spritebatch selfcheck complete - batching off, batches=(\d+)")


def log(msg):
    print("run_spritebatch_test: " + msg, flush=True)


def fail(msg):
    print("run_spritebatch_test: FAILED - " + msg, flush=True)
    sys.exit(1)


def run_player(args, tag, batching_on):
    shot = os.path.join(args.dir, tag + ".png")
    if os.path.exists(shot):
        os.unlink(shot)
    env = dict(os.environ)
    # the selfcheck's mode-over-window schedule ends at frame 160; the cap
    # only backs up its early exit
    env.update({
        "ORKIGE_DEMO_FRAMES": "200",
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_SPRITEBATCH_SELFCHECK": "1",
        "ORKIGE_CVARS": "r.spriteBatching=%d" % (1 if batching_on else 0),
    })
    cmd = [args.player, "scenes/fixture_sprites.oscene", "--project",
           os.path.join(args.repo, "projects/benchmark")]
    result = subprocess.run(cmd, cwd=args.repo, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=180)
    output = result.stdout.decode("utf-8", "replace")
    if result.returncode != 0:
        log(output[-1500:])
        fail("%s: player exited %d" % (tag, result.returncode))
    if not os.path.exists(shot):
        fail("%s: no screenshot written" % tag)
    return output, shot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--flavor", required=True, choices=("next", "classic"))
    args = parser.parse_args()
    expected = EXPECTED[args.flavor]
    os.makedirs(args.dir, exist_ok=True)

    out_on, shot_on = run_player(args, "batching_on", True)
    out_off, shot_off = run_player(args, "batching_off", False)

    match_on = ON_PATTERN.search(out_on)
    if not match_on:
        fail("no batching-on result line in the player output")
    batches_on, runs_on, live_off = (int(match_on.group(1)),
                                     int(match_on.group(2)),
                                     int(match_on.group(3)))
    match_off = OFF_PATTERN.search(out_off)
    if not match_off:
        fail("no batching-off result line in the player output")
    batches_off = int(match_off.group(1))

    log("batches on=%d off=%d liveOff=%d, runs=%d"
        % (batches_on, batches_off, live_off, runs_on))
    if runs_on != expected["runs"]:
        fail("the fixture must realize exactly %d runs (saw %d) - the "
             "grouping contract regressed" % (expected["runs"], runs_on))
    if batches_off - batches_on != expected["delta"]:
        fail("the batching-off minus batching-on delta must be exactly %d "
             "(off %d, on %d) - the merge contract regressed"
             % (expected["delta"], batches_off, batches_on))
    if live_off != batches_off:
        fail("the live escape hatch must render like the booted-off run "
             "(live %d vs booted %d)" % (live_off, batches_off))

    # pixel identity between the two runs (both moved A4 at frame 20)
    a = decode_png(shot_on)
    b = decode_png(shot_off)
    wa, ha, ca, pa = a
    wb, hb, cb, pb = b
    if (wa, ha) != (wb, hb):
        fail("image sizes differ")
    channels = min(ca, cb, 3)
    outliers = 0
    worst = 0
    for y in range(ha):
        row = y * wa
        for x in range(wa):
            base_a = (row + x) * ca
            base_b = (row + x) * cb
            delta = 0
            for c in range(channels):
                diff = abs(pa[base_a + c] - pb[base_b + c])
                if diff > delta:
                    delta = diff
            if delta > 0:
                outliers += 1
                if delta > worst:
                    worst = delta
    frac = outliers / float(wa * ha)
    log("pixel identity: %d differing px (worst delta %d, %.6f%%)"
        % (outliers, worst, 100.0 * frac))
    if worst > MAX_DELTA or frac > OUTLIER_FRACTION:
        fail("exceeded the 1-LSB edge-rounding allowance")
    log("OK")


if __name__ == "__main__":
    main()
