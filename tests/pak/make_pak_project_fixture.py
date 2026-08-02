#!/usr/bin/env python3
"""Build the mounted-media ASSET ID fixture: a project laid out exactly the way
a packaged bundle reaches the player, so id-based asset resolution can be
proven against a mounted archive on the desktop.

A stored APK (export.android.assets=stored) and a browser export's game.pak
both MOUNT their bulk media in place and materialise only what a reader opens
by path or discovers by walking a directory (PlayerBundle::isMountedMediaPath).
This fixture reproduces that split literally:

  <dir>/game.pak                              STORED zip
      game/pak_tex.png                        the texture - ONLY in the archive
  <dir>/project/project.orkproj               extracted (fopen, tinyxml2)
  <dir>/project/scenes/main.oscene            extracted (fopen, XMLArchive)
  <dir>/project/assets/pak_tex.png.orkmeta    extracted (fopen + directory walk)

There is deliberately NO loose assets/pak_tex.png: the sidecar is the only
trace of the asset in the filesystem, exactly as in a shipped bundle.

The scene's sprite references the texture by a STALE name ("renamed_away.png")
plus the asset id, so the sprite can only find its texture if the id resolves -
which needs the sidecar to have been read and registered. Without that, the
sprite keeps the stale name and the texture never loads.

Usage: make_pak_project_fixture.py <dir>

Stdlib only (python_stdlib_lint): zipfile builds the pak with ZIP_STORED so the
entries are read in place, and zlib+struct emit a tiny valid PNG.
"""
import os
import struct
import sys
import zipfile
import zlib

# the fixture's asset id: FIXED (not minted) so the player selfcheck can assert
# the exact value it expects with no side channel between the two
ASSET_ID = "a55e7100000000000000000000000001"

MANIFEST_XML = """<?xml version="1.0" encoding="UTF-8"?>
<OrkigeProject version="1">
    <Name>PakAssetId</Name>
    <MainScene>scenes/main.oscene</MainScene>
</OrkigeProject>
"""

# the sidecar the packaged bundle materialises: the id AND a <texture> import
# block whose non-default filter ("point") is read live at sprite creation -
# the second thing a mounted sidecar would silently lose
SIDECAR_XML = """<?xml version="1.0" encoding="UTF-8"?>
<orkmeta id="%s">
    <texture filter="point" wrap="wrap" maxSize="0" premultiply="false" generateMips="false" format="none" quality="normal"/>
</orkmeta>
""" % ASSET_ID

# a v7 scene: one object with a Transform and a Sprite whose `texture` AssetRef
# carries a STALE value plus the resolving id (the trailing String of an
# AssetRef field is its asset id - @see SceneSerializer::saveComponentProperties)
SCENE_XML = """<?xml version="1.0" encoding="UTF-8"?>
<XMLArchive Version="0">
    <String value="orkige.oscene"/>
    <int value="7"/>
    <unsigned_int value="1"/>
    <String value="PakIdSprite"/>
    <String value=""/>
    <bool value="1"/>
    <unsigned_int value="0"/>
    <String value=""/>
    <unsigned_int value="2"/>
    <String value="TransformComponent"/>
    <TransformComponent create="0">
        <unsigned_int value="0"/>
        <String value=""/>
        <unsigned_int value="3"/>
        <String value="position"/>
        <int value="5"/>
        <String value="0 0 0"/>
        <String value=""/>
        <String value="orientation"/>
        <int value="6"/>
        <String value="1 0 0 0"/>
        <String value=""/>
        <String value="scale"/>
        <int value="5"/>
        <String value="1 1 1"/>
        <String value=""/>
    </TransformComponent>
    <String value="SpriteComponent"/>
    <SpriteComponent create="0">
        <unsigned_int value="0"/>
        <String value=""/>
        <unsigned_int value="1"/>
        <String value="texture"/>
        <int value="8"/>
        <String value="renamed_away.png"/>
        <String value="%s"/>
    </SpriteComponent>
</XMLArchive>
""" % ASSET_ID


def solid_png(width, height, rgba):
    """A minimal valid RGBA8 PNG of one solid colour (rgba = 4 bytes)."""
    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xffffffff))
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = b"".join(b"\x00" + rgba * width for _ in range(height))
    idat = zlib.compress(raw, 9)
    return (sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat)
            + chunk(b"IEND", b""))


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: make_pak_project_fixture.py <dir>\n")
        return 2
    out_dir = argv[1]
    project_dir = os.path.join(out_dir, "project")
    os.makedirs(project_dir, exist_ok=True)

    write_text(os.path.join(project_dir, "project.orkproj"), MANIFEST_XML)
    write_text(os.path.join(project_dir, "scenes", "main.oscene"), SCENE_XML)
    write_text(os.path.join(project_dir, "assets", "pak_tex.png.orkmeta"),
               SIDECAR_XML)

    # the asset itself lives ONLY inside the archive - assert that, because
    # a stray loose copy would silently invalidate the whole proof
    loose = os.path.join(project_dir, "assets", "pak_tex.png")
    if os.path.exists(loose):
        os.remove(loose)

    pak_path = os.path.join(out_dir, "game.pak")
    with zipfile.ZipFile(pak_path, "w",
                         compression=zipfile.ZIP_STORED) as pak:
        pak.writestr("game/pak_tex.png", solid_png(8, 8, b"\x40\xa0\xf0\xff"))
    with zipfile.ZipFile(pak_path) as pak:
        for info in pak.infolist():
            if info.compress_type != zipfile.ZIP_STORED:
                sys.stderr.write("make_pak_project_fixture: %s is not STORED\n"
                                 % info.filename)
                return 1

    sys.stdout.write("make_pak_project_fixture: wrote %s (texture in the "
                     "archive, sidecar on disk)\n" % pak_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
