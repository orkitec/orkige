#!/usr/bin/env python3
"""The static mobility flag's end-to-end contract, per flavor.

Boots projects/benchmark scenes/fixture_static.oscene (8 STATIC prop cubes +
one dynamic twin, deterministic and director-free) four times and checks the
whole pillar in one ctest:

  1. TOGGLE IDENTITY - the scene rendered with the static fast path
     (r.staticScene=1, the default) must match the same scene rendered fully
     dynamic (r.staticScene=0). On the next flavor the comparison is
     BYTE-EXACT (SCENE_STATIC only skips recomputation - the GPU sees the
     same matrices). On classic a tight tolerance applies: StaticGeometry
     re-expresses vertices relative to the region origin, so isolated edge
     pixels may differ by float rounding (measured 2 px per 921600).
  2. DRAW-CALL WIN - the batch counts the player reports must show the
     classic bake collapsing the 8 static cubes into region draws
     (before-move batches with the gate ON strictly below OFF); next keeps
     its native auto-batching either way.
  3. THE MOBILITY CONTRACT - ORKIGE_STATICMOVE_SELFCHECK moves the static
     "Static3" at frame 30. The run must log EXACTLY ONE mobility-contract
     warning, the move must land (the selfcheck verifies the world position),
     the batch delta must match the flavor (classic +1: the demoted entity
     draws individually again; next +0), and the MOVED frame must render
     the same pixels as a fully-dynamic run of the same move (the repair
     path is correct, not just warned).

Pure stdlib. Exit codes: 0 pass, 1 fail.
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_benchmark_pixel_test import decode_png, pixel  # noqa: E402

# classic tolerance for the StaticGeometry vertex re-expression (see above);
# next must be byte-identical so it gets zeroes
CLASSIC_MEAN_TOLERANCE = 0.01
CLASSIC_OUTLIER_TOLERANCE = 8
CLASSIC_OUTLIER_FRACTION = 0.0005

WARNING_PATTERN = re.compile(r"on STATIC node .* static means static")
RESULT_PATTERN = re.compile(
    r"staticmove selfcheck complete - .*batches before=(\d+) after=(\d+)")


def log(msg):
    print("run_static_contract_test: " + msg, flush=True)


def fail(msg):
    print("run_static_contract_test: FAILED - " + msg, flush=True)
    sys.exit(1)


def run_player(args, outdir, tag, static_on, move, frames=90):
    shot = os.path.join(outdir, tag + ".png")
    if os.path.exists(shot):
        os.unlink(shot)
    env = dict(os.environ)
    env.update({
        "ORKIGE_DEMO_FRAMES": str(frames),
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_CVARS": "r.staticScene=%d" % (1 if static_on else 0),
    })
    if move:
        env["ORKIGE_STATICMOVE_SELFCHECK"] = "1"
    cmd = [args.player, "scenes/fixture_static.oscene", "--project",
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


def compare(shot_a, shot_b, classic, what):
    a = decode_png(shot_a)
    b = decode_png(shot_b)
    wa, ha, ca, pa = a
    wb, hb, cb, pb = b
    if (wa, ha) != (wb, hb):
        fail("%s: image sizes differ (%dx%d vs %dx%d)" % (what, wa, ha, wb, hb))
    channels = min(ca, cb, 3)
    total = 0
    outliers = 0
    for y in range(ha):
        row_a = y * wa
        for x in range(wa):
            base_a = (row_a + x) * ca
            base_b = (row_a + x) * cb
            worst = 0
            for c in range(channels):
                diff = abs(pa[base_a + c] - pb[base_b + c])
                total += diff
                if diff > worst:
                    worst = diff
            if worst > CLASSIC_OUTLIER_TOLERANCE:
                outliers += 1
    count = wa * ha
    mean = total / float(count * channels)
    frac = outliers / float(count)
    log("%s: mean %.5f, outliers(>%d) %.6f%%"
        % (what, mean, CLASSIC_OUTLIER_TOLERANCE, 100.0 * frac))
    if classic:
        if mean > CLASSIC_MEAN_TOLERANCE or frac > CLASSIC_OUTLIER_FRACTION:
            fail("%s: beyond the classic StaticGeometry rounding tolerance"
                 % what)
    elif mean > 0.0:
        fail("%s: the next flavor must render BYTE-IDENTICAL pixels" % what)


def batches_from(output, tag):
    match = RESULT_PATTERN.search(output)
    if not match:
        fail("%s: no staticmove result line in the player output" % tag)
    return int(match.group(1)), int(match.group(2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--flavor", required=True, choices=("next", "classic"))
    args = parser.parse_args()
    classic = args.flavor == "classic"
    os.makedirs(args.dir, exist_ok=True)

    # 1+2: the toggle-identity pair (no move)
    out_on, shot_on = run_player(args, args.dir, "static_on", True, False)
    out_off, shot_off = run_player(args, args.dir, "static_off", False, False)
    if WARNING_PATTERN.search(out_on) or WARNING_PATTERN.search(out_off):
        fail("a mobility-contract warning fired without any move")
    compare(shot_on, shot_off, classic, "toggle identity")

    # 3: the contract pair (Static3 moved at frame 30 in both runs)
    out_move_on, shot_move_on = run_player(
        args, args.dir, "move_on", True, True)
    out_move_off, shot_move_off = run_player(
        args, args.dir, "move_off", False, True)

    warnings = len(WARNING_PATTERN.findall(out_move_on))
    if warnings != 1:
        fail("the static move must warn EXACTLY ONCE (saw %d)" % warnings)
    if WARNING_PATTERN.search(out_move_off):
        fail("a fully-dynamic run must not warn about static moves")

    before_on, after_on = batches_from(out_move_on, "move_on")
    before_off, after_off = batches_from(out_move_off, "move_off")
    expected_delta = 1 if classic else 0
    if after_on - before_on != expected_delta:
        fail("the %s move delta must be %+d draw batches (before=%d after=%d)"
             % (args.flavor, expected_delta, before_on, after_on))
    if after_off != before_off:
        fail("a dynamic move must not change the batch count (%d -> %d)"
             % (before_off, after_off))
    if classic and before_on >= before_off:
        fail("the static bake shows no draw-call win (on=%d vs off=%d)"
             % (before_on, before_off))
    log("batches: gate on %d->%d, gate off %d->%d"
        % (before_on, after_on, before_off, after_off))

    compare(shot_move_on, shot_move_off, classic, "repair-path identity")
    log("OK")


if __name__ == "__main__":
    main()
