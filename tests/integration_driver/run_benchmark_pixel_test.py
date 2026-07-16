#!/usr/bin/env python3
"""Assert the benchmark vista LOOKS right, not just that it runs.

The results-artifact test (run_benchmark_test.py) proves the tour traverses;
it cannot see a scene that renders WRONG. This drives the player over the
vista opening (the first scene of projects/benchmark), captures a frame and
asserts two measured discriminators that separate a correct frame from the
two silent failure classes a backend can produce:

  * HUD GLYPHS PRESENT - the director's title/info labels are per-frame-dirty
    gui content (the fps line rewrites its batch every frame), so a 2D
    compositing regression that only hits per-frame-rebuilt batches erases
    them while static HUDs elsewhere keep passing. The title row must show a
    substantial count of near-white glyph pixels.
  * DAYLIT TERRAIN - the vista's tiling ground under the atmosphere-linked
    morning sun reads green over blue. A sun-linkage regression (the sun
    sampled below the horizon) floods the frame with night-blue light:
    green collapses to ~0 while blue dominates. Requires mean green above a
    floor AND green > blue over the terrain band.

Reference readings (1280x720, frame 150): correct frame hudBright ~3200,
terrain mean (r 21, g 22, b 13); the broken frame hudBright 0, terrain
mean (r 0, g 0, b 72). The thresholds sit far from both.

Pure stdlib (zlib PNG decode, the parity driver's decoder). Runs per flavor.

Exit codes: 0 pass, 1 fail.
"""

import argparse
import os
import struct
import subprocess
import sys
import zlib

HUD_BRIGHT_MIN = 200        # near-white glyph pixels in the title block
TERRAIN_GREEN_MIN = 5.0     # mean green floor over the terrain band (0..255)


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
                predictor = left if (pa <= pb and pa <= pc) else \
                    (up if pb <= pc else up_left)
                line[i] = (line[i] + predictor) & 0xFF
        elif ftype != 0:
            raise ValueError(f"{path}: unknown PNG filter {ftype}")
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, out


def pixel(pixels, channels, width, x, y):
    base = (y * width + x) * channels
    return pixels[base], pixels[base + 1], pixels[base + 2]


def hud_bright_count(width, height, channels, pixels):
    """near-white pixels over the HUD title block (the labels sit at fixed
    pixel offsets from the top-left, font-sized - the block bounds them with
    headroom on any window size the player opens)."""
    count = 0
    for y in range(8, min(64, height)):
        for x in range(8, min(360, width)):
            r, g, b = pixel(pixels, channels, width, x, y)
            if r >= 220 and g >= 220 and b >= 220:
                count += 1
    return count


def terrain_band_means(width, height, channels, pixels):
    """mean r/g/b over a spread of the lower terrain rows (below every cube,
    inside the ground plane on both flavors' framing)."""
    rs = gs = bs = 0
    samples = 0
    for fy in (0.75, 0.80, 0.85, 0.90, 0.95):
        for fx in (0.05, 0.15, 0.25, 0.35, 0.5, 0.65, 0.75, 0.85, 0.95):
            x = min(width - 1, int(fx * width))
            y = min(height - 1, int(fy * height))
            r, g, b = pixel(pixels, channels, width, x, y)
            rs += r
            gs += g
            bs += b
            samples += 1
    return rs / samples, gs / samples, bs / samples


def log(msg):
    print("run_benchmark_pixel_test: " + msg, flush=True)


def fail(msg):
    print("run_benchmark_pixel_test: FAILED - " + msg, flush=True)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True, help="scratch dir for the capture")
    parser.add_argument("--frames", type=int, default=150,
                        help="capture frame (morning sun, HUD settled)")
    args = parser.parse_args()

    os.makedirs(args.dir, exist_ok=True)
    shot = os.path.join(args.dir, "vista_frame.png")
    if os.path.exists(shot):
        os.unlink(shot)

    env = dict(os.environ)
    env.update({
        "ORKIGE_DEMO_FRAMES": str(args.frames),
        "ORKIGE_DEMO_SCREENSHOT": shot,
        # keep the progression/save files out of the user dir
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": args.dir,
    })
    cmd = [args.player, "--project",
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

    width, height, channels, pixels = decode_png(shot)
    bright = hud_bright_count(width, height, channels, pixels)
    red, green, blue = terrain_band_means(width, height, channels, pixels)
    log("hud bright pixels %d (want >= %d); terrain band mean "
        "r=%.1f g=%.1f b=%.1f (want g >= %.1f and g > b)"
        % (bright, HUD_BRIGHT_MIN, red, green, blue, TERRAIN_GREEN_MIN))

    if bright < HUD_BRIGHT_MIN:
        fail("HUD title glyphs absent (bright=%d < %d) - per-frame-dirty gui "
             "batches are not reaching the frame" % (bright, HUD_BRIGHT_MIN))
    if green < TERRAIN_GREEN_MIN:
        fail("terrain band is not daylit (mean green %.1f < %.1f)"
             % (green, TERRAIN_GREEN_MIN))
    if green <= blue:
        fail("terrain band reads blue over green (g %.1f <= b %.1f) - the "
             "sun/atmosphere linkage is lighting the scene from below the "
             "horizon" % (green, blue))
    log("OK")


if __name__ == "__main__":
    main()
