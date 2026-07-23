#!/usr/bin/env python3
"""Cross-FLAVOR look parity on a benchmark vignette: next vs classic, one gate.

Every other benchmark probe bands each flavor SEPARATELY (is-this-flavor-in-
its-own-corridor), so the two flavors could drift arbitrarily far apart without
any gate noticing - the black-water/cyan-sky regression class. This test boots
the SAME scene on BOTH flavors' players, captures the same deterministic frame,
and compares broad scene REGIONS (per-scene profiles in PROFILES - sky band,
water/mirror bands, terrain band) between the two images:

  * per-region MEAN colour must agree within that region's measured
    tolerance-parity corridor (loose enough for the documented
    BRDF/vertex-vs-pixel differences, tight enough that a black,
    inverted-hue or washed-out region on ONE flavor fails - and, on the
    mirror scene, that a planar mirror-strength drift fails);
  * scenes carrying the streak contract: both flavors must show the water's
    bright sun highlight (the specular streak - its absence on one flavor
    was a real regression).

Needs BOTH build trees: the sibling flavor's player path is passed in and the
test SKIPS (exit 77) when it is not built - CI jobs build one flavor per job,
so this gate runs where both trees exist (a developer machine / the local
verification pass).

Pure stdlib (the sibling pixel test's PNG decoder). Exit codes: 0 pass,
1 fail, 77 skip.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_benchmark_pixel_test import decode_png, pixel  # noqa: E402


#: per-scene comparison profiles: named regions as frame fractions
#: (fx0, fy0, fx1, fy1, corridor) plus whether the scene carries the
#: sun-streak contract. Corridors are tolerance-parity: measured on the
#: deterministic capture recipe, wide enough for the documented residual
#: flavor differences and tight enough that a black/inverted/washed region
#: on one flavor (deltas 120+) or a mirror-strength drift fails.
PROFILES = {
    # the refraction-only lake framing: the camera looks low ACROSS the water
    # toward the sun - sky above, the shore island band mid-frame (sampled
    # left of the sun-streak column, whose breadth differs legitimately
    # between the flavors' specular models), open water across the lower
    # half. The 28 corridor's history: the classic atmospheric object fog
    # (the generated materials' fog stage + the water programs' fog block
    # now run the default backend's exact haze-colour/transmittance
    # formulas) dropped the shore band from its historical ~32 delta to a
    # measured 10 and the sky to 7; the ratio-true HDR water mirror dropped
    # the water band from its ~20 to a measured 5. The showcase lake then
    # opened its water to opacity 0.55 (the refracted scene carries 45% of
    # the compose), which exposes the remaining flavor seams at 3x their
    # former weight; the specular-hemisphere lane + the shared mirror
    # source trimmed the water band to a measured 25 (sky 7, terrain 4).
    # The residual is the flavors' ENV-FILL sampling of the environment
    # chain's deep mips (the classic stage reads the mathematical face
    # average, the default backend's native env sample reads measurably
    # darker from the SAME chain bytes - the named successor task). The
    # corridor tracks that honestly and re-tightens when that seam closes.
    "lake.oscene": {
        "regions": {
            # the open sky above the horizon
            "sky": (0.35, 0.10, 0.95, 0.24, 28.0),
            # the shore island band (left half, clear of the streak column)
            "terrain": (0.25, 0.31, 0.44, 0.41, 28.0),
            # the open water foreground, flanking the streak
            "water": (0.05, 0.55, 0.35, 0.85, 28.0),
        },
        "streak": True,
    },
    # the planar-mirror sibling: a low, close camera over a calm surface, the
    # widened shore ridge spanning the far edge, waterline rocks. The tight
    # gates are where BOTH flavors mirror the SAME content: the waterline
    # strips (the mirrored ridge, measured delta 6-8) and the rock-mirror
    # band (measured 17; corridor 26 - the old 3x-strong next mirror
    # calibration measures 35 here, so a mirror-strength drift breaches).
    # The open lower water mirrors the MID/HIGH sky, where two documented
    # approximations diverge: the flavors' sky-dome colour away from the
    # horizon (a seam nothing but a mirror ever sees - the direct sky bands
    # match at delta 9) and the classic mirror's screen-UV paint, which
    # stretches the mirrored ridge further down than the default backend's
    # true projective mapping. Measured 44 healthy vs 69 under the
    # 3x-strong calibration - the 55 corridor bites on strength drift and
    # re-tightens when the sky-dome seam closes. No streak contract: the
    # mirrored sun is occluded by the ridge, so the classic flavor's
    # analytic streak has no default-backend counterpart in this framing
    # (the lake scene carries the streak gate).
    "mirrorlake.oscene": {
        "regions": {
            "sky": (0.30, 0.02, 0.95, 0.08, 22.0),
            "shore": (0.15, 0.12, 0.85, 0.22, 20.0),
            "watermirror_l": (0.08, 0.25, 0.40, 0.28, 20.0),
            "watermirror_r": (0.60, 0.25, 0.92, 0.28, 20.0),
            "rockmirror": (0.38, 0.38, 0.52, 0.50, 26.0),
            "water_open": (0.05, 0.36, 0.35, 0.52, 55.0),
        },
        "streak": False,
    },
}


def fail(message):
    print("crossflavor_parity: FAIL: " + message)
    sys.exit(1)


def skip(message):
    print("crossflavor_parity: SKIP: " + message)
    sys.exit(77)


def capture(player, repo, scene, shot, out_dir, frames):
    env = dict(os.environ)
    env.update({
        "ORKIGE_DEMO_FRAMES": str(frames),
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": out_dir,
        # deterministic frame: freeze the wall-time orbit, un-cap the ramp
        "ORKIGE_CVARS": "benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0",
    })
    result = subprocess.run(
        [player, scene, "--project", "projects/benchmark"],
        cwd=repo, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=420)
    if result.returncode != 0:
        fail(f"{player} exited {result.returncode}:\n" +
             result.stdout.decode("utf-8", "replace")[-2000:])
    if not os.path.exists(shot):
        fail(f"{player} wrote no screenshot to {shot}")
    return decode_png(shot)


def region_mean(img, x0, y0, x1, y1, step=4):
    width, height, channels, data = img
    total = [0.0, 0.0, 0.0]
    count = 0
    for y in range(y0, min(y1, height), step):
        for x in range(x0, min(x1, width), step):
            r, g, b = pixel(data, channels, width, x, y)
            total[0] += r
            total[1] += g
            total[2] += b
            count += 1
    return tuple(t / max(count, 1) for t in total)


def region_max_luma(img, x0, y0, x1, y1, step=2):
    width, height, channels, data = img
    best = 0.0
    for y in range(y0, min(y1, height), step):
        for x in range(x0, min(x1, width), step):
            r, g, b = pixel(data, channels, width, x, y)
            luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
            best = max(best, luma)
    return best


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player-next", required=True)
    parser.add_argument("--player-classic", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--scene", default="scenes/lake.oscene")
    parser.add_argument("--frames", type=int, default=90)
    args = parser.parse_args()

    if not os.path.exists(args.player_next):
        skip("next player not built: " + args.player_next)
    if not os.path.exists(args.player_classic):
        skip("classic player not built: " + args.player_classic)

    os.makedirs(args.dir, exist_ok=True)
    shot_next = os.path.join(args.dir, "next.png")
    shot_classic = os.path.join(args.dir, "classic.png")
    img_next = capture(args.player_next, args.repo, args.scene, shot_next,
                       args.dir, args.frames)
    img_classic = capture(args.player_classic, args.repo, args.scene,
                          shot_classic, args.dir, args.frames)

    if img_next[0] != img_classic[0] or img_next[1] != img_classic[1]:
        fail(f"capture sizes differ: {img_next[0]}x{img_next[1]} vs "
             f"{img_classic[0]}x{img_classic[1]}")
    width, height = img_next[0], img_next[1]

    def sx(fraction):
        return int(width * fraction)

    def sy(fraction):
        return int(height * fraction)

    profile = PROFILES.get(os.path.basename(args.scene))
    if profile is None:
        fail(f"no region profile for scene '{args.scene}' - add one to "
             "PROFILES with measured corridors")
    regions = {name: (sx(fx0), sy(fy0), sx(fx1), sy(fy1), tolerance)
               for name, (fx0, fy0, fx1, fy1, tolerance)
               in profile["regions"].items()}
    for name, (x0, y0, x1, y1, tolerance) in regions.items():
        mean_next = region_mean(img_next, x0, y0, x1, y1)
        mean_classic = region_mean(img_classic, x0, y0, x1, y1)
        deltas = [abs(a - b) for a, b in zip(mean_next, mean_classic)]
        print(f"crossflavor_parity: {name}: next=({mean_next[0]:.0f},"
              f"{mean_next[1]:.0f},{mean_next[2]:.0f}) classic="
              f"({mean_classic[0]:.0f},{mean_classic[1]:.0f},"
              f"{mean_classic[2]:.0f}) delta=({deltas[0]:.0f},"
              f"{deltas[1]:.0f},{deltas[2]:.0f}) tol={tolerance:.0f}")
        if max(deltas) > tolerance:
            fail(f"region '{name}' diverges between flavors: max channel "
                 f"delta {max(deltas):.0f} > {tolerance} - the flavors "
                 "no longer show the same scene (capture pair kept in "
                 f"{args.dir})")

    if profile.get("streak"):
        # the sun's specular streak on the water: both flavors must carry a
        # bright highlight down the frame centre (its absence on one flavor
        # was the flat-lifeless-water regression)
        streak_box = (sx(0.40), sy(0.35), sx(0.62), sy(0.75))
        STREAK_MIN = 200.0
        for label, img in (("next", img_next), ("classic", img_classic)):
            luma = region_max_luma(img, *streak_box)
            print(f"crossflavor_parity: sun-streak max luma [{label}] = "
                  f"{luma:.0f}")
            if luma < STREAK_MIN:
                fail(f"{label} shows no bright sun highlight on the water "
                     f"(max luma {luma:.0f} < {STREAK_MIN}) - the specular "
                     "streak is missing on this flavor")

    print("crossflavor_parity: PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
