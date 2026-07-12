#!/usr/bin/env python3
"""Cook a Lottie JSON animation into a native .oanim asset.

The animated sibling of cook_shapes.py: a Lottie document (the open,
Linux-Foundation-standardized vector animation interchange format) is
translated at import time into the engine's `.oanim` text asset - a layer rig
with keyframed transforms, opacities and path poses, carved into named clips.
Cooking keeps the RUNTIME dependency surface at zero (the player never parses
JSON or beziers) and makes unsupported features fail loudly HERE, per layer,
never silently at play. The source .json stays the artist's living document;
the cook is idempotent and deterministic, so re-running it after an edit
regenerates the same .oanim byte-for-byte.

Supported subset (v1, flat-colour art direction):
  layers    shape (ty 4), null (ty 3), solid (ty 1), precomp (ty 0 - inlined
            at cook when untimed: stretch 1, no time remap; nested inlining
            works, the crop rectangle is not applied), parenting, in/out
            windows (baked into the opacity channel), hidden layers skipped
  shapes    groups (static group transforms baked into vertices, animated
            group opacity folded into the fill), paths, ellipses, rects
            (incl. rounded - flattened to beziers), flat fills with animated
            colour and opacity; fill rule (nonzero/evenodd) drives hole
            assignment
  timing    keyframes with cubic value-bezier easing and hold keys; markers
            become clips (a `#once` comment suffix makes a one-shot clip;
            zero-duration markers extend to the next marker); `--clips`
            overrides markers for marker-less authoring tools
Everything the runtime grammar cannot express directly (spatial position
tangents, split/per-dimension easing, misaligned multi-property shape
animation, keys outside the timeline, windowed animated opacity) is DENSIFIED
at cook: baked into per-frame keys, so the runtime interpolator stays cubic
value-bezier + hold. Paths flatten with a FIXED per-edge segment count chosen
from the worst-case curvature across ALL keyframes of that path, so every key
has the identical vertex count - the .oanim topology law - and lerping the
flattened vertices equals flattening the lerped bezier exactly (beziers are
linear in their control points).

Out of subset - each a named, per-layer cook error (never a silent skip):
gradients, strokes, masks, track mattes, layer effects, expressions, image
and text layers, repeaters, merge/trim paths, rounded-corner modifiers,
skew, auto-orient, 3D layers, time stretch/remap, TIMED precomps.

A document where NOTHING animates cooks to a plain .oshape instead (the
static one-shape-core case) - the output path's suffix is switched.

Coordinates are y-flipped (Lottie is y-down, .oanim is +y up; rotations
negate to CCW), centered on the composition's midpoint and scaled so the
larger composition extent spans --extent world units. Lottie scale 100 maps
to 1.0, opacity 100 to 1.0.

Pure stdlib. `--selftest` cooks embedded fixture documents covering every
mapped feature and every error path, asserts on the emitted text, and
re-cooks the committed round-trip fixture (tests/assets/vectoranim/) checking
byte identity - the C++ side of that fixture is the cook_vector_anim_roundtrip
unit test, which feeds the SAME .oanim through the real parser + evaluator.
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# constants

KAPPA = 0.5522847498307936      # cubic arc approximation of a quarter circle
MAX_EDGE_SEGMENTS = 32          # per-edge flattening cap
MIN_EDGE_SEGMENTS = 1
EPS = 1e-6


class CookError(ValueError):
    """A named, per-layer import error (the honest failure mode)."""


# ---------------------------------------------------------------------------
# small math helpers


def _bezier_ease(ox, oy, ix, iy, u):
    """Evaluate a cubic value bezier (0,0)-(ox,oy)-(ix,iy)-(1,1) at time
    fraction u: solve x(t) = u by bisection (x is monotone for ox/ix in 0..1),
    return y(t). Mirrors VectorAnimEval::evalEase."""
    if u <= 0.0:
        return 0.0
    if u >= 1.0:
        return 1.0
    ox = min(max(ox, 0.0), 1.0)
    ix = min(max(ix, 0.0), 1.0)

    def curve(a, b, t):
        mt = 1.0 - t
        return 3 * mt * mt * t * a + 3 * mt * t * t * b + t * t * t

    lo, hi = 0.0, 1.0
    for _ in range(48):
        mid = (lo + hi) * 0.5
        if curve(ox, ix, mid) < u:
            lo = mid
        else:
            hi = mid
    t = (lo + hi) * 0.5
    return curve(oy, iy, t)


def _cubic_point(p0, c1, c2, p3, t):
    mt = 1.0 - t
    a = mt * mt * mt
    b = 3 * mt * mt * t
    c = 3 * mt * t * t
    d = t * t * t
    return (a * p0[0] + b * c1[0] + c * c2[0] + d * p3[0],
            a * p0[1] + b * c1[1] + c * c2[1] + d * p3[1])


def _dist_to_segment(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length_sq = dx * dx + dy * dy
    if length_sq <= EPS * EPS:
        return math.hypot(p[0] - a[0], p[1] - a[1])
    t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length_sq
    t = min(max(t, 0.0), 1.0)
    return math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy))


def _point_in_polygon(p, poly):
    """Even-odd ray cast: is p strictly inside the polygon?"""
    inside = False
    n = len(poly)
    j = n - 1
    for k in range(n):
        xi, yi = poly[k]
        xj, yj = poly[j]
        if (yi > p[1]) != (yj > p[1]):
            x_cross = (xj - xi) * (p[1] - yi) / (yj - yi) + xi
            if p[0] < x_cross:
                inside = not inside
        j = k
    return inside


def _polygon_area(poly):
    """Signed area (positive = counter-clockwise in the polygon's space)."""
    area = 0.0
    n = len(poly)
    for k in range(n):
        x0, y0 = poly[k]
        x1, y1 = poly[(k + 1) % n]
        area += x0 * y1 - x1 * y0
    return area * 0.5


# ---------------------------------------------------------------------------
# Lottie animatable properties
#
# A property is either a bare value, {"a":0,"k":value} (static) or
# {"a":1,"k":[keyframes]}. Keyframes: {"t","s","o","i","h","ti","to"} with the
# legacy "e" (segment end value) resolved against the next key's "s".


def _has_expression(prop):
    return isinstance(prop, dict) and isinstance(prop.get("x"), str)


def _as_list(value):
    if isinstance(value, (int, float)):
        return [float(value)]
    return [float(v) for v in value]


def _prop_keys(prop, dim):
    """Normalize an ANIMATED property's keyframes: a list of dicts with
    t (float), s (list[dim]), h (bool), ease ((ox,oy,ix,iy) per component or
    None for the last key), ti/to (spatial tangents or None)."""
    raw = prop.get("k", [])
    keys = []
    for idx, k in enumerate(raw):
        if not isinstance(k, dict):
            return None
        s = k.get("s")
        if s is None and idx > 0:
            s = raw[idx - 1].get("e")   # legacy: segment end value
        if s is None:
            return None
        entry = {
            "t": float(k.get("t", 0.0)),
            "s": _as_list(s)[:dim],
            "h": int(k.get("h", 0)) == 1,
            "o": k.get("o"),
            "i": k.get("i"),
            "ti": k.get("ti"),
            "to": k.get("to"),
        }
        while len(entry["s"]) < dim:
            entry["s"].append(entry["s"][-1] if entry["s"] else 0.0)
        keys.append(entry)
    keys.sort(key=lambda e: e["t"])
    return keys


def _is_animated(prop, dim):
    if not isinstance(prop, dict):
        return False
    if int(prop.get("a", 0)) != 1:
        return False
    keys = _prop_keys(prop, dim)
    if not keys or len(keys) < 2:
        return False
    first = keys[0]["s"]
    return any(k["s"] != first for k in keys)


def _static_value(prop, dim, default):
    """The value of a property that does not animate (bare, a:0, or a single
    effectively-constant keyframe run)."""
    if prop is None:
        return list(default)
    if not isinstance(prop, dict):
        return _as_list(prop)[:dim] + [0.0] * max(0, dim - 1)
    if int(prop.get("a", 0)) == 1:
        keys = _prop_keys(prop, dim)
        if not keys:
            return list(default)
        return list(keys[0]["s"])
    value = _as_list(prop.get("k", default))[:dim]
    while len(value) < dim:
        value.append(value[-1] if value else 0.0)
    return value


def _ease_components(tangent, dim, default):
    """A keyframe's o/i tangent as per-component (x, y) pairs. Lottie stores
    {"x": v, "y": v} where v is a number or a per-component list."""
    if not isinstance(tangent, dict):
        return [(default, default)] * dim
    xs = _as_list(tangent.get("x", default))
    ys = _as_list(tangent.get("y", default))
    pairs = []
    for d in range(dim):
        x = xs[d] if d < len(xs) else xs[-1] if xs else default
        y = ys[d] if d < len(ys) else ys[-1] if ys else default
        pairs.append((x, y))
    return pairs


def _segment_ease(key, dim):
    """The easing of the segment leaving `key`, as ('hold',) /
    ('lin',) / ('ease', ox, oy, ix, iy), or None when the components
    disagree per dimension (a densify case)."""
    if key["h"]:
        return ("hold",)
    o_pairs = _ease_components(key.get("o"), dim, 1.0 / 3.0)
    i_pairs = _ease_components(key.get("i"), dim, 2.0 / 3.0)
    if any(p != o_pairs[0] for p in o_pairs) or \
            any(p != i_pairs[0] for p in i_pairs):
        return None
    ox, oy = o_pairs[0]
    ix, iy = i_pairs[0]
    if abs(ox - oy) < EPS and abs(ix - iy) < EPS:
        return ("lin",)
    return ("ease", ox, oy, ix, iy)


def _has_spatial_tangents(keys):
    for k in keys:
        for tangent in (k.get("ti"), k.get("to")):
            if tangent and any(abs(float(c)) > EPS for c in tangent):
                return True
    return False


def _sample_keys(keys, frame, dim):
    """Sample a normalized keyframe list at a frame (clamped outside the key
    range; hold / bezier easing inside; spatial position tangents honoured)."""
    if frame <= keys[0]["t"]:
        return list(keys[0]["s"])
    if frame >= keys[-1]["t"]:
        return list(keys[-1]["s"])
    for idx in range(len(keys) - 1):
        k0, k1 = keys[idx], keys[idx + 1]
        if k0["t"] <= frame < k1["t"] or \
                (idx == len(keys) - 2 and frame <= k1["t"]):
            if frame >= k1["t"]:
                return list(k1["s"])
            if k0["h"]:
                return list(k0["s"])
            u = (frame - k0["t"]) / (k1["t"] - k0["t"])
            o_pairs = _ease_components(k0.get("o"), dim, 1.0 / 3.0)
            i_pairs = _ease_components(k0.get("i"), dim, 2.0 / 3.0)
            eased = []
            for d in range(dim):
                ox, oy = o_pairs[d]
                ix, iy = i_pairs[d]
                eased.append(_bezier_ease(ox, oy, ix, iy, u))
            to = k0.get("to")
            ti = k0.get("ti")
            spatial = dim == 2 and (
                (to and any(abs(float(c)) > EPS for c in to)) or
                (ti and any(abs(float(c)) > EPS for c in ti)))
            if spatial:
                p0 = (k0["s"][0], k0["s"][1])
                p3 = (k1["s"][0], k1["s"][1])
                tov = to or [0.0, 0.0]
                tiv = ti or [0.0, 0.0]
                c1 = (p0[0] + float(tov[0]), p0[1] + float(tov[1]))
                c2 = (p3[0] + float(tiv[0]), p3[1] + float(tiv[1]))
                pt = _cubic_point(p0, c1, c2, p3, eased[0])
                return [pt[0], pt[1]]
            return [k0["s"][d] + eased[d] * (k1["s"][d] - k0["s"][d])
                    for d in range(dim)]
    return list(keys[-1]["s"])


# ---------------------------------------------------------------------------
# bezier path construction (everything becomes {closed, v, i, o} - absolute
# vertices with relative in/out tangents, the Lottie path vocabulary)


def _path_from_lottie(shape_value):
    v = [(float(p[0]), float(p[1])) for p in shape_value.get("v", [])]
    i = [(float(p[0]), float(p[1])) for p in shape_value.get("i", [])]
    o = [(float(p[0]), float(p[1])) for p in shape_value.get("o", [])]
    while len(i) < len(v):
        i.append((0.0, 0.0))
    while len(o) < len(v):
        o.append((0.0, 0.0))
    return {"closed": bool(shape_value.get("c", True)),
            "v": v, "i": i, "o": o}


def _path_ellipse(center, size):
    cx, cy = center
    rx, ry = abs(size[0]) * 0.5, abs(size[1]) * 0.5
    kx, ky = KAPPA * rx, KAPPA * ry
    return {"closed": True,
            "v": [(cx, cy - ry), (cx + rx, cy), (cx, cy + ry), (cx - rx, cy)],
            "o": [(kx, 0.0), (0.0, ky), (-kx, 0.0), (0.0, -ky)],
            "i": [(-kx, 0.0), (0.0, -ky), (kx, 0.0), (0.0, ky)]}


def _path_rect(center, size, radius, rounded_topology):
    """A rect as a bezier path. rounded_topology keeps the 8-vertex rounded
    form even at radius 0 (degenerate corners) so a radius ANIMATION keeps
    the fixed vertex count."""
    cx, cy = center
    w2, h2 = abs(size[0]) * 0.5, abs(size[1]) * 0.5
    r = min(max(radius, 0.0), min(w2, h2))
    if not rounded_topology:
        return {"closed": True,
                "v": [(cx - w2, cy - h2), (cx + w2, cy - h2),
                      (cx + w2, cy + h2), (cx - w2, cy + h2)],
                "o": [(0.0, 0.0)] * 4, "i": [(0.0, 0.0)] * 4}
    k = KAPPA * r
    v = [(cx + w2 - r, cy - h2), (cx + w2, cy - h2 + r),   # top-right corner
         (cx + w2, cy + h2 - r), (cx + w2 - r, cy + h2),   # bottom-right
         (cx - w2 + r, cy + h2), (cx - w2, cy + h2 - r),   # bottom-left
         (cx - w2, cy - h2 + r), (cx - w2 + r, cy - h2)]   # top-left
    o = [(k, 0.0), (0.0, 0.0), (0.0, k), (0.0, 0.0),
         (-k, 0.0), (0.0, 0.0), (0.0, -k), (0.0, 0.0)]
    i = [(0.0, 0.0), (0.0, -k), (0.0, 0.0), (k, 0.0),
         (0.0, 0.0), (0.0, k), (0.0, 0.0), (-k, 0.0)]
    return {"closed": True, "v": v, "o": o, "i": i}


def _transform_path(path, affine):
    """Apply a static 2x3 affine (a, b, c, d, tx, ty) to a bezier path:
    vertices fully, tangents by the linear part only."""
    a, b, c, d, tx, ty = affine
    def full(p):
        return (a * p[0] + b * p[1] + tx, c * p[0] + d * p[1] + ty)
    def linear(p):
        return (a * p[0] + b * p[1], c * p[0] + d * p[1])
    return {"closed": path["closed"],
            "v": [full(p) for p in path["v"]],
            "i": [linear(p) for p in path["i"]],
            "o": [linear(p) for p in path["o"]]}


def _path_edges(path):
    """The path's cubic edges as (p0, c1, c2, p3) tuples. Fills imply
    closure, so an open path gets a straight closing edge."""
    v, i, o = path["v"], path["i"], path["o"]
    n = len(v)
    edges = []
    for j in range(n):
        j2 = (j + 1) % n
        if j2 == 0 and not path["closed"]:
            edges.append((v[j], v[j], v[0], v[0]))     # straight closure
        else:
            edges.append((v[j],
                          (v[j][0] + o[j][0], v[j][1] + o[j][1]),
                          (v[j2][0] + i[j2][0], v[j2][1] + i[j2][1]),
                          v[j2]))
    return edges


def _edge_segment_counts(paths_across_keys, tol):
    """The fixed-segment-count discipline: for each edge index, the WORST
    CASE curvature across every keyframe's path decides one segment count,
    applied to that edge in EVERY key - identical vertex counts by
    construction. paths_across_keys: [path, ...] (same structure)."""
    n = len(paths_across_keys[0]["v"])
    counts = []
    for j in range(n):
        deviation = 0.0
        for path in paths_across_keys:
            p0, c1, c2, p3 = _path_edges(path)[j]
            deviation = max(deviation,
                            _dist_to_segment(c1, p0, p3),
                            _dist_to_segment(c2, p0, p3))
        if deviation <= EPS:
            counts.append(MIN_EDGE_SEGMENTS)
        else:
            need = math.ceil(math.sqrt(0.75 * deviation / max(tol, EPS)))
            counts.append(min(max(need, MIN_EDGE_SEGMENTS),
                              MAX_EDGE_SEGMENTS))
    return counts


def _flatten_path(path, counts):
    """Flatten to a closed polyline with the given per-edge segment counts.
    Uniform-parameter sampling, so lerping flattened vertices between keys
    equals flattening the lerped bezier (beziers are linear in their control
    points)."""
    edges = _path_edges(path)
    points = [path["v"][0]]
    for j, (p0, c1, c2, p3) in enumerate(edges):
        segs = counts[j]
        for k in range(1, segs + 1):
            points.append(_cubic_point(p0, c1, c2, p3, k / segs))
    points.pop()    # the last edge returns to the start vertex
    return points


def _lerp_path(path0, path1, u):
    def lerp_pts(a, b):
        return [(pa[0] + u * (pb[0] - pa[0]), pa[1] + u * (pb[1] - pa[1]))
                for pa, pb in zip(a, b)]
    return {"closed": path0["closed"],
            "v": lerp_pts(path0["v"], path1["v"]),
            "i": lerp_pts(path0["i"], path1["i"]),
            "o": lerp_pts(path0["o"], path1["o"])}


# ---------------------------------------------------------------------------
# document walk: subset validation, precomp inlining, the flat layer list


_LAYER_TYPE_NAMES = {
    0: "precomp", 1: "solid", 2: "image", 3: "null", 4: "shape", 5: "text",
    6: "audio", 7: "video placeholder", 8: "image sequence", 9: "video",
    13: "camera",
}

_SHAPE_ITEM_ERRORS = {
    "gf": "gradient fill (flat fills only)",
    "gs": "gradient stroke (flat fills only)",
    "st": "stroke (flat fills only)",
    "rp": "repeater",
    "mm": "merge paths",
    "tm": "trim paths",
    "rd": "rounded-corners modifier (round the source path instead)",
    "pb": "pucker/bloat",
    "zz": "zig-zag",
    "op": "offset path",
    "tw": "twist",
}


def _check_expressions(prop, layer_name, what, errors):
    if _has_expression(prop):
        errors.append("expression on %s of layer '%s' - expressions are not "
                      "supported; bake them to keyframes" % (what, layer_name))


def _sanitize_name(name, fallback):
    name = re.sub(r"\s+", "_", str(name or "").strip())
    name = re.sub(r"[#]", "_", name)
    return name if name else fallback


def _validate_transform(ks, layer_name, errors):
    """Reject the transform features the rig cannot express."""
    if not isinstance(ks, dict):
        return
    for prop_name, what in (("p", "position"), ("a", "anchor"),
                            ("s", "scale"), ("r", "rotation"),
                            ("o", "opacity")):
        prop = ks.get(prop_name)
        _check_expressions(prop, layer_name, what, errors)
        if isinstance(prop, dict) and prop_name == "p" and prop.get("s"):
            for comp in ("x", "y"):
                _check_expressions(prop.get(comp), layer_name,
                                   "position." + comp, errors)
    for skew_prop, what in ((ks.get("sk"), "skew"), ((ks.get("sa")), None)):
        if what is None:
            continue
        if _is_animated(skew_prop, 1) or \
                abs(_static_value(skew_prop, 1, [0.0])[0]) > EPS:
            errors.append("skew on layer '%s' - not supported" % layer_name)


def _walk_shape_items(items, layer_name, group_affines, group_opacities,
                      blocks, errors):
    """Recurse a shape-item list. Paths accumulate per group; each fill emits
    one paint block over that group's paths (styles bind within their group).
    group_affines: composed STATIC group transforms (outer to inner);
    group_opacities: animatable group-opacity props on the chain."""
    # the group transform rides as the (by convention last) `tr` item but
    # applies to the WHOLE group: bind it before walking the paints
    for item in items:
        if item.get("ty") != "tr" or item.get("hd"):
            continue
        # static only (an animated group transform is out of subset -
        # animate the LAYER instead); group opacity MAY animate (it folds
        # into the fill alpha)
        for prop_name, dim, what in (("p", 2, "position"),
                                     ("a", 2, "anchor"),
                                     ("s", 2, "scale"),
                                     ("r", 1, "rotation")):
            prop = item.get(prop_name)
            _check_expressions(prop, layer_name, "group " + what, errors)
            if _is_animated(prop, dim):
                errors.append(
                    "animated group %s on layer '%s' - animate the "
                    "layer transform or split into layers" %
                    (what, layer_name))
        opacity = item.get("o")
        _check_expressions(opacity, layer_name, "group opacity", errors)
        if opacity is not None:
            group_opacities.append(opacity)
        group_affines.append(_static_group_affine(item))

    paths = []      # (kind, props) collected in this group, document order
    for item in items:
        ty = item.get("ty")
        name = item.get("nm", "")
        if item.get("hd") or ty == "tr":
            continue        # hidden items never render; tr is bound above
        if ty in _SHAPE_ITEM_ERRORS:
            errors.append("%s on layer '%s'%s - not supported" %
                          (_SHAPE_ITEM_ERRORS[ty], layer_name,
                           " (item '%s')" % name if name else ""))
        elif ty == "gr":
            _walk_shape_items(item.get("it", []), layer_name,
                              list(group_affines), list(group_opacities),
                              blocks, errors)
        elif ty == "sh":
            _check_expressions(item.get("ks"), layer_name, "path", errors)
            paths.append(("sh", item))
        elif ty == "el":
            for prop in (item.get("p"), item.get("s")):
                _check_expressions(prop, layer_name, "ellipse", errors)
            paths.append(("el", item))
        elif ty == "rc":
            for prop in (item.get("p"), item.get("s"), item.get("r")):
                _check_expressions(prop, layer_name, "rect", errors)
            paths.append(("rc", item))
        elif ty == "fl":
            _check_expressions(item.get("c"), layer_name, "fill colour",
                               errors)
            _check_expressions(item.get("o"), layer_name, "fill opacity",
                               errors)
            if not paths:
                continue    # a fill with nothing to style renders nothing
            blocks.append({
                "paths": list(paths),
                "fill": item,
                "affines": list(group_affines),
                "opacities": list(group_opacities),
                "layer": layer_name,
            })
        else:
            errors.append("unsupported shape item '%s' on layer '%s' - not "
                          "in the cook subset" % (ty, layer_name))


def _static_group_affine(tr):
    """A group transform as a static 2x3 affine in Lottie (y-down) space:
    translate(p) . rotate(r) . scale(s) . translate(-a)."""
    p = _static_value(tr.get("p"), 2, [0.0, 0.0])
    a = _static_value(tr.get("a"), 2, [0.0, 0.0])
    s = _static_value(tr.get("s"), 2, [100.0, 100.0])
    r = _static_value(tr.get("r"), 1, [0.0])[0]
    rad = math.radians(r)
    cos_r, sin_r = math.cos(rad), math.sin(rad)
    sx, sy = s[0] / 100.0, s[1] / 100.0
    # linear part: R . S  (y-down rotation matrix)
    la, lb = cos_r * sx, -sin_r * sy
    lc, ld = sin_r * sx, cos_r * sy
    tx = p[0] - (la * a[0] + lb * a[1])
    ty = p[1] - (lc * a[0] + ld * a[1])
    return (la, lb, lc, ld, tx, ty)


def _compose_affines(affines):
    out = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    for aff in affines:
        a0, b0, c0, d0, tx0, ty0 = out
        a1, b1, c1, d1, tx1, ty1 = aff
        out = (a0 * a1 + b0 * c1, a0 * b1 + b0 * d1,
               c0 * a1 + d0 * c1, c0 * b1 + d0 * d1,
               a0 * tx1 + b0 * ty1 + tx0, c0 * tx1 + d0 * ty1 + ty0)
    return out


def _flatten_layers(data, errors):
    """Validate every layer and inline untimed precomps: the flat layer list
    (document order preserved; precomp children replace their precomp at its
    stacking position, parented to a synthesized transform carrier)."""
    assets = {a.get("id"): a for a in data.get("assets", [])
              if isinstance(a, dict)}
    comp_ip = float(data.get("ip", 0.0))
    flat = []

    def walk(layers, prefix, offset, parent_of_root, ref_stack):
        # per-comp map: Lottie layer ind -> flat entry (for parent links)
        by_ind = {}
        for raw in layers:
            ty = raw.get("ty")
            name = _sanitize_name(raw.get("nm"),
                                  "layer%d" % len(flat))
            if prefix:
                name = prefix + "/" + name
            if raw.get("hd"):
                continue    # hidden layers never render
            if ty not in (0, 1, 3, 4):
                errors.append("unsupported %s layer '%s' - only shape, "
                              "null, solid and untimed precomp layers cook" %
                              (_LAYER_TYPE_NAMES.get(ty, "type %s" % ty),
                               name))
                continue
            if int(raw.get("ddd", 0)) == 1:
                errors.append("3D layer '%s' - only 2D layers cook" % name)
                continue
            if abs(float(raw.get("sr", 1.0)) - 1.0) > EPS:
                errors.append("time stretch on layer '%s' - not supported; "
                              "bake the retime into keyframes" % name)
                continue
            if raw.get("tm") is not None:
                errors.append("time remap on layer '%s' - not supported; "
                              "bake the retime into keyframes" % name)
                continue
            if raw.get("tt") or raw.get("td"):
                errors.append("track matte on layer '%s' - not supported" %
                              name)
                continue
            if raw.get("masksProperties"):
                errors.append("mask on layer '%s' - not supported" % name)
                continue
            if raw.get("ef"):
                errors.append("layer effects on layer '%s' - not supported" %
                              name)
                continue
            if int(raw.get("ao", 0)) == 1:
                errors.append("auto-orient on layer '%s' - not supported" %
                              name)
                continue
            ks = raw.get("ks", {})
            _validate_transform(ks, name, errors)
            entry = {
                "name": name,
                "ty": ty,
                "ks": ks,
                "offset": offset,
                "window": (float(raw.get("ip", comp_ip)) + offset,
                           float(raw.get("op", 1e30)) + offset),
                "parent": None,         # flat entry, filled below
                "blocks": [],
                "inherit_opacity": False,
            }
            parent_ind = raw.get("parent")
            if parent_ind is not None:
                if parent_ind in by_ind:
                    entry["parent"] = by_ind[parent_ind]
                else:
                    errors.append("layer '%s' parents a missing or "
                                  "unsupported layer (ind %s)" %
                                  (name, parent_ind))
            else:
                entry["parent"] = parent_of_root

            if ty == 4:
                _walk_shape_items(raw.get("shapes", []), name, [], [],
                                  entry["blocks"], errors)
            elif ty == 1:
                entry["blocks"].append(_solid_block(raw, name, errors))
            elif ty == 0:
                ref = raw.get("refId")
                asset = assets.get(ref)
                if not isinstance(asset, dict) or \
                        not isinstance(asset.get("layers"), list):
                    errors.append("precomp layer '%s' references missing "
                                  "composition '%s'" % (name, ref))
                    continue
                if ref in ref_stack:
                    errors.append("precomp layer '%s' creates a composition "
                                  "cycle ('%s')" % (name, ref))
                    continue
                # the precomp layer becomes a transform carrier whose
                # opacity multiplies down to every inlined child
                entry["inherit_opacity"] = True
                flat.append(entry)
                if raw.get("ind") is not None:
                    by_ind[raw.get("ind")] = entry
                child_offset = offset + float(raw.get("st", 0.0))
                walk(asset["layers"], name, child_offset, entry,
                     ref_stack + [ref])
                continue
            flat.append(entry)
            if raw.get("ind") is not None:
                by_ind[raw.get("ind")] = entry

    walk(data.get("layers", []), "", -comp_ip, None, [])
    return flat


def _solid_block(raw, name, errors):
    """A solid layer as one static rect block filled with its colour."""
    sw = float(raw.get("sw", 0.0))
    sh = float(raw.get("sh", 0.0))
    colour = str(raw.get("sc", "#000000")).lstrip("#")
    try:
        rgb = tuple(int(colour[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    except (ValueError, IndexError):
        errors.append("solid layer '%s' has an unreadable colour '%s'" %
                      (name, raw.get("sc")))
        rgb = (0.0, 0.0, 0.0)
    rect = {"ty": "rc",
            "p": {"a": 0, "k": [sw * 0.5, sh * 0.5]},
            "s": {"a": 0, "k": [sw, sh]},
            "r": {"a": 0, "k": 0}}
    fill = {"ty": "fl",
            "c": {"a": 0, "k": [rgb[0], rgb[1], rgb[2], 1.0]},
            "o": {"a": 0, "k": 100}}
    return {"paths": [("rc", rect)], "fill": fill, "affines": [],
            "opacities": [], "layer": name}


# ---------------------------------------------------------------------------
# channel conversion: Lottie property -> .oanim channel keys
#
# Direct mapping preserves the source's cubic value-bezier easing 1:1. When
# the runtime grammar cannot express a case (spatial position tangents, split
# x/y position, per-dimension easing, keys outside the timeline), the channel
# is DENSIFIED: sampled at every integer frame across its animated span and
# emitted as linear keys.


def _densify_frames(start, end, duration):
    """The densify frame ladder: integer frames spanning [start, end],
    clamped to the timeline, endpoints included, strictly increasing."""
    start = max(0.0, min(start, duration))
    end = max(0.0, min(end, duration))
    if end <= start + EPS:
        return [start]
    frames = [start]
    f = math.floor(start) + 1.0
    while f < end - EPS:
        if f > start + EPS:
            frames.append(f)
        f += 1.0
    frames.append(end)
    return frames


def _convert_channel(prop, dim, duration, offset, value_map,
                     split_props=None):
    """Convert one transform channel. Returns (keys, animated) where keys is
    a list of (frame, values, ease) with cooked values, or ([], False) when
    the channel is static at its source value (the caller compares against
    the channel default). value_map maps a raw Lottie value list to cooked
    values. split_props: (x_prop, y_prop) for a split position."""
    if split_props is not None:
        x_prop, y_prop = split_props
        x_anim = _is_animated(x_prop, 1)
        y_anim = _is_animated(y_prop, 1)
        if not x_anim and not y_anim:
            value = [_static_value(x_prop, 1, [0.0])[0],
                     _static_value(y_prop, 1, [0.0])[0]]
            return [(0.0, value_map(value), None)], False
        # split dimensions cannot share one easing spec: densify
        x_keys = _prop_keys(x_prop, 1) if x_anim else None
        y_keys = _prop_keys(y_prop, 1) if y_anim else None
        times = []
        for keys in (x_keys, y_keys):
            if keys:
                times += [k["t"] + offset for k in keys]
        frames = _densify_frames(min(times), max(times), duration)
        out = []
        for f in frames:
            x = _sample_keys(x_keys, f - offset, 1)[0] if x_keys else \
                _static_value(x_prop, 1, [0.0])[0]
            y = _sample_keys(y_keys, f - offset, 1)[0] if y_keys else \
                _static_value(y_prop, 1, [0.0])[0]
            out.append((f, value_map([x, y]), ("lin",)))
        return out, True

    if not _is_animated(prop, dim):
        return [(0.0, value_map(_static_value(prop, dim,
                                              [0.0] * dim)), None)], False

    keys = _prop_keys(prop, dim)
    expressible = not (dim == 2 and _has_spatial_tangents(keys))
    if expressible:
        for k in keys[:-1]:
            if _segment_ease(k, dim) is None:
                expressible = False     # per-dimension easing mismatch
                break
    if expressible:
        for k in keys:
            f = k["t"] + offset
            if f < -EPS or f > duration + EPS:
                expressible = False     # keys outside the timeline
                break
    if expressible:
        out = []
        for idx, k in enumerate(keys):
            frame = min(max(k["t"] + offset, 0.0), duration)
            ease = _segment_ease(k, dim) if idx < len(keys) - 1 else ("lin",)
            out.append((frame, value_map(k["s"]), ease))
        return out, True

    frames = _densify_frames(keys[0]["t"] + offset, keys[-1]["t"] + offset,
                             duration)
    out = [(f, value_map(_sample_keys(keys, f - offset, dim)), ("lin",))
           for f in frames]
    return out, True


def _apply_window(opacity_keys, animated, window, duration):
    """Bake a layer's in/out window into its opacity channel: 0 outside
    [win_start, win_end), hold-stepped at the boundaries. A static opacity
    stays three hold keys; an animated one is densified inside the window."""
    win_start = max(0.0, window[0])
    win_end = min(duration, window[1])
    if win_start <= EPS and win_end >= duration - EPS:
        return opacity_keys, animated
    if win_end <= win_start + EPS:
        return [(0.0, [0.0], None)], False      # never visible
    out = []
    if not animated:
        value = opacity_keys[0][1] if opacity_keys else [1.0]
        if win_start > EPS:
            out.append((0.0, [0.0], ("hold",)))
        out.append((win_start, list(value), ("hold",)))
    else:
        if win_start > EPS:
            out.append((0.0, [0.0], ("hold",)))
        # sample the authored curve per frame inside the window
        sampler_keys = opacity_keys
        trailing_zero = win_end < duration - EPS
        for f in _densify_frames(win_start, win_end, duration):
            if trailing_zero and f >= win_end - EPS:
                break
            out.append((f, list(_sample_channel_keys(sampler_keys, f)),
                        ("lin",)))
        if out and out[-1][0] < win_end - EPS:
            last = out[-1]
            out[-1] = (last[0], last[1], ("hold",))
    if win_end < duration - EPS:
        out.append((win_end, [0.0], ("hold",)))
    return out, True


def _sample_channel_keys(keys, frame):
    """Sample already-COOKED channel keys (frame, values, ease) at a frame -
    used when a window densifies an animated opacity."""
    if not keys:
        return [1.0]
    if frame <= keys[0][0]:
        return list(keys[0][1])
    if frame >= keys[-1][0]:
        return list(keys[-1][1])
    for idx in range(len(keys) - 1):
        f0, v0, ease = keys[idx]
        f1, v1, _ = keys[idx + 1]
        if f0 <= frame <= f1:
            if ease and ease[0] == "hold":
                return list(v0)
            u = (frame - f0) / max(f1 - f0, EPS)
            if ease and ease[0] == "ease":
                u = _bezier_ease(ease[1], ease[2], ease[3], ease[4], u)
            return [a + u * (b - a) for a, b in zip(v0, v1)]
    return list(keys[-1][1])


# ---------------------------------------------------------------------------
# shape blocks: paths + fill -> .oanim shape keys (regions with holes)


def _block_path_at(kind, item, frame):
    """The block path's bezier at a source-time frame (params sampled)."""
    if kind == "sh":
        prop = item.get("ks", {})
        if _is_animated_path(prop):
            keys = _path_prop_keys(prop)
            return _sample_path_keys(keys, frame)
        return _path_from_lottie(_static_path_value(prop))
    if kind == "el":
        p = _sample_prop(item.get("p"), 2, [0.0, 0.0], frame)
        s = _sample_prop(item.get("s"), 2, [0.0, 0.0], frame)
        return _path_ellipse(p, s)
    # rc
    p = _sample_prop(item.get("p"), 2, [0.0, 0.0], frame)
    s = _sample_prop(item.get("s"), 2, [0.0, 0.0], frame)
    r = _sample_prop(item.get("r"), 1, [0.0], frame)[0]
    rounded = _rect_max_radius(item) > EPS
    return _path_rect(p, s, r, rounded)


def _rect_max_radius(item):
    prop = item.get("r")
    if _is_animated(prop, 1):
        return max(abs(k["s"][0]) for k in _prop_keys(prop, 1))
    return abs(_static_value(prop, 1, [0.0])[0])


def _sample_prop(prop, dim, default, frame):
    if _is_animated(prop, dim):
        return _sample_keys(_prop_keys(prop, dim), frame, dim)
    return _static_value(prop, dim, default)


def _static_path_value(prop):
    if not isinstance(prop, dict):
        return {"v": [], "i": [], "o": [], "c": True}
    if int(prop.get("a", 0)) == 1:
        keys = _path_prop_keys(prop)
        return keys[0]["path_raw"] if keys else \
            {"v": [], "i": [], "o": [], "c": True}
    return prop.get("k", {"v": [], "i": [], "o": [], "c": True})


def _path_prop_keys(prop):
    """Normalized PATH keyframes: t, path (bezier), h, o/i easing. Lottie
    wraps the bezier value in a one-element list."""
    keys = []
    for k in prop.get("k", []):
        s = k.get("s")
        if isinstance(s, list) and s and isinstance(s[0], dict):
            s = s[0]
        if not isinstance(s, dict):
            continue
        keys.append({"t": float(k.get("t", 0.0)),
                     "path_raw": s,
                     "path": _path_from_lottie(s),
                     "h": int(k.get("h", 0)) == 1,
                     "o": k.get("o"), "i": k.get("i")})
    keys.sort(key=lambda e: e["t"])
    return keys


def _is_animated_path(prop):
    if not isinstance(prop, dict) or int(prop.get("a", 0)) != 1:
        return False
    keys = _path_prop_keys(prop)
    if len(keys) < 2:
        return False
    first = keys[0]["path"]
    return any(k["path"] != first for k in keys)


def _sample_path_keys(keys, frame):
    """Sample path keyframes: lerp the bezier control data with the eased
    time fraction (beziers are linear in control points, so this equals the
    renderer's interpolation)."""
    if frame <= keys[0]["t"]:
        return keys[0]["path"]
    if frame >= keys[-1]["t"]:
        return keys[-1]["path"]
    for idx in range(len(keys) - 1):
        k0, k1 = keys[idx], keys[idx + 1]
        if k0["t"] <= frame <= k1["t"]:
            if k0["h"]:
                return k0["path"]
            u = (frame - k0["t"]) / max(k1["t"] - k0["t"], EPS)
            ease = _segment_ease({"h": False, "o": k0.get("o"),
                                  "i": k0.get("i")}, 1)
            if ease is None:
                ease = ("lin",)
            if ease[0] == "ease":
                u = _bezier_ease(ease[1], ease[2], ease[3], ease[4], u)
            elif ease[0] == "hold":
                return k0["path"]
            if len(k0["path"]["v"]) != len(k1["path"]["v"]):
                return k0["path"]
            return _lerp_path(k0["path"], k1["path"], u)
    return keys[-1]["path"]


def _fill_alpha_props(block):
    """The animatable opacity chain of a block: the fill's own opacity plus
    every enclosing group's opacity (all 0..100)."""
    props = [block["fill"].get("o")]
    props += block["opacities"]
    return [p for p in props if p is not None]


def _sample_fill_rgba(block, frame):
    """The block's straight RGBA at a source-time frame."""
    c_prop = block["fill"].get("c")
    if _is_animated(c_prop, 4):
        keys = _prop_keys(c_prop, 4)
        raw = c_prop.get("k", [])
        comp_count = len(_as_list(raw[0].get("s", [0, 0, 0])))
        value = _sample_keys(keys, frame, 4)
    else:
        value = _static_value(c_prop, 4, [0.0, 0.0, 0.0, 1.0])
        comp_count = 4
        if isinstance(c_prop, dict) and not int(c_prop.get("a", 0)):
            comp_count = len(_as_list(c_prop.get("k", [0, 0, 0, 1])))
        elif c_prop is not None and not isinstance(c_prop, dict):
            comp_count = len(_as_list(c_prop))
    alpha = value[3] if comp_count >= 4 else 1.0
    for prop in _fill_alpha_props(block):
        alpha *= _sample_prop(prop, 1, [100.0], frame)[0] / 100.0
    clamp = lambda v: min(max(v, 0.0), 1.0)
    return (clamp(value[0]), clamp(value[1]), clamp(value[2]), clamp(alpha))


def _block_sample_times(block, duration, offset):
    """(times, eases, densified): the shape-key timeline of a block in
    OUTPUT frames. Direct when exactly one animated source exists, it is a
    'sh' path or scalar chain with expressible easing and in-range keys;
    otherwise the union span densifies to integer frames."""
    animated_paths = []
    for kind, item in block["paths"]:
        if kind == "sh":
            if _is_animated_path(item.get("ks", {})):
                animated_paths.append(("sh", item))
        else:
            for prop_name in ("p", "s", "r"):
                if _is_animated(item.get(prop_name), 2 if prop_name != "r"
                                else 1):
                    animated_paths.append((kind, item))
                    break
    colour_animated = _is_animated(block["fill"].get("c"), 4)
    alpha_animated = any(_is_animated(p, 1) for p in _fill_alpha_props(block))

    sources = len(animated_paths) + (1 if colour_animated else 0) + \
        (1 if alpha_animated else 0)
    if sources == 0:
        return [0.0], [None], False

    if sources == 1 and len(animated_paths) == 1 and \
            animated_paths[0][0] == "sh":
        keys = _path_prop_keys(animated_paths[0][1].get("ks", {}))
        ok = all(0.0 - EPS <= k["t"] + offset <= duration + EPS for k in keys)
        eases = []
        for k in keys[:-1]:
            ease = _segment_ease({"h": k["h"], "o": k.get("o"),
                                  "i": k.get("i")}, 1)
            if ease is None:
                ok = False
                break
            eases.append(ease)
        if ok and len(keys) >= 2:
            return ([k["t"] + offset for k in keys], eases + [None], False)
    elif sources == 1 and not animated_paths:
        prop = block["fill"].get("c") if colour_animated else None
        if prop is None:
            animated_alpha = [p for p in _fill_alpha_props(block)
                              if _is_animated(p, 1)]
            prop = animated_alpha[0]
        dim = 4 if colour_animated else 1
        keys = _prop_keys(prop, dim)
        ok = all(0.0 - EPS <= k["t"] + offset <= duration + EPS for k in keys)
        eases = []
        for k in keys[:-1]:
            ease = _segment_ease(k, dim)
            if ease is None:
                ok = False
                break
            eases.append(ease)
        if ok and len(keys) >= 2:
            return ([k["t"] + offset for k in keys], eases + [None], False)

    # densify: integer frames across the union of animated spans
    times = []
    for kind, item in animated_paths:
        if kind == "sh":
            keys = _path_prop_keys(item.get("ks", {}))
            times += [k["t"] + offset for k in keys]
        else:
            for prop_name, dim in (("p", 2), ("s", 2), ("r", 1)):
                prop = item.get(prop_name)
                if _is_animated(prop, dim):
                    times += [k["t"] + offset
                              for k in _prop_keys(prop, dim)]
    if colour_animated:
        times += [k["t"] + offset
                  for k in _prop_keys(block["fill"].get("c"), 4)]
    for prop in _fill_alpha_props(block):
        if _is_animated(prop, 1):
            times += [k["t"] + offset for k in _prop_keys(prop, 1)]
    frames = _densify_frames(min(times), max(times), duration)
    return frames, [("lin",)] * (len(frames) - 1) + [None], True


def _assign_holes(contours, fill_rule):
    """Containment-based hole assignment at the reference key. Returns a
    list of regions: (outer_index, [hole_indices]). evenodd: odd containment
    depth = a hole of its immediate container; nonzero: additionally the
    winding must OPPOSE the container's, else the nested contour is its own
    filled region."""
    n = len(contours)
    containers = [[] for _ in range(n)]
    for i in range(n):
        rep = contours[i][0]
        for j in range(n):
            if i != j and _point_in_polygon(rep, contours[j]):
                containers[i].append(j)
    depth = [len(c) for c in containers]
    regions = []
    region_of = {}
    order = sorted(range(n), key=lambda i: (depth[i], i))
    for i in order:
        if depth[i] % 2 == 0:
            region_of[i] = len(regions)
            regions.append((i, []))
        else:
            parent = None
            for j in containers[i]:
                if depth[j] == depth[i] - 1:
                    parent = j if parent is None else parent
            is_hole = parent is not None and parent in region_of
            if is_hole and fill_rule != 2:      # nonzero: winding must oppose
                if (_polygon_area(contours[i]) > 0) == \
                        (_polygon_area(contours[parent]) > 0):
                    is_hole = False
            if is_hole:
                regions[region_of[parent]][1].append(i)
            else:
                region_of[i] = len(regions)
                regions.append((i, []))
    return regions


def _convert_block(block, duration, offset, tol, place, errors):
    """One paint block -> a list of .oanim shape entries, each
    {"keys": [(frame, ease, fill_rgba, outer, holes)]} with fixed topology
    across keys. Vertices are placed to world (cooked) space."""
    affine = _compose_affines(block["affines"])
    times, eases, _densified = _block_sample_times(block, duration, offset)

    # worst-case flattening resolution: gather every path's bezier at every
    # source KEY time (interpolated paths are lerps of key paths, so key
    # curvature bounds them), transformed by the static group affine
    per_path_counts = []
    for kind, item in block["paths"]:
        key_times = set()
        if kind == "sh" and _is_animated_path(item.get("ks", {})):
            key_times = {k["t"] for k in _path_prop_keys(item.get("ks", {}))}
        else:
            for prop_name, dim in (("p", 2), ("s", 2), ("r", 1)):
                prop = item.get(prop_name) if kind != "sh" else None
                if prop is not None and _is_animated(prop, dim):
                    key_times |= {k["t"] for k in _prop_keys(prop, dim)}
        if not key_times:
            key_times = {times[0] - offset}
        beziers = [_transform_path(_block_path_at(kind, item, t), affine)
                   for t in sorted(key_times)]
        if len(set(len(b["v"]) for b in beziers)) > 1:
            errors.append("path keyframes with differing vertex counts on "
                          "layer '%s' - every key must share one path "
                          "structure" % block["layer"])
            return []
        if len(beziers[0]["v"]) < 3 and kind == "sh":
            errors.append("a path with fewer than 3 vertices on layer '%s'" %
                          block["layer"])
            return []
        per_path_counts.append(_edge_segment_counts(beziers, tol))

    # flatten every path at every emitted time (fixed counts = fixed verts)
    contours_per_time = []
    for t in times:
        contours = []
        for (kind, item), counts in zip(block["paths"], per_path_counts):
            path = _transform_path(_block_path_at(kind, item, t - offset),
                                   affine)
            contours.append(_flatten_path(path, counts))
        contours_per_time.append(contours)

    # hole assignment from the FIRST key's geometry, reused at every key
    # (the fixed-topology law: structure never changes mid-animation)
    fill_rule = int(_static_value(block["fill"].get("r"), 1, [1.0])[0])
    regions = _assign_holes(contours_per_time[0], fill_rule)

    entries = []
    for outer_idx, hole_indices in regions:
        if len(contours_per_time[0][outer_idx]) < 3:
            continue
        keys = []
        for t_idx, t in enumerate(times):
            rgba = _sample_fill_rgba(block, t - offset)
            outer = [place(p) for p in contours_per_time[t_idx][outer_idx]]
            holes = [[place(p) for p in contours_per_time[t_idx][h]]
                     for h in hole_indices if
                     len(contours_per_time[0][h]) >= 3]
            keys.append((t, eases[t_idx], rgba, outer, holes))
        entries.append({"keys": keys})
    return entries


# ---------------------------------------------------------------------------
# rig assembly: transform carriers first, paint layers in paint order
#
# The .oanim grammar requires every parent to PRECEDE its children while the
# file order IS the paint order. Lottie parenting only inherits transforms
# (opacity stays per-layer) and a parent may paint above its child, so the
# two orders can conflict. The cook resolves both structurally: every layer
# that is referenced as a parent contributes a TRANSFORM CARRIER (a null; no
# opacity except for inlined precomps, whose opacity legitimately multiplies
# down to their children), emitted first in hierarchy order; paint layers
# follow in paint order (Lottie lists top-first, so reversed), each parented
# to its carrier or its parent's carrier.


def _layer_channels(entry, duration, scale):
    """The cooked transform channels of a flat layer: dict name ->
    (keys, animated). Values are converted to cooked space (y flip, scale
    /100, rotation negated to CCW, opacity /100)."""
    ks = entry["ks"] if isinstance(entry["ks"], dict) else {}
    offset = entry["offset"]
    place_vec = lambda v: [v[0] * scale, -v[1] * scale]
    channels = {}
    p_prop = ks.get("p")
    if isinstance(p_prop, dict) and p_prop.get("s"):
        channels["pos"] = _convert_channel(
            None, 2, duration, offset, place_vec,
            split_props=(p_prop.get("x"), p_prop.get("y")))
    else:
        channels["pos"] = _convert_channel(p_prop, 2, duration, offset,
                                           place_vec)
    channels["anchor"] = _convert_channel(ks.get("a"), 2, duration, offset,
                                          place_vec)
    scale_prop = ks.get("s")
    channels["scale"] = _convert_channel(
        scale_prop if scale_prop is not None else {"a": 0, "k": [100, 100]},
        2, duration, offset, lambda v: [v[0] / 100.0, v[1] / 100.0])
    channels["rot"] = _convert_channel(ks.get("r"), 1, duration, offset,
                                       lambda v: [-v[0]])
    opacity_prop = ks.get("o")
    channels["opacity"] = _convert_channel(
        opacity_prop if opacity_prop is not None else {"a": 0, "k": 100},
        1, duration, offset,
        lambda v: [min(max(v[0] / 100.0, 0.0), 1.0)])
    return channels


_CHANNEL_DEFAULTS = {
    "pos": [0.0, 0.0], "anchor": [0.0, 0.0], "scale": [1.0, 1.0],
    "rot": [0.0], "opacity": [1.0],
}


def _channel_is_default(name, converted):
    keys, animated = converted
    if animated:
        return False
    if not keys:
        return True
    value = keys[0][1]
    default = _CHANNEL_DEFAULTS[name]
    return all(abs(a - b) <= 1e-5 for a, b in zip(value, default))


def _build_rig(flat, duration, comp_w, comp_h, scale, tol, errors):
    """The emit-ready rig: a list of layer dicts {name, parent (emit index),
    channels, shapes} honouring both grammar order constraints."""
    parent_refs = set()
    for entry in flat:
        if entry["parent"] is not None:
            parent_refs.add(id(entry["parent"]))
    # carriers: every referenced layer, plus its ancestors (closure)
    carriers = []
    carrier_ids = set()

    def need_carrier(entry):
        if id(entry) in carrier_ids:
            return
        if entry["parent"] is not None:
            need_carrier(entry["parent"])
        carrier_ids.add(id(entry))
        carriers.append(entry)

    for entry in flat:
        if id(entry) in parent_refs:
            need_carrier(entry)

    emit = []
    emit_index = {}         # id(flat entry) -> carrier emit index

    # the synthetic world root: centers the composition on the origin
    root_pos = [-(comp_w * 0.5) * scale, (comp_h * 0.5) * scale]
    emit.append({"name": "comp", "parent": -1,
                 "channels": {"pos": ([(0.0, root_pos, None)], False)},
                 "shapes": []})

    for entry in carriers:
        channels = _layer_channels(entry, duration, scale)
        kept = {"pos": channels["pos"], "anchor": channels["anchor"],
                "scale": channels["scale"], "rot": channels["rot"]}
        if entry["inherit_opacity"]:
            kept["opacity"] = _apply_window(channels["opacity"][0],
                                            channels["opacity"][1],
                                            entry["window"], duration)
        parent = emit_index.get(id(entry["parent"]), 0) \
            if entry["parent"] is not None else 0
        emit_index[id(entry)] = len(emit)
        emit.append({"name": entry["name"], "parent": parent,
                     "channels": kept, "shapes": []})

    place = lambda p: (p[0] * scale, -p[1] * scale)
    for entry in reversed(flat):    # bottom paint first
        if not entry["blocks"]:
            continue
        shapes = []
        for block in entry["blocks"]:
            shapes += _convert_block(block, duration, entry["offset"], tol,
                                     place, errors)
        shapes.reverse()    # shape items list top-first, like layers
        split = id(entry) in carrier_ids
        if split:
            channels = _layer_channels(entry, duration, scale)
            kept = {"opacity": _apply_window(channels["opacity"][0],
                                             channels["opacity"][1],
                                             entry["window"], duration)}
            parent = emit_index[id(entry)]
            name = entry["name"] + "_paint"
        else:
            channels = _layer_channels(entry, duration, scale)
            kept = {"pos": channels["pos"], "anchor": channels["anchor"],
                    "scale": channels["scale"], "rot": channels["rot"],
                    "opacity": _apply_window(channels["opacity"][0],
                                             channels["opacity"][1],
                                             entry["window"], duration)}
            parent = emit_index.get(id(entry["parent"]), 0) \
                if entry["parent"] is not None else 0
            name = entry["name"]
        emit.append({"name": name, "parent": parent, "channels": kept,
                     "shapes": shapes})
    return emit


# ---------------------------------------------------------------------------
# clips


def _build_clips(data, comp_ip, duration, clips_override, errors):
    """Named clips: `--clips` overrides markers; a marker comment's `#once`
    suffix makes a one-shot; a zero-duration marker extends to the next
    marker (or the end). No clips at all = the parser's implicit default."""
    if clips_override:
        clips = []
        for spec in clips_override.split(","):
            parts = spec.strip().split(":")
            if len(parts) < 3:
                errors.append("bad --clips entry '%s' (want "
                              "name:start:end[:loop|once])" % spec)
                continue
            name = _sanitize_name(parts[0], "")
            try:
                start = float(parts[1]) - comp_ip
                end = float(parts[2]) - comp_ip
            except ValueError:
                errors.append("bad --clips frames in '%s'" % spec)
                continue
            loop = True
            if len(parts) >= 4:
                if parts[3] == "once":
                    loop = False
                elif parts[3] != "loop":
                    errors.append("bad --clips mode '%s' (loop|once)" %
                                  parts[3])
                    continue
            clips.append((name, start, end, loop))
    else:
        markers = sorted((m for m in data.get("markers", [])
                          if isinstance(m, dict)),
                         key=lambda m: float(m.get("tm", 0.0)))
        clips = []
        for idx, marker in enumerate(markers):
            comment = str(marker.get("cm", "")).strip()
            loop = True
            if comment.endswith("#once"):
                loop = False
                comment = comment[:-len("#once")].strip()
            name = _sanitize_name(comment, "clip%d" % idx)
            start = float(marker.get("tm", 0.0)) - comp_ip
            length = float(marker.get("dr", 0.0))
            if length > EPS:
                end = start + length
            elif idx + 1 < len(markers):
                end = float(markers[idx + 1].get("tm", 0.0)) - comp_ip
            else:
                end = duration
            clips.append((name, start, end, loop))

    seen = set()
    out = []
    for name, start, end, loop in clips:
        base = name
        n = 2
        while name in seen:     # duplicate marker names get a suffix
            name = "%s_%d" % (base, n)
            n += 1
        seen.add(name)
        start = max(0.0, start)
        end = min(duration, end)
        if end <= start + EPS:
            errors.append("clip '%s' has an empty frame range (%g..%g) - "
                          "give the marker a duration or fix --clips" %
                          (base, start, end))
            continue
        out.append((name, start, end, loop))
    return out


# ---------------------------------------------------------------------------
# emission


def _fmt_frame(f):
    f = round(f, 4)
    return "%g" % (abs(f) if f == 0.0 else f)


def _fmt_val(v):
    if abs(v) < 5e-6:
        v = 0.0     # canonical zero (never -0.00000)
    return "%.5f" % v


def _fmt_ease(ease):
    if ease is None or ease[0] == "lin":
        return ""
    if ease[0] == "hold":
        return " hold"
    return " ease %.4f %.4f %.4f %.4f" % ease[1:]


def _emit_oanim(fps, duration, clips, rig):
    lines = ["# orkige vector animation v1 - cooked from Lottie JSON by "
             "Util/cook_vector_anim.py",
             "version 1",
             "fps %s" % _fmt_frame(fps),
             "duration %s" % _fmt_frame(duration)]
    for name, start, end, loop in clips:
        lines.append("clip %s %s %s %s" % (name, _fmt_frame(start),
                                           _fmt_frame(end),
                                           "loop" if loop else "once"))
    for layer in rig:
        lines.append("layer %s parent %d" % (layer["name"], layer["parent"]))
        for channel in ("pos", "anchor", "scale", "rot", "opacity"):
            converted = layer["channels"].get(channel)
            if converted is None or _channel_is_default(channel, converted):
                continue
            keys, _animated = converted
            lines.append("  %s k %d" % (channel, len(keys)))
            for frame, values, ease in keys:
                lines.append("    kf %s %s%s" % (
                    _fmt_frame(frame),
                    " ".join(_fmt_val(v) for v in values),
                    _fmt_ease(ease)))
        for shape in layer["shapes"]:
            lines.append("  shape k %d" % len(shape["keys"]))
            for frame, ease, rgba, outer, holes in shape["keys"]:
                lines.append("    kf %s%s" % (_fmt_frame(frame),
                                              _fmt_ease(ease)))
                lines.append("      fill %.4f %.4f %.4f %.4f" % rgba)
                lines.append("      contour %d" % len(outer))
                for p in outer:
                    lines.append("      v %s %s" % (_fmt_val(p[0]),
                                                    _fmt_val(p[1])))
                for hole in holes:
                    lines.append("      hole %d" % len(hole))
                    for p in hole:
                        lines.append("      v %s %s" % (_fmt_val(p[0]),
                                                        _fmt_val(p[1])))
    return "\n".join(lines) + "\n"


def _rig_is_static(rig):
    for layer in rig:
        for converted in layer["channels"].values():
            if converted[1]:
                return False
        for shape in layer["shapes"]:
            if len(shape["keys"]) > 1:
                return False
    return True


def _emit_oshape_static(rig):
    """The static case: compose every layer's frame-0 transform chain and
    write a plain .oshape (fill/contour/hole vocabulary, world space)."""
    affines = []
    opacities = []
    lines = ["# orkige vector shape v1 - cooked from a static Lottie "
             "document by Util/cook_vector_anim.py",
             "version 1"]
    emitted = 0
    for layer in rig:
        def channel_value(name):
            converted = layer["channels"].get(name)
            if converted is None or not converted[0]:
                return list(_CHANNEL_DEFAULTS[name])
            return list(converted[0][0][1])
        pos = channel_value("pos")
        anchor = channel_value("anchor")
        scale_v = channel_value("scale")
        rot = math.radians(channel_value("rot")[0])
        opacity = channel_value("opacity")[0]
        cos_r, sin_r = math.cos(rot), math.sin(rot)
        # +y-up CCW rotation composed with scale, anchored (RenderMath space)
        la, lb = cos_r * scale_v[0], -sin_r * scale_v[1]
        lc, ld = sin_r * scale_v[0], cos_r * scale_v[1]
        tx = pos[0] - (la * anchor[0] + lb * anchor[1])
        ty = pos[1] - (lc * anchor[0] + ld * anchor[1])
        local = (la, lb, lc, ld, tx, ty)
        parent = layer["parent"]
        world = _compose_affines([affines[parent], local]) \
            if parent >= 0 else local
        world_opacity = (opacities[parent] if parent >= 0 else 1.0) * opacity
        affines.append(world)
        opacities.append(world_opacity)

        a, b, c, d, wtx, wty = world
        def xf(p):
            return (a * p[0] + b * p[1] + wtx, c * p[0] + d * p[1] + wty)
        for shape in layer["shapes"]:
            _frame, _ease, rgba, outer, holes = shape["keys"][0]
            lines.append("fill %.4f %.4f %.4f %.4f" %
                         (rgba[0], rgba[1], rgba[2],
                          rgba[3] * world_opacity))
            lines.append("contour %d" % len(outer))
            for p in outer:
                wx, wy = xf(p)
                lines.append("v %s %s" % (_fmt_val(wx), _fmt_val(wy)))
            for hole in holes:
                lines.append("hole %d" % len(hole))
                for p in hole:
                    wx, wy = xf(p)
                    lines.append("v %s %s" % (_fmt_val(wx), _fmt_val(wy)))
            emitted += 1
    if emitted == 0:
        raise CookError("the document has no fillable shapes")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# top level


def cook(lottie_text, extent=2.0, tolerance=None, clips_override=None):
    """Cook Lottie JSON text. Returns (kind, text): kind is "oanim" for an
    animated document or "oshape" when nothing animates (the static case).
    Raises CookError listing EVERY unsupported feature, per layer."""
    try:
        data = json.loads(lottie_text)
    except json.JSONDecodeError as exc:
        raise CookError("not valid JSON: %s" % exc)
    if not isinstance(data, dict) or not isinstance(data.get("layers"), list):
        raise CookError("not a Lottie document (no layer list)")

    fps = float(data.get("fr", 0.0))
    comp_ip = float(data.get("ip", 0.0))
    comp_op = float(data.get("op", 0.0))
    comp_w = float(data.get("w", 0.0))
    comp_h = float(data.get("h", 0.0))
    duration = comp_op - comp_ip
    errors = []
    if fps <= 0.0:
        errors.append("frame rate (fr) must be > 0")
    if duration <= 0.0:
        errors.append("empty timeline (op must be greater than ip)")
    if comp_w <= 0.0 or comp_h <= 0.0:
        errors.append("composition size (w/h) must be > 0")
    if errors:
        raise CookError("\n".join(errors))

    span = max(comp_w, comp_h)
    scale = extent / span
    # flatten tolerance in composition units: 0.25% of the larger extent
    tol = tolerance if tolerance is not None else span * 0.0025

    flat = _flatten_layers(data, errors)
    clips = _build_clips(data, comp_ip, duration, clips_override, errors)
    if not errors:
        rig = _build_rig(flat, duration, comp_w, comp_h, scale, tol, errors)
    if errors:
        raise CookError("\n".join(errors))
    if not any(layer["shapes"] for layer in rig):
        raise CookError("the document has no fillable shapes")

    if _rig_is_static(rig):
        return "oshape", _emit_oshape_static(rig)
    return "oanim", _emit_oanim(fps, duration, clips, rig)


# ---------------------------------------------------------------------------
# selftest


def _parse_oanim(text):
    """A tiny strict .oanim re-parser mirroring VectorAnimAsset (selftest
    only): enforces the same grammar rules, so a malformed emission fails
    HERE rather than at engine load."""
    doc = {"fps": None, "duration": None, "clips": [], "layers": []}
    state = {"channel": None, "chan_left": 0, "chan_dim": 0,
             "shape": None, "shape_left": 0, "key": None,
             "target": None, "verts_left": 0}

    def close_key():
        key = state["key"]
        if key is not None:
            assert state["verts_left"] == 0, "truncated vertex run"
            assert key["fill"] is not None and len(key["outer"]) >= 3
            first = state["shape"]["keys"][0]
            assert len(key["outer"]) == len(first["outer"]), "topology"
            assert [len(h) for h in key["holes"]] == \
                [len(h) for h in first["holes"]], "hole topology"
        state["key"] = None
        state["target"] = None

    def close_all():
        assert state["chan_left"] == 0, "truncated channel run"
        close_key()
        assert state["shape_left"] == 0, "missing shape keys"
        state["channel"] = None
        state["shape"] = None

    def parse_ease(tokens):
        if not tokens:
            return ("lin",)
        if tokens[0] == "lin":
            return ("lin",)
        if tokens[0] == "hold":
            return ("hold",)
        assert tokens[0] == "ease" and len(tokens) == 5, "bad easing"
        return ("ease",) + tuple(float(t) for t in tokens[1:5])

    for raw in text.splitlines():
        tokens = raw.split("#", 1)[0].split()
        if not tokens:
            continue
        word = tokens[0]
        if word == "v":
            assert state["target"] is not None and state["verts_left"] > 0
            state["target"].append((float(tokens[1]), float(tokens[2])))
            state["verts_left"] -= 1
        elif word == "kf":
            if state["channel"] is not None:
                frame = float(tokens[1])
                dim = state["chan_dim"]
                values = [float(t) for t in tokens[2:2 + dim]]
                ease = parse_ease(tokens[2 + dim:])
                assert 0.0 <= frame <= doc["duration"], "frame out of range"
                keys = state["channel"]
                assert not keys or frame > keys[-1][0], "frames must rise"
                keys.append((frame, values, ease))
                state["chan_left"] -= 1
                if state["chan_left"] == 0:
                    state["channel"] = None
            else:
                assert state["shape"] is not None, "stray kf"
                close_key()
                assert state["shape_left"] > 0, "extra shape key"
                frame = float(tokens[1])
                ease = parse_ease(tokens[2:])
                assert 0.0 <= frame <= doc["duration"]
                keys = state["shape"]["keys"]
                assert not keys or frame > keys[-1]["frame"]
                key = {"frame": frame, "ease": ease, "fill": None,
                       "outer": [], "holes": []}
                keys.append(key)
                state["key"] = key
                state["shape_left"] -= 1
        elif word == "fill":
            key = state["key"]
            assert key is not None and key["fill"] is None
            key["fill"] = tuple(float(t) for t in tokens[1:5])
        elif word == "contour":
            key = state["key"]
            assert key is not None and key["fill"] is not None and \
                not key["outer"]
            state["verts_left"] = int(tokens[1])
            assert state["verts_left"] > 0
            state["target"] = key["outer"]
        elif word == "hole":
            key = state["key"]
            assert key is not None and key["outer"] and \
                state["verts_left"] == 0
            key["holes"].append([])
            state["verts_left"] = int(tokens[1])
            state["target"] = key["holes"][-1]
        elif word in ("pos", "anchor", "scale", "rot", "opacity"):
            close_all()
            assert doc["layers"], "channel before any layer"
            layer = doc["layers"][-1]
            assert tokens[1] == "k" and word not in layer["channels"]
            layer["channels"][word] = []
            state["channel"] = layer["channels"][word]
            state["chan_dim"] = 1 if word in ("rot", "opacity") else 2
            state["chan_left"] = int(tokens[2])
        elif word == "shape":
            close_all()
            assert doc["layers"] and tokens[1] == "k"
            shape = {"keys": []}
            doc["layers"][-1]["shapes"].append(shape)
            state["shape"] = shape
            state["shape_left"] = int(tokens[2])
        elif word == "layer":
            close_all()
            assert doc["fps"] and doc["duration"], "header incomplete"
            assert tokens[2] == "parent"
            parent = int(tokens[3])
            assert -1 <= parent < len(doc["layers"]), "bad parent"
            doc["layers"].append({"name": tokens[1], "parent": parent,
                                  "channels": {}, "shapes": []})
        elif word == "clip":
            close_all()
            assert not doc["layers"], "clips live in the header"
            start, end = float(tokens[2]), float(tokens[3])
            assert 0.0 <= start < end <= doc["duration"], "bad clip range"
            assert tokens[4] in ("loop", "once")
            assert tokens[1] not in [c[0] for c in doc["clips"]]
            doc["clips"].append((tokens[1], start, end, tokens[4]))
        elif word == "fps":
            doc["fps"] = float(tokens[1])
        elif word == "duration":
            doc["duration"] = float(tokens[1])
        elif word == "version":
            assert int(tokens[1]) == 1
        else:
            raise AssertionError("unknown keyword '%s'" % word)
    close_all()
    assert doc["fps"] and doc["duration"] and doc["layers"]
    return doc


def _fx_static(value):
    return {"a": 0, "k": value}


def _fx_ks(p=None, a=None, s=None, r=None, o=None):
    return {"p": p if p is not None else _fx_static([0, 0]),
            "a": a if a is not None else _fx_static([0, 0]),
            "s": s if s is not None else _fx_static([100, 100]),
            "r": r if r is not None else _fx_static(0),
            "o": o if o is not None else _fx_static(100)}


def _fx_ellipse_group(fill=(0.9, 0.42, 0.38, 1), size=(60, 80), fill_o=None,
                      group_tr=None):
    items = [{"ty": "el", "p": _fx_static([0, 0]),
              "s": _fx_static(list(size))},
             {"ty": "fl", "c": _fx_static(list(fill)),
              "o": fill_o if fill_o is not None else _fx_static(100)}]
    if group_tr is not None:
        items.append(group_tr)
    return {"ty": "gr", "nm": "blob", "it": items}


def _fx_doc(layers, markers=None, assets=None, fr=30, ip=0, op=60,
            w=200, h=200):
    doc = {"v": "5.7.0", "fr": fr, "ip": ip, "op": op, "w": w, "h": h,
           "layers": layers}
    if markers is not None:
        doc["markers"] = markers
    if assets is not None:
        doc["assets"] = assets
    return json.dumps(doc)


def _fx_shape_layer(name, ind, shapes, ks=None, parent=None, **extra):
    layer = {"ty": 4, "nm": name, "ind": ind,
             "ks": ks if ks is not None else _fx_ks(),
             "shapes": shapes}
    if parent is not None:
        layer["parent"] = parent
    layer.update(extra)
    return layer


def _expect_error(fixture_text, *needles, **kwargs):
    try:
        cook(fixture_text, **kwargs)
    except CookError as exc:
        message = str(exc)
        for needle in needles:
            assert needle in message, \
                "error should name '%s', got: %s" % (needle, message)
        return
    raise AssertionError("expected a cook error naming %s" % (needles,))


def _layer_by_name(doc, name):
    for layer in doc["layers"]:
        if layer["name"] == name:
            return layer
    raise AssertionError("no layer '%s' in %s" %
                         (name, [l["name"] for l in doc["layers"]]))


def _selftest():
    checks = 0

    # --- 1: transforms, parenting, easing, markers -> clips ----------------
    bez = {"o": {"x": 0.42, "y": 0}, "i": {"x": 0.58, "y": 1}}
    fixture = _fx_doc(
        layers=[
            {"ty": 3, "nm": "rig root", "ind": 1,
             "ks": _fx_ks(p=_fx_static([100, 100]),
                          r={"a": 1, "k": [
                              dict(t=0, s=[0], **bez),
                              dict(t=30, s=[20], **bez),
                              {"t": 60, "s": [0]}]})},
            _fx_shape_layer("body", 2, [_fx_ellipse_group()], parent=1,
                            ks=_fx_ks(p={"a": 1, "k": [
                                dict(t=0, s=[0, 0], **bez),
                                {"t": 60, "s": [0, -20]}]})),
        ],
        markers=[{"tm": 0, "cm": "idle", "dr": 30},
                 {"tm": 30, "cm": "walk #once", "dr": 30}])
    kind, text = cook(fixture)
    assert kind == "oanim"
    doc = _parse_oanim(text)
    assert doc["fps"] == 30 and doc["duration"] == 60
    assert doc["clips"] == [("idle", 0.0, 30.0, "loop"),
                            ("walk", 30.0, 60.0, "once")]
    comp = doc["layers"][0]
    assert comp["name"] == "comp" and comp["parent"] == -1
    assert comp["channels"]["pos"][0][1] == [-1.0, 1.0]     # centering
    rig = _layer_by_name(doc, "rig_root")
    assert rig["channels"]["pos"][0][1] == [1.0, -1.0]      # y flip + scale
    rot = rig["channels"]["rot"]
    assert [k[1][0] for k in rot] == [0.0, -20.0, 0.0]      # CCW negation
    assert rot[0][2] == ("ease", 0.42, 0.0, 0.58, 1.0)      # bezier kept
    body = _layer_by_name(doc, "body")
    assert body["parent"] == doc["layers"].index(rig)
    assert body["channels"]["pos"][-1][1] == [0.0, 0.2]     # -20 px -> +0.2
    ellipse = body["shapes"][0]["keys"][0]
    assert len(ellipse["outer"]) >= 12
    assert abs(ellipse["fill"][0] - 0.9) < 1e-3
    assert max(abs(p[0]) for p in ellipse["outer"]) <= 0.301
    checks += 1

    # --- 2: path shape keyframes (direct, fixed vertex counts) -------------
    square0 = {"c": True, "v": [[0, 0], [40, 0], [40, 40], [0, 40]],
               "i": [[0, 0]] * 4, "o": [[0, 0]] * 4}
    square1 = {"c": True, "v": [[0, 0], [60, 0], [60, 40], [0, 40]],
               "i": [[0, 0]] * 4, "o": [[0, 0]] * 4}
    fixture = _fx_doc(layers=[_fx_shape_layer("morpher", 1, [
        {"ty": "gr", "it": [
            {"ty": "sh", "ks": {"a": 1, "k": [
                dict(t=0, s=[square0], **bez), {"t": 60, "s": [square1]}]}},
            {"ty": "fl", "c": _fx_static([0.2, 0.8, 0.2, 1]),
             "o": _fx_static(100)}]}])])
    doc = _parse_oanim(cook(fixture)[1])
    shape = _layer_by_name(doc, "morpher")["shapes"][0]
    assert len(shape["keys"]) == 2
    assert shape["keys"][0]["ease"] == ("ease", 0.42, 0.0, 0.58, 1.0)
    assert len(shape["keys"][0]["outer"]) == len(shape["keys"][1]["outer"])
    assert shape["keys"][0]["outer"] != shape["keys"][1]["outer"]
    checks += 1

    # --- 3: animated fill colour / animated group opacity ------------------
    lin = {"o": {"x": 0.33, "y": 0.33}, "i": {"x": 0.67, "y": 0.67}}
    fixture = _fx_doc(layers=[_fx_shape_layer("tint", 1, [
        {"ty": "gr", "it": [
            {"ty": "el", "p": _fx_static([0, 0]), "s": _fx_static([50, 50])},
            {"ty": "fl", "c": {"a": 1, "k": [
                dict(t=0, s=[1, 0, 0], **lin), {"t": 60, "s": [0, 0, 1]}]},
             "o": _fx_static(100)}]}])])
    doc = _parse_oanim(cook(fixture)[1])
    keys = _layer_by_name(doc, "tint")["shapes"][0]["keys"]
    assert len(keys) == 2 and keys[0]["ease"] == ("lin",)
    assert keys[0]["fill"][:3] == (1.0, 0.0, 0.0)
    assert keys[1]["fill"][:3] == (0.0, 0.0, 1.0)
    assert keys[0]["outer"] == keys[1]["outer"]     # geometry static
    fixture = _fx_doc(layers=[_fx_shape_layer("fader", 1, [
        _fx_ellipse_group(group_tr={
            "ty": "tr", "p": _fx_static([0, 0]), "a": _fx_static([0, 0]),
            "s": _fx_static([100, 100]), "r": _fx_static(0),
            "o": {"a": 1, "k": [dict(t=0, s=[100], **lin),
                                {"t": 60, "s": [0]}]}})])])
    doc = _parse_oanim(cook(fixture)[1])
    keys = _layer_by_name(doc, "fader")["shapes"][0]["keys"]
    assert keys[0]["fill"][3] == 1.0 and keys[-1]["fill"][3] == 0.0
    checks += 1

    # --- 4: spatial position tangents densify to per-frame keys ------------
    fixture = _fx_doc(layers=[_fx_shape_layer(
        "swinger", 1, [_fx_ellipse_group()],
        ks=_fx_ks(p={"a": 1, "k": [
            dict(t=0, s=[0, 0], to=[40, 0], ti=[0, 0], **lin),
            {"t": 30, "s": [0, -40]}]}))])
    doc = _parse_oanim(cook(fixture)[1])
    pos = _layer_by_name(doc, "swinger")["channels"]["pos"]
    assert len(pos) > 10                    # densified
    assert all(k[2] == ("lin",) for k in pos[:-1])
    mid = pos[len(pos) // 2][1]
    assert mid[0] > 0.02                    # the spatial bow (to the right)
    checks += 1

    # --- 5: split x/y position densifies ----------------------------------
    fixture = _fx_doc(layers=[_fx_shape_layer(
        "splitter", 1, [_fx_ellipse_group()],
        ks=_fx_ks(p={"s": True,
                     "x": {"a": 1, "k": [dict(t=0, s=[0], **lin),
                                         {"t": 30, "s": [50]}]},
                     "y": _fx_static(20)}))])
    doc = _parse_oanim(cook(fixture)[1])
    pos = _layer_by_name(doc, "splitter")["channels"]["pos"]
    assert len(pos) > 10
    assert all(abs(k[1][1] + 0.2) < 1e-4 for k in pos)      # y constant
    checks += 1

    # --- 6: rect topology (sharp 4 verts; animated radius keeps 8-vert) ----
    fixture = _fx_doc(layers=[_fx_shape_layer("box", 1, [
        {"ty": "gr", "it": [
            {"ty": "rc", "p": _fx_static([0, 0]), "s": _fx_static([80, 40]),
             "r": _fx_static(0)},
            {"ty": "fl", "c": _fx_static([0, 0, 0, 1]),
             "o": _fx_static(100)}]}]),
        _fx_shape_layer("pill", 2, [
            {"ty": "gr", "it": [
                {"ty": "rc", "p": _fx_static([0, 0]),
                 "s": _fx_static([80, 40]),
                 "r": {"a": 1, "k": [dict(t=0, s=[0], **lin),
                                     {"t": 60, "s": [20]}]}},
                {"ty": "fl", "c": _fx_static([0, 0, 0, 1]),
                 "o": _fx_static(100)}]}])])
    doc = _parse_oanim(cook(fixture)[1])
    assert len(_layer_by_name(doc, "box")["shapes"][0]["keys"][0]["outer"]) \
        == 4
    pill_keys = _layer_by_name(doc, "pill")["shapes"][0]["keys"]
    assert len(pill_keys) > 10              # animated rc densifies
    counts = {len(k["outer"]) for k in pill_keys}
    assert len(counts) == 1 and counts.pop() > 4    # fixed rounded topology
    checks += 1

    # --- 7: solid layer -> coloured rect -----------------------------------
    fixture = _fx_doc(layers=[
        {"ty": 1, "nm": "backdrop", "ind": 1, "ks": _fx_ks(
            o={"a": 1, "k": [dict(t=0, s=[100], **lin),
                             {"t": 60, "s": [0]}]}),
         "sc": "#3366cc", "sw": 100, "sh": 50}])
    doc = _parse_oanim(cook(fixture)[1])
    solid = _layer_by_name(doc, "backdrop")
    key = solid["shapes"][0]["keys"][0]
    assert len(key["outer"]) == 4
    assert abs(key["fill"][0] - 0.2) < 1e-3 and \
        abs(key["fill"][2] - 0.8) < 1e-3
    assert len(solid["channels"]["opacity"]) == 2   # animated layer opacity
    checks += 1

    # --- 8: nested untimed precomps inline (offset, opacity inherits) ------
    inner_anim = {"a": 1, "k": [dict(t=0, s=[0, 0], **lin),
                                {"t": 54, "s": [0, -10]}]}
    fixture = _fx_doc(
        layers=[{"ty": 0, "nm": "child", "ind": 1, "refId": "pc1", "st": 6,
                 "w": 100, "h": 100,
                 "ks": _fx_ks(p=_fx_static([100, 100]),
                              o=_fx_static(50))}],
        assets=[{"id": "pc1", "layers": [
            {"ty": 0, "nm": "grand", "ind": 1, "refId": "pc2",
             "ks": _fx_ks()},
        ]},
            {"id": "pc2", "layers": [
                _fx_shape_layer("leaf", 1, [_fx_ellipse_group()],
                                ks=_fx_ks(p=inner_anim))]}])
    doc = _parse_oanim(cook(fixture)[1])
    child = _layer_by_name(doc, "child")
    assert abs(child["channels"]["opacity"][0][1][0] - 0.5) < 1e-4
    leaf = _layer_by_name(doc, "child/grand/leaf")
    frames = [k[0] for k in leaf["channels"]["pos"]]
    assert frames == [6.0, 60.0]            # st offset applied
    checks += 1

    # --- 9: hole assignment (evenodd -> hole; nonzero same-winding -> two
    # regions) and the static document cooking to a plain .oshape -----------
    outer_sq = {"c": True, "v": [[50, 50], [150, 50], [150, 150], [50, 150]],
                "i": [[0, 0]] * 4, "o": [[0, 0]] * 4}
    inner_sq = {"c": True, "v": [[75, 75], [125, 75], [125, 125], [75, 125]],
                "i": [[0, 0]] * 4, "o": [[0, 0]] * 4}
    def ring_fixture(rule):
        return _fx_doc(layers=[_fx_shape_layer("ring", 1, [
            {"ty": "gr", "it": [
                {"ty": "sh", "ks": _fx_static(outer_sq)},
                {"ty": "sh", "ks": _fx_static(inner_sq)},
                {"ty": "fl", "c": _fx_static([0.5, 0.3, 0.1, 1]),
                 "o": _fx_static(100), "r": rule}]}])])
    kind, text = cook(ring_fixture(2))
    assert kind == "oshape"                 # nothing animates
    assert "layer" not in text and "hole 4" in text
    assert text.count("fill") == 1
    kind, text = cook(ring_fixture(1))      # same winding: no hole
    assert kind == "oshape" and "hole" not in text
    assert text.count("fill") == 2
    checks += 1

    # --- 10: in/out window bakes into the opacity channel ------------------
    fixture = _fx_doc(layers=[
        _fx_shape_layer("early", 1, [_fx_ellipse_group()], ip=10, op=40,
                        ks=_fx_ks(p={"a": 1, "k": [
                            dict(t=0, s=[0, 0], **lin),
                            {"t": 60, "s": [10, 0]}]}))])
    doc = _parse_oanim(cook(fixture)[1])
    opacity = _layer_by_name(doc, "early")["channels"]["opacity"]
    assert opacity == [(0.0, [0.0], ("hold",)), (10.0, [1.0], ("hold",)),
                       (40.0, [0.0], ("hold",))]
    checks += 1

    # --- 11: static group transform bakes into vertices --------------------
    fixture = _fx_doc(layers=[_fx_shape_layer("shifted", 1, [
        _fx_ellipse_group(group_tr={
            "ty": "tr", "p": _fx_static([10, 0]), "a": _fx_static([0, 0]),
            "s": _fx_static([100, 100]), "r": _fx_static(0),
            "o": _fx_static(100)}),
    ], ks=_fx_ks(p={"a": 1, "k": [dict(t=0, s=[0, 0], **lin),
                                  {"t": 60, "s": [0, 5]}]}))])
    doc = _parse_oanim(cook(fixture)[1])
    outer = _layer_by_name(doc, "shifted")["shapes"][0]["keys"][0]["outer"]
    center_x = sum(p[0] for p in outer) / len(outer)
    assert abs(center_x - 0.1) < 1e-3       # +10 px baked = +0.1 world
    checks += 1

    # --- 12: keys outside the timeline densify into range ------------------
    fixture = _fx_doc(layers=[_fx_shape_layer(
        "clamped", 1, [_fx_ellipse_group()],
        ks=_fx_ks(p={"a": 1, "k": [dict(t=-10, s=[0, 0], **lin),
                                   {"t": 70, "s": [20, 0]}]}))])
    doc = _parse_oanim(cook(fixture)[1])
    pos = _layer_by_name(doc, "clamped")["channels"]["pos"]
    assert all(0.0 <= k[0] <= 60.0 for k in pos)
    checks += 1

    # --- 13: zero-duration markers, --clips override, bad clip -------------
    plain = [_fx_shape_layer("body", 1, [_fx_ellipse_group()],
                             ks=_fx_ks(p={"a": 1, "k": [
                                 dict(t=0, s=[0, 0], **lin),
                                 {"t": 60, "s": [10, 0]}]}))]
    fixture = _fx_doc(layers=plain,
                      markers=[{"tm": 0, "cm": "a", "dr": 0},
                               {"tm": 30, "cm": "b", "dr": 0}])
    doc = _parse_oanim(cook(fixture)[1])
    assert doc["clips"] == [("a", 0.0, 30.0, "loop"),
                            ("b", 30.0, 60.0, "loop")]
    doc = _parse_oanim(cook(fixture,
                            clips_override="one:0:20:once,two:20:60")[1])
    assert doc["clips"] == [("one", 0.0, 20.0, "once"),
                            ("two", 20.0, 60.0, "loop")]
    _expect_error(fixture, "empty frame range",
                  clips_override="bad:30:30")
    checks += 1

    # --- every out-of-subset feature raises its named error ----------------
    def with_shape_item(item):
        return _fx_doc(layers=[_fx_shape_layer("hero", 1, [
            {"ty": "gr", "it": [
                {"ty": "el", "p": _fx_static([0, 0]),
                 "s": _fx_static([50, 50])}, item]}])])
    _expect_error(with_shape_item({"ty": "gf", "nm": "shine"}),
                  "gradient fill", "hero", "shine")
    _expect_error(with_shape_item({"ty": "gs"}), "gradient stroke", "hero")
    _expect_error(with_shape_item({"ty": "st"}), "stroke", "hero")
    _expect_error(with_shape_item({"ty": "rp"}), "repeater", "hero")
    _expect_error(with_shape_item({"ty": "mm"}), "merge paths", "hero")
    _expect_error(with_shape_item({"ty": "tm"}), "trim paths", "hero")
    _expect_error(with_shape_item({"ty": "rd"}), "rounded-corners", "hero")
    _expect_error(with_shape_item({"ty": "xyz"}),
                  "unsupported shape item 'xyz'", "hero")
    _expect_error(with_shape_item({
        "ty": "tr", "p": _fx_static([0, 0]), "a": _fx_static([0, 0]),
        "s": _fx_static([100, 100]),
        "r": {"a": 1, "k": [dict(t=0, s=[0], **lin), {"t": 60, "s": [90]}]},
        "o": _fx_static(100)}), "animated group rotation", "hero")

    def one_layer(**extra):
        return _fx_doc(layers=[
            _fx_shape_layer("hero", 1, [_fx_ellipse_group()], **extra)])
    _expect_error(one_layer(masksProperties=[{"mode": "a"}]), "mask", "hero")
    _expect_error(one_layer(tt=1), "track matte", "hero")
    _expect_error(one_layer(ef=[{"ty": 21}]), "layer effects", "hero")
    _expect_error(one_layer(sr=2), "time stretch", "hero")
    _expect_error(one_layer(tm=_fx_static(0)), "time remap", "hero")
    _expect_error(one_layer(ddd=1), "3D layer", "hero")
    _expect_error(one_layer(ao=1), "auto-orient", "hero")
    _expect_error(one_layer(
        ks=_fx_ks(p={"a": 0, "k": [0, 0], "x": "var $bm_rt = [0,0];"})),
        "expression", "position", "hero")
    _expect_error(_fx_doc(layers=[
        {"ty": 2, "nm": "photo", "ind": 1, "refId": "img_0",
         "ks": _fx_ks()}]), "image layer", "photo")
    _expect_error(_fx_doc(layers=[
        {"ty": 5, "nm": "caption", "ind": 1, "ks": _fx_ks()}]),
        "text layer", "caption")
    # a TIMED precomp (stretched or remapped) refuses to inline
    _expect_error(_fx_doc(
        layers=[{"ty": 0, "nm": "slowmo", "ind": 1, "refId": "pc1",
                 "sr": 2, "ks": _fx_ks()}],
        assets=[{"id": "pc1",
                 "layers": [_fx_shape_layer("leaf", 1,
                                            [_fx_ellipse_group()])]}]),
        "time stretch", "slowmo")
    _expect_error(_fx_doc(
        layers=[{"ty": 0, "nm": "loops", "ind": 1, "refId": "pc1",
                 "ks": _fx_ks()}],
        assets=[{"id": "pc1", "layers": [
            {"ty": 0, "nm": "again", "ind": 1, "refId": "pc1",
             "ks": _fx_ks()}]}]),
        "cycle", "loops/again")
    _expect_error(_fx_doc(layers=[{"ty": 3, "nm": "empty", "ind": 1,
                                   "ks": _fx_ks()}]), "no fillable shapes")
    _expect_error("{ not json")
    checks += 1

    # --- the committed round-trip fixture reproduces byte-identically ------
    fixture_dir = Path(__file__).resolve().parent.parent / "tests" / \
        "assets" / "vectoranim"
    source = fixture_dir / "roundtrip.json"
    reference = fixture_dir / "roundtrip.oanim"
    kind, cooked = cook(source.read_text(encoding="utf-8"))
    assert kind == "oanim"
    _parse_oanim(cooked)
    expected = reference.read_text(encoding="utf-8")
    assert cooked == expected, \
        ("the cook no longer reproduces tests/assets/vectoranim/"
         "roundtrip.oanim byte-identically - regenerate the fixture "
         "(python3 Util/cook_vector_anim.py %s %s), re-run the "
         "cook_vector_anim_roundtrip unit test, and commit both" %
         (source, reference))
    checks += 1

    print("cook_vector_anim selftest OK: %d feature groups, every "
          "out-of-subset error named, round-trip fixture byte-identical" %
          checks)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", nargs="?", help="source Lottie .json")
    parser.add_argument("output", nargs="?",
                        help="destination .oanim (default: the input with "
                        "its suffix swapped; a static document switches the "
                        "suffix to .oshape)")
    parser.add_argument("--extent", type=float, default=2.0,
                        help="world-unit size the composition's larger side "
                        "spans (default 2)")
    parser.add_argument("--tolerance", type=float, default=None,
                        help="flatten chord tolerance in composition units")
    parser.add_argument("--clips", default=None, metavar="SPEC",
                        help="clip ranges overriding the document markers: "
                        "name:start:end[:loop|once],... (frames)")
    parser.add_argument("--selftest", action="store_true",
                        help="cook embedded fixtures and assert (a CI gate)")
    args = parser.parse_args()
    if args.selftest:
        return _selftest()
    if not args.input:
        parser.error("input is required (or use --selftest)")
    try:
        with open(args.input, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError as exc:
        print("cook_vector_anim: cannot read %s: %s" % (args.input, exc),
              file=sys.stderr)
        return 1
    try:
        kind, cooked = cook(text, extent=args.extent,
                            tolerance=args.tolerance,
                            clips_override=args.clips)
    except CookError as exc:
        print("cook_vector_anim: cannot cook %s:" % args.input,
              file=sys.stderr)
        for line in str(exc).splitlines():
            print("  " + line, file=sys.stderr)
        return 1
    out_path = Path(args.output) if args.output else \
        Path(args.input).with_suffix(".oanim")
    if kind == "oshape":
        out_path = out_path.with_suffix(".oshape")
        print("nothing animates - cooked a static .oshape instead")
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write(cooked)
    print("cooked %s -> %s" % (args.input, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
