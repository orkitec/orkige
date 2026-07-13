#!/usr/bin/env python3
"""Generate the projects/benchmark/ showcase - an autonomous, self-running
3D+2D feature tour that doubles as a machine benchmark.

The project is a LevelManager sequence of vignette scenes, each scored by the
per-scene benchmark recorder (core_debug/BenchmarkRecorder, armed with
ORKIGE_BENCHMARK). A single shared director script (scripts/director.component.lua)
runs each scene autonomously - it sets up the camera, drives the scene's
motion/ramp, and after a frame budget wipes to the next scene. No input is
required; the sequence loops on the final results card.

Scenes, in order (levels.olevels):
  1. vista     baked terrain valley, a day->night sun arc through the atmosphere
               facade, PSSM shadows, PBS-material props, a rain weather phase
  2. lake      terrain shoreline + a WaterComponent expanse under a sunset sky,
               a slow camera drift
  3. lumens    the night preset + a pool of point lights ramped up over the
               scene's budget (self-limiting on the frame delta)
  4. swarm     3D particle stress: emitters ramped, additive + alpha mixes
  5. field     hundreds of ModelComponents sharing one mesh + one material
               (the Hlms auto-batch showcase), ramped like lumens
  6. flatland  the flat-colour 2D look: soft-body vector blobs, a morph blob,
               a sprite, parallax by zOrder, ortho camera
  7. console   a .oui GUI stress: an animated HUD + a settings screen cycling
               the project's languages, scroll views, nine-slice, TTF
  8. cascade   a physics field: a rain of planar bodies onto a floor, a
               time-scale hitstop beat
  9. tally     a GUI results card (per-scene names + the live frame delta the
               director tracked across the run) then loops

Everything is generated or hand-authored and license-clean:
  * terrain / material-prop / particle / GUI-atlas assets come from the tree's
    existing generators, run into assets/ (make_terrain_mesh, make_material_demo,
    make_particle_textures, make_gui_atlas)
  * vector .oshape blobs, PBS .omat variants, localisation .xlf, .oui screens,
    the scenes, levels.olevels, physics.olayers and the manifest are written here

Usage:
    python3 Util/make_benchmark_assets.py [project_dir]
    python3 Util/make_benchmark_assets.py --selftest   # validate, no writes
Defaults to projects/benchmark/ next to this repo.
"""

import argparse
import math
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UTIL = REPO / "Util"


# ---------------------------------------------------------------------------
# scene / prefab serialization (the XMLArchive forms SceneSerializer produces).
# The reflected components emit a count-prefixed NAMED (name, kind, value, ref)
# block; the loader keeps the constructed default for any field the file omits,
# so we only write the fields we set.
# ---------------------------------------------------------------------------

K_INT, K_FLOAT, K_BOOL, K_STRING, K_ENUM, K_VEC3, K_QUAT, K_COLOR, K_ASSETREF = range(9)


def fmt(value):
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, float):
        return "%g" % value
    return str(value)


class ComponentWriter:
    @staticmethod
    def _base_fields():
        return ['<unsigned_int value="0"/>', '<String value=""/>']

    @staticmethod
    def _named(records):
        lines = ['<unsigned_int value="%d"/>' % len(records)]
        for (name, kind, value, ref) in records:
            lines.append('<String value="%s"/>' % name)
            lines.append('<int value="%d"/>' % kind)
            lines.append('<String value="%s"/>' % value)
            lines.append('<String value="%s"/>' % ref)
        return lines

    def _reflected(self, type_name, records):
        return (type_name, self._base_fields() + self._named(records))

    def _component(self, type_name, fields):
        return (type_name, self._base_fields() + fields)

    # -- reflected components (subset-tolerant) ----------------------------

    def transform(self, x=0.0, y=0.0, z=0.0, sx=1.0, sy=1.0, sz=1.0,
                  quat=(1.0, 0.0, 0.0, 0.0)):
        return self._reflected("TransformComponent", [
            ("position", K_VEC3, "%s %s %s" % (fmt(float(x)), fmt(float(y)), fmt(float(z))), ""),
            ("orientation", K_QUAT, "%s %s %s %s" % tuple(fmt(float(q)) for q in quat), ""),
            ("scale", K_VEC3, "%s %s %s" % (fmt(float(sx)), fmt(float(sy)), fmt(float(sz))), ""),
        ])

    def model(self, mesh, material=""):
        recs = [("mesh", K_ASSETREF, mesh, "")]
        if material:
            recs.append(("material", K_ASSETREF, material, ""))
        return self._reflected("ModelComponent", recs)

    def light(self, light_type=1, colour=(1.0, 1.0, 1.0), intensity=1.0,
              rng=10.0, inner=30.0, outer=45.0, casts=False):
        return self._reflected("LightComponent", [
            ("type", K_ENUM, "%d" % light_type, ""),
            ("colour", K_COLOR, "%s %s %s 1" % tuple(fmt(float(c)) for c in colour), ""),
            ("intensity", K_FLOAT, fmt(float(intensity)), ""),
            ("range", K_FLOAT, fmt(float(rng)), ""),
            ("innerAngle", K_FLOAT, fmt(float(inner)), ""),
            ("outerAngle", K_FLOAT, fmt(float(outer)), ""),
            ("castsShadows", K_BOOL, fmt(bool(casts)), ""),
        ])

    def water(self, size_x=40.0, size_z=40.0, deep=(0.02, 0.08, 0.12, 1.0),
              shallow=(0.1, 0.4, 0.45, 1.0), opacity=0.85, wave_scale=6.0,
              wave_speed=0.05, fresnel=2.5, normal_tex="demo_terrain_normal.png"):
        return self._reflected("WaterComponent", [
            ("sizeX", K_FLOAT, fmt(float(size_x)), ""),
            ("sizeZ", K_FLOAT, fmt(float(size_z)), ""),
            ("deepColour", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in deep), ""),
            ("shallowColour", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in shallow), ""),
            ("opacity", K_FLOAT, fmt(float(opacity)), ""),
            ("waveScale", K_FLOAT, fmt(float(wave_scale)), ""),
            ("waveSpeed", K_FLOAT, fmt(float(wave_speed)), ""),
            ("fresnelPower", K_FLOAT, fmt(float(fresnel)), ""),
            ("normalTexture", K_ASSETREF, normal_tex, ""),
        ])

    def sprite(self, texture, width, height, z_order=0, tint=(1, 1, 1, 1),
               visible=True):
        return self._reflected("SpriteComponent", [
            ("width", K_FLOAT, fmt(float(width)), ""),
            ("height", K_FLOAT, fmt(float(height)), ""),
            ("tint", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in tint), ""),
            ("zOrder", K_INT, "%d" % z_order, ""),
            ("visible", K_BOOL, fmt(visible), ""),
            ("texture", K_ASSETREF, texture, ""),
        ])

    def vectorshape(self, shape, tint=(0.9, 0.45, 0.4, 1.0), scale=1.0,
                    z_order=0, soft_body=False, wobble_amount=0.0,
                    morph_clip=-1, morph_speed=1.0, morph_loop=True):
        recs = [
            ("tint", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in tint), ""),
            ("scale", K_FLOAT, fmt(float(scale)), ""),
            ("zOrder", K_INT, "%d" % z_order, ""),
            ("softBody", K_BOOL, fmt(bool(soft_body)), ""),
            ("wobbleAmount", K_FLOAT, fmt(float(wobble_amount)), ""),
            ("morphClip", K_INT, "%d" % morph_clip, ""),
            ("morphSpeed", K_FLOAT, fmt(float(morph_speed)), ""),
            ("morphLoop", K_BOOL, fmt(bool(morph_loop)), ""),
            ("shape", K_ASSETREF, shape, ""),
        ]
        return self._reflected("VectorShapeComponent", recs)

    def vectoranim(self, animation, clip="", speed=1.0, playing=True,
                   scale=1.0, z_order=0, tint=(1.0, 1.0, 1.0, 1.0)):
        recs = [
            ("clip", K_STRING, clip, ""),
            ("speed", K_FLOAT, fmt(float(speed)), ""),
            ("playing", K_BOOL, fmt(bool(playing)), ""),
            ("tint", K_COLOR, "%s %s %s %s" % tuple(fmt(float(c)) for c in tint), ""),
            ("scale", K_FLOAT, fmt(float(scale)), ""),
            ("zOrder", K_INT, "%d" % z_order, ""),
            ("animation", K_ASSETREF, animation, ""),
        ]
        return self._reflected("VectorAnimationComponent", recs)

    def rigid_box(self, hx, hy, hz, body_type=1, friction=0.5, restitution=0.0,
                  planar=True, layer="Default", is_sensor=False):
        return self._reflected("RigidBodyComponent", [
            ("bodyType", K_ENUM, "%d" % body_type, ""),
            ("shapeType", K_ENUM, "0", ""),
            ("halfExtents", K_VEC3, "%s %s %s" % (fmt(float(hx)), fmt(float(hy)), fmt(float(hz))), ""),
            ("mass", K_FLOAT, "1", ""),
            ("friction", K_FLOAT, fmt(float(friction)), ""),
            ("restitution", K_FLOAT, fmt(float(restitution)), ""),
            ("planar", K_BOOL, fmt(bool(planar)), ""),
            ("layer", K_STRING, layer, ""),
            ("isSensor", K_BOOL, fmt(bool(is_sensor)), ""),
        ])

    def rigid_sphere(self, radius, body_type=2, friction=0.4, restitution=0.3,
                     planar=True, layer="Default"):
        return self._reflected("RigidBodyComponent", [
            ("bodyType", K_ENUM, "%d" % body_type, ""),
            ("shapeType", K_ENUM, "1", ""),
            ("radius", K_FLOAT, fmt(float(radius)), ""),
            ("mass", K_FLOAT, "1", ""),
            ("friction", K_FLOAT, fmt(float(friction)), ""),
            ("restitution", K_FLOAT, fmt(float(restitution)), ""),
            ("planar", K_BOOL, fmt(bool(planar)), ""),
            ("layer", K_STRING, layer, ""),
            ("isSensor", K_BOOL, "0", ""),
        ])

    def script(self, kind, path, exports=()):
        records = [
            ("script", K_ASSETREF, path, ""),
            ("enabled", K_BOOL, "1", ""),
        ] + list(exports)
        return (kind, "ScriptComponent", self._base_fields() + self._named(records))

    def particles(self, texture, emission_rate=40.0, burst_count=0,
                  max_particles=200, z_order=6, blend_mode=1,
                  lifetime=(1.2, 2.4), speed=(1.0, 3.0),
                  start_size=0.4, end_size=0.0,
                  start_color=(1.0, 0.7, 0.3, 1.0), end_color=(1.0, 0.3, 0.1, 0.0),
                  space3d=True, world_space=True, emission_volume=1,
                  volume_extents=(6.0, 0.5, 6.0),
                  direction3d=(0.0, 1.0, 0.0), gravity3d=(0.0, -1.5, 0.0),
                  wind=(0.0, 0.0, 0.0), stretch=0.0):
        """A ParticleComponent, positional order mirrors ParticleComponent::save.
        Defaults are a rising-ember 3D emitter; presets override the tunables."""
        emit_on_start = emission_rate > 0.0
        return self._component("ParticleComponent", [
            '<String value="%s"/>' % texture,
            '<bool value="%s"/>' % fmt(emit_on_start),
            '<float value="%s"/>' % fmt(float(emission_rate)),
            '<int value="%d"/>' % burst_count,
            '<float value="0"/>',                              # duration
            '<bool value="1"/>',                               # looping
            '<float value="%s"/>' % fmt(float(lifetime[0])),
            '<float value="%s"/>' % fmt(float(lifetime[1])),
            '<float value="0"/>', '<float value="0"/>',        # spawnOffset x/y (2D)
            '<float value="90"/>', '<float value="40"/>',      # directionAngle / spreadAngle (2D)
            '<float value="%s"/>' % fmt(float(speed[0])),
            '<float value="%s"/>' % fmt(float(speed[1])),
            '<float value="0"/>', '<float value="0"/>',        # gravity x/y (2D)
            '<float value="1"/>',                              # damping
            '<float value="-30"/>', '<float value="30"/>',     # spin min/max
            '<float value="%s"/>' % fmt(float(start_size)),
            '<float value="%s"/>' % fmt(float(end_size)),
            '<float value="%s"/>' % fmt(float(start_color[0])),
            '<float value="%s"/>' % fmt(float(start_color[1])),
            '<float value="%s"/>' % fmt(float(start_color[2])),
            '<float value="%s"/>' % fmt(float(start_color[3])),
            '<float value="%s"/>' % fmt(float(end_color[0])),
            '<float value="%s"/>' % fmt(float(end_color[1])),
            '<float value="%s"/>' % fmt(float(end_color[2])),
            '<float value="%s"/>' % fmt(float(end_color[3])),
            '<String value="linear"/>', '<String value="quadOut"/>',
            '<int value="1"/>', '<int value="1"/>',            # atlas cols/rows
            '<int value="0"/>', '<int value="0"/>',            # atlas frame min/max
            '<int value="%d"/>' % max_particles,
            '<int value="%d"/>' % z_order,
            '<int value="%d"/>' % blend_mode,
            # 3D / weather block
            '<bool value="%s"/>' % fmt(bool(space3d)),
            '<bool value="%s"/>' % fmt(bool(world_space)),
            '<int value="%d"/>' % emission_volume,
            '<float value="%s"/>' % fmt(float(volume_extents[0])),
            '<float value="%s"/>' % fmt(float(volume_extents[1])),
            '<float value="%s"/>' % fmt(float(volume_extents[2])),
            '<float value="0"/>', '<float value="0"/>', '<float value="0"/>',  # spawnOffset3D
            '<float value="%s"/>' % fmt(float(direction3d[0])),
            '<float value="%s"/>' % fmt(float(direction3d[1])),
            '<float value="%s"/>' % fmt(float(direction3d[2])),
            '<float value="%s"/>' % fmt(float(gravity3d[0])),
            '<float value="%s"/>' % fmt(float(gravity3d[1])),
            '<float value="%s"/>' % fmt(float(gravity3d[2])),
            '<float value="%s"/>' % fmt(float(wind[0])),
            '<float value="%s"/>' % fmt(float(wind[1])),
            '<float value="%s"/>' % fmt(float(wind[2])),
            '<float value="%s"/>' % fmt(float(stretch)),
            '<float value="0"/>', '<float value="0"/>',        # flutter amp / freq
        ])

    @staticmethod
    def object_lines(name, parent, active, tags, components):
        lines = []
        lines.append('    <String value="%s"/>' % name)
        lines.append('    <String value="%s"/>' % parent)
        lines.append('    <bool value="%s"/>' % fmt(active))
        lines.append('    <unsigned_int value="%d"/>' % len(tags))
        lines.extend('    <String value="%s"/>' % tag for tag in tags)
        lines.append('    <String value=""/>')            # prefabRef (plain object)
        lines.append('    <unsigned_int value="%d"/>' % len(components))
        for comp in components:
            tag = comp[0]
            element, fields = (comp[1], comp[2]) if len(comp) == 3 \
                else (comp[0], comp[1])
            lines.append('    <String value="%s"/>' % tag)
            lines.append('    <%s create="0">' % element)
            lines.extend('        ' + field for field in fields)
            lines.append('    </%s>' % element)
        return lines


class SceneWriter(ComponentWriter):
    def __init__(self):
        self.objects = []

    def add(self, name, *components, parent="", active=True, tags=()):
        self.objects.append((name, parent, active, tuple(tags),
                             sorted(components, key=lambda c: c[0])))

    def to_text(self):
        lines = [
            '<?xml version="1.0" encoding="UTF-8"?>',
            '<XMLArchive Version="0">',
            '    <String value="orkige.oscene"/>',
            '    <int value="7"/>',
            '    <unsigned_int value="%d"/>' % len(self.objects),
        ]
        for (name, parent, active, tags, components) in self.objects:
            lines.extend(self.object_lines(name, parent, active, list(tags),
                                           components))
        lines.append('</XMLArchive>')
        return "\n".join(lines) + "\n"

    def write(self, path):
        path.write_text(self.to_text())


# ---------------------------------------------------------------------------
# .oshape organic blobs (VectorShapeComponent). Deterministic wobble contour.
# ---------------------------------------------------------------------------

def blob_contour(points, radius, wobble, salt):
    verts = []
    for i in range(points):
        a = 2.0 * math.pi * i / points
        # deterministic per-vertex radius variation (no random module)
        n = ((i * 1103515245 + salt * 12345) & 0x7fffffff) / 0x7fffffff
        r = radius * (1.0 + wobble * (n - 0.5))
        verts.append((r * math.cos(a), r * math.sin(a)))
    return verts


def write_oshape(path, fill, verts, morph_verts=None):
    lines = ["# orkige vector shape v1 - generated organic blob", "version 1"]
    lines.append("fill %.3f %.3f %.3f %.3f" % fill)
    lines.append("contour %d" % len(verts))
    for (x, y) in verts:
        lines.append("v %.4f %.4f" % (x, y))
    if morph_verts is not None:
        lines.append("morph target 1")
        lines.append("contour %d" % len(morph_verts))
        for (x, y) in morph_verts:
            lines.append("v %.4f %.4f" % (x, y))
    path.write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# .omat PBS material variants
# ---------------------------------------------------------------------------

OMAT_VARIANTS = {
    "prop_rock.omat": """# rough grey rock (dielectric)
version 1
albedo 0.45 0.44 0.42 1.0
metalness 0.0
roughness 0.85
""",
    "prop_metal.omat": """# polished metal
version 1
albedo 0.62 0.64 0.68 1.0
metalness 0.9
roughness 0.28
""",
    "prop_crystal.omat": """# glowing crystal (emissive)
version 1
albedo 0.20 0.35 0.55 1.0
metalness 0.1
roughness 0.4
emissive 0.15 0.45 0.85
""",
    "field_stone.omat": """# the shared instance-field material
version 1
albedo 0.55 0.50 0.42 1.0
metalness 0.0
roughness 0.7
""",
}


# ---------------------------------------------------------------------------
# localisation (.xlf, XLIFF 1.2). en source + de translation + en-XA pseudo.
# ---------------------------------------------------------------------------

STRINGS = {
    "bench.title": "Feature Tour",
    "bench.scene": "Scene",
    "bench.of": "of",
    "bench.settings": "Settings",
    "bench.graphics": "Graphics",
    "bench.audio": "Audio",
    "bench.language": "Language",
    "bench.shadows": "Shadows",
    "bench.results": "Results",
    "bench.frameMs": "Frame time",
    "bench.done": "Tour complete",
    # a non-Latin line to exercise lazy glyph paging
    "bench.hello": "Hello / Здравствуй / こんにちは",
}

DE = {
    "bench.title": "Funktionstour",
    "bench.scene": "Szene",
    "bench.of": "von",
    "bench.settings": "Einstellungen",
    "bench.graphics": "Grafik",
    "bench.audio": "Audio",
    "bench.language": "Sprache",
    "bench.shadows": "Schatten",
    "bench.results": "Ergebnisse",
    "bench.frameMs": "Bildzeit",
    "bench.done": "Tour abgeschlossen",
    "bench.hello": "Hallo / Здравствуй / こんにちは",
}

# pseudo-localisation: wrap + accent (deterministic, ASCII-only fallback so the
# bundled font always has the glyphs) - marks untranslated strings on screen.
_PSEUDO = str.maketrans("aeiouAEIOU", "àéîöüÀÉÎÖÜ")


def pseudo(text):
    return "[!" + text.translate(_PSEUDO) + "!]"


def xliff_unit(key, source, target=None):
    lines = ['      <trans-unit id="%s" resname="%s" xml:space="preserve">'
             % (key, key)]
    lines.append("        <source>%s</source>" % source)
    if target is not None:
        lines.append('        <target state="translated">%s</target>' % target)
    lines.append("      </trans-unit>")
    return lines


def write_xliff(path, source_lang, target_lang, translate=None):
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<xliff version="1.2" xmlns="urn:oasis:names:tc:xliff:document:1.2">',
        '  <file original="orkige-strings" source-language="%s"%s datatype="plaintext">'
        % (source_lang, (' target-language="%s"' % target_lang) if target_lang else ""),
        "    <header>",
        '      <tool tool-id="orkige_loc" tool-name="orkige_loc" tool-version="1"/>',
        "    </header>",
        "    <body>",
    ]
    for key in sorted(STRINGS):
        src = STRINGS[key]
        tgt = None if translate is None else translate(key, src)
        lines.extend(xliff_unit(key, src, tgt))
    lines.append("    </body>")
    lines.append("  </file>")
    lines.append("</xliff>")
    path.write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# .oui screens
# ---------------------------------------------------------------------------

HUD_OUI = """# hud.oui - the live tour HUD (scene title + a progress bar)
[Layout]
atlas = gui_default
root = fullwindow

[DecorWidget hudBar]
sprite = panel
z = 4
anchor = stretchtop
pivot = 0.5 0
offsets = 16 16 -16 72
nineSlice = true

[Label hudTitle]
parent = hudBar
z = 5
font = 9
text = @bench.title
anchor = stretchall
offsets = 16 8 -16 -8
textAlignment = topleft

[ProgressBar hudProgress]
parent = hudBar
z = 5
anchor = stretchbottom
offsets = 16 -18 -16 -6
"""

SETTINGS_OUI = """# settings.oui - a nine-slice settings-style card (console scene). A panel
# holds a title, a vertical layout group of localised rows and a button - the
# rows re-caption when the director cycles the active language.
[Layout]
atlas = gui_default
root = fullwindow

[DecorWidget panel]
sprite = panel
z = 4
anchor = center
pivot = 0.5 0.5
anchoredPos = 0 0
sizeDelta = 480 460
nineSlice = true

[Label title]
parent = panel
z = 5
font = 24
text = @bench.settings
anchor = stretchtop
offsets = 24 18 -24 60
textAlignment = topleft

[DecorWidget content]
parent = panel
z = 5
sprite = none
size = 120 120
color = 0 0 0 0
anchor = stretchall
offsets = 24 78 -24 -70
group = vertical
padding = 4 4 4 4
spacing = 12
childExpand = true

[Label rowGraphics]
parent = content
z = 5
font = 9
text = @bench.graphics
size = 300 30

[Label rowShadows]
parent = content
z = 5
font = 9
text = @bench.shadows
size = 300 30

[Label rowLanguage]
parent = content
z = 5
font = 9
text = @bench.language
size = 300 30

[Label rowHello]
parent = content
z = 5
font = 9
text = @bench.hello
size = 380 30

[Button btnOk]
parent = panel
z = 5
font = 9
sprite = button
text = @bench.done
anchor = bottom
pivot = 0.5 1
anchoredPos = 0 -20
sizeDelta = 260 40
"""


# ---------------------------------------------------------------------------
# scene builders. Cameras and motion are driven by the director; scenes carry
# the static content. A "Director" object with the shared director script runs
# each scene (mode selects the behavior); ramp scenes pre-place a POOL of
# inactive objects the director activates progressively.
# ---------------------------------------------------------------------------

# per-scene: (file, benchmark label, director mode, base seconds)
SCENES = [
    ("vista.oscene", "Terrace Vista", "vista", 12.0),
    ("lake.oscene", "Still Water", "lake", 10.0),
    ("lumens.oscene", "Night Lumens", "lumens", 12.0),
    ("swarm.oscene", "Ember Swarm", "swarm", 10.0),
    ("field.oscene", "Instance Field", "field", 12.0),
    ("flatland.oscene", "Flatland", "flatland", 12.0),
    ("console.oscene", "Console", "console", 10.0),
    ("cascade.oscene", "Cascade", "cascade", 10.0),
    ("tally.oscene", "Tally", "tally", 10.0),
]


def director(scene_writer, mode, label, seconds, extra_exports=()):
    exports = [
        ("sceneLabel", K_STRING, label, ""),
        ("mode", K_STRING, mode, ""),
        ("seconds", K_FLOAT, fmt(float(seconds)), ""),
    ] + list(extra_exports)
    scene_writer.add("Director",
                     scene_writer.script("director",
                                         "scripts/director.component.lua",
                                         exports=exports))


def terrain_object(scene, y=-4.0):
    scene.add("Terrain",
              scene.transform(0.0, y, 0.0),
              scene.model("demo_terrain.glb", "demo_terrain.omat"),
              tags=("terrain",))


def build_vista():
    s = SceneWriter()
    director(s, "vista", "Terrace Vista", 12.0)
    # the sun: the FIRST directional light (the atmosphere facade links its
    # sky to this light's direction); the director rotates it through the arc.
    s.add("Sun",
          s.transform(0.0, 20.0, 0.0,
                      quat=(0.9239, -0.3827, 0.0, 0.0)),  # tilted down ~45deg
          s.light(light_type=0, colour=(1.0, 0.95, 0.85), intensity=1.1,
                  casts=True),
          tags=("sun",))
    terrain_object(s)
    # PBS-material props scattered on the terrace
    props = [
        (-6.0, 1.0, -4.0, "prop_rock.omat", 1.2),
        (5.0, 1.2, -6.0, "prop_metal.omat", 1.4),
        (0.0, 0.8, -9.0, "prop_crystal.omat", 1.0),
        (-3.0, 0.9, -12.0, "prop_rock.omat", 1.1),
        (7.0, 1.0, -13.0, "prop_crystal.omat", 0.9),
    ]
    for i, (x, y, z, mat, sc) in enumerate(props):
        s.add("Prop%d" % i,
              s.transform(x, y, z, sc, sc, sc),
              s.model("demo_material_cube.glb", mat))
    # weather: a world-space rain emitter high over the valley (director toggles
    # it on for the rain phase by activating this object)
    s.add("Rain",
          s.transform(0.0, 16.0, -8.0),
          s.particles("particle_rain.png", emission_rate=120.0,
                      max_particles=600, blend_mode=0,
                      lifetime=(1.0, 1.6), speed=(0.0, 0.5),
                      start_size=0.5, end_size=0.5,
                      start_color=(0.7, 0.8, 0.95, 0.6),
                      end_color=(0.7, 0.8, 0.95, 0.0),
                      volume_extents=(18.0, 0.5, 18.0),
                      direction3d=(0.0, -1.0, 0.0),
                      gravity3d=(0.0, -18.0, 0.0), stretch=0.4),
          active=False, tags=("weather",))
    return s


def build_lake():
    s = SceneWriter()
    director(s, "lake", "Still Water", 10.0)
    s.add("Sun",
          s.transform(0.0, 20.0, 0.0, quat=(0.9659, -0.2588, 0.0, 0.0)),
          s.light(light_type=0, colour=(1.0, 0.7, 0.5), intensity=1.0,
                  casts=True),
          tags=("sun",))
    terrain_object(s, y=-4.5)
    # the water expanse just below the shoreline
    s.add("Lake",
          s.transform(0.0, -3.2, -6.0),
          s.water(size_x=60.0, size_z=60.0),
          tags=("water",))
    # a couple of rocks along the shore
    for i, (x, z) in enumerate([(-8.0, -3.0), (9.0, -5.0), (2.0, -10.0)]):
        s.add("Shore%d" % i,
              s.transform(x, 0.4, z, 1.3, 1.3, 1.3),
              s.model("demo_material_cube.glb", "prop_rock.omat"))
    return s


def build_lumens():
    s = SceneWriter()
    director(s, "lumens", "Night Lumens", 12.0)
    # a dim moon so the terrain reads under the point lights
    s.add("Sun",
          s.transform(0.0, 20.0, 0.0, quat=(0.9239, -0.3827, 0.0, 0.0)),
          s.light(light_type=0, colour=(0.3, 0.35, 0.5), intensity=0.5),
          tags=("sun",))
    terrain_object(s)
    # the point-light pool: laid out on a grid over the terrace, all INACTIVE;
    # the director ramps how many are on until the frame budget bends.
    colours = [(1.0, 0.3, 0.2), (0.2, 0.6, 1.0), (0.3, 1.0, 0.4),
               (1.0, 0.8, 0.2), (0.9, 0.3, 0.9)]
    idx = 0
    # a 5x6 grid (30 lamps): a real "many lights" ramp, kept within the classic
    # forward renderer's per-pass headroom so both flavors survive the ramp.
    for gz in range(6):
        for gx in range(5):
            x = -10.0 + gx * 5.0
            z = -2.0 - gz * 3.0
            col = colours[idx % len(colours)]
            s.add("Lamp%d" % idx,
                  s.transform(x, 1.5, z),
                  s.light(light_type=1, colour=col, intensity=4.0, rng=6.0),
                  active=False, tags=("pool",))
            idx += 1
    return s


def build_swarm():
    s = SceneWriter()
    director(s, "swarm", "Ember Swarm", 10.0)
    s.add("Sun",
          s.transform(0.0, 20.0, 0.0, quat=(0.9239, -0.3827, 0.0, 0.0)),
          s.light(light_type=0, colour=(0.4, 0.4, 0.5), intensity=0.8),
          tags=("sun",))
    # a pool of 3D particle emitters, additive and alpha mixed, INACTIVE
    for i in range(8):
        x = -10.0 + (i % 4) * 6.0
        z = -4.0 - (i // 4) * 6.0
        additive = (i % 2 == 0)
        s.add("Emitter%d" % i,
              s.transform(x, 0.0, z),
              s.particles("particle_dot.png",
                          emission_rate=90.0, max_particles=400,
                          blend_mode=1 if additive else 0,
                          lifetime=(1.0, 2.0), speed=(1.5, 3.5),
                          start_size=0.5, end_size=0.0,
                          start_color=(1.0, 0.6, 0.2, 1.0) if additive
                          else (0.6, 0.7, 1.0, 0.8),
                          end_color=(1.0, 0.2, 0.05, 0.0) if additive
                          else (0.6, 0.7, 1.0, 0.0),
                          emission_volume=0,
                          direction3d=(0.0, 1.0, 0.0),
                          gravity3d=(0.0, -1.0, 0.0)),
              active=False, tags=("pool",))
    return s


def build_field():
    s = SceneWriter()
    director(s, "field", "Instance Field", 12.0)
    s.add("Sun",
          s.transform(0.0, 20.0, 0.0, quat=(0.9239, -0.3827, 0.0, 0.0)),
          s.light(light_type=0, colour=(1.0, 0.96, 0.9), intensity=1.1,
                  casts=True),
          tags=("sun",))
    # a large field of ModelComponents sharing ONE mesh + ONE material - the
    # Hlms auto-batch showcase. All INACTIVE; the director ramps the count.
    idx = 0
    n = 18   # 18x18 = 324 instances
    for gz in range(n):
        for gx in range(n):
            x = -18.0 + gx * 2.0
            z = -2.0 - gz * 2.0
            s.add("Inst%d" % idx,
                  s.transform(x, 0.0, z, 0.5, 0.5, 0.5),
                  s.model("demo_material_cube.glb", "field_stone.omat"),
                  active=False, tags=("pool",))
            idx += 1
    return s


def build_flatland():
    s = SceneWriter()
    director(s, "flatland", "Flatland", 12.0)
    # parallax background bands (sprites at different zOrder), a soft-body blob,
    # a morph blob, a vector-anim character and a sprite.
    s.add("BgFar", s.transform(0.0, 0.0, 0.0),
          s.sprite("particle_dot.png", 40.0, 24.0, z_order=0,
                   tint=(0.3, 0.45, 0.7, 1.0)))
    s.add("BgNear", s.transform(0.0, -3.0, 0.0),
          s.sprite("particle_dot.png", 40.0, 12.0, z_order=1,
                   tint=(0.4, 0.6, 0.5, 1.0)))
    # a bouncing soft-body blob (a sibling rigid body squashes it)
    s.add("Blob",
          s.transform(-4.0, 4.0, 0.0),
          s.vectorshape("blob.oshape", tint=(0.95, 0.55, 0.45, 1.0),
                        scale=1.4, z_order=5, soft_body=True,
                        wobble_amount=0.35),
          s.rigid_sphere(1.2, planar=True, layer="Default"),
          tags=("blob",))
    # a morph blob driven by the director
    s.add("Morpher",
          s.transform(3.0, 1.0, 0.0),
          s.vectorshape("morphblob.oshape", tint=(0.5, 0.8, 0.9, 1.0),
                        scale=1.2, z_order=5, morph_clip=0, morph_speed=0.6),
          tags=("morph",))
    # a vector-animation character (idle clip)
    s.add("Walker",
          s.transform(0.0, -1.0, 0.0),
          s.vectoranim("walker.oanim", clip="idle", scale=1.4, z_order=6),
          tags=("walker",))
    # a foreground sprite
    s.add("Star", s.transform(-6.0, 3.0, 0.0),
          s.sprite("particle_dot.png", 1.0, 1.0, z_order=7,
                   tint=(1.0, 0.9, 0.4, 1.0)))
    return s


def build_console():
    s = SceneWriter()
    director(s, "console", "Console", 10.0)
    return s


def build_cascade():
    s = SceneWriter()
    director(s, "cascade", "Cascade", 10.0)
    # a static floor (planar bodies stack onto it, roller-proven)
    s.add("Floor",
          s.transform(0.0, -6.0, 0.0),
          s.sprite("particle_dot.png", 26.0, 0.6, z_order=1,
                   tint=(0.3, 0.3, 0.35, 1.0)),
          s.rigid_box(13.0, 0.3, 0.5, body_type=0, planar=True,
                      friction=0.6, layer="ground"))
    for i, x in enumerate([-11.0, 11.0]):
        s.add("Wall%d" % i,
              s.transform(x, -3.0, 0.0),
              s.rigid_box(0.3, 3.0, 0.5, body_type=0, planar=True,
                          layer="ground"))
    # a pool of dynamic bodies (spheres + boxes), INACTIVE, rained by the
    # director in waves onto the floor
    idx = 0
    for row in range(6):
        for col in range(10):
            x = -9.0 + col * 2.0
            y = 5.0 + row * 2.0
            if (idx % 2) == 0:
                body = s.rigid_sphere(0.6, planar=True, layer="body")
                spr = s.sprite("particle_dot.png", 1.2, 1.2, z_order=5,
                               tint=(1.0, 0.5, 0.3, 1.0))
            else:
                body = s.rigid_box(0.55, 0.55, 0.5, body_type=2, planar=True,
                                   layer="body", friction=0.5, restitution=0.1)
                spr = s.sprite("particle_dot.png", 1.1, 1.1, z_order=5,
                               tint=(0.4, 0.7, 1.0, 1.0))
            s.add("Body%d" % idx, s.transform(x, y, 0.0), spr, body,
                  active=False, tags=("pool",))
            idx += 1
    return s


def build_tally():
    s = SceneWriter()
    director(s, "tally", "Tally", 10.0)
    return s


BUILDERS = {
    "vista": build_vista, "lake": build_lake, "lumens": build_lumens,
    "swarm": build_swarm, "field": build_field, "flatland": build_flatland,
    "console": build_console, "cascade": build_cascade, "tally": build_tally,
}


# ---------------------------------------------------------------------------
# config assets + manifest
# ---------------------------------------------------------------------------

def write_levels(path):
    lines = [
        "<?1.0, UTF-8, yes?>",
        '<XMLArchive Version="0">',
        '    <String value="orkige.olevels"/>',
        '    <int value="1"/>',
        '    <unsigned_int value="%d"/>' % len(SCENES),
    ]
    for (file, label, mode, seconds) in SCENES:
        lines.append('    <String value="scenes/%s"/>' % file)
        lines.append('    <String value="%s"/>' % label)
        lines.append('    <int value="0"/>')
    lines.append('</XMLArchive>')
    path.write_text("\n".join(lines) + "\n")


def write_layers(path):
    # Default collides with all; body collides with ground + itself; ground with
    # everything but not... keep it simple: body x {ground, body}, ground static.
    names = ["Default", "body", "ground"]
    matrix = [
        [True, True, True],
        [True, True, True],
        [True, True, False],
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
        lines.extend('    <bool value="%d"/>' % (1 if cell else 0) for cell in row)
    lines.append('</XMLArchive>')
    path.write_text("\n".join(lines) + "\n")


MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<OrkigeProject version="1">
    <Name>Benchmark</Name>
    <MainScene>scenes/vista.oscene</MainScene>
    <Settings>
        <Setting key="physics.layers" value="physics.olayers"/>
        <Setting key="levels" value="levels.olevels"/>
        <Setting key="localisation" value="loc"/>
        <Setting key="cvar.r.shadowQuality" value="medium"/>
        <Setting key="export.macos.bundleId" value="com.orkitec.benchmark"/>
        <Setting key="export.ios.bundleId" value="com.orkitec.benchmark"/>
        <Setting key="export.android.package" value="com.orkitec.benchmark"/>
    </Settings>
</OrkigeProject>
"""

ATTRIBUTION = """# Attribution

Every asset in this project is generated by `Util/make_benchmark_assets.py`
(and the tree's shared generators it invokes) or hand-authored plain text.
Nothing is downloaded or third-party.

- Terrain mesh + ground material/textures: `Util/make_terrain_mesh.py`
- PBS demo prop mesh + maps: `Util/make_material_demo.py`
- Particle sprites: `Util/make_particle_textures.py`
- GUI atlas: `Util/make_gui_atlas.py`
- Vector `.oshape` blobs, `.omat` variants, `.oui` screens, localisation
  `.xlf`, the scenes, `levels.olevels`, `physics.olayers` and the manifest:
  written by `Util/make_benchmark_assets.py`
- Vector animation `walker.oanim`: cooked by `Util/cook_vector_anim.py` from a
  generated Lottie source kept beside it

The engine-default UI font (Nunito, SIL OFL) ships with the engine, not this
project.
"""


# ---------------------------------------------------------------------------
# a tiny Lottie walker source (idle + hop clips), cooked to walker.oanim
# ---------------------------------------------------------------------------

def walker_lottie():
    """A minimal two-clip Lottie: a bobbing rounded body. Deterministic, hand
    shaped to cook cleanly through Util/cook_vector_anim.py."""
    # a rounded square body path (4 vertices, bezier-rounded)
    def rect_shape(cy):
        w, h = 60.0, 70.0
        return {
            "ty": "gr",
            "nm": "body",
            "it": [
                {
                    "ty": "sh",
                    "ks": {"a": 0, "k": {
                        "c": True,
                        "v": [[-w, -h], [w, -h], [w, h], [-w, h]],
                        "i": [[0, -20], [20, 0], [0, 20], [-20, 0]],
                        "o": [[0, 20], [-20, 0], [0, -20], [20, 0]],
                    }},
                },
                {"ty": "fl", "c": {"a": 0, "k": [0.95, 0.6, 0.4, 1]}, "o": {"a": 0, "k": 100}},
                {"ty": "tr", "p": {"a": 0, "k": [0, cy]}, "a": {"a": 0, "k": [0, 0]},
                 "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}},
            ],
        }
    body_layer = {
        "ty": 4,
        "nm": "body",
        "ind": 1,
        "ks": {
            "p": {"a": 1, "k": [
                {"t": 0, "s": [100, 100], "i": {"x": [0.4], "y": [1]}, "o": {"x": [0.6], "y": [0]}},
                {"t": 15, "s": [100, 88], "i": {"x": [0.4], "y": [1]}, "o": {"x": [0.6], "y": [0]}},
                {"t": 30, "s": [100, 100], "i": {"x": [0.4], "y": [1]}, "o": {"x": [0.6], "y": [0]}},
                {"t": 45, "s": [100, 60], "i": {"x": [0.4], "y": [1]}, "o": {"x": [0.6], "y": [0]}},
                {"t": 60, "s": [100, 100]},
            ]},
            "a": {"a": 0, "k": [0, 0]},
            "s": {"a": 0, "k": [100, 100]},
            "r": {"a": 0, "k": 0},
            "o": {"a": 0, "k": 100},
        },
        "shapes": [rect_shape(0.0)],
        "ip": 0,
        "op": 60,
    }
    return {
        "v": "5.7.0",
        "fr": 30,
        "ip": 0,
        "op": 60,
        "w": 200,
        "h": 200,
        "markers": [
            {"tm": 0, "cm": "idle", "dr": 30},
            {"tm": 30, "cm": "hop", "dr": 30},
        ],
        "layers": [body_layer],
    }


# ---------------------------------------------------------------------------
# driver
# ---------------------------------------------------------------------------

def run_generator(script, *args):
    cmd = [sys.executable, str(UTIL / script)] + [str(a) for a in args]
    subprocess.run(cmd, check=True, cwd=str(REPO))


def build_all(project_dir):
    assets = project_dir / "assets"
    scenes = project_dir / "scenes"
    scripts = project_dir / "scripts"
    loc = project_dir / "loc"
    for d in (assets, scenes, scripts, loc):
        d.mkdir(parents=True, exist_ok=True)

    # shared generators -> assets/
    run_generator("make_terrain_mesh.py", "--chunks", "8", "--chunk-quads", "32",
                  assets)
    run_generator("make_material_demo.py", assets)
    run_generator("make_particle_textures.py", assets)
    run_generator("make_gui_atlas.py", assets, "gui_default")

    # .omat variants
    for name, text in OMAT_VARIANTS.items():
        (assets / name).write_text(text)

    # .oshape blobs (a plain blob + a morphing blob)
    base = blob_contour(12, 1.0, 0.25, 3)
    write_oshape(assets / "blob.oshape", (0.95, 0.55, 0.45, 1.0), base)
    star = blob_contour(12, 1.0, 0.55, 11)
    write_oshape(assets / "morphblob.oshape", (0.5, 0.8, 0.9, 1.0), base,
                 morph_verts=star)

    # vector animation: write the Lottie source and cook it in place
    import json
    walker_json = assets / "walker.json"
    walker_json.write_text(json.dumps(walker_lottie(), indent=1) + "\n")
    run_generator("cook_vector_anim.py", walker_json)

    # localisation
    write_xliff(loc / "en.xlf", "en", None, translate=None)
    write_xliff(loc / "de.xlf", "en", "de", translate=lambda k, s: DE.get(k, s))
    write_xliff(loc / "en-XA.xlf", "en", "en-XA",
                translate=lambda k, s: pseudo(s))

    # .oui screens
    (assets / "hud.oui").write_text(HUD_OUI)
    (assets / "settings.oui").write_text(SETTINGS_OUI)

    # scenes
    for (file, label, mode, seconds) in SCENES:
        s = BUILDERS[mode]()
        s.write(scenes / file)
        print("wrote %s (%d objects)" % (scenes / file, len(s.objects)))

    # config + manifest + attribution
    write_levels(project_dir / "levels.olevels")
    write_layers(project_dir / "physics.olayers")
    (project_dir / "project.orkproj").write_text(MANIFEST)
    (project_dir / "ATTRIBUTION.md").write_text(ATTRIBUTION)
    print("wrote manifest, levels.olevels, physics.olayers, ATTRIBUTION.md")


def selftest():
    # exercise the pure writers without touching disk / subprocesses
    for mode, builder in BUILDERS.items():
        s = builder()
        text = s.to_text()
        assert text.startswith('<?xml'), mode
        assert '<int value="7"/>' in text, "scene version 7 marker missing"
        assert 'Director' in text, "no director in %s" % mode
    # xliff round-trips its keys
    import io
    buf = io.StringIO()
    for key in sorted(STRINGS):
        buf.write(key)
    assert "bench.title" in buf.getvalue()
    # oshape contour count matches
    c = blob_contour(12, 1.0, 0.25, 3)
    assert len(c) == 12
    # walker lottie has both clip markers
    w = walker_lottie()
    names = [m["cm"] for m in w["markers"]]
    assert "idle" in names and "hop" in names, names
    print("make_benchmark_assets selftest OK: %d scenes, %d strings" %
          (len(BUILDERS), len(STRINGS)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project_dir", nargs="?",
                        default=str(REPO / "projects/benchmark"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    build_all(Path(args.project_dir))


if __name__ == "__main__":
    main()
