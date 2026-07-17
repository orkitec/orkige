#!/usr/bin/env python3
"""Per-scene pixel probes for the benchmark showcase - the vignette must LOOK
right on the running flavor, not just traverse.

Three probe modes, all direct scene boots of projects/benchmark (deterministic,
no editor session). The band thresholds are deliberately generous: they bound
BOTH flavors from the same side (a reading either flavor produces must land in
the same corridor), so a flavor that regresses out of the corridor fails while
legitimate tolerance-parity differences (PBS vs Blinn-Phong shading) pass.

  * lumens - the night vignette mid-ramp: the terrain band must be readable
    (NOT black - the moonlit fill), the upper sky band must stay night-dark,
    and lamp glow must be visible (bright pixels over the terrain band; the
    ramp activates lamps from the first frames). Guards the sun-exposure
    linkage + the night atmosphere look on both flavors.
  * field - the instance-field vignette mid-ramp: the ramped cubes must be
    VISIBLE against the sky backdrop and LIT (mean brightness of cube pixels
    above the lit floor - the black-cube regression class).
  * hud2x - the HUD layout at a simulated dense display (content scale 2):
    the scene title and the stats line must render as two VERTICALLY DISJOINT
    text bands (the overlap regression class - authored positions are
    physical pixels while glyphs scale with density).

Pure stdlib (the sibling pixel test's PNG decoder). Runs per flavor.
Exit codes: 0 pass, 1 fail.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_benchmark_pixel_test import decode_png, pixel  # noqa: E402

# lumens corridors (0..255 means over the band)
LUMENS_TERRAIN_MIN = 8.0      # moonlit terrain floor (black-night regression)
LUMENS_TERRAIN_MAX = 110.0    # still a NIGHT - a daylit terrain is a bug
LUMENS_SKY_MAX = 90.0         # the upper sky band stays night-dark
# a lamp-glow pixel: clearly brighter than the moonlit base AND colour-
# saturated (the pools are tinted; the moonlit terrain is a desaturated grey-
# olive) - the same criterion reads both flavors' pool renderings
LUMENS_LAMP_MAX_CHANNEL = 95
LUMENS_LAMP_SPREAD = 25
LUMENS_LAMP_MIN_FRACTION = 0.002    # of the terrain band

# field corridor: the median of the cube-dominated grid-centre band (a mix of
# sun-facing faces, away faces and sky gaps; the black-cube regression drags
# the median to near-black on either flavor)
FIELD_CUBE_LIT_MIN = 55.0

# hud2x: bright glyph pixels of the two HUD rows
HUD_BRIGHT = 220
HUD_ROW_MIN = 8               # a text row carries at least this many glyph px


def log(msg):
    print("run_benchmark_scene_probe: " + msg, flush=True)


def fail(msg):
    print("run_benchmark_scene_probe: FAILED - " + msg, flush=True)
    sys.exit(1)


def luminance(r, g, b):
    return (r + g + b) / 3.0


def band_stats(img, x0, y0, x1, y1):
    width, height, channels, pixels = img
    total = 0.0
    count = 0
    glow = 0
    for y in range(int(y0 * height), int(y1 * height), 2):
        for x in range(int(x0 * width), int(x1 * width), 2):
            r, g, b = pixel(pixels, channels, width, x, y)
            total += luminance(r, g, b)
            count += 1
            if max(r, g, b) >= LUMENS_LAMP_MAX_CHANNEL and \
                    max(r, g, b) - min(r, g, b) >= LUMENS_LAMP_SPREAD:
                glow += 1
    return total / max(count, 1), glow, count


def probe_lumens(img):
    terrain_mean, lamp_pixels, samples = band_stats(img, 0.05, 0.62, 0.95, 0.95)
    sky_mean, _, _ = band_stats(img, 0.05, 0.06, 0.95, 0.28)
    lamp_fraction = lamp_pixels / max(samples, 1)
    log("lumens: terrain mean %.1f (want %.1f..%.1f), sky mean %.1f "
        "(want <= %.1f), lamp-glow fraction %.5f (want >= %.5f)"
        % (terrain_mean, LUMENS_TERRAIN_MIN, LUMENS_TERRAIN_MAX, sky_mean,
           LUMENS_SKY_MAX, lamp_fraction, LUMENS_LAMP_MIN_FRACTION))
    if terrain_mean < LUMENS_TERRAIN_MIN:
        fail("the night terrain is black - the moonlit fill is gone")
    if terrain_mean > LUMENS_TERRAIN_MAX:
        fail("the night terrain reads daylit - the night exposure is gone")
    if sky_mean > LUMENS_SKY_MAX:
        fail("the night sky band is bright - the night dome look is gone")
    if lamp_fraction < LUMENS_LAMP_MIN_FRACTION:
        fail("no lamp glow over the terrain - the point-light ramp renders "
             "nothing on this flavor")


def probe_field(img):
    width, height, channels, pixels = img
    # the camera is FROZEN at the init framing (benchmark.cameraOrbit=0), so
    # the grid-centre band is a deterministic cube-dominated region on both
    # flavors (they share the frustum). Its MEDIAN mixes lit faces, away faces
    # and sky gaps - the black-cube regression drags it to near-black.
    cube_lums = []
    for y in range(int(height * 0.44), int(height * 0.54), 2):
        for x in range(int(width * 0.35), int(width * 0.65), 2):
            r, g, b = pixel(pixels, channels, width, x, y)
            cube_lums.append(luminance(r, g, b))
    cube_lums.sort()
    median = cube_lums[len(cube_lums) // 2] if cube_lums else 0.0
    log("field: grid-centre median %.1f (want >= %.1f)"
        % (median, FIELD_CUBE_LIT_MIN))
    if median < FIELD_CUBE_LIT_MIN:
        fail("the instance-field cubes render black (median %.1f)" % median)


def probe_hud2x(img):
    width, height, channels, pixels = img
    # bright-glyph histogram over the HUD corner (rows are DEVICE pixels; the
    # title is a 24px font at 2x -> a tall band, the stats line a short one)
    rows = []
    for y in range(0, min(int(height * 0.25), 320)):
        count = 0
        for x in range(0, int(width * 0.45), 2):
            r, g, b = pixel(pixels, channels, width, x, y)
            if r >= HUD_BRIGHT and g >= HUD_BRIGHT and b >= HUD_BRIGHT:
                count += 1
        rows.append(count)
    # contiguous text bands (rows with enough glyph pixels)
    bands = []
    start = None
    for y, count in enumerate(rows):
        if count >= HUD_ROW_MIN and start is None:
            start = y
        elif count < HUD_ROW_MIN and start is not None:
            bands.append((start, y - 1))
            start = None
    if start is not None:
        bands.append((start, len(rows) - 1))
    log("hud2x: text bands %s" % bands)
    if len(bands) < 2:
        fail("the HUD title and stats line do not form two disjoint text "
             "bands - the rows overlap at this content scale")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--probe", required=True,
                        choices=("lumens", "field", "hud2x"))
    parser.add_argument("--frames", type=int, default=240,
                        help="capture is at frame 60; later frames only pad "
                             "the clean-exit check")
    args = parser.parse_args()

    scene = {"lumens": "scenes/lumens.oscene", "field": "scenes/field.oscene",
             "hud2x": "scenes/lumens.oscene"}[args.probe]

    os.makedirs(args.dir, exist_ok=True)
    shot = os.path.join(args.dir, args.probe + "_frame.png")
    if os.path.exists(shot):
        os.unlink(shot)

    env = dict(os.environ)
    env.update({
        "ORKIGE_DEMO_FRAMES": str(args.frames),
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": args.dir,
        # force the stress ramps to their ceiling (a debug build's frame cost
        # would trip the self-limit after one pooled object) and freeze the
        # wall-time camera orbit at the init framing - both make the captured
        # frame machine-independent (the cvars are the director's automation
        # seams)
        "ORKIGE_CVARS": "benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0",
    })
    if args.probe == "hud2x":
        env["ORKIGE_FAKE_CONTENT_SCALE"] = "2"

    cmd = [args.player, scene, "--project",
           os.path.join(args.repo, "projects/benchmark")]
    log("running: " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=args.repo, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=180)
    if result.returncode != 0:
        log(result.stdout.decode("utf-8", "replace")[-1500:])
        fail("player exited %d" % result.returncode)
    if not os.path.exists(shot):
        fail("no screenshot written to " + shot)

    img = decode_png(shot)
    { "lumens": probe_lumens, "field": probe_field,
      "hud2x": probe_hud2x }[args.probe](img)
    log("OK")


if __name__ == "__main__":
    main()
