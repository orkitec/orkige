#!/usr/bin/env python3
"""Pixel probes for the `.omat` material surface - the LOOK the triangle
counts cannot see (the water_looks_right lesson, applied to materials).

Legs (--leg):

  looks    The maps demo: drives ORKIGE_DEMO_MATLOOKS twice - once with the
           full demo material (albedo + normal + emissive maps) on the hero
           cube, once with the map-free flat sibling - and asserts on the
           frame-locked captures:
             * normal-mapped lit != flat-lit: the hero region of the mapped
               run differs measurably from the flat run (same geometry, same
               light, same albedo - the difference IS the maps);
             * the cast shadow composes on the normal-mapped receiver: the
               cast shadow is located ANALYTICALLY (the coherent dark region on
               the lit ground) and its mean is darker than the open ground (the
               receiver stage and the normal-map stage feed the same lighting
               stage) - content-anchored, so it follows the shadow when a
               display clamps the window aspect;
             * emissive glows in the dark: after lights-out the mapped hero
               still shows bright emissive texels while the ground reads
               near-black; the flat hero's dark capture stays near-black.

  cutout   The alpha-test + two-sided demo (ORKIGE_DEMO_CUTOUT): a leaf quad
           with a cutout material over a lit ground; asserts the hole in the
           leaf shows the backdrop through (alpha test renders as cutout),
           the back-facing sibling quad still renders (two-sided), and the
           leaf's cast shadow has a LIT hole (the caster carries the cutout).
           The leaves are located ANALYTICALLY (green-dominant connected
           blobs) and the ring/hole/shadow sample regions derived from each
           blob's measured geometry - so the probe follows the content when a
           display clamps the window aspect (the hosted-CI 1024-wide finding).
           `--image <png>` runs the cutout checks against a saved frame with
           no demo run (a clean debugging affordance, e.g. a CI screenshot).

  accents  The runtime accent demo (ORKIGE_DEMO_ACCENTS): two instances of
           ONE material; asserts a tint+emissive-boost on instance A changes
           its pixels, leaves sibling B untouched, and that clearing the
           accents restores instance A exactly (toggle identity). The two cube
           instances are located ANALYTICALLY (warm blobs over the blue
           backdrop) in the untouched pre-accent frame - A is the left, B the
           right - so the sample boxes follow the content under an aspect clamp.

Pure stdlib (zlib PNG decode). Runs per flavor.
"""

import argparse
import os
import struct
import subprocess
import sys
import zlib


def decode_png(path):
    """Minimal PNG decoder: 8-bit RGB/RGBA/gray, non-interlaced -> (w,h,ch,bytes)."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    width = height = colour_type = None
    idat = bytearray()
    while pos < len(data):
        length, chunk_type = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            (width, height, bit_depth, colour_type,
             _c, _f, interlace) = struct.unpack(">IIBBBBB", chunk)
            if bit_depth != 8 or colour_type not in (0, 2, 6) or interlace != 0:
                raise ValueError(f"{path}: unsupported PNG")
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
        ftype = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                up_left = previous[i - channels] if i >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                predictor = left if (pa <= pb and pa <= pc) else (up if pb <= pc else up_left)
                line[i] = (line[i] + predictor) & 0xFF
        elif ftype != 0:
            raise ValueError(f"{path}: unknown PNG filter {ftype}")
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, out


class Image:
    def __init__(self, path):
        self.w, self.h, self.ch, self.px = decode_png(path)

    def rgb(self, fx, fy):
        x = min(self.w - 1, int(fx * self.w))
        y = min(self.h - 1, int(fy * self.h))
        base = (y * self.w + x) * self.ch
        return (self.px[base], self.px[base + 1], self.px[base + 2])

    def rgb_px(self, x, y):
        base = (y * self.w + x) * self.ch
        return (self.px[base], self.px[base + 1], self.px[base + 2])

    def lum(self, fx, fy):
        r, g, b = self.rgb(fx, fy)
        return (r + g + b) / 3.0


def region_samples(image, region, step=0.01):
    """luminance samples over a fractional region (x0, y0, x1, y1)"""
    x0, y0, x1, y1 = region
    out = []
    fy = y0
    while fy < y1:
        fx = x0
        while fx < x1:
            out.append(image.lum(fx, fy))
            fx += step
        fy += step
    return out


def region_mean(image, region):
    samples = region_samples(image, region)
    return sum(samples) / len(samples)


def region_max(image, region):
    return max(region_samples(image, region))


def region_diff(image_a, image_b, region, step=0.01):
    """mean absolute per-sample luminance difference over one region of two
    same-size captures"""
    a = region_samples(image_a, region, step)
    b = region_samples(image_b, region, step)
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def run_demo(binary, env_extra, out_dir, tag, frames=100, timeout=180,
             expect_second=True):
    """run the demo once; returns (main_shot_path, second_shot_path) - the
    second capture only exists in legs whose demo schedules one"""
    os.makedirs(out_dir, exist_ok=True)
    main_shot = os.path.join(out_dir, f"mat_{tag}.png")
    second_shot = os.path.join(out_dir, f"mat_{tag}_2.png")
    env = dict(os.environ, ORKIGE_DEMO_FRAMES=str(frames),
               ORKIGE_AUTOMATED_RUN="1",
               ORKIGE_DEMO_SCREENSHOT=main_shot,
               ORKIGE_DEMO_SCREENSHOT2=second_shot,
               **env_extra)
    result = subprocess.run([binary], env=env, timeout=timeout,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    if result.returncode != 0:
        raise RuntimeError(f"demo run '{tag}' exited {result.returncode}")
    for path in (main_shot,) + ((second_shot,) if expect_second else ()):
        if not os.path.exists(path):
            raise RuntimeError(f"demo run '{tag}' captured no frame at {path}")
    return main_shot, second_shot


def check(failures, ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


# probe regions, fractions of the window (calibrated against the committed
# rig: camera (0, 1.9, 5.6) looking at (0, -0.7, 0), hero cube at the centre,
# sun from (+x, +z) so the shadow falls screen-right of the hero). The cast
# shadow is NOT a fixed rect - it is located by content (locate_ground_shadow).
HERO_FACE = (0.42, 0.42, 0.58, 0.66)        # the hero cube's front face
GROUND_OPEN = (0.15, 0.64, 0.30, 0.74)      # open ground, same depth band

# the receiver ground band (below the sky and the cube top); the cast shadow is
# the largest connected region here darker than a fraction of the band's lit
# median - small scattered dark checker texels never reach the min-area bar,
# and the soft next-flavour shadow (a mild darkening) still forms one blob
GROUND_BAND = (0.50, 0.92)
SHADOW_DARK_FRACTION = 0.86     # a shadow cell is below this fraction of median
SHADOW_MIN_AREA_FRAC = 0.02     # the cast shadow is a large coherent region


def locate_ground_shadow(image):
    """content-anchored cast shadow: the largest dark blob on the lit ground
    band, returned with its mean luminance and an open-ground reference (the
    lit band cells outside it). Returns None when no shadow-sized dark region
    is present, so the caller can fail honestly instead of sampling a fixed
    rect that layout drift has emptied."""
    band = (0.02, GROUND_BAND[0], 0.98, GROUND_BAND[1])
    lums = region_samples(image, band)
    lums.sort()
    median = lums[len(lums) // 2]
    threshold = SHADOW_DARK_FRACTION * median
    blobs = find_blobs(image, lambda r, g, b: (r + g + b) / 3.0 < threshold,
                       min_area_frac=SHADOW_MIN_AREA_FRAC, y_band=GROUND_BAND)
    if not blobs:
        return None
    shadow = blobs[0]
    fx0, fy0 = shadow["x0"] / image.w, shadow["y0"] / image.h
    fx1, fy1 = shadow["x1"] / image.w, shadow["y1"] / image.h
    # open ground: the LIT band cells (>= the median) outside the shadow box
    opens = []
    fy = GROUND_BAND[0]
    while fy < GROUND_BAND[1]:
        fx = 0.02
        while fx < 0.98:
            lum = image.lum(fx, fy)
            if lum >= median and not (fx0 <= fx <= fx1 and fy0 <= fy <= fy1):
                opens.append(lum)
            fx += 0.01
        fy += 0.01
    open_mean = sum(opens) / len(opens) if opens else median
    return {"mean": shadow["lum_mean"], "open": open_mean}


def leg_looks(binary, out_dir):
    lit_mapped, dark_mapped = run_demo(
        binary, {"ORKIGE_DEMO_MATLOOKS": "1"}, out_dir, "looks_mapped")
    lit_flat, dark_flat = run_demo(
        binary, {"ORKIGE_DEMO_MATLOOKS": "flat"}, out_dir, "looks_flat")

    mapped = Image(lit_mapped)
    flat = Image(lit_flat)
    dark = Image(dark_mapped)
    dark_f = Image(dark_flat)

    failures = []
    # 1. the maps change the LIT render: same geometry/light/albedo, only the
    # normal + emissive maps differ between the two runs
    maps_delta = region_diff(mapped, flat, HERO_FACE)
    check(failures, maps_delta > 6.0,
          f"normal/emissive maps change the lit hero (mean delta "
          f"{maps_delta:.1f} > 6.0)")
    # 2. the cast shadow composes on the normal-mapped receiver - located by
    # content (the coherent dark region on the lit ground), so it follows the
    # shadow under an aspect clamp instead of sampling a fixed rect
    shadow = locate_ground_shadow(mapped)
    check(failures, shadow is not None,
          "the cast shadow forms a coherent dark region on the receiver")
    if shadow is not None:
        check(failures, shadow["mean"] < 0.88 * shadow["open"],
              f"cast shadow darkens the normal-mapped ground (shadow "
              f"{shadow['mean']:.1f} < 88% of open {shadow['open']:.1f})")
    # 3. emissive survives lights-out on the mapped hero only
    glow = region_max(dark, HERO_FACE)
    glow_flat = region_max(dark_f, HERO_FACE)
    ground_dark = region_mean(dark, GROUND_OPEN)
    check(failures, glow > 60.0,
          f"emissive texels glow in the dark (max {glow:.1f} > 60)")
    check(failures, glow_flat < 32.0,
          f"the flat sibling shows no glow (max {glow_flat:.1f} < 32)")
    check(failures, ground_dark < 30.0,
          f"the emission-free ground reads dark (mean {ground_dark:.1f} < 30)")
    return failures


def leaf_green(rgb):
    """is the sample leaf-coloured (green-dominant)? The backdrop through the
    hole is the orange ground checker or the blue sky - never green-led."""
    r, g, b = rgb
    return g > r * 1.15 and g > b * 1.15


def _blob_green(r, g, b):
    """membership test for the leaf-blob detector: green-led (the same 1.15 bar
    the leaf_green ASSERTION uses) over a small brightness floor. The floor only
    rejects near-black noise - it must stay well below the back-facing leaf,
    which classic lights very dimly (its unlit side reads ~(13,26,13) green);
    the green-dominance ratio, not brightness, is what isolates the leaves from
    the orange ground / blue sky / neutral shadow."""
    return g > r * 1.15 and g > b * 1.15 and g > 10


def find_blobs(image, member, min_area_frac=0.005, y_band=None):
    """Locate connected same-property blobs ANALYTICALLY rather than at fixed
    screen rects, so a probe follows the content when the window aspect shifts
    (a hosted CI runner clamps 1280x720 to its 1024-wide display). `member(r,
    g, b) -> bool` selects the feature pixels; the scan runs on a stride-reduced
    grid (resolution-independent, allocation-light). `y_band=(fy0, fy1)`
    restricts the scanned rows to a fractional vertical band (None = the whole
    frame). Each blob reports its pixel bounding box, centroid, area and
    lum_mean (mean luminance of its member cells). Returns blobs larger than
    `min_area_frac` of the frame, sorted largest-first."""
    from collections import deque
    w, h = image.w, image.h
    stride = max(1, min(w, h) // 360)
    gw = (w + stride - 1) // stride
    gh = (h + stride - 1) // stride
    gy0 = 0 if y_band is None else max(0, int(y_band[0] * gh))
    gy1 = gh if y_band is None else min(gh, int(y_band[1] * gh))
    mask = bytearray(gw * gh)
    lumg = [0.0] * (gw * gh)
    for gy in range(gy0, gy1):
        y = min(gy * stride, h - 1)
        row = gy * gw
        for gx in range(gw):
            r, g, b = image.rgb_px(min(gx * stride, w - 1), y)
            lumg[row + gx] = (r + g + b) / 3.0
            if member(r, g, b):
                mask[row + gx] = 1
    seen = bytearray(gw * gh)
    blobs = []
    for start in range(gw * gh):
        if not mask[start] or seen[start]:
            continue
        queue = deque([start])
        seen[start] = 1
        sx = sy = count = 0
        lum_sum = 0.0
        minx = maxx = start % gw
        miny = maxy = start // gw
        while queue:
            cell = queue.popleft()
            cy, cx = divmod(cell, gw)
            sx += cx
            sy += cy
            count += 1
            lum_sum += lumg[cell]
            if cx < minx:
                minx = cx
            if cx > maxx:
                maxx = cx
            if cy < miny:
                miny = cy
            if cy > maxy:
                maxy = cy
            for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                if 0 <= nx < gw and 0 <= ny < gh:
                    k = ny * gw + nx
                    if mask[k] and not seen[k]:
                        seen[k] = 1
                        queue.append(k)
        blobs.append({
            "area": count * stride * stride,
            "x0": minx * stride, "x1": maxx * stride,
            "y0": miny * stride, "y1": maxy * stride,
            "cx": (sx / count) * stride, "cy": (sy / count) * stride,
            "lum_mean": lum_sum / count,
        })
    min_area = min_area_frac * w * h
    blobs = [blob for blob in blobs if blob["area"] >= min_area]
    blobs.sort(key=lambda blob: blob["area"], reverse=True)
    return blobs


def find_green_blobs(image, min_area_frac=0.005):
    """the leaf-blob detector: green-dominant connected regions (see
    find_blobs). The leaves are the cutout leg's content anchor."""
    return find_blobs(image, _blob_green, min_area_frac)


def box_rgb(image, x0, y0, x1, y1, step=2):
    """mean RGB over a pixel-space box (clamped to the frame)"""
    x0 = max(0, int(x0)); y0 = max(0, int(y0))
    x1 = min(image.w, int(x1)); y1 = min(image.h, int(y1))
    sums = [0.0, 0.0, 0.0]
    count = 0
    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            r, g, b = image.rgb_px(x, y)
            sums[0] += r; sums[1] += g; sums[2] += b
            count += 1
    if count == 0:
        return (0.0, 0.0, 0.0)
    return tuple(value / count for value in sums)


def box_lum(image, x0, y0, x1, y1, step=2):
    r, g, b = box_rgb(image, x0, y0, x1, y1, step)
    return (r + g + b) / 3.0


def lum_px(image, x, y):
    r, g, b = image.rgb_px(int(x), int(y))
    return (r + g + b) / 3.0


def _leaf_ring_box(blob):
    """the solid green ring: a box on the ring's left arm (offset in from the
    bounding-box edge toward the hole, at the vertical centre)"""
    w = blob["x1"] - blob["x0"]
    h = blob["y1"] - blob["y0"]
    return (blob["x0"] + 0.10 * w, blob["cy"] - 0.06 * h,
            blob["x0"] + 0.20 * w, blob["cy"] + 0.06 * h)


def _leaf_hole_box(blob):
    """the central hole: the interior non-green area at the blob's centroid (a
    FILLED hole reads green here and fails the assertion loudly)"""
    w = blob["x1"] - blob["x0"]
    h = blob["y1"] - blob["y0"]
    return (blob["cx"] - 0.06 * w, blob["cy"] - 0.06 * h,
            blob["cx"] + 0.06 * w, blob["cy"] + 0.06 * h)


def analyze_cutout(image):
    """the content-anchored cutout checks over a decoded frame (shared by the
    live leg and the --image debugging affordance). Locates the two leaf blobs,
    derives each one's ring/hole sample regions from its measured geometry, and
    keeps the SAME assertions: ring green, hole not green, a second blob present
    (two-sided), the back hole preserved, and a cutout shadow with a LIT hole."""
    failures = []
    blobs = find_green_blobs(image)
    # degenerate/two-sided guard: two leaf blobs must be present. A culled back
    # face (two-sided broken) or a genuinely broken render lands here and fails
    # loudly - the probe never passes vacuously on a leaf-less frame.
    check(failures, len(blobs) >= 2,
          f"the cutout render shows two leaf blobs (found {len(blobs)}) - a "
          f"culled back face (two-sided) or a degenerate render fails here")
    if len(blobs) < 2:
        return failures
    front, back = sorted(blobs[:2], key=lambda blob: blob["cx"])
    # 1./2. each leaf renders as a cutout: solid green ring, backdrop through
    # the hole (never green-led). The second (right) blob existing IS two-sided;
    # its hole staying non-green IS the preserved back hole.
    for name, blob in (("front", front), ("back", back)):
        ring = box_rgb(image, *_leaf_ring_box(blob))
        hole = box_rgb(image, *_leaf_hole_box(blob))
        check(failures, leaf_green(ring),
              f"the {name} leaf's solid ring renders green "
              f"(rgb {tuple(round(v) for v in ring)})")
        check(failures, not leaf_green(hole),
              f"the {name} leaf's hole shows the backdrop through "
              f"(rgb {tuple(round(v) for v in hole)})")
    # 3. the CAST SHADOW is a cutout too: scan the ground BELOW the front leaf
    # (a box centred on the leaf's measured centroid, sized in leaf-heights so
    # it follows the content and stays clear of the sibling's shadow) for the
    # dark shadowed samples; the centroid of the shadow RING's dark pixels must
    # land in its LIT hole. The scan starts above the leaf's bottom to catch the
    # ring's top arc, so the centroid stays balanced on the ring centre (a
    # lighting gradient thins one arc otherwise). A filled-disc shadow (a caster
    # that lost the cutout) puts its centroid on dark ground -> the lit-hole
    # ratio drops and fails; a healthy cutout reads ~1.0 on both flavors.
    fw = front["x1"] - front["x0"]
    fh = front["y1"] - front["y0"]
    fcx, fcy = front["cx"], front["cy"]
    sx0 = max(0, int(fcx - 0.70 * fw))
    sx1 = min(image.w, int(fcx + 0.70 * fw))
    sy0 = max(0, int(fcy + 0.43 * fh))
    sy1 = min(image.h, int(fcy + 1.07 * fh))
    # open lit ground beside the shadow at the same depth band
    ox0 = int(0.02 * image.w)
    ox1 = max(ox0 + 20, sx0 - 10)
    open_lum = box_lum(image, ox0, int(fcy + 0.6 * fh), ox1, int(fcy + 1.0 * fh))
    dark = []
    total = 0
    for y in range(sy0, sy1, 2):
        for x in range(sx0, sx1, 2):
            total += 1
            if lum_px(image, x, y) < 0.7 * open_lum:
                dark.append((x, y))
    dark_frac = (len(dark) / total) if total else 0.0
    check(failures, dark_frac > 0.03,
          f"the leaf casts a shadow at all ({len(dark)} dark samples, "
          f"{dark_frac * 100:.0f}% of the scan > 3%)")
    if dark:
        cx = sum(p[0] for p in dark) / len(dark)
        cy = sum(p[1] for p in dark) / len(dark)
        centre = box_lum(image, cx - 8, cy - 8, cx + 8, cy + 8, step=1)
        # the lit hole must clearly outshine a filled-disc shadow (which puts its
        # dark centroid on shadowed ground, ~0.5 of open). The floor is 0.70, not
        # a tighter 0.85: the classic hemisphere-ambient fill softens its shadow
        # ring (the ambient is unshadowed, matching next), thinning the dark ring
        # so the centroid samples a touch of the ring edge - classic reads ~0.78
        # of open (open ground itself matches next within ~3%), next ~1.03. 0.70
        # keeps the filled-disc regression caught with margin on both flavors.
        check(failures, centre > 0.70 * open_lum,
              f"the shadow has a LIT hole (ring centroid ({cx:.0f},{cy:.0f}) "
              f"luminance {centre:.1f} > 70% of open {open_lum:.1f})")
    return failures


def leg_cutout(binary, out_dir):
    shot, _second = run_demo(
        binary, {"ORKIGE_DEMO_CUTOUT": "1"}, out_dir, "cutout", frames=90,
        expect_second=False)
    return analyze_cutout(Image(shot))


def _cube_warm(r, g, b):
    """membership test for the accent-cube blob detector: warm and r-dominant
    (the orange/cream checker cube) over the pure-blue backdrop. The blue sky
    is b-dominant with r ~ 0, so it never passes; the floor rejects near-black
    noise. Both cube instances share this look in the untouched pre frame."""
    return r >= 45 and r > b * 1.15 and r >= g


def _blob_inset_region(image, blob, inset=0.22):
    """a fractional (x0, y0, x1, y1) box inset into a blob's bounding box, so
    the sample stays on the feature surface and clear of its edges/backdrop"""
    w = blob["x1"] - blob["x0"]
    h = blob["y1"] - blob["y0"]
    return ((blob["x0"] + inset * w) / image.w,
            (blob["y0"] + inset * h) / image.h,
            (blob["x1"] - inset * w) / image.w,
            (blob["y1"] - inset * h) / image.h)


def leg_accents(binary, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    restored_shot = os.path.join(out_dir, "mat_accents_restored.png")
    accented_shot, pre_shot = run_demo(
        binary, {"ORKIGE_DEMO_ACCENTS": "1",
                 "ORKIGE_DEMO_SCREENSHOT3": restored_shot},
        out_dir, "accents")
    if not os.path.exists(restored_shot):
        raise RuntimeError(f"no post-restore capture at {restored_shot}")
    pre = Image(pre_shot)
    accented = Image(accented_shot)
    restored = Image(restored_shot)

    failures = []
    # locate the two cube instances by content in the untouched PRE frame (both
    # are the SAME warm cube there; the accent has not landed yet). A is the
    # left instance, B the right; the sample boxes are derived from each blob's
    # measured geometry, so they follow the content under an aspect clamp.
    cubes = find_blobs(pre, _cube_warm, min_area_frac=0.02)
    check(failures, len(cubes) >= 2,
          f"the accent rig shows two cube instances (found {len(cubes)}) - a "
          f"degenerate render fails here rather than passing vacuously")
    if len(cubes) < 2:
        return failures
    left, right = sorted(cubes[:2], key=lambda blob: blob["cx"])
    region_a = _blob_inset_region(pre, left)
    region_b = _blob_inset_region(pre, right)
    # 1. the accent changes instance A (tint + emissive boost visible)
    delta_a = region_diff(pre, accented, region_a)
    check(failures, delta_a > 8.0,
          f"tint + emissive boost change instance A (mean delta "
          f"{delta_a:.1f} > 8.0)")
    # 2. instance B - the SAME shared material - stays untouched
    delta_b = region_diff(pre, accented, region_b)
    check(failures, delta_b < 1.5,
          f"the sibling instance B stays untouched (mean delta "
          f"{delta_b:.2f} < 1.5)")
    # 3. toggle identity: clearing the accents restores A exactly
    delta_restore = region_diff(pre, restored, region_a)
    check(failures, delta_restore < 1.5,
          f"clearing the accents restores instance A exactly (mean delta "
          f"{delta_restore:.2f} < 1.5)")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="the hello_orkige app")
    parser.add_argument("--out", help="scratch dir for captures")
    parser.add_argument("--leg", required=True,
                        choices=["looks", "cutout", "accents"])
    parser.add_argument("--image", help="analyze a saved PNG instead of running "
                        "the demo (cutout leg only; a debugging affordance)")
    args = parser.parse_args()

    # --image: exercise the probe logic against a saved frame (no demo run) -
    # the way the CI screenshot is checked. Cutout leg only.
    if args.image:
        if args.leg != "cutout":
            print("--image is only supported for the cutout leg")
            return 2
        failures = analyze_cutout(Image(args.image))
        if failures:
            print(f"{len(failures)} material probe check(s) failed")
            return 1
        print("material probe leg 'cutout' (--image): all checks passed")
        return 0

    if not args.binary or not args.out:
        print("--binary and --out are required unless --image is given")
        return 2
    if not os.path.exists(args.binary):
        print(f"SKIP: demo app not built: {args.binary}")
        return 77

    try:
        failures = {"looks": leg_looks,
                    "cutout": leg_cutout,
                    "accents": leg_accents}[args.leg](args.binary, args.out)
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}")
        return 1
    if failures:
        print(f"{len(failures)} material probe check(s) failed")
        return 1
    print(f"material probe leg '{args.leg}': all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
