#!/usr/bin/env python3
"""Build the pak-mount test fixture: a STORED zip whose "game/" sub-tree holds
a v7 scene, a texture and an OGG, all resolvable through the resource system
exactly like loose files. This mirrors what an Android APK carries under
"assets/" in the `stored` (mount-in-place) mode - the desktop reborn-BigZip
acceptance fixture (Docs/filesystem.md).

Usage: make_pak_fixture.py <out.pak> <blip.ogg>

Stdlib only (python_stdlib_lint): zipfile builds the pak with ZIP_STORED so the
entries are read in place / seekable, and zlib+struct emit a tiny valid PNG.
"""
import struct
import sys
import zipfile
import zlib


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


# a v7 scene (SceneSerializer format): one object with a TransformComponent and
# a SpriteComponent whose texture resolves from the pak (pak_tex.png). The
# player reads THIS through the resource system and parses it in memory - no
# fopen against a zip entry.
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


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: make_pak_fixture.py <out.pak> <blip.ogg>\n")
        return 2
    out_pak, ogg_path = argv[1], argv[2]
    with open(ogg_path, "rb") as handle:
        ogg_bytes = handle.read()
    texture = solid_png(8, 8, b"\x40\xa0\xf0\xff")

    # ZIP_STORED: the entries stay uncompressed so they are read in place and
    # the OGG is randomly seekable - the same choice the APK `stored` mode makes
    with zipfile.ZipFile(out_pak, "w", compression=zipfile.ZIP_STORED) as pak:
        pak.writestr("game/pak.oscene", SCENE_XML)
        pak.writestr("game/pak_tex.png", texture)
        pak.writestr("game/music.ogg", ogg_bytes)

    # verify the entries really are STORED (the contract the Android structure
    # test also asserts, here proven for the desktop fixture)
    with zipfile.ZipFile(out_pak) as pak:
        for info in pak.infolist():
            if info.compress_type != zipfile.ZIP_STORED:
                sys.stderr.write("make_pak_fixture: %s is not STORED\n"
                                 % info.filename)
                return 1
    sys.stdout.write("make_pak_fixture: wrote %s (3 STORED entries)\n" % out_pak)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
