#!/usr/bin/env python3
"""Assert the GEOMETRIC water swell moves the surface over time on both flavors.

The water swell (RenderWaterDesc::waveHeight, reflected on WaterComponent as the
`waveHeight` property; 0.0 = byte-stable OFF by default) is a vertex-stage
displacement that lifts and drops the water surface as a travelling two-sine
wave: next = a custom Hlms vertex piece + a pass-buffer swell clock, classic =
the same formula in its refraction/reflection water vertex program. This drives
the ORKIGE_DEMO_WATER swell leg, which stands a high-contrast EMISSIVE backdrop
wall behind the far water edge (so the surface's silhouette reads as a clear
boundary band) and FREEZES the rest of the scene - the scene orbit is held still
and the ripple SCROLL is off - so the ONLY thing that can move the water is the
swell itself (the scrolling normal shimmer animates the water's SHADING, not its
geometry, and would otherwise mask the silhouette). It runs the leg two ways:

  * SWELL ON  (ORKIGE_DEMO_WATER_SWELL)     - waveHeight > 0, the surface swells
  * SWELL OFF (ORKIGE_DEMO_WATER_SWELL_OFF) - waveHeight 0, the flat baseline

Two frames apart in time are captured per run, and in the SILHOUETTE band (the
rows the far water edge sweeps as it rises, left and right of the centre scene
objects) it asserts:

  * ANIMATES - the swelling silhouette MOVES between the two frames (the swell
    clock advances and drives the displacement); with the scene and scroll
    frozen, this motion can only be the geometric swell, so it catches a missing
    vertex piece, an unregistered clock listener OR a frozen phase (on classic,
    the drawn pass not being the one the per-frame push updates);
  * OFF STAYS OFF - the waveHeight-0 baseline is byte-stable between the two
    frames (the frozen scene's flat plane does not move - the shipped
    waveHeight-0 compat guarantee, and a guard against the swell leaking into
    the flat path).

The static swell displacement vs the flat baseline is printed for context
(diagnostic only) - the temporal ANIMATES check is the discriminator, since a
silhouette cannot move between frames without the swell displacing it.

Pure stdlib; reuses the water-probe PNG decoder in this directory.
"""

import argparse
import os
import subprocess
import sys

# the sibling water probe's zlib PNG decoder + luminance helper (this file's
# directory is on sys.path[0] when run as a script)
from run_water_probe_test import decode_png, _lum


def _band(path):
    """luminance samples over the SILHOUETTE band - the rows the far water edge
    sweeps as the swell lifts it (fy 0.26..0.44, above the flat waterline), at
    the LEFT/RIGHT columns that flank the centre scene objects (the centre
    columns are occluded by the demo cubes)."""
    width, height, channels, pixels = decode_png(path)
    rows = []
    for step in range(22):
        fy = 0.26 + (0.44 - 0.26) * step / 21.0
        row = []
        for lo, hi in ((0.06, 0.34), (0.66, 0.94)):
            for i in range(26):
                fx = lo + (hi - lo) * i / 25.0
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


def _run(binary, out, swell_on, tag):
    late = os.path.join(out, f"swell_{tag}_60.png")
    early = os.path.join(out, f"swell_{tag}_40.png")
    env = dict(os.environ, ORKIGE_DEMO_WATER="1", ORKIGE_DEMO_FRAMES="70",
               ORKIGE_AUTOMATED_RUN="1",
               ORKIGE_DEMO_SCREENSHOT=late, ORKIGE_DEMO_SCREENSHOT2=early)
    env["ORKIGE_DEMO_WATER_SWELL" if swell_on
        else "ORKIGE_DEMO_WATER_SWELL_OFF"] = "1"
    try:
        result = subprocess.run([binary], env=env, timeout=120,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        print(f"FAIL: demo_water swell leg ({tag}) hung")
        return None
    if result.returncode != 0:
        print(f"FAIL: demo_water swell leg ({tag}) exited {result.returncode}")
        return None
    for path in (early, late):
        if not os.path.exists(path):
            print(f"FAIL: no swell frame captured at {path}")
            return None
    return early, late


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
    on_early, on_late = on
    off = _run(args.binary, args.out, False, "off")
    if off is None:
        return 1
    off_early, off_late = off

    on_e, on_l = _band(on_early), _band(on_late)
    off_e, off_l = _band(off_early), _band(off_late)

    on_motion = _mean_abs_diff(on_e, on_l)      # the swell moves between frames
    off_motion = _mean_abs_diff(off_e, off_l)   # the flat baseline stays put
    displaces = _mean_abs_diff(on_l, off_l)     # swelled vs flat (diagnostic)

    # thresholds (measured on this machine, frame-locked/deterministic): with the
    # scene and the ripple scroll frozen, the swelling silhouette moves between
    # the two frames by ~15 luminance on next and ~32 on classic (classic's swell
    # is a broad near-uniform bob that sweeps the whole waterline; next's
    # travelling hump moves a narrower band), while the OFF baseline is byte-stable
    # (~0.0 - a fully static frozen scene). Software rasterizers (lavapipe/
    # llvmpipe on CI) land at roughly half the macOS signal on the moving leg (the
    # refraction probe's history), so the ANIMATES floor sits near a third of the
    # SMALLER (next) value; the OFF byte-stability stays ~0 (a static scene renders
    # deterministically), so its ceiling sits well under the animating floor.
    MIN_ON_MOTION = 5.0      # the swell moves (macOS next ~15, classic ~32)
    MAX_OFF_MOTION = 2.0     # waveHeight 0 is byte-stable over time (macOS ~0.0)

    print(f"swell probe: on_motion={on_motion:.2f} (>{MIN_ON_MOTION}), "
          f"off_motion={off_motion:.3f} (<{MAX_OFF_MOTION}), "
          f"displaces={displaces:.2f} (diagnostic)")

    failures = []
    if on_motion <= MIN_ON_MOTION:
        failures.append(f"the swell does not move between frames "
                        f"(motion {on_motion:.2f}) - the swell is not displacing "
                        f"the surface or its clock is frozen (piece/listener "
                        f"missing, phase not advancing, or the drawn pass is not "
                        f"the one the per-frame push updates)")
    if off_motion >= MAX_OFF_MOTION:
        failures.append(f"the waveHeight-0 baseline is not byte-stable "
                        f"(off_motion {off_motion:.3f}) - the swell leaked into "
                        f"the flat path")
    if failures:
        for line in failures:
            print(f"FAIL: {line}")
        return 1
    print("water_swell_looks_right: the surface swells and its silhouette moves "
          "as the swell clock advances, and waveHeight 0 stays byte-stable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
