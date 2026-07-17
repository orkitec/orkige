#!/usr/bin/env python3
"""Cooked-texture runtime-load proof (stdlib only): stage the asset_rename
fixture project into a temp copy, run the REAL export texture cook over it
(Util/cook_textures.py + the tree's texcook encoder), assert the payload
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
ios (ASTC) and android (ETC2) cooks are asserted structurally here (the
next flavor's .oitd output shape) and load-proven by the iOS-simulator and
Android Play/export device tests.

    run_cooked_textures_test.py --repo <root> --player <exe> --texcook <exe>
                                --flavor next|classic
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


def stage_and_cook(repo, cook_textures, platform, flavor, texcook, temp_root,
                   leg_name, expect_extension):
    """copy the fixture, cook it for (platform, flavor), assert the rename
    and return the cooked project directory"""
    project_dir = os.path.join(temp_root, "project-" + leg_name)
    shutil.copytree(os.path.join(repo, FIXTURE), project_dir)
    cooked = cook_textures.cook_payload(project_dir, platform, flavor,
                                        texcook)
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
    parser.add_argument("--texcook", required=True)
    parser.add_argument("--flavor", required=True,
                        choices=("next", "classic"))
    args = parser.parse_args()

    sys.path.insert(0, os.path.join(args.repo, "Util"))
    import cook_textures  # noqa: E402  (the real export cook)

    if not os.path.isfile(args.texcook):
        fail("no texcook encoder at '%s' - build the tree first"
             % args.texcook)

    with tempfile.TemporaryDirectory() as temp_root:
        # desktop leg (BCn in .dds, both flavors): the ID reference...
        project = stage_and_cook(args.repo, cook_textures, "", args.flavor,
                                 args.texcook, temp_root, "dds-id", ".dds")
        run_player(args.player, project, "ball_renamed.dds", "dds-id")
        # ... and the BARE id-less reference through the extension fallback
        project = stage_and_cook(args.repo, cook_textures, "", args.flavor,
                                 args.texcook, temp_root, "dds-bare", ".dds")
        rewrite_scene_bare(project)
        run_player(args.player, project, "ball_renamed.png", "dds-bare")

        if args.flavor == "next":
            # the mobile cooks, asserted structurally (a desktop host cannot
            # boot ASTC/ETC2 - the mobile Play/export device tests own that
            # proof): the ios (ASTC) and android (ETC2) payloads must emit
            # well-formed .oitd containers with the sidecar renamed along
            for platform, leg in (("ios", "oitd-astc"),
                                  ("android", "oitd-etc2")):
                project = stage_and_cook(args.repo, cook_textures, platform,
                                         args.flavor, args.texcook,
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
