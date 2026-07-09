#!/usr/bin/env python3
"""Generate projects/roller/ assets and scene - the Continuity x Rolando
prototype ("roller"): a 2x2 grid of movable tiles and a ball rolled by tilt
gravity, built entirely on SpriteComponent (2D) + planar Jolt bodies.

Everything is license-clean and reproducible from the Python standard
library (struct + zlib), following Util/make_jumper_assets.py - but the
sprites need REAL alpha, so the PNG encoder here writes RGBA8.

Textures (projects/roller/assets/):
    ball.png        64x64   shaded red ball (radial light, specular dot,
                            antialiased alpha edge, a dark "roll marker"
                            stripe so the planar spin is visible)
    wall.png        64x64   opaque slate bricks (walls/platforms stretch it)
    tile_frame.png  128x128 beveled steel frame, TRANSPARENT center
    goal.png        64x64   golden star with alpha
    cursor.png      64x64   translucent yellow highlight frame (move mode)

Prefab (projects/roller/assets/tile.oprefab) - written in the exact
XMLArchive form PrefabSerializer::savePrefab produces: the reusable tile
subtree (root "Tile" + frame sprite + all four edge walls as prefab-local
children "Frame"/"WallTop"/"WallBottom"/"WallLeft"/"WallRight").

Scene (projects/roller/scenes/main.oscene) - written in the exact
XMLArchive form SceneSerializer::saveScene produces (see
projects/jumper-lua/scenes/main.oscene for a native reference):

    grid slots (tile size 6):  s2 (-3, 3)   s3 ( 3, 3)
                               s0 (-3,-3)   s1 ( 3,-3)
    Tile A  slot 0  ball spawn; walls top/bottom/left + interior ledge,
                    RIGHT edge open
    Tile B  slot 3  the goal tile; walls top/bottom/right, LEFT edge open
    Tile C  slot 2  fully walled (pure obstacle mass)
    slot 1          EMPTY - the sliding-puzzle hole. Solution: slide B down
                    (into slot 1), then tilt right: the ball rolls through
                    A's right opening into B onto the goal.

    Ball    dynamic planar sphere + ball.png sprite + scripts/ball.lua
    Game    scripts/game.lua (UI, mode state machine, tile sliding)
    Cursor  cursor.png highlight over the empty slot (hidden in play mode)
    Goal    goal.png sprite inside tile B (moves with the tile group)

Each tile is ONE prefab INSTANCE (scene format v3): a "Tile<key>" root at
the slot center referencing assets/tile.oprefab, with the open edges as
suppressedChildren (structural overrides) - the loader instantiates the
prefab subtree under the deterministic ids "Tile<key>/Frame",
"Tile<key>/WallTop", ... and drops the suppressed walls. Scene-side EXTRA
children (TileA's interior ledge, the Goal star inside tile B) stay plain
serialized objects parented under the instance root. Sliding a tile is a
single TransformComponent:teleport of the parent - the engine snaps every
rigid body in the subtree along, even while the simulation is paused.
Wall bodies are KINEMATIC (not static) on purpose: kinematic bodies are
the honest Jolt notion for game-code-moved colliders.

Usage:
    python3 Util/make_roller_assets.py [project_dir]
Defaults to projects/roller/ next to this repo. (The fastgui atlas the HUD
uses is generated separately: Util/make_fastgui_atlas.py <assets_dir>.)
"""

import hashlib
import math
import re
import struct
import sys
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def write_meta(project_dir, asset_path):
    """Emit the asset's sidecar .orkmeta (core_project/AssetDatabase) so the
    generated project carries stable asset ids from the start.

    An existing sidecar is PRESERVED (its id is the asset's identity - the
    whole point of the database); a missing one gets a deterministic id
    (md5 of the project-relative path under a fixed namespace), so
    regenerating the project never churns ids in version control.
    Returns the asset's id (the scene embeds it next to asset references)."""
    meta_path = Path(str(asset_path) + ".orkmeta")
    if meta_path.exists():
        match = re.search(r'id="([0-9a-f]+)"', meta_path.read_text())
        assert match, "unreadable sidecar: %s" % meta_path
        return match.group(1)
    relative = Path(asset_path).resolve().relative_to(
        Path(project_dir).resolve()).as_posix()
    asset_id = hashlib.md5(
        ("orkige.roller:" + relative).encode("utf-8")).hexdigest()
    meta_path.write_text('<orkmeta id="%s"/>\n' % asset_id)
    print("wrote %s (id %s)" % (meta_path, asset_id))
    return asset_id


# ---------------------------------------------------------------------------
# minimal RGBA PNG encoder (RGBA8, no interlace, filter 0 per scanline)
# ---------------------------------------------------------------------------

def encode_png_rgba(width, height, rgba_rows):
    """rgba_rows: list of rows, each a list of (r, g, b, a) 0..255 tuples."""

    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    raw = b"".join(
        b"\x00" + bytes(c for px in row for c in px) for row in rgba_rows
    )
    ihdr = struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0)  # RGBA8
    return (
        PNG_SIGNATURE
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def hash_noise(x, y, salt=0):
    """Deterministic pseudo-noise in 0..1 (no random module)."""
    n = (x * 374761393 + y * 668265263 + salt * 2147483647) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def clamp8(v):
    return max(0, min(255, int(v)))


# ---------------------------------------------------------------------------
# textures
# ---------------------------------------------------------------------------

def make_ball_texture(size=64):
    """Shaded red ball: radial gradient lit from the upper left, specular
    highlight, a darker equator stripe (so rolling reads on screen) and an
    antialiased alpha edge."""
    center = (size - 1) / 2.0
    radius = center - 1.0
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            dx, dy = x - center, y - center
            dist = math.hypot(dx, dy)
            if dist > radius + 1.0:
                row.append((0, 0, 0, 0))
                continue
            # antialiased rim: alpha fades over the last texel
            alpha = clamp8(255 * min(1.0, radius + 1.0 - dist))
            # radial shading, light from (-0.5, -0.6)
            lx, ly = dx / radius + 0.5, dy / radius + 0.6
            light = max(0.0, 1.0 - 0.55 * math.hypot(lx, ly))
            r = 120 + 135 * light
            g = 30 + 80 * light
            b = 30 + 60 * light
            # roll marker: a dark diagonal stripe through the middle
            if abs(dx + dy) < size * 0.06 and dist < radius * 0.92:
                r, g, b = r * 0.45, g * 0.45, b * 0.45
            # specular dot
            sx, sy = dx + radius * 0.38, dy + radius * 0.42
            if math.hypot(sx, sy) < radius * 0.16:
                r, g, b = 250, 235, 225
            row.append((clamp8(r), clamp8(g), clamp8(b), alpha))
        rows.append(row)
    return rows


def make_wall_texture(size=64):
    """Opaque slate bricks: 2 rows of offset bricks, mortar lines, noise."""
    rows = []
    brick_h = size // 2
    brick_w = size // 2
    for y in range(size):
        row = []
        for x in range(size):
            course = y // brick_h
            offset = (brick_w // 2) if course % 2 else 0
            in_mortar_y = (y % brick_h) < 2
            in_mortar_x = ((x + offset) % brick_w) < 2
            base = 30 + 26 * hash_noise((x + offset) // brick_w, course, 7)
            r = base + 38 + 16 * hash_noise(x, y, 8)
            g = base + 44 + 16 * hash_noise(x, y, 9)
            b = base + 56 + 16 * hash_noise(x, y, 10)
            if in_mortar_x or in_mortar_y:
                r, g, b = r * 0.5, g * 0.5, b * 0.5
            # darker outer edge so stretched walls keep definition
            if y < 2 or y >= size - 2 or x < 2 or x >= size - 2:
                r, g, b = r * 0.6, g * 0.6, b * 0.6
            row.append((clamp8(r), clamp8(g), clamp8(b), 255))
        rows.append(row)
    return rows


def make_tile_frame_texture(size=128, border=10):
    """Beveled steel frame with a fully TRANSPARENT center - the visual
    boundary of one movable tile."""
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            edge = min(x, y, size - 1 - x, size - 1 - y)
            if edge >= border:
                row.append((0, 0, 0, 0))
                continue
            # bevel: bright outer lip, darker inner lip
            t = edge / (border - 1)
            base = 150 - 70 * t
            r = base + 8 * hash_noise(x // 4, y // 4, 11)
            g = base + 12 + 8 * hash_noise(x // 4, y // 4, 12)
            b = base + 26 + 8 * hash_noise(x // 4, y // 4, 13)
            if edge == 0 or edge == border - 1:
                r, g, b = r * 0.55, g * 0.55, b * 0.55
            row.append((clamp8(r), clamp8(g), clamp8(b), 255))
        rows.append(row)
    return rows


def make_goal_texture(size=64):
    """Golden five-pointed star on transparent ground, thin dark outline."""
    center = (size - 1) / 2.0
    outer = center - 2.0
    inner = outer * 0.45

    def star_radius(angle):
        # 5 points, point up: radius alternates outer/inner over 36 degrees
        a = (angle + math.pi / 2.0) % (2.0 * math.pi / 5.0)
        a = abs(a - math.pi / 5.0) / (math.pi / 5.0)  # 0 at point, 1 at notch
        return inner + (outer - inner) * (1.0 - a)

    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            dx, dy = x - center, y - center
            dist = math.hypot(dx, dy)
            boundary = star_radius(math.atan2(dy, dx))
            if dist > boundary + 1.2:
                row.append((0, 0, 0, 0))
                continue
            alpha = clamp8(255 * min(1.0, boundary + 1.2 - dist))
            t = dist / max(boundary, 1e-5)
            r, g, b = 250 - 40 * t, 205 - 60 * t, 40 + 10 * t
            if dist > boundary - 1.5:  # outline
                r, g, b = 120, 80, 10
            row.append((clamp8(r), clamp8(g), clamp8(b), alpha))
        rows.append(row)
    return rows


def make_cursor_texture(size=64, border=7):
    """Translucent yellow highlight frame + faint fill - the move-mode
    cursor that marks the EMPTY grid slot tiles slide into."""
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            edge = min(x, y, size - 1 - x, size - 1 - y)
            if edge < border:
                pulse = 200 - 60 * (edge / border)
                row.append((255, 235, 90, clamp8(pulse)))
            else:
                row.append((255, 235, 90, 36))
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# scene/prefab writers (the XMLArchive forms SceneSerializer::saveScene and
# PrefabSerializer::savePrefab produce)
# ---------------------------------------------------------------------------

def fmt(value):
    """Match XMLArchive float formatting closely enough (plain repr)."""
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, float):
        text = ("%g" % value)
        return text
    return str(value)


class ComponentWriter:
    """The component builders shared by the scene and the prefab writer -
    each returns (componentTypeName, [serialized field lines])."""

    @staticmethod
    def _base_fields():
        # GameObjectComponent base save: version uint + empty string
        return ['<unsigned_int value="0"/>', '<String value=""/>']

    def _component(self, type_name, fields):
        return (type_name, self._base_fields() + fields)

    def transform(self, x, y, z=0.0, sx=1.0, sy=1.0, sz=1.0):
        return self._component("TransformComponent", [
            '<float value="%s"/>' % fmt(float(v))
            for v in (x, y, z, 1.0, 0.0, 0.0, 0.0, sx, sy, sz)
        ])

    def sprite(self, texture, width, height, z_order=0, tint=(1, 1, 1, 1),
               visible=True):
        return self._component("SpriteComponent", [
            '<String value="%s"/>' % texture,
            '<float value="%s"/>' % fmt(float(width)),
            '<float value="%s"/>' % fmt(float(height)),
            '<float value="0"/>', '<float value="0"/>',
            '<float value="1"/>', '<float value="1"/>',
            '<float value="%s"/>' % fmt(float(tint[0])),
            '<float value="%s"/>' % fmt(float(tint[1])),
            '<float value="%s"/>' % fmt(float(tint[2])),
            '<float value="%s"/>' % fmt(float(tint[3])),
            '<bool value="0"/>', '<bool value="0"/>',        # flips
            '<int value="%d"/>' % z_order,
            '<bool value="%s"/>' % fmt(visible),
        ])

    def rigid_box(self, hx, hy, hz, body_type=1, friction=0.5,
                  restitution=0.0, layer="Default"):
        """body_type: 0 static, 1 KINEMATIC (movable tiles), 2 dynamic.
        layer: collision-layer NAME (physics.olayers) - the LAST field, matches
        RigidBodyComponent::save."""
        return self._component("RigidBodyComponent", [
            '<int value="%d"/>' % body_type,
            '<int value="0"/>',                              # ST_BOX
            '<float value="%s"/>' % fmt(float(hx)),
            '<float value="%s"/>' % fmt(float(hy)),
            '<float value="%s"/>' % fmt(float(hz)),
            '<float value="0.5"/>', '<float value="0.5"/>',  # radius/halfHeight
            '<float value="1"/>',                            # mass
            '<float value="%s"/>' % fmt(float(friction)),
            '<float value="%s"/>' % fmt(float(restitution)),
            '<bool value="0"/>',                             # planar (dynamic only)
            '<String value="%s"/>' % layer,                  # collision layer
        ])

    def rigid_sphere(self, radius, mass=1.0, friction=0.5, restitution=0.2,
                     planar=True, layer="Default"):
        return self._component("RigidBodyComponent", [
            '<int value="2"/>',                              # BT_DYNAMIC
            '<int value="1"/>',                              # ST_SPHERE
            '<float value="0.5"/>', '<float value="0.5"/>', '<float value="0.5"/>',
            '<float value="%s"/>' % fmt(float(radius)),
            '<float value="0.5"/>',
            '<float value="%s"/>' % fmt(float(mass)),
            '<float value="%s"/>' % fmt(float(friction)),
            '<float value="%s"/>' % fmt(float(restitution)),
            '<bool value="%s"/>' % fmt(planar),
            '<String value="%s"/>' % layer,                  # collision layer
        ])

    def script(self, path, enabled=True):
        return self._component("ScriptComponent", [
            '<String value="%s"/>' % path,
            '<bool value="%s"/>' % fmt(enabled),
        ])

    def particles(self, texture, burst_count=24, max_particles=48,
                  z_order=6, blend_mode=1,
                  start_color=(1.0, 0.9, 0.4, 1.0),
                  end_color=(1.0, 0.5, 0.1, 0.0)):
        """A burst-only 2D particle emitter (WP #82). The field ORDER mirrors
        ParticleComponent::save exactly - keep the two in sync. blend_mode:
        0 alpha, 1 additive (the burst default)."""
        return self._component("ParticleComponent", [
            '<String value="%s"/>' % texture,     # texture name (plain, no assetId)
            '<bool value="0"/>',                  # emitOnStart (burst-only)
            '<float value="0"/>',                 # emissionRate (0 = burst-only)
            '<int value="%d"/>' % burst_count,    # burstCount
            '<float value="0"/>',                 # duration
            '<bool value="1"/>',                  # looping
            '<float value="0.5"/>', '<float value="0.9"/>',   # lifetime min/max
            '<float value="0"/>', '<float value="0"/>',       # spawnOffset x/y
            '<float value="90"/>', '<float value="180"/>',    # directionAngle / spreadAngle (full radial)
            '<float value="3"/>', '<float value="6"/>',       # speed min/max
            '<float value="0"/>', '<float value="-4"/>',      # gravity x/y
            '<float value="1"/>',                             # damping
            '<float value="-180"/>', '<float value="180"/>',  # spin min/max (deg/s)
            '<float value="0.35"/>', '<float value="0"/>',    # startSize / endSize
            '<float value="%s"/>' % fmt(float(start_color[0])),
            '<float value="%s"/>' % fmt(float(start_color[1])),
            '<float value="%s"/>' % fmt(float(start_color[2])),
            '<float value="%s"/>' % fmt(float(start_color[3])),
            '<float value="%s"/>' % fmt(float(end_color[0])),
            '<float value="%s"/>' % fmt(float(end_color[1])),
            '<float value="%s"/>' % fmt(float(end_color[2])),
            '<float value="%s"/>' % fmt(float(end_color[3])),
            '<String value="linear"/>', '<String value="quadOut"/>',  # size / colour ease
            '<int value="1"/>', '<int value="1"/>',           # atlas columns/rows
            '<int value="0"/>', '<int value="0"/>',           # atlas frame min/max
            '<int value="%d"/>' % max_particles,              # maxParticles
            '<int value="%d"/>' % z_order,                    # zOrder
            '<int value="%d"/>' % blend_mode,                 # blendMode
        ])

    @staticmethod
    def _object_lines(name, parent, active, prefab_lines, components):
        """One scene-v3/prefab-v1 per-object block (they share the shape);
        prefab_lines carries the prefabRef element plus, on instance roots,
        the suppressed-children count and ids."""
        lines = []
        lines.append('    <String value="%s"/>' % name)
        lines.append('    <String value="%s"/>' % parent)
        lines.append('    <bool value="%s"/>' % fmt(active))
        lines.extend(prefab_lines)
        lines.append('    <unsigned_int value="%d"/>' % len(components))
        for type_name, fields in components:
            lines.append('    <String value="%s"/>' % type_name)
            lines.append('    <%s create="0">' % type_name)
            lines.extend('        ' + field for field in fields)
            lines.append('    </%s>' % type_name)
        return lines


class SceneWriter(ComponentWriter):
    """Writes scene format VERSION 3: every object carries its parent id
    ("" = root), its activeSelf flag and its prefabRef ("" = plain object;
    instance roots additionally list their suppressed prefab children) next
    to the components (core_game/SceneSerializer.cpp)."""

    def __init__(self):
        # list of (name, parent, active, prefabRef, prefabAssetId,
        #          suppressed locals, [ (componentType, [lines]) ])
        self.objects = []

    def add(self, name, *components, parent="", active=True, prefab_ref="",
            prefab_asset_id="", suppressed=()):
        # keep the component order SceneSerializer produces (sorted by type
        # name) so a re-save in the editor diffs minimally. Objects with a
        # parent carry LOCAL transforms (relative to the parent's center).
        self.objects.append(
            (name, parent, active, prefab_ref, prefab_asset_id,
             tuple(suppressed), sorted(components, key=lambda c: c[0])))

    def write(self, path):
        lines = [
            "<?1.0, UTF-8, yes?>",
            '<XMLArchive Version="0">',
            '    <String value="orkige.oscene"/>',
            '    <int value="3"/>',
            '    <unsigned_int value="%d"/>' % len(self.objects),
        ]
        for (name, parent, active, prefab_ref, prefab_asset_id, suppressed,
             components) in self.objects:
            if prefab_ref and prefab_asset_id:
                prefab_lines = ['    <String value="%s" assetId="%s"/>'
                                % (prefab_ref, prefab_asset_id)]
            else:
                prefab_lines = ['    <String value="%s"/>' % prefab_ref]
            if prefab_ref:
                # instance roots list their structural overrides
                prefab_lines.append(
                    '    <unsigned_int value="%d"/>' % len(suppressed))
                prefab_lines.extend(
                    '    <String value="%s"/>' % local for local in suppressed)
            lines.extend(self._object_lines(
                name, parent, active, prefab_lines, components))
        lines.append('</XMLArchive>')
        path.write_text("\n".join(lines) + "\n")


class PrefabWriter(ComponentWriter):
    """Writes prefab format VERSION 1 (core_game/PrefabSerializer): magic,
    version, the root's prefab-local id, then every subtree object under
    its prefab-LOCAL id with LOCAL parent links (the per-object block is
    the scene v3 shape with prefabRef always "" - nested prefabs are
    refused by the engine)."""

    def __init__(self, root_local_id):
        self.root_local_id = root_local_id
        # list of (localId, parentLocalId, [ (componentType, [lines]) ])
        self.objects = []

    def add(self, local_id, *components, parent=""):
        self.objects.append(
            (local_id, parent, sorted(components, key=lambda c: c[0])))

    def write(self, path):
        lines = [
            "<?1.0, UTF-8, yes?>",
            '<XMLArchive Version="0">',
            '    <String value="orkige.oprefab"/>',
            '    <int value="1"/>',
            '    <String value="%s"/>' % self.root_local_id,
            '    <unsigned_int value="%d"/>' % len(self.objects),
        ]
        for local_id, parent, components in self.objects:
            lines.extend(self._object_lines(
                local_id, parent, True, ['    <String value=""/>'],
                components))
        lines.append('</XMLArchive>')
        path.write_text("\n".join(lines) + "\n")


TILE = 6.0                 # tile edge length in world units
HALF = TILE / 2.0
WALL = 0.5                 # wall thickness
WALL_HALF = WALL / 2.0
SLOTS = {0: (-HALF, -HALF), 1: (HALF, -HALF), 2: (-HALF, HALF), 3: (HALF, HALF)}

Z_FRAME, Z_WALL, Z_GOAL, Z_BALL, Z_CURSOR = 1, 2, 3, 5, 8


def wall_components(writer, cx, cy, horizontal, length):
    """One wall segment's components: kinematic box + stretched wall sprite
    (cx/cy are LOCAL to the tile center)."""
    if horizontal:
        w, h = length, WALL
    else:
        w, h = WALL, length
    return (
        writer.transform(cx, cy),
        writer.sprite("wall.png", w, h, Z_WALL),
        writer.rigid_box(w / 2.0, h / 2.0, WALL_HALF, body_type=1,
                         friction=0.4, layer="obstacle"),
    )


#: prefab-local wall id per tile edge (fixed order - the suppressed lists
#: stay deterministic across regenerations)
EDGE_LOCALS = (("top", "WallTop"), ("bottom", "WallBottom"),
               ("left", "WallLeft"), ("right", "WallRight"))


def build_tile_prefab():
    """The reusable tile subtree: root "Tile" + frame sprite + all FOUR edge
    walls - instances open edges by SUPPRESSING the wall children."""
    prefab = PrefabWriter("Tile")
    prefab.add("Tile", prefab.transform(0.0, 0.0))
    prefab.add("Frame", prefab.transform(0.0, 0.0),
               prefab.sprite("tile_frame.png", TILE, TILE, Z_FRAME),
               parent="Tile")
    edge_offset = HALF - WALL_HALF
    offsets = {"top": (0.0, edge_offset, True),
               "bottom": (0.0, -edge_offset, True),
               "left": (-edge_offset, 0.0, False),
               "right": (edge_offset, 0.0, False)}
    for edge, local_id in EDGE_LOCALS:
        cx, cy, horizontal = offsets[edge]
        prefab.add(local_id,
                   *wall_components(prefab, cx, cy, horizontal, TILE),
                   parent="Tile")
    return prefab


def add_tile(scene, key, slot, open_edges, tile_asset_id, ledge=False):
    """A tile at its INITIAL slot: ONE prefab INSTANCE root ("Tile<key>") at
    the slot center referencing assets/tile.oprefab - the frame and walls
    come from the prefab ("Tile<key>/Frame", "Tile<key>/WallTop", ...), the
    open edges are suppressedChildren. Sliding the tile is ONE teleport of
    the root (the GameObject tree replaced the historical Lua-side group
    tables); scene-side EXTRAS (the ledge) stay plain serialized children."""
    cx, cy = SLOTS[slot]
    parent = "Tile%s" % key
    suppressed = [local_id for edge, local_id in EDGE_LOCALS
                  if edge in open_edges]
    scene.add(parent, scene.transform(cx, cy),
              prefab_ref="assets/tile.oprefab",
              prefab_asset_id=tile_asset_id,
              suppressed=suppressed)
    if ledge:
        # interior platform: rolling obstacle inside the tile (an EXTRA
        # child of the instance - it does not come from the prefab)
        scene.add("Tile%s_Ledge" % key,
                  *wall_components(scene, -1.0, -1.0, True, 2.5),
                  parent=parent)


def build_scene(tile_asset_id):
    scene = SceneWriter()
    # game flow / UI / tile sliding
    scene.add("Game", scene.script("scripts/game.lua"))
    # the ball: dynamic planar sphere, spawn on tile A's floor
    ball_spawn_y = SLOTS[0][1] - HALF + WALL + 0.4  # floor top + radius
    scene.add(
        "Ball",
        scene.transform(SLOTS[0][0], ball_spawn_y),
        scene.sprite("ball.png", 0.8, 0.8, Z_BALL),
        scene.rigid_sphere(0.4, mass=1.0, friction=0.4, restitution=0.2,
                           planar=True, layer="ball"),
        scene.script("scripts/ball.lua"),
        # star-collect burst (WP #82): ball.lua fires self.particles:burst()
        # on the win - a golden additive shower of the goal-star texture
        scene.particles("goal.png"),
    )
    # tiles: A = spawn (right edge open), B = goal (left open, starts top
    # right), C = filler (closed), slot 1 stays empty
    add_tile(scene, "A", 0, open_edges=("right",),
             tile_asset_id=tile_asset_id, ledge=True)
    add_tile(scene, "B", 3, open_edges=("left",),
             tile_asset_id=tile_asset_id)
    add_tile(scene, "C", 2, open_edges=(), tile_asset_id=tile_asset_id)
    # the goal star inside tile B - a CHILD of the TileB group (local
    # offset on B's floor near the right wall), so it slides along
    scene.add("Goal", scene.transform(1.6, -HALF + WALL + 0.5),
              scene.sprite("goal.png", 1.0, 1.0, Z_GOAL),
              parent="TileB")
    # move-mode cursor: highlights the EMPTY slot; game.lua repositions and
    # shows/hides it (start: hidden over slot 1)
    scene.add("Cursor", scene.transform(SLOTS[1][0], SLOTS[1][1]),
              scene.sprite("cursor.png", TILE, TILE, Z_CURSOR, visible=False))
    return scene


def write_layers(path):
    """physics.olayers (PhysicsWorld::LayerConfig, XMLArchive) - proves the
    data-driven collision matrix gates real roller collisions: the ball rolls
    on the obstacle walls/tiles (ball x obstacle = collide) while ball/ball and
    obstacle/obstacle are OFF. Default (0) collides with everything so any
    unlabeled body behaves as before. Row-major NxN bools, symmetric."""
    names = ["Default", "ball", "obstacle"]
    # symmetric collision matrix (index order matches names)
    matrix = [
        [True,  True,  True],   # Default: collides with all
        [True,  False, True],   # ball:    not with itself, yes with obstacles
        [True,  True,  False],  # obstacle: not with itself
    ]
    lines = [
        "<?1.0, UTF-8, yes?>",
        '<XMLArchive Version="0">',
        '    <String value="orkige.olayers"/>',
        '    <int value="1"/>',
        '    <unsigned_int value="%d"/>' % len(names),
    ]
    lines.extend('    <String value="%s"/>' % name for name in names)
    for row in matrix:
        lines.extend('    <bool value="%d"/>' % (1 if cell else 0)
                     for cell in row)
    lines.append('</XMLArchive>')
    path.write_text("\n".join(lines) + "\n")


def main():
    default = Path(__file__).resolve().parent.parent / "projects/roller"
    project_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    assets = project_dir / "assets"
    scenes = project_dir / "scenes"
    assets.mkdir(parents=True, exist_ok=True)
    scenes.mkdir(parents=True, exist_ok=True)

    textures = {
        "ball.png": make_ball_texture(),
        "wall.png": make_wall_texture(),
        "tile_frame.png": make_tile_frame_texture(),
        "goal.png": make_goal_texture(),
        "cursor.png": make_cursor_texture(),
    }
    for name, rows in textures.items():
        data = encode_png_rgba(len(rows[0]), len(rows), rows)
        (assets / name).write_bytes(data)
        print("wrote %s (%d bytes)" % (assets / name, len(data)))
        write_meta(project_dir, assets / name)

    prefab = build_tile_prefab()
    prefab.write(assets / "tile.oprefab")
    tile_asset_id = write_meta(project_dir, assets / "tile.oprefab")
    print("wrote %s (%d objects)" % (assets / "tile.oprefab",
                                     len(prefab.objects)))

    scene = build_scene(tile_asset_id)
    scene.write(scenes / "main.oscene")
    print("wrote %s (%d objects)" % (scenes / "main.oscene",
                                     len(scene.objects)))

    # collision layers (project-config, referenced from the manifest by the
    # Settings key "physics.layers")
    write_layers(project_dir / "physics.olayers")
    print("wrote %s" % (project_dir / "physics.olayers"))


if __name__ == "__main__":
    main()
