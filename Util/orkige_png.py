#!/usr/bin/env python3
"""Minimal stdlib-only PNG decode/encode + image ops for Orkige's asset
pipeline (make_sprite_atlas.py, cook_textures.py). No third-party imaging - the
same zlib/struct precedent make_fastgui_atlas.py established, extended with a
DECODER (fastgui only ever encoded).

Scope is deliberately narrow: 8-bit-per-channel PNGs (grayscale, grayscale+
alpha, RGB, RGBA and 8-bit palette, with tRNS), non-interlaced. That covers the
PNG/JPG-only runtime's texture inputs. GPU-compressed formats (ETC2/ASTC/BCn)
are intentionally out of scope and double-blocked: the runtime registers only
the STBI PNG/JPG image codec, AND there is no block-compression encoder in the
Python stdlib - so a compression cook would produce assets nothing can load.
Compression is its own separate future effort (encoder decision + a loader codec).
"""

import struct
import zlib


class Image:
    """an 8-bit RGBA raster (row-major, top-left origin), pixels as a
    bytearray of width*height*4 bytes"""

    def __init__(self, width, height, pixels=None):
        self.width = width
        self.height = height
        self.pixels = pixels if pixels is not None else \
            bytearray(width * height * 4)

    def get(self, x, y):
        i = (y * self.width + x) * 4
        return tuple(self.pixels[i:i + 4])

    def put(self, x, y, rgba):
        if 0 <= x < self.width and 0 <= y < self.height:
            i = (y * self.width + x) * 4
            self.pixels[i:i + 4] = bytes(rgba)

    def blit(self, source, dx, dy):
        for sy in range(source.height):
            row = ((dy + sy) * self.width + dx) * 4
            srow = (sy * source.width) * 4
            self.pixels[row:row + source.width * 4] = \
                source.pixels[srow:srow + source.width * 4]


# --- decode ----------------------------------------------------------------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(path):
    """decode an 8-bit PNG file into an Image (RGBA). Raises ValueError on an
    unsupported PNG (16-bit, interlaced, ...)."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    offset = 8
    width = height = bit_depth = colour_type = interlace = 0
    idat = bytearray()
    palette = None
    trns = None
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        tag = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length  # length + tag + payload + crc
        if tag == b"IHDR":
            (width, height, bit_depth, colour_type, _comp, _filt,
             interlace) = struct.unpack(">IIBBBBB", payload)
        elif tag == b"PLTE":
            palette = payload
        elif tag == b"tRNS":
            trns = payload
        elif tag == b"IDAT":
            idat.extend(payload)
        elif tag == b"IEND":
            break
    if bit_depth != 8:
        raise ValueError("%s: only 8-bit PNGs are supported (got %d)"
                         % (path, bit_depth))
    if interlace != 0:
        raise ValueError("%s: interlaced PNGs are not supported" % path)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(colour_type)
    if channels is None:
        raise ValueError("%s: unsupported colour type %d" % (path, colour_type))
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    # un-filter scanlines (filter methods 0..4)
    out = bytearray(stride * height)
    previous = bytearray(stride)
    pos = 0
    for row in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filter_type == 1:      # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:    # Up
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:    # Average
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:    # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = previous[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(a, previous[i], c)) & 0xFF
        elif filter_type != 0:
            raise ValueError("%s: bad filter type %d" % (path, filter_type))
        out[row * stride:(row + 1) * stride] = line
        previous = line
    # expand to RGBA
    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        base = i * channels
        if colour_type == 6:      # RGBA
            rgba[i * 4:i * 4 + 4] = out[base:base + 4]
        elif colour_type == 2:    # RGB
            rgba[i * 4:i * 4 + 3] = out[base:base + 3]
            rgba[i * 4 + 3] = 255
        elif colour_type == 0:    # grayscale
            g = out[base]
            rgba[i * 4:i * 4 + 4] = bytes((g, g, g, 255))
        elif colour_type == 4:    # grayscale + alpha
            g = out[base]
            rgba[i * 4:i * 4 + 4] = bytes((g, g, g, out[base + 1]))
        elif colour_type == 3:    # palette
            index = out[base]
            rgba[i * 4:i * 4 + 3] = palette[index * 3:index * 3 + 3]
            rgba[i * 4 + 3] = trns[index] if trns and index < len(trns) else 255
    return Image(width, height, rgba)


# --- encode ----------------------------------------------------------------

def encode_png(image, path):
    """write an Image as an 8-bit RGBA PNG (filter None, max zlib)"""
    raw = bytearray()
    stride = image.width * 4
    for y in range(image.height):
        raw.append(0)  # filter type None
        raw.extend(image.pixels[y * stride:(y + 1) * stride])

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", image.width, image.height, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(png)


def png_size(path):
    """(width, height) straight out of the IHDR - cheap, no full decode"""
    with open(path, "rb") as handle:
        header = handle.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    return struct.unpack(">II", header[16:24])


# --- image ops -------------------------------------------------------------

def premultiply(image):
    """premultiply alpha into RGB in place (straight -> premultiplied)"""
    pixels = image.pixels
    for i in range(0, len(pixels), 4):
        a = pixels[i + 3]
        if a != 255:
            pixels[i] = (pixels[i] * a) // 255
            pixels[i + 1] = (pixels[i + 1] * a) // 255
            pixels[i + 2] = (pixels[i + 2] * a) // 255
    return image


def downscale(image, target_width, target_height):
    """area-average (box) downscale to target size - deterministic, avoids the
    aliasing a nearest-neighbour resize would show. Only meant for shrinking."""
    if target_width == image.width and target_height == image.height:
        return image
    out = Image(target_width, target_height)
    for ty in range(target_height):
        sy0 = (ty * image.height) // target_height
        sy1 = max(sy0 + 1, ((ty + 1) * image.height) // target_height)
        for tx in range(target_width):
            sx0 = (tx * image.width) // target_width
            sx1 = max(sx0 + 1, ((tx + 1) * image.width) // target_width)
            r = g = b = a = count = 0
            for sy in range(sy0, sy1):
                base = (sy * image.width + sx0) * 4
                for _sx in range(sx0, sx1):
                    r += image.pixels[base]
                    g += image.pixels[base + 1]
                    b += image.pixels[base + 2]
                    a += image.pixels[base + 3]
                    base += 4
                    count += 1
            out.put(tx, ty, (r // count, g // count, b // count, a // count))
    return out


def fit_within(width, height, max_size):
    """the largest (w, h) with the same aspect whose longest side is
    <= max_size (>= 1 each); returns (width, height) unchanged when max_size is
    0/negative or already fits"""
    if max_size <= 0 or max(width, height) <= max_size:
        return width, height
    if width >= height:
        new_w = max_size
        new_h = max(1, round(height * max_size / width))
    else:
        new_h = max_size
        new_w = max(1, round(width * max_size / height))
    return new_w, new_h
