#!/usr/bin/env python3
"""Cross-backend pixel comparison of the render_facade_selfcheck output.

The WYSIWYG backend-parity gate: the facade selfcheck renders the SAME scene
on the classic-OGRE and Ogre-Next flavors; this driver compares the named
screenshot pairs within tolerance. Two roads reach the same comparison:

  * RUN BOTH BINARIES (--next-binary/--classic-binary): the developer road,
    on a machine carrying both build trees. Registered as ctest
    `render_backend_parity` on the NEXT preset. The classic binary lives in
    another build tree - when it is absent the test SKIPs honestly (exit 77,
    ctest SKIP_RETURN_CODE) instead of failing or silently passing.
  * COMPARE CAPTURED DIRECTORIES (--classic-shots/--next-shots): each flavor
    ran its own selfcheck elsewhere and its output directory was carried
    here. One flavor per machine is the shape a per-flavor build matrix has,
    so this is how the gate runs where no single machine holds both trees.
    A directory that is missing, empty or short of the compared shots is a
    FAILURE, never a skip - a parity gate that compared nothing must not
    report parity.

Comparison model (per pair): images must have identical dimensions; the mean
absolute per-channel error must stay below MEAN_TOLERANCE and at most
OUTLIER_FRACTION of the pixels may differ by more than OUTLIER_TOLERANCE per
channel. The window shot contains PBS-vs-RTSS-Phong LIT content (the textured
platform), which legitimately shades a little differently - the thresholds
leave room for shading models while catching the actual parity failure
classes (sRGB/gamma mismatches shift EVERYTHING by dozens of levels, missing
content flips whole regions).

Two numbers cannot say WHERE a pair disagrees, so every pair also reports the
largest 8-connected region of differing pixels (parity_diff), and a pair that
fails leaves a DIFF IMAGE beside the compared shot - `<shot>.diff.png`, the
delta painted over the frame it belongs to. The region size is reported, not
gated: see parity_diff for why.

Pure stdlib (zlib PNG decode - the screenshots are 8-bit RGB/RGBA PNGs).
"""

import argparse
import contextlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parity_diff  # noqa: E402

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
    # the PARAMETRIC mesh tier: a `.omesh` text asset parsed, built through
    # RenderWorld::createMeshFromData and instantiated the ordinary way. Its
    # surface is emissive-only with every light suppressed, so this capture is a
    # silhouette/coverage image of the GENERATED GEOMETRY - the two flavors'
    # shading models cannot contribute a delta, which makes it the strictest
    # geometry comparison in the set.
    "selfcheck_omesh.png",
]

# The corridors were measured on the developer pair (GL3Plus against Metal).
# What the CI pair (a software GL rasterizer against a software Vulkan one)
# actually measures, from a green run of the parity job:
#
#   selfcheck_window.png       mean 0.17   outliers 0.30%
#   selfcheck_drawlayer2d.png  mean 0.17   outliers 0.30%
#   selfcheck_rtt.png          mean 0.00   outliers 0.00%
#   selfcheck_omesh.png        mean 2.08   outliers 0.00%
#
# Two facts to keep: the means sit far inside a 6.0 corridor, and the two
# window shots carry ~0.3% of pixels differing by MORE than 48 while their
# mean is 0.17 - a small, strongly-differing set that the mean alone cannot
# see. That is the shape the region report exists to name. The corridors stay
# where they are until a tightening is measured rather than guessed.
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


def compare_pair(classic_path, next_path, diff_dir=None):
    """Compare one screenshot pair; returns (ok, human-readable summary).

    One pass over the pair produces the per-pixel delta map, and the mean, the
    outlier fraction and the region shape are all read off THAT - the pixels
    are never walked twice for three numbers.
    """
    classic = decode_png(classic_path)
    nxt = decode_png(next_path)
    if (classic[0], classic[1]) != (nxt[0], nxt[1]):
        return False, (f"dimension mismatch: classic {classic[0]}x{classic[1]} "
                       f"vs next {nxt[0]}x{nxt[1]}")
    dmap = parity_diff.delta_map(classic, nxt)
    pixel_count = dmap.width * dmap.height
    outliers = sum(1 for delta in dmap.deltas if delta > OUTLIER_TOLERANCE)
    mean = dmap.channel_sum / float(pixel_count * dmap.channels)
    outlier_fraction = outliers / float(pixel_count)
    ok = mean <= MEAN_TOLERANCE and outlier_fraction <= OUTLIER_FRACTION
    spatial = parity_diff.spatial_summary(dmap, OUTLIER_TOLERANCE)
    summary = (f"mean {mean:.2f}/{MEAN_TOLERANCE} "
               f"outliers {outlier_fraction * 100.0:.2f}%/"
               f"{OUTLIER_FRACTION * 100.0:.0f}% (>{OUTLIER_TOLERANCE}); "
               + parity_diff.describe(spatial, pixel_count, OUTLIER_TOLERANCE))
    destination = parity_diff.diff_path(next_path, diff_dir)
    if ok:
        parity_diff.drop_stale_diff(destination)
    else:
        # the failure travels with its picture: the delta over the classic
        # frame, beside the shot, inside whatever the job uploads
        written = parity_diff.try_write_diff(destination, dmap, classic)
        if written:
            summary += f"; diff image {written}"
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
                            stderr=subprocess.STDOUT, timeout=300)
    if result.returncode != 0:
        sys.stdout.buffer.write(result.stdout)
        raise RuntimeError(f"{binary} exited with {result.returncode}")


class ShotsUnusable(Exception):
    """A captured output directory cannot be compared - refuse, never pass."""


def verify_shots_dir(label, directory):
    """Refuse a captured directory that carries nothing to compare.

    The failure mode this exists to prevent: a comparison handed an empty or
    wrong directory reports parity because it found no disagreement. Silence
    is not evidence, so a directory that is absent, empty or short of every
    compared screenshot raises instead.
    """
    if not os.path.isdir(directory):
        raise ShotsUnusable(f"{label} screenshot directory does not exist: "
                            f"{directory}")
    if not os.listdir(directory):
        raise ShotsUnusable(f"{label} screenshot directory is empty: "
                            f"{directory}")
    if not any(os.path.exists(os.path.join(directory, shot))
               for shot in COMPARED_SHOTS):
        raise ShotsUnusable(
            f"{label} screenshot directory carries none of the compared "
            f"screenshots ({', '.join(COMPARED_SHOTS)}): {directory}")


def compare_shot_dirs(classic_out, next_out, diff_dir=None):
    """Compare two captured selfcheck output directories; returns failures."""
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
        ok, summary = compare_pair(classic_path, next_path, diff_dir)
        print(f"{'ok  ' if ok else 'FAIL'} {shot}: {summary}")
        failures += 0 if ok else 1
    return failures


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--next-binary",
                        help="this build tree's render_facade_selfcheck")
    parser.add_argument("--classic-binary",
                        help="the classic tree's render_facade_selfcheck "
                             "(SKIP when absent)")
    parser.add_argument("--out",
                        help="working directory for both runs' screenshots")
    parser.add_argument("--repo",
                        help="repo root (the selfcheck's working directory)")
    parser.add_argument("--classic-shots",
                        help="a classic selfcheck output directory captured "
                             "elsewhere (compared as-is, nothing is run)")
    parser.add_argument("--next-shots",
                        help="a next selfcheck output directory captured "
                             "elsewhere (compared as-is, nothing is run)")
    parser.add_argument("--diff-dir",
                        help="where a failing pair's diff image lands "
                             "(default: beside the next screenshot)")
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the pure parts and exit")
    return parser.parse_args(argv)


def resolve_directories(args):
    """Produce the (classic, next) directories to compare, running if asked.

    Returns None when the run road is unavailable and the honest answer is a
    skip; raises ShotsUnusable when captured directories cannot be compared.
    """
    captured = bool(args.classic_shots) or bool(args.next_shots)
    if captured:
        if not (args.classic_shots and args.next_shots):
            raise ShotsUnusable("--classic-shots and --next-shots come as a "
                                "pair - one alone compares nothing")
        verify_shots_dir("classic", args.classic_shots)
        verify_shots_dir("next", args.next_shots)
        return args.classic_shots, args.next_shots

    missing = [name for name, value in (("--next-binary", args.next_binary),
                                        ("--classic-binary",
                                         args.classic_binary),
                                        ("--out", args.out),
                                        ("--repo", args.repo)) if not value]
    if missing:
        raise ShotsUnusable("either the captured directories "
                            "(--classic-shots + --next-shots) or the full "
                            "run arguments are required; missing "
                            + ", ".join(missing))
    if not os.path.exists(args.classic_binary):
        return None

    classic_out = os.path.join(args.out, "classic")
    next_out = os.path.join(args.out, "next")
    print(f"running classic selfcheck: {args.classic_binary}")
    run_selfcheck(args.classic_binary, classic_out, args.repo)
    print(f"running next selfcheck: {args.next_binary}")
    run_selfcheck(args.next_binary, next_out, args.repo)
    return classic_out, next_out


def main(argv=None):
    args = parse_args(argv)
    if args.selftest:
        return selftest()

    try:
        directories = resolve_directories(args)
    except ShotsUnusable as refusal:
        print(f"render_backend_parity: FAIL: {refusal}")
        return 1
    if directories is None:
        print(f"SKIP: classic selfcheck binary not built "
              f"({args.classic_binary}) - configure + build the classic "
              f"preset to enable the cross-backend parity comparison")
        return SKIP_EXIT_CODE

    failures = compare_shot_dirs(*directories, diff_dir=args.diff_dir)
    if failures:
        print(f"render_backend_parity: {failures} screenshot pair(s) out of "
              f"tolerance - the backends must render the same image "
              f"(Docs/render-abstraction.md, colour parity)")
        return 1
    print("render_backend_parity: all screenshot pairs within tolerance")
    return 0


# --- selftest ---------------------------------------------------------------

def write_png(path, width, height, fill):
    """Write a minimal 8-bit RGB PNG of one colour (selftest fixture)."""
    raw = bytearray()
    for _row in range(height):
        raw.append(0)                       # filter type None
        raw.extend(bytes(fill) * width)
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw))))
        handle.write(chunk(b"IEND", b""))


def write_shots_dir(directory, fill, logical=(960, 540), pixel=(960, 540)):
    os.makedirs(directory, exist_ok=True)
    for shot in COMPARED_SHOTS:
        write_png(os.path.join(directory, shot), 8, 8, fill)
    with open(os.path.join(directory, "dimensions.txt"), "w") as handle:
        handle.write("logical %d %d\npixel %d %d\n"
                     % (logical[0], logical[1], pixel[0], pixel[1]))


def run_quiet(argv):
    """Run main() swallowing its report - a passing selftest logs no FAIL."""
    captured = io.StringIO()
    with contextlib.redirect_stdout(captured):
        code = main(argv)
    return code, captured.getvalue()


def expect_refusal(what, argv, names):
    """A run that MUST refuse: exit 1, saying which directory it refused."""
    code, said = run_quiet(argv)
    if code != 1:
        raise AssertionError(f"{what} returned {code}, must refuse with 1")
    if names not in said:
        raise AssertionError(f"{what} refused without naming {names}: {said}")


def selftest():
    scratch = tempfile.mkdtemp(prefix="orkige_parity_selftest_")
    classic = os.path.join(scratch, "classic")
    nxt = os.path.join(scratch, "next")

    # the shared diagnosis: clustering, the heat ramp, the diff destination,
    # and the writer read back through THIS file's decoder
    parity_diff.selftest_pure()
    parity_diff.selftest_roundtrip(decode_png, scratch)

    # identical captures compare clean, report their shape and leave no diff
    write_shots_dir(classic, (40, 80, 120))
    write_shots_dir(nxt, (40, 80, 120))
    code, said = run_quiet(["--classic-shots", classic, "--next-shots", nxt])
    assert code == 0, said
    assert "no pixel over" in said, said
    diff_image = os.path.join(nxt, COMPARED_SHOTS[0].replace(".png",
                                                             ".diff.png"))
    assert not os.path.exists(diff_image), "a clean pair wrote a diff image"

    # a difference beyond tolerance is caught, names the region it found and
    # leaves the picture behind - the whole 8x8 fixture is one region
    write_shots_dir(nxt, (200, 80, 120))
    code, said = run_quiet(["--classic-shots", classic, "--next-shots", nxt])
    assert code == 1, said
    assert "largest region 64px" in said, said
    assert os.path.exists(diff_image), said
    assert decode_png(diff_image)[0] == 8
    assert diff_image in said, said

    # ... a diff directory of its own is honored
    elsewhere = os.path.join(scratch, "diffs")
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt,
                      "--diff-dir", elsewhere])[0] == 1
    assert os.path.exists(os.path.join(
        elsewhere, COMPARED_SHOTS[0].replace(".png", ".diff.png")))

    # ... and a difference inside tolerance is not a failure, and takes the
    # now-stale picture of the old one away with it
    write_shots_dir(nxt, (43, 80, 120))
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 0
    assert not os.path.exists(diff_image), "a passing pair kept a stale diff"

    # a density disagreement fails on its own, before any pixel differs
    write_shots_dir(nxt, (40, 80, 120), pixel=(1920, 1080))
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 1
    write_shots_dir(nxt, (40, 80, 120))

    # THE refusals: comparing nothing must never read as parity
    absent = os.path.join(scratch, "absent")
    expect_refusal("a missing directory",
                   ["--classic-shots", absent, "--next-shots", nxt], absent)
    empty = os.path.join(scratch, "empty")
    os.makedirs(empty, exist_ok=True)
    expect_refusal("an empty directory",
                   ["--classic-shots", empty, "--next-shots", nxt], empty)
    unrelated = os.path.join(scratch, "unrelated")
    os.makedirs(unrelated, exist_ok=True)
    write_png(os.path.join(unrelated, "something_else.png"), 8, 8, (0, 0, 0))
    expect_refusal("a directory holding no compared screenshot",
                   ["--classic-shots", unrelated, "--next-shots", nxt],
                   unrelated)
    expect_refusal("one directory without the other",
                   ["--classic-shots", classic], "--next-shots")
    expect_refusal("no arguments at all", [], "--classic-shots")

    # a shot missing from an otherwise usable directory is a failure too
    partial = os.path.join(scratch, "partial")
    write_shots_dir(partial, (40, 80, 120))
    os.remove(os.path.join(partial, COMPARED_SHOTS[-1]))
    code, said = run_quiet(["--classic-shots", classic,
                            "--next-shots", partial])
    assert code == 1 and COMPARED_SHOTS[-1] in said, said

    # the run road keeps its honest skip when the sibling tree is unbuilt
    assert run_quiet(["--next-binary", "/nonexistent/next",
                      "--classic-binary", "/nonexistent/classic",
                      "--out", scratch,
                      "--repo", scratch])[0] == SKIP_EXIT_CODE

    # argument routing
    parsed = parse_args(["--classic-shots", "a", "--next-shots", "b"])
    assert parsed.classic_shots == "a" and parsed.next_shots == "b"
    assert parse_args(["--selftest"]).selftest is True

    shutil.rmtree(scratch, ignore_errors=True)
    print("compare_backend_screenshots: selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
