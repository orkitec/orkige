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
  * vistashadow - the vista vignette rendered TWICE (r.shadowQuality off vs
    medium over the ORKIGE_CVARS seed) and compared pixel-for-pixel: with
    shadows on, a real fraction of the terrain band must read measurably
    DARKER (the prop/terrain shadows), while the frame stays daylit. A
    differential probe, so it needs no fixed shadow positions and reads both
    flavors (PBS and the RTSS integrated PSSM) with one criterion.

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
LUMENS_LAMP_MAX_CHANNEL = 85
LUMENS_LAMP_SPREAD = 25
LUMENS_LAMP_MIN_FRACTION = 0.002    # of the terrain band

# field corridor: the median of the cube-dominated grid-centre band (a mix of
# sun-facing faces, away faces and sky gaps; the black-cube regression drags
# the median to near-black on either flavor)
FIELD_CUBE_LIT_MIN = 55.0

# hud2x: bright glyph pixels of the two HUD rows
HUD_BRIGHT = 220
HUD_ROW_MIN = 8               # a text row carries at least this many glyph px

# vistashadow corridors: a shadowed pixel darkens by at least this much
# (0..255) between the off and on runs, and at least this fraction of the
# terrain band must darken (5 props + terrain self-shadowing); the daylit
# floor guards against a flavor that darkens by rendering BROKEN (black)
SHADOW_DARKEN_MIN = 12.0
SHADOW_FRACTION_MIN = 0.004
SHADOW_DAYLIT_FLOOR = 35.0


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


# flatland: the flat-colour 2D vector showcase. Its shapes (a red soft-body
# blob, an orange morph blob, a blue vector-anim walker, a yellow sun sprite)
# sit OVER a full-screen gradient backdrop sprite. If the 2D painter order
# regresses - the backdrop paints on TOP of the shapes, washing them to the
# backdrop's blue-grey (the classic transparent-sort-vs-render-queue fault) -
# the saturated shape colours vanish. This corridor bounds BOTH flavors from
# the same side: the shapes MUST contribute enough clearly-warm (red/orange)
# and clearly-yellow (sun) pixels, which a washed frame cannot.
FLATLAND_WARM_MIN = 400     # red blob + orange blob pixels (of the ~102k grid)
FLATLAND_SUN_MIN = 8        # the small bright-yellow sun sprite


def probe_flatland(img):
    width, height, channels, pixels = img
    warm = 0        # strongly red/orange - the blob + morph shapes
    sun = 0         # bright warm-yellow - the sun sprite
    for y in range(0, height, 2):
        for x in range(0, width, 2):
            r, g, b = pixel(pixels, channels, width, x, y)
            # the blue-grey backdrop (e.g. 76,114,178) has b dominant; a shape
            # pixel is clearly red-dominant (blob 230,77,52 / orange 242,153,102)
            if r >= 180 and r >= g + 70 and r >= b + 90:
                warm += 1
            # the sun is bright yellow (255,229,102): high r+g, low-ish b
            if r >= 210 and g >= 180 and b <= 170:
                sun += 1
    log("flatland: warm(red/orange) px %d (want >= %d), sun px %d (want >= %d)"
        % (warm, FLATLAND_WARM_MIN, sun, FLATLAND_SUN_MIN))
    if warm < FLATLAND_WARM_MIN:
        fail("the flat-colour shapes are washed out - the 2D backdrop paints "
             "over them (the render-queue/painter-order regression)")
    if sun < FLATLAND_SUN_MIN:
        fail("the sun sprite is washed out - a high-zOrder sprite renders "
             "behind the backdrop")


def probe_vistashadow(img_off, img_on):
    width, height, channels, pixels_on = img_on
    off_w, off_h, off_c, pixels_off = img_off
    if (off_w, off_h) != (width, height):
        fail("the off/on captures disagree on size (%dx%d vs %dx%d)"
             % (off_w, off_h, width, height))
    darkened = 0
    samples = 0
    total_on = 0.0
    for y in range(int(height * 0.55), int(height * 0.95), 2):
        for x in range(int(width * 0.05), int(width * 0.95), 2):
            r0, g0, b0 = pixel(pixels_off, off_c, width, x, y)
            r1, g1, b1 = pixel(pixels_on, channels, width, x, y)
            lum_on = luminance(r1, g1, b1)
            total_on += lum_on
            samples += 1
            if luminance(r0, g0, b0) - lum_on >= SHADOW_DARKEN_MIN:
                darkened += 1
    fraction = darkened / max(samples, 1)
    mean_on = total_on / max(samples, 1)
    log("vistashadow: darkened fraction %.5f (want >= %.5f), terrain mean "
        "with shadows %.1f (want >= %.1f)"
        % (fraction, SHADOW_FRACTION_MIN, mean_on, SHADOW_DAYLIT_FLOOR))
    if mean_on < SHADOW_DAYLIT_FLOOR:
        fail("the shadowed vista is not daylit - the shadow pass darkened "
             "the whole frame instead of casting shadows")
    if fraction < SHADOW_FRACTION_MIN:
        fail("no shadow-darkened band on the vista terrain - the casting sun "
             "renders no shadows on this flavor")


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


def run_player_capture(args, scene, shot, extra_cvars="", fake_scale=None):
    """boot the player on one scene, capture the frame-60 screenshot, decode"""
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
        "ORKIGE_CVARS": "benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0"
                        + extra_cvars,
    })
    if fake_scale is not None:
        env["ORKIGE_FAKE_CONTENT_SCALE"] = fake_scale

    cmd = [args.player, scene, "--project",
           os.path.join(args.repo, "projects/benchmark")]
    log("running: " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=args.repo, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=180)
    output = result.stdout.decode("utf-8", "replace")
    if result.returncode != 0:
        log(output[-1500:])
        fail("player exited %d" % result.returncode)
    if not os.path.exists(shot):
        fail("no screenshot written to " + shot)
    return decode_png(shot), output


def probe_lumens_ramp(output):
    """The many-lights MECHANISM: the director must query the ACTIVE flavor's
    light budget (engine:getLightBudget) and cap the point-light ramp at it.
    Asserts the mechanism, NOT an absolute count (a CI software rasterizer
    legitimately ramps to a low frame-budget cap; here the self-limit is
    disabled via rampBudgetMs, so the ramp reaches its HARD ceiling): the ramp
    functioned (>= 1 lamp lit) and its ceiling never exceeded the queried
    budget. Parses the director's own log lines."""
    import re
    budget_match = re.search(
        r"director: light budget (\d+) \(ramp ceiling (\d+)\)", output)
    if budget_match is None:
        fail("the director never reported its queried light budget - "
             "engine:getLightBudget is not driving the ramp")
    queried = int(budget_match.group(1))
    ceiling = int(budget_match.group(2))
    log("lumens ramp: queried budget %d, effective ceiling %d" %
        (queried, ceiling))
    if queried < 1:
        fail("the queried light budget is 0 - the render backend reported no "
             "dynamic-light ceiling")
    if ceiling > queried:
        fail("the ramp ceiling %d exceeds the queried light budget %d - the "
             "budget is not the hard cap" % (ceiling, queried))
    # the ramp reached its ceiling OR stalled on the frame budget; either way a
    # count line must appear and its count must honor the ceiling and be >= 1
    reached = re.findall(r"director\[lumens\]: ramp (?:reached ceiling|capped "
                         r"at) (\d+)", output)
    if not reached:
        fail("the lumens ramp never activated a lamp - the point-light ramp "
             "did not function")
    active = max(int(n) for n in reached)
    log("lumens ramp: activated %d lamp(s) (want 1..%d)" % (active, ceiling))
    if active < 1:
        fail("the lumens ramp activated no lamps")
    if active > ceiling:
        fail("the lumens ramp activated %d lamps, above its ceiling %d" %
             (active, ceiling))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--probe", required=True,
                        choices=("lumens", "field", "hud2x", "flatland",
                                 "vistashadow"))
    parser.add_argument("--frames", type=int, default=240,
                        help="capture is at frame 60; later frames only pad "
                             "the clean-exit check")
    args = parser.parse_args()

    os.makedirs(args.dir, exist_ok=True)

    if args.probe == "vistashadow":
        # the differential probe: the SAME deterministic vista frame with the
        # shadow knob off, then on - only the shadows may differ
        img_off, _ = run_player_capture(
            args, "scenes/vista.oscene",
            os.path.join(args.dir, "vistashadow_off.png"),
            extra_cvars=",r.shadowQuality=off")
        img_on, _ = run_player_capture(
            args, "scenes/vista.oscene",
            os.path.join(args.dir, "vistashadow_on.png"),
            extra_cvars=",r.shadowQuality=medium")
        probe_vistashadow(img_off, img_on)
        log("OK")
        return

    scene = {"lumens": "scenes/lumens.oscene", "field": "scenes/field.oscene",
             "hud2x": "scenes/lumens.oscene",
             "flatland": "scenes/flatland.oscene"}[args.probe]
    shot = os.path.join(args.dir, args.probe + "_frame.png")
    img, output = run_player_capture(
        args, scene, shot,
        fake_scale="2" if args.probe == "hud2x" else None)
    { "lumens": probe_lumens, "field": probe_field,
      "hud2x": probe_hud2x, "flatland": probe_flatland }[args.probe](img)
    # the lumens scene also carries the many-lights ramp MECHANISM assertion
    if args.probe == "lumens":
        probe_lumens_ramp(output)
    log("OK")


if __name__ == "__main__":
    main()
