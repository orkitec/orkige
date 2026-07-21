#!/usr/bin/env python3
"""Assert planar water reflection MIRRORS the actual scene in the surface.

Planar reflection (RenderWaterDesc::planarReflection): a dedicated camera
reflected across the water plane renders the scene (the water surface hidden,
geometry below the plane clipped) into a reflection RenderTexture, which the
water shader samples at the fragment's ripple-perturbed screen UV - so the
surface shows a MIRROR of the sky + terrain + objects rather than just the sky
IBL cubemap. This drives the ORKIGE_DEMO_WATER reflection leg, which stands a
TALL, brightly MAGENTA marker wall above the water, and runs it two ways:

  * REFLECTION ON  (ORKIGE_DEMO_WATER_REFLECT)     - the marker's magenta mirror
    appears in the water band
  * REFLECTION OFF (ORKIGE_DEMO_WATER_REFLECT_OFF) - the byte-stable
    sky-reflection look, no marker mirror in the water

In the water band (below the scene, full width) it asserts:

  * the MARKER'S MAGENTA appears in the band with reflection ON (a hue neither
    the teal water nor the blue sky carries, so its mirror is unambiguous), and
  * it is ABSENT with reflection OFF (the dark Stage-1 water), and
  * the band ON differs strongly from the OFF baseline (the mirror renders).

Capability-gated: the demo logs `supported=0/1`. A backend/context that cannot
do planar reflection (the next flavor, a GLES/WebGL classic context) renders the
sky-reflection look both ways, so the probe SKIPS (exit 77) rather than failing.

Pure stdlib; reuses the water-probe PNG decoder in this directory.
"""

import argparse
import os
import re
import subprocess
import sys

# the sibling water probe's zlib PNG decoder (this file's directory is on
# sys.path[0] when run as a script)
from run_water_probe_test import decode_png


def _band(path):
    """RGB samples over the WATER band (fy 0.76..0.96, below the scene, full
    width) - the region the water surface fills and reflects into."""
    width, height, channels, pixels = decode_png(path)
    samples = []
    for fy in (0.78, 0.82, 0.86, 0.90, 0.94):
        for step in range(2, 39):
            fx = step / 40.0
            x = min(width - 1, int(fx * width))
            y = min(height - 1, int(fy * height))
            idx = (y * width + x) * channels
            samples.append((pixels[idx], pixels[idx + 1], pixels[idx + 2]))
    return samples


def _magentaness(samples):
    """mean 'how magenta' over the band: max(0, min(R,B) - G) / 255. Magenta
    (high R+B, low G - the marker) scores high; teal water and blue sky (low R)
    score ~0."""
    total = 0.0
    for r, g, b in samples:
        total += max(0, min(r, b) - g)
    return total / (len(samples) * 255.0) if samples else 0.0


def _mean_abs_lum_diff(a, b):
    total = 0.0
    for (ra, ga, ba), (rb, gb, bb) in zip(a, b):
        la = 0.299 * ra + 0.587 * ga + 0.114 * ba
        lb = 0.299 * rb + 0.587 * gb + 0.114 * bb
        total += abs(la - lb)
    return total / len(a) if a else 0.0


def _run(binary, out, reflect_on, tag):
    shot = os.path.join(out, f"reflect_{tag}.png")
    env = dict(os.environ, ORKIGE_DEMO_WATER="1", ORKIGE_DEMO_FRAMES="70",
               ORKIGE_AUTOMATED_RUN="1", ORKIGE_DEMO_SCREENSHOT=shot)
    env["ORKIGE_DEMO_WATER_REFLECT" if reflect_on
        else "ORKIGE_DEMO_WATER_REFLECT_OFF"] = "1"
    try:
        result = subprocess.run([binary], env=env, timeout=120,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        print(f"FAIL: demo_water reflection leg ({tag}) hung")
        return None
    if result.returncode != 0:
        print(f"FAIL: demo_water reflection leg ({tag}) exited "
              f"{result.returncode}")
        return None
    if not os.path.exists(shot):
        print(f"FAIL: no reflection frame captured at {shot}")
        return None
    text = result.stdout.decode("utf-8", "replace")
    match = re.search(r"reflection leg up.*supported=(\d)", text)
    supported = match.group(1) == "1" if match else True
    return shot, supported


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
    on_shot, supported = on
    if not supported:
        print("SKIP: planar reflection is not supported on this "
              "backend/context (the water reflects the sky IBL cubemap)")
        return 77

    off = _run(args.binary, args.out, False, "off")
    if off is None:
        return 1
    off_shot, _ = off

    on_band = _band(on_shot)
    off_band = _band(off_shot)
    on_mag = _magentaness(on_band)
    off_mag = _magentaness(off_band)
    lum_diff = _mean_abs_lum_diff(on_band, off_band)

    # thresholds (measured, frame-locked/deterministic): the magenta marker's
    # mirror fills a large part of the band ON (magentaness ~0.2+) while the OFF
    # dark water carries essentially none (~0.01); the mirror changes the band
    # luminance by tens of levels vs the dark baseline. Wide margins below.
    MIN_ON_MAGENTA = 0.06       # the marker's mirror is clearly in the band
    MIN_MAGENTA_RATIO = 4.0     # far more magenta ON than the dark OFF baseline
    MIN_LUM_DIFF = 12.0         # the mirror renders (band differs from baseline)

    print(f"reflection probe: on_magenta={on_mag:.3f} (>{MIN_ON_MAGENTA}), "
          f"off_magenta={off_mag:.3f} (ratio>{MIN_MAGENTA_RATIO}), "
          f"lum_diff={lum_diff:.1f} (>{MIN_LUM_DIFF})")

    failures = []
    if on_mag <= MIN_ON_MAGENTA:
        failures.append(f"the marker's magenta mirror is not in the water band "
                        f"(magenta {on_mag:.3f}) - the reflection is not showing "
                        f"the scene")
    if on_mag <= MIN_MAGENTA_RATIO * max(off_mag, 0.005):
        failures.append(f"the reflection's magenta ({on_mag:.3f}) is not clearly "
                        f"more than the OFF baseline ({off_mag:.3f})")
    if lum_diff <= MIN_LUM_DIFF:
        failures.append(f"the water band barely differs from the OFF baseline "
                        f"(lum diff {lum_diff:.1f}) - the mirror is not rendering")
    if failures:
        for line in failures:
            print(f"FAIL: {line}")
        return 1
    print("water_reflection_looks_right: the marker's mirror image appears in "
          "the water surface with reflection on, absent off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
