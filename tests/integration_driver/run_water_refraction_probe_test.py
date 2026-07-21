#!/usr/bin/env python3
"""Assert screen-space water refraction DISTORTS the scene behind the surface.

Stage-3 water refraction (RenderWaterDesc::screenSpaceRefraction): the opaque
scene is captured before the water draws, and the surface samples it at a
normal-perturbed screen UV, so what sits under the water bends/wobbles. This
drives the ORKIGE_DEMO_WATER refraction leg, which places a HIGH-CONTRAST
checkerboard LAKEBED under the water, and runs it two ways:

  * REFRACTION ON  (ORKIGE_DEMO_WATER_REFRACT)     - the wavy, moving surface
  * REFRACTION OFF (ORKIGE_DEMO_WATER_REFRACT_OFF) - the straight/stable baseline

Two frames apart in time are captured per run, and in the water band (below the
scene cubes) it asserts:

  * WAVY DISPLACEMENT - the ON water band differs strongly from the OFF baseline
    (the checkerboard is bent/displaced through the surface, not straight);
  * CHANGES BETWEEN FRAMES when ON - the refracted bed shifts as the ripple
    scrolls (a temporal motion the straight OFF baseline does not have).

Capability-gated: the demo logs `supported=0/1`. A backend/context that cannot
refract (the next flavor today, a GLES/WebGL classic context) renders the
Stage-1 look both ways, so the probe SKIPS (exit 77) rather than failing.

Pure stdlib; reuses the water-probe PNG decoder in this directory.
"""

import argparse
import os
import re
import subprocess
import sys

# the sibling water probe's zlib PNG decoder + luminance helper (this file's
# directory is on sys.path[0] when run as a script)
from run_water_probe_test import decode_png, _lum


def _band(path):
    """luminance samples over the WATER band (fy 0.76..0.96, below the scene
    cubes, full width) - the region the water surface fills and refracts."""
    width, height, channels, pixels = decode_png(path)
    rows = []
    for fy in (0.76, 0.80, 0.84, 0.88, 0.92, 0.96):
        row = []
        for step in range(2, 39):
            fx = step / 40.0
            row.append(_lum(pixels, channels,
                            min(width - 1, int(fx * width)),
                            min(height - 1, int(fy * height)), width))
        rows.append(row)
    return rows


def _mean_abs_diff(a, b):
    total = 0.0
    count = 0
    for row_a, row_b in zip(a, b):
        for va, vb in zip(row_a, row_b):
            total += abs(va - vb)
            count += 1
    return total / count if count else 0.0


def _run(binary, out, refract_on, tag):
    late = os.path.join(out, f"refract_{tag}_60.png")
    early = os.path.join(out, f"refract_{tag}_40.png")
    env = dict(os.environ, ORKIGE_DEMO_WATER="1", ORKIGE_DEMO_FRAMES="70",
               ORKIGE_AUTOMATED_RUN="1",
               ORKIGE_DEMO_SCREENSHOT=late, ORKIGE_DEMO_SCREENSHOT2=early)
    env["ORKIGE_DEMO_WATER_REFRACT" if refract_on
        else "ORKIGE_DEMO_WATER_REFRACT_OFF"] = "1"
    try:
        result = subprocess.run([binary], env=env, timeout=120,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        print(f"FAIL: demo_water refraction leg ({tag}) hung")
        return None
    if result.returncode != 0:
        print(f"FAIL: demo_water refraction leg ({tag}) exited "
              f"{result.returncode}")
        return None
    for path in (early, late):
        if not os.path.exists(path):
            print(f"FAIL: no refraction frame captured at {path}")
            return None
    text = result.stdout.decode("utf-8", "replace")
    match = re.search(r"supported=(\d)", text)
    supported = match.group(1) == "1" if match else True
    return early, late, supported


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="the hello_orkige app")
    parser.add_argument("--out", required=True, help="scratch dir for the frames")
    args = parser.parse_args()
    if not os.path.exists(args.binary):
        print(f"SKIP: demo app not built: {args.binary}")
        return 77
    os.makedirs(args.out, exist_ok=True)

    on = _run(args.binary, args.out, True, "on")
    if on is None:
        return 1
    on_early, on_late, supported = on
    if not supported:
        print("SKIP: screen-space refraction is not supported on this "
              "backend/context (the water renders the Stage-1 look)")
        return 77

    off = _run(args.binary, args.out, False, "off")
    if off is None:
        return 1
    off_early, off_late, _ = off

    on_e, on_l = _band(on_early), _band(on_late)
    off_l = _band(off_late)

    spatial = _mean_abs_diff(on_l, off_l)      # ON distortion vs straight OFF
    on_motion = _mean_abs_diff(on_e, on_l)     # ON changes between frames
    off_e, off_l2 = _band(off_early), off_l
    off_motion = _mean_abs_diff(off_e, off_l2)  # OFF baseline motion

    # thresholds (measured, frame-locked/deterministic): the checkerboard bed
    # refracted through the surface differs from the straight OFF baseline by
    # ~55 luminance; the scrolling refraction moves the band ~0.8 between the two
    # frames while the straight OFF baseline moves ~0.1. Wide margins below.
    MIN_SPATIAL = 15.0      # wavy displacement vs the straight baseline
    MIN_ON_MOTION = 0.35    # the refraction changes between frames
    MIN_MOTION_RATIO = 3.0  # ON moves clearly more than the straight OFF

    print(f"refraction probe: on_vs_off_distortion={spatial:.1f} "
          f"(>{MIN_SPATIAL}), on_motion={on_motion:.2f} (>{MIN_ON_MOTION}), "
          f"off_motion={off_motion:.2f} (ratio>{MIN_MOTION_RATIO})")

    failures = []
    if spatial <= MIN_SPATIAL:
        failures.append(f"the refractive water barely differs from the plain "
                        f"baseline (distortion {spatial:.1f}) - not refracting")
    if on_motion <= MIN_ON_MOTION:
        failures.append(f"the refraction does not change between frames "
                        f"(motion {on_motion:.2f}) - the ripple is not driving it")
    if on_motion <= MIN_MOTION_RATIO * max(off_motion, 0.02):
        failures.append(f"the refraction ({on_motion:.2f}) does not move clearly "
                        f"more than the straight baseline ({off_motion:.2f})")
    if failures:
        for line in failures:
            print(f"FAIL: {line}")
        return 1
    print("water_refraction_looks_right: the lakebed bends through the surface "
          "(wavy displacement) and moves as the ripple scrolls")
    return 0


if __name__ == "__main__":
    sys.exit(main())
