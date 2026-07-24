#!/usr/bin/env python3
"""Assert the water's waves PERTURB the planar mirror on BOTH flavors.

Physically a rippled water surface bends the reflected ray, so the mirror image
shimmers with the swell + detail normals. The classic water program perturbs its
mirror sample UV by the wave normal's horizontal slope (RenderSystemClassic
WaterReflect_fs); the next flavor's native Ogre::PlanarReflections samples a FLAT
screen-projected mirror, so its NextBackend override (createOrUpdateWaterDatablock
MIRROR RIPPLE block) adds the same slope-driven UV perturbation. Without either,
the mirror reads glassy/static no matter the waves.

The gate is PACING-INDEPENDENT. Wall-clock frame pacing makes the absolute
between-frame wobble nondeterministic (a slow/debug host advances the wave clock
erratically), so an absolute two-phase delta cannot separate a glassy mirror from
a live one in every config. Instead this gate captures the SAME frame (identical
wave phase) TWICE per flavor - once normally, once with the `ORKIGE_WATER_FLAT_MIRROR`
diagnostic seam that zeroes ONLY the ripple perturbation - and measures how much
the reflective band changes between them. Because only the perturbation differs
(same phase, same everything else), a working mirror moves the band while a mirror
that stopped rippling makes the two frames IDENTICAL (delta exactly 0). Taking the
MAX over a few frames catches a phase where the surface is meaningfully wavy.

Measured (Mirror Lake, reflective band, max over frames), rippled-vs-flat:

  regressed / glassy mirror   0.000  (identical frames - the guarded failure)
  next   (working)            ~0.04 (debug) .. 0.10 (release)
  classic(working)            ~0.13 (debug) .. 0.39 (release)

so a floor between 0 and the working minimum catches a mirror that stopped
rippling on either flavor, in either build config, with wide headroom - and it
does not depend on wall-clock pacing at all.

next-only: gates next alone (the fix's regression guard). With the classic sibling
player built, gates BOTH. Pure stdlib.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_benchmark_pixel_test import decode_png, pixel  # noqa: E402

#: the reflective surface band (frame fractions): the strip below the waterline
#: carrying the cubes' mirror images + the sun's reflection streak - the whole
#: planar mirror, which rides one perturbation.
MIRROR_REGION = (0.08, 0.28, 0.92, 0.50)
#: wave phases (demo frame counts) spanning the Mirror Lake vignette (still shown
#: at frame 360); six of them so the MAX reliably catches a meaningfully wavy
#: phase regardless of how the host's pacing lands the wave clock on a frame.
PHASES = (80, 130, 180, 230, 290, 350)
#: the shared existence floor: the working MAX is >= ~0.03 in the leanest (debug)
#: config and ~0.04-0.39 in release, while a regressed glassy mirror is exactly
#: 0.0 (identical normal/flat frames); 0.015 sits well inside that gap in EVERY
#: build config and does not depend on wall-clock pacing.
WOBBLE_FLOOR = 0.015


def log(msg):
    print("water_wobble_probe: " + msg, flush=True)


def capture(player, repo, frames, shot, out_dir, flat):
    env = dict(os.environ)
    env.update({
        "ORKIGE_DEMO_FRAMES": str(frames),
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_AUTOMATED_RUN": "1",
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": out_dir,
        # freeze the wall-time camera orbit + un-cap the feature ramp so the
        # ONLY thing that differs between the normal and flat captures is the
        # mirror perturbation
        "ORKIGE_CVARS": "benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0",
    })
    if flat:
        env["ORKIGE_WATER_FLAT_MIRROR"] = "1"
    if os.path.exists(shot):
        os.unlink(shot)
    result = subprocess.run(
        [player, "scenes/mirrorlake.oscene", "--project", "projects/benchmark"],
        cwd=repo, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=300)
    if result.returncode != 0:
        log(result.stdout.decode("utf-8", "replace")[-1500:])
        log("FAIL: %s exited %d (frame %d, flat=%s)" %
            (player, result.returncode, frames, flat))
        return None
    if not os.path.exists(shot):
        log("FAIL: %s wrote no screenshot (frame %d, flat=%s)" %
            (player, frames, flat))
        return None
    return decode_png(shot)


def region_lum_delta(img_a, img_b):
    wa, ha, ca, da = img_a
    wb, hb, cb, db = img_b
    if (wa, ha) != (wb, hb):
        return None
    fx0, fy0, fx1, fy1 = MIRROR_REGION
    total = 0.0
    count = 0
    for y in range(int(fy0 * ha), min(int(fy1 * ha), ha), 2):
        for x in range(int(fx0 * wa), min(int(fx1 * wa), wa), 2):
            ra, ga, ba = pixel(da, ca, wa, x, y)
            rb, gb, bb = pixel(db, cb, wb, x, y)
            la = 0.299 * ra + 0.587 * ga + 0.114 * ba
            lb = 0.299 * rb + 0.587 * gb + 0.114 * bb
            total += abs(la - lb)
            count += 1
    return total / count if count else 0.0


def measure_flavor(player, repo, out_dir):
    """max over frames of the rippled-vs-flat reflective-band change."""
    best = 0.0
    for phase in PHASES:
        normal = capture(player, repo, phase,
                         os.path.join(out_dir, "n_%d.png" % phase), out_dir, False)
        if normal is None:
            return None
        flat = capture(player, repo, phase,
                       os.path.join(out_dir, "f_%d.png" % phase), out_dir, True)
        if flat is None:
            return None
        delta = region_lum_delta(normal, flat)
        if delta is None:
            log("FAIL: normal/flat frames differ in size")
            return None
        best = max(best, delta)
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player-next", required=True)
    parser.add_argument("--player-classic", default="",
                        help="optional sibling flavor; gated too when present")
    parser.add_argument("--dir", required=True)
    args = parser.parse_args()

    if not os.path.exists(args.player_next):
        log("SKIP: next player not built: " + args.player_next)
        return 77
    os.makedirs(args.dir, exist_ok=True)

    flavors = [("next", args.player_next)]
    if args.player_classic and os.path.exists(args.player_classic):
        flavors.append(("classic", args.player_classic))
    else:
        log("note: classic sibling player not built - gating next only")

    failures = []
    for name, player in flavors:
        out_dir = os.path.join(args.dir, name)
        os.makedirs(out_dir, exist_ok=True)
        wobble = measure_flavor(player, args.repo, out_dir)
        if wobble is None:
            return 1
        verdict = "ok" if wobble > WOBBLE_FLOOR else "TOO STATIC"
        log("%s mirror perturbation = %.3f (floor %.3f) - %s" %
            (name, wobble, WOBBLE_FLOOR, verdict))
        if wobble <= WOBBLE_FLOOR:
            failures.append(
                "%s: the wave ripple does not move the planar mirror "
                "(%.3f <= %.3f) - the reflection is flat/glassy"
                % (name, wobble, WOBBLE_FLOOR))

    if failures:
        for line in failures:
            log("FAIL: " + line)
        return 1
    log("water_mirror_wobble: the wave ripple perturbs the mirror on "
        "every gated flavor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
