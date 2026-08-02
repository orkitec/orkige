#!/usr/bin/env python3
"""Build the mounted-media SAMPLER fixture: a project laid out exactly the way
a packaged bundle reaches the player, so the baked texture-sampler answer can be
proven against a mounted archive on the desktop.

A stored APK (export.android.assets=stored) and a browser export's game.pak
both MOUNT their bulk media in place and materialise only what a reader opens by
path (PlayerBundle::isMountedMediaPath). This fixture reproduces that split
literally, INCLUDING what a packaged payload does not carry - no `.orkmeta`
sidecars, because they are editor bookkeeping the export strips:

  <dir>/game.pak                              STORED zip
      game/pak_tex.png                        the texture - ONLY in the archive
  <dir>/project/project.orkproj               extracted (fopen, tinyxml2)
  <dir>/project/scenes/main.oscene            extracted (fopen, XMLArchive)

The manifest carries the baked <TextureSamplers> block an export writes: the
texture is authored `point`/`wrap`, a NON-default sampler no miss can pass off
as the default. Without the bake the sprite would render bilinear/clamp with
nothing on disk to recover the intent from.

Usage: make_pak_project_fixture.py <dir>

Stdlib only (python_stdlib_lint): zipfile builds the pak with ZIP_STORED so the
entries are read in place, and zlib+struct emit a tiny valid PNG.
"""
import os
import struct
import sys
import zipfile
import zlib

MANIFEST_XML = """<?xml version="1.0" encoding="UTF-8"?>
<OrkigeProject version="1">
    <Name>PakSampler</Name>
    <MainScene>scenes/main.oscene</MainScene>
    <TextureSamplers>
        <Sampler texture="pak_tex" filter="point" wrap="wrap"/>
    </TextureSamplers>
</OrkigeProject>
"""

# a v7 scene: one object with a Transform and a Sprite naming the texture that
# lives only inside the pak (the trailing String of an AssetRef field is its
# asset id - a packaged payload has none, @see
# SceneSerializer::saveComponentProperties)
SCENE_XML = """<?xml version="1.0" encoding="UTF-8"?>
<XMLArchive Version="0">
    <String value="orkige.oscene"/>
    <int value="7"/>
    <unsigned_int value="1"/>
    <String value="PakSprite"/>
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
        <String value="pak_tex.png"/>
        <String value=""/>
    </SpriteComponent>
</XMLArchive>
"""


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

    # the texture lives ONLY inside the archive, and no sidecar is left behind -
    # a stray copy of either would silently invalidate the whole proof
    for stale in ("pak_tex.png", "pak_tex.png.orkmeta"):
        path = os.path.join(project_dir, "assets", stale)
        if os.path.exists(path):
            os.remove(path)
    os.makedirs(os.path.join(project_dir, "assets"), exist_ok=True)

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
                     "archive, sampler baked into the manifest)\n" % pak_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
