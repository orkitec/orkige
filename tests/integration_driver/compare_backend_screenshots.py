#!/usr/bin/env python3
"""Cross-backend pixel comparison of the render_facade_selfcheck output.

The WYSIWYG backend-parity gate: the facade selfcheck renders the SAME scene
on the classic-OGRE and Ogre-Next flavors; this driver compares the named
screenshot pairs within tolerance. It carries TWO comparisons over the same
pair of capture directories:

  * THE PIXEL GATE (COMPARED_SHOTS, the default mode) - the four frames whose
    per-pixel agreement has always been required.
  * THE FEATURE SWEEP (`--feature-sweep`, FEATURE_SHOTS) - every OTHER frame
    the selfcheck writes on both flavors, scored per shot against its own
    measured corridor. Shadows, materials, IBL, sky, fog, decals, bloom, the
    output grade, the 2D layers and the offscreen path each leave a frame
    behind; without the sweep a drift in any of them is invisible to every
    gate. Most of the sweep is REPORT-ONLY by construction (see FEATURE_SHOTS)
    - a shot gates only once its corridor has been measured tight.

Two roads reach the same comparison:

  * RUN BOTH BINARIES (--next-binary/--classic-binary): the developer road,
    on a machine carrying both build trees. Registered as ctest
    `render_backend_parity` on the NEXT preset. The classic binary lives in
    another build tree - when it is absent the test SKIPs honestly (exit 77,
    ctest SKIP_RETURN_CODE) instead of failing or silently passing.
  * COMPARE CAPTURED DIRECTORIES (--classic-shots/--next-shots): each flavor
    ran its own selfcheck elsewhere and its output directory was carried
    here. One flavor per machine is the shape a per-flavor build matrix has,
    so this is how the gate runs where no single machine holds both trees.
    A directory that is missing, empty or short of the compared shots is a
    FAILURE, never a skip - a parity gate that compared nothing must not
    report parity.

Comparison model (per pair): images must have identical dimensions; the mean
absolute per-channel error must stay below MEAN_TOLERANCE and at most
OUTLIER_FRACTION of the pixels may differ by more than OUTLIER_TOLERANCE per
channel. The window shot contains PBS-vs-RTSS-Phong LIT content (the textured
platform), which legitimately shades a little differently - the thresholds
leave room for shading models while catching the actual parity failure
classes (sRGB/gamma mismatches shift EVERYTHING by dozens of levels, missing
content flips whole regions).

Two numbers cannot say WHERE a pair disagrees, so every pair also reports the
largest 8-connected region of differing pixels (parity_diff), and any pair
with at least one pixel over the outlier threshold leaves a DIFF IMAGE beside
the compared shot - `<shot>.diff.png`, the delta painted over the frame it
belongs to - ON GREEN AS WELL AS RED: a verdict inside its corridor can sit
over a real region of strongly differing pixels, and the picture is how such
a region gets looked at instead of discovered later. The region size is
reported, not gated: see parity_diff for why.

Pure stdlib (zlib PNG decode - the screenshots are 8-bit RGB/RGBA PNGs).
"""

import argparse
import contextlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parity_diff  # noqa: E402

SKIP_EXIT_CODE = 77

#: screenshots compared (written by tests/render_facade/selfcheck_main.cpp)
COMPARED_SHOTS = [
    # 3D scene over the window: vertex-coloured mesh (unlit), textured
    # platform (lit - the tolerance headroom is for this one), background
    "selfcheck_window.png",
    # the DrawLayer2D conformance pattern over the same scene
    "selfcheck_drawlayer2d.png",
    # the offscreen path: sprite through an ortho camera into an RTT
    "selfcheck_rtt.png",
    # the PARAMETRIC mesh tier: a `.omesh` text asset parsed, built through
    # RenderWorld::createMeshFromData and instantiated the ordinary way. Its
    # surface is emissive-only with every light suppressed, so this capture is a
    # silhouette/coverage image of the GENERATED GEOMETRY - the two flavors'
    # shading models cannot contribute a delta, which makes it the strictest
    # geometry comparison in the set.
    "selfcheck_omesh.png",
]

# The corridors were measured on the developer pair (GL3Plus against Metal).
# What the CI pair (a software GL rasterizer against a software Vulkan one)
# actually measures, from a green run of the parity job:
#
#   selfcheck_window.png       mean 0.17   outliers 0.30%
#   selfcheck_drawlayer2d.png  mean 0.17   outliers 0.30%
#   selfcheck_rtt.png          mean 0.00   outliers 0.00%
#   selfcheck_omesh.png        mean 2.08   outliers 0.00%
#
# Two facts to keep: the means sit far inside a 6.0 corridor, and the two
# window shots carry ~0.3% of pixels differing by MORE than 48 while their
# mean is 0.17 - a small, strongly-differing set that the mean alone cannot
# see. That is the shape the region report exists to name. The corridors stay
# where they are until a tightening is measured rather than guessed.
MEAN_TOLERANCE = 6.0          # mean abs diff per channel, 0..255
OUTLIER_TOLERANCE = 48        # a pixel "differs" above this per-channel delta
OUTLIER_FRACTION = 0.02       # fraction of differing pixels allowed


# --- the feature sweep ------------------------------------------------------

#: one shot's corridor in the sweep. `mean` and `outliers` score the WHOLE
#: frame the way the pixel gate does; `region` bounds the per-region mean
#: colour delta (the vignette gate's instrument, which sees a whole area
#: shifting that a frame mean averages away). `gated` says whether a breach
#: refuses or is merely reported - see FEATURE_SHOTS.
ShotCorridor = namedtuple("ShotCorridor", "mean outliers region gated note")

#: the sweep's default region layout. No frame here has hand-drawn regions the
#: way a benchmark vignette does, so the layout is derived from the frame: the
#: four quadrants plus a centre box. A quadrant catches "one corner of the
#: image is wrong" (a shadow that lands elsewhere, a sky band that stops at a
#: different height); the centre box catches the subject, which on every one of
#: these frames sits in the middle. Region MEANS are what is compared, so the
#: known-benign isolated outlier pixels - a texture-sampling phase checkerboard
#: adjudicated harmless - cannot move them, and no single-pixel maximum is
#: gated anywhere in this file for the same reason.
CENTRE_INSET = 0.3

#: the sweep's own outlier threshold, kept equal to the pixel gate's so every
#: number this repository prints about a frame pair is in one unit.
FEATURE_OUTLIER_TOLERANCE = OUTLIER_TOLERANCE

#: how far a region mean may drift before the pair is worth a person's time.
#: Used by --adjudication-dir, not by any verdict.
ADJUDICATION_REGION_DELTA = 8.0

#: sampled luma spread at or below which a frame carries no picture - one flat
#: colour edge to edge. TWO FLAT FRAMES AGREE PERFECTLY AND PROVE NOTHING, so
#: a shot the table GATES must carry content on both sides; a flat pair under
#: a gated entry is a failure, not a pass. It is the same rule as the empty
#: capture directory one level down: silence is not evidence.
FLAT_FRAME_SPREAD = 1

#: Every screenshot BOTH flavors write, with the corridor measured for it.
#:
#: WHAT IS NOT HERE, and why: `selfcheck_ui_surface.png` and
#: `selfcheck_ui_window.png` are written only where
#: `RenderCaps::OffscreenOwnedLayers` is supported (the offscreen 2D
#: composition case, next only - classic reports no offscreen owned layers),
#: so there is no classic counterpart to compare and they are not a parity
#: subject. `selfcheck_sky_skybox_cooked.png` needs
#: ORKIGE_SELFCHECK_COOKED_CUBE_DIR and is written by neither flavor in a
#: plain run. `light_probe.png` is a probe image, not a look frame. The four
#: COMPARED_SHOTS stay in the pixel gate above and are not repeated here.
#:
#: MEASUREMENT: every `note` carries what the developer pair measures - mean /
#: outlier fraction / worst region-mean delta, from the classic GL3Plus
#: against the next Metal flavor at 1920x1080. A corridor is a measured value
#: with headroom, never a guess.
#:
#: GATING: `gated=True` only where the pair measures CLEAN and both frames
#: carry a picture - the corridor then locks a shot that agrees today into
#: agreeing tomorrow, and the clean set all sits inside the pixel gate's own
#: long-standing 6.0 / 2% numbers, so the whole gated set is ONE corridor to
#: reason about rather than one per shot.
#:
#: Everywhere the pair measures a REAL DIVERGENCE the entry is `gated=False`,
#: and its corridor is the measured state with headroom: the flavors do not
#: agree there, the difference has not been adjudicated, and a corridor wide
#: enough to pass a known divergence gates nothing. What such an entry IS good
#: for is a ratchet - it names today's divergence in a number, so a shot
#: getting WORSE is visible in the report. Closing one is a change that
#: tightens its corridor and flips its flag in the same commit.
#:
#: Two entries are report-only for a different reason: both flavors render a
#: FLAT frame there (an unlit black scene, a bare clear colour), so the pair
#: agrees perfectly while proving nothing. The sweep says so by name rather
#: than banking the agreement.
FEATURE_SHOTS = {
    # -- 2D layers: the DrawLayer2D conformance pattern's siblings, through
    # the same 2D path the gated selfcheck_drawlayer2d.png takes.
    "selfcheck_drawlayer2d_dynamic.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "a rebuilt batch: mean 0.67, outliers 0.97%, region br 1.6 - the outlier set is the imported platform (the ambient seam), inside the gate"),
    "selfcheck_drawlayer2d_rebake.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "the same batch re-baked: mean 0.67, outliers 0.97%, region br 1.6 - the outlier set is the imported platform (the ambient seam), inside the gate"),
    "selfcheck_drawlayer2d_shown.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "a hidden layer shown again: mean 0.67, outliers 0.97%, region br "
        "1.6"),
    "selfcheck_drawlayer2d_removed.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "the layer removed, the scene alone: mean 0.67, outliers 0.97%, "
        "region br 1.6"),
    "selfcheck_rtt_2d.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "the offscreen target with no window 2D leaked into it: mean 0.00, "
        "outliers 0.00%, region 0.0"),

    # -- lighting: the rig over the platform, on and off.
    "selfcheck_light_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "the lit sphere, converged by the metallic-workflow import: mean "
        "0.00, outliers 0.00%, region 0.0"),
    "selfcheck_light_off.png": ShotCorridor(
        6.0, 0.02, 6.0, False,
        "FLAT PAIR: both flavors render solid black with every light off, so "
        "the perfect agreement is not evidence"),

    # -- shadows: the PSSM pass and each knob that suppresses part of it.
    # CONVERGED: the flat-normal import stopped the slab pooling, and the two
    # imported-material fixes (classic onto the engine's surface stages, next
    # onto the metallic workflow) met on the sunlit floor - the family fell
    # from mean ~22 / region ~42-49 to mean ~2 / region <5, with the outlier
    # fraction at effectively zero. The direct (sun) response of an imported
    # material now matches between the flavors; what residue this scene has
    # left is a handful of sub-100px clusters on shadow edges.
    "selfcheck_shadow_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 1.94, outliers 0.03%, region centre 3.7"),
    "selfcheck_shadow_receive_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 1.95, outliers 0.04%, region centre 3.6"),
    "selfcheck_shadow_low.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 1.93, outliers 0.03%, region centre 3.7"),
    "selfcheck_shadow_off.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 2.00, outliers 0.00%, region centre 4.4"),
    "selfcheck_shadow_caster_off.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 2.00, outliers 0.00%, region centre 4.4"),
    "selfcheck_shadow_mesh_caster_off.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 2.00, outliers 0.00%, region centre 4.4"),
    "selfcheck_shadow_receive_off.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 2.00, outliers 0.00%, region centre 4.4"),

    # -- atmosphere: sky dome, exposure, the driven and restored states.
    "selfcheck_atmosphere_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.01, outliers 0.00%, region 0.0 - the sky-band gradient seam "
        "closed with the grade colour-space fix"),
    # driven/exposure held their converged figure on a second pair (mean
    # 1.93, region bl 5.4, zero outlier pixels both times), so they gate.
    "selfcheck_atmosphere_exposure.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 1.93, outliers 0.00%, region bl 5.4 - confirmed on a second "
        "pair"),
    "selfcheck_atmosphere_driven.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 1.93, outliers 0.00%, region bl 5.4 - confirmed on a second "
        "pair"),
    "selfcheck_atmosphere_restored.png": ShotCorridor(
        6.0, 0.02, 12.0, False,
        "mean 4.54, outliers 0.00%, region centre 9.4 - no pixel over the "
        "outlier threshold; the restored terrain reads ~8 levels less "
        "saturated on next (green/blue up, red equal), a small tint residual "
        "of the flavors' surface models"),
    "selfcheck_atmosphere_off.png": ShotCorridor(
        6.0, 0.02, 6.0, False,
        "FLAT PAIR: both flavors render solid black with the atmosphere off"),
    "selfcheck_atmosphere_skyoff.png": ShotCorridor(
        6.0, 0.02, 6.0, False,
        "FLAT PAIR: both flavors render one flat clear colour with the sky "
        "suppressed"),

    # -- sky: the procedural dome, the skybox cube, the flat colour.
    "selfcheck_sky_procedural.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "the dome gradient, identical since the grade colour-space fix: "
        "mean 0.00, outliers 0.00%, region 0.0"),
    "selfcheck_sky_procedural_restored.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.00, outliers 0.00%, region 0.0"),
    "selfcheck_sky_skybox.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "the cube-map sky, content-bearing and identical: mean 0.00, "
        "outliers 0.00%, region 0.0"),
    "selfcheck_sky_colour.png": ShotCorridor(
        6.0, 0.02, 6.0, False,
        "FLAT PAIR: one bare clear colour edge to edge; the grade "
        "colour-space fix took it from the sweep's worst divergence (region "
        "147.0) to identical (mean 0.00) - flat by design, so the agreement "
        "is not banked"),

    # -- image-based lighting, from the debug cube and the procedural sky.
    "selfcheck_ibl_on.png": ShotCorridor(
        6.0, 0.02, 12.0, False,
        "mean 1.56, outliers 0.08%, region centre 9.4 - the IBL-lit body, one "
        "1.6k-pixel cluster left"),
    # with IBL off, classic keeps a dim sky-coloured hemisphere fill on the
    # body while next goes fully dark - the adjudicated flavor-model
    # difference this pair pictures (the numbers are byte-stable across the
    # imported-material fixes, which do not touch it)
    "selfcheck_ibl_off.png": ShotCorridor(
        8.0, 0.20, 48.0, False,
        "mean 6.59, outliers 16.00%, region centre 40.2 - classic's residual "
        "hemisphere fill against next's black"),
    "selfcheck_ibl_restored.png": ShotCorridor(
        8.0, 0.20, 48.0, False,
        "mean 6.59, outliers 16.00%, region centre 40.2 - same residual as "
        "ibl_off"),
    "selfcheck_ibl_proc_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "the procedurally-lit body, converged by the flat-normal import and "
        "now gated: mean 0.77, outliers 0.20%, region centre 2.8"),
    "selfcheck_ibl_proc_off.png": ShotCorridor(
        6.0, 0.02, 8.0, False,
        "mean 1.60, outliers 0.30%, region centre 6.2 - one 6.2k-pixel "
        "cluster on the body"),

    # -- fog.
    "selfcheck_fog_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.28, outliers 0.00%, region tr 1.5"),
    "selfcheck_fog_off.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.04, outliers 0.00%, region centre 0.2"),
    "selfcheck_fog_switchoff.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.04, outliers 0.00%, region centre 0.2"),

    # -- decals. The scene is the imported platform under a FLAT AMBIENT
    # (0.5 grey) plus a point light, and that ambient term is where the two
    # imported-material fixes still disagree: classic reads the floor a
    # uniform ~1.6x brighter in display space (~2.9x linear), edge to edge,
    # with no bump under the point light - so the divergence is the ambient
    # response alone, while the SAME mesh under the shadow family's direct
    # sun matches at mean ~1.94. The re-routes moved the two flavors in
    # opposite directions here (classic gained the display transfer, next
    # lost the white-specular lift), widening this family 9.55 -> 26.91
    # mean; the same seam is the window/drawlayer2d shots' platform cluster
    # (0.36% -> 0.97% outliers, still inside their gate). Named, unresolved:
    # the imported-material ambient seam.
    "selfcheck_decal_baseline.png": ShotCorridor(
        32.0, 0.55, 68.0, False,
        "mean 26.91, outliers 47.12%, region centre 56.3 - the "
        "imported-material ambient seam (see the family note)"),
    "selfcheck_decal_faded.png": ShotCorridor(
        32.0, 0.55, 68.0, False,
        "mean 26.91, outliers 47.12%, region centre 56.3"),
    "selfcheck_decal_budget_off.png": ShotCorridor(
        32.0, 0.55, 68.0, False,
        "mean 26.91, outliers 47.12%, region centre 56.3"),
    "selfcheck_decal_mark.png": ShotCorridor(
        27.0, 0.40, 37.0, False,
        "mean 22.33, outliers 33.78%, region br 30.4 - the mark itself "
        "reads on both flavors; the offset is the family's ambient seam"),

    # -- bloom.
    "selfcheck_bloom_off.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.00, outliers 0.00%, region 0.0"),
    "selfcheck_bloom_off_restored.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.00, outliers 0.00%, region 0.0"),
    "selfcheck_bloom_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.12, outliers 0.00%, region br 0.2"),
    "selfcheck_bloom_high.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.10, outliers 0.00%, region centre 0.2"),

    # -- the output grade. The grade's INDUCED deltas have their own gate
    # (run_grade_probe_test.py); these frames are the absolute look on each
    # side of it, which that gate's difference-of-differences cannot see.
    "selfcheck_grade_off.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.00, outliers 0.00%, region 0.0"),
    "selfcheck_grade_off_restored.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.00, outliers 0.00%, region 0.0"),
    "selfcheck_grade_on.png": ShotCorridor(
        6.0, 0.02, 6.0, True,
        "mean 0.00, outliers 0.00%, region 0.0 - the one display-space "
        "grade shader on both flavors"),
}


def decode_png(path):
    """Minimal PNG decoder: 8-bit RGB/RGBA/gray, non-interlaced.

    Returns (width, height, channels, bytearray of unfiltered scanlines).
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    width = height = None
    bit_depth = colour_type = None
    idat = bytearray()
    while pos < len(data):
        length, chunk_type = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            (width, height, bit_depth, colour_type,
             _compression, _filter, interlace) = struct.unpack(
                ">IIBBBBB", chunk)
            if bit_depth != 8 or colour_type not in (0, 2, 6):
                raise ValueError(f"{path}: unsupported PNG "
                                 f"(depth {bit_depth}, colour {colour_type})")
            if interlace != 0:
                raise ValueError(f"{path}: interlaced PNGs unsupported")
        elif chunk_type == b"IDAT":
            idat.extend(chunk)
        elif chunk_type == b"IEND":
            break
    channels = {0: 1, 2: 3, 6: 4}[colour_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * channels)
    previous = bytearray(stride)
    src = 0
    for row in range(height):
        filter_type = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if filter_type == 1:    # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                up_left = previous[i - channels] if i >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                if pa <= pb and pa <= pc:
                    predictor = left
                elif pb <= pc:
                    predictor = up
                else:
                    predictor = up_left
                line[i] = (line[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"{path}: unknown PNG filter {filter_type}")
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, out


def compare_pair(classic_path, next_path, diff_dir=None):
    """Compare one screenshot pair; returns (ok, human-readable summary).

    One pass over the pair produces the per-pixel delta map, and the mean, the
    outlier fraction and the region shape are all read off THAT - the pixels
    are never walked twice for three numbers.
    """
    classic = decode_png(classic_path)
    nxt = decode_png(next_path)
    if (classic[0], classic[1]) != (nxt[0], nxt[1]):
        return False, (f"dimension mismatch: classic {classic[0]}x{classic[1]} "
                       f"vs next {nxt[0]}x{nxt[1]}")
    dmap = parity_diff.delta_map(classic, nxt)
    pixel_count = dmap.width * dmap.height
    outliers = sum(1 for delta in dmap.deltas if delta > OUTLIER_TOLERANCE)
    mean = dmap.channel_sum / float(pixel_count * dmap.channels)
    outlier_fraction = outliers / float(pixel_count)
    ok = mean <= MEAN_TOLERANCE and outlier_fraction <= OUTLIER_FRACTION
    spatial = parity_diff.spatial_summary(dmap, OUTLIER_TOLERANCE)
    summary = (f"mean {mean:.2f}/{MEAN_TOLERANCE} "
               f"outliers {outlier_fraction * 100.0:.2f}%/"
               f"{OUTLIER_FRACTION * 100.0:.0f}% (>{OUTLIER_TOLERANCE}); "
               + parity_diff.describe(spatial, pixel_count, OUTLIER_TOLERANCE))
    destination = parity_diff.diff_path(next_path, diff_dir)
    if outliers == 0:
        parity_diff.drop_stale_diff(destination)
    else:
        # the picture travels whenever there is structure to look at, ON
        # GREEN TOO: a verdict inside its corridor can still sit over a
        # region of strongly differing pixels, and the numbers alone cannot
        # show what that region is. A pair with no pixel over the threshold
        # writes nothing - a heat map of zeros is noise
        written = parity_diff.try_write_diff(destination, dmap, classic)
        if written:
            summary += f"; diff image {written}"
    return ok, summary


def read_dimensions(out_dir):
    """Parse a selfcheck's dimensions.txt sidecar.

    Returns {"logical": (w, h), "pixel": (w, h)} or None when absent/malformed.
    The sidecar lets the parity gate assert both flavors agree on the LOGICAL
    (points) window and the PIXEL (drawable) surface for the same request -
    the density-disagreement signal, kept independent of the host's display
    scale (both flavors track the same OS backing scale, so they must match on
    any machine).
    """
    path = os.path.join(out_dir, "dimensions.txt")
    if not os.path.exists(path):
        return None
    result = {}
    with open(path) as handle:
        for line in handle:
            parts = line.split()
            if len(parts) == 3 and parts[0] in ("logical", "pixel"):
                result[parts[0]] = (int(parts[1]), int(parts[2]))
    if "logical" not in result or "pixel" not in result:
        return None
    return result


def compare_dimensions(classic_dims, next_dims):
    """Assert the two flavors made the same pixel-density choice.

    Returns (ok, summary). Fails if either the logical (points) request or the
    resulting pixel (drawable) surface differs between flavors - the exact
    class of bug the HiDPI gap was (Metal at 2x backing vs GL at 1x logical).
    """
    if classic_dims is None or next_dims is None:
        return False, ("dimensions.txt missing - cannot verify the flavors "
                       "agree on window pixel density")
    ok = True
    notes = []
    for key in ("logical", "pixel"):
        cw, ch = classic_dims[key]
        nw, nh = next_dims[key]
        if (cw, ch) != (nw, nh):
            ok = False
            notes.append(f"{key} mismatch: classic {cw}x{ch} vs next {nw}x{nh}")
        else:
            notes.append(f"{key} {cw}x{ch}")
    return ok, "; ".join(notes)


def run_selfcheck(binary, out_dir, cwd):
    os.makedirs(out_dir, exist_ok=True)
    environment = dict(os.environ)
    environment["ORKIGE_SELFCHECK_OUT"] = out_dir
    result = subprocess.run([binary], cwd=cwd, env=environment,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=300)
    if result.returncode != 0:
        sys.stdout.buffer.write(result.stdout)
        raise RuntimeError(f"{binary} exited with {result.returncode}")


class ShotsUnusable(Exception):
    """A captured output directory cannot be compared - refuse, never pass."""


def verify_shots_dir(label, directory):
    """Refuse a captured directory that carries nothing to compare.

    The failure mode this exists to prevent: a comparison handed an empty or
    wrong directory reports parity because it found no disagreement. Silence
    is not evidence, so a directory that is absent, empty or short of every
    compared screenshot raises instead.
    """
    if not os.path.isdir(directory):
        raise ShotsUnusable(f"{label} screenshot directory does not exist: "
                            f"{directory}")
    if not os.listdir(directory):
        raise ShotsUnusable(f"{label} screenshot directory is empty: "
                            f"{directory}")
    if not any(os.path.exists(os.path.join(directory, shot))
               for shot in COMPARED_SHOTS):
        raise ShotsUnusable(
            f"{label} screenshot directory carries none of the compared "
            f"screenshots ({', '.join(COMPARED_SHOTS)}): {directory}")


def compare_shot_dirs(classic_out, next_out, diff_dir=None):
    """Compare two captured selfcheck output directories; returns failures."""
    failures = 0

    # density gate FIRST: both flavors must make the same pixel-density choice
    # for the same window request (logical points AND drawable pixels). This is
    # the WYSIWYG contract at the surface level - if it fails, the per-pixel
    # compare below would also fail on dimensions, but this reports the real
    # cause (a flavor ignoring the OS backing scale) directly.
    dims_ok, dims_summary = compare_dimensions(
        read_dimensions(classic_out), read_dimensions(next_out))
    print(f"{'ok  ' if dims_ok else 'FAIL'} window density: {dims_summary}")
    failures += 0 if dims_ok else 1

    for shot in COMPARED_SHOTS:
        classic_path = os.path.join(classic_out, shot)
        next_path = os.path.join(next_out, shot)
        if not os.path.exists(classic_path) or not os.path.exists(next_path):
            print(f"FAIL {shot}: screenshot missing "
                  f"({classic_path} / {next_path})")
            failures += 1
            continue
        ok, summary = compare_pair(classic_path, next_path, diff_dir)
        print(f"{'ok  ' if ok else 'FAIL'} {shot}: {summary}")
        failures += 0 if ok else 1
    return failures


# --- the feature sweep: region math, per-shot scoring, the package ----------

def frame_regions(width, height):
    """The sweep's region boxes for a frame of this size (pure).

    Four quadrants plus a centre box, as (name, (x0, y0, x1, y1)) with x1/y1
    exclusive. An odd dimension gives its extra row/column to the second half
    rather than dropping it, so the four quadrants tile the frame exactly.
    """
    half_x, half_y = width // 2, height // 2
    inset_x, inset_y = int(width * CENTRE_INSET), int(height * CENTRE_INSET)
    return (
        ("tl", (0, 0, half_x, half_y)),
        ("tr", (half_x, 0, width, half_y)),
        ("bl", (0, half_y, half_x, height)),
        ("br", (half_x, half_y, width, height)),
        ("centre", (inset_x, inset_y, width - inset_x, height - inset_y)),
    )


def region_mean(image, box, step=4):
    """Mean (r, g, b) over a box, sampled every `step` pixels (pure).

    Sampled rather than exhaustive because the answer wanted is a REGION's
    colour, which a quarter-density sample carries to well under a level, and
    the sweep walks 40-odd full-HD frames.
    """
    width, height, channels, data = image
    x0, y0, x1, y1 = box
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(x1, width), min(y1, height)
    total = [0, 0, 0]
    count = 0
    for y in range(y0, y1, step):
        row = y * width
        for x in range(x0, x1, step):
            base = (row + x) * channels
            total[0] += data[base]
            total[1] += data[base + 1] if channels >= 3 else data[base]
            total[2] += data[base + 2] if channels >= 3 else data[base]
            count += 1
    if count == 0:
        return (0.0, 0.0, 0.0)
    return tuple(value / float(count) for value in total)


def frame_spread(image, step=4):
    """The sampled red-channel spread of a frame: max minus min (pure).

    Zero means one flat colour edge to edge - a frame with no picture in it.
    The red channel alone is enough for the question asked (is there ANY
    variation), and one channel is a third of the walk.
    """
    width, height, channels, data = image
    lowest, highest = 255, 0
    for y in range(0, height, step):
        row = y * width
        for x in range(0, width, step):
            value = data[(row + x) * channels]
            if value < lowest:
                lowest = value
            if value > highest:
                highest = value
    return max(0, highest - lowest)


def region_deltas(classic, nxt, step=4):
    """Per-region max channel delta of the two frames' region means (pure)."""
    result = []
    for name, box in frame_regions(classic[0], classic[1]):
        mean_c = region_mean(classic, box, step)
        mean_n = region_mean(nxt, box, step)
        result.append((name, max(abs(a - b) for a, b in zip(mean_c, mean_n))))
    return result


#: one shot's measured numbers. `worst_region`/`worst_delta` name the region
#: that disagrees most, which is the sweep's severity key.
ShotMeasurement = namedtuple(
    "ShotMeasurement",
    "shot mean outlier_fraction regions worst_region worst_delta spatial "
    "pixel_count flat")


def measure_pair(shot, classic_path, next_path):
    """Score one pair: frame mean, outlier fraction, regions, cluster shape.

    One `delta_map` pass produces the mean, the outliers and the spatial
    summary; the region means are read off the decoded frames beside it. The
    frames are never walked twice for the same fact.
    """
    classic = decode_png(classic_path)
    nxt = decode_png(next_path)
    if (classic[0], classic[1]) != (nxt[0], nxt[1]):
        raise ValueError(f"dimension mismatch: classic "
                         f"{classic[0]}x{classic[1]} vs next "
                         f"{nxt[0]}x{nxt[1]}")
    dmap = parity_diff.delta_map(classic, nxt)
    pixel_count = dmap.width * dmap.height
    outliers = sum(1 for delta in dmap.deltas
                   if delta > FEATURE_OUTLIER_TOLERANCE)
    regions = region_deltas(classic, nxt)
    worst_region, worst_delta = max(regions, key=lambda entry: entry[1])
    return (ShotMeasurement(
        shot=shot,
        mean=dmap.channel_sum / float(pixel_count * dmap.channels),
        outlier_fraction=outliers / float(pixel_count),
        regions=regions,
        worst_region=worst_region,
        worst_delta=worst_delta,
        spatial=parity_diff.spatial_summary(dmap, FEATURE_OUTLIER_TOLERANCE),
        pixel_count=pixel_count,
        flat=(frame_spread(classic) <= FLAT_FRAME_SPREAD and
              frame_spread(nxt) <= FLAT_FRAME_SPREAD)), dmap, classic)


def measurement_line(measurement, corridor=None):
    """The measurement table's row for one shot."""
    regions = " ".join(f"{name}={delta:.1f}"
                       for name, delta in measurement.regions)
    corridor_text = ""
    if corridor is not None:
        corridor_text = (f" [corridor mean {corridor.mean:.1f} outliers "
                         f"{corridor.outliers * 100.0:.0f}% region "
                         f"{corridor.region:.1f}"
                         f"{'' if corridor.gated else ', report-only'}]")
    return (f"{measurement.shot}: mean {measurement.mean:.2f} outliers "
            f"{measurement.outlier_fraction * 100.0:.2f}% worst region "
            f"{measurement.worst_region}={measurement.worst_delta:.1f}; "
            + parity_diff.describe(measurement.spatial,
                                   measurement.pixel_count,
                                   FEATURE_OUTLIER_TOLERANCE)
            + (" [FLAT PAIR]" if measurement.flat else "")
            + corridor_text)


def corridor_breaches(measurement, corridor):
    """Which of a corridor's three bounds this measurement exceeds."""
    breaches = []
    if measurement.mean > corridor.mean:
        breaches.append(f"mean {measurement.mean:.2f} > {corridor.mean:.1f}")
    if measurement.outlier_fraction > corridor.outliers:
        breaches.append(
            f"outliers {measurement.outlier_fraction * 100.0:.2f}% > "
            f"{corridor.outliers * 100.0:.0f}%")
    if measurement.worst_delta > corridor.region:
        breaches.append(f"region {measurement.worst_region} "
                        f"{measurement.worst_delta:.1f} > "
                        f"{corridor.region:.1f}")
    return breaches


def stage_adjudication(directory, shot, classic_path, next_path, dmap,
                       reference):
    """Write one shot's side-by-side pair plus its diff, self-describing.

    `<shot>-classic.png` / `<shot>-next.png` / `<shot>-diff.png` - the three
    files a person needs to say whether a divergence is a defect or the
    flavors' documented seam. Copied rather than referenced because the pair
    is the evidence and a capture directory is scratch.
    """
    os.makedirs(directory, exist_ok=True)
    stem = os.path.splitext(shot)[0]
    shutil.copyfile(classic_path, os.path.join(directory,
                                               stem + "-classic.png"))
    shutil.copyfile(next_path, os.path.join(directory, stem + "-next.png"))
    parity_diff.try_write_diff(os.path.join(directory, stem + "-diff.png"),
                               dmap, reference)
    return stem


def verify_feature_shots_dir(label, directory):
    """Refuse a captured directory that carries none of the swept shots.

    The same rule the pixel gate keeps, over the sweep's own shot set: a
    comparison handed an empty or wrong directory must refuse rather than
    report parity because it found nothing to disagree about.
    """
    if not os.path.isdir(directory):
        raise ShotsUnusable(f"{label} screenshot directory does not exist: "
                            f"{directory}")
    if not any(os.path.exists(os.path.join(directory, shot))
               for shot in FEATURE_SHOTS):
        raise ShotsUnusable(
            f"{label} screenshot directory carries none of the swept feature "
            f"screenshots: {directory}")


def feature_sweep(classic_out, next_out, report_only=False, diff_dir=None,
                  adjudication_dir=None,
                  adjudication_delta=ADJUDICATION_REGION_DELTA):
    """Compare every FEATURE_SHOTS pair; returns the number of failures.

    A shot in the table that is missing from either directory is a FAILURE in
    EVERY mode, `--report-only` included: report-only silences a corridor
    VERDICT, never the fact that there was nothing to compare.
    """
    verify_feature_shots_dir("classic", classic_out)
    verify_feature_shots_dir("next", next_out)

    failures = 0
    measurements = []
    for shot in sorted(FEATURE_SHOTS):
        classic_path = os.path.join(classic_out, shot)
        next_path = os.path.join(next_out, shot)
        missing = [path for path in (classic_path, next_path)
                   if not os.path.exists(path) or
                   os.path.getsize(path) == 0]
        if missing:
            print(f"FAIL {shot}: capture missing or empty "
                  f"({', '.join(missing)}) - a parity gate that compared "
                  f"nothing must not report parity")
            failures += 1
            continue
        try:
            measurement, dmap, reference = measure_pair(
                shot, classic_path, next_path)
        except (ValueError, OSError) as error:
            print(f"FAIL {shot}: {error}")
            failures += 1
            continue
        measurements.append((measurement, dmap, reference,
                             classic_path, next_path))

    # the table, worst first - severity is what a reader wants ordered
    measurements.sort(key=lambda entry: entry[0].worst_delta, reverse=True)
    for measurement, dmap, reference, classic_path, next_path in measurements:
        corridor = FEATURE_SHOTS[measurement.shot]
        breaches = corridor_breaches(measurement, corridor)
        # a GATED shot that went flat on BOTH flavors is a failure whatever
        # its deltas say: two blank frames agree perfectly, so banking that
        # agreement would report parity over a frame carrying no picture. The
        # table marks the shots that are flat BY DESIGN report-only.
        if corridor.gated and measurement.flat:
            breaches.append("both frames are one flat colour - a flat pair "
                            "agrees perfectly and proves nothing")
        gated = corridor.gated and not report_only
        if breaches and gated:
            status = "FAIL"
            failures += 1
        elif breaches:
            status = "note"
        else:
            status = "ok  "
        print(f"{status} {measurement_line(measurement, corridor)}")
        if breaches:
            print(f"       breach: {'; '.join(breaches)}")

        # the picture travels whenever there is structure, on green too - a
        # verdict inside its corridor can sit over a real differing region
        destination = parity_diff.diff_path(next_path, diff_dir)
        if measurement.spatial.largest:
            parity_diff.try_write_diff(destination, dmap, reference)
        else:
            parity_diff.drop_stale_diff(destination)

        if adjudication_dir and measurement.worst_delta >= adjudication_delta:
            stem = stage_adjudication(adjudication_dir, measurement.shot,
                                      classic_path, next_path, dmap, reference)
            print(f"       adjudication: {stem}-classic/-next/-diff.png in "
                  f"{adjudication_dir}")
    return failures


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--next-binary",
                        help="this build tree's render_facade_selfcheck")
    parser.add_argument("--classic-binary",
                        help="the classic tree's render_facade_selfcheck "
                             "(SKIP when absent)")
    parser.add_argument("--out",
                        help="working directory for both runs' screenshots")
    parser.add_argument("--repo",
                        help="repo root (the selfcheck's working directory)")
    parser.add_argument("--classic-shots",
                        help="a classic selfcheck output directory captured "
                             "elsewhere (compared as-is, nothing is run)")
    parser.add_argument("--next-shots",
                        help="a next selfcheck output directory captured "
                             "elsewhere (compared as-is, nothing is run)")
    parser.add_argument("--diff-dir",
                        help="where a failing pair's diff image lands "
                             "(default: beside the next screenshot)")
    parser.add_argument("--feature-sweep", action="store_true",
                        help="compare the FEATURE_SHOTS set (every other "
                             "frame both flavors write) instead of the pixel "
                             "gate's four")
    parser.add_argument("--report-only", action="store_true",
                        help="measure and print, gate nothing (a missing "
                             "capture still fails)")
    parser.add_argument("--adjudication-dir",
                        help="write <shot>-classic/-next/-diff.png here for "
                             "every swept shot whose worst region mean "
                             "differs by --adjudication-delta or more")
    parser.add_argument("--adjudication-delta", type=float,
                        default=ADJUDICATION_REGION_DELTA,
                        help="region-mean delta at which a shot joins the "
                             "adjudication package (default %(default)s)")
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the pure parts and exit")
    return parser.parse_args(argv)


def resolve_directories(args):
    """Produce the (classic, next) directories to compare, running if asked.

    Returns None when the run road is unavailable and the honest answer is a
    skip; raises ShotsUnusable when captured directories cannot be compared.
    """
    captured = bool(args.classic_shots) or bool(args.next_shots)
    if captured:
        if not (args.classic_shots and args.next_shots):
            raise ShotsUnusable("--classic-shots and --next-shots come as a "
                                "pair - one alone compares nothing")
        verify_shots_dir("classic", args.classic_shots)
        verify_shots_dir("next", args.next_shots)
        return args.classic_shots, args.next_shots

    missing = [name for name, value in (("--next-binary", args.next_binary),
                                        ("--classic-binary",
                                         args.classic_binary),
                                        ("--out", args.out),
                                        ("--repo", args.repo)) if not value]
    if missing:
        raise ShotsUnusable("either the captured directories "
                            "(--classic-shots + --next-shots) or the full "
                            "run arguments are required; missing "
                            + ", ".join(missing))
    if not os.path.exists(args.classic_binary):
        return None

    classic_out = os.path.join(args.out, "classic")
    next_out = os.path.join(args.out, "next")
    print(f"running classic selfcheck: {args.classic_binary}")
    run_selfcheck(args.classic_binary, classic_out, args.repo)
    print(f"running next selfcheck: {args.next_binary}")
    run_selfcheck(args.next_binary, next_out, args.repo)
    return classic_out, next_out


def main(argv=None):
    args = parse_args(argv)
    if args.selftest:
        return selftest()

    try:
        directories = resolve_directories(args)
    except ShotsUnusable as refusal:
        print(f"render_backend_parity: FAIL: {refusal}")
        return 1
    if directories is None:
        print(f"SKIP: classic selfcheck binary not built "
              f"({args.classic_binary}) - configure + build the classic "
              f"preset to enable the cross-backend parity comparison")
        return SKIP_EXIT_CODE

    if args.feature_sweep:
        try:
            failures = feature_sweep(
                *directories, report_only=args.report_only,
                diff_dir=args.diff_dir,
                adjudication_dir=args.adjudication_dir,
                adjudication_delta=args.adjudication_delta)
        except ShotsUnusable as refusal:
            print(f"render_feature_parity: FAIL: {refusal}")
            return 1
        if failures:
            print(f"render_feature_parity: {failures} feature screenshot "
                  f"pair(s) out of tolerance - the flavors must render the "
                  f"same image (Docs/render-abstraction.md, colour parity)")
            return 1
        print("render_feature_parity: every compared feature screenshot "
              "within its corridor"
              + (" (report-only)" if args.report_only else ""))
        return 0

    failures = compare_shot_dirs(*directories, diff_dir=args.diff_dir)
    if failures:
        print(f"render_backend_parity: {failures} screenshot pair(s) out of "
              f"tolerance - the backends must render the same image "
              f"(Docs/render-abstraction.md, colour parity)")
        return 1
    print("render_backend_parity: all screenshot pairs within tolerance")
    return 0


# --- selftest ---------------------------------------------------------------

def write_png(path, width, height, fill, poke=None, stripe=None):
    """Write a minimal 8-bit RGB PNG of one colour (selftest fixture).

    poke=(x, y, colour) recolours a single pixel - the smallest possible
    "structure" for the on-green diff-image case. stripe=colour paints the
    lower half in a second colour, which is what makes a fixture frame
    CONTENT-BEARING rather than flat (the sweep refuses to bank a gated
    agreement between two flat frames).
    """
    raw = bytearray()
    for row in range(height):
        raw.append(0)                       # filter type None
        band = stripe if (stripe is not None and row >= height // 2) else fill
        raw.extend(bytes(band) * width)
        if poke is not None and poke[1] == row:
            base = len(raw) - (width - poke[0]) * 3
            raw[base:base + 3] = bytes(poke[2])
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw))))
        handle.write(chunk(b"IEND", b""))


def write_shots_dir(directory, fill, logical=(960, 540), pixel=(960, 540),
                    poke=None):
    os.makedirs(directory, exist_ok=True)
    for shot in COMPARED_SHOTS:
        write_png(os.path.join(directory, shot), 8, 8, fill, poke)
    with open(os.path.join(directory, "dimensions.txt"), "w") as handle:
        handle.write("logical %d %d\npixel %d %d\n"
                     % (logical[0], logical[1], pixel[0], pixel[1]))


def write_feature_dir(directory, fill, stripe=(200, 40, 40), poke=None,
                      flat=False):
    """A directory carrying the whole swept set, content-bearing by default."""
    write_shots_dir(directory, fill)
    for shot in FEATURE_SHOTS:
        write_png(os.path.join(directory, shot), 8, 8, fill, poke,
                  None if flat else stripe)


def run_quiet(argv):
    """Run main() swallowing its report - a passing selftest logs no FAIL."""
    captured = io.StringIO()
    with contextlib.redirect_stdout(captured):
        code = main(argv)
    return code, captured.getvalue()


def expect_refusal(what, argv, names):
    """A run that MUST refuse: exit 1, saying which directory it refused."""
    code, said = run_quiet(argv)
    if code != 1:
        raise AssertionError(f"{what} returned {code}, must refuse with 1")
    if names not in said:
        raise AssertionError(f"{what} refused without naming {names}: {said}")


def selftest():
    scratch = tempfile.mkdtemp(prefix="orkige_parity_selftest_")
    classic = os.path.join(scratch, "classic")
    nxt = os.path.join(scratch, "next")

    # the shared diagnosis: clustering, the heat ramp, the diff destination,
    # and the writer read back through THIS file's decoder
    parity_diff.selftest_pure()
    parity_diff.selftest_roundtrip(decode_png, scratch)

    # identical captures compare clean, report their shape and leave no diff
    write_shots_dir(classic, (40, 80, 120))
    write_shots_dir(nxt, (40, 80, 120))
    code, said = run_quiet(["--classic-shots", classic, "--next-shots", nxt])
    assert code == 0, said
    assert "no pixel over" in said, said
    diff_image = os.path.join(nxt, COMPARED_SHOTS[0].replace(".png",
                                                             ".diff.png"))
    assert not os.path.exists(diff_image), "a clean pair wrote a diff image"

    # a GREEN pair with structure - one pixel over the threshold, well
    # inside the outlier fraction - leaves its picture behind: the on-green
    # diff is what lets a passing-but-real region be looked at
    write_shots_dir(nxt, (40, 80, 120), poke=(3, 3, (250, 80, 120)))
    code, said = run_quiet(["--classic-shots", classic, "--next-shots", nxt])
    assert code == 0, said
    assert "largest region 1px" in said, said
    assert os.path.exists(diff_image), "a green pair with structure wrote " \
        "no diff image"
    # and a later clean run drops the stale picture
    write_shots_dir(nxt, (40, 80, 120))
    code, said = run_quiet(["--classic-shots", classic, "--next-shots", nxt])
    assert code == 0, said
    assert not os.path.exists(diff_image), "a clean re-run kept a stale diff"

    # a difference beyond tolerance is caught, names the region it found and
    # leaves the picture behind - the whole 8x8 fixture is one region
    write_shots_dir(nxt, (200, 80, 120))
    code, said = run_quiet(["--classic-shots", classic, "--next-shots", nxt])
    assert code == 1, said
    assert "largest region 64px" in said, said
    assert os.path.exists(diff_image), said
    assert decode_png(diff_image)[0] == 8
    assert diff_image in said, said

    # ... a diff directory of its own is honored
    elsewhere = os.path.join(scratch, "diffs")
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt,
                      "--diff-dir", elsewhere])[0] == 1
    assert os.path.exists(os.path.join(
        elsewhere, COMPARED_SHOTS[0].replace(".png", ".diff.png")))

    # ... and a difference inside tolerance is not a failure, and takes the
    # now-stale picture of the old one away with it
    write_shots_dir(nxt, (43, 80, 120))
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 0
    assert not os.path.exists(diff_image), "a passing pair kept a stale diff"

    # a density disagreement fails on its own, before any pixel differs
    write_shots_dir(nxt, (40, 80, 120), pixel=(1920, 1080))
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 1
    write_shots_dir(nxt, (40, 80, 120))

    # THE refusals: comparing nothing must never read as parity
    absent = os.path.join(scratch, "absent")
    expect_refusal("a missing directory",
                   ["--classic-shots", absent, "--next-shots", nxt], absent)
    empty = os.path.join(scratch, "empty")
    os.makedirs(empty, exist_ok=True)
    expect_refusal("an empty directory",
                   ["--classic-shots", empty, "--next-shots", nxt], empty)
    unrelated = os.path.join(scratch, "unrelated")
    os.makedirs(unrelated, exist_ok=True)
    write_png(os.path.join(unrelated, "something_else.png"), 8, 8, (0, 0, 0))
    expect_refusal("a directory holding no compared screenshot",
                   ["--classic-shots", unrelated, "--next-shots", nxt],
                   unrelated)
    expect_refusal("one directory without the other",
                   ["--classic-shots", classic], "--next-shots")
    expect_refusal("no arguments at all", [], "--classic-shots")

    # a shot missing from an otherwise usable directory is a failure too
    partial = os.path.join(scratch, "partial")
    write_shots_dir(partial, (40, 80, 120))
    os.remove(os.path.join(partial, COMPARED_SHOTS[-1]))
    code, said = run_quiet(["--classic-shots", classic,
                            "--next-shots", partial])
    assert code == 1 and COMPARED_SHOTS[-1] in said, said

    # the run road keeps its honest skip when the sibling tree is unbuilt
    assert run_quiet(["--next-binary", "/nonexistent/next",
                      "--classic-binary", "/nonexistent/classic",
                      "--out", scratch,
                      "--repo", scratch])[0] == SKIP_EXIT_CODE

    # argument routing
    parsed = parse_args(["--classic-shots", "a", "--next-shots", "b"])
    assert parsed.classic_shots == "a" and parsed.next_shots == "b"
    assert parse_args(["--selftest"]).selftest is True
    assert parse_args(["--feature-sweep", "--report-only"]).report_only is True

    selftest_feature_sweep(scratch)

    shutil.rmtree(scratch, ignore_errors=True)
    print("compare_backend_screenshots: selftest OK")
    return 0


def selftest_region_math():
    """The sweep's pure geometry and sampling."""
    regions = dict(frame_regions(100, 60))
    assert set(regions) == {"tl", "tr", "bl", "br", "centre"}
    # the four quadrants tile the frame exactly - no pixel counted twice, none
    # dropped, which is what makes four region means a partition of the image
    assert regions["tl"] == (0, 0, 50, 30) and regions["bl"] == (0, 30, 50, 60)
    assert regions["tr"] == (50, 0, 100, 30)
    assert regions["br"] == (50, 30, 100, 60)
    # an odd dimension gives its extra row/column to the second half
    odd = dict(frame_regions(9, 7))
    assert odd["tl"] == (0, 0, 4, 3) and odd["br"] == (4, 3, 9, 7)
    # the centre box is inset from every edge and lies inside the frame
    cx0, cy0, cx1, cy1 = regions["centre"]
    assert 0 < cx0 < cx1 < 100 and 0 < cy0 < cy1 < 60

    # region_mean reads the colour it is pointed at, and only that colour
    width, height = 8, 8
    data = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            base = (y * width + x) * 3
            data[base:base + 3] = (b"\x64\x00\x00" if y < 4
                                   else b"\x00\xc8\x00")
    image = (width, height, 3, data)
    top = region_mean(image, (0, 0, 8, 4), step=1)
    assert top == (100.0, 0.0, 0.0), top
    bottom = region_mean(image, (0, 4, 8, 8), step=1)
    assert bottom == (0.0, 200.0, 0.0), bottom
    # a box clamped to nothing means nothing, never a division by zero
    assert region_mean(image, (50, 50, 60, 60), step=1) == (0.0, 0.0, 0.0)

    # frame_spread: flat says flat, two bands say otherwise
    flat = (4, 4, 3, bytearray(b"\x20\x20\x20" * 16))
    assert frame_spread(flat, step=1) == 0
    assert frame_spread(image, step=1) == 100

    # a region-mean delta is blind to the scattered lone outlier pixels the
    # facade window shots carry (a texture-sampling phase checkerboard,
    # adjudicated harmless) - the whole reason the sweep scores region MEANS
    # and no per-pixel maximum anywhere in this file
    side = 64
    field = bytearray(b"\x40\x50\x60" * (side * side))
    speckled = bytearray(field)
    for index in range(0, side * side // 4, 37):     # scattered, not clustered
        speckled[index * 3] = 255
    deltas = dict(region_deltas((side, side, 3, field),
                                (side, side, 3, speckled), step=1))
    assert max(delta for _name, delta in deltas.items()) < 6.0, deltas


def selftest_corridor_table():
    """The corridor table is well-formed and says which shots it gates."""
    for shot, corridor in FEATURE_SHOTS.items():
        assert shot.endswith(".png"), shot
        assert isinstance(corridor, ShotCorridor), shot
        assert corridor.mean > 0 and corridor.region > 0, shot
        assert 0.0 < corridor.outliers <= 1.0, shot
        assert isinstance(corridor.gated, bool), shot
        # every entry carries the measurement its corridor came from
        assert corridor.note and len(corridor.note) > 20, shot
    # the two sets are disjoint: the pixel gate's four are not re-scored here
    assert not set(FEATURE_SHOTS) & set(COMPARED_SHOTS)
    # the sweep is worth running: some of it actually gates
    assert any(corridor.gated for corridor in FEATURE_SHOTS.values())

    # corridor_breaches names each bound it crosses, and only those
    clean = ShotMeasurement("s.png", 1.0, 0.001, [("tl", 1.0)], "tl", 1.0,
                            parity_diff.spatial_summary(
                                parity_diff.make_map(2, 2, [0] * 4)),
                            4, False)
    corridor = ShotCorridor(6.0, 0.02, 6.0, True, "a measured corridor line")
    assert corridor_breaches(clean, corridor) == []
    breached = clean._replace(mean=9.0, worst_delta=99.0)
    said = "; ".join(corridor_breaches(breached, corridor))
    assert "mean 9.00 > 6.0" in said and "region tl 99.0 > 6.0" in said, said
    assert "outliers" not in said, said


def selftest_feature_sweep(scratch):
    """The sweep end to end: verdicts, the refusals, the flat-pair rule."""
    selftest_region_math()
    selftest_corridor_table()

    classic = os.path.join(scratch, "fclassic")
    nxt = os.path.join(scratch, "fnext")
    sweep = ["--feature-sweep", "--classic-shots", classic,
             "--next-shots", nxt]

    # identical content-bearing captures compare clean
    write_feature_dir(classic, (40, 80, 120))
    write_feature_dir(nxt, (40, 80, 120))
    code, said = run_quiet(sweep)
    assert code == 0, said
    assert "within its corridor" in said, said

    # a gated shot outside its corridor FAILS, naming the bound it crossed
    gated = next(shot for shot, corridor in FEATURE_SHOTS.items()
                 if corridor.gated)
    write_png(os.path.join(nxt, gated), 8, 8, (240, 80, 120),
              stripe=(200, 40, 40))
    code, said = run_quiet(sweep)
    assert code == 1 and gated in said, said
    assert "breach:" in said and "region" in said, said
    # ... and --report-only measures the same divergence without gating it
    code, said = run_quiet(sweep + ["--report-only"])
    assert code == 0 and gated in said, said
    assert "breach:" in said, said
    write_png(os.path.join(nxt, gated), 8, 8, (40, 80, 120),
              stripe=(200, 40, 40))

    # a report-only shot outside its corridor is noted, never gated
    reported = next(shot for shot, corridor in FEATURE_SHOTS.items()
                    if not corridor.gated)
    write_png(os.path.join(nxt, reported), 8, 8, (250, 250, 250),
              stripe=(250, 250, 250))
    code, said = run_quiet(sweep)
    assert code == 0, said
    assert f"note {reported}" in said, said
    write_png(os.path.join(nxt, reported), 8, 8, (40, 80, 120),
              stripe=(200, 40, 40))

    # THE refusals: comparing nothing must never read as parity, and
    # --report-only silences a VERDICT, never a missing capture
    absent_shot = os.path.join(nxt, gated)
    os.remove(absent_shot)
    for extra in ([], ["--report-only"]):
        code, said = run_quiet(sweep + extra)
        assert code == 1, (extra, said)
        assert gated in said and "compared nothing" in said, said
    open(absent_shot, "wb").close()
    code, said = run_quiet(sweep)
    assert code == 1 and "missing or empty" in said, said
    write_png(absent_shot, 8, 8, (40, 80, 120), stripe=(200, 40, 40))

    # a directory carrying none of the swept shots refuses rather than passes
    unrelated = os.path.join(scratch, "funrelated")
    os.makedirs(unrelated, exist_ok=True)
    write_png(os.path.join(unrelated, "something_else.png"), 8, 8, (0, 0, 0))
    code, said = run_quiet(["--feature-sweep", "--classic-shots", unrelated,
                            "--next-shots", nxt])
    assert code == 1 and unrelated in said, said

    # a differently sized capture has no per-pixel correspondence at all
    write_png(os.path.join(nxt, gated), 16, 16, (40, 80, 120),
              stripe=(200, 40, 40))
    code, said = run_quiet(sweep)
    assert code == 1 and "dimension mismatch" in said, said
    write_png(os.path.join(nxt, gated), 8, 8, (40, 80, 120),
              stripe=(200, 40, 40))

    # THE flat-pair rule: two blank frames agree perfectly and prove nothing,
    # so a GATED shot that went flat on both sides is a failure, not a pass
    write_png(os.path.join(classic, gated), 8, 8, (40, 80, 120))
    write_png(os.path.join(nxt, gated), 8, 8, (40, 80, 120))
    code, said = run_quiet(sweep)
    assert code == 1, said
    assert "flat colour" in said and "proves nothing" in said, said
    # the shots the table marks flat BY DESIGN stay report-only, not failures
    assert "FLAT PAIR" in said, said
    write_feature_dir(classic, (40, 80, 120))
    write_feature_dir(nxt, (40, 80, 120))

    # the adjudication package: the pair AND the picture, self-describing
    package = os.path.join(scratch, "adjudicate")
    write_png(os.path.join(nxt, reported), 8, 8, (120, 80, 120),
              stripe=(220, 40, 40))
    code, said = run_quiet(sweep + ["--adjudication-dir", package,
                                    "--adjudication-delta", "5"])
    assert code == 0, said
    stem = os.path.splitext(reported)[0]
    for suffix in ("-classic.png", "-next.png", "-diff.png"):
        assert os.path.exists(os.path.join(package, stem + suffix)), suffix
    assert decode_png(os.path.join(package, stem + "-diff.png"))[0] == 8
    # a shot inside the delta stays out of the package - it is a reading list,
    # not a copy of the capture directory
    assert not os.path.exists(
        os.path.join(package, os.path.splitext(gated)[0] + "-diff.png"))


if __name__ == "__main__":
    sys.exit(main())
