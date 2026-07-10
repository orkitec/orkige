#!/usr/bin/env python3
"""Cook an SVG vector drawing into a native .oshape asset.

The flat-colour organic-shape import path: a hand-drawn (or agent-drawn) SVG is
flattened to polyline contours in world units and written as a lean .oshape
text asset (the same form VectorShapeAsset parses at runtime). Cooking here
keeps the RUNTIME dependency surface at zero - the player never parses SVG or
beziers, it reads the pre-flattened .oshape. An agent can equally write an
.oshape directly (plain text); this script is the SVG on-ramp.

Supported SVG subset (fill-only, phase 1):
  <path d="..."> with M/L/H/V/C/Q/Z (absolute AND relative; S/T smooth curves
  are approximated as C/Q without the reflected control point), <polygon>,
  <rect>, <circle>, <ellipse>. Each CLOSED subpath becomes one filled region
  in the element's fill colour; automatic hole detection is NOT done (author
  holes directly in the .oshape 'hole' block - a documented phase-1 limit).
Coordinates are y-flipped (SVG is y-down, .oshape is y-up), centered on the
origin and scaled so the drawing's larger extent spans --extent world units.

Pure stdlib. `--selftest` cooks a synthetic in-memory SVG and re-parses the
result, asserting well-formed regions (a CI gate, like cook_textures.py).

Note: the engine's C++ flattener (core_util/VectorTessellator::flattenCubic) is
the authority for RUNTIME flattening; this cook mirrors the same adaptive
subdivision. They are not bit-pinned to each other (float paths differ) - the
shared contract is "smooth to a chord tolerance", which both honour.
"""

import argparse
import math
import re
import sys
import xml.etree.ElementTree as ET

FLATTEN_MAX_DEPTH = 16

_NAMED_COLOURS = {
    "black": (0.0, 0.0, 0.0), "white": (1.0, 1.0, 1.0),
    "red": (1.0, 0.0, 0.0), "green": (0.0, 0.5, 0.0),
    "blue": (0.0, 0.0, 1.0), "yellow": (1.0, 1.0, 0.0),
    "gray": (0.5, 0.5, 0.5), "grey": (0.5, 0.5, 0.5),
}


def _parse_colour(text, opacity):
    """Parse an SVG fill string to straight RGBA 0..1; None for 'none'."""
    if text is None:
        text = "black"          # SVG default fill
    text = text.strip().lower()
    if text == "none":
        return None
    rgb = None
    if text.startswith("#"):
        hexpart = text[1:]
        if len(hexpart) == 3:
            rgb = tuple(int(c * 2, 16) / 255.0 for c in hexpart)
        elif len(hexpart) == 6:
            rgb = tuple(int(hexpart[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    elif text.startswith("rgb("):
        parts = text[4:].rstrip(")").split(",")
        if len(parts) == 3:
            rgb = tuple(float(p.strip().rstrip("%")) / 255.0 for p in parts)
    elif text in _NAMED_COLOURS:
        rgb = _NAMED_COLOURS[text]
    if rgb is None:
        rgb = (0.0, 0.0, 0.0)
    return (rgb[0], rgb[1], rgb[2], float(opacity))


def _flatten_cubic(p0, p1, p2, p3, tol, out, depth=0):
    """Adaptive de-Casteljau cubic flatten; appends points EXCLUDING p0."""
    def dist_sq_to_line(p, a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length_sq = dx * dx + dy * dy
        if length_sq <= 0.0:
            return (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2
        cross = (p[0] - a[0]) * dy - (p[1] - a[1]) * dx
        return (cross * cross) / length_sq
    d1 = dist_sq_to_line(p1, p0, p3)
    d2 = dist_sq_to_line(p2, p0, p3)
    if depth >= FLATTEN_MAX_DEPTH or (d1 <= tol * tol and d2 <= tol * tol):
        out.append(p3)
        return
    def mid(a, b):
        return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
    p01, p12, p23 = mid(p0, p1), mid(p1, p2), mid(p2, p3)
    p012, p123 = mid(p01, p12), mid(p12, p23)
    m = mid(p012, p123)
    _flatten_cubic(p0, p01, p012, m, tol, out, depth + 1)
    _flatten_cubic(m, p123, p23, p3, tol, out, depth + 1)


def _flatten_quadratic(p0, p1, p2, tol, out):
    c1 = ((p0[0] + 2 * p1[0]) / 3.0, (p0[1] + 2 * p1[1]) / 3.0)
    c2 = ((p2[0] + 2 * p1[0]) / 3.0, (p2[1] + 2 * p1[1]) / 3.0)
    _flatten_cubic(p0, c1, c2, p2, tol, out)


_TOKEN_RE = re.compile(r"[MmLlHhVvCcSsQqTtZz]|[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?")


def _parse_path(d, tol):
    """Parse an SVG path 'd' into a list of closed subpaths (point lists)."""
    tokens = _TOKEN_RE.findall(d)
    i = 0
    subpaths = []
    current = []
    cx = cy = 0.0
    start = (0.0, 0.0)
    cmd = None

    def num():
        nonlocal i
        value = float(tokens[i])
        i += 1
        return value

    while i < len(tokens):
        token = tokens[i]
        if token.isalpha():
            cmd = token
            i += 1
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            x, y = num(), num()
            if rel:
                x, y = cx + x, cy + y
            if current:
                subpaths.append(current)
            current = [(x, y)]
            cx, cy = x, y
            start = (x, y)
            cmd = "l" if rel else "L"     # subsequent pairs are line-tos
        elif c == "L":
            x, y = num(), num()
            if rel:
                x, y = cx + x, cy + y
            current.append((x, y))
            cx, cy = x, y
        elif c == "H":
            x = num()
            x = cx + x if rel else x
            current.append((x, cy))
            cx = x
        elif c == "V":
            y = num()
            y = cy + y if rel else y
            current.append((cx, y))
            cy = y
        elif c in ("C", "S"):
            if c == "S":                  # smooth: reuse current point as ctrl1
                x1, y1 = cx, cy
                x2, y2 = num(), num()
                x, y = num(), num()
            else:
                x1, y1 = num(), num()
                x2, y2 = num(), num()
                x, y = num(), num()
            if rel:
                x1, y1, x2, y2, x, y = (cx + x1, cy + y1, cx + x2, cy + y2,
                                        cx + x, cy + y)
            _flatten_cubic((cx, cy), (x1, y1), (x2, y2), (x, y), tol, current)
            cx, cy = x, y
        elif c in ("Q", "T"):
            if c == "T":
                x1, y1 = cx, cy
                x, y = num(), num()
            else:
                x1, y1 = num(), num()
                x, y = num(), num()
            if rel:
                x1, y1, x, y = cx + x1, cy + y1, cx + x, cy + y
            _flatten_quadratic((cx, cy), (x1, y1), (x, y), tol, current)
            cx, cy = x, y
        elif c == "Z":
            if current:
                subpaths.append(current)
                current = []
            cx, cy = start
        else:
            i += 1
    if current:
        subpaths.append(current)
    # drop a duplicated closing point (Z back to the start)
    cleaned = []
    for sub in subpaths:
        if len(sub) >= 2 and abs(sub[0][0] - sub[-1][0]) < 1e-9 and \
                abs(sub[0][1] - sub[-1][1]) < 1e-9:
            sub = sub[:-1]
        if len(sub) >= 3:
            cleaned.append(sub)
    return cleaned


def _local(tag):
    return tag.split("}")[-1]


def _shape_elements(root, tol):
    """Yield (subpaths, rgba) for every fillable element in document order."""
    for elem in root.iter():
        tag = _local(elem.tag)
        fill = _parse_colour(elem.get("fill"),
                             elem.get("fill-opacity", "1"))
        if fill is None and tag != "svg":
            # an explicit fill:none still means skip; no fill attr defaults black
            if elem.get("fill", "").strip().lower() == "none":
                continue
        subpaths = []
        if tag == "path" and elem.get("d"):
            subpaths = _parse_path(elem.get("d"), tol)
        elif tag == "polygon" and elem.get("points"):
            nums = [float(n) for n in re.findall(
                r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", elem.get("points"))]
            pts = list(zip(nums[0::2], nums[1::2]))
            if len(pts) >= 3:
                subpaths = [pts]
        elif tag == "rect":
            x = float(elem.get("x", 0)); y = float(elem.get("y", 0))
            w = float(elem.get("width", 0)); h = float(elem.get("height", 0))
            if w > 0 and h > 0:
                subpaths = [[(x, y), (x + w, y), (x + w, y + h), (x, y + h)]]
        elif tag in ("circle", "ellipse"):
            cx = float(elem.get("cx", 0)); cy = float(elem.get("cy", 0))
            if tag == "circle":
                rx = ry = float(elem.get("r", 0))
            else:
                rx = float(elem.get("rx", 0)); ry = float(elem.get("ry", 0))
            if rx > 0 and ry > 0:
                steps = max(12, int(2 * math.pi * max(rx, ry) / max(tol, 1e-3)))
                steps = min(steps, 256)
                subpaths = [[(cx + rx * math.cos(2 * math.pi * k / steps),
                              cy + ry * math.sin(2 * math.pi * k / steps))
                             for k in range(steps)]]
        if subpaths:
            yield subpaths, (fill if fill is not None else (0.0, 0.0, 0.0, 1.0))


def cook(svg_text, tolerance=None, extent=2.0):
    """Cook SVG text to .oshape text. Raises ValueError on no fillable shape."""
    root = ET.fromstring(svg_text)
    # a tolerance relative to the drawing size (0.25% of the extent) unless set
    tol = tolerance if tolerance is not None else extent * 0.0025
    # SVG userspace tolerance: convert the world tol back once we know the scale;
    # first pass flattens in userspace with a small absolute tol, then rescale
    regions = list(_shape_elements(root, tol=1.0))
    if not regions:
        raise ValueError("no fillable shapes in the SVG")

    all_points = [p for subs, _ in regions for sub in subs for p in sub]
    min_x = min(p[0] for p in all_points)
    max_x = max(p[0] for p in all_points)
    min_y = min(p[1] for p in all_points)
    max_y = max(p[1] for p in all_points)
    span = max(max_x - min_x, max_y - min_y, 1e-6)
    scale = extent / span
    cx = (min_x + max_x) * 0.5
    cy = (min_y + max_y) * 0.5

    def place(p):
        # center, scale to world units, and flip Y (SVG down -> engine up)
        return ((p[0] - cx) * scale, -(p[1] - cy) * scale)

    lines = ["# orkige vector shape v1 - cooked from SVG by Util/cook_shapes.py",
             "# units are world units, +x right, +y UP", "version 1"]
    emitted = 0
    for subpaths, rgba in regions:
        for sub in subpaths:
            if len(sub) < 3:
                continue
            lines.append("fill %.4f %.4f %.4f %.4f" % rgba)
            lines.append("contour %d" % len(sub))
            for p in sub:
                wx, wy = place(p)
                lines.append("v %.5f %.5f" % (wx, wy))
            emitted += 1
    if emitted == 0:
        raise ValueError("no closed contours with >= 3 vertices")
    return "\n".join(lines) + "\n"


def _parse_oshape(text):
    """A tiny .oshape re-parser mirroring VectorShapeAsset (selftest only)."""
    regions = []
    target = None
    remaining = 0
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].split()
        if not line:
            continue
        key = line[0]
        if key == "fill":
            assert remaining == 0
            regions.append({"fill": tuple(float(v) for v in line[1:5]),
                            "outer": [], "holes": []})
            target = None
        elif key == "contour":
            assert remaining == 0 and regions
            remaining = int(line[1])
            assert remaining > 0
            target = regions[-1]["outer"]
        elif key == "hole":
            assert remaining == 0 and regions
            remaining = int(line[1])
            regions[-1]["holes"].append([])
            target = regions[-1]["holes"][-1]
        elif key == "v":
            assert target is not None and remaining > 0
            target.append((float(line[1]), float(line[2])))
            remaining -= 1
        elif key == "version":
            pass
    assert remaining == 0
    return regions


def _selftest():
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">'
           '<rect x="10" y="10" width="80" height="60" fill="#e56b61"/>'
           '<path d="M 20 20 L 60 20 L 40 50 Z" fill="rgb(40,40,60)"/>'
           '<circle cx="50" cy="50" r="20" fill="blue"/>'
           '</svg>')
    oshape = cook(svg)
    regions = _parse_oshape(oshape)
    assert len(regions) >= 3, "expected >= 3 regions, got %d" % len(regions)
    for region in regions:
        assert len(region["outer"]) >= 3, "a region has < 3 vertices"
        assert len(region["fill"]) == 4, "a region has no RGBA fill"
    # the rect region round-trips to 4 vertices; the triangle to 3
    counts = sorted(len(r["outer"]) for r in regions)
    assert 3 in counts and 4 in counts, "rect/triangle vertex counts wrong"
    # the salmon fill colour survived (0.898 ~ 0xe5/255)
    assert any(abs(r["fill"][0] - 0.898) < 0.01 for r in regions), \
        "fill colour did not round-trip"
    print("cook_shapes selftest OK: %d regions, vertex counts %s" %
          (len(regions), counts))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", nargs="?", help="source .svg")
    parser.add_argument("output", nargs="?", help="destination .oshape")
    parser.add_argument("--tolerance", type=float, default=None,
                        help="absolute flatten chord tolerance in SVG units")
    parser.add_argument("--extent", type=float, default=2.0,
                        help="world-unit size the drawing's larger side spans")
    parser.add_argument("--selftest", action="store_true",
                        help="cook + re-parse a synthetic SVG and assert")
    args = parser.parse_args()
    if args.selftest:
        return _selftest()
    if not args.input or not args.output:
        parser.error("input and output are required (or use --selftest)")
    with open(args.input, "r", encoding="utf-8") as handle:
        svg_text = handle.read()
    oshape = cook(svg_text, tolerance=args.tolerance, extent=args.extent)
    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write(oshape)
    print("cooked %s -> %s" % (args.input, args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
