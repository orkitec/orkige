#!/usr/bin/env python3
"""Export-time texture cook (stdlib only) - the resize/premultiply/compress
step orkige_export.py runs over a staged project payload, per target platform.

The DEV loop stays raw: textures load straight from the project (dev iteration
must not wait on a cook). Only EXPORT conditions the shipped pixels, honoring
each texture's import settings (core_project/AssetDatabase.h) resolved for the
target platform:

  * maxSize      downscale so the longest side <= maxSize (area-averaged)
  * premultiply  fold alpha into RGB (for premultiplied-alpha blending)
  * format       GPU block compression - "auto" resolves the platform's best
                 format, "none" ships the PNG, an explicit token forces one
  * quality      encoder effort (and the ASTC block size under "auto")
  * generateMips bake an offline mip chain into the compressed container

Compression itself runs in the native `texcook` tool (tools/texcook, built in
every desktop engine tree): this cook stays stdlib-only, decodes/conditions the
PNG here, and shells the raw RGBA levels out to the encoder. A compressed
texture replaces its .png in the payload (ball.png -> ball.dds/.ktx/.oitd) and
its sidecar is renamed along with it, so the asset-id machinery resolves scene
references to the shipped name; the render backends also fall back from a
missing .png to its cooked siblings for bare-name references. A texture whose
sidecar is id-only cooks with the DEFAULT settings (format "auto"); a texture
without any sidecar ships untouched. Full model: Docs/textures.md.

CUBEMAPS ride the same cook: a sidecar-carrying .dds whose container marks it a
six-face cubemap (what Util/make_sky_assets.py bakes) block-compresses through
the SAME format matrix and encoder, preserving the six faces (order
+X,-X,+Y,-Y,+Z,-Z) and the BAKED mip chain exactly - a skybox's chain is the
prefiltered roughness chain the IBL samplers index, so it is re-encoded level by
level, never regenerated. The BC container reuses the .dds name (in place); the
mobile ASTC/ETC2 containers rename .dds -> .oitd/.ktx, and the skybox loaders
fall back from a missing .dds to those cooked siblings. A non-cubemap .dds (or
one already compressed) ships verbatim.

Sampler settings (filter/wrap) are NOT cooked: they are honored LIVE at sprite
material/datablock creation from the same sidecar (which ships alongside the
texture), so the cook leaves them for the runtime.

    cook_textures.py <payload_dir> [platform] [flavor] [texcook]
    cook_textures.py --selftest [texcook]

`platform` is "", "ios", "android" or "web" (default ""); `flavor` is the
render backend the export packages ("next"/"classic" - it picks the container
and the auto formats); `texcook` is the encoder binary (orkige_export.py
resolves it from the engine build tree, env ORKIGE_TEXCOOK overrides). A
payload that needs compression but has no encoder is REFUSED - never a
half-cooked export.
"""

import os
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_png  # noqa: E402  (sibling stdlib helper)

META_EXTENSION = ".orkmeta"

DEFAULT_SETTINGS = {"filter": "bilinear", "wrap": "clamp", "maxSize": 0,
                    "premultiply": False, "generateMips": False,
                    "format": "auto", "quality": "normal"}

# the sidecar's explicit format tokens ("etc2" is a FAMILY: the cook picks the
# RGB8 or RGBA8 variant per texture from its alpha channel)
EXPLICIT_FORMATS = ("none", "astc-4x4", "astc-6x6", "astc-8x8", "etc2",
                    "bc1", "bc3", "bc7")

# container per format family and flavor: BCn ships as .dds (both flavors
# register a DDS codec); ASTC/ETC2 ship in the Ogre-Next native .oitd on the
# next flavor and in KTX1 (.ktx - the classic compressed-texture codec's
# container) on classic
BC_FORMATS = ("bc1", "bc3", "bc7")


class CookError(Exception):
    """a texture cannot be cooked as requested - the export must refuse"""


def _as_bool(text, fallback):
    if text is None:
        return fallback
    return text.strip().lower() in ("1", "true", "yes")


def resolve_import_settings(meta_path, platform):
    """the effective texture import settings from a sidecar, resolved for the
    platform token ("", "ios", "android", "web"). An id-only sidecar resolves
    to the DEFAULT settings (every id-tracked texture ships with format
    "auto"); returns None only when the sidecar is missing/invalid - such a
    file has no import intent at all and ships untouched."""
    try:
        root = ET.parse(meta_path).getroot()
    except (ET.ParseError, OSError):
        return None
    if root.tag != "orkmeta":
        return None
    texture = root.find("texture")
    if texture is None:
        return dict(DEFAULT_SETTINGS)

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
        if element.get("format") is not None:
            settings["format"] = element.get("format")
        if element.get("quality") is not None:
            settings["quality"] = element.get("quality")
        return settings

    base = read_block(texture, DEFAULT_SETTINGS)
    override = texture.find(platform) \
        if platform in ("ios", "android", "web") else None
    return read_block(override, base) if override is not None else base


def has_alpha(image):
    """does any pixel carry an alpha below 255 (drives the etc2/BCn variant)"""
    return any(image.pixels[i] != 255
               for i in range(3, len(image.pixels), 4))


def resolve_format(settings, platform, flavor, alpha, warn=None):
    """resolve the sidecar's format/quality for one (platform, flavor) pair
    into a concrete encoder format, or None to ship the PNG.

    Returns (encoder_format, container). Raises CookError on an impossible
    explicit pair; calls warn(message) for permitted-but-lossy overrides (web,
    classic GLES2 mobile - support there is a per-device lottery).

    The auto table (see Docs/textures.md for the rationale):
      desktop+next     opaque bc1 (bc7 at quality high), alpha bc7
      desktop+classic  opaque bc1, alpha bc3 (the classic default GL renderer
                       has no BC7 support on every desktop it runs on)
      ios/android+next astc (quality high 4x4 / normal 6x6 / low 8x8) - every
                       Metal-capable iPhone AND every Vulkan-capable Android at
                       our API-28 arm64 floor decodes ASTC LDR; etc2 stays a
                       reachable explicit override
      ios/android+classic  none - ETC2/ASTC are NOT guaranteed in the classic
                       flavor's GLES2 context, so auto ships PNG there
      web              none - compressed-texture support in the browser is a
                       property of the visitor's GPU, none is guaranteed
    """
    fmt = settings.get("format", "auto")
    quality = settings.get("quality", "normal")
    if fmt == "none":
        return None, None
    if fmt == "auto":
        if platform == "web":
            return None, None
        if flavor == "classic" and platform in ("ios", "android"):
            return None, None
        if platform in ("ios", "android"):
            fmt = {"high": "astc-4x4", "low": "astc-8x8"}.get(quality,
                                                              "astc-6x6")
        else:  # desktop
            if flavor == "classic":
                fmt = "bc3" if alpha else "bc1"
            else:
                fmt = "bc7" if (alpha or quality == "high") else "bc1"
    elif fmt not in EXPLICIT_FORMATS:
        raise CookError("unknown texture format '%s'" % fmt)
    else:
        # an explicit format: validate the (platform, flavor) pair
        if fmt in BC_FORMATS and platform in ("ios", "android"):
            raise CookError("format '%s' cannot ship to '%s' - mobile GPUs "
                            "have no BCn support (use astc/etc2 or none)"
                            % (fmt, platform))
        if fmt not in BC_FORMATS and platform in ("", "macos"):
            raise CookError("format '%s' cannot ship on a desktop export - "
                            "the desktop runtimes load only the BC family "
                            "(classic desktop GL exposes neither ASTC nor "
                            "ETC2, and the next flavor's desktop renderer "
                            "maps them only in its mobile builds); use "
                            "bc1/bc3%s or none" % (fmt,
                            "" if flavor == "classic" else "/bc7"))
        if flavor == "classic" and platform in ("", "macos") and fmt == "bc7":
            raise CookError("format 'bc7' cannot ship on the classic "
                            "desktop flavor - its default GL renderer "
                            "has no BC7 support (use bc1/bc3)")
        if platform == "web" and warn:
            warn("WARNING: explicit format '%s' on the web build only loads "
                 "on visitor GPUs exposing the matching compressed-texture "
                 "extension - none is guaranteed in a browser" % fmt)
        if flavor == "classic" and platform in ("ios", "android") and warn:
            warn("WARNING: explicit format '%s' on the classic GLES2 mobile "
                 "flavor is unproven - ETC2/ASTC are GLES3-tier and may not "
                 "load in a GLES2 context on every device" % fmt)
    # the etc2 family resolves per texture: EAC alpha only when needed
    if fmt == "etc2":
        fmt = "etc2-rgba" if alpha else "etc2-rgb"
    container = "dds" if fmt in BC_FORMATS \
        else ("oitd" if flavor == "next" else "ktx")
    return fmt, container


def build_mip_levels(image, generate_mips):
    """the RGBA level chain the encoder consumes: the base image, then - when
    generate_mips - area-averaged downscales to 1x1, level i sized base>>i
    (min 1), matching the encoder's level layout exactly"""
    levels = [image]
    if not generate_mips:
        return levels
    level = 1
    while (image.width >> level) > 0 or (image.height >> level) > 0:
        target_w = max(1, image.width >> level)
        target_h = max(1, image.height >> level)
        levels.append(orkige_png.downscale(image, target_w, target_h))
        if target_w == 1 and target_h == 1:
            break
        level += 1
    return levels


def encode_compressed(png_path, image, fmt, container, quality, generate_mips,
                      texcook):
    """run the native encoder over the conditioned image and replace the
    payload .png (and its sidecar name) with the compressed container.
    Returns the new file path."""
    if not texcook or not os.path.isfile(texcook):
        raise CookError(
            "texture '%s' resolves to '%s' but no texcook encoder is "
            "available%s - build a desktop engine tree (the texcook target) "
            "or point ORKIGE_TEXCOOK at the binary; refusing a half-cooked "
            "export" % (os.path.basename(png_path), fmt,
                        " at '%s'" % texcook if texcook else ""))
    levels = build_mip_levels(image, generate_mips)
    out_path = os.path.splitext(png_path)[0] + "." + container
    raw = tempfile.NamedTemporaryFile(suffix=".rgba", delete=False)
    try:
        for level in levels:
            raw.write(level.pixels)
        raw.close()
        result = subprocess.run(
            [texcook, "--input", raw.name, "--output", out_path,
             "--width", str(image.width), "--height", str(image.height),
             "--levels", str(len(levels)), "--format", fmt,
             "--quality", quality, "--container", container],
            capture_output=True, text=True)
        if result.returncode != 0:
            raise CookError("texcook failed on '%s': %s"
                            % (os.path.basename(png_path),
                               result.stderr.strip() or "exit %d"
                               % result.returncode))
    finally:
        os.unlink(raw.name)
    os.unlink(png_path)
    # the sidecar travels with the texture (the documented keep-the-id rule):
    # the runtime's read-only scan then registers the COOKED name under the
    # same id, so id-carrying scene references resolve to it
    meta_path = png_path + META_EXTENSION
    if os.path.isfile(meta_path):
        os.replace(meta_path, out_path + META_EXTENSION)
    return out_path


def decode_dds_cubemap(dds_path):
    """decode an UNCOMPRESSED masked-32bpp cubemap .dds (what
    Util/make_sky_assets.py bakes) into its faces and BAKED mip chain. Returns
    (size, mips, faces) where faces is a list of 6 lists of RGBA bytearrays
    (one per mip level, face order +X,-X,+Y,-Y,+Z,-Z), or None when the file is
    not an uncompressed cubemap we can re-encode (already block-compressed, a
    DX10/fourCC container, a 2D texture, an odd bit layout) - such a .dds ships
    verbatim.

    The mip chain is READ, never regenerated: a sky cubemap's chain is the
    prefiltered roughness chain the IBL samplers index, so the cook preserves
    it exactly (each level re-encoded into the compressed container as-is)."""
    with open(dds_path, "rb") as handle:
        data = handle.read()
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    (header_size, _flags, height, width, _pitch, _depth, mips) = \
        struct.unpack_from("<7I", data, 4)
    if header_size != 124:
        return None
    (pf_size, pf_flags, _four_cc, bit_count, r_mask, g_mask, b_mask, a_mask) = \
        struct.unpack_from("<8I", data, 76)
    (_caps, caps2) = struct.unpack_from("<2I", data, 108)
    # a real six-face cubemap (CUBEMAP bit + all six face bits)
    if (caps2 & 0xFE00) != 0xFE00:
        return None
    # uncompressed 32bpp RGB(A) only: a fourCC/DX10 payload is already
    # compressed (or an unsupported layout) and re-cooks from nothing
    if (pf_flags & 0x4) or not (pf_flags & 0x40) or bit_count != 32:
        return None
    if width != height or mips < 1:
        return None

    def channel_shift(mask):
        # byte index of a 0xFF-aligned channel mask in a little-endian pixel
        for byte in range(4):
            if mask == (0xFF << (byte * 8)):
                return byte
        return None
    r_i, g_i, b_i = (channel_shift(r_mask), channel_shift(g_mask),
                     channel_shift(b_mask))
    a_i = channel_shift(a_mask) if (pf_flags & 0x1) else None
    if None in (r_i, g_i, b_i):
        return None

    def level_bytes(level):
        dim = max(1, width >> level)
        return dim * dim * 4
    face_stride = sum(level_bytes(level) for level in range(mips))
    body = data[4 + 124:]
    if len(body) < face_stride * 6:
        return None
    faces = []
    for face in range(6):
        levels = []
        offset = face * face_stride
        for level in range(mips):
            count = level_bytes(level)
            src = body[offset:offset + count]
            rgba = bytearray(count)
            for pixel in range(0, count, 4):
                rgba[pixel] = src[pixel + r_i]
                rgba[pixel + 1] = src[pixel + g_i]
                rgba[pixel + 2] = src[pixel + b_i]
                rgba[pixel + 3] = src[pixel + a_i] if a_i is not None else 255
            levels.append(rgba)
            offset += count
        faces.append(levels)
    return width, mips, faces


def _cube_has_alpha(faces):
    """any face texel with alpha below 255 (drives the etc2/BCn variant)"""
    return any(level[i] != 255 for face in faces for level in face
               for i in range(3, len(level), 4))


def cook_cubemap(dds_path, settings, platform="", flavor="next",
                 texcook=None, warn=None):
    """block-compress one cubemap .dds per its resolved format; returns a short
    report when it changed the file, else None. Only the `format`/`quality`
    settings apply - a cubemap's size and prefiltered mip chain are authored,
    so maxSize/premultiply/generateMips are ignored here."""
    decoded = decode_dds_cubemap(dds_path)
    if decoded is None:
        return None  # not an uncompressed cubemap we cook - ships verbatim
    size, mips, faces = decoded
    fmt, container = resolve_format(settings, platform, flavor,
                                    _cube_has_alpha(faces), warn=warn)
    if not fmt:
        return None  # auto/none on this platform ships the .dds verbatim
    if not texcook or not os.path.isfile(texcook):
        raise CookError(
            "cubemap '%s' resolves to '%s' but no texcook encoder is "
            "available - build a desktop engine tree (the texcook target) or "
            "point ORKIGE_TEXCOOK at the binary; refusing a half-cooked export"
            % (os.path.basename(dds_path), fmt))
    out_path = os.path.splitext(dds_path)[0] + "." + container
    raw = tempfile.NamedTemporaryFile(suffix=".rgba", delete=False)
    try:
        # FACE-major on disk: face 0's whole chain, then face 1's, ...
        for face in faces:
            for level in face:
                raw.write(level)
        raw.close()
        # write to a temp then move into place - the BC container reuses the
        # source's own .dds name, so a direct write could clobber the input
        tmp_out = out_path + ".cooking"
        result = subprocess.run(
            [texcook, "--input", raw.name, "--output", tmp_out,
             "--width", str(size), "--height", str(size),
             "--levels", str(mips), "--faces", "6", "--format", fmt,
             "--quality", settings.get("quality", "normal"),
             "--container", container],
            capture_output=True, text=True)
        if result.returncode != 0:
            if os.path.exists(tmp_out):
                os.unlink(tmp_out)
            raise CookError("texcook failed on '%s': %s"
                            % (os.path.basename(dds_path),
                               result.stderr.strip() or "exit %d"
                               % result.returncode))
        if out_path != dds_path:
            os.unlink(dds_path)
        os.replace(tmp_out, out_path)
    finally:
        os.unlink(raw.name)
    meta_path = dds_path + META_EXTENSION
    if os.path.isfile(meta_path) and out_path + META_EXTENSION != meta_path:
        os.replace(meta_path, out_path + META_EXTENSION)
    return "%s -> %s (cubemap, %d faces, %d mips)" \
        % (fmt, os.path.basename(out_path), 6, mips)


def cook_texture(png_path, settings, platform="", flavor="next",
                 texcook=None, warn=None):
    """apply the resolved settings to one payload PNG; returns a short report
    string when it changed anything, else None"""
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
    fmt, container = resolve_format(settings, platform, flavor,
                                    has_alpha(image), warn=warn)
    if fmt:
        out_path = encode_compressed(png_path, image, fmt, container,
                                     settings.get("quality", "normal"),
                                     settings.get("generateMips", False),
                                     texcook)
        changed.append("%s -> %s" % (fmt, os.path.basename(out_path)))
    elif changed:
        orkige_png.encode_png(image, png_path)
    if not changed:
        return None
    return ", ".join(changed)


def cook_payload(payload_dir, platform="", flavor="next", texcook=None,
                 log=None):
    """cook every sidecar-carrying *.png under payload_dir in place; returns
    the number of textures actually rewritten. Raises CookError when a
    texture cannot ship as its settings demand (the export must refuse)."""
    cooked = 0
    for parent, _dirs, files in os.walk(payload_dir):
        for name in sorted(files):
            lowered = name.lower()
            is_png = lowered.endswith(".png")
            is_dds = lowered.endswith(".dds")
            if not (is_png or is_dds):
                continue
            source_path = os.path.join(parent, name)
            meta_path = source_path + META_EXTENSION
            if not os.path.isfile(meta_path):
                continue
            settings = resolve_import_settings(meta_path, platform)
            if settings is None:
                continue
            # a .png is a 2D texture; a .dds is a cubemap when its container
            # says so (else it is final artwork and ships verbatim)
            if is_png:
                report = cook_texture(source_path, settings, platform, flavor,
                                      texcook, warn=log)
            else:
                report = cook_cubemap(source_path, settings, platform, flavor,
                                      texcook, warn=log)
            if report:
                cooked += 1
                if log:
                    log("cooked %s (%s)" % (name, report))
    return cooked


# --- self-test --------------------------------------------------------------

def _write_meta(path, body):
    with open(path, "w", newline="\n") as handle:
        handle.write(body)


def _resolution_selftest(check):
    """the pure format-resolution matrix (no encoder needed)"""
    auto = dict(DEFAULT_SETTINGS)

    def resolved(settings, platform, flavor, alpha):
        return resolve_format(settings, platform, flavor, alpha)[0:2]

    # the auto table
    check(resolved(auto, "web", "classic", True) == (None, None),
          "web auto must ship PNG")
    check(resolved(auto, "ios", "classic", True) == (None, None),
          "classic GLES2 mobile auto must ship PNG")
    check(resolved(auto, "android", "classic", False) == (None, None),
          "classic GLES2 mobile auto must ship PNG (android)")
    check(resolved(auto, "ios", "next", True) == ("astc-6x6", "oitd"),
          "ios next auto should be astc-6x6/oitd at normal quality")
    high = dict(auto, quality="high")
    check(resolved(high, "ios", "next", True) == ("astc-4x4", "oitd"),
          "ios quality high should pick astc-4x4")
    low = dict(auto, quality="low")
    check(resolved(low, "ios", "next", False) == ("astc-8x8", "oitd"),
          "ios quality low should pick astc-8x8")
    check(resolved(auto, "android", "next", False) == ("astc-6x6", "oitd"),
          "android auto should be astc-6x6/oitd at normal quality")
    check(resolved(high, "android", "next", True) == ("astc-4x4", "oitd"),
          "android quality high should pick astc-4x4")
    check(resolved(low, "android", "next", False) == ("astc-8x8", "oitd"),
          "android quality low should pick astc-8x8")
    # etc2 stays reachable as an EXPLICIT override on android
    check(resolved(dict(auto, format="etc2"), "android", "next", False)
          == ("etc2-rgb", "oitd"),
          "explicit etc2 on android should still resolve etc2-rgb")
    check(resolved(dict(auto, format="etc2"), "android", "next", True)
          == ("etc2-rgba", "oitd"),
          "explicit etc2 alpha on android should resolve etc2-rgba")
    check(resolved(auto, "", "next", False) == ("bc1", "dds"),
          "desktop next opaque should be bc1")
    check(resolved(auto, "", "next", True) == ("bc7", "dds"),
          "desktop next alpha should be bc7")
    check(resolved(high, "", "next", False) == ("bc7", "dds"),
          "desktop next opaque at high quality should be bc7")
    check(resolved(auto, "", "classic", True) == ("bc3", "dds"),
          "desktop classic alpha should be bc3 (no BC7 on classic GL)")
    check(resolved(auto, "", "classic", False) == ("bc1", "dds"),
          "desktop classic opaque should be bc1")
    # explicit formats win, per family
    explicit = dict(auto, format="astc-8x8")
    check(resolved(explicit, "ios", "next", True) == ("astc-8x8", "oitd"),
          "an explicit format must win over the auto pick")
    etc2 = dict(auto, format="etc2")
    check(resolved(etc2, "android", "next", True) == ("etc2-rgba", "oitd"),
          "the etc2 family must resolve its alpha variant")
    check(resolved(etc2, "ios", "classic", False)[0] == "etc2-rgb" and
          resolved(etc2, "ios", "classic", False)[1] == "ktx",
          "an explicit etc2 on classic mobile ships KTX1 (warned)")
    # the classic container for astc/etc2 is KTX1
    astc = dict(auto, format="astc-4x4")
    warned = []
    fmt, container = resolve_format(astc, "web", "classic", True,
                                    warn=warned.append)
    check((fmt, container) == ("astc-4x4", "ktx") and warned,
          "an explicit web override is permitted but must warn loudly")
    # impossible pairs refuse
    for settings, platform, flavor in (
            (dict(auto, format="bc7"), "ios", "next"),
            (dict(auto, format="bc1"), "android", "next"),
            (dict(auto, format="bc7"), "", "classic"),
            (dict(auto, format="astc-4x4"), "", "classic"),
            (dict(auto, format="astc-4x4"), "", "next"),
            (dict(auto, format="etc2"), "", "next"),
            (dict(auto, format="nonsense"), "", "next")):
        try:
            resolve_format(settings, platform, flavor, True)
            check(False, "format '%s' on %s/%s must refuse"
                  % (settings["format"], platform or "desktop", flavor))
        except CookError:
            pass


def selftest(texcook=None):
    import tempfile as tf
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    _resolution_selftest(check)

    with tf.TemporaryDirectory() as tmp:
        # (1) a 64x64 texture with maxSize 16 + premultiply + format none:
        # the uncompressed conditioning path stays byte-for-byte what it was
        big = orkige_png.Image(64, 64,
                               bytearray(bytes((200, 100, 50, 128)) * (64 * 64)))
        big_path = os.path.join(tmp, "big.png")
        orkige_png.encode_png(big, big_path)
        _write_meta(big_path + META_EXTENSION,
                    '<orkmeta id="a"><texture filter="point" wrap="clamp" '
                    'maxSize="16" premultiply="true" generateMips="false" '
                    'format="none"/></orkmeta>')
        # (2) a per-platform override: android caps harder (8), web opts out
        # of the base cap - the web slot resolves like the mobile ones
        plat = orkige_png.Image(64, 64,
                                bytearray(bytes((10, 20, 30, 255)) * (64 * 64)))
        plat_path = os.path.join(tmp, "plat.png")
        orkige_png.encode_png(plat, plat_path)
        _write_meta(plat_path + META_EXTENSION,
                    '<orkmeta id="b"><texture maxSize="32" format="none">'
                    '<android maxSize="8"/><web maxSize="0"/>'
                    '</texture></orkmeta>')
        # (3) no sidecar at all: untouched (no import intent)
        none = orkige_png.Image(20, 20,
                                bytearray(bytes((9, 9, 9, 255)) * (20 * 20)))
        none_path = os.path.join(tmp, "none.png")
        orkige_png.encode_png(none, none_path)

        # cook for the DEFAULT platform ("")
        cooked = cook_payload(tmp, "")
        check(cooked == 2, "expected 2 cooked textures, got %d" % cooked)
        check(orkige_png.png_size(big_path) == (16, 16),
              "big.png should have been downscaled to 16x16")
        decoded = orkige_png.decode_png(big_path)
        r, g, b, a = decoded.get(8, 8)
        check(a == 128 and r == 200 * 128 // 255,
              "big.png alpha was not premultiplied (got %r)" % ((r, g, b, a),))
        check(orkige_png.png_size(plat_path) == (32, 32),
              "plat.png default should be 32x32")
        check(orkige_png.png_size(none_path) == (20, 20),
              "sidecar-less texture must be untouched")

    # a fresh payload cooked for android/web applies each slot's override
    for platform, expected in (("android", (8, 8)), ("web", (64, 64))):
        with tf.TemporaryDirectory() as tmp:
            plat = orkige_png.Image(64, 64,
                                    bytearray(bytes((10, 20, 30, 255)) * (64 * 64)))
            plat_path = os.path.join(tmp, "plat.png")
            orkige_png.encode_png(plat, plat_path)
            _write_meta(plat_path + META_EXTENSION,
                        '<orkmeta id="b"><texture maxSize="32" format="none">'
                        '<android maxSize="8"/><web maxSize="0"/>'
                        '</texture></orkmeta>')
            cook_payload(tmp, platform)
            if orkige_png.png_size(plat_path) != expected:
                failures.append("%s override should size plat.png %r"
                                % (platform, expected))

    # refusal without an encoder: an id-only sidecar resolves to the default
    # (format auto -> compressed on desktop) and must refuse, not half-cook
    with tf.TemporaryDirectory() as tmp:
        raw = orkige_png.Image(20, 20,
                               bytearray(bytes((1, 2, 3, 255)) * (20 * 20)))
        raw_path = os.path.join(tmp, "raw.png")
        orkige_png.encode_png(raw, raw_path)
        _write_meta(raw_path + META_EXTENSION, '<orkmeta id="c"/>')
        try:
            cook_payload(tmp, "", "next", texcook=None)
            failures.append("compression without an encoder must refuse")
        except CookError:
            pass
        check(os.path.isfile(raw_path),
              "the refused payload must keep its PNG")
        # the same payload for web ships PNG - no encoder needed
        cooked = cook_payload(tmp, "web", "classic", texcook=None)
        check(cooked == 0 and orkige_png.png_size(raw_path) == (20, 20),
              "web auto must ship the untouched PNG")

    # a cubemap with an auto sidecar also needs the encoder on desktop (refuse,
    # never ship a half-cooked cube), but ships verbatim where auto is PNG
    with tf.TemporaryDirectory() as tmp:
        import make_sky_assets  # sibling Util helper (the cube .dds baker)
        cube_path = os.path.join(tmp, "sky.dds")
        with open(cube_path, "wb") as handle:
            handle.write(make_sky_assets.build_faces(8))
        _write_meta(cube_path + META_EXTENSION, '<orkmeta id="j"/>')
        before = os.path.getsize(cube_path)
        try:
            cook_payload(tmp, "", "next", texcook=None)
            failures.append("cube compression without an encoder must refuse")
        except CookError:
            pass
        check(os.path.isfile(cube_path),
              "the refused cube payload must keep its .dds")
        cooked = cook_payload(tmp, "web", "classic", texcook=None)
        check(cooked == 0 and os.path.getsize(cube_path) == before,
              "web auto must ship the cubemap .dds verbatim")

    # the encoder legs (run whenever a texcook binary is available - the
    # ctest registration always passes the tree's own)
    if texcook and os.path.isfile(texcook):
        with tf.TemporaryDirectory() as tmp:
            # alpha texture on desktop next: bc7 dds + renamed sidecar + mips
            spr = orkige_png.Image(20, 12)
            for y in range(12):
                for x in range(20):
                    spr.put(x, y, (x * 12, y * 20, 128, 255 - x))
            spr_path = os.path.join(tmp, "spr.png")
            orkige_png.encode_png(spr, spr_path)
            _write_meta(spr_path + META_EXTENSION,
                        '<orkmeta id="d"><texture generateMips="true" '
                        'quality="low"/></orkmeta>')
            cooked = cook_payload(tmp, "", "next", texcook=texcook)
            dds_path = os.path.join(tmp, "spr.dds")
            check(cooked == 1 and os.path.isfile(dds_path) and
                  not os.path.exists(spr_path),
                  "desktop next should replace spr.png with spr.dds")
            check(os.path.isfile(dds_path + META_EXTENSION) and
                  not os.path.exists(spr_path + META_EXTENSION),
                  "the sidecar must be renamed with the texture")
            with open(dds_path, "rb") as handle:
                header = handle.read(128)
            check(header[:4] == b"DDS ", "spr.dds must carry the DDS magic")
            mip_count = struct.unpack_from("<I", header, 28)[0]
            check(mip_count == 5, "20x12 with generateMips should bake 5 "
                  "levels, got %d" % mip_count)
        with tf.TemporaryDirectory() as tmp:
            # android next auto: ASTC in .oitd (the modern-Android default)
            tile = orkige_png.Image(16, 16,
                                    bytearray(bytes((60, 120, 60, 255)) * 256))
            tile_path = os.path.join(tmp, "tile.png")
            orkige_png.encode_png(tile, tile_path)
            _write_meta(tile_path + META_EXTENSION, '<orkmeta id="e"/>')
            cook_payload(tmp, "android", "next", texcook=texcook)
            oitd_path = os.path.join(tmp, "tile.oitd")
            check(os.path.isfile(oitd_path), "android next should emit .oitd")
            with open(oitd_path, "rb") as handle:
                head = handle.read(21)
            check(head[:4] == b"OITD", "tile.oitd must carry the OITD magic")
            # the .oitd PixelFormatGpu (offset 4 + 14, LE u16) must be an ASTC
            # value (astc-4x4/6x6/8x8 = 126/130/133 in texcook's format table)
            pixel_format = head[4 + 14] | (head[4 + 15] << 8)
            check(pixel_format in (126, 130, 133),
                  "android auto should now encode ASTC (got PixelFormatGpu %d)"
                  % pixel_format)
        with tf.TemporaryDirectory() as tmp:
            # android next EXPLICIT etc2: still reachable, still .oitd
            tile = orkige_png.Image(16, 16,
                                    bytearray(bytes((60, 120, 60, 255)) * 256))
            tile_path = os.path.join(tmp, "tile.png")
            orkige_png.encode_png(tile, tile_path)
            _write_meta(tile_path + META_EXTENSION,
                        '<orkmeta id="e2"><texture format="etc2"/></orkmeta>')
            cook_payload(tmp, "android", "next", texcook=texcook)
            oitd_path = os.path.join(tmp, "tile.oitd")
            check(os.path.isfile(oitd_path),
                  "explicit etc2 on android next should still emit .oitd")
            with open(oitd_path, "rb") as handle:
                head = handle.read(21)
            pixel_format = head[4 + 14] | (head[4 + 15] << 8)
            check(pixel_format in (113, 115),
                  "explicit etc2 should encode an ETC2 PixelFormatGpu "
                  "(got %d)" % pixel_format)
        with tf.TemporaryDirectory() as tmp:
            # explicit etc2 on classic mobile: permitted, warned, KTX1
            tile = orkige_png.Image(16, 16,
                                    bytearray(bytes((60, 120, 60, 255)) * 256))
            tile_path = os.path.join(tmp, "tile.png")
            orkige_png.encode_png(tile, tile_path)
            _write_meta(tile_path + META_EXTENSION,
                        '<orkmeta id="f"><texture format="etc2" '
                        'quality="low"/></orkmeta>')
            warnings = []
            cook_payload(tmp, "android", "classic", texcook=texcook,
                         log=warnings.append)
            ktx_path = os.path.join(tmp, "tile.ktx")
            check(os.path.isfile(ktx_path),
                  "explicit etc2 on classic should emit .ktx")
            check(any("WARNING" in message for message in warnings),
                  "the classic GLES2 override must warn loudly")
            with open(ktx_path, "rb") as handle:
                magic = handle.read(7)
            check(magic[1:7] == b"KTX 11", "tile.ktx must be a KTX1 file")
        # cubemap legs: a generated uncompressed six-face cube .dds (the sky
        # baker's container) block-compresses through the same matrix, keeping
        # all six faces + the baked mip chain
        import make_sky_assets  # sibling Util helper (the cube .dds baker)
        cube_dds = make_sky_assets.build_faces(8)   # 8px faces, 4 baked mips
        src_mips = make_sky_assets._mip_count(8)
        with tf.TemporaryDirectory() as tmp:
            # desktop next auto: opaque -> bc1 in the SAME .dds name, in place
            cube_path = os.path.join(tmp, "sky.dds")
            with open(cube_path, "wb") as handle:
                handle.write(cube_dds)
            _write_meta(cube_path + META_EXTENSION, '<orkmeta id="g"/>')
            cooked = cook_payload(tmp, "", "next", texcook=texcook)
            check(cooked == 1 and os.path.isfile(cube_path),
                  "desktop cube should cook in place to bc1 .dds")
            with open(cube_path, "rb") as handle:
                header = handle.read(128)
            check(header[:4] == b"DDS ", "cooked cube must carry DDS magic")
            (_hs, _fl, _h, _w, _p, _d, cube_mips) = \
                struct.unpack_from("<7I", header, 4)
            check(cube_mips == src_mips,
                  "the baked mip chain must be preserved (%d vs %d)"
                  % (cube_mips, src_mips))
            caps2 = struct.unpack_from("<I", header, 4 + 108)[0]
            check((caps2 & 0xFE00) == 0xFE00,
                  "the cooked cube must keep the cubemap caps")
            four_cc = header[84:88]
            check(four_cc == b"DXT1", "opaque desktop cube should be bc1/DXT1")
        with tf.TemporaryDirectory() as tmp:
            # android next auto: astc -> .oitd cube (TypeCube), .dds removed,
            # sidecar renamed along
            cube_path = os.path.join(tmp, "sky.dds")
            with open(cube_path, "wb") as handle:
                handle.write(cube_dds)
            _write_meta(cube_path + META_EXTENSION, '<orkmeta id="h"/>')
            cook_payload(tmp, "android", "next", texcook=texcook)
            oitd_path = os.path.join(tmp, "sky.oitd")
            check(os.path.isfile(oitd_path) and not os.path.exists(cube_path),
                  "android cube should rename .dds -> .oitd")
            check(os.path.isfile(oitd_path + META_EXTENSION) and
                  not os.path.exists(cube_path + META_EXTENSION),
                  "the cube sidecar must rename along with the container")
            with open(oitd_path, "rb") as handle:
                head = handle.read(21)
            check(head[:4] == b"OITD" and head[4 + 13] == 5,
                  "the cooked cube .oitd must be a TypeCube container")
        with tf.TemporaryDirectory() as tmp:
            # a non-cubemap .dds ships verbatim (final artwork, not cooked)
            flat = struct.pack("<4s7I44x8I2I12x", b"DDS ", 124, 0x0002100F,
                               4, 4, 16, 0, 1, 32, 0x41, 0, 32, 0x00FF0000,
                               0x0000FF00, 0x000000FF, 0xFF000000, 0x1000, 0)
            flat += bytes(4 * 4 * 4)
            flat_path = os.path.join(tmp, "flat.dds")
            with open(flat_path, "wb") as handle:
                handle.write(flat)
            _write_meta(flat_path + META_EXTENSION, '<orkmeta id="i"/>')
            before = os.path.getsize(flat_path)
            cooked = cook_payload(tmp, "", "next", texcook=texcook)
            check(cooked == 0 and os.path.getsize(flat_path) == before,
                  "a non-cubemap .dds must ship verbatim")
    else:
        print("cook_textures: (encoder legs skipped - no texcook binary "
              "given; pass one or set ORKIGE_TEXCOOK)")

    if failures:
        for message in failures:
            print("cook_textures: SELFTEST FAILED - " + message,
                  file=sys.stderr)
        sys.exit(1)
    print("cook_textures: self-test OK (resolution matrix + resize + "
          "premultiply + per-platform/web overrides + cubemap ship/refuse + "
          "refusals%s)" % ("" if not texcook else " + encoder legs incl. "
                           "cubemap round-trip"))


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        selftest(sys.argv[2] if len(sys.argv) > 2
                 else os.environ.get("ORKIGE_TEXCOOK"))
        return
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    payload_dir = sys.argv[1]
    platform = sys.argv[2] if len(sys.argv) > 2 else ""
    flavor = sys.argv[3] if len(sys.argv) > 3 else "next"
    texcook = sys.argv[4] if len(sys.argv) > 4 \
        else os.environ.get("ORKIGE_TEXCOOK")
    try:
        cooked = cook_payload(payload_dir, platform, flavor, texcook,
                              log=lambda m: print("cook_textures: " + m))
    except CookError as error:
        print("cook_textures: ERROR - %s" % error, file=sys.stderr)
        sys.exit(1)
    print("cook_textures: %d texture(s) cooked in %s (platform '%s', "
          "flavor '%s')" % (cooked, payload_dir, platform, flavor))


if __name__ == "__main__":
    main()
