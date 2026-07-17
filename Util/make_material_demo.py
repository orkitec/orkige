#!/usr/bin/env python3
"""Generate the hello_orkige PBS-material demo assets - stdlib only.

The `.omat` material asset (core_util/MaterialAsset) needs a demo object:
a UV-mapped cube plus the three texture maps the format can reference.
Everything is generated deterministically here (license-clean, tiny), in the
same spirit as make_uvcheck_mesh.py / make_test_mesh.py:

  samples/hello_orkige/media/demo_material_cube.glb  a unit cube, 24 vertices
      (per-face normals + UVs, indexed triangles, no vertex colours, no glTF
      material) - assimp imports it, the engine generates normals-preserving
      geometry and builds TANGENTS from the UVs, so normal-mapping materials
      apply on the Ogre-Next flavor.
  samples/hello_orkige/media/demo_mat_albedo.png     8x8 orange/cream checker
  samples/hello_orkige/media/demo_mat_normal.png     16x16 tangent-space bump
      grid (flat +Z base 128/128/255 with tilted bevel bands)
  samples/hello_orkige/media/demo_mat_emissive.png   8x8 dark field with a
      bright 2x2 core
  samples/hello_orkige/media/demo_material.omat      the material text asset
      tying them together (metal-rough scalars + all three maps)
  samples/hello_orkige/media/demo_material_flat.omat the SAME surface minus
      the maps (the pixel-probe sibling: rendering the hero object with this
      isolates what the normal/emissive maps contribute)
  samples/hello_orkige/media/demo_material_ground.omat a normal-mapped,
      emission-free ground material (the probe rig's shadow receiver)

Usage:
    python3 Util/make_material_demo.py [output_dir]
Defaults to samples/hello_orkige/media/ next to this repo. The script
re-opens the written .glb and validates the chunk layout.
"""

import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_sidecar  # noqa: E402  (sibling stdlib helper)
from pathlib import Path

GLB_MAGIC = 0x46546C67  # 'glTF'
CHUNK_JSON = 0x4E4F534A  # 'JSON'
CHUNK_BIN = 0x004E4942  # 'BIN\0'


def encode_png(width, height, rows):
    """Minimal PNG encoder - 8-bit RGB or RGBA (by tuple width), one IDAT,
    filter 0 per scanline."""
    channels = len(rows[0][0])
    raw = bytearray()
    for row in rows:
        assert len(row) == width
        raw.append(0)  # filter type 0 (None)
        for pixel in row:
            assert len(pixel) == channels
            raw.extend(pixel)

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    colour_type = 2 if channels == 3 else 6  # RGB / RGBA
    ihdr = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


def build_albedo_png(size=8):
    """Orange/cream checker: reads clearly as 'textured' from any angle."""
    orange = (224, 120, 40)
    cream = (238, 226, 200)
    rows = [[orange if (x + y) % 2 == 0 else cream for x in range(size)]
            for y in range(size)]
    return encode_png(size, size, rows)


def build_normal_png(size=16):
    """Tangent-space bump grid: flat +Z (128,128,255) with bevel bands.

    Every 4th column tilts the normal +X (192,128,255), every 4th row +Y
    (128,192,255) - a simple emboss grid that lights differently from the
    flat base, which is all the demo needs to prove the map is sampled.
    """
    flat = (128, 128, 255)
    tilt_x = (192, 128, 234)
    tilt_y = (128, 192, 234)
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            if x % 4 == 0:
                row.append(tilt_x)
            elif y % 4 == 0:
                row.append(tilt_y)
            else:
                row.append(flat)
        rows.append(row)
    return encode_png(size, size, rows)


def build_emissive_png(size=8):
    """Dark field with a bright warm 2x2 core (most texels emit nothing)."""
    dark = (0, 0, 0)
    glow = (255, 200, 80)
    core = (size // 2 - 1, size // 2)
    rows = [[glow if (x in core and y in core) else dark
             for x in range(size)] for y in range(size)]
    return encode_png(size, size, rows)


def build_leaf_png(size=48):
    """RGBA cutout leaf: a green disc with a round central HOLE, alpha 0
    outside disc and inside hole (binary edges - the alpha test is a hard
    keep/discard, so the demo texture is authored hard too)."""
    centre = (size - 1) / 2.0
    disc_r = size * 0.46
    hole_r = size * 0.17
    green = (74, 148, 66)
    vein = (108, 182, 92)
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            dx, dy = x - centre, y - centre
            dist = (dx * dx + dy * dy) ** 0.5
            inside = hole_r <= dist <= disc_r
            colour = vein if (x + y) % 7 == 0 else green
            row.append((colour if inside else (0, 0, 0)) +
                       ((255,) if inside else (0,)))
        rows.append(row)
    return encode_png(size, size, rows)


OMAT_TEXT = """# hello_orkige PBS material demo - generated by Util/make_material_demo.py
version 1
albedo 1.0 1.0 1.0 1.0
albedoTexture demo_mat_albedo.png
metalness 0.15
roughness 0.35
normalTexture demo_mat_normal.png
emissive 0.9 0.7 0.3
emissiveTexture demo_mat_emissive.png
"""

# the SAME surface minus the maps: the flat sibling the material_looks_right
# pixel probe renders in the hero cube's place - any pixel difference between
# the two runs is exactly what the normal/emissive maps contribute
OMAT_FLAT_TEXT = """# hello_orkige PBS material demo (flat sibling) - generated by Util/make_material_demo.py
version 1
albedo 1.0 1.0 1.0 1.0
albedoTexture demo_mat_albedo.png
metalness 0.15
roughness 0.35
"""

# a normal-mapped, emission-free ground: the shadow RECEIVER of the probe rig
# (a cast shadow must compose with the normal-map stage on both flavors)
OMAT_GROUND_TEXT = """# hello_orkige PBS material demo (ground) - generated by Util/make_material_demo.py
version 1
albedo 1.0 1.0 1.0 1.0
albedoTexture demo_mat_albedo.png
roughness 0.85
normalTexture demo_mat_normal.png
"""

# the CUTOUT + TWO-SIDED demo surface: a leaf quad that must render (and cast
# its shadow) with the hole punched through, visible from both sides
OMAT_LEAF_TEXT = """# hello_orkige PBS material demo (cutout leaf) - generated by Util/make_material_demo.py
version 1
albedoTexture demo_mat_leaf.png
roughness 0.9
alphaTest 0.5
twoSided 1
"""


def build_cube_geometry(half=0.8):
    """24-vertex unit cube: per-face normals + UVs, 12 CCW triangles.

    Matches the editor cube's half extent so the demo object reads at the
    same scale as the procedural content around it.
    """
    s = half
    # per face: (normal, four corners CCW seen from outside)
    faces = [
        ((0, 0, 1), [(-s, -s, s), (s, -s, s), (s, s, s), (-s, s, s)]),
        ((0, 0, -1), [(s, -s, -s), (-s, -s, -s), (-s, s, -s), (s, s, -s)]),
        ((1, 0, 0), [(s, -s, s), (s, -s, -s), (s, s, -s), (s, s, s)]),
        ((-1, 0, 0), [(-s, -s, -s), (-s, -s, s), (-s, s, s), (-s, s, -s)]),
        ((0, 1, 0), [(-s, s, s), (s, s, s), (s, s, -s), (-s, s, -s)]),
        ((0, -1, 0), [(-s, -s, -s), (s, -s, -s), (s, -s, s), (-s, -s, s)]),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, corners in faces:
        base = len(positions)
        positions.extend(corners)
        normals.extend([normal] * 4)
        uvs.extend([(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
    return positions, normals, uvs, indices


def build_quad_geometry(half=0.9):
    """4-vertex upright quad in the XY plane, +Z normal, full-rect UVs -
    single-sided geometry on purpose (the two-sided MATERIAL shows the back)."""
    s = half
    positions = [(-s, -s, 0.0), (s, -s, 0.0), (s, s, 0.0), (-s, s, 0.0)]
    normals = [(0.0, 0.0, 1.0)] * 4
    uvs = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    indices = [0, 1, 2, 0, 2, 3]
    return positions, normals, uvs, indices


def build_glb(name, geometry):
    positions, normals, uvs, indices = geometry

    pos_bin = b"".join(struct.pack("<3f", *p) for p in positions)
    nrm_bin = b"".join(struct.pack("<3f", *n) for n in normals)
    uv_bin = b"".join(struct.pack("<2f", *u) for u in uvs)
    idx_bin = struct.pack("<%dH" % len(indices), *indices)
    idx_pad = idx_bin + b"\x00" * (-len(idx_bin) % 4)
    bin_chunk = pos_bin + nrm_bin + uv_bin + idx_pad
    bin_chunk += b"\x00" * (-len(bin_chunk) % 4)

    mins = [min(p[i] for p in positions) for i in range(3)]
    maxs = [max(p[i] for p in positions) for i in range(3)]

    off_pos = 0
    off_nrm = off_pos + len(pos_bin)
    off_uv = off_nrm + len(nrm_bin)
    off_idx = off_uv + len(uv_bin)

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "orkige Util/make_material_demo.py",
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [
            {
                "name": name,
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 3,
                        "mode": 4,  # TRIANGLES
                    }
                ],
            }
        ],
        "buffers": [{"byteLength": len(bin_chunk)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": off_pos, "byteLength": len(pos_bin),
                "target": 34962},  # ARRAY_BUFFER
            {"buffer": 0, "byteOffset": off_nrm, "byteLength": len(nrm_bin),
                "target": 34962},
            {"buffer": 0, "byteOffset": off_uv, "byteLength": len(uv_bin),
                "target": 34962},
            {"buffer": 0, "byteOffset": off_idx, "byteLength": len(idx_bin),
                "target": 34963},  # ELEMENT_ARRAY_BUFFER
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions),
                "type": "VEC3", "min": mins, "max": maxs},
            {"bufferView": 1, "componentType": 5126, "count": len(normals),
                "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uvs),
                "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(indices),
                "type": "SCALAR"},
        ],
    }

    json_chunk = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * (-len(json_chunk) % 4)

    total = 12 + 8 + len(json_chunk) + 8 + len(bin_chunk)
    return b"".join(
        [
            struct.pack("<3I", GLB_MAGIC, 2, total),
            struct.pack("<2I", len(json_chunk), CHUNK_JSON),
            json_chunk,
            struct.pack("<2I", len(bin_chunk), CHUNK_BIN),
            bin_chunk,
        ]
    )


def validate_glb(path):
    """Structural check: magic, version, chunk layout, required attributes."""
    data = path.read_bytes()
    magic, version, total = struct.unpack_from("<3I", data, 0)
    assert magic == GLB_MAGIC, "bad magic"
    assert version == 2, "bad version"
    assert total == len(data), "length mismatch"
    json_len, json_type = struct.unpack_from("<2I", data, 12)
    assert json_type == CHUNK_JSON, "first chunk must be JSON"
    doc = json.loads(data[20 : 20 + json_len])
    bin_off = 20 + json_len
    bin_len, bin_type = struct.unpack_from("<2I", data, bin_off)
    assert bin_type == CHUNK_BIN, "second chunk must be BIN"
    assert bin_off + 8 + bin_len == total, "BIN chunk length mismatch"
    prim = doc["meshes"][0]["primitives"][0]
    assert "TEXCOORD_0" in prim["attributes"], "TEXCOORD_0 missing"
    assert "NORMAL" in prim["attributes"], "NORMAL missing"
    return doc


def main():
    default = (
        Path(__file__).resolve().parent.parent / "samples/hello_orkige/media"
    )
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    out_dir.mkdir(parents=True, exist_ok=True)

    glb = out_dir / "demo_material_cube.glb"
    glb.write_bytes(build_glb("demo_material_cube", build_cube_geometry()))
    doc = validate_glb(glb)
    leaf_glb = out_dir / "demo_material_leaf.glb"
    leaf_glb.write_bytes(build_glb("demo_material_leaf", build_quad_geometry()))
    validate_glb(leaf_glb)

    written = [glb, leaf_glb]
    for name, payload in [
        ("demo_mat_albedo.png", build_albedo_png()),
        ("demo_mat_normal.png", build_normal_png()),
        ("demo_mat_emissive.png", build_emissive_png()),
        ("demo_mat_leaf.png", build_leaf_png()),
    ]:
        path = out_dir / name
        path.write_bytes(payload)
        written.append(path)
        if name == "demo_mat_normal.png":
            # normal maps encode directions - block compression distorts
            # them, so the export cook ships this one as exact PNG texels
            orkige_sidecar.stamp_texture_sidecar(str(path))
    for name, text in [
        ("demo_material.omat", OMAT_TEXT),
        ("demo_material_flat.omat", OMAT_FLAT_TEXT),
        ("demo_material_ground.omat", OMAT_GROUND_TEXT),
        ("demo_material_leaf.omat", OMAT_LEAF_TEXT),
    ]:
        omat = out_dir / name
        omat.write_text(text)
        written.append(omat)

    print("wrote %d files (cube: %d vertices):" %
          (len(written), doc["accessors"][0]["count"]))
    for path in written:
        print("  %s (%d bytes)" % (path, path.stat().st_size))


if __name__ == "__main__":
    main()
