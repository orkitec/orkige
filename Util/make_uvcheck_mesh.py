#!/usr/bin/env python3
"""Generate samples/hello_orkige/media/uvcheck_mesh.glb - a UV-orientation probe.

A textured unit quad whose baseColor texture is a vertical gradient (top rows
RED, bottom rows BLUE) embedded in the .glb. It exists to pin the glTF UV
convention across render backends: glTF's texture origin is top-left, so when
the mesh renders upright the RED end of the gradient must land at the top of
the quad. A vertically flipped import (the classic Codec_Assimp path enables
aiProcess_FlipUVs; the Ogre-Next path must match) would swap red and blue -
the render_facade selfcheck probes both ends and the cross-backend parity gate
compares the whole capture, so any V-flip disagreement fails a pixel test.

Everything is written from scratch with the Python standard library (no PIL):
a hand-rolled PNG encoder for the gradient, and the same glTF 2.0 binary
container layout as make_test_mesh.py.

Geometry
    a 1x1 quad in the XY plane facing +Z, 4 vertices, 2 triangles.
Attributes
    POSITION    (VEC3 float)
    TEXCOORD_0  (VEC2 float) - glTF top-left origin: the +Y (top) edge carries
                V=0, the -Y (bottom) edge V=1, so image row 0 maps to the top.
    COLOR_0     (VEC3 float) - white; the engine renders imported meshes
                vertex-colour unlit (like the jumper assets), so a white
                vertex colour lets the raw texture through on both backends
                (a mesh WITHOUT vertex colours renders black under classic's
                vertex-colour tracking).
Material
    an unlit-friendly baseColorTexture; lighting never touches the gradient
    hues, so the red/blue orientation reads identically on both flavors.

Usage:
    python3 Util/make_uvcheck_mesh.py [output.glb]
Defaults to samples/hello_orkige/media/uvcheck_mesh.glb next to this repo.
The script re-opens the written file and validates the chunk layout.
"""

import json
import struct
import sys
import zlib
from pathlib import Path

GLB_MAGIC = 0x46546C67  # 'glTF'
CHUNK_JSON = 0x4E4F534A  # 'JSON'
CHUNK_BIN = 0x004E4942  # 'BIN\0'


def build_gradient_png(width=8, height=64):
    """Return PNG bytes: a vertical gradient, top row RED -> bottom row BLUE.

    Minimal encoder - 8-bit RGB, one IDAT, filter type 0 per scanline.
    """
    raw = bytearray()
    for row in range(height):
        # row 0 (top of the image) is pure red, the last row pure blue
        t = row / float(height - 1)
        r = int(round(255 * (1.0 - t)))
        b = int(round(255 * t))
        raw.append(0)  # filter type 0 (None) for this scanline
        raw.extend((r, 0, b) * width)

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # RGB
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


def build_geometry():
    """Return (positions, uvs, colours, indices) for the textured quad.

    Front face toward +Z (CCW); +Y edge is V=0 so image row 0 is on top.
    """
    positions = [
        (-0.5, -0.5, 0.0),  # 0 bottom-left
        (0.5, -0.5, 0.0),   # 1 bottom-right
        (0.5, 0.5, 0.0),    # 2 top-right
        (-0.5, 0.5, 0.0),   # 3 top-left
    ]
    uvs = [
        (0.0, 1.0),  # 0 bottom-left
        (1.0, 1.0),  # 1 bottom-right
        (1.0, 0.0),  # 2 top-right
        (0.0, 0.0),  # 3 top-left
    ]
    colours = [(1.0, 1.0, 1.0)] * 4  # white: let the raw texture through
    indices = [0, 1, 2, 0, 2, 3]
    return positions, uvs, colours, indices


def build_glb():
    positions, uvs, colours, indices = build_geometry()
    png = build_gradient_png()

    pos_bin = b"".join(struct.pack("<3f", *p) for p in positions)
    uv_bin = b"".join(struct.pack("<2f", *u) for u in uvs)
    col_bin = b"".join(struct.pack("<3f", *c) for c in colours)
    idx_bin = struct.pack("<%dH" % len(indices), *indices)
    img_bin = png + b"\x00" * (-len(png) % 4)
    assert (len(pos_bin) % 4 == 0 and len(uv_bin) % 4 == 0 and
            len(col_bin) % 4 == 0)

    # pad the index view up to 4-byte alignment before the image view starts
    idx_pad = idx_bin + b"\x00" * (-len(idx_bin) % 4)
    bin_chunk = pos_bin + uv_bin + col_bin + idx_pad + img_bin
    bin_chunk += b"\x00" * (-len(bin_chunk) % 4)

    mins = [min(p[i] for p in positions) for i in range(3)]
    maxs = [max(p[i] for p in positions) for i in range(3)]

    off_pos = 0
    off_uv = off_pos + len(pos_bin)
    off_col = off_uv + len(uv_bin)
    off_idx = off_col + len(col_bin)
    off_img = off_idx + len(idx_pad)

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "orkige Util/make_uvcheck_mesh.py",
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "uvcheck_mesh"}],
        "meshes": [
            {
                "name": "uvcheck_mesh",
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": 0, "TEXCOORD_0": 1, "COLOR_0": 2},
                        "indices": 3,
                        "material": 0,
                        "mode": 4,  # TRIANGLES
                    }
                ],
            }
        ],
        "materials": [
            {
                "name": "uvcheck_gradient",
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                },
            }
        ],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987,
            "wrapS": 10497, "wrapT": 10497}],
        "images": [{"name": "uvcheck_gradient.png", "bufferView": 4,
            "mimeType": "image/png"}],
        "buffers": [{"byteLength": len(bin_chunk)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": off_pos, "byteLength": len(pos_bin),
                "target": 34962},  # ARRAY_BUFFER
            {"buffer": 0, "byteOffset": off_uv, "byteLength": len(uv_bin),
                "target": 34962},
            {"buffer": 0, "byteOffset": off_col, "byteLength": len(col_bin),
                "target": 34962},
            {"buffer": 0, "byteOffset": off_idx, "byteLength": len(idx_bin),
                "target": 34963},  # ELEMENT_ARRAY_BUFFER
            {"buffer": 0, "byteOffset": off_img, "byteLength": len(png)},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions),
                "type": "VEC3", "min": mins, "max": maxs},
            {"bufferView": 1, "componentType": 5126, "count": len(uvs),
                "type": "VEC2"},
            {"bufferView": 2, "componentType": 5126, "count": len(colours),
                "type": "VEC3"},
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


def validate(path):
    """Structural check: magic, version, chunk layout, embedded texture."""
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
    assert doc["images"][0]["mimeType"] == "image/png", "embedded PNG missing"
    return doc


def main():
    default = (
        Path(__file__).resolve().parent.parent
        / "samples/hello_orkige/media/uvcheck_mesh.glb"
    )
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(build_glb())
    doc = validate(out)
    print(
        "wrote %s (%d bytes, %d vertices, embedded %s texture)"
        % (
            out,
            out.stat().st_size,
            doc["accessors"][0]["count"],
            doc["images"][0]["mimeType"],
        )
    )


if __name__ == "__main__":
    main()
