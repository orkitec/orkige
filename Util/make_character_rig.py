#!/usr/bin/env python3
"""Generate a blocky SKINNED character rig - a glTF .glb mannequin with a
programmatic skeleton, per-vertex weights and two keyframed animation clips.

This is the house test rig for CHARACTER animation (the deterministic,
art-free counterpart of make_terrain_mesh.py's baked terrain): a seven-joint
mannequin (root/spine/head + two arms + two legs) skinned to blocky body-part
boxes, carrying a `walk` cycle and an `idle` sway. It is the 3D skeletal-
animation proof fixture - loaded by the player's character-rig selfcheck to
verify that clip playback moves bone-driven vertices, and by the editor/MCP
authoring paths as a real animated asset.

Everything is generated deterministically with the Python standard library
only (no numpy, no external DCC export - license-clean, tiny, reproducible),
reusing the same stdlib glTF 2.0 binary container as make_terrain_mesh.py,
extended with the glTF skinning vocabulary:

  * skin           joints + inverseBindMatrices (one MAT4 per joint)
  * JOINTS_0       per-vertex joint indices (VEC4 unsigned byte)
  * WEIGHTS_0      per-vertex blend weights (VEC4 float, sum == 1)
  * animations     one glTF animation per clip; rotation samplers (quaternion
                   VEC4 output over a shared time input) target joint nodes

The skeleton (joint : parent : local translation):
    0 root   -1   (0, 1.90, 0)      the pelvis, the skin's skeleton root
    1 spine   0   (0, 0.55, 0)
    2 head    1   (0, 0.65, 0)
    3 armL    1   (0.50, 0.45, 0)
    4 armR    1   (-0.50, 0.45, 0)
    5 legL    0   (0.22, -0.90, 0)
    6 legR    0   (-0.22, -0.90, 0)
Rest transforms are translation-only, so a joint's inverse bind matrix is a
translation by minus its world rest position, and at the rest pose every skin
matrix is identity (the mesh loads undeformed).

The two clips are pure joint ROTATIONS about X (limbs swinging forward/back),
so playback visibly moves the skinned box vertices in world space:
  * walk   1.0 s, looping   legs + arms swing in opposition (a stride)
  * idle   2.0 s, looping   a gentle spine/head/arm sway

Usage:
    python3 Util/make_character_rig.py [output_dir]    # write the rig .glb
    python3 Util/make_character_rig.py --selftest      # validate, no writes
Defaults to tests/projects/character/assets/ next to this repo. The script
re-opens the written .glb and validates the skeleton/skin/clip structure.
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path

GLB_MAGIC = 0x46546C67  # 'glTF'
CHUNK_JSON = 0x4E4F534A  # 'JSON'
CHUNK_BIN = 0x004E4942  # 'BIN\0'

GLB_NAME = "character_rig.glb"

# --- the skeleton (joint : parent : LOCAL translation) ---------------------
# index-ordered so a parent always precedes its children (a forward pass
# composes world transforms; glTF and the engine both want this order).
JOINTS = [
    ("root",  -1, (0.0,  1.90, 0.0)),
    ("spine",  0, (0.0,  0.55, 0.0)),
    ("head",   1, (0.0,  0.65, 0.0)),
    ("armL",   1, (0.50, 0.45, 0.0)),
    ("armR",   1, (-0.50, 0.45, 0.0)),
    ("legL",   0, (0.22, -0.90, 0.0)),
    ("legR",   0, (-0.22, -0.90, 0.0)),
]

# --- the skinned body-part boxes (joint : world-rest centre : half-extents)
# Each box is bound (fully, except the torso's shared bottom face) to one
# joint; the box sits at the joint's rest pose so the figure reads as a
# mannequin. The torso's bottom four vertices blend 50/50 pelvis/spine to
# exercise real multi-joint weighting (still summing to 1).
BOXES = [
    # joint_index, centre,            half-extents
    (0, (0.0,  1.90, 0.0), (0.32, 0.22, 0.20)),   # pelvis
    (1, (0.0,  2.45, 0.0), (0.38, 0.40, 0.22)),   # torso (blended bottom)
    (2, (0.0,  3.25, 0.0), (0.26, 0.28, 0.26)),   # head
    (3, (0.66, 2.35, 0.0), (0.12, 0.50, 0.12)),   # left arm
    (4, (-0.66, 2.35, 0.0), (0.12, 0.50, 0.12)),  # right arm
    (5, (0.22, 0.50, 0.0), (0.14, 0.50, 0.14)),   # left leg
    (6, (-0.22, 0.50, 0.0), (0.14, 0.50, 0.14)),  # right leg
]
TORSO_BOX = 1  # the box whose bottom face blends to the pelvis

# --- the two animation clips -----------------------------------------------
# A clip is a dict of joint_index -> list of (time_seconds, x_rotation_deg)
# keys (rotation about the X axis; the sampler emits quaternions). Looping
# clips repeat the first key's value at the end so the wrap is seamless.
WALK_DURATION = 1.0
IDLE_DURATION = 2.0

WALK_CLIP = {
    5: [(0.0,  25.0), (0.5, -25.0), (1.0,  25.0)],   # left leg
    6: [(0.0, -25.0), (0.5,  25.0), (1.0, -25.0)],   # right leg
    3: [(0.0, -20.0), (0.5,  20.0), (1.0, -20.0)],   # left arm (counter-swing)
    4: [(0.0,  20.0), (0.5, -20.0), (1.0,  20.0)],   # right arm
    1: [(0.0,   0.0), (0.5,   4.0), (1.0,   0.0)],   # spine bob
}
IDLE_CLIP = {
    1: [(0.0, 0.0), (1.0,  4.0), (2.0, 0.0)],        # spine lean
    2: [(0.0, 0.0), (1.0, -3.0), (2.0, 0.0)],        # head nod
    3: [(0.0, 0.0), (1.0,  5.0), (2.0, 0.0)],        # arm sway
    4: [(0.0, 0.0), (1.0, -5.0), (2.0, 0.0)],
}
CLIPS = [
    ("walk", WALK_DURATION, WALK_CLIP),
    ("idle", IDLE_DURATION, IDLE_CLIP),
]


# --- skeleton maths ---------------------------------------------------------

def joint_world_rest():
    """World-space rest position of each joint (translation-only chain)."""
    world = []
    for _, parent, local in JOINTS:
        base = world[parent] if parent >= 0 else (0.0, 0.0, 0.0)
        world.append((base[0] + local[0], base[1] + local[1],
                      base[2] + local[2]))
    return world


def inverse_bind_matrix(world_pos):
    """glTF inverse bind matrix (column-major 16 floats) for a translation-only
    rest transform: the inverse is a translation by minus the world position."""
    x, y, z = world_pos
    return [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            -x, -y, -z, 1.0]


def quat_x(degrees):
    """Quaternion (x, y, z, w) for a rotation of `degrees` about the X axis."""
    half = math.radians(degrees) * 0.5
    return (math.sin(half), 0.0, 0.0, math.cos(half))


# --- box geometry (per-face verts so the blocky normals are flat) ----------

# Unit cube faces: (normal, four corner signs in CCW winding seen from outside)
_CUBE_FACES = [
    ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),    # +Z
    ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),  # -Z
    ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),    # +X
    ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),  # -X
    ((0, 1, 0), [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),    # +Y
    ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]),  # -Y
]


def build_mesh():
    """Return (positions, normals, joints, weights, indices) for the whole
    mannequin: every body-part box, per-face vertices, skin-weighted."""
    positions, normals, joints, weights, indices = [], [], [], [], []
    for box_index, (joint, centre, half) in enumerate(BOXES):
        for normal, corners in _CUBE_FACES:
            base = len(positions)
            is_bottom = normal == (0, -1, 0)
            for sx, sy, sz in corners:
                px = centre[0] + sx * half[0]
                py = centre[1] + sy * half[1]
                pz = centre[2] + sz * half[2]
                positions.append((px, py, pz))
                normals.append(tuple(float(c) for c in normal))
                # the torso's bottom face blends to the pelvis; everything
                # else binds fully to its own joint
                if box_index == TORSO_BOX and is_bottom:
                    joints.append((joint, 0, 0, 0))
                    weights.append((0.5, 0.5, 0.0, 0.0))
                else:
                    joints.append((joint, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
            indices.extend([base, base + 1, base + 2,
                            base, base + 2, base + 3])
    return positions, normals, joints, weights, indices


# --- glTF .glb container ----------------------------------------------------

def build_glb():
    """Pack the mannequin + skeleton + skin + two clips into a single-buffer
    glTF 2.0 .glb."""
    positions, normals, joints, weights, indices = build_mesh()
    world_rest = joint_world_rest()

    bin_parts = []
    buffer_views = []
    accessors = []
    offset = 0

    def add_view(payload, target=None):
        nonlocal offset
        pad = (-offset) % 4
        if pad:
            bin_parts.append(b"\x00" * pad)
            offset += pad
        view_index = len(buffer_views)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        buffer_views.append(view)
        bin_parts.append(payload)
        offset += len(payload)
        return view_index

    def add_accessor(**kw):
        accessors.append(kw)
        return len(accessors) - 1

    # --- geometry accessors ---
    pos_bin = b"".join(struct.pack("<3f", *p) for p in positions)
    nrm_bin = b"".join(struct.pack("<3f", *n) for n in normals)
    jnt_bin = b"".join(struct.pack("<4B", *j) for j in joints)
    wgt_bin = b"".join(struct.pack("<4f", *w) for w in weights)
    idx_bin = struct.pack("<%dH" % len(indices), *indices)

    mins = [min(p[i] for p in positions) for i in range(3)]
    maxs = [max(p[i] for p in positions) for i in range(3)]

    pos_acc = add_accessor(bufferView=add_view(pos_bin, 34962),
                           componentType=5126, count=len(positions),
                           type="VEC3", min=mins, max=maxs)
    nrm_acc = add_accessor(bufferView=add_view(nrm_bin, 34962),
                           componentType=5126, count=len(normals), type="VEC3")
    jnt_acc = add_accessor(bufferView=add_view(jnt_bin, 34962),
                           componentType=5121, count=len(joints), type="VEC4")
    wgt_acc = add_accessor(bufferView=add_view(wgt_bin, 34962),
                           componentType=5126, count=len(weights), type="VEC4")
    idx_acc = add_accessor(bufferView=add_view(idx_bin, 34963),
                           componentType=5123, count=len(indices),
                           type="SCALAR")

    # --- inverse bind matrices (one MAT4 per joint) ---
    ibm_bin = b"".join(struct.pack("<16f", *inverse_bind_matrix(w))
                       for w in world_rest)
    ibm_acc = add_accessor(bufferView=add_view(ibm_bin),
                           componentType=5126, count=len(JOINTS), type="MAT4")

    # --- animation accessors (shared time input + a quaternion output per
    #     animated joint) ---
    animations = []
    for clip_name, duration, channels in CLIPS:
        samplers = []
        anim_channels = []
        for joint_index, keys in sorted(channels.items()):
            times = [t for t, _ in keys]
            time_bin = b"".join(struct.pack("<f", t) for t in times)
            time_acc = add_accessor(bufferView=add_view(time_bin),
                                    componentType=5126, count=len(times),
                                    type="SCALAR", min=[min(times)],
                                    max=[max(times)])
            quats = [quat_x(deg) for _, deg in keys]
            quat_bin = b"".join(struct.pack("<4f", *q) for q in quats)
            quat_acc = add_accessor(bufferView=add_view(quat_bin),
                                    componentType=5126, count=len(quats),
                                    type="VEC4")
            sampler_index = len(samplers)
            samplers.append({"input": time_acc, "output": quat_acc,
                             "interpolation": "LINEAR"})
            anim_channels.append({"sampler": sampler_index,
                                  "target": {"node": joint_index,
                                             "path": "rotation"}})
        animations.append({"name": clip_name, "samplers": samplers,
                           "channels": anim_channels})

    # --- nodes: the joint hierarchy (0..6) + the skinned mesh node (7) ---
    nodes = []
    children = {i: [] for i in range(len(JOINTS))}
    for index, (_, parent, _) in enumerate(JOINTS):
        if parent >= 0:
            children[parent].append(index)
    for index, (name, _, local) in enumerate(JOINTS):
        node = {"name": name, "translation": list(local)}
        if children[index]:
            node["children"] = children[index]
        nodes.append(node)
    mesh_node = len(nodes)
    nodes.append({"name": "mannequin", "mesh": 0, "skin": 0})

    gltf = {
        "asset": {"version": "2.0",
                  "generator": "orkige Util/make_character_rig.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, mesh_node]}],  # skeleton root + mesh
        "nodes": nodes,
        "meshes": [{"name": "mannequin", "primitives": [{
            "attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc,
                           "JOINTS_0": jnt_acc, "WEIGHTS_0": wgt_acc},
            "indices": idx_acc, "material": 0, "mode": 4}]}],
        "materials": [{"name": "mannequin_mat", "pbrMetallicRoughness": {
            "baseColorFactor": [0.7, 0.72, 0.78, 1.0],
            "metallicFactor": 0.0, "roughnessFactor": 0.8}}],
        "skins": [{"skeleton": 0, "joints": list(range(len(JOINTS))),
                   "inverseBindMatrices": ibm_acc}],
        "animations": animations,
        "buffers": [{"byteLength": 0}],  # patched below
        "bufferViews": buffer_views,
        "accessors": accessors,
    }

    bin_chunk = b"".join(bin_parts)
    bin_chunk += b"\x00" * (-len(bin_chunk) % 4)
    gltf["buffers"][0]["byteLength"] = len(bin_chunk)

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


# --- validation / selftest --------------------------------------------------

def validate_glb(data):
    """Structural + skinning check on freshly built .glb BYTES. Returns the
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

    def read_view(accessor):
        view = doc["bufferViews"][accessor["bufferView"]]
        start = bin_base + view["byteOffset"]
        return data[start:start + view["byteLength"]]

    # --- skeleton / skin ---
    assert len(doc["nodes"]) == len(JOINTS) + 1, "node count wrong"
    skin = doc["skins"][0]
    assert len(skin["joints"]) == len(JOINTS), "joint count wrong"
    assert skin["skeleton"] == 0, "skeleton root must be joint 0"
    ibm_acc = doc["accessors"][skin["inverseBindMatrices"]]
    assert ibm_acc["type"] == "MAT4" and ibm_acc["count"] == len(JOINTS), \
        "one inverse bind matrix per joint"

    # the mesh node references the skin
    mesh_nodes = [n for n in doc["nodes"] if "skin" in n]
    assert len(mesh_nodes) == 1 and mesh_nodes[0]["skin"] == 0, \
        "exactly one skinned mesh node"

    # --- mesh attributes + weights ---
    prim = doc["meshes"][0]["primitives"][0]
    attrs = prim["attributes"]
    assert set(attrs) >= {"POSITION", "NORMAL", "JOINTS_0", "WEIGHTS_0"}, \
        "the skinned primitive is missing an attribute"
    pos_acc = doc["accessors"][attrs["POSITION"]]
    jnt_acc = doc["accessors"][attrs["JOINTS_0"]]
    wgt_acc = doc["accessors"][attrs["WEIGHTS_0"]]
    vcount = pos_acc["count"]
    assert jnt_acc["count"] == vcount and wgt_acc["count"] == vcount, \
        "JOINTS_0/WEIGHTS_0 count disagrees with POSITION"
    assert jnt_acc["componentType"] == 5121, "JOINTS_0 must be unsigned byte"

    # every vertex's weights sum to 1 and reference valid joints
    jnt_raw = read_view(jnt_acc)
    wgt_raw = read_view(wgt_acc)
    saw_blended = False
    for i in range(vcount):
        j = struct.unpack_from("<4B", jnt_raw, i * 4)
        w = struct.unpack_from("<4f", wgt_raw, i * 16)
        assert abs(sum(w) - 1.0) < 1e-5, "a vertex's weights do not sum to 1"
        for slot in range(4):
            assert 0 <= j[slot] < len(JOINTS), "a joint index is out of range"
        nonzero = sum(1 for x in w if x > 0.0)
        if nonzero >= 2:
            saw_blended = True
    assert saw_blended, "no multi-joint (blended) vertex - weighting untested"

    # indices in range + triangle multiple
    idx_acc = doc["accessors"][prim["indices"]]
    assert idx_acc["count"] % 3 == 0, "index count not a triangle multiple"
    idx_raw = read_view(idx_acc)
    idxs = struct.unpack("<%dH" % idx_acc["count"], idx_raw)
    assert max(idxs) < vcount and min(idxs) >= 0, "an index is out of range"

    # --- animations / clips ---
    assert len(doc["animations"]) == len(CLIPS), "clip count wrong"
    for (name, duration, channels), anim in zip(CLIPS, doc["animations"]):
        assert anim["name"] == name, "clip name mismatch"
        assert len(anim["channels"]) == len(channels), "channel count wrong"
        # the clip duration is the max sampler input time
        clip_max = 0.0
        for sampler in anim["samplers"]:
            in_acc = doc["accessors"][sampler["input"]]
            clip_max = max(clip_max, in_acc["max"][0])
            out_acc = doc["accessors"][sampler["output"]]
            assert out_acc["type"] == "VEC4", "rotation output must be VEC4"
        assert abs(clip_max - duration) < 1e-6, \
            "clip '%s' duration %.3f != %.3f" % (name, clip_max, duration)
        # every channel targets a real joint node's rotation
        for channel in anim["channels"]:
            assert channel["target"]["path"] == "rotation", \
                "a channel does not drive rotation"
            assert 0 <= channel["target"]["node"] < len(JOINTS), \
                "a channel targets a non-joint node"
    return doc


def _selftest():
    glb = build_glb()
    doc = validate_glb(glb)

    # determinism: same inputs -> byte-identical .glb
    assert build_glb() == glb, "GLB not deterministic"

    # the rest pose really is undeformed: the inverse-bind translation must be
    # minus each joint's world rest position (so skinMatrix == identity at rest)
    world = joint_world_rest()
    for wpos in world:
        ibm = inverse_bind_matrix(wpos)
        assert (abs(ibm[12] + wpos[0]) < 1e-6 and abs(ibm[13] + wpos[1]) < 1e-6
                and abs(ibm[14] + wpos[2]) < 1e-6), "inverse bind mismatch"

    vcount = doc["accessors"][
        doc["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]["count"]
    print("make_character_rig selftest OK: %d joints, %d skinned verts "
          "(per-face boxes), %d clips (%s), %d-byte .glb; deterministic; "
          "weights sum to 1 with a blended joint; rest pose undeformed"
          % (len(JOINTS), vcount, len(CLIPS),
             ", ".join("%s %.1fs" % (n, d) for n, d, _ in CLIPS), len(glb)))
    return 0


# --- driver -----------------------------------------------------------------

def generate(out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    glb = build_glb()
    validate_glb(glb)
    glb_path = out_dir / GLB_NAME
    glb_path.write_bytes(glb)
    print("wrote %s (%d bytes): %d joints, %d clips"
          % (glb_path, glb_path.stat().st_size, len(JOINTS), len(CLIPS)))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output_dir", nargs="?",
                        help="destination dir (default: the character fixture)")
    parser.add_argument("--selftest", action="store_true",
                        help="build in memory and assert; write nothing")
    args = parser.parse_args()

    if args.selftest:
        return _selftest()

    default = (Path(__file__).resolve().parent.parent
               / "tests/projects/character/assets")
    out_dir = Path(args.output_dir) if args.output_dir else default
    generate(out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
