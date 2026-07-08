#!/usr/bin/env python3
"""Generate samples/jumper/media/ - the textured assets of the jumper sample.

Everything is license-clean and reproducible: this script writes the PNG
textures AND the textured glTF 2.0 binary (.glb) meshes from scratch using
only the Python standard library (struct + zlib), extending the GLB-writing
approach of Util/make_test_mesh.py with UVs, materials and embedded images.

Textures (also written as standalone .png files for inspection/editing):
    platform.png  64x64  green checkerboard (16 checks - still reads as a
                         checker when a platform is scaled wide)
    crate.png     64x64  orange-brown planks with deterministic hash noise,
                         dark frame + cross braces
    player.png    64x64  warm yellow suit with a simple face (eyes + smile)
                         in the band the capsule's head maps to

Meshes (each embeds its texture as a PNG in the GLB BIN chunk, referenced
through a standard glTF material -> texture -> sampler -> image chain):
    jumper_platform.glb  unit cube (1x1x1, extents -0.5..0.5), per-face UVs -
                         scale the GameObject to make platforms of any size
                         (RigidBody halfExtents = scale * 0.5)
    jumper_crate.glb     unit cube with the crate texture
    jumper_player.glb    low-poly capsule figure: 8 segments, radius 0.35,
                         cylinder half-height 0.25 (total height 1.2) -
                         matches the ST_CAPSULE body the jumper app creates;
                         cylindrical UVs, the face looks toward +Z

All meshes carry COLOR_0 = white on every vertex ON PURPOSE: the engine's
shared "unlit fix" (PrimitiveUtil::makeEntityVertexColourUnlit, applied by
the player/editor/jumper after a scene load) tracks vertex colours as
diffuse, and white x texture = texture. Without COLOR_0 that fix would read
undefined vertex data. No NORMAL attribute on purpose: OGRE's Codec_Assimp
generates normals on import (aiProcess_GenNormals), same as test_mesh.glb.

Embedded vs sidecar textures: OGRE 14.5's Codec_Assimp loads GLB-embedded
images (AssimpLoader.cpp "Create embedded textures" registers each one in
the TextureManager under a mesh-scoped generated name and getTextureName
resolves the material's "*N" reference to that same name), so the PNGs are
embedded in the GLBs and the standalone .png files are reference copies -
the engine does not read them.

Usage:
    python3 Util/make_jumper_assets.py [output_dir]
Defaults to samples/jumper/media/ next to this repo. The script re-opens
every written GLB and validates chunk layout, UVs, winding and the embedded
PNG bytes.
"""

import json
import math
import struct
import sys
import zlib
from pathlib import Path

GLB_MAGIC = 0x46546C67  # 'glTF'
CHUNK_JSON = 0x4E4F534A  # 'JSON'
CHUNK_BIN = 0x004E4942  # 'BIN\0'

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


# --------------------------------------------------------------------------
# minimal PNG encoder (RGB8, no interlace, filter 0 on every scanline)
# --------------------------------------------------------------------------

def encode_png(width, height, rgb_rows):
    """rgb_rows: list of rows, each a list of (r, g, b) 0..255 tuples."""

    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    raw = b"".join(
        b"\x00" + bytes(c for px in row for c in px) for row in rgb_rows
    )
    ihdr = struct.pack(">2I5B", width, height, 8, 2, 0, 0, 0)  # RGB8
    return (
        PNG_SIGNATURE
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def hash_noise(x, y, salt=0):
    """Deterministic pseudo-noise in 0..1 (no random module - reproducible)."""
    n = (x * 374761393 + y * 668265263 + salt * 2147483647) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def make_platform_texture(size=64):
    """Green checkerboard, 16 checks per side, slight per-check variation."""
    light, dark = (96, 190, 110), (36, 96, 64)
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            cx, cy = x * 16 // size, y * 16 // size
            base = light if (cx + cy) % 2 == 0 else dark
            wobble = int(14 * hash_noise(cx, cy, 1)) - 7
            row.append(tuple(max(0, min(255, c + wobble)) for c in base))
        rows.append(row)
    return rows


def make_crate_texture(size=64):
    """Orange-brown crate: vertical gradient + noise, dark frame + braces."""
    rows = []
    frame = 5
    for y in range(size):
        row = []
        t = y / (size - 1)
        for x in range(size):
            r = int(150 + 45 * t + 22 * hash_noise(x, y, 2) - 11)
            g = int(92 + 36 * t + 18 * hash_noise(x, y, 3) - 9)
            b = int(42 + 18 * t + 12 * hash_noise(x, y, 4) - 6)
            on_frame = (
                x < frame or x >= size - frame or y < frame or y >= size - frame
            )
            on_brace = abs(x - y) < 3 or abs((size - 1 - x) - y) < 3
            if on_frame or on_brace:
                r, g, b = int(r * 0.55), int(g * 0.55), int(b * 0.55)
            row.append(
                (max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b)))
            )
        rows.append(row)
    return rows


def make_player_texture(size=64):
    """Warm yellow suit; face (eyes + smile) in the head band.

    The capsule's cylindrical UVs put v=0 at the image top (= capsule top) and
    u=0.5 at +Z (the camera-facing front): the face is painted around x=32 in
    the y band 12..30, which the top cap / upper cylinder maps onto.
    """
    suit, belly = (240, 196, 60), (250, 226, 130)
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            # belly patch on the front lower half
            if 22 <= x <= 42 and 34 <= y <= 52:
                base = belly
            else:
                base = suit
            wobble = int(10 * hash_noise(x // 4, y // 4, 5)) - 5
            r, g, b = (max(0, min(255, c + wobble)) for c in base)
            # eyes: two dark dots with white highlight
            for ex in (25, 39):
                if (x - ex) ** 2 + (y - 17) ** 2 <= 16:
                    r, g, b = 25, 25, 35
                if (x - (ex + 1)) ** 2 + (y - 16) ** 2 <= 2:
                    r, g, b = 245, 245, 245
            # smile: lower arc of a circle around (32, 20)
            dx, dy = x - 32, y - 20
            dist = math.hypot(dx, dy)
            if 6.5 <= dist <= 8.0 and dy > 3:
                r, g, b = 150, 60, 40
            # feet band at the very bottom (bottom cap)
            if y >= 58:
                r, g, b = 190, 120, 40
            row.append((r, g, b))
        rows.append(row)
    return rows


# --------------------------------------------------------------------------
# geometry
# --------------------------------------------------------------------------

def build_unit_cube():
    """Unit cube (extents -0.5..0.5), 24 vertices, per-face 0..1 UVs."""
    s = 0.5
    # per face: outward normal axis, origin corner, u axis, v axis
    # corners listed so triangles (0,1,2)(0,2,3) wind CCW seen from outside
    faces = [
        # +X
        [(s, -s, s), (s, -s, -s), (s, s, -s), (s, s, s)],
        # -X
        [(-s, -s, -s), (-s, -s, s), (-s, s, s), (-s, s, -s)],
        # +Y
        [(-s, s, s), (s, s, s), (s, s, -s), (-s, s, -s)],
        # -Y
        [(-s, -s, -s), (s, -s, -s), (s, -s, s), (-s, -s, s)],
        # +Z
        [(-s, -s, s), (s, -s, s), (s, s, s), (-s, s, s)],
        # -Z
        [(s, -s, -s), (-s, -s, -s), (-s, s, -s), (s, s, -s)],
    ]
    positions, uvs, indices = [], [], []
    face_uvs = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    for corners in faces:
        base = len(positions)
        positions.extend(corners)
        uvs.extend(face_uvs)
        indices.extend(
            [base, base + 1, base + 2, base, base + 2, base + 3]
        )
    return positions, uvs, indices


def build_capsule(segments=8, radius=0.35, half_height=0.25):
    """Low-poly capsule around the origin: cylinder + hemispherical caps.

    Matches PhysicsWorld ST_CAPSULE (halfHeight = cylinder part, radius) so
    the visual and the collision shape agree. 9 columns (u seam duplicate),
    rows bottom pole -> top pole; u=0.5 faces +Z.
    """
    cols = segments + 1
    # rows: (y, ring radius, v)  -  v=0 at the image top = capsule top
    r45 = radius * math.sin(math.pi / 4)
    rows = [
        (half_height + radius, 0.0, 0.0),               # top pole
        (half_height + r45, radius * math.cos(math.pi / 4), 0.12),
        (half_height, radius, 0.25),                    # cylinder top
        (-half_height, radius, 0.75),                   # cylinder bottom
        (-half_height - r45, radius * math.cos(math.pi / 4), 0.88),
        (-half_height - radius, 0.0, 1.0),              # bottom pole
    ]
    positions, uvs = [], []
    for y, ring_r, v in rows:
        for i in range(cols):
            u = i / segments
            theta = 2.0 * math.pi * u
            # u=0.5 -> +Z (the face looks at the default camera)
            positions.append(
                (ring_r * math.sin(theta), y, -ring_r * math.cos(theta))
            )
            uvs.append((u, v))
    indices = []
    for j in range(len(rows) - 1):
        top_pole = rows[j][1] == 0.0
        bottom_pole = rows[j + 1][1] == 0.0
        for i in range(segments):
            a = j * cols + i          # upper row, this column
            b = j * cols + i + 1      # upper row, next column
            c = (j + 1) * cols + i    # lower row, this column
            d = (j + 1) * cols + i + 1
            # CCW from outside (y decreases from row j to row j+1)
            if not bottom_pole:
                indices.extend([a, d, c])
            if not top_pole:
                indices.extend([a, b, d])
    return positions, uvs, indices


def check_outward_winding(positions, indices):
    """All triangles of these origin-centered convex shapes must face outward."""
    for k in range(0, len(indices), 3):
        pa = positions[indices[k]]
        pb = positions[indices[k + 1]]
        pc = positions[indices[k + 2]]
        ab = [pb[i] - pa[i] for i in range(3)]
        ac = [pc[i] - pa[i] for i in range(3)]
        normal = [
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        ]
        centroid = [(pa[i] + pb[i] + pc[i]) / 3.0 for i in range(3)]
        dot = sum(normal[i] * centroid[i] for i in range(3))
        assert dot > 0.0, "inward-facing triangle at index %d" % k


# --------------------------------------------------------------------------
# GLB writer (positions + UVs + white COLOR_0 + indices + embedded PNG)
# --------------------------------------------------------------------------

def build_glb(mesh_name, positions, uvs, indices, png_bytes, texture_name):
    check_outward_winding(positions, indices)
    assert len(positions) == len(uvs)

    pos_bin = b"".join(struct.pack("<3f", *p) for p in positions)
    uv_bin = b"".join(struct.pack("<2f", *uv) for uv in uvs)
    col_bin = struct.pack("<3f", 1.0, 1.0, 1.0) * len(positions)
    idx_bin = struct.pack("<%dH" % len(indices), *indices)
    idx_bin += b"\x00" * (-len(idx_bin) % 4)  # keep the image view 4-aligned
    img_bin = png_bytes

    views = []
    blob = b""
    for data, target in (
        (pos_bin, 34962),   # ARRAY_BUFFER
        (uv_bin, 34962),
        (col_bin, 34962),
        (idx_bin, 34963),   # ELEMENT_ARRAY_BUFFER
        (img_bin, None),    # image data - no target
    ):
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        blob += data
    blob += b"\x00" * (-len(blob) % 4)

    mins = [min(p[i] for p in positions) for i in range(3)]
    maxs = [max(p[i] for p in positions) for i in range(3)]

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "orkige Util/make_jumper_assets.py",
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": mesh_name}],
        "meshes": [
            {
                "name": mesh_name,
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": 0,
                            "TEXCOORD_0": 1,
                            "COLOR_0": 2,
                        },
                        "indices": 3,
                        "material": 0,
                        "mode": 4,  # TRIANGLES
                    }
                ],
            }
        ],
        "materials": [
            {
                "name": mesh_name + "_mat",
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                },
            }
        ],
        "textures": [{"sampler": 0, "source": 0}],
        "samplers": [
            {
                "magFilter": 9729,  # LINEAR
                "minFilter": 9987,  # LINEAR_MIPMAP_LINEAR
                "wrapS": 10497,     # REPEAT
                "wrapT": 10497,
            }
        ],
        "images": [
            {
                "name": texture_name,
                "mimeType": "image/png",
                "bufferView": 4,
            }
        ],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,  # FLOAT
                "count": len(positions),
                "type": "VEC3",
                "min": mins,
                "max": maxs,
            },
            {
                "bufferView": 1,
                "componentType": 5126,
                "count": len(uvs),
                "type": "VEC2",
            },
            {
                "bufferView": 2,
                "componentType": 5126,
                "count": len(positions),
                "type": "VEC3",
            },
            {
                "bufferView": 3,
                "componentType": 5123,  # UNSIGNED_SHORT
                "count": len(indices),
                "type": "SCALAR",
            },
        ],
    }

    json_chunk = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * (-len(json_chunk) % 4)

    total = 12 + 8 + len(json_chunk) + 8 + len(blob)
    return b"".join(
        [
            struct.pack("<3I", GLB_MAGIC, 2, total),
            struct.pack("<2I", len(json_chunk), CHUNK_JSON),
            json_chunk,
            struct.pack("<2I", len(blob), CHUNK_BIN),
            blob,
        ]
    )


def validate_glb(path):
    """Structural check: chunks, UV accessor, material chain, embedded PNG."""
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
    assert "COLOR_0" in prim["attributes"], "COLOR_0 missing"
    material = doc["materials"][prim["material"]]
    tex_index = material["pbrMetallicRoughness"]["baseColorTexture"]["index"]
    image = doc["images"][doc["textures"][tex_index]["source"]]
    view = doc["bufferViews"][image["bufferView"]]
    img_off = bin_off + 8 + view["byteOffset"]
    png = data[img_off : img_off + view["byteLength"]]
    assert png.startswith(PNG_SIGNATURE), "embedded image is not a PNG"
    return doc


def main():
    default = Path(__file__).resolve().parent.parent / "samples/jumper/media"
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    out_dir.mkdir(parents=True, exist_ok=True)

    textures = {
        "platform.png": make_platform_texture(),
        "crate.png": make_crate_texture(),
        "player.png": make_player_texture(),
    }
    pngs = {}
    for name, rows in textures.items():
        pngs[name] = encode_png(len(rows[0]), len(rows), rows)
        (out_dir / name).write_bytes(pngs[name])
        print("wrote %s (%d bytes)" % (out_dir / name, len(pngs[name])))

    cube_positions, cube_uvs, cube_indices = build_unit_cube()
    cap_positions, cap_uvs, cap_indices = build_capsule()
    meshes = [
        ("jumper_platform.glb", cube_positions, cube_uvs, cube_indices,
         "platform.png"),
        ("jumper_crate.glb", cube_positions, cube_uvs, cube_indices,
         "crate.png"),
        ("jumper_player.glb", cap_positions, cap_uvs, cap_indices,
         "player.png"),
    ]
    for glb_name, positions, uvs, indices, tex in meshes:
        path = out_dir / glb_name
        path.write_bytes(
            build_glb(glb_name[:-4], positions, uvs, indices, pngs[tex], tex)
        )
        doc = validate_glb(path)
        print(
            "wrote %s (%d bytes, %d vertices, %d indices, texture %s)"
            % (
                path,
                path.stat().st_size,
                doc["accessors"][0]["count"],
                doc["accessors"][3]["count"],
                tex,
            )
        )


if __name__ == "__main__":
    main()
