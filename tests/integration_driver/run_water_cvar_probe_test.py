#!/usr/bin/env python3
"""Assert the live `water.*` cvar tier reaches the rendered water, and that its
DEFAULTS change nothing at all.

The water/mirror look constants (the mirror's weight, its fresnel and body
albedo laws, the next flavor's baked sample LOD and ripple distortion) are
`water.*` cvars - engine_render/RenderWaterTuning.h - so the look is dialled in
at runtime through any cvar door (the console, MSG_SET_CVAR, MCP `set_cvar`,
an `ORKIGE_CVAR_*` boot seed) instead of recompiled. Two properties matter and
this probe measures both, on the ORKIGE_DEMO_WATER reflection leg (the same
scene run_water_reflection_probe_test.py drives - a TALL magenta marker wall
whose mirror fills the water band):

  * LIVE: seeding `water.mirrorSpecular` to 0 - the mirror's weight in the
    environment specular term - visibly changes the water band. The mirror is
    the term that knob scales, so at 0 the marker's mirror leaves the surface.

  * DEFAULT-NEUTRAL: seeding the very same knob with its shipped DEFAULT
    produces a frame BYTE-IDENTICAL to a run that seeds nothing. This is the
    hard requirement behind the pixel gates: adding the tier must not have
    moved a single pixel, and a default that drifted would show up here rather
    than as an unexplained failure in the parity/self-drift gates.

The seed rides the general `ORKIGE_CVAR_<name>` boot hook (underscores become
dots), so this exercises the same registry path every other cvar door reaches.

Capability-gated: the demo logs `supported=0/1` for planar reflection. Where the
mirror cannot render there is no mirror term for the knob to scale, so the probe
SKIPS (exit 77) rather than failing. Skips too if the app is unbuilt.

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

#: the knob this probe drives, as its environment-seed name (underscores in the
#: variable's suffix become dots in the cvar name)
SEED_VARIABLE = "ORKIGE_CVAR_water_mirrorSpecular"
#: the shipped default, which must reproduce the untouched look exactly
DEFAULT_VALUE = "0.43"
#: the mirror switched off - the largest honest move this one knob can make
CHANGED_VALUE = "0"


def _band(path):
    """RGB samples over the water rows where the fresnel mirror reads
    strongest - the same band the reflection probe measures (upper water rows,
    left/right edge columns, away from the sun's specular streak)."""
    width, height, channels, pixels = decode_png(path)
    samples = []
    for fy in (0.58, 0.63, 0.68, 0.73, 0.78, 0.83):
        for fx100 in list(range(2, 30, 2)) + list(range(72, 98, 2)):
            fx = fx100 / 100.0
            x = min(width - 1, int(fx * width))
            y = min(height - 1, int(fy * height))
            idx = (y * width + x) * channels
            samples.append((pixels[idx], pixels[idx + 1], pixels[idx + 2]))
    return samples


def _mean_abs_lum_diff(a, b):
    total = 0.0
    for (ra, ga, ba), (rb, gb, bb) in zip(a, b):
        la = 0.299 * ra + 0.587 * ga + 0.114 * ba
        lb = 0.299 * rb + 0.587 * gb + 0.114 * bb
        total += abs(la - lb)
    return total / len(a) if a else 0.0


def _magentaness(samples):
    """mean 'how magenta' over the band - the marker's mirror, the signal the
    mirror weight scales (@see run_water_reflection_probe_test.py)"""
    total = 0.0
    for r, g, b in samples:
        total += max(0, min(r, b) - g)
    return total / (len(samples) * 255.0) if samples else 0.0


def _run(binary, out, tag, seed):
    """one reflection-leg capture; seed is None (nothing set) or the value to
    put on SEED_VARIABLE"""
    shot = os.path.join(out, f"water_cvar_{tag}.png")
    env = dict(os.environ, ORKIGE_DEMO_WATER="1", ORKIGE_DEMO_FRAMES="70",
               ORKIGE_AUTOMATED_RUN="1", ORKIGE_DEMO_SCREENSHOT=shot,
               ORKIGE_DEMO_WATER_REFLECT="1")
    env.pop(SEED_VARIABLE, None)
    if seed is not None:
        env[SEED_VARIABLE] = seed
    try:
        result = subprocess.run([binary], env=env, timeout=120,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        print(f"FAIL: demo_water cvar leg ({tag}) hung")
        return None
    if result.returncode != 0:
        print(f"FAIL: demo_water cvar leg ({tag}) exited {result.returncode}")
        return None
    if not os.path.exists(shot):
        print(f"FAIL: no frame captured at {shot}")
        return None
    text = result.stdout.decode("utf-8", "replace")
    match = re.search(r"reflection leg up.*supported=(\d)", text)
    supported = match.group(1) == "1" if match else True
    return shot, supported


def _read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="the hello_orkige app")
    parser.add_argument("--out", required=True, help="scratch dir for the frames")
    args = parser.parse_args()
    if not os.path.exists(args.binary):
        print(f"SKIP: demo app not built: {args.binary}")
        return 77
    os.makedirs(args.out, exist_ok=True)

    baseline = _run(args.binary, args.out, "baseline", None)
    if baseline is None:
        return 1
    baseline_shot, supported = baseline
    if not supported:
        print("SKIP: planar reflection is not supported on this "
              "backend/context - there is no mirror term for the knob to scale")
        return 77

    seeded_default = _run(args.binary, args.out, "default", DEFAULT_VALUE)
    if seeded_default is None:
        return 1
    default_shot, _ = seeded_default

    changed = _run(args.binary, args.out, "changed", CHANGED_VALUE)
    if changed is None:
        return 1
    changed_shot, _ = changed

    failures = []

    # --- DEFAULT-NEUTRAL: byte-identical, no tolerance -------------------
    # the demo's water leg is frame-locked (a fixed 0.05s tick, a fixed frame
    # count, a fixed capture frame), so two runs of the SAME look produce the
    # same file byte for byte - which is what makes an exact comparison the
    # honest test here rather than a corridor
    identical = _read_bytes(baseline_shot) == _read_bytes(default_shot)
    if not identical:
        failures.append(
            f"seeding {SEED_VARIABLE}={DEFAULT_VALUE} (the shipped default) did "
            f"NOT reproduce the untouched frame byte for byte - the tier's "
            f"default has drifted from the constant it replaced")

    # --- LIVE: the knob reaches the rendered mirror ----------------------
    base_band = _band(baseline_shot)
    changed_band = _band(changed_shot)
    base_magenta = _magentaness(base_band)
    changed_magenta = _magentaness(changed_band)
    lum_diff = _mean_abs_lum_diff(base_band, changed_band)

    # thresholds: switching the mirror weight to 0 removes the marker's mirror
    # from the band, so its magenta collapses toward the no-mirror water (the
    # reflection probe measures ~0.019 classic / ~0.032 next with the mirror on
    # and ~0.00x with it off). The floor keeps headroom over the lower of the
    # two measured mirrors; a knob that never reached the surface leaves the
    # two frames identical, which reads 0.000 on both metrics.
    MIN_MAGENTA_DROP = 0.008
    # the band visibly moves as well (the mirror carried real luminance) - a
    # residual guard well under the measured drop on either flavor
    MIN_LUM_DIFF = 1.5

    print(f"water cvar probe: default_identical={identical}, "
          f"base_magenta={base_magenta:.3f}, "
          f"mirror_off_magenta={changed_magenta:.3f} "
          f"(drop>{MIN_MAGENTA_DROP}), lum_diff={lum_diff:.1f} "
          f"(>{MIN_LUM_DIFF})")

    if base_magenta - changed_magenta <= MIN_MAGENTA_DROP:
        failures.append(
            f"seeding {SEED_VARIABLE}={CHANGED_VALUE} barely changed the "
            f"marker's mirror in the water band (magenta {base_magenta:.3f} -> "
            f"{changed_magenta:.3f}) - the knob is not reaching the surface")
    if lum_diff <= MIN_LUM_DIFF:
        failures.append(
            f"the water band barely differs with the mirror weight at "
            f"{CHANGED_VALUE} (lum diff {lum_diff:.1f}) - the knob is not live")

    if failures:
        for line in failures:
            print(f"FAIL: {line}")
        return 1
    print("water_cvar_tier_is_live: the water.* tier reaches the rendered "
          "water, and its defaults render the untouched frame byte for byte")
    return 0


if __name__ == "__main__":
    sys.exit(main())
