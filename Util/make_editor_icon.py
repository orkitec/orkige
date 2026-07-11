#!/usr/bin/env python3
"""Generate the Orkige editor's macOS app icon as an .iconset directory.

Renders the icon procedurally (stdlib zlib/struct only, same policy as
make_gui_atlas.py): the roller ball - the engine's first modern game -
as an orange sphere with a star, on a dark squircle matching the macOS
Big Sur+ icon grid (824px squircle on a 1024px canvas).

Usage:  make_editor_icon.py <output.iconset>

The output directory is filled with the ten icon_NxN[@2x].png entries
`iconutil -c icns` expects; CMake runs iconutil afterwards (build-time
artifact, nothing is checked in).
"""

import math
import os
import struct
import sys
import zlib

BASE = 1024                     # master render size
SQUIRCLE_HALF = 412.0           # 824px squircle (Apple's icon grid)
SQUIRCLE_POWER = 5.0            # superellipse exponent
FEATHER = 1.6                   # anti-alias feather in pixels

# palette (sRGB)
BG_TOP = (44, 52, 71)
BG_BOTTOM = (18, 22, 32)
BALL_LIGHT = (255, 176, 102)
BALL_MID = (235, 106, 44)
BALL_RIM = (158, 58, 22)
STAR_COLOUR = (255, 210, 63)
STAR_RIM = (196, 148, 22)

BALL_CENTRE = (472.0, 560.0)
BALL_RADIUS = 240.0
STAR_CENTRE = (700.0, 330.0)
STAR_OUTER = 108.0
STAR_INNER = 44.0
STAR_TILT = -0.22               # radians


def smoothstep(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def lerp3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t)


def star_points():
    points = []
    for i in range(10):
        angle = STAR_TILT - math.pi / 2.0 + i * math.pi / 5.0
        radius = STAR_OUTER if i % 2 == 0 else STAR_INNER
        points.append((STAR_CENTRE[0] + radius * math.cos(angle),
                       STAR_CENTRE[1] + radius * math.sin(angle)))
    return points


def polygon_signed_distance(px, py, points):
    """negative inside, positive outside (even-odd winding)."""
    inside = False
    min_dist_sq = float("inf")
    count = len(points)
    j = count - 1
    for i in range(count):
        xi, yi = points[i]
        xj, yj = points[j]
        if (yi > py) != (yj > py):
            if px < (xj - xi) * (py - yi) / (yj - yi) + xi:
                inside = not inside
        # distance to segment i-j
        dx, dy = xj - xi, yj - yi
        length_sq = dx * dx + dy * dy
        t = 0.0 if length_sq == 0.0 else max(
            0.0, min(1.0, ((px - xi) * dx + (py - yi) * dy) / length_sq))
        ex, ey = xi + t * dx - px, yi + t * dy - py
        min_dist_sq = min(min_dist_sq, ex * ex + ey * ey)
        j = i
    dist = math.sqrt(min_dist_sq)
    return -dist if inside else dist


def render_base():
    """render the 1024px master as a straight-alpha RGBA bytearray."""
    pixels = bytearray(BASE * BASE * 4)
    star = star_points()
    star_min_x = min(p[0] for p in star) - 4.0
    star_max_x = max(p[0] for p in star) + 4.0
    star_min_y = min(p[1] for p in star) - 4.0
    star_max_y = max(p[1] for p in star) + 4.0
    half = BASE / 2.0
    for y in range(BASE):
        py = y + 0.5
        row = y * BASE * 4
        for x in range(BASE):
            px = x + 0.5
            # squircle mask: superellipse, radially normalized so the
            # feather stays ~constant along the boundary
            fx = abs(px - half) / SQUIRCLE_HALF
            fy = abs(py - half) / SQUIRCLE_HALF
            radial = (fx ** SQUIRCLE_POWER +
                      fy ** SQUIRCLE_POWER) ** (1.0 / SQUIRCLE_POWER)
            alpha = 1.0 - smoothstep(1.0 - FEATHER / SQUIRCLE_HALF,
                                     1.0 + FEATHER / SQUIRCLE_HALF, radial)
            if alpha <= 0.0:
                continue
            # background: vertical gradient + corner vignette
            colour = lerp3(BG_TOP, BG_BOTTOM, py / BASE)
            vignette = smoothstep(0.72, 1.0, radial) * 0.35
            colour = lerp3(colour, (0, 0, 0), vignette)
            # ball: off-centre radial gradient + specular highlight
            bx, by = px - BALL_CENTRE[0], py - BALL_CENTRE[1]
            ball_dist = math.sqrt(bx * bx + by * by)
            if ball_dist < BALL_RADIUS + FEATHER:
                shade = min(1.0, ball_dist / BALL_RADIUS)
                # light from the upper left
                lx = (bx + BALL_RADIUS * 0.38) / BALL_RADIUS
                ly = (by + BALL_RADIUS * 0.42) / BALL_RADIUS
                light = math.sqrt(lx * lx + ly * ly) * 0.72
                ball = lerp3(BALL_LIGHT, BALL_MID, min(1.0, light))
                ball = lerp3(ball, BALL_RIM, smoothstep(0.62, 1.0, shade))
                spec = math.exp(-(lx * lx + ly * ly) * 5.5) * 0.5
                ball = lerp3(ball, (255, 255, 255), spec)
                coverage = 1.0 - smoothstep(BALL_RADIUS - FEATHER,
                                            BALL_RADIUS + FEATHER, ball_dist)
                colour = lerp3(colour, ball, coverage)
            # star (bbox-gated: the polygon SDF costs 10 edges per pixel)
            if star_min_x <= px <= star_max_x and \
                    star_min_y <= py <= star_max_y:
                sdist = polygon_signed_distance(px, py, star)
                body = 1.0 - smoothstep(-FEATHER, FEATHER, sdist)
                if body > 0.0:
                    tone = lerp3(STAR_COLOUR, STAR_RIM,
                                 smoothstep(-14.0, 0.0, sdist))
                    colour = lerp3(colour, tone, body)
            i = row + x * 4
            pixels[i] = int(colour[0] + 0.5)
            pixels[i + 1] = int(colour[1] + 0.5)
            pixels[i + 2] = int(colour[2] + 0.5)
            pixels[i + 3] = int(alpha * 255.0 + 0.5)
    return pixels


def downscale(pixels, size, factor):
    """box-filter a straight-alpha RGBA buffer down by an integer factor
    (premultiplied accumulation, so transparent texels don't bleed colour)."""
    out_size = size // factor
    out = bytearray(out_size * out_size * 4)
    samples = factor * factor
    for oy in range(out_size):
        for ox in range(out_size):
            r_sum = g_sum = b_sum = a_sum = 0
            for sy in range(factor):
                base = ((oy * factor + sy) * size + ox * factor) * 4
                for sx in range(factor):
                    i = base + sx * 4
                    a = pixels[i + 3]
                    r_sum += pixels[i] * a
                    g_sum += pixels[i + 1] * a
                    b_sum += pixels[i + 2] * a
                    a_sum += a
            o = (oy * out_size + ox) * 4
            if a_sum:
                out[o] = (r_sum + a_sum // 2) // a_sum
                out[o + 1] = (g_sum + a_sum // 2) // a_sum
                out[o + 2] = (b_sum + a_sum // 2) // a_sum
            out[o + 3] = (a_sum + samples // 2) // samples
    return out


def write_png(path, pixels, size):
    raw = bytearray()
    stride = size * 4
    for y in range(size):
        raw.append(0)  # filter type None
        raw.extend(pixels[y * stride:(y + 1) * stride])

    def chunk(tag, payload):
        data = tag + payload
        return (struct.pack(">I", len(payload)) + data +
                struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(png)


# iconset entry -> pixel size (iconutil's expected file names)
ICONSET_ENTRIES = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
]


def main():
    if len(sys.argv) != 2 or not sys.argv[1].endswith(".iconset"):
        sys.exit("usage: make_editor_icon.py <output.iconset>")
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    base = render_base()
    # self-check: corners transparent (squircle), centre opaque (ball)
    if base[3] != 0 or base[(BASE * BASE - 1) * 4 + 3] != 0:
        sys.exit("make_editor_icon: squircle corners are not transparent")
    centre = ((BASE // 2) * BASE + BASE // 2) * 4
    if base[centre + 3] != 255:
        sys.exit("make_editor_icon: icon centre is not opaque")
    by_size = {BASE: base}
    for size in (512, 256, 128, 64, 32, 16):
        by_size[size] = downscale(base, BASE, BASE // size)
    for name, size in ICONSET_ENTRIES:
        write_png(os.path.join(out_dir, name), by_size[size], size)
    print("make_editor_icon: wrote %d entries to %s"
          % (len(ICONSET_ENTRIES), out_dir))


if __name__ == "__main__":
    main()
