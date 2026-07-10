#!/usr/bin/env python3
"""Generate the tests/projects/integration fixture: a compact project whose
scenes each COMBINE several runtime systems in one running world, so the
player selfchecks can assert that the systems still cooperate end to end (no
single-feature selfcheck exercises the combinations).

Two scenes, two selfchecks (tools/player/main.cpp):

  scenes/contact.oscene  (ORKIGE_INTEGRATION_CONTACT_SELFCHECK)
      A dynamic Ball (RigidBody) over a static sensor Goal tagged "goal".
      ball_probe.lua discovers the goal BY TAG (world.findByTag), holds the
      ball still until an INPUT ACTION fires ("jump" on SPACE, injected by the
      selfcheck), then drops it into the sensor whose overlap fires the
      onContactBegin CONTACT EVENT - the whole tags + input-action + physics +
      contact chain in one script.

  scenes/levelA.oscene / scenes/levelB.oscene
      (ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK)
      Level A runs a live TWEEN (a moving Mover sprite) and a live PARTICLE
      emitter (Fx) while director.lua requests a DEFERRED LEVEL SWITCH mid-
      tween; level B's survivor.lua proves the new level ticks and that the
      shared table survived the GameObjectManager::clear teardown.

The scene/asset writers are reused from make_roller_assets (the same
XMLArchive forms SceneSerializer/PrefabSerializer produce); this module only
composes the fixture. Deterministic: rerunning it never churns committed ids.
Run from the repo root:  python3 Util/make_integration_assets.py
"""
import hashlib
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_roller_assets as roller  # noqa: E402  (writers + PNG encoder)


def write_meta(project_dir, asset_path):
    """Emit the asset's .orkmeta sidecar with a deterministic id (md5 of the
    project-relative path under this fixture's namespace); an existing sidecar
    is preserved so regenerating never churns ids."""
    meta_path = Path(str(asset_path) + ".orkmeta")
    if meta_path.exists():
        import re
        match = re.search(r'id="([0-9a-f]+)"', meta_path.read_text())
        assert match, "unreadable sidecar: %s" % meta_path
        return match.group(1)
    relative = Path(asset_path).resolve().relative_to(
        Path(project_dir).resolve()).as_posix()
    asset_id = hashlib.md5(
        ("orkige.integration:" + relative).encode("utf-8")).hexdigest()
    meta_path.write_text('<orkmeta id="%s"/>\n' % asset_id)
    return asset_id


def make_dot_texture(size=32):
    """A plain soft-edged white dot - the one sprite texture every object in
    the fixture shares (the selfchecks assert behavior, not pixels)."""
    rows = []
    half = (size - 1) / 2.0
    for y in range(size):
        row = []
        for x in range(size):
            dx = (x - half) / half
            dy = (y - half) / half
            dist = (dx * dx + dy * dy) ** 0.5
            alpha = roller.clamp8(255 * (1.0 - min(1.0, dist)))
            row.append((255, 255, 255, alpha))
        rows.append(row)
    return roller.encode_png_rgba(size, size, rows)


def build_contact_scene(writer_cls):
    """Ball (dynamic sphere, tagged 'mover') dropping into the Goal (static
    sensor box, tagged 'goal') once the input action gates gravity on."""
    scene = writer_cls()
    scene.add(
        "Ball",
        scene.transform(0.0, 2.0, 0.0),
        scene.sprite("dot.png", 0.8, 0.8, z_order=1),
        scene.rigid_sphere(0.4, mass=1.0, planar=True, layer="ball"),
        scene.script("scripts/ball_probe.lua"),
        tags=["mover"],
    )
    scene.add(
        "Goal",
        scene.transform(0.0, -0.5, 0.0),
        scene.sprite("dot.png", 1.2, 1.2, z_order=0, tint=(1.0, 0.9, 0.2, 1.0)),
        scene.rigid_sensor_box(0.6, 0.6, 0.6, layer="Trigger"),
        tags=["goal"],
    )
    return scene


def build_level_a_scene(writer_cls):
    """Mover (tween target + director script) plus a live particle emitter."""
    scene = writer_cls()
    scene.add(
        "Mover",
        scene.transform(-2.0, 0.0, 0.0),
        scene.sprite("dot.png", 0.6, 0.6, z_order=1),
        scene.script("scripts/director.lua"),
    )
    scene.add(
        "Fx",
        scene.transform(0.0, 0.0, 0.0),
        scene.particles("dot.png", burst_count=0, max_particles=48),
    )
    return scene


def build_level_b_scene(writer_cls):
    """The switched-to level: one Survivor proving the new world ticks."""
    scene = writer_cls()
    scene.add(
        "Survivor",
        scene.transform(0.0, 0.0, 0.0),
        scene.sprite("dot.png", 0.6, 0.6, z_order=1, tint=(0.3, 1.0, 0.4, 1.0)),
        scene.script("scripts/survivor.lua"),
    )
    return scene


BALL_PROBE_LUA = """\
-- ball_probe.lua - the tags + input-action + physics + contact-event chain in
-- one script (player_integration_contact_selfcheck). The ball hangs still
-- (gravity off) until the injected "jump" action fires, then falls into the
-- goal SENSOR discovered BY TAG; the sensor overlap fires onContactBegin.
local physics
local actions
local goalId

function init(self)
\tphysics = PhysicsWorld.getSingleton()
\tactions = InputActions.getSingleton()
\t-- gravity OFF at boot: the input action is what causally drops the ball
\tphysics:setGravity(Vector3(0.0, 0.0, 0.0))
\tshared.integration = {
\t\tfound = 0, foundGoal = "", input = 0, contact = 0, contactOther = "",
\t}
\t-- discover the goal by TAG, not a hardcoded id
\tlocal goals = world.findByTag("goal")
\tshared.integration.found = #goals
\tif #goals > 0 then
\t\tgoalId = goals[1].id
\t\tshared.integration.foundGoal = goalId
\tend
end

function update(self, dt)
\t-- the named action edge (SPACE), injected by the selfcheck: it turns
\t-- gravity on, so the contact that follows is INPUT-driven
\tif actions:pressed("jump") then
\t\tshared.integration.input = shared.integration.input + 1
\t\tphysics:setGravity(Vector3(0.0, -14.0, 0.0))
\tend
end

function onContactBegin(self, other)
\t-- only count the contact when the other body is the tag-discovered goal
\tif goalId ~= nil and other.id == goalId then
\t\tshared.integration.contact = shared.integration.contact + 1
\t\tshared.integration.contactOther = other.id
\tend
end
"""

DIRECTOR_LUA = """\
-- director.lua - level A of the level-switch integration
-- (player_integration_levelswitch_selfcheck). It starts a TWEEN on itself and
-- requests a DEFERRED level switch WHILE the tween (and the Fx particle
-- emitter) are still live, so the switch must tear a running tween + emitter
-- down cleanly. shared.integ2.carry proves the shared table survives the swap.
local elapsed = 0
local switched = false

function init(self)
\tshared.integ2 = shared.integ2 or {}
\tshared.integ2.aliveA = 1
\tshared.integ2.carry = 4242
\tshared.integ2.moverStartX = self.transform:getPosition().x
\t-- a 1.5s move: still running when we switch ~0.3s in
\ttween.move("Mover", 2.0, 0.0, 0.0, 1.5)
\t-- and a live particle burst on the sibling emitter (world.getParticles):
\t-- these are ALSO in flight when the switch tears the scene down
\tlocal fx = world.getParticles("Fx")
\tif fx ~= nil then
\t\tfx:burst(24)
\tend
end

function update(self, dt)
\telapsed = elapsed + dt
\tshared.integ2.moverX = self.transform:getPosition().x
\tif not switched and elapsed > 0.3 then
\t\tswitched = true
\t\tshared.integ2.switched = 1
\t\tworld.loadScene("scenes/levelB.oscene")
\tend
end
"""

SURVIVOR_LUA = """\
-- survivor.lua - level B of the level-switch integration. Its init proves the
-- new level booted (and that shared.integ2.carry survived the teardown); its
-- update proves the switched-to world actually ticks.
function init(self)
\tshared.integ2 = shared.integ2 or {}
\tshared.integ2.levelBBooted = 1
\tshared.integ2.carrySeen = shared.integ2.carry or -1
\tshared.integ2.levelBTicks = 0
end

function update(self, dt)
\tshared.integ2.levelBTicks = (shared.integ2.levelBTicks or 0) + 1
end
"""

PROJECT_ORKPROJ = """\
<?xml version="1.0" encoding="UTF-8"?>
<OrkigeProject version="1">
    <Name>Integration Fixture</Name>
    <MainScene>scenes/contact.oscene</MainScene>
    <Settings>
        <Setting key="physics.layers" value="physics.olayers"/>
    </Settings>
</OrkigeProject>
"""


def main():
    root = Path(__file__).resolve().parent.parent
    project_dir = root / "tests" / "projects" / "integration"
    scenes = project_dir / "scenes"
    scripts = project_dir / "scripts"
    assets = project_dir / "assets"
    for directory in (scenes, scripts, assets):
        directory.mkdir(parents=True, exist_ok=True)

    (project_dir / "project.orkproj").write_text(PROJECT_ORKPROJ)
    roller.write_layers(project_dir / "physics.olayers")

    (assets / "dot.png").write_bytes(make_dot_texture())
    write_meta(project_dir, assets / "dot.png")

    (scripts / "ball_probe.lua").write_text(BALL_PROBE_LUA)
    (scripts / "director.lua").write_text(DIRECTOR_LUA)
    (scripts / "survivor.lua").write_text(SURVIVOR_LUA)

    build_contact_scene(roller.SceneWriter).write(scenes / "contact.oscene")
    build_level_a_scene(roller.SceneWriter).write(scenes / "levelA.oscene")
    build_level_b_scene(roller.SceneWriter).write(scenes / "levelB.oscene")

    print("wrote %s" % project_dir)


if __name__ == "__main__":
    main()
