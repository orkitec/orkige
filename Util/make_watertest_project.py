#!/usr/bin/env python3
"""Generate projects/watertest - a minimal WATER TEST BENCH project.

A hand-testing playground for the water surface: one scene with a water
plane over a bright checkered bed and three accent blocks at different
depths (one piercing the surface), seen from a raised vantage where
transparency is trivially readable - unlike the benchmark lake's grazing
showcase camera. Open the project in the editor, tweak the WaterComponent
in the Inspector (opacity, colours, waves, refraction) and Play; or run
the player directly:

    ./build/<preset>/tools/player/orkige_player scenes/main.oscene \
        --project projects/watertest

Everything is generated: re-run this script after changing it. The prop
mesh + maps come from make_material_demo.py --cube-only (the shared demo
cube); the water plane/normal map are engine media and need no project
asset. Stdlib only.
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UTIL = REPO / "Util"
sys.path.insert(0, str(UTIL))
import make_benchmark_assets as mba  # noqa: E402  (SceneWriter reuse)

BED_OMAT = """# watertest: bright checkered bed - the transparency read target
version 1
albedo 1.0 1.0 1.0 1.0
albedoTexture demo_mat_albedo.png
metalness 0.0
roughness 0.8
"""

RED_OMAT = """# watertest: flat bright accent block
version 1
albedo 0.9 0.15 0.1 1.0
metalness 0.0
roughness 0.6
"""

MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<OrkigeProject version="1">
    <Name>Water Test</Name>
    <MainScene>scenes/main.oscene</MainScene>
</OrkigeProject>
"""

CAM_SCRIPT = """--- watertest camera: a fixed raised vantage over the test tank so the
--- submerged bed and blocks read immediately (a grazing camera hides
--- transmission behind fresnel - this one looks ~30 degrees down).
--- The day atmosphere comes from the TESTED preset (raw sky/fog values
--- white surfaces out or black the sky - always blend presets).

function init(self)
	engine = Engine.getSingleton()
	engine:setAtmosphereSky("procedural", "")
	engine:setAtmosphereBlend("day", "sunset", 0.0)
	engine:setCameraPerspective()
	local cam = engine:getCamera()
	local node = cam ~= nil and cam:getNode() or nil
	if node ~= nil then
		node:setPosition(Vector3(0, 7, 13))
		node:lookAt(Vector3(0, -1.0, -2.0), TS.TS_WORLD, Vector3(0, 0, -1))
	end
end

function update(self, dt)
end
"""


def build_scene():
    s = mba.SceneWriter()
    s.add("Camera",
          s.transform(0.0, 0.0, 0.0),
          s.script("watercam", "scripts/watercam.component.lua"))
    # a clear-day sun, high enough that the bed is well lit
    s.add("Sun",
          s.transform(0.0, 20.0, 0.0, quat=(0.9239, -0.3827, 0.0, 0.0)),
          s.light(light_type=0, colour=(1.0, 0.95, 0.85), intensity=1.3),
          tags=("sun",))
    s.add("Water",
          s.transform(0.0, 0.0, 0.0),
          s.water(size_x=30.0, size_z=30.0,
                  wave_height=0.25, screen_space_refraction=True,
                  planar_reflection=False,
                  deep=(0.05, 0.22, 0.32, 1.0),
                  shallow=(0.30, 0.47, 0.62, 1.0),
                  opacity=0.6,
                  normal_tex="water_normal.png"),
          tags=("water",))
    # the bright checkered bed: a flattened demo cube, top ~2.1 under the
    # surface (cube half-extent 0.8 x scaleY 0.5 = 0.4)
    s.add("Bed",
          s.transform(0.0, -2.5, 0.0, 11.0, 0.5, 11.0, static=True),
          s.model("demo_material_cube.glb", "watertest_bed.omat"))
    # accent blocks: shallow / piercing / deep
    for name, (x, y, z, sc) in {
            "BlockShallow": (-4.0, -1.2, -2.0, 1.0),
            "BlockPiercing": (3.0, -0.35, -4.0, 1.0),
            "BlockDeep": (0.0, -1.8, 2.0, 1.0)}.items():
        s.add(name,
              s.transform(x, y, z, sc, sc, sc),
              s.model("demo_material_cube.glb", "watertest_red.omat"))
    return s


def main():
    project = REPO / "projects" / "watertest"
    assets = project / "assets"
    scenes = project / "scenes"
    scripts = project / "scripts"
    for d in (assets, scenes, scripts):
        d.mkdir(parents=True, exist_ok=True)

    subprocess.run([sys.executable, str(UTIL / "make_material_demo.py"),
                    "--cube-only", str(assets)], check=True,
                   stdout=subprocess.DEVNULL)
    (assets / "watertest_bed.omat").write_text(BED_OMAT, newline="\n")
    (assets / "watertest_red.omat").write_text(RED_OMAT, newline="\n")
    (project / "project.orkproj").write_text(MANIFEST, newline="\n")
    (scripts / "watercam.component.lua").write_text(CAM_SCRIPT, newline="\n")
    build_scene().write(scenes / "main.oscene")
    print("watertest project written to %s" % project)


if __name__ == "__main__":
    main()
