#!/usr/bin/env python3
"""Generate the engine-default decal textures the DecalComponent projects onto
surfaces: a neutral soft round MARK (impact marks, paint splats, footprints -
tintable, white) and a soft dark BLOB (the blob-shadow fallback tier - a dark
ellipse used as a cheap character shadow where real shadow maps are refused).
Both are RGBA with a smooth alpha falloff so the projected/aligned decal blends
softly at its rim. Stdlib only (the orkige_png codec).

The engine decal path (next: a projected Ogre-Next Decal fed from a fixed-size
diffuse texture ARRAY pool; classic: a surface-aligned textured quad) pools every
decal texture at ONE resolution, so these are authored at 256x256 - the
DecalComponent's DECAL_TEXTURE_SIZE. A project's own decal texture should match
that size (see Docs/materials.md#decals).

Run from the repo root:
    python3 Util/make_decal_textures.py [out_dir]

Defaults to orkige_engine/media/decals (the engine media dir the player/editor
register like the water/font dirs and bundle to exports). Deterministic:
re-running rewrites byte-identical PNGs.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_png  # noqa: E402

# the single decal-pool resolution (mirrors DecalComponent::DECAL_TEXTURE_SIZE)
DECAL_SIZE = 256


def _smoothstep(edge0, edge1, x):
    if edge1 <= edge0:
        return 0.0 if x < edge0 else 1.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def make_mark(size=DECAL_SIZE):
    """a neutral soft round mark: WHITE rgb (tintable) with a smooth alpha
    falloff from a full-alpha core to a feathered rim - the general impact /
    splat / footprint decal."""
    image = orkige_png.Image(size, size)
    centre = (size - 1) / 2.0
    radius = size / 2.0
    for y in range(size):
        for x in range(size):
            dx = (x - centre) / radius
            dy = (y - centre) / radius
            dist = math.sqrt(dx * dx + dy * dy)
            # a soft core out to ~0.55, feathering to zero at the rim
            alpha = 1.0 - _smoothstep(0.55, 1.0, dist)
            image.put(x, y, (255, 255, 255, int(round(alpha * 255))))
    return image


def make_blob(size=DECAL_SIZE):
    """a soft dark ellipse: BLACK rgb with a smooth alpha falloff - the
    blob-shadow fallback (a cheap dark oval under an object where real shadow
    maps are off/refused). Slightly softer core than the mark so it reads as a
    diffuse shadow rather than a hard stamp."""
    image = orkige_png.Image(size, size)
    centre = (size - 1) / 2.0
    radius = size / 2.0
    for y in range(size):
        for x in range(size):
            dx = (x - centre) / radius
            dy = (y - centre) / radius
            dist = math.sqrt(dx * dx + dy * dy)
            # a soft shadow: peak alpha ~0.75 at the centre, fading to the rim
            alpha = (1.0 - _smoothstep(0.0, 1.0, dist)) * 0.75
            image.put(x, y, (0, 0, 0, int(round(alpha * 255))))
    return image


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        "orkige_engine", "media", "decals")
    os.makedirs(out_dir, exist_ok=True)
    mark_path = os.path.join(out_dir, "decal_mark.png")
    blob_path = os.path.join(out_dir, "decal_blob.png")
    orkige_png.encode_png(make_mark(), mark_path)
    orkige_png.encode_png(make_blob(), blob_path)
    print("wrote %s" % mark_path)
    print("wrote %s" % blob_path)


if __name__ == "__main__":
    main()
