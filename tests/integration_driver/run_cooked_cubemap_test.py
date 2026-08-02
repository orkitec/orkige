#!/usr/bin/env python3
"""Cooked-cubemap runtime-load proof (stdlib only): block-compress the stock
debug cubemap through the REAL export cook (`orkige_export cook-textures`,
the same code an export runs), then boot the render-facade selfcheck against it
and let its
skybox leg assert the compressed cube still samples its +X face red - proving
this render flavor LOADS a block-compressed cubemap with the face order and the
baked (prefiltered) mip chain intact.

Only the desktop BCn container boots on a desktop host (the next flavor's
desktop renderer maps ASTC/ETC2 pixel formats only in its mobile builds), so
this proves the BC1 .dds cube on both flavors; the mobile ASTC/ETC2 .oitd/.ktx
cube containers are asserted structurally by ExportTextureCookTests and
ride the device tests for their on-GPU proof.

    run_cooked_cubemap_test.py --repo <root> --selfcheck <exe> --exporter <exe>
                               --flavor next|classic
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile


def fail(message):
    print("run_cooked_cubemap_test: FAILED - " + message, file=sys.stderr)
    sys.exit(1)



def cook_payload(exporter, directory, platform, flavor):
    """run the REAL export texture cook over a staged directory in place - the
    exporter's own `cook-textures` entry point, the same code an export runs
    over its payload. Returns the number of textures rewritten."""
    command = [exporter, "cook-textures", directory, "--flavor", flavor]
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--selfcheck", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--flavor", required=True, choices=("next", "classic"))
    args = parser.parse_args()

    if not os.path.isfile(args.exporter):
        fail("no exporter at '%s' - build the tree first" % args.exporter)

    source = os.path.join(args.repo, "samples", "hello_orkige", "media",
                          "sky_faces.dds")
    if not os.path.isfile(source):
        fail("the stock debug cubemap '%s' is missing (make_sky_assets.py)"
             % source)

    with tempfile.TemporaryDirectory() as temp_root:
        cooked_dir = os.path.join(temp_root, "cooked")
        os.makedirs(cooked_dir)
        # a DISTINCT name so it never collides with the plain sky_faces.dds the
        # selfcheck also registers; BC1 (both desktop flavors load a compressed
        # DDS cube) via an explicit sidecar format
        cube = os.path.join(cooked_dir, "sky_faces_cooked.dds")
        shutil.copy2(source, cube)
        with open(cube + ".orkmeta", "w", newline="\n") as handle:
            handle.write('<orkmeta id="c0b0d0e0f00102030405060708090a0b">'
                         '<texture format="bc1"/></orkmeta>')
        cooked = cook_payload(args.exporter, cooked_dir, "", args.flavor)
        if cooked != 1:
            fail("expected 1 cooked cubemap, got %d" % cooked)
        # the cook rewrote it in place as a compressed cube DDS
        with open(cube, "rb") as handle:
            header = handle.read(128)
        if header[:4] != b"DDS " or header[84:88] != b"DXT1":
            fail("the cooked cube is not a BC1 DDS (%r)" % header[84:88])
        caps2 = struct.unpack_from("<I", header, 4 + 108)[0]
        if (caps2 & 0xFE00) != 0xFE00:
            fail("the cooked cube lost its cubemap caps")

        out_dir = os.path.join(temp_root, "out")
        os.makedirs(out_dir)
        env = dict(os.environ)
        env["ORKIGE_SELFCHECK_OUT"] = out_dir
        env["ORKIGE_SELFCHECK_COOKED_CUBE_DIR"] = cooked_dir
        result = subprocess.run([args.selfcheck], capture_output=True,
                                text=True, env=env, cwd=args.repo, timeout=300)
        output = (result.stdout or "") + (result.stderr or "")
        if result.returncode != 0 or \
                "compressed skybox cube probe" not in output:
            sys.stderr.write(output[-4000:])
            fail("the selfcheck did not prove the compressed cube loads "
                 "(exit %d)" % result.returncode)
        print("run_cooked_cubemap_test: OK (%s flavor loads a BC1 compressed "
              "cube - face order + mip chain intact)" % args.flavor)


if __name__ == "__main__":
    main()
