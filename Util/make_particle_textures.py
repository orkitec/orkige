#!/usr/bin/env python3
"""Generate the small soft particle textures the 3D particle / weather demo
uses: a round soft dot (snow flakes, sparks) and a soft vertical streak (rain
drops). Both are white RGB with a smooth alpha falloff so the additive/alpha
billboard blend reads as a soft glow. Stdlib only (the orkige_png codec).

Run from the repo root:
    python3 Util/make_particle_textures.py [out_dir]

Defaults to samples/hello_orkige/media (the demo asset dir). Deterministic:
re-running rewrites byte-identical PNGs.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_png  # noqa: E402


def _smoothstep(edge0, edge1, x):
    if edge1 <= edge0:
        return 0.0 if x < edge0 else 1.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def make_dot(size=32):
    """a radially symmetric soft dot: full alpha at the centre fading to zero
    at the rim (a squared falloff for a soft edge)."""
    image = orkige_png.Image(size, size)
    centre = (size - 1) / 2.0
    radius = size / 2.0
    for y in range(size):
        for x in range(size):
            dx = (x - centre) / radius
            dy = (y - centre) / radius
            dist = math.sqrt(dx * dx + dy * dy)
            alpha = 1.0 - _smoothstep(0.15, 1.0, dist)
            image.put(x, y, (255, 255, 255, int(round(alpha * 255))))
    return image


def make_streak(width=16, height=64):
    """a soft vertical streak: bright down the centre column, fading to the
    sides, tapering at the top and bottom - the rain-drop billboard texture."""
    image = orkige_png.Image(width, height)
    cx = (width - 1) / 2.0
    halfWidth = width / 2.0
    for y in range(height):
        # taper the ends: full through the middle, fading over the last quarter
        v = y / (height - 1.0)
        endTaper = _smoothstep(0.0, 0.25, v) * (1.0 - _smoothstep(0.75, 1.0, v))
        for x in range(width):
            across = abs(x - cx) / halfWidth
            sideFalloff = 1.0 - _smoothstep(0.0, 1.0, across)
            alpha = sideFalloff * endTaper
            image.put(x, y, (255, 255, 255, int(round(alpha * 255))))
    return image


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        "samples", "hello_orkige", "media")
    os.makedirs(out_dir, exist_ok=True)
    dot_path = os.path.join(out_dir, "particle_dot.png")
    rain_path = os.path.join(out_dir, "particle_rain.png")
    orkige_png.encode_png(make_dot(), dot_path)
    orkige_png.encode_png(make_streak(), rain_path)
    print("wrote %s" % dot_path)
    print("wrote %s" % rain_path)


if __name__ == "__main__":
    main()
