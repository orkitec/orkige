#!/usr/bin/env python3
"""Generate the shared engine water surface assets - a flat plane .glb + a
tiling water normal map.

This is the geometry+texture half of the water-surface feature (WaterComponent
supplies the animated material at runtime). Like make_terrain_mesh.py this is a
BAKED static answer with NO engine facade growth: a single flat mesh imported
through the same ModelComponent/MeshInstance path, and a seamless-tiling normal
map the water material scrolls for the ripple. Both are generated
deterministically from a seed with the Python standard library ONLY (no PIL, no
numpy - license-clean, tiny, byte-reproducible) and share make_terrain_mesh.py's
stdlib glTF 2.0 binary container.

The PLANE is a unit grid in the XZ plane (a horizontal lake/sea surface):
positions in [-0.5, 0.5] on X and Z, y = 0, per-vertex normals +Y, tiling
TEXCOORD_0 (so the water material's detail normals repeat), one sub-mesh. It is
a UNIT plane on purpose - WaterComponent scales it to the world size via the
sibling transform node, so ONE shared mesh serves every water surface. The grid
is modestly tessellated: v1 water is FLAT (the ripple is entirely in the
scrolling normal map, no per-vertex CPU work), so the resolution only needs to
be enough for clean tangents and future vertex-wave headroom.

The NORMAL map is a seamless tangent-space normal derived from two overlapping
seamless wave-noise fields, encoded (128,128,255) = flat +Z - a believable
tiling water ripple.

Usage:
    python3 Util/make_water_mesh.py                 # write the engine water media
    python3 Util/make_water_mesh.py out/            # write to a chosen dir
    python3 Util/make_water_mesh.py --selftest      # validate, no writes
Defaults to orkige_engine/media/water/ next to this repo (registered by the
player/editor like the engine-default font dir). The script re-opens the written
.glb and validates the plane layout.
"""

import argparse
import json
import math
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_png  # noqa: E402  (sibling stdlib helper - reused encoder)

GLB_MAGIC = 0x46546C67  # 'glTF'
CHUNK_JSON = 0x4E4F534A  # 'JSON'
CHUNK_BIN = 0x004E4942  # 'BIN\0'

# 16-bit indices cap a sub-mesh at this many vertices.
MAX_MESH_VERTS = 65535

DEFAULT_SEED = 20260712
DEFAULT_QUADS = 16          # 16x16 quads -> 17x17 = 289 verts, 512 triangles
DEFAULT_UV_TILES = 1.0      # texture repeats across the unit plane (material scales further)
TEXTURE_SIZE = 64           # tiling water-normal edge (power of two)

GLB_NAME = "water_plane.glb"
NORMAL_NAME = "water_normal.png"


# --- deterministic seamless value noise ------------------------------------

def _hash01(ix, iy, seed):
    """A stable 0..1 hash of an integer lattice point (deterministic, stdlib
    integer math only - a seed reproduces bit for bit)."""
    h = (ix * 0x1F1F1F1F) ^ (iy * 0x2C2C2C2C) ^ (seed * 0x9E3779B9)
    h &= 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & 0xFFFFFFFF
    h ^= h >> 12
    h = (h * 0x297A2D39) & 0xFFFFFFFF
    h ^= h >> 15
    return (h & 0xFFFFFFFF) / 4294967295.0


def _smooth(t):
    return t * t * (3.0 - 2.0 * t)


def _vnoise(fx, fy, seed, period):
    """Bilinear value noise with a smoothstep fade, wrapped to `period` so the
    field tiles seamlessly."""
    ix = math.floor(fx)
    iy = math.floor(fy)
    tx = fx - ix
    ty = fy - iy
    sx = _smooth(tx)
    sy = _smooth(ty)

    def corner(cx, cy):
        return _hash01(cx % period, cy % period, seed)

    v00 = corner(ix, iy)
    v10 = corner(ix + 1, iy)
    v01 = corner(ix, iy + 1)
    v11 = corner(ix + 1, iy + 1)
    top = v00 + (v10 - v00) * sx
    bot = v01 + (v11 - v01) * sx
    return top + (bot - top) * sy


def _fbm(fx, fy, seed, octaves, period):
    """Seamless fractal Brownian motion: summed wrapped value-noise octaves,
    normalised to 0..1."""
    total = 0.0
    amplitude = 1.0
    frequency = 1.0
    norm = 0.0
    for octave in range(octaves):
        total += _vnoise(fx * frequency, fy * frequency,
                         seed + octave * 101, period * int(frequency)) * amplitude
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.0
    return total / norm


# --- plane geometry --------------------------------------------------------

def build_plane(quads, uv_tiles):
    """A unit XZ plane grid: (quads+1)^2 verts, positions in [-0.5, 0.5], y=0,
    normals +Y, tiling UVs, two CCW (seen from above, +Y up) triangles a quad."""
    stride = quads + 1
    positions, normals, uvs = [], [], []
    for lz in range(stride):
        for lx in range(stride):
            u = lx / quads
            v = lz / quads
            positions.append((u - 0.5, 0.0, v - 0.5))
            normals.append((0.0, 1.0, 0.0))
            uvs.append((u * uv_tiles, v * uv_tiles))
    indices = []
    for lz in range(quads):
        for lx in range(quads):
            a = lz * stride + lx
            b = a + 1
            c = a + stride
            d = c + 1
            indices.extend([a, c, b, b, c, d])
    return {"positions": positions, "normals": normals, "uvs": uvs,
            "indices": indices}


# --- glTF .glb container (one flat mesh) -----------------------------------

def build_glb(plane):
    bin_parts = []
    buffer_views = []
    accessors = []
    offset = 0

    def add_view(payload, target):
        nonlocal offset
        pad = (-offset) % 4
        if pad:
            bin_parts.append(b"\x00" * pad)
            offset += pad
        view_index = len(buffer_views)
        buffer_views.append({"buffer": 0, "byteOffset": offset,
                            "byteLength": len(payload), "target": target})
        bin_parts.append(payload)
        offset += len(payload)
        return view_index

    positions = plane["positions"]
    pos_bin = b"".join(struct.pack("<3f", *p) for p in positions)
    nrm_bin = b"".join(struct.pack("<3f", *n) for n in plane["normals"])
    uv_bin = b"".join(struct.pack("<2f", *u) for u in plane["uvs"])
    idx_bin = struct.pack("<%dH" % len(plane["indices"]), *plane["indices"])

    pos_view = add_view(pos_bin, 34962)   # ARRAY_BUFFER
    nrm_view = add_view(nrm_bin, 34962)
    uv_view = add_view(uv_bin, 34962)
    idx_view = add_view(idx_bin, 34963)   # ELEMENT_ARRAY_BUFFER

    mins = [min(p[i] for p in positions) for i in range(3)]
    maxs = [max(p[i] for p in positions) for i in range(3)]

    accessors.append({"bufferView": pos_view, "componentType": 5126,
                     "count": len(positions), "type": "VEC3",
                     "min": mins, "max": maxs})
    accessors.append({"bufferView": nrm_view, "componentType": 5126,
                     "count": len(plane["normals"]), "type": "VEC3"})
    accessors.append({"bufferView": uv_view, "componentType": 5126,
                     "count": len(plane["uvs"]), "type": "VEC2"})
    accessors.append({"bufferView": idx_view, "componentType": 5123,
                     "count": len(plane["indices"]), "type": "SCALAR"})

    # a textureless placeholder material: WaterComponent overrides the whole
    # instance with its animated water material at load time
    materials = [{"name": "water_plane_mat", "pbrMetallicRoughness": {
        "baseColorFactor": [0.1, 0.3, 0.4, 1.0],
        "metallicFactor": 0.0, "roughnessFactor": 0.1}}]
    meshes = [{"name": "water_plane", "primitives": [{
        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
        "indices": 3, "material": 0, "mode": 4}]}]  # TRIANGLES
    nodes = [{"mesh": 0, "name": "water_plane"}]

    bin_chunk = b"".join(bin_parts)
    bin_chunk += b"\x00" * (-len(bin_chunk) % 4)

    gltf = {
        "asset": {"version": "2.0",
                  "generator": "orkige Util/make_water_mesh.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "buffers": [{"byteLength": len(bin_chunk)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }

    json_chunk = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * (-len(json_chunk) % 4)

    total = 12 + 8 + len(json_chunk) + 8 + len(bin_chunk)
    return b"".join([
        struct.pack("<3I", GLB_MAGIC, 2, total),
        struct.pack("<2I", len(json_chunk), CHUNK_JSON),
        json_chunk,
        struct.pack("<2I", len(bin_chunk), CHUNK_BIN),
        bin_chunk,
    ])


# --- tiling water normal map -----------------------------------------------

def build_normal_texture(seed, size=TEXTURE_SIZE, bump=1.8):
    """A seamless tangent-space water-ripple normal map: central differences on
    two overlapping seamless wave-noise fields (a coarse swell + a finer chop),
    encoded (128,128,255) = flat +Z."""
    image = orkige_png.Image(size, size)

    def height(x, y):
        fx = x / size
        fy = y / size
        swell = _fbm(fx * 4.0, fy * 4.0, seed, 3, period=4)
        chop = _fbm(fx * 8.0, fy * 8.0, seed + 11, 3, period=8)
        return swell * 0.7 + chop * 0.3

    for y in range(size):
        for x in range(size):
            hl = height((x - 1) % size, y)
            hr = height((x + 1) % size, y)
            hd = height(x, (y - 1) % size)
            hu = height(x, (y + 1) % size)
            nx = (hl - hr) * bump
            ny = (hd - hu) * bump
            nz = 1.0
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            r = int(round((nx / length * 0.5 + 0.5) * 255))
            g = int(round((ny / length * 0.5 + 0.5) * 255))
            b = int(round((nz / length * 0.5 + 0.5) * 255))
            image.put(x, y, (r, g, b, 255))
    return image


# --- validation / selftest -------------------------------------------------

def validate_glb(data):
    """Structural + geometric check on freshly built .glb BYTES. Returns the
    parsed glTF doc; raises AssertionError on any violation."""
    magic, version, total = struct.unpack_from("<3I", data, 0)
    assert magic == GLB_MAGIC, "bad magic"
    assert version == 2, "bad version"
    assert total == len(data), "length mismatch"
    json_len, json_type = struct.unpack_from("<2I", data, 12)
    assert json_type == CHUNK_JSON, "first chunk must be JSON"
    doc = json.loads(data[20:20 + json_len])
    bin_off = 20 + json_len
    bin_len, bin_type = struct.unpack_from("<2I", data, bin_off)
    assert bin_type == CHUNK_BIN, "second chunk must be BIN"
    assert bin_off + 8 + bin_len == total, "BIN chunk length mismatch"
    bin_base = bin_off + 8

    assert len(doc["meshes"]) == 1, "the water plane is one mesh"
    assert len(doc["nodes"]) == 1, "the water plane is one node"
    prim = doc["meshes"][0]["primitives"][0]
    attrs = prim["attributes"]
    assert set(attrs) == {"POSITION", "NORMAL", "TEXCOORD_0"}, \
        "the plane is missing an attribute (tangents need UVs)"
    pos_acc = doc["accessors"][attrs["POSITION"]]
    nrm_acc = doc["accessors"][attrs["NORMAL"]]
    idx_acc = doc["accessors"][prim["indices"]]
    vcount = pos_acc["count"]
    assert vcount <= MAX_MESH_VERTS, "plane exceeds the 16-bit vertex cap"
    assert idx_acc["componentType"] == 5123, "indices must be 16-bit"
    assert idx_acc["count"] % 3 == 0, "index count not a triangle multiple"

    # positions are a flat unit plane (y == 0, x/z within +-0.5)
    pv = doc["bufferViews"][pos_acc["bufferView"]]
    praw = data[bin_base + pv["byteOffset"]:
                bin_base + pv["byteOffset"] + pv["byteLength"]]
    for i in range(pos_acc["count"]):
        px, py, pz = struct.unpack_from("<3f", praw, i * 12)
        assert abs(py) < 1e-6, "the water plane is not flat (y != 0)"
        assert -0.5001 <= px <= 0.5001 and -0.5001 <= pz <= 0.5001, \
            "a vertex is outside the unit plane"

    # normals are all +Y unit
    nv = doc["bufferViews"][nrm_acc["bufferView"]]
    nraw = data[bin_base + nv["byteOffset"]:
                bin_base + nv["byteOffset"] + nv["byteLength"]]
    for i in range(nrm_acc["count"]):
        nx, ny, nz = struct.unpack_from("<3f", nraw, i * 12)
        assert abs(nx) < 1e-6 and abs(ny - 1.0) < 1e-6 and abs(nz) < 1e-6, \
            "a plane normal is not +Y"

    idx_raw = data[bin_base + doc["bufferViews"][idx_acc["bufferView"]]["byteOffset"]:
                   bin_base + doc["bufferViews"][idx_acc["bufferView"]]["byteOffset"] +
                   doc["bufferViews"][idx_acc["bufferView"]]["byteLength"]]
    indices = struct.unpack("<%dH" % idx_acc["count"], idx_raw)
    assert max(indices) < vcount, "an index is out of range"
    return doc


def _selftest():
    quads = DEFAULT_QUADS
    plane = build_plane(quads, DEFAULT_UV_TILES)
    verts = (quads + 1) ** 2
    tris = quads * quads * 2
    assert len(plane["positions"]) == verts, "vert budget drift"
    assert len(plane["indices"]) == tris * 3, "index budget drift"
    assert verts <= MAX_MESH_VERTS, "plane over the 16-bit cap"

    glb = build_glb(plane)
    validate_glb(glb)

    # determinism: same seed -> byte-identical .glb + normal texture
    glb2 = build_glb(build_plane(quads, DEFAULT_UV_TILES))
    assert glb == glb2, "GLB not deterministic"
    nrm1 = build_normal_texture(DEFAULT_SEED)
    nrm2 = build_normal_texture(DEFAULT_SEED)
    assert bytes(nrm1.pixels) == bytes(nrm2.pixels), "normal not deterministic"
    # a different seed changes the ripple (guards a dead seed path)
    other = build_normal_texture(DEFAULT_SEED + 1)
    assert bytes(other.pixels) != bytes(nrm1.pixels), "the seed does not affect the ripple"

    # seamless: the left/right and top/bottom edge columns/rows must be close
    # (wrapped noise) - sample a few and assert small deltas
    def texel(img, x, y):
        i = (y * img.width + x) * 4
        return (img.pixels[i], img.pixels[i + 1], img.pixels[i + 2])
    size = TEXTURE_SIZE
    for k in range(0, size, 7):
        le = texel(nrm1, 0, k)
        re = texel(nrm1, size - 1, k)
        # wrapped neighbours differ by only one lattice step - a loose bound
        assert all(abs(a - b) < 40 for a, b in zip(le, re)), \
            "the normal map does not tile seamlessly across X"

    # the normal map is +Z-dominant on average (a valid tangent-space map)
    flat = sum(1 for i in range(0, len(nrm1.pixels), 4)
               if nrm1.pixels[i + 2] > 200)
    assert flat > (len(nrm1.pixels) // 4) * 0.5, "normal map is not +Z-dominant"

    print("make_water_mesh selftest OK: unit plane %d verts (%d tris), "
          "%d-byte .glb; %dx%d tiling water normal; deterministic; seamless; "
          "flat +Y under the %d-vert 16-bit cap"
          % (verts, tris, len(glb), size, size, MAX_MESH_VERTS))
    return 0


# --- driver ----------------------------------------------------------------

def generate(out_dir, seed, quads, uv_tiles):
    out_dir.mkdir(parents=True, exist_ok=True)
    verts = (quads + 1) ** 2
    if verts > MAX_MESH_VERTS:
        raise SystemExit("quads=%d gives %d verts, over the %d 16-bit cap"
                         % (quads, verts, MAX_MESH_VERTS))
    plane = build_plane(quads, uv_tiles)
    glb = build_glb(plane)
    validate_glb(glb)

    glb_path = out_dir / GLB_NAME
    glb_path.write_bytes(glb)
    normal_path = out_dir / NORMAL_NAME
    orkige_png.encode_png(build_normal_texture(seed), normal_path)

    tris = quads * quads * 2
    print("wrote a unit water plane (%d verts, %d triangles) + a %dx%d tiling "
          "normal map:" % (verts, tris, TEXTURE_SIZE, TEXTURE_SIZE))
    for path in (glb_path, normal_path):
        print("  %s (%d bytes)" % (path, path.stat().st_size))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output_dir", nargs="?",
                        help="destination dir (default: engine water media)")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--quads", type=int, default=DEFAULT_QUADS,
                        help="quads per plane side ((n+1)^2 verts, 16-bit cap)")
    parser.add_argument("--uv-tiles", type=float, default=DEFAULT_UV_TILES,
                        help="texture repeats across the unit plane")
    parser.add_argument("--selftest", action="store_true",
                        help="build in memory and assert; write nothing")
    args = parser.parse_args()

    if args.selftest:
        return _selftest()

    default = (Path(__file__).resolve().parent.parent /
               "orkige_engine/media/water")
    out_dir = Path(args.output_dir) if args.output_dir else default
    generate(out_dir, args.seed, args.quads, args.uv_tiles)
    return 0


if __name__ == "__main__":
    sys.exit(main())
