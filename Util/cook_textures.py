#!/usr/bin/env python3
"""Export-time texture cook (stdlib only) - the resize/premultiply/validate
step orkige_export.py runs over a staged project payload, per target platform.

The DEV loop stays raw: textures load straight from the project (dev iteration
must not wait on a cook). Only EXPORT conditions the shipped pixels, honoring
each texture's import settings (core_project/AssetDatabase.h) resolved for the
target platform:

  * maxSize   downscale so the longest side <= maxSize (area-averaged)
  * premultiply  fold alpha into RGB (for premultiplied-alpha blending)
  * (generateMips is carried in the sidecar but NOT acted on here - runtime
    mip generation is future work)

What this cook deliberately does NOT do: GPU compression (ETC2/ASTC/BCn). It is
double-blocked - the runtime registers only the PNG/JPG image codec, and the
Python stdlib has no block-compression encoder - so it stays out of v1 and is
its own future work package. The cook only ever rewrites UNCOMPRESSED PNGs.

Sampler settings (filter/wrap) are NOT cooked: they are honored LIVE at sprite
material/datablock creation from the same sidecar (which ships alongside the
texture), so the cook leaves them for the runtime.

    cook_textures.py <payload_dir> [platform]     # cook a staged payload
    cook_textures.py --selftest

`platform` is "", "ios" or "android" (default ""); orkige_export.py maps its
--platform onto that. The cook walks the payload for *.png that carry a sibling
"<name>.png.orkmeta" with a <texture> block and rewrites them in place; textures
without a sidecar or without a <texture> block are shipped untouched.
"""

import os
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_png  # noqa: E402  (sibling stdlib helper)

META_EXTENSION = ".orkmeta"


def _as_bool(text, fallback):
    if text is None:
        return fallback
    return text.strip().lower() in ("1", "true", "yes")


def resolve_import_settings(meta_path, platform):
    """the effective texture import settings from a sidecar, resolved for the
    platform token ("", "ios", "android"). Returns None when the sidecar is
    missing/invalid or carries no <texture> block (an id-only v1 sidecar)."""
    try:
        root = ET.parse(meta_path).getroot()
    except (ET.ParseError, OSError):
        return None
    if root.tag != "orkmeta":
        return None
    texture = root.find("texture")
    if texture is None:
        return None

    def read_block(element, base):
        settings = dict(base)
        if element.get("filter") is not None:
            settings["filter"] = element.get("filter")
        if element.get("wrap") is not None:
            settings["wrap"] = element.get("wrap")
        if element.get("maxSize") is not None:
            settings["maxSize"] = int(element.get("maxSize"))
        if element.get("premultiply") is not None:
            settings["premultiply"] = _as_bool(element.get("premultiply"),
                                                settings["premultiply"])
        if element.get("generateMips") is not None:
            settings["generateMips"] = _as_bool(element.get("generateMips"),
                                                 settings["generateMips"])
        return settings

    base = read_block(texture, {"filter": "bilinear", "wrap": "clamp",
                                "maxSize": 0, "premultiply": False,
                                "generateMips": False})
    override = texture.find(platform) if platform in ("ios", "android") \
        else None
    return read_block(override, base) if override is not None else base


def cook_texture(png_path, settings):
    """apply the resolved settings to a PNG in place; returns a short report
    string when it changed, else None"""
    image = orkige_png.decode_png(png_path)
    original = (image.width, image.height)
    target = orkige_png.fit_within(image.width, image.height,
                                   settings.get("maxSize", 0))
    changed = []
    if target != original:
        image = orkige_png.downscale(image, target[0], target[1])
        changed.append("resized %dx%d->%dx%d" % (original[0], original[1],
                                                 target[0], target[1]))
    if settings.get("premultiply"):
        orkige_png.premultiply(image)
        changed.append("premultiplied")
    if not changed:
        return None
    orkige_png.encode_png(image, png_path)
    return ", ".join(changed)


def cook_payload(payload_dir, platform="", log=None):
    """cook every sidecar-carrying *.png under payload_dir in place; returns
    the number of textures actually rewritten"""
    cooked = 0
    for parent, _dirs, files in os.walk(payload_dir):
        for name in sorted(files):
            if not name.lower().endswith(".png"):
                continue
            png_path = os.path.join(parent, name)
            meta_path = png_path + META_EXTENSION
            if not os.path.isfile(meta_path):
                continue
            settings = resolve_import_settings(meta_path, platform)
            if settings is None:
                continue
            report = cook_texture(png_path, settings)
            if report:
                cooked += 1
                if log:
                    log("cooked %s (%s)" % (name, report))
    return cooked


# --- self-test --------------------------------------------------------------

def _write_meta(path, body):
    with open(path, "w", newline="\n") as handle:
        handle.write(body)


def selftest():
    import tempfile
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    with tempfile.TemporaryDirectory() as tmp:
        # (1) a 64x64 texture with maxSize 16 + premultiply, default block
        big = orkige_png.Image(64, 64,
                               bytearray(bytes((200, 100, 50, 128)) * (64 * 64)))
        big_path = os.path.join(tmp, "big.png")
        orkige_png.encode_png(big, big_path)
        _write_meta(big_path + META_EXTENSION,
                    '<orkmeta id="a"><texture filter="point" wrap="clamp" '
                    'maxSize="16" premultiply="true" generateMips="false"/>'
                    '</orkmeta>')
        # (2) a per-platform override: android caps harder (8)
        plat = orkige_png.Image(64, 64,
                                bytearray(bytes((10, 20, 30, 255)) * (64 * 64)))
        plat_path = os.path.join(tmp, "plat.png")
        orkige_png.encode_png(plat, plat_path)
        _write_meta(plat_path + META_EXTENSION,
                    '<orkmeta id="b"><texture maxSize="32">'
                    '<android maxSize="8"/></texture></orkmeta>')
        # (3) an id-only v1 sidecar: must stay untouched
        raw = orkige_png.Image(20, 20,
                               bytearray(bytes((1, 2, 3, 255)) * (20 * 20)))
        raw_path = os.path.join(tmp, "raw.png")
        orkige_png.encode_png(raw, raw_path)
        _write_meta(raw_path + META_EXTENSION, '<orkmeta id="c"/>')
        # (4) no sidecar at all: untouched
        none = orkige_png.Image(20, 20,
                                bytearray(bytes((9, 9, 9, 255)) * (20 * 20)))
        none_path = os.path.join(tmp, "none.png")
        orkige_png.encode_png(none, none_path)

        # cook for the DEFAULT platform ("")
        cooked = cook_payload(tmp, "")
        check(cooked == 2, "expected 2 cooked textures, got %d" % cooked)
        check(orkige_png.png_size(big_path) == (16, 16),
              "big.png should have been downscaled to 16x16")
        # the shipped PNG must still decode (the runtime loads PNG only)
        decoded = orkige_png.decode_png(big_path)
        check((decoded.width, decoded.height) == (16, 16),
              "cooked big.png does not re-decode at 16x16")
        # premultiply: 200*128/255 = 100
        r, g, b, a = decoded.get(8, 8)
        check(a == 128 and r == 200 * 128 // 255,
              "big.png alpha was not premultiplied (got %r)" % ((r, g, b, a),))
        # plat.png at DEFAULT resolves to maxSize 32
        check(orkige_png.png_size(plat_path) == (32, 32),
              "plat.png default should be 32x32")
        check(orkige_png.png_size(raw_path) == (20, 20),
              "id-only sidecar texture must be untouched")
        check(orkige_png.png_size(none_path) == (20, 20),
              "sidecar-less texture must be untouched")

    # a fresh payload cooked for android must apply the 8px override
    with tempfile.TemporaryDirectory() as tmp:
        plat = orkige_png.Image(64, 64,
                                bytearray(bytes((10, 20, 30, 255)) * (64 * 64)))
        plat_path = os.path.join(tmp, "plat.png")
        orkige_png.encode_png(plat, plat_path)
        _write_meta(plat_path + META_EXTENSION,
                    '<orkmeta id="b"><texture maxSize="32">'
                    '<android maxSize="8"/></texture></orkmeta>')
        cook_payload(tmp, "android")
        if orkige_png.png_size(plat_path) != (8, 8):
            failures.append("android override should cap plat.png at 8x8")

    if failures:
        for message in failures:
            print("cook_textures: SELFTEST FAILED - " + message,
                  file=sys.stderr)
        sys.exit(1)
    print("cook_textures: self-test OK (resize + premultiply + per-platform "
          "override + untouched paths)")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        selftest()
        return
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    payload_dir = sys.argv[1]
    platform = sys.argv[2] if len(sys.argv) > 2 else ""
    cooked = cook_payload(payload_dir, platform,
                          log=lambda m: print("cook_textures: " + m))
    print("cook_textures: %d texture(s) cooked in %s (platform '%s')"
          % (cooked, payload_dir, platform))


if __name__ == "__main__":
    main()
