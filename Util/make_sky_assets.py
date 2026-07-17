#!/usr/bin/env python3
"""Procedural sky cubemap baker (stdlib only, deterministic).

Bakes the engine's stock skybox cubemaps for the atmosphere's "skybox" sky
type (AtmosphereDesc::skyType - see Docs/render-abstraction.md):

  * sky_day.dds   - a blue day sky with fBm value-noise clouds (the terrain
                    generator's noise recipe, lifted to 3D so the cube has no
                    face seams)
  * sky_night.dds - a deep-blue night gradient with a seeded starfield
  * sky_faces.dds - a tiny six-colour debug cubemap (one flat colour per
                    face) for pixel probes and orientation checks

Container: ONE uncompressed BGRA8 .dds cubemap per sky with a full box-filter
mip chain - the single cubemap form BOTH render flavors load natively (the
classic codec feeds SceneManager::setSkyBox, the next TextureGpuManager feeds
SceneManager::setSky), and the export cook ships untouched (it only
block-compresses .png payloads). Faces are written in the container's
standard +X,-X,+Y,-Y,+Z,-Z order and orientation.

Everything derives from an integer seed - re-running the tool reproduces the
committed bytes exactly (--selftest asserts it).
"""

import argparse
import functools
import math
import os
import struct
import sys

DEFAULT_SIZE = 128      # day/night face edge (power of two for clean mips)
FACES_SIZE = 8          # debug cubemap face edge
DEFAULT_SEED = 7

# the six face colours of sky_faces.dds (+X,-X,+Y,-Y,+Z,-Z) - saturated and
# pairwise distinct so a probe can tell every face (and the procedural
# gradient) apart
FACE_TEST_COLOURS = [
    (0.9, 0.1, 0.1),    # +X red
    (0.1, 0.9, 0.9),    # -X cyan
    (0.1, 0.9, 0.1),    # +Y green
    (0.9, 0.1, 0.9),    # -Y magenta
    (0.1, 0.1, 0.9),    # +Z blue
    (0.9, 0.9, 0.1),    # -Z yellow
]


# --- deterministic value noise (3D lift of make_terrain_mesh.py's) ---------

@functools.lru_cache(maxsize=None)
def _hash01(ix, iy, iz, seed):
    """A stable 0..1 hash of an integer lattice point (deterministic across
    runs and platforms - no random module state)."""
    h = (ix * 374761393 + iy * 668265263 + iz * 2147483647 + seed * 144665) \
        & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return h / 0xFFFFFFFF


def _smooth(t):
    return t * t * (3.0 - 2.0 * t)


def _vnoise3(fx, fy, fz, seed):
    """Trilinear 3D value noise with a smoothstep fade."""
    ix, iy, iz = math.floor(fx), math.floor(fy), math.floor(fz)
    tx, ty, tz = _smooth(fx - ix), _smooth(fy - iy), _smooth(fz - iz)
    c = [[[_hash01(ix + dx, iy + dy, iz + dz, seed)
           for dz in (0, 1)] for dy in (0, 1)] for dx in (0, 1)]
    lerp = lambda a, b, t: a + (b - a) * t
    yz = [[lerp(c[0][dy][dz], c[1][dy][dz], tx)
           for dz in (0, 1)] for dy in (0, 1)]
    z = [lerp(yz[0][dz], yz[1][dz], ty) for dz in (0, 1)]
    return lerp(z[0], z[1], tz)


def _fbm3(fx, fy, fz, seed, octaves):
    """Fractal Brownian motion: summed 3D value-noise octaves, 0..1."""
    total, amplitude, frequency, norm = 0.0, 1.0, 1.0, 0.0
    for octave in range(octaves):
        total += _vnoise3(fx * frequency, fy * frequency, fz * frequency,
                          seed + octave * 101) * amplitude
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.0
    return total / norm


# --- cubemap face directions ------------------------------------------------

def face_direction(face, u, v):
    """The unnormalised view direction of face pixel (u,v in 0..1, v down),
    in the container's standard face layout."""
    a, b = 2.0 * u - 1.0, 2.0 * v - 1.0
    if face == 0:
        d = (1.0, -b, -a)       # +X
    elif face == 1:
        d = (-1.0, -b, a)       # -X
    elif face == 2:
        d = (a, 1.0, b)         # +Y
    elif face == 3:
        d = (a, -1.0, -b)       # -Y
    elif face == 4:
        d = (a, -b, 1.0)        # +Z
    else:
        d = (-a, -b, -1.0)      # -Z
    length = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    return (d[0] / length, d[1] / length, d[2] / length)


# --- the two stock skies ----------------------------------------------------

def _lerp3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t)


def day_colour(direction, seed):
    """Blue gradient + fBm clouds above the horizon, soft ground haze below."""
    x, y, z = direction
    zenith = (0.25, 0.45, 0.85)
    horizon = (0.72, 0.82, 0.94)
    ground = (0.42, 0.46, 0.52)
    if y >= 0.0:
        base = _lerp3(horizon, zenith, _smooth(min(1.0, y)))
        # clouds thin toward the zenith and vanish at the horizon line
        density = _fbm3(x * 3.0, y * 3.0, z * 3.0, seed, 4)
        cover = max(0.0, min(1.0, (density - 0.52) * 4.0))
        cover *= _smooth(min(1.0, y * 4.0))
        return _lerp3(base, (0.98, 0.98, 1.0), cover)
    return _lerp3(horizon, ground, _smooth(min(1.0, -y * 2.0)))


def night_colour(direction, face, px, py, seed):
    """Deep-blue night gradient + a seeded starfield above the horizon."""
    x, y, z = direction
    zenith = (0.015, 0.03, 0.09)
    horizon = (0.05, 0.07, 0.16)
    ground = (0.008, 0.012, 0.03)
    if y >= 0.0:
        base = _lerp3(horizon, zenith, _smooth(min(1.0, y)))
        # stars: a sparse per-pixel hash; brightness from a second hash so the
        # field twinkles in size. Baked on the pixel grid (stars soften away
        # in the mip chain, like a distant field should).
        gate = _hash01(px, py, face, seed + 9001)
        if gate > 0.997 and y > 0.05:
            brightness = 0.4 + 0.6 * _hash01(px, py, face, seed + 9002)
            return _lerp3(base, (0.9, 0.93, 1.0), brightness)
        return base
    return _lerp3(horizon, ground, _smooth(min(1.0, -y * 2.0)))


# --- DDS cubemap writer -----------------------------------------------------

def _mip_chain(face_pixels, size):
    """Box-filtered mip chain of one face: [(size, rows-of-(r,g,b) floats)]
    down to 1x1. face_pixels is a flat row-major list of (r,g,b)."""
    chain = [(size, face_pixels)]
    current, dim = face_pixels, size
    while dim > 1:
        half = dim // 2
        smaller = []
        for y in range(half):
            for x in range(half):
                acc = [0.0, 0.0, 0.0]
                for dy in (0, 1):
                    for dx in (0, 1):
                        p = current[(y * 2 + dy) * dim + (x * 2 + dx)]
                        acc[0] += p[0]
                        acc[1] += p[1]
                        acc[2] += p[2]
                smaller.append((acc[0] / 4.0, acc[1] / 4.0, acc[2] / 4.0))
        chain.append((half, smaller))
        current, dim = smaller, half
    return chain


def _mip_count(size):
    count = 1
    while size > 1:
        size //= 2
        count += 1
    return count


def build_dds_cubemap(size, colour_at):
    """The complete .dds byte string: uncompressed BGRA8 cubemap, full mip
    chain, faces via colour_at(face, px, py, direction) -> (r,g,b) floats."""
    mips = _mip_count(size)
    header = struct.pack(
        "<4s7I44x8I2I12x",
        b"DDS ",
        124,                    # header size
        0x0002100F,             # CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT|MIPMAPCOUNT
        size, size,
        size * 4,               # pitch of the top mip
        0,                      # depth
        mips,
        # DDS_PIXELFORMAT: 32bpp masked BGRA (bytes B,G,R,A little-endian)
        32, 0x41, 0,            # size, RGB|ALPHAPIXELS, no fourCC
        32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,
        0x00401008,             # caps: COMPLEX|TEXTURE|MIPMAP
        0x0000FE00)             # caps2: CUBEMAP + all six faces
    body = bytearray()
    for face in range(6):
        pixels = []
        for py in range(size):
            for px in range(size):
                direction = face_direction(face, (px + 0.5) / size,
                                           (py + 0.5) / size)
                pixels.append(colour_at(face, px, py, direction))
        for dim, mip in _mip_chain(pixels, size):
            for r, g, b in mip:
                body.append(min(255, max(0, int(b * 255.0 + 0.5))))
                body.append(min(255, max(0, int(g * 255.0 + 0.5))))
                body.append(min(255, max(0, int(r * 255.0 + 0.5))))
                body.append(255)
    return header + bytes(body)


def build_day(size, seed):
    return build_dds_cubemap(
        size, lambda face, px, py, d: day_colour(d, seed))


def build_night(size, seed):
    return build_dds_cubemap(
        size, lambda face, px, py, d: night_colour(d, face, px, py, seed))


def build_faces(size=FACES_SIZE):
    return build_dds_cubemap(
        size, lambda face, px, py, d: FACE_TEST_COLOURS[face])


# --- output -----------------------------------------------------------------

def write_assets(out_dir, size, seed, sidecars=False):
    """Bake the three skies into out_dir; returns the written paths."""
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for name, data in (("sky_day.dds", build_day(size, seed)),
                       ("sky_night.dds", build_night(size, seed)),
                       ("sky_faces.dds", build_faces())):
        path = os.path.join(out_dir, name)
        with open(path, "wb") as handle:
            handle.write(data)
        written.append(path)
        if sidecars:
            # project assets ride the sidecar id machinery; cubemaps are
            # final artwork - the export cook never touches .dds anyway,
            # format="none" states the intent
            import orkige_sidecar
            orkige_sidecar.stamp_texture_sidecar(path, "none")
    return written


# --- selftest ---------------------------------------------------------------

def parse_dds(data):
    """Minimal structural parse; returns (size, mips, caps2, body_length)."""
    assert data[:4] == b"DDS ", "missing DDS magic"
    (header_size, flags, height, width, pitch, depth, mips) = \
        struct.unpack_from("<7I", data, 4)
    assert header_size == 124, "bad header size"
    (pf_size, pf_flags) = struct.unpack_from("<2I", data, 76)
    (caps, caps2) = struct.unpack_from("<2I", data, 108)
    return {
        "width": width, "height": height, "mips": mips,
        "flags": flags, "pf_size": pf_size, "pf_flags": pf_flags,
        "caps": caps, "caps2": caps2,
        "body": data[4 + 124:],
    }


def _expected_body_length(size, mips):
    total, dim = 0, size
    for _ in range(mips):
        total += dim * dim * 4
        dim = max(1, dim // 2)
    return total * 6


def _face_pixel(parsed, face, x, y):
    """Top-mip pixel (r,g,b) 0..1 of a parsed cubemap."""
    size = parsed["width"]
    face_bytes = _expected_body_length(size, parsed["mips"]) // 6
    at = face * face_bytes + (y * size + x) * 4
    b, g, r = parsed["body"][at], parsed["body"][at + 1], parsed["body"][at + 2]
    return (r / 255.0, g / 255.0, b / 255.0)


def selftest():
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    size, seed = 16, DEFAULT_SEED
    day = build_day(size, seed)
    night = build_night(size, seed)
    faces = build_faces()

    # structure: a real cubemap container with a complete mip chain
    for name, data, expected_size in (("day", day, size),
                                      ("night", night, size),
                                      ("faces", faces, FACES_SIZE)):
        parsed = parse_dds(data)
        check(parsed["width"] == expected_size and
              parsed["height"] == expected_size,
              "%s: wrong face size" % name)
        check(parsed["mips"] == _mip_count(expected_size),
              "%s: incomplete mip chain" % name)
        check(parsed["pf_size"] == 32 and parsed["pf_flags"] == 0x41,
              "%s: not masked 32bpp BGRA" % name)
        check(parsed["caps2"] == 0x0000FE00,
              "%s: not a six-face cubemap" % name)
        check(len(parsed["body"]) ==
              _expected_body_length(expected_size, parsed["mips"]),
              "%s: body length mismatch" % name)

    # determinism: the same seed reproduces the committed bytes exactly
    check(build_day(size, seed) == day, "day bake is not deterministic")
    check(build_night(size, seed) == night, "night bake is not deterministic")
    check(build_faces() == faces, "faces bake is not deterministic")
    check(build_day(size, seed + 1) != day,
          "a different seed should change the day sky")

    # the debug cubemap really carries one distinct colour per face
    parsed_faces = parse_dds(faces)
    for face, expected in enumerate(FACE_TEST_COLOURS):
        got = _face_pixel(parsed_faces, face, FACES_SIZE // 2, FACES_SIZE // 2)
        check(all(abs(g - e) < 0.01 for g, e in zip(got, expected)),
              "faces: face %d colour mismatch (%r vs %r)"
              % (face, got, expected))

    # the skies look like their names: day's +Y zenith is bright and
    # blue-dominant, night's is dark; both keep the horizon above the ground
    parsed_day = parse_dds(day)
    parsed_night = parse_dds(night)
    day_zenith = _face_pixel(parsed_day, 2, size // 2, size // 2)
    night_zenith = _face_pixel(parsed_night, 2, size // 2, size // 2)
    check(day_zenith[2] > 0.5 and day_zenith[2] >= day_zenith[0],
          "day zenith should be bright blue-dominant (%r)" % (day_zenith,))
    check(sum(night_zenith) < 0.7,
          "night zenith should be dark (%r)" % (night_zenith,))
    down_day = _face_pixel(parsed_day, 3, size // 2, size // 2)
    check(sum(down_day) < sum(_face_pixel(parsed_day, 2, 0, size - 1)) + 1.5,
          "day ground should not out-shine the sky")

    if failures:
        for failure in failures:
            print("make_sky_assets selftest FAIL: %s" % failure)
        return 1
    print("make_sky_assets selftest OK")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="bake the stock sky cubemaps (.dds, both-flavor native)")
    parser.add_argument("--out", default=os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "samples", "hello_orkige", "media"),
        help="output directory (default: the hello_orkige media dir)")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE,
                        help="face edge in pixels (power of two)")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--sidecars", action="store_true",
                        help="stamp .orkmeta sidecars (project assets)")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        sys.exit(selftest())
    if args.size < 4 or (args.size & (args.size - 1)) != 0:
        parser.error("--size must be a power of two >= 4")
    for path in write_assets(args.out, args.size, args.seed, args.sidecars):
        print("wrote %s" % path)


if __name__ == "__main__":
    main()
