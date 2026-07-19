#!/usr/bin/env python3
"""Cooked-cubemap runtime-load proof (stdlib only): block-compress the stock
debug cubemap through the REAL export cook (Util/cook_textures.py + the tree's
texcook encoder), then boot the render-facade selfcheck against it and let its
skybox leg assert the compressed cube still samples its +X face red - proving
this render flavor LOADS a block-compressed cubemap with the face order and the
baked (prefiltered) mip chain intact.

Only the desktop BCn container boots on a desktop host (the next flavor's
desktop renderer maps ASTC/ETC2 pixel formats only in its mobile builds), so
this proves the BC1 .dds cube on both flavors; the mobile ASTC/ETC2 .oitd/.ktx
cube containers are asserted structurally by cook_textures.py --selftest and
ride the device tests for their on-GPU proof.

    run_cooked_cubemap_test.py --repo <root> --selfcheck <exe> --texcook <exe>
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--selfcheck", required=True)
    parser.add_argument("--texcook", required=True)
    parser.add_argument("--flavor", required=True, choices=("next", "classic"))
    args = parser.parse_args()

    if not os.path.isfile(args.texcook):
        fail("no texcook encoder at '%s' - build the tree first" % args.texcook)

    sys.path.insert(0, os.path.join(args.repo, "Util"))
    import cook_textures  # noqa: E402  (the real export cook)

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
        cooked = cook_textures.cook_payload(cooked_dir, "", args.flavor,
                                            args.texcook)
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
