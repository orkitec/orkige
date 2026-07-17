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
               ground region in the hero's shadow is darker than the open
               ground (the receiver stage and the normal-map stage feed the
               same lighting stage);
             * emissive glows in the dark: after lights-out the mapped hero
               still shows bright emissive texels while the ground reads
               near-black; the flat hero's dark capture stays near-black.

  cutout   The alpha-test + two-sided demo (ORKIGE_DEMO_CUTOUT): a leaf quad
           with a cutout material over a lit ground; asserts the hole in the
           leaf shows the backdrop through (alpha test renders as cutout),
           the back-facing sibling quad still renders (two-sided), and the
           leaf's cast shadow has a LIT hole (the caster carries the cutout).

  accents  The runtime accent demo (ORKIGE_DEMO_ACCENTS): two instances of
           ONE material; asserts a tint+emissive-boost on instance A changes
           its pixels, leaves sibling B untouched, and that clearing the
           accents restores instance A exactly (toggle identity).

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
# sun from (+x, +z) so the shadow falls screen-right of the hero)
HERO_FACE = (0.42, 0.42, 0.58, 0.66)        # the hero cube's front face
GROUND_SHADOW = (0.60, 0.60, 0.70, 0.74)    # ground inside the cast shadow
GROUND_OPEN = (0.15, 0.64, 0.30, 0.74)      # open ground, same depth band


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
    # 2. the cast shadow composes on the normal-mapped receiver
    shadow_lum = region_mean(mapped, GROUND_SHADOW)
    open_lum = region_mean(mapped, GROUND_OPEN)
    check(failures, shadow_lum < 0.88 * open_lum,
          f"cast shadow darkens the normal-mapped ground (shadow "
          f"{shadow_lum:.1f} < 88% of open {open_lum:.1f})")
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


# cutout-rig regions: the two leaves' screen placement (front leaf around
# x 0.29..0.49, back-facing leaf around x 0.56..0.76, both y 0.30..0.64)
LEAF_RING = (0.30, 0.42, 0.335, 0.50)       # solid part of the front leaf
LEAF_HOLE = (0.375, 0.44, 0.405, 0.49)      # its central hole (backdrop shows)
BACK_RING = (0.57, 0.42, 0.60, 0.50)        # the back-facing leaf's solid part
BACK_HOLE = (0.63, 0.44, 0.66, 0.49)        # its hole
CUTOUT_OPEN = (0.10, 0.68, 0.20, 0.76)      # open lit ground, no shadow
SHADOW_SCAN = (0.26, 0.62, 0.54, 0.82)      # where the front leaf's shadow lands


def region_rgb(image, region, step=0.01):
    x0, y0, x1, y1 = region
    sums = [0.0, 0.0, 0.0]
    count = 0
    fy = y0
    while fy < y1:
        fx = x0
        while fx < x1:
            r, g, b = image.rgb(fx, fy)
            sums[0] += r
            sums[1] += g
            sums[2] += b
            count += 1
            fx += step
        fy += step
    return tuple(value / count for value in sums)


def leaf_green(rgb):
    """is the sample leaf-coloured (green-dominant)? The backdrop through the
    hole is the orange ground checker or the blue sky - never green-led."""
    r, g, b = rgb
    return g > r * 1.15 and g > b * 1.15


def leg_cutout(binary, out_dir):
    shot, _second = run_demo(
        binary, {"ORKIGE_DEMO_CUTOUT": "1"}, out_dir, "cutout", frames=90,
        expect_second=False)
    image = Image(shot)
    failures = []
    # 1. the cutout renders as a cutout: leaf ring solid green, the hole
    # showing the backdrop through (never green-led)
    ring = region_rgb(image, LEAF_RING)
    hole = region_rgb(image, LEAF_HOLE)
    check(failures, leaf_green(ring),
          f"the leaf's solid ring renders green (rgb {ring})")
    check(failures, not leaf_green(hole),
          f"the leaf's hole shows the backdrop through (rgb {hole})")
    # 2. two-sided: the BACK-facing sibling quad renders at all (single-sided
    # geometry - culled to nothing without the two-sided material)
    back_ring = region_rgb(image, BACK_RING)
    back_hole = region_rgb(image, BACK_HOLE)
    check(failures, leaf_green(back_ring),
          f"the back-facing leaf renders (two-sided; rgb {back_ring})")
    check(failures, not leaf_green(back_hole),
          f"the back-facing leaf keeps its hole (rgb {back_hole})")
    # 3. the CAST SHADOW is a cutout too: collect the dark (shadowed) samples
    # under the front leaf - their centroid must land in the shadow's LIT
    # hole (a filled-disc shadow puts its centroid on dark ground; measured
    # centre/open ratios: healthy ~1.0 both flavors, filled ~<=0.7)
    open_lum = region_mean(image, CUTOUT_OPEN)
    dark = []
    x0, y0, x1, y1 = SHADOW_SCAN
    fy = y0
    while fy < y1:
        fx = x0
        while fx < x1:
            if image.lum(fx, fy) < 0.7 * open_lum:
                dark.append((fx, fy))
            fx += 0.005
        fy += 0.005
    check(failures, len(dark) > 100,
          f"the leaf casts a shadow at all ({len(dark)} dark samples > 100)")
    if dark:
        cx = sum(p[0] for p in dark) / len(dark)
        cy = sum(p[1] for p in dark) / len(dark)
        centre = region_mean(image, (cx - 0.02, cy - 0.02, cx + 0.02, cy + 0.02))
        check(failures, centre > 0.85 * open_lum,
              f"the shadow has a LIT hole (ring centroid ({cx:.2f},{cy:.2f}) "
              f"luminance {centre:.1f} > 85% of open {open_lum:.1f})")
    return failures


# accent-rig regions: instance A (accented) left, instance B (the untouched
# sibling of the SAME material) right
ACCENT_A = (0.24, 0.40, 0.40, 0.70)
ACCENT_B = (0.60, 0.40, 0.76, 0.70)


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
    # 1. the accent changes instance A (tint + emissive boost visible)
    delta_a = region_diff(pre, accented, ACCENT_A)
    check(failures, delta_a > 8.0,
          f"tint + emissive boost change instance A (mean delta "
          f"{delta_a:.1f} > 8.0)")
    # 2. instance B - the SAME shared material - stays untouched
    delta_b = region_diff(pre, accented, ACCENT_B)
    check(failures, delta_b < 1.5,
          f"the sibling instance B stays untouched (mean delta "
          f"{delta_b:.2f} < 1.5)")
    # 3. toggle identity: clearing the accents restores A exactly
    delta_restore = region_diff(pre, restored, ACCENT_A)
    check(failures, delta_restore < 1.5,
          f"clearing the accents restores instance A exactly (mean delta "
          f"{delta_restore:.2f} < 1.5)")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="the hello_orkige app")
    parser.add_argument("--out", required=True, help="scratch dir for captures")
    parser.add_argument("--leg", required=True,
                        choices=["looks", "cutout", "accents"])
    args = parser.parse_args()
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
