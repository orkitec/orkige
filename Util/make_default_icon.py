#!/usr/bin/env python3
"""Generate the neutral engine default app icon (Util/media/orkige_default_icon.png).

Rendered procedurally (stdlib only, PNG via orkige_png): a rounded-square frame
mark on the engine's dark gradient, sized to the macOS Big Sur+ icon grid (824px
squircle on a 1024px canvas). Projects that set no export.icon get this icon at
export time, so a freshly exported app is never iconless.

The mark is a plain geometric frame - a neutral engine glyph, NOT the editor's
branded ball-with-star (that one identifies the tool, this one identifies an
unbranded exported game).

Usage:  make_default_icon.py <output.png>   write the icon (default:
                                            Util/media/orkige_default_icon.png)
        make_default_icon.py --selftest     render + assert corner/centre alpha
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_png  # noqa: E402  (sibling Util helper)

BASE = 1024                      # master render size (square)
SQUIRCLE_HALF = 412.0            # 824px squircle (Apple's icon grid)
SQUIRCLE_POWER = 5.0             # superellipse exponent
FEATHER = 1.6                    # anti-alias feather in pixels

# palette (sRGB) - the engine's neutral dark gradient + a cool accent
BG_TOP = (44, 52, 71)
BG_BOTTOM = (18, 22, 32)
MARK_LIGHT = (120, 196, 236)
MARK_DARK = (66, 132, 176)

FRAME_HALF = 236.0               # rounded-square frame half-extent
FRAME_CORNER = 84.0              # frame corner radius
FRAME_THICKNESS = 60.0           # stroke half-width


def smoothstep(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def lerp3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t)


def rounded_rect_distance(px, py, half, radius):
    """signed distance to a centred rounded square (negative inside)."""
    qx = abs(px) - (half - radius)
    qy = abs(py) - (half - radius)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    inside = min(max(qx, qy), 0.0)
    return outside + inside - radius


def render():
    """render the 1024px master as a straight-alpha orkige_png.Image."""
    image = orkige_png.Image(BASE, BASE)
    pixels = image.pixels
    half = BASE / 2.0
    for y in range(BASE):
        py = y + 0.5
        row = y * BASE * 4
        for x in range(BASE):
            px = x + 0.5
            # squircle mask (superellipse), radially normalised so the feather
            # stays ~constant along the boundary
            fx = abs(px - half) / SQUIRCLE_HALF
            fy = abs(py - half) / SQUIRCLE_HALF
            radial = (fx ** SQUIRCLE_POWER +
                      fy ** SQUIRCLE_POWER) ** (1.0 / SQUIRCLE_POWER)
            alpha = 1.0 - smoothstep(1.0 - FEATHER / SQUIRCLE_HALF,
                                     1.0 + FEATHER / SQUIRCLE_HALF, radial)
            if alpha <= 0.0:
                continue
            # background: vertical gradient + a soft corner vignette
            colour = lerp3(BG_TOP, BG_BOTTOM, py / BASE)
            vignette = smoothstep(0.72, 1.0, radial) * 0.35
            colour = lerp3(colour, (0, 0, 0), vignette)
            # the frame mark: |distance-to-rounded-rect| within the stroke
            frame = abs(rounded_rect_distance(px - half, py - half,
                                              FRAME_HALF, FRAME_CORNER))
            coverage = 1.0 - smoothstep(FRAME_THICKNESS - FEATHER,
                                        FRAME_THICKNESS + FEATHER, frame)
            if coverage > 0.0:
                # top-lit gradient across the stroke
                tone = lerp3(MARK_LIGHT, MARK_DARK, (py - half) / (2.0 * half)
                             + 0.5)
                colour = lerp3(colour, tone, coverage)
            i = row + x * 4
            pixels[i] = int(colour[0] + 0.5)
            pixels[i + 1] = int(colour[1] + 0.5)
            pixels[i + 2] = int(colour[2] + 0.5)
            pixels[i + 3] = int(alpha * 255.0 + 0.5)
    return image


def selftest():
    image = render()
    # corners transparent (squircle), centre opaque (background inside the
    # frame hole) - the same invariant make_editor_icon self-checks
    if image.get(0, 0)[3] != 0 or image.get(BASE - 1, BASE - 1)[3] != 0:
        sys.exit("make_default_icon: squircle corners are not transparent")
    if image.get(BASE // 2, BASE // 2)[3] != 255:
        sys.exit("make_default_icon: icon centre is not opaque")
    # the frame mark actually rendered: a point on the top stroke is accent-lit,
    # not the bare background
    stroke = image.get(BASE // 2, int(BASE / 2.0 - FRAME_HALF + FRAME_CORNER))
    if stroke[2] <= stroke[0]:
        sys.exit("make_default_icon: frame stroke did not render")
    print("make_default_icon: selftest OK")


DEFAULT_OUTPUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "media", "orkige_default_icon.png")


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        selftest()
        return
    out_path = sys.argv[1] if len(sys.argv) >= 2 else DEFAULT_OUTPUT
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    orkige_png.encode_png(render(), out_path)
    print("make_default_icon: wrote %s (%dx%d)" % (out_path, BASE, BASE))


if __name__ == "__main__":
    main()
