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
                     edge_softness=0.0, visible=True):
        return ("VectorShapeComponent", self._named([
            ("tint", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in tint),
             ""),
            ("scale", K_FLOAT, fmt(float(scale)), ""),
            ("edgeSoftness", K_FLOAT, fmt(float(edge_softness)), ""),
            ("zOrder", K_INT, "%d" % z_order, ""),
            ("visible", K_BOOL, fmt(visible), ""),
            ("shape", K_ASSETREF, shape, ""),
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


def main():
    default = Path(__file__).resolve().parent.parent / "projects/vectorshapes"
    project_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    assets = project_dir / "assets"
    scenes = project_dir / "scenes"
    assets.mkdir(parents=True, exist_ok=True)
    scenes.mkdir(parents=True, exist_ok=True)

    (assets / "blob.oshape").write_text(blob_oshape((0.90, 0.45, 0.40, 1.0)))
    (assets / "star.oshape").write_text(star_oshape((1.0, 0.82, 0.30, 1.0)))

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

    manifest = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                '<OrkigeProject version="1">\n'
                '    <Name>VectorShapes</Name>\n'
                '    <MainScene>scenes/main.oscene</MainScene>\n'
                '</OrkigeProject>\n')
    (project_dir / "project.orkproj").write_text(manifest)

    print("wrote %s (2 shapes, 3 instances)" % project_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
