#!/usr/bin/env python3
"""Generate a baked-mesh terrain - a chunked glTF .glb + a tiling PBS material.

This is the v1 terrain answer: a BAKED static mesh (no engine facade growth, no
runtime terrain component, mobile-safe), rendered through the existing
ModelComponent + `.omat` path. A procedural fractal (fBm value) heightfield is
turned into a grid of self-contained mesh CHUNKS - each chunk a glTF mesh (an
engine sub-mesh), frustum-cullable, indexed with 16-bit indices - carrying
proper per-vertex NORMALs (central-difference over the shared global heightfield,
so chunk borders match with no cracks or seams) and tiling TEXCOORD_0s (so a
small seamless ground texture repeats across the terrain, and the engine builds
tangents from the UVs so a normal map applies on the Ogre-Next flavor). One
tiling `.omat` ground material covers every chunk (the splat-baked-albedo
simplification for v1).

Everything is generated deterministically from a seed with the Python standard
library only (no PIL, no numpy, no external heightmap data - license-clean, tiny,
reproducible): the same seeded fBm as the noise below, a seamless-tiling
value-noise texture pair (albedo + tangent-space normal), and the same
stdlib glTF 2.0 binary container as make_uvcheck_mesh.py / make_material_demo.py.

Mobile vertex budgets (the chunk geometry knobs, --chunks / --chunk-quads):
  * A chunk is chunk_quads x chunk_quads quads -> (chunk_quads+1)^2 vertices,
    chunk_quads*chunk_quads*6 indices. 16-bit indices cap a sub-mesh at 65535
    vertices; the generator ENFORCES (chunk_quads+1)^2 <= 65535 so every chunk
    is one 16-bit-indexed draw (a hard error otherwise). Mobile vista guidance:
    keep chunk_quads <= 64 (4225 verts / 24576 indices per chunk - a comfortable
    single draw on Metal/Vulkan) and build the world from an 8x8..16x16 chunk
    grid - a streaming-free, per-chunk-cullable vista at a few hundred thousand
    triangles.
  * The COMMITTED demo terrain (the defaults, written next to this repo) is
    deliberately small and repo-friendly: a 3x3 chunk grid of 10x10-quad chunks
    = 9 sub-meshes, 121 verts / 200 triangles per chunk, 1089 verts / 1800
    triangles total over a 48x48-unit patch.

Usage:
    python3 Util/make_terrain_mesh.py [output_dir]      # write the demo set
    python3 Util/make_terrain_mesh.py --chunks 8 --chunk-quads 48 out/
    python3 Util/make_terrain_mesh.py --selftest        # validate, no writes
Defaults to samples/hello_orkige/media/ next to this repo. The script re-opens
the written .glb and validates the chunk layout.
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

# 16-bit indices cap a sub-mesh (one chunk) at this many vertices.
MAX_CHUNK_VERTS = 65535

# The committed demo terrain (small, reproducible, repo-friendly).
DEFAULT_SEED = 20260712
DEFAULT_CHUNKS = 3          # 3x3 chunk grid
DEFAULT_CHUNK_QUADS = 10    # 10x10 quads per chunk -> 11x11 = 121 verts
DEFAULT_WORLD_SIZE = 48.0   # world units the whole terrain spans (X and Z)
DEFAULT_HEIGHT = 5.0        # peak-to-floor height amplitude in world units
DEFAULT_UV_TILES_PER_CHUNK = 2.0  # ground-texture repeats across one chunk
TEXTURE_SIZE = 64           # tiling ground/normal texture edge (power of two)

GLB_NAME = "demo_terrain.glb"
ALBEDO_NAME = "demo_terrain_albedo.png"
NORMAL_NAME = "demo_terrain_normal.png"
OMAT_NAME = "demo_terrain.omat"


# --- deterministic value noise --------------------------------------------

def _hash01(ix, iy, seed):
    """A stable 0..1 hash of an integer lattice point (deterministic, stdlib
    integer math only - no float RNG, so a seed reproduces bit for bit)."""
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


def _vnoise(fx, fy, seed, period=None):
    """Bilinear value noise with a smoothstep fade. `period` (an integer lattice
    period) wraps the corner hashes so the field tiles seamlessly - used for the
    ground textures; None for the open heightfield."""
    ix = math.floor(fx)
    iy = math.floor(fy)
    tx = fx - ix
    ty = fy - iy
    sx = _smooth(tx)
    sy = _smooth(ty)

    def corner(cx, cy):
        if period is not None:
            cx %= period
            cy %= period
        return _hash01(cx, cy, seed)

    v00 = corner(ix, iy)
    v10 = corner(ix + 1, iy)
    v01 = corner(ix, iy + 1)
    v11 = corner(ix + 1, iy + 1)
    top = v00 + (v10 - v00) * sx
    bot = v01 + (v11 - v01) * sx
    return top + (bot - top) * sy


def _fbm(fx, fy, seed, octaves, period=None):
    """Fractal Brownian motion: summed value-noise octaves, normalised to 0..1.
    With an integer `period` every octave frequency stays an integer multiple of
    the base period, so the sum tiles seamlessly (used for the textures)."""
    total = 0.0
    amplitude = 1.0
    frequency = 1.0
    norm = 0.0
    for octave in range(octaves):
        oct_period = period * int(frequency) if period is not None else None
        total += _vnoise(fx * frequency, fy * frequency,
                         seed + octave * 101, oct_period) * amplitude
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.0
    return total / norm


# --- heightfield + geometry ------------------------------------------------

def build_heightfield(verts_per_side, seed, height_amp,
                      base_frequency=3.0, octaves=5):
    """A (verts_per_side x verts_per_side) grid of heights in world units.

    Sampled once over the WHOLE terrain so chunk borders share exact heights.
    A radial falloff sinks the rim so the patch reads as a self-contained
    valley/hill island rather than a clipped tile."""
    grid = []
    last = verts_per_side - 1
    for gz in range(verts_per_side):
        row = []
        for gx in range(verts_per_side):
            u = gx / last
            v = gz / last
            noise = _fbm(u * base_frequency, v * base_frequency, seed, octaves)
            # centre-weighted falloff: 1 at the middle, 0 at the rim
            dx = (u - 0.5) * 2.0
            dz = (v - 0.5) * 2.0
            radial = max(0.0, 1.0 - (dx * dx + dz * dz))
            row.append((noise * 0.7 + 0.3) * radial * height_amp)
        grid.append(row)
    return grid


def build_normals(heights, cell_size):
    """Per-vertex normals from the shared heightfield (central differences on
    the y=f(x,z) surface). Border rows use one-sided differences via clamped
    neighbours; because it is computed on the GLOBAL grid, a vertex shared by two
    chunks gets the SAME normal in both - no seam."""
    n = len(heights)
    last = n - 1
    normals = []
    for gz in range(n):
        row = []
        for gx in range(n):
            xl = heights[gz][max(0, gx - 1)]
            xr = heights[gz][min(last, gx + 1)]
            zl = heights[max(0, gz - 1)][gx]
            zr = heights[min(last, gz + 1)][gx]
            span_x = (min(last, gx + 1) - max(0, gx - 1)) * cell_size
            span_z = (min(last, gz + 1) - max(0, gz - 1)) * cell_size
            dydx = (xr - xl) / span_x
            dydz = (zr - zl) / span_z
            nx, ny, nz = -dydx, 1.0, -dydz
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            row.append((nx / length, ny / length, nz / length))
        normals.append(row)
    return normals


def build_chunks(chunks, chunk_quads, world_size, height_amp, seed,
                 uv_tiles_per_chunk):
    """Return a list of chunk dicts (positions/normals/uvs/indices) plus the
    shared world stats. Each chunk is a self-contained sub-grid extracted from
    the global heightfield, so its 16-bit indices are chunk-local."""
    verts_per_side = chunks * chunk_quads + 1
    cell_size = world_size / (verts_per_side - 1)
    heights = build_heightfield(verts_per_side, seed, height_amp)
    normals = build_normals(heights, cell_size)
    uv_tiles_total = uv_tiles_per_chunk * chunks

    chunk_list = []
    cq = chunk_quads
    last = verts_per_side - 1
    for cz in range(chunks):
        for cx in range(chunks):
            positions, chunk_normals, uvs = [], [], []
            base_x = cx * cq
            base_z = cz * cq
            for lz in range(cq + 1):
                gz = base_z + lz
                for lx in range(cq + 1):
                    gx = base_x + lx
                    wx = (gx / last - 0.5) * world_size
                    wz = (gz / last - 0.5) * world_size
                    positions.append((wx, heights[gz][gx], wz))
                    chunk_normals.append(normals[gz][gx])
                    uvs.append((gx / last * uv_tiles_total,
                               gz / last * uv_tiles_total))
            indices = []
            stride = cq + 1
            for lz in range(cq):
                for lx in range(cq):
                    a = lz * stride + lx
                    b = a + 1
                    c = a + stride
                    d = c + 1
                    # two CCW (seen from above, +Y up) triangles per quad
                    indices.extend([a, c, b, b, c, d])
            chunk_list.append({
                "positions": positions,
                "normals": chunk_normals,
                "uvs": uvs,
                "indices": indices,
            })
    return chunk_list, {
        "verts_per_side": verts_per_side,
        "cell_size": cell_size,
        "world_size": world_size,
    }


# --- glTF .glb container ----------------------------------------------------

def build_glb(chunk_list):
    """Pack the chunks into a single-buffer glTF 2.0 .glb: one mesh + node per
    chunk, each with POSITION / NORMAL / TEXCOORD_0 + indexed triangles."""
    bin_parts = []
    buffer_views = []
    accessors = []
    meshes = []
    nodes = []
    materials = []
    offset = 0

    def add_view(payload, target):
        nonlocal offset
        # 4-byte align the start of every view (glTF alignment rule)
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

    for index, chunk in enumerate(chunk_list):
        positions = chunk["positions"]
        pos_bin = b"".join(struct.pack("<3f", *p) for p in positions)
        nrm_bin = b"".join(struct.pack("<3f", *n) for n in chunk["normals"])
        uv_bin = b"".join(struct.pack("<2f", *u) for u in chunk["uvs"])
        idx_bin = struct.pack("<%dH" % len(chunk["indices"]), *chunk["indices"])

        pos_view = add_view(pos_bin, 34962)   # ARRAY_BUFFER
        nrm_view = add_view(nrm_bin, 34962)
        uv_view = add_view(uv_bin, 34962)
        idx_view = add_view(idx_bin, 34963)   # ELEMENT_ARRAY_BUFFER

        mins = [min(p[i] for p in positions) for i in range(3)]
        maxs = [max(p[i] for p in positions) for i in range(3)]

        pos_acc = len(accessors)
        accessors.append({"bufferView": pos_view, "componentType": 5126,
                         "count": len(positions), "type": "VEC3",
                         "min": mins, "max": maxs})
        nrm_acc = len(accessors)
        accessors.append({"bufferView": nrm_view, "componentType": 5126,
                         "count": len(chunk["normals"]), "type": "VEC3"})
        uv_acc = len(accessors)
        accessors.append({"bufferView": uv_view, "componentType": 5126,
                         "count": len(chunk["uvs"]), "type": "VEC2"})
        idx_acc = len(accessors)
        accessors.append({"bufferView": idx_view, "componentType": 5123,
                         "count": len(chunk["indices"]), "type": "SCALAR"})

        name = "terrain_chunk_%d" % index
        # a DISTINCT (textureless) material per chunk: the static importer
        # merges glTF meshes that share a material into one, which would fuse
        # the chunks - a unique material index per chunk keeps every chunk its
        # own engine sub-mesh. The tiling ground `.omat` is applied over these
        # at load time (whole-instance setMaterial), so they carry no texture.
        materials.append({"name": name + "_mat", "pbrMetallicRoughness": {
            "baseColorFactor": [0.45, 0.5, 0.4, 1.0],
            "metallicFactor": 0.0, "roughnessFactor": 0.9}})
        meshes.append({"name": name, "primitives": [{
            "attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc,
                          "TEXCOORD_0": uv_acc},
            "indices": idx_acc, "material": index, "mode": 4}]})  # TRIANGLES
        nodes.append({"mesh": index, "name": name})

    bin_chunk = b"".join(bin_parts)
    bin_chunk += b"\x00" * (-len(bin_chunk) % 4)

    gltf = {
        "asset": {"version": "2.0",
                  "generator": "orkige Util/make_terrain_mesh.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
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


# --- tiling ground textures -------------------------------------------------

def build_albedo_texture(seed, size=TEXTURE_SIZE):
    """A seamless-tiling earthy ground albedo (mossy green mottled with soil).

    Two seamless fBm fields: a coarse patchiness that mixes green<->brown and a
    fine grain that jitters the value - reads clearly as 'ground' when tiled."""
    image = orkige_png.Image(size, size)
    grass = (86, 122, 54)
    soil = (120, 96, 62)
    for y in range(size):
        for x in range(size):
            fx = x / size * 4.0
            fy = y / size * 4.0
            patch = _fbm(fx, fy, seed, 4, period=4)
            grain = _fbm(fx * 4.0, fy * 4.0, seed + 7, 3, period=16)
            mix = max(0.0, min(1.0, patch * 0.8 + grain * 0.4 - 0.1))
            shade = 0.75 + 0.5 * grain
            r = int(max(0, min(255, (grass[0] * (1 - mix) + soil[0] * mix) * shade)))
            g = int(max(0, min(255, (grass[1] * (1 - mix) + soil[1] * mix) * shade)))
            b = int(max(0, min(255, (grass[2] * (1 - mix) + soil[2] * mix) * shade)))
            image.put(x, y, (r, g, b, 255))
    return image


def build_normal_texture(seed, size=TEXTURE_SIZE, bump=1.4):
    """A seamless tangent-space normal map derived from a seamless height field:
    central differences on the same fBm the albedo grain uses, so the bumps line
    up with the visible grain. Encoded (128,128,255) = flat +Z."""
    image = orkige_png.Image(size, size)

    def height(x, y):
        fx = x / size * 8.0
        fy = y / size * 8.0
        return _fbm(fx, fy, seed + 3, 4, period=32)

    for y in range(size):
        for x in range(size):
            # wrap neighbours for a seamless map
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


OMAT_TEMPLATE = """# baked-terrain ground material - generated by Util/make_terrain_mesh.py
version 1
albedo 1.0 1.0 1.0 1.0
albedoTexture {albedo}
metalness 0.0
roughness 0.9
normalTexture {normal}
"""


# --- validation / selftest --------------------------------------------------

def validate_glb(data, expected_chunks):
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

    assert len(doc["meshes"]) == expected_chunks, "chunk/mesh count wrong"
    assert len(doc["nodes"]) == expected_chunks, "chunk/node count wrong"
    assert len(doc["scenes"][0]["nodes"]) == expected_chunks, \
        "scene root must reference every chunk node"
    # each chunk carries its OWN material so the static importer keeps it a
    # separate sub-mesh (it would fuse same-material chunks otherwise)
    assert len(doc["materials"]) == expected_chunks, "material/chunk count wrong"
    seen_materials = set()
    for mesh in doc["meshes"]:
        prim = mesh["primitives"][0]
        attrs = prim["attributes"]
        assert "material" in prim, "a chunk has no distinct material"
        seen_materials.add(prim["material"])
        assert set(attrs) == {"POSITION", "NORMAL", "TEXCOORD_0"}, \
            "a chunk is missing an attribute"
        pos_acc = doc["accessors"][attrs["POSITION"]]
        nrm_acc = doc["accessors"][attrs["NORMAL"]]
        uv_acc = doc["accessors"][attrs["TEXCOORD_0"]]
        idx_acc = doc["accessors"][prim["indices"]]
        vcount = pos_acc["count"]
        assert vcount <= MAX_CHUNK_VERTS, "chunk exceeds the 16-bit vertex cap"
        assert nrm_acc["count"] == vcount and uv_acc["count"] == vcount, \
            "attribute counts disagree within a chunk"
        assert idx_acc["componentType"] == 5123, "indices must be 16-bit"
        assert idx_acc["count"] % 3 == 0, "index count not a triangle multiple"

        # indices in range
        iv = doc["bufferViews"][idx_acc["bufferView"]]
        raw = data[bin_base + iv["byteOffset"]:
                   bin_base + iv["byteOffset"] + iv["byteLength"]]
        indices = struct.unpack("<%dH" % idx_acc["count"], raw)
        assert max(indices) < vcount, "an index is out of range"
        assert min(indices) >= 0, "a negative index"

        # normal lengths ~ 1, UV bounds finite
        nv = doc["bufferViews"][nrm_acc["bufferView"]]
        nraw = data[bin_base + nv["byteOffset"]:
                    bin_base + nv["byteOffset"] + nv["byteLength"]]
        for i in range(nrm_acc["count"]):
            nx, ny, nz = struct.unpack_from("<3f", nraw, i * 12)
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            assert abs(length - 1.0) < 1e-3, "a normal is not unit-length"
            assert ny > 0.0, "a terrain normal points downward"

        uvv = doc["bufferViews"][uv_acc["bufferView"]]
        uraw = data[bin_base + uvv["byteOffset"]:
                    bin_base + uvv["byteOffset"] + uvv["byteLength"]]
        for i in range(uv_acc["count"]):
            u, v = struct.unpack_from("<2f", uraw, i * 8)
            assert math.isfinite(u) and math.isfinite(v), "a UV is not finite"
    assert len(seen_materials) == expected_chunks, \
        "chunk materials are not distinct (the importer would fuse chunks)"
    return doc


def _selftest():
    chunks = DEFAULT_CHUNKS
    chunk_quads = DEFAULT_CHUNK_QUADS

    chunk_list, stats = build_chunks(chunks, chunk_quads, DEFAULT_WORLD_SIZE,
                                     DEFAULT_HEIGHT, DEFAULT_SEED,
                                     DEFAULT_UV_TILES_PER_CHUNK)
    # budget assertions
    verts_per_chunk = (chunk_quads + 1) ** 2
    tris_per_chunk = chunk_quads * chunk_quads * 2
    assert len(chunk_list) == chunks * chunks, "chunk count wrong"
    assert verts_per_chunk <= MAX_CHUNK_VERTS, "chunk over the 16-bit cap"
    for chunk in chunk_list:
        assert len(chunk["positions"]) == verts_per_chunk, "vert budget drift"
        assert len(chunk["indices"]) == tris_per_chunk * 3, "index budget drift"

    glb = build_glb(chunk_list)
    doc = validate_glb(glb, chunks * chunks)

    # seam check: a vertex shared on a chunk border has the same height in both
    # (the global heightfield guarantees it) - spot the right edge of chunk 0
    # against the left edge of chunk 1 through the world positions.
    def edge_positions(chunk_index, pick):
        cq = chunk_quads
        stride = cq + 1
        result = []
        for lz in range(stride):
            result.append(chunk_list[chunk_index]["positions"][pick(lz, stride)])
        return result
    right = edge_positions(0, lambda lz, stride: lz * stride + (stride - 1))
    left = edge_positions(1, lambda lz, stride: lz * stride + 0)
    for a, b in zip(right, left):
        assert abs(a[1] - b[1]) < 1e-6, "chunk border heights disagree (seam)"

    # determinism: same seed -> byte-identical .glb + textures
    glb2 = build_glb(build_chunks(chunks, chunk_quads, DEFAULT_WORLD_SIZE,
                                  DEFAULT_HEIGHT, DEFAULT_SEED,
                                  DEFAULT_UV_TILES_PER_CHUNK)[0])
    assert glb == glb2, "GLB not deterministic for a fixed seed"

    alb1 = build_albedo_texture(DEFAULT_SEED)
    alb2 = build_albedo_texture(DEFAULT_SEED)
    assert bytes(alb1.pixels) == bytes(alb2.pixels), "albedo not deterministic"
    nrm1 = build_normal_texture(DEFAULT_SEED)
    nrm2 = build_normal_texture(DEFAULT_SEED)
    assert bytes(nrm1.pixels) == bytes(nrm2.pixels), "normal not deterministic"
    # a different seed changes the terrain (guards against a dead seed path)
    other = build_glb(build_chunks(chunks, chunk_quads, DEFAULT_WORLD_SIZE,
                                   DEFAULT_HEIGHT, DEFAULT_SEED + 1,
                                   DEFAULT_UV_TILES_PER_CHUNK)[0])
    assert other != glb, "the seed does not affect the terrain"

    # the normal texture must actually be near-flat +Z on average (a valid map)
    flat = sum(1 for i in range(0, len(nrm1.pixels), 4)
               if nrm1.pixels[i + 2] > 200)
    assert flat > (len(nrm1.pixels) // 4) * 0.5, "normal map is not +Z-dominant"

    print("make_terrain_mesh selftest OK: %d chunks x %d verts (%d tris), "
          "world %.0f units, %d-byte .glb; deterministic; seams matched; "
          "budgets under the %d-vert 16-bit cap"
          % (len(chunk_list), verts_per_chunk, tris_per_chunk,
             stats["world_size"], len(glb), MAX_CHUNK_VERTS))
    return 0


# --- driver -----------------------------------------------------------------

def generate(out_dir, seed, chunks, chunk_quads, world_size, height_amp,
             uv_tiles_per_chunk):
    out_dir.mkdir(parents=True, exist_ok=True)
    verts_per_chunk = (chunk_quads + 1) ** 2
    if verts_per_chunk > MAX_CHUNK_VERTS:
        raise SystemExit(
            "chunk_quads=%d gives %d verts/chunk, over the %d 16-bit cap"
            % (chunk_quads, verts_per_chunk, MAX_CHUNK_VERTS))

    chunk_list, stats = build_chunks(chunks, chunk_quads, world_size,
                                     height_amp, seed, uv_tiles_per_chunk)
    glb = build_glb(chunk_list)
    doc = validate_glb(glb, chunks * chunks)

    glb_path = out_dir / GLB_NAME
    glb_path.write_bytes(glb)
    albedo_path = out_dir / ALBEDO_NAME
    orkige_png.encode_png(build_albedo_texture(seed), albedo_path)
    normal_path = out_dir / NORMAL_NAME
    orkige_png.encode_png(build_normal_texture(seed), normal_path)
    omat_path = out_dir / OMAT_NAME
    omat_path.write_text(OMAT_TEMPLATE.format(albedo=ALBEDO_NAME,
                                              normal=NORMAL_NAME))

    total_verts = sum(len(c["positions"]) for c in chunk_list)
    total_tris = sum(len(c["indices"]) for c in chunk_list) // 3
    print("wrote %d chunks (%d sub-meshes), %d verts, %d triangles over "
          "%.0f world units:" % (len(chunk_list), len(doc["meshes"]),
                                 total_verts, total_tris, stats["world_size"]))
    for path in (glb_path, albedo_path, normal_path, omat_path):
        print("  %s (%d bytes)" % (path, path.stat().st_size))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output_dir", nargs="?",
                        help="destination dir (default: hello_orkige media)")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--chunks", type=int, default=DEFAULT_CHUNKS,
                        help="chunk grid is chunks x chunks")
    parser.add_argument("--chunk-quads", type=int, default=DEFAULT_CHUNK_QUADS,
                        help="quads per chunk side ((n+1)^2 verts, 16-bit cap)")
    parser.add_argument("--world-size", type=float, default=DEFAULT_WORLD_SIZE,
                        help="world units the terrain spans (X and Z)")
    parser.add_argument("--height", type=float, default=DEFAULT_HEIGHT,
                        help="height amplitude in world units")
    parser.add_argument("--uv-tiles-per-chunk", type=float,
                        default=DEFAULT_UV_TILES_PER_CHUNK,
                        help="ground-texture repeats across one chunk")
    parser.add_argument("--selftest", action="store_true",
                        help="build in memory and assert; write nothing")
    args = parser.parse_args()

    if args.selftest:
        return _selftest()

    default = Path(__file__).resolve().parent.parent / "samples/hello_orkige/media"
    out_dir = Path(args.output_dir) if args.output_dir else default
    generate(out_dir, args.seed, args.chunks, args.chunk_quads,
             args.world_size, args.height, args.uv_tiles_per_chunk)
    return 0


if __name__ == "__main__":
    sys.exit(main())
