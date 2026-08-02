#!/usr/bin/env python3
"""Cooked-texture runtime-load proof (stdlib only): stage the asset_rename
fixture project into a temp copy, run the REAL export texture cook over it
(`orkige_export cook-textures`, the same code an export runs), assert the
payload
rename (ball_renamed.png -> ball_renamed.dds/.oitd, sidecar renamed along),
then boot the actual player on the cooked copy and let its
ORKIGE_COOKED_SELFCHECK assert the sprite renders from the compressed
container. Two references are proven per leg:

  * the ID leg: the committed scene's stale "ball.png" + sidecar id must
    resolve to the COOKED file name through the asset-id machinery,
  * the BARE leg: a rewritten scene referencing "ball_renamed.png" with NO
    id must render through the backend's cooked-extension fallback.

Boot legs run the desktop cook (BCn -> .dds, both flavors). The MOBILE
containers cannot boot on a desktop host - the next flavor's desktop
renderer maps ASTC/ETC2 pixel formats only in its mobile builds - so the
ios and android cooks (both ASTC by default) are asserted structurally here
(the next flavor's .oitd output shape) and load-proven by the iOS-simulator
and Android Play/export device tests.

    run_cooked_textures_test.py --repo <root> --player <exe>
                                --exporter <exe> --flavor next|classic
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

FIXTURE = os.path.join("tests", "projects", "asset_rename")


def fail(message):
    print("run_cooked_textures_test: FAILED - " + message, file=sys.stderr)
    sys.exit(1)


def cook_payload(exporter, project_dir, platform, flavor):
    """run the REAL export texture cook over a staged project in place - the
    exporter's own `cook-textures` entry point, which is the same code an
    export runs over its payload. Returns the number of textures rewritten."""
    command = [exporter, "cook-textures", project_dir, "--flavor", flavor]
    if platform:
        command += ["--platform", platform]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        fail("the texture cook failed: %s%s"
             % (result.stdout or "", result.stderr or ""))
    marker = "orkige_export: COOKED "
    for line in (result.stdout or "").splitlines():
        if line.startswith(marker):
            return int(line[len(marker):].strip())
    fail("the texture cook reported no count: " + (result.stdout or ""))


def stage_and_cook(repo, exporter, platform, flavor, temp_root,
                   leg_name, expect_extension):
    """copy the fixture, cook it for (platform, flavor), assert the rename
    and return the cooked project directory"""
    project_dir = os.path.join(temp_root, "project-" + leg_name)
    shutil.copytree(os.path.join(repo, FIXTURE), project_dir)
    cooked = cook_payload(exporter, project_dir, platform, flavor)
    if cooked != 1:
        fail("%s: expected 1 cooked texture, got %d" % (leg_name, cooked))
    assets = os.path.join(project_dir, "assets")
    cooked_file = os.path.join(assets, "ball_renamed" + expect_extension)
    if not os.path.isfile(cooked_file):
        fail("%s: expected '%s' in the cooked payload" % (leg_name,
                                                          cooked_file))
    if os.path.exists(os.path.join(assets, "ball_renamed.png")):
        fail("%s: the source PNG must be replaced, not kept" % leg_name)
    if not os.path.isfile(cooked_file + ".orkmeta"):
        fail("%s: the sidecar must be renamed with the texture" % leg_name)
    return project_dir


def rewrite_scene_bare(project_dir):
    """turn the committed id-carrying stale reference into a BARE, id-less
    reference to the source file name (the extension-fallback case)"""
    scene = os.path.join(project_dir, "scenes", "main.oscene")
    with open(scene) as handle:
        text = handle.read()
    text = text.replace('<String value="ball.png"/>',
                        '<String value="ball_renamed.png"/>')
    text = text.replace(
        '<String value="f1e2d3c4b5a697880123456789abcdef"/>',
        '<String value=""/>')
    with open(scene, "w", newline="\n") as handle:
        handle.write(text)


def run_player(player, project_dir, expected_texture, leg_name):
    env = dict(os.environ)
    env["ORKIGE_COOKED_SELFCHECK"] = expected_texture
    env["ORKIGE_DEMO_FRAMES"] = "60"
    result = subprocess.run([player, "--project", project_dir],
                            capture_output=True, text=True, env=env,
                            timeout=300)
    output = (result.stdout or "") + (result.stderr or "")
    if result.returncode != 0 or "COOKED SELFCHECK PASSED" not in output:
        sys.stderr.write(output[-4000:])
        fail("%s: player exited %d without a COOKED SELFCHECK PASS"
             % (leg_name, result.returncode))
    print("run_cooked_textures_test: %s leg passed ('%s')"
          % (leg_name, expected_texture))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--flavor", required=True,
                        choices=("next", "classic"))
    args = parser.parse_args()

    if not os.path.isfile(args.exporter):
        fail("no exporter at '%s' - build the tree first" % args.exporter)

    with tempfile.TemporaryDirectory() as temp_root:
        # desktop leg (BCn in .dds, both flavors): the ID reference...
        project = stage_and_cook(args.repo, args.exporter, "", args.flavor,
                                 temp_root, "dds-id", ".dds")
        run_player(args.player, project, "ball_renamed.dds", "dds-id")
        # ... and the BARE id-less reference through the extension fallback
        project = stage_and_cook(args.repo, args.exporter, "", args.flavor,
                                 temp_root, "dds-bare", ".dds")
        rewrite_scene_bare(project)
        run_player(args.player, project, "ball_renamed.png", "dds-bare")

        if args.flavor == "next":
            # the mobile cooks, asserted structurally (a desktop host cannot
            # boot ASTC/ETC2 - the mobile Play/export device tests own that
            # proof): the ios and android payloads (both ASTC by default) must
            # emit well-formed .oitd containers with the sidecar renamed along
            for platform, leg in (("ios", "oitd-ios"),
                                  ("android", "oitd-android")):
                project = stage_and_cook(args.repo, args.exporter,
                                         platform, args.flavor,
                                         temp_root, leg, ".oitd")
                cooked_file = os.path.join(project, "assets",
                                           "ball_renamed.oitd")
                with open(cooked_file, "rb") as handle:
                    magic = handle.read(4)
                if magic != b"OITD":
                    fail("%s: bad container magic %r" % (leg, magic))
                print("run_cooked_textures_test: %s structure leg passed"
                      % leg)

    print("run_cooked_textures_test: OK")


if __name__ == "__main__":
    main()
