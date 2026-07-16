#!/usr/bin/env python3
"""Assert the water demo LOOKS like water, not just that it renders.

The old demo_water probe only checked a triangle count + a scroll clock, so a
near-black flat slab (PBS water lit by nothing, or a broken datablock) passed as
"rendering" - the fog-leg lesson. This drives ORKIGE_DEMO_WATER, captures the
water at two frames (40 + 60), samples the water band (the lower rows, left and
right of the centre cubes) and asserts:

  * COLOUR VARIATION across the band - a uniform/flat slab fails (a lit rippling
    surface has a sun glint + ripple relief, so its luminance spread is wide);
  * NOT near-black - the surface is actually lit/coloured;
  * MOTION between the two frames - the ripple scrolled (a static surface fails).

Pure stdlib (zlib PNG decode), reusing the parity driver's decoder. Runs per
flavor (the demo carries a directional sun so PBS water has something to light).
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


def _lum(pixels, channels, x, y, width):
    base = (y * width + x) * channels
    return (pixels[base] + pixels[base + 1] + pixels[base + 2]) / 3.0


def water_samples(width, height, channels, pixels):
    """luminance over the whole water band (ripples under the horizon + the
    centre-bottom), for the not-black + motion checks."""
    lums = []
    for fy in [0.46, 0.50, 0.54, 0.58, 0.84, 0.88, 0.92, 0.96]:
        for fx in [0.04, 0.10, 0.16, 0.22, 0.42, 0.50, 0.58, 0.80, 0.88, 0.95]:
            lums.append(_lum(pixels, channels,
                             min(width - 1, int(fx * width)),
                             min(height - 1, int(fy * height)), width))
    return lums


def glint_contrast(width, height, channels, pixels):
    """the centre-bottom region where the sun reflection lands: its (max - mean)
    is the SPECULAR GLINT contrast. Lit water with a fresnel reflection has a
    bright spot here (high contrast); an unlit/flat slab is uniform (~0). This is
    what caught the near-black no-sun slab that the triangle count passed."""
    vals = []
    for yi in range(int(0.82 * height), int(0.97 * height), 2):
        for xi in range(int(0.34 * width), int(0.62 * width), 3):
            vals.append(_lum(pixels, channels, xi, yi, width))
    mean = sum(vals) / len(vals)
    return max(vals) - mean


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="the hello_orkige app")
    parser.add_argument("--out", required=True, help="scratch dir for the two frames")
    args = parser.parse_args()
    if not os.path.exists(args.binary):
        print(f"SKIP: demo app not built: {args.binary}")
        return 77

    os.makedirs(args.out, exist_ok=True)
    early = os.path.join(args.out, "water_frame40.png")
    late = os.path.join(args.out, "water_frame60.png")
    env = dict(os.environ, ORKIGE_DEMO_WATER="1", ORKIGE_DEMO_FRAMES="70",
               ORKIGE_AUTOMATED_RUN="1",
               ORKIGE_DEMO_SCREENSHOT=late, ORKIGE_DEMO_SCREENSHOT2=early)
    try:
        result = subprocess.run([args.binary], env=env, timeout=120,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        print("FAIL: demo_water hung")
        return 1
    if result.returncode != 0:
        print(f"FAIL: demo_water exited {result.returncode}")
        return 1
    for path in (early, late):
        if not os.path.exists(path):
            print(f"FAIL: no water frame captured at {path}")
            return 1

    w, h, ch, late_px = decode_png(late)
    _, _, _, early_px = decode_png(early)
    late_lum = water_samples(w, h, ch, late_px)
    early_lum = water_samples(w, h, ch, early_px)

    mean_lum = sum(late_lum) / len(late_lum)
    motion = sum(abs(a - b) for a, b in zip(late_lum, early_lum)) / len(late_lum)
    glint = glint_contrast(w, h, ch, late_px)

    # thresholds calibrated (measured) against the unlit near-flat slab vs the
    # lit, sun-glinted, scrolling water: unlit glint-region contrast ~0.2, the
    # earlier subdued material ~11, the tuned lively material ~97 (deterministic
    # across runs - the captures are frame-locked); unlit mean ~0 when truly
    # black, lit mean ~71; motion ~5.8 when scrolling. MIN_GLINT sits between
    # the subdued and lively responses so a regression to the dull read fails,
    # with ~5x margin below the measured value.
    NOT_BLACK = 8.0        # mean luminance (0..255): a truly black slab fails
    MIN_MOTION = 1.2       # the ripple must scroll frame-to-frame
    MIN_GLINT = 20.0       # a LIVELY specular sun glint (lit ~97; subdued ~11)

    print(f"water probe: mean_luminance={mean_lum:.1f} (>{NOT_BLACK}), "
          f"motion={motion:.2f} (>{MIN_MOTION}), "
          f"glint_contrast={glint:.1f} (>{MIN_GLINT})")

    failures = []
    if mean_lum <= NOT_BLACK:
        failures.append(f"water is near-black (mean {mean_lum:.1f}) - unlit slab")
    if motion <= MIN_MOTION:
        failures.append(f"water is static (motion {motion:.2f}) - ripple not scrolling")
    if glint <= MIN_GLINT:
        failures.append(f"water has no specular glint (contrast {glint:.1f}) - "
                        "unlit / no fresnel reflection (the near-black-slab bug)")
    if failures:
        for f in failures:
            print(f"FAIL: {f}")
        return 1
    print("water_looks_right: lit (with a sun glint), varied and moving")
    return 0


if __name__ == "__main__":
    sys.exit(main())
