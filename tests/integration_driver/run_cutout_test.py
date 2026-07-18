#!/usr/bin/env python3
"""Textured cutout parts on a vector rig, end to end, per flavor.

Boots projects/vectorshapes scenes/cutout.oscene under
ORKIGE_CUTOUT_SELFCHECK: the player asserts the mixed flat+textured rig's
draw-run split (>= 3 textured of >= 4 runs) and that the wave clip moves the
pose through the dynamic per-section upload, then saves a mid-clip
screenshot this driver PIXEL-PROBES:

  * each textured part's signature colour is present in real quantity
    (body blue, arm yellow, the head's two bands) - the textures actually
    SAMPLE, they are not tint-coloured silhouettes;
  * the head's dark band sits ABOVE its light band - the texture's row 0
    lands on the rect's TOP edge (the v-orientation contract);
  * the flat-colour shadow renders too - flat and textured regions coexist
    in one rig (and the flat pipeline still paints).

Pure stdlib. Exit codes: 0 pass, 1 fail.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_benchmark_pixel_test import decode_png  # noqa: E402

# signature colours of the GENERATED probe art (make_vectorshape_demo.py);
# unlit + white tint, so the screenshot carries them near-verbatim
TARGETS = {
    "body": (40, 110, 220),
    "arm": (250, 205, 60),
    "head_top": (120, 70, 30),
    "head_bottom": (240, 170, 90),
    "shadow": (61, 61, 89),
}
CHANNEL_TOLERANCE = 14      # per-channel wiggle (measured < 4 on both flavors)
MIN_FRACTION = 0.00005      # each part must cover a real area of the frame


def log(msg):
    print("run_cutout_test: " + msg, flush=True)


def fail(msg):
    print("run_cutout_test: FAILED - " + msg, flush=True)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True,
                        help="scratch dir for the screenshot")
    parser.add_argument("--source", required=True,
                        help="repo root (the player's working directory)")
    args = parser.parse_args()

    os.makedirs(args.dir, exist_ok=True)
    shot = os.path.join(args.dir, "cutout_play.png")
    if os.path.exists(shot):
        os.unlink(shot)

    env = dict(os.environ)
    env["ORKIGE_CUTOUT_SELFCHECK"] = "1"
    env["ORKIGE_CUTOUT_SCREENSHOT_DIR"] = args.dir
    log("running the cutout selfcheck")
    result = subprocess.run(
        [args.player, "projects/vectorshapes/scenes/cutout.oscene",
         "--project", "projects/vectorshapes"],
        cwd=args.source, env=env, capture_output=True, text=True,
        timeout=300)
    tail = (result.stdout + result.stderr).splitlines()[-15:]
    if result.returncode != 0:
        fail("player selfcheck exited %d:\n  %s"
             % (result.returncode, "\n  ".join(tail)))
    if not os.path.exists(shot):
        fail("the selfcheck left no screenshot at %s" % shot)

    width, height, channels, data = decode_png(shot)
    log("probing %s (%dx%d)" % (shot, width, height))
    counts = {name: 0 for name in TARGETS}
    y_sums = {name: 0 for name in TARGETS}
    for y in range(height):
        row = y * width * channels
        for x in range(width):
            i = row + x * channels
            for name, (r, g, b) in TARGETS.items():
                if abs(data[i] - r) <= CHANNEL_TOLERANCE and \
                        abs(data[i + 1] - g) <= CHANNEL_TOLERANCE and \
                        abs(data[i + 2] - b) <= CHANNEL_TOLERANCE:
                    counts[name] += 1
                    y_sums[name] += y

    floor = max(50, int(width * height * MIN_FRACTION))
    for name in TARGETS:
        log("  %s: %d px (floor %d)" % (name, counts[name], floor))
        if counts[name] < floor:
            fail("part colour '%s' barely present (%d px < %d) - the "
                 "texture (or the flat shadow) did not render"
                 % (name, counts[name], floor))

    # v orientation: the head texture's TOP band (dark) must render above
    # its bottom band (screen y grows downward)
    top_y = y_sums["head_top"] / counts["head_top"]
    bottom_y = y_sums["head_bottom"] / counts["head_bottom"]
    log("  head bands: dark mean y %.1f, light mean y %.1f"
        % (top_y, bottom_y))
    if top_y >= bottom_y:
        fail("the head texture renders upside down (dark band mean y %.1f "
             "not above light band %.1f) - the v-orientation contract broke"
             % (top_y, bottom_y))

    log("PASSED - all cutout textures sample, the band orientation holds "
        "and the flat shadow coexists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
