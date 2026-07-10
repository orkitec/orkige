#!/usr/bin/env python3
"""App-icon generation for project export (stdlib only, reuses orkige_png).

Turns a single square source PNG (the project's export.icon, or the checked-in
engine default) into the per-platform icon sets the exporters need:

  macOS    a .iconset directory (10 icon_NxN[@2x].png) for `iconutil -c icns`
  iOS      the loose CFBundleIconFiles PNGs the simulator honours at the bundle
           root (no asset catalog / actool - that would need non-stdlib tools)
  Android  res/mipmap-<density>/ic_launcher.png at the five legacy densities

Every resize is an area-average downscale (orkige_png.downscale) from the source,
so a ~1024px source yields clean icons at every size. Imported by
Util/orkige_export.py as a sibling module.

Usage:  orkige_icons.py --selftest    synth a source, run all three generators
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_png  # noqa: E402  (sibling Util helper)

# the checked-in neutral engine icon used when a project sets no export.icon
DEFAULT_ICON = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "media", "orkige_default_icon.png")

# macOS .iconset entry -> pixel size (iconutil's expected file names), the same
# table make_editor_icon.py emits
MACOS_ICONSET_ENTRIES = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
]

# loose iOS icon file -> pixel size: iPhone app icon @2x/@3x plus the iPad @2x.
# The simulator picks by scale from CFBundleIconFiles; no asset catalog needed.
IOS_ICON_ENTRIES = [
    ("AppIcon60x60@2x.png", 120),
    ("AppIcon60x60@3x.png", 180),
    ("AppIcon76x76@2x.png", 152),
]

# Android legacy PNG mipmap density -> pixel size (adaptive icons, which need a
# vector foreground/background XML pair, are deferred - see Docs/ports.md)
ANDROID_MIPMAP_DENSITIES = [
    ("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
    ("xxhdpi", 144), ("xxxhdpi", 192),
]


def load_square_source(path):
    """decode an icon source PNG into a square orkige_png.Image, centre-cropping
    a non-square source to its largest centred square. Raises on a missing or
    too-small (< 64px) source."""
    if not os.path.isfile(path):
        raise ValueError("icon source '%s' does not exist" % path)
    image = orkige_png.decode_png(path)
    side = min(image.width, image.height)
    if side < 64:
        raise ValueError("icon source '%s' is too small (%dx%d, need >= 64px)"
                         % (path, image.width, image.height))
    if image.width == image.height:
        return image
    # centre-crop to a square
    ox = (image.width - side) // 2
    oy = (image.height - side) // 2
    cropped = orkige_png.Image(side, side)
    for y in range(side):
        src = ((oy + y) * image.width + ox) * 4
        dst = y * side * 4
        cropped.pixels[dst:dst + side * 4] = image.pixels[src:src + side * 4]
    return cropped


def resolve_icon_source(project, log=None):
    """the abspath of the icon source for a project: export.icon when set AND
    present, else the checked-in engine default. A set-but-missing export.icon
    warns and falls back (never fails - an app should still ship an icon)."""
    def emit(message):
        if log:
            log(message)
    relative = project.settings.get("export.icon", "").strip()
    if relative:
        candidate = os.path.join(project.root, relative)
        if os.path.isfile(candidate):
            emit("icon: %s" % relative)
            return os.path.abspath(candidate)
        emit("WARNING: export.icon references '%s' but no such file exists - "
             "using the engine default icon" % relative)
    else:
        emit("icon: engine default (set export.icon to override)")
    return DEFAULT_ICON


def _write_sizes(source, out_dir, entries):
    """write downscaled copies of source into out_dir per (filename, size)."""
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for filename, size in entries:
        orkige_png.encode_png(orkige_png.downscale(source, size, size),
                              os.path.join(out_dir, filename))
        written.append(filename)
    return written


def make_macos_iconset(source, iconset_dir):
    """write the 10 icon_NxN[@2x].png entries iconutil expects."""
    return _write_sizes(source, iconset_dir, MACOS_ICONSET_ENTRIES)


def make_ios_icons(source, out_dir):
    """write the loose iOS icon PNGs; returns the filenames written (the caller
    lists them, sans .png, in CFBundleIconFiles)."""
    return _write_sizes(source, out_dir, IOS_ICON_ENTRIES)


def make_android_mipmaps(source, res_dir):
    """write res/mipmap-<density>/ic_launcher.png at the five legacy densities."""
    written = []
    for density, size in ANDROID_MIPMAP_DENSITIES:
        density_dir = os.path.join(res_dir, "mipmap-" + density)
        os.makedirs(density_dir, exist_ok=True)
        target = os.path.join(density_dir, "ic_launcher.png")
        orkige_png.encode_png(orkige_png.downscale(source, size, size), target)
        written.append(target)
    return written


def _synth_source(side):
    """a deterministic non-uniform square source (a diagonal colour ramp) so
    the selftest exercises real downscaling."""
    image = orkige_png.Image(side, side)
    for y in range(side):
        for x in range(side):
            image.put(x, y, (x * 255 // side, y * 255 // side,
                             (x + y) * 255 // (2 * side), 255))
    return image


def selftest():
    import tempfile
    source = _synth_source(256)
    with tempfile.TemporaryDirectory() as work:
        iconset = os.path.join(work, "App.iconset")
        make_macos_iconset(source, iconset)
        for filename, size in MACOS_ICONSET_ENTRIES:
            path = os.path.join(iconset, filename)
            assert os.path.isfile(path), "missing iconset entry " + filename
            assert orkige_png.png_size(path) == (size, size), \
                "iconset %s wrong size" % filename

        ios_dir = os.path.join(work, "ios")
        names = make_ios_icons(source, ios_dir)
        assert names == [e[0] for e in IOS_ICON_ENTRIES], "iOS icon name set"
        for filename, size in IOS_ICON_ENTRIES:
            assert orkige_png.png_size(os.path.join(ios_dir, filename)) == \
                (size, size), "iOS %s wrong size" % filename

        res_dir = os.path.join(work, "res")
        make_android_mipmaps(source, res_dir)
        for density, size in ANDROID_MIPMAP_DENSITIES:
            path = os.path.join(res_dir, "mipmap-" + density, "ic_launcher.png")
            assert os.path.isfile(path), "missing mipmap " + density
            assert orkige_png.png_size(path) == (size, size), \
                "mipmap %s wrong size" % density

        # a non-square source centre-crops to square
        wide = orkige_png.Image(200, 100)
        wide_path = os.path.join(work, "wide.png")
        orkige_png.encode_png(wide, wide_path)
        cropped = load_square_source(wide_path)
        assert cropped.width == cropped.height == 100, "centre-crop to square"

        # the checked-in default icon decodes and is square (present in-tree)
        if os.path.isfile(DEFAULT_ICON):
            default = load_square_source(DEFAULT_ICON)
            assert default.width == default.height, "default icon is square"
    print("orkige_icons: selftest OK")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        selftest()
    else:
        sys.exit("usage: orkige_icons.py --selftest")
