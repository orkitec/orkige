#!/usr/bin/env python3
"""Cross-backend pixel comparison of the render_facade_selfcheck output.

The WYSIWYG backend-parity gate: the facade selfcheck renders the SAME scene
on the classic-OGRE and Ogre-Next flavors; this driver runs both binaries
(each into its own output directory) and compares the named screenshot pairs
within tolerance. Registered as ctest `render_backend_parity` on the NEXT
preset only (tests/CMakeLists.txt): the next flavor is the one that must
match the established classic output. Cross-preset reality: the classic
binary lives in another build tree - when it is absent (not configured/built
on this machine) the test SKIPs honestly (exit 77, ctest SKIP_RETURN_CODE)
instead of failing or silently passing.

Comparison model (per pair): images must have identical dimensions; the mean
absolute per-channel error must stay below MEAN_TOLERANCE and at most
OUTLIER_FRACTION of the pixels may differ by more than OUTLIER_TOLERANCE per
channel. The window shot contains PBS-vs-RTSS-Phong LIT content (the textured
platform), which legitimately shades a little differently - the thresholds
leave room for shading models while catching the actual parity failure
classes (sRGB/gamma mismatches shift EVERYTHING by dozens of levels, missing
content flips whole regions).

Pure stdlib (zlib PNG decode - the screenshots are 8-bit RGB/RGBA PNGs).
"""

import argparse
import os
import struct
import subprocess
import sys
import zlib

SKIP_EXIT_CODE = 77

#: screenshots compared (written by tests/render_facade/selfcheck_main.cpp)
COMPARED_SHOTS = [
    # 3D scene over the window: vertex-coloured mesh (unlit), textured
    # platform (lit - the tolerance headroom is for this one), background
    "selfcheck_window.png",
    # the DrawLayer2D conformance pattern over the same scene
    "selfcheck_drawlayer2d.png",
    # the offscreen path: sprite through an ortho camera into an RTT
    "selfcheck_rtt.png",
]

MEAN_TOLERANCE = 6.0          # mean abs diff per channel, 0..255
OUTLIER_TOLERANCE = 48        # a pixel "differs" above this per-channel delta
OUTLIER_FRACTION = 0.02       # fraction of differing pixels allowed


def decode_png(path):
    """Minimal PNG decoder: 8-bit RGB/RGBA/gray, non-interlaced.

    Returns (width, height, channels, bytearray of unfiltered scanlines).
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    width = height = None
    bit_depth = colour_type = None
    idat = bytearray()
    while pos < len(data):
        length, chunk_type = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            (width, height, bit_depth, colour_type,
             _compression, _filter, interlace) = struct.unpack(
                ">IIBBBBB", chunk)
            if bit_depth != 8 or colour_type not in (0, 2, 6):
                raise ValueError(f"{path}: unsupported PNG "
                                 f"(depth {bit_depth}, colour {colour_type})")
            if interlace != 0:
                raise ValueError(f"{path}: interlaced PNGs unsupported")
        elif chunk_type == b"IDAT":
            idat.extend(chunk)
        elif chunk_type == b"IEND":
            break
    channels = {0: 1, 2: 3, 6: 4}[colour_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * channels)
    previous = bytearray(stride)
    src = 0
    for row in range(height):
        filter_type = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if filter_type == 1:    # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                up_left = previous[i - channels] if i >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                if pa <= pb and pa <= pc:
                    predictor = left
                elif pb <= pc:
                    predictor = up
                else:
                    predictor = up_left
                line[i] = (line[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"{path}: unknown PNG filter {filter_type}")
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, out


def compare_pair(classic_path, next_path):
    """Compare one screenshot pair; returns (ok, human-readable summary)."""
    cw, ch, cc, classic = decode_png(classic_path)
    nw, nh, nc, nxt = decode_png(next_path)
    if (cw, ch) != (nw, nh):
        return False, (f"dimension mismatch: classic {cw}x{ch} vs "
                       f"next {nw}x{nh}")
    pixel_count = cw * ch
    compare_channels = min(cc, nc, 3)   # RGB only (alpha differs by format)
    total_diff = 0
    outliers = 0
    for pixel in range(pixel_count):
        c_base = pixel * cc
        n_base = pixel * nc
        worst = 0
        for channel in range(compare_channels):
            diff = abs(classic[c_base + channel] - nxt[n_base + channel])
            total_diff += diff
            if diff > worst:
                worst = diff
        if worst > OUTLIER_TOLERANCE:
            outliers += 1
    mean = total_diff / float(pixel_count * compare_channels)
    outlier_fraction = outliers / float(pixel_count)
    ok = mean <= MEAN_TOLERANCE and outlier_fraction <= OUTLIER_FRACTION
    summary = (f"mean {mean:.2f}/{MEAN_TOLERANCE} "
               f"outliers {outlier_fraction * 100.0:.2f}%/"
               f"{OUTLIER_FRACTION * 100.0:.0f}% (>{OUTLIER_TOLERANCE})")
    return ok, summary


def read_dimensions(out_dir):
    """Parse a selfcheck's dimensions.txt sidecar.

    Returns {"logical": (w, h), "pixel": (w, h)} or None when absent/malformed.
    The sidecar lets the parity gate assert both flavors agree on the LOGICAL
    (points) window and the PIXEL (drawable) surface for the same request -
    the density-disagreement signal, kept independent of the host's display
    scale (both flavors track the same OS backing scale, so they must match on
    any machine).
    """
    path = os.path.join(out_dir, "dimensions.txt")
    if not os.path.exists(path):
        return None
    result = {}
    with open(path) as handle:
        for line in handle:
            parts = line.split()
            if len(parts) == 3 and parts[0] in ("logical", "pixel"):
                result[parts[0]] = (int(parts[1]), int(parts[2]))
    if "logical" not in result or "pixel" not in result:
        return None
    return result


def compare_dimensions(classic_dims, next_dims):
    """Assert the two flavors made the same pixel-density choice.

    Returns (ok, summary). Fails if either the logical (points) request or the
    resulting pixel (drawable) surface differs between flavors - the exact
    class of bug the HiDPI gap was (Metal at 2x backing vs GL at 1x logical).
    """
    if classic_dims is None or next_dims is None:
        return False, ("dimensions.txt missing - cannot verify the flavors "
                       "agree on window pixel density")
    ok = True
    notes = []
    for key in ("logical", "pixel"):
        cw, ch = classic_dims[key]
        nw, nh = next_dims[key]
        if (cw, ch) != (nw, nh):
            ok = False
            notes.append(f"{key} mismatch: classic {cw}x{ch} vs next {nw}x{nh}")
        else:
            notes.append(f"{key} {cw}x{ch}")
    return ok, "; ".join(notes)


def run_selfcheck(binary, out_dir, cwd):
    os.makedirs(out_dir, exist_ok=True)
    environment = dict(os.environ)
    environment["ORKIGE_SELFCHECK_OUT"] = out_dir
    result = subprocess.run([binary], cwd=cwd, env=environment,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=120)
    if result.returncode != 0:
        sys.stdout.buffer.write(result.stdout)
        raise RuntimeError(f"{binary} exited with {result.returncode}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--next-binary", required=True,
                        help="this build tree's render_facade_selfcheck")
    parser.add_argument("--classic-binary", required=True,
                        help="the classic tree's render_facade_selfcheck "
                             "(SKIP when absent)")
    parser.add_argument("--out", required=True,
                        help="working directory for both runs' screenshots")
    parser.add_argument("--repo", required=True,
                        help="repo root (the selfcheck's working directory)")
    args = parser.parse_args()

    if not os.path.exists(args.classic_binary):
        print(f"SKIP: classic selfcheck binary not built "
              f"({args.classic_binary}) - configure + build the classic "
              f"preset to enable the cross-backend parity comparison")
        return SKIP_EXIT_CODE

    classic_out = os.path.join(args.out, "classic")
    next_out = os.path.join(args.out, "next")
    print(f"running classic selfcheck: {args.classic_binary}")
    run_selfcheck(args.classic_binary, classic_out, args.repo)
    print(f"running next selfcheck: {args.next_binary}")
    run_selfcheck(args.next_binary, next_out, args.repo)

    failures = 0

    # density gate FIRST: both flavors must make the same pixel-density choice
    # for the same window request (logical points AND drawable pixels). This is
    # the WYSIWYG contract at the surface level - if it fails, the per-pixel
    # compare below would also fail on dimensions, but this reports the real
    # cause (a flavor ignoring the OS backing scale) directly.
    dims_ok, dims_summary = compare_dimensions(
        read_dimensions(classic_out), read_dimensions(next_out))
    print(f"{'ok  ' if dims_ok else 'FAIL'} window density: {dims_summary}")
    failures += 0 if dims_ok else 1

    for shot in COMPARED_SHOTS:
        classic_path = os.path.join(classic_out, shot)
        next_path = os.path.join(next_out, shot)
        if not os.path.exists(classic_path) or not os.path.exists(next_path):
            print(f"FAIL {shot}: screenshot missing "
                  f"({classic_path} / {next_path})")
            failures += 1
            continue
        ok, summary = compare_pair(classic_path, next_path)
        print(f"{'ok  ' if ok else 'FAIL'} {shot}: {summary}")
        failures += 0 if ok else 1
    if failures:
        print(f"render_backend_parity: {failures} screenshot pair(s) out of "
              f"tolerance - the backends must render the same image "
              f"(Docs/render-abstraction.md, colour parity)")
        return 1
    print("render_backend_parity: all screenshot pairs within tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())
