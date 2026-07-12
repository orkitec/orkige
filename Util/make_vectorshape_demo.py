#!/usr/bin/env python3
"""Generate projects/vectorshapes/ - a reproducible flat-colour vector-shape
demo project.

Writes a couple of hand-authorable .oshape blobs (an organic silhouette and a
concave star - proving concave tessellation) plus a scene that places several
instances at different zOrders and tints so the painter's-order sort among
shapes is visible. Stdlib-only, deterministic, in the exact XMLArchive forms
SceneSerializer::saveScene emits (mirrors Util/make_roller_assets.py).

The .oshape files are the strong agent story: an agent can write one directly
over write_project_file. This generator is the reproducible content example a
human (or the player_vectorshape selfcheck) can load.
"""

import json
import math
import sys
from pathlib import Path


def fmt(value):
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, float):
        return "%g" % value
    return str(value)


# PropertyKind ordinals (core_base/PropertyValue.h)
K_INT, K_FLOAT, K_BOOL, K_STRING, K_ENUM, K_VEC3, K_QUAT, K_COLOR, K_ASSETREF = \
    range(9)


class SceneWriter:
    """The scene-v7 XMLArchive writer (the subset this demo needs)."""

    def __init__(self):
        self.objects = []

    @staticmethod
    def _base():
        return ['<unsigned_int value="0"/>', '<String value=""/>']

    @classmethod
    def _named(cls, records):
        lines = cls._base()
        lines.append('<unsigned_int value="%d"/>' % len(records))
        for (name, kind, value, ref) in records:
            lines.append('<String value="%s"/>' % name)
            lines.append('<int value="%d"/>' % kind)
            lines.append('<String value="%s"/>' % value)
            lines.append('<String value="%s"/>' % ref)
        return lines

    def transform(self, x, y, z=0.0):
        return ("TransformComponent", self._named([
            ("position", K_VEC3, "%s %s %s" % (fmt(float(x)), fmt(float(y)),
                                               fmt(float(z))), ""),
            ("orientation", K_QUAT, "1 0 0 0", ""),
            ("scale", K_VEC3, "1 1 1", ""),
        ]))

    def vector_shape(self, shape, z_order=0, tint=(1, 1, 1, 1), scale=1.0,
                     edge_softness=0.0, visible=True, soft_body=False,
                     wobble_stiffness=140.0, wobble_damping=7.0,
                     wobble_amount=1.0, squash_amount=0.5, morph_clip=-1,
                     morph_speed=1.0, morph_loop=False):
        # the soft-body tunables come BEFORE the shape ref so the deformer the
        # shape builds picks them up (the shape ref is applied last)
        return ("VectorShapeComponent", self._named([
            ("tint", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in tint),
             ""),
            ("scale", K_FLOAT, fmt(float(scale)), ""),
            ("edgeSoftness", K_FLOAT, fmt(float(edge_softness)), ""),
            ("zOrder", K_INT, "%d" % z_order, ""),
            ("visible", K_BOOL, fmt(visible), ""),
            ("softBody", K_BOOL, fmt(soft_body), ""),
            ("wobbleStiffness", K_FLOAT, fmt(float(wobble_stiffness)), ""),
            ("wobbleDamping", K_FLOAT, fmt(float(wobble_damping)), ""),
            ("wobbleAmount", K_FLOAT, fmt(float(wobble_amount)), ""),
            ("squashAmount", K_FLOAT, fmt(float(squash_amount)), ""),
            ("morphClip", K_INT, "%d" % morph_clip, ""),
            ("morphSpeed", K_FLOAT, fmt(float(morph_speed)), ""),
            ("morphLoop", K_BOOL, fmt(morph_loop), ""),
            ("shape", K_ASSETREF, shape, ""),
        ]))

    def vector_animation(self, animation, clip="", z_order=0, tint=(1, 1, 1, 1),
                         scale=1.0, edge_softness=0.0, visible=True, speed=1.0,
                         playing=True, transition_time=0.0):
        # the playback settings + rig state come BEFORE the animation ref so the
        # rig the ref builds picks up the clip/speed/playing set above (the ref
        # is applied last, mirroring VectorShapeComponent)
        return ("VectorAnimationComponent", self._named([
            ("clip", K_STRING, clip, ""),
            ("speed", K_FLOAT, fmt(float(speed)), ""),
            ("playing", K_BOOL, fmt(playing), ""),
            ("transitionTime", K_FLOAT, fmt(float(transition_time)), ""),
            ("tint", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in tint),
             ""),
            ("scale", K_FLOAT, fmt(float(scale)), ""),
            ("edgeSoftness", K_FLOAT, fmt(float(edge_softness)), ""),
            ("zOrder", K_INT, "%d" % z_order, ""),
            ("visible", K_BOOL, fmt(visible), ""),
            ("animation", K_ASSETREF, animation, ""),
        ]))

    def rigid_sphere(self, radius, mass=1.0, friction=0.5, restitution=0.0,
                     planar=True, layer="Default"):
        return ("RigidBodyComponent", self._named([
            ("bodyType", K_ENUM, "2", ""),          # BT_DYNAMIC
            ("shapeType", K_ENUM, "1", ""),         # ST_SPHERE
            ("halfExtents", K_VEC3, "0.5 0.5 0.5", ""),
            ("radius", K_FLOAT, fmt(float(radius)), ""),
            ("halfHeight", K_FLOAT, "0.5", ""),
            ("mass", K_FLOAT, fmt(float(mass)), ""),
            ("friction", K_FLOAT, fmt(float(friction)), ""),
            ("restitution", K_FLOAT, fmt(float(restitution)), ""),
            ("planar", K_BOOL, fmt(planar), ""),
            ("layer", K_STRING, layer, ""),
            ("isSensor", K_BOOL, "0", ""),
        ]))

    def rigid_box(self, hx, hy, hz, body_type=0, friction=0.6, layer="Default"):
        return ("RigidBodyComponent", self._named([
            ("bodyType", K_ENUM, "%d" % body_type, ""),
            ("shapeType", K_ENUM, "0", ""),         # ST_BOX
            ("halfExtents", K_VEC3, "%s %s %s" % (fmt(float(hx)),
                                                  fmt(float(hy)),
                                                  fmt(float(hz))), ""),
            ("radius", K_FLOAT, "0.5", ""),
            ("halfHeight", K_FLOAT, "0.5", ""),
            ("mass", K_FLOAT, "1", ""),
            ("friction", K_FLOAT, fmt(float(friction)), ""),
            ("restitution", K_FLOAT, "0", ""),
            ("planar", K_BOOL, "0", ""),
            ("layer", K_STRING, layer, ""),
            ("isSensor", K_BOOL, "0", ""),
        ]))

    def script(self, path, enabled=True):
        return ("ScriptComponent", self._named([
            ("script", K_ASSETREF, path, ""),
            ("enabled", K_BOOL, fmt(enabled), ""),
        ]))

    def add(self, name, *components):
        self.objects.append((name, sorted(components, key=lambda c: c[0])))

    def write(self, path):
        lines = ['<?xml version="1.0" encoding="UTF-8"?>',
                 '<XMLArchive Version="0">',
                 '    <String value="orkige.oscene"/>',
                 '    <int value="7"/>',
                 '    <unsigned_int value="%d"/>' % len(self.objects)]
        for name, components in self.objects:
            lines.append('    <String value="%s"/>' % name)
            lines.append('    <String value=""/>')          # parent (root)
            lines.append('    <bool value="1"/>')            # active
            lines.append('    <unsigned_int value="0"/>')    # tags
            lines.append('    <String value=""/>')           # prefabRef
            lines.append('    <unsigned_int value="%d"/>' % len(components))
            for type_name, fields in components:
                lines.append('    <String value="%s"/>' % type_name)
                lines.append('    <%s create="0">' % type_name)
                lines.extend('        ' + f for f in fields)
                lines.append('    </%s>' % type_name)
        lines.append('</XMLArchive>')
        path.write_text("\n".join(lines) + "\n")


def blob_oshape(fill, seed=1):
    """An organic ~10-gon silhouette (deterministic per seed)."""
    n = 10
    lines = ["# orkige vector shape v1 - generated organic blob",
             "version 1", "fill %.3f %.3f %.3f %.3f" % fill, "contour %d" % n]
    for i in range(n):
        angle = 2.0 * math.pi * i / n
        # a smooth, deterministic radius wobble (no RNG - reproducible)
        radius = 1.0 + 0.18 * math.sin(angle * 3.0 + seed) \
            + 0.08 * math.cos(angle * 5.0 - seed)
        lines.append("v %.4f %.4f" % (radius * math.cos(angle),
                                      radius * math.sin(angle)))
    return "\n".join(lines) + "\n"


def star_oshape(fill, points=5):
    """A concave star (alternating outer/inner radius) - a concave tessellation
    proof."""
    n = points * 2
    lines = ["# orkige vector shape v1 - generated concave star",
             "version 1", "fill %.3f %.3f %.3f %.3f" % fill, "contour %d" % n]
    for i in range(n):
        angle = math.pi / 2.0 + 2.0 * math.pi * i / n
        radius = 1.0 if i % 2 == 0 else 0.45
        lines.append("v %.4f %.4f" % (radius * math.cos(angle),
                                      radius * math.sin(angle)))
    return "\n".join(lines) + "\n"


def blob_morph_oshape(fill, n=16):
    """An organic blob PLUS one same-structure morph target (a wide squash pose)
    - the soft-body morph example. Both contours have the SAME vertex count and
    order, so the runtime blends control point for control point."""
    def contour(radius_scale_x, radius_scale_y):
        out = ["fill %.3f %.3f %.3f %.3f" % fill, "contour %d" % n]
        for i in range(n):
            angle = 2.0 * math.pi * i / n
            r = 1.0 + 0.1 * math.sin(angle * 3.0)
            out.append("v %.4f %.4f" % (radius_scale_x * r * math.cos(angle),
                                        radius_scale_y * r * math.sin(angle)))
        return out
    lines = ["# orkige vector shape v1 - blob with a squash morph target",
             "version 1"]
    lines += contour(1.0, 1.0)          # base (round)
    lines.append("morph squash")
    lines += contour(1.35, 0.6)         # target (wide + flat)
    return "\n".join(lines) + "\n"


def hero_lottie():
    """A tiny hand-authored Lottie character - a two-blob hero (a body with a
    parented head) whose one timeline carries an `idle` bob (0..30, loop) and a
    `hop` jump (30..60, once) via markers. The cook flattens the ellipses,
    flips/scales into rig space and carves the markers into clips. Cooking it
    (below) keeps the source as the living document and the .oanim as the
    committed runtime form - the roundtrip fixture precedent."""
    def ease():
        return {"i": {"x": 0.42, "y": 0}, "o": {"x": 0.58, "y": 1}}
    doc = {
        "v": "5.7.0", "fr": 30, "ip": 0, "op": 60, "w": 200, "h": 200,
        "markers": [{"tm": 0, "cm": "idle", "dr": 30},
                    {"tm": 30, "cm": "hop #once", "dr": 30}],
        "layers": [
            {"ty": 3, "nm": "root", "ind": 1,
             "ks": {"p": {"a": 0, "k": [100, 100]},
                    "a": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [100, 100]},
                    "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}}},
            {"ty": 4, "nm": "body", "ind": 2, "parent": 1,
             "ks": {"p": {"a": 1, "k": [
                        dict(t=0, s=[0, 0], **ease()),
                        dict(t=15, s=[0, -10], **ease()),
                        dict(t=30, s=[0, 0], **ease()),
                        dict(t=42, s=[0, -60], **ease()),
                        {"t": 60, "s": [0, 0]}]},
                    "a": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [100, 100]},
                    "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}},
             "shapes": [{"ty": "gr", "nm": "belly", "it": [
                 {"ty": "el", "p": {"a": 0, "k": [0, 0]},
                  "s": {"a": 0, "k": [86, 74]}},
                 {"ty": "fl", "c": {"a": 0, "k": [0.95, 0.55, 0.35, 1]},
                  "o": {"a": 0, "k": 100}}]}]},
            {"ty": 4, "nm": "head", "ind": 3, "parent": 2,
             "ks": {"p": {"a": 0, "k": [0, -48]},
                    "a": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [100, 100]},
                    "r": {"a": 1, "k": [
                        {"t": 0, "s": [0]}, {"t": 30, "s": [0]},
                        dict(t=42, s=[-16], **ease()), {"t": 60, "s": [0]}]},
                    "o": {"a": 0, "k": 100}},
             "shapes": [{"ty": "gr", "nm": "face", "it": [
                 {"ty": "el", "p": {"a": 0, "k": [0, 0]},
                  "s": {"a": 0, "k": [50, 50]}},
                 {"ty": "fl", "c": {"a": 0, "k": [0.98, 0.78, 0.42, 1]},
                  "o": {"a": 0, "k": 100}}]}]},
        ]}
    return json.dumps(doc, indent=2) + "\n"


def main():
    default = Path(__file__).resolve().parent.parent / "projects/vectorshapes"
    project_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    assets = project_dir / "assets"
    scenes = project_dir / "scenes"
    assets.mkdir(parents=True, exist_ok=True)
    scenes.mkdir(parents=True, exist_ok=True)

    scripts = project_dir / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)

    (assets / "blob.oshape").write_text(blob_oshape((0.90, 0.45, 0.40, 1.0)))
    (assets / "star.oshape").write_text(star_oshape((1.0, 0.82, 0.30, 1.0)))
    (assets / "softblob.oshape").write_text(
        blob_oshape((0.95, 0.55, 0.45, 1.0), seed=2))
    (assets / "morphblob.oshape").write_text(
        blob_morph_oshape((0.55, 0.80, 0.95, 1.0)))

    scene = SceneWriter()
    # three overlapping instances at rising zOrders + distinct tints: the
    # painter's order and per-instance tint recolour are both visible
    scene.add("BackBlob",
              scene.transform(-0.8, 0.0),
              scene.vector_shape("blob.oshape", z_order=0,
                                 tint=(0.9, 0.5, 0.4, 1.0)))
    scene.add("MidStar",
              scene.transform(0.4, 0.3),
              scene.vector_shape("star.oshape", z_order=2,
                                 tint=(1.0, 0.85, 0.3, 1.0)))
    scene.add("FrontBlob",
              scene.transform(1.0, -0.4),
              scene.vector_shape("blob.oshape", z_order=4,
                                 tint=(0.4, 0.7, 0.9, 1.0)))
    scene.write(scenes / "main.oscene")

    # the SOFT-BODY scene: a deformable blob with a dynamic RigidBody falls onto
    # a static floor - it squashes on landing and wobbles (the physics-driven
    # deform) - plus a MorphBlob whose Lua script blends a squash morph (the
    # scripted deform drive). Verified by the softbody selfcheck.
    soft = SceneWriter()
    soft.add("Floor",
             soft.transform(0.0, -3.0),
             soft.rigid_box(4.0, 0.5, 1.0, body_type=0))
    soft.add("FallBlob",
             soft.transform(0.0, 2.5),
             soft.rigid_sphere(1.0, planar=True),
             soft.vector_shape("softblob.oshape", z_order=2,
                               tint=(0.95, 0.55, 0.45, 1.0), soft_body=True,
                               squash_amount=0.6, wobble_amount=1.2))
    soft.add("MorphBlob",
             soft.transform(-3.0, 0.0),
             soft.vector_shape("morphblob.oshape", z_order=1,
                               tint=(0.55, 0.80, 0.95, 1.0), soft_body=True,
                               morph_clip=0, morph_speed=1.5, morph_loop=True),
             soft.script("scripts/morph.lua"))
    soft.write(scenes / "softbody.oscene")

    # the morph driver: on init it plays the shape's first morph target on a
    # loop (proving the Lua self.shape drive of a soft, deformable shape)
    (scripts / "morph.lua").write_text(
        "-- drive a looping squash morph on the sibling soft-body shape\n"
        "function init(self)\n"
        "    if self.shape then\n"
        "        self.shape:playMorph(0, 1.5, true)\n"
        "    end\n"
        "end\n"
        "function update(self, dt)\n"
        "end\n")

    # the VECTOR-ANIMATION scene: a hand-authored Lottie hero (assets/hero.json)
    # cooked to the native assets/hero.oanim, driven by a script that plays the
    # `idle` clip and crossfades to the one-shot `hop`. Verified by the
    # player_vectoranim selfcheck.
    hero_json = hero_lottie()
    (assets / "hero.json").write_text(hero_json)
    # cook the source into the committed runtime .oanim (import the sibling cook
    # so the whole project regenerates from one command - the .json stays the
    # living document, the .oanim is the runtime form)
    import cook_vector_anim
    kind, oanim_text = cook_vector_anim.cook(hero_json)
    assert kind == "oanim", "hero.json did not cook to an animation"
    (assets / "hero.oanim").write_text(oanim_text)

    anim = SceneWriter()
    anim.add("Hero",
             anim.transform(0.0, 0.0),
             anim.vector_animation("hero.oanim", clip="idle", z_order=1,
                                   tint=(1.0, 1.0, 1.0, 1.0),
                                   transition_time=0.3),
             anim.script("scripts/hero_anim.lua"))
    anim.write(scenes / "vectoranim.oscene")

    # the animation driver: play idle on boot, then crossfade to the one-shot
    # `hop` after a beat; subscribe to the ended event so the selfcheck sees the
    # once-clip completion arrive in a script (the Lua drive + event path)
    (scripts / "hero_anim.lua").write_text(
        "-- drive the sibling vector-animation rig: idle, then a crossfade to\n"
        "-- the one-shot hop; record clip + the ended event into shared.heroanim\n"
        "function init(self)\n"
        "    self.elapsed = 0.0\n"
        "    self.hopped = false\n"
        "    shared.heroanim = shared.heroanim or {}\n"
        "    shared.heroanim.ended = 0\n"
        "    shared.heroanim.lastEnded = \"\"\n"
        "    events.subscribe(\"animation.ended\", function(e)\n"
        "        shared.heroanim.ended = (shared.heroanim.ended or 0) + 1\n"
        "        shared.heroanim.lastEnded = e.clip\n"
        "    end)\n"
        "    if self.anim then self.anim:play(\"idle\") end\n"
        "end\n"
        "function update(self, dt)\n"
        "    self.elapsed = self.elapsed + dt\n"
        "    -- after a beat of idle, crossfade to the one-shot hop once\n"
        "    if self.anim and not self.hopped and self.elapsed > 0.6 then\n"
        "        self.hopped = true\n"
        "        self.anim:crossFade(\"hop\", 0.3)\n"
        "    end\n"
        "    if self.anim then\n"
        "        shared.heroanim.clip = self.anim:currentClip()\n"
        "    end\n"
        "end\n")

    manifest = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                '<OrkigeProject version="1">\n'
                '    <Name>VectorShapes</Name>\n'
                '    <MainScene>scenes/main.oscene</MainScene>\n'
                '</OrkigeProject>\n')
    (project_dir / "project.orkproj").write_text(manifest)

    print("wrote %s (4 shapes + hero animation, main + softbody + vectoranim "
          "scenes)" % project_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
