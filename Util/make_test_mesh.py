#!/usr/bin/env python3
"""Generate samples/hello_orkige/media/test_mesh.glb - the engine's glTF test asset.

Writes a fully license-clean glTF 2.0 binary (.glb) from scratch using only the
Python standard library: a vertex-colored octahedron, slightly elongated along Y
(so it reads as a "gem"/spinning-top and cannot be mistaken for the demo cubes).

Geometry
    6 vertices: apex (0, 1.4, 0), base (0, -1.4, 0), equator ring of 4 at
    radius 0.8 - roughly unit size.
    8 triangles, indexed, counter-clockwise winding (glTF front faces).
Attributes
    POSITION  (VEC3 float)
    COLOR_0   (VEC3 float) - apex white, bottom magenta, equator R/G/B/yellow.
    No NORMAL on purpose: OGRE's Codec_Assimp import pipeline generates flat
    normals (aiProcess_GenNormals), and the engine renders the asset unlit
    with vertex-color tracking anyway (same treatment as the demo cubes).
    No material/texture: COLOR_0 needs neither; assimp synthesizes a default
    material on import.

GLB layout (per glTF 2.0 spec chapter "GLB File Format"):
    12-byte header (magic 'glTF', version 2, total length)
    JSON chunk  (type 'JSON', padded to 4 bytes with spaces)
    BIN  chunk  (type 'BIN\\0', padded to 4 bytes with zeros)

Usage:
    python3 Util/make_test_mesh.py [output.glb]
Defaults to samples/hello_orkige/media/test_mesh.glb next to this repo.
The script re-opens the written file and validates the chunk layout.
"""

import json
import struct
import sys
from pathlib import Path

GLB_MAGIC = 0x46546C67  # 'glTF'
CHUNK_JSON = 0x4E4F534A  # 'JSON'
CHUNK_BIN = 0x004E4942  # 'BIN\0'


def build_geometry():
    """Return (positions, colors, indices) for the vertex-colored octahedron."""
    apex_y = 1.4
    ring_r = 0.8
    positions = [
        (0.0, apex_y, 0.0),   # 0 apex
        (ring_r, 0.0, 0.0),   # 1 +x
        (0.0, 0.0, ring_r),   # 2 +z
        (-ring_r, 0.0, 0.0),  # 3 -x
        (0.0, 0.0, -ring_r),  # 4 -z
        (0.0, -apex_y, 0.0),  # 5 bottom
    ]
    colors = [
        (1.0, 1.0, 1.0),  # apex: white
        (1.0, 0.0, 0.0),  # +x: red
        (0.0, 1.0, 0.0),  # +z: green
        (0.0, 0.0, 1.0),  # -x: blue
        (1.0, 1.0, 0.0),  # -z: yellow
        (1.0, 0.0, 1.0),  # bottom: magenta
    ]
    ring = [1, 2, 3, 4]
    indices = []
    for i in range(4):
        a, b = ring[i], ring[(i + 1) % 4]
        indices += [0, b, a]  # upper faces, CCW seen from outside
        indices += [5, a, b]  # lower faces, CCW seen from outside
    return positions, colors, indices


def build_glb():
    positions, colors, indices = build_geometry()

    pos_bin = b"".join(struct.pack("<3f", *p) for p in positions)
    col_bin = b"".join(struct.pack("<3f", *c) for c in colors)
    idx_bin = struct.pack("<%dH" % len(indices), *indices)
    # buffer views must start 4-byte aligned; these sizes already are
    assert len(pos_bin) % 4 == 0 and len(col_bin) % 4 == 0
    bin_chunk = pos_bin + col_bin + idx_bin
    bin_chunk += b"\x00" * (-len(bin_chunk) % 4)

    mins = [min(p[i] for p in positions) for i in range(3)]
    maxs = [max(p[i] for p in positions) for i in range(3)]

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "orkige Util/make_test_mesh.py",
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "test_mesh"}],
        "meshes": [
            {
                "name": "test_mesh",
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "COLOR_0": 1},
                        "indices": 2,
                        "mode": 4,  # TRIANGLES
                    }
                ],
            }
        ],
        "buffers": [{"byteLength": len(bin_chunk)}],
        "bufferViews": [
            {
                "buffer": 0,
                "byteOffset": 0,
                "byteLength": len(pos_bin),
                "target": 34962,  # ARRAY_BUFFER
            },
            {
                "buffer": 0,
                "byteOffset": len(pos_bin),
                "byteLength": len(col_bin),
                "target": 34962,
            },
            {
                "buffer": 0,
                "byteOffset": len(pos_bin) + len(col_bin),
                "byteLength": len(idx_bin),
                "target": 34963,  # ELEMENT_ARRAY_BUFFER
            },
        ],
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
                "count": len(colors),
                "type": "VEC3",
            },
            {
                "bufferView": 2,
                "componentType": 5123,  # UNSIGNED_SHORT
                "count": len(indices),
                "type": "SCALAR",
            },
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
    """Structural check: magic, version, chunk layout, JSON parses."""
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
    assert doc["buffers"][0]["byteLength"] == bin_len, "buffer size mismatch"
    prim = doc["meshes"][0]["primitives"][0]
    assert "COLOR_0" in prim["attributes"], "COLOR_0 missing"
    return doc


def main():
    default = (
        Path(__file__).resolve().parent.parent
        / "samples/hello_orkige/media/test_mesh.glb"
    )
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(build_glb())
    doc = validate(out)
    print(
        "wrote %s (%d bytes, %d vertices, %d indices)"
        % (
            out,
            out.stat().st_size,
            doc["accessors"][0]["count"],
            doc["accessors"][2]["count"],
        )
    )


if __name__ == "__main__":
    main()
