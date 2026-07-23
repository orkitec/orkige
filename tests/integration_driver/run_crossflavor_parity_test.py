#!/usr/bin/env python3
"""Cross-FLAVOR look parity on a benchmark vignette: next vs classic, one gate.

Every other benchmark probe bands each flavor SEPARATELY (is-this-flavor-in-
its-own-corridor), so the two flavors could drift arbitrarily far apart without
any gate noticing - the black-water/cyan-sky regression class. This test boots
the SAME scene on BOTH flavors' players, captures the same deterministic frame,
and compares broad scene REGIONS (sky band, water band, terrain band) between
the two images:

  * per-region MEAN colour must agree within a tolerance-parity corridor
    (loose enough for the documented BRDF/vertex-vs-pixel differences, tight
    enough that a black, inverted-hue or washed-out region on ONE flavor
    fails);
  * both flavors must show the water's bright sun highlight (the specular
    streak - its absence on one flavor was a real regression).

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

    # broad regions of the lake showcase framing (fractions survive window-
    # size changes; the sky band starts right of the HUD box). The camera
    # looks low ACROSS the water toward the sun: sky above, the shore island
    # band mid-frame (sampled left of the sun-streak column, whose breadth
    # differs legitimately between the flavors' specular models), open water
    # across the lower half.
    regions = {
        # the open sky above the horizon
        "sky": (sx(0.35), sy(0.10), sx(0.95), sy(0.24)),
        # the shore island band (left half, clear of the streak column)
        "terrain": (sx(0.25), sy(0.31), sx(0.44), sy(0.41)),
        # the open water foreground, flanking the streak
        "water": (sx(0.05), sy(0.55), sx(0.35), sy(0.85)),
    }
    # tolerance-parity corridor: catches a black/inverted/washed region on one
    # flavor (those sit at deltas of 120+) while allowing the documented
    # residual differences. The classic atmospheric object fog (the generated
    # materials' fog stage + the water programs' fog block now run the default
    # backend's exact haze-colour/transmittance formulas) dropped the shore
    # band from its historical ~32 delta to a measured 10 and the sky to 7;
    # the water surface's sky mirror now carries the default backend's exact
    # env-term energy over a ratio-true HDR sky capture (the former visually
    # dialed mirror of the CLAMPED capture read as a pale pedestal), which
    # dropped the water band from its ~20 to a measured 5 and the shore band
    # to 5. The showcase lake then opened its water to opacity 0.55 (the
    # refracted scene now carries 45% of the compose instead of 15%), which
    # EXPOSES the remaining flavor seams at 3x their former weight. The
    # specular-hemisphere ambient lane and the shared ratio-true water
    # mirror source landed (probe-identical hemisphere response on both
    # flavors; both mirrors read one HDR capture), which trimmed the water
    # band from 26 to a measured 25 (sky 7, terrain 4): the residual is the
    # flavors' ENV-FILL sampling of the environment chain's deep mips - the
    # classic stage reads the mathematical face average there while the
    # default backend's native env sample reads measurably darker from the
    # SAME chain bytes (probe-isolated on the blue lane; its exact next-side
    # mechanism is the named successor task, needing a fill-only probe leg
    # that suspends the atmosphere's per-frame sun re-drive). The corridor
    # tracks that honestly and re-tightens when that sampling seam closes.
    MEAN_TOLERANCE = 28.0

    for name, (x0, y0, x1, y1) in regions.items():
        mean_next = region_mean(img_next, x0, y0, x1, y1)
        mean_classic = region_mean(img_classic, x0, y0, x1, y1)
        deltas = [abs(a - b) for a, b in zip(mean_next, mean_classic)]
        print(f"crossflavor_parity: {name}: next=({mean_next[0]:.0f},"
              f"{mean_next[1]:.0f},{mean_next[2]:.0f}) classic="
              f"({mean_classic[0]:.0f},{mean_classic[1]:.0f},"
              f"{mean_classic[2]:.0f}) delta=({deltas[0]:.0f},"
              f"{deltas[1]:.0f},{deltas[2]:.0f})")
        if max(deltas) > MEAN_TOLERANCE:
            fail(f"region '{name}' diverges between flavors: max channel "
                 f"delta {max(deltas):.0f} > {MEAN_TOLERANCE} - the flavors "
                 "no longer show the same scene (capture pair kept in "
                 f"{args.dir})")

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
