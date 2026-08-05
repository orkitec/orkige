#!/usr/bin/env python3
"""Cross-FLAVOR look parity on a benchmark vignette: next vs classic, one gate.

Every other benchmark probe bands each flavor SEPARATELY (is-this-flavor-in-
its-own-corridor), so the two flavors could drift arbitrarily far apart without
any gate noticing - the black-water/cyan-sky regression class. This test boots
the SAME scene on BOTH flavors' players, captures the same deterministic frame,
and compares broad scene REGIONS (per-scene profiles in PROFILES - sky band,
water/mirror bands, terrain band) between the two images:

  * per-region MEAN colour must agree within that region's measured
    tolerance-parity corridor (loose enough for the documented
    BRDF/vertex-vs-pixel differences, tight enough that a black,
    inverted-hue or washed-out region on ONE flavor fails - and, on the
    mirror scene, that a planar mirror-strength drift fails);
  * scenes carrying the streak contract: both flavors must show the water's
    bright sun highlight (the specular streak - its absence on one flavor
    was a real regression).

Capturing and comparing are separable, because one machine does not always
hold both flavors:

  * BOTH PLAYERS (--player-next + --player-classic): capture both, compare,
    one run. The developer road, and the ctest; the sibling flavor's player
    path is passed in and the test SKIPS (exit 77) when it is not built.
  * CAPTURE ONE (--capture next|classic): boot that flavor's player, write
    its frame, stop. What a per-flavor build job can do.
  * COMPARE TWO CAPTURES (--compare-shots --shot-next + --shot-classic): the
    comparison alone, on frames captured elsewhere. A missing or empty
    capture FAILS - a parity gate that compared nothing must not report
    parity.

A region mean says THAT two frames disagree, never where, so every comparison
also reports the largest 8-connected region of differing pixels across the
whole frame (parity_diff), and any comparison with structure over the
threshold - green or red - leaves a DIFF IMAGE beside
the next capture - `next.diff.png`, the delta painted over the scene. The
region size is reported, not gated: see parity_diff for why.

Pure stdlib (the sibling pixel test's PNG decoder). Exit codes: 0 pass,
1 fail, 77 skip.
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
from run_benchmark_pixel_test import decode_png, pixel  # noqa: E402


#: What the CI rasterizer pair (software GL against software Vulkan) measures
#: for these corridors, from a green run of the parity job - max channel delta
#: against the corridor:
#:
#:   lake        sky 7/28    terrain 4/28   water 26/28
#:   mirrorlake  sky 8/22    shore 4/20     watermirror 15/20, 19/20
#:               rockmirror 30/33           water_open 47/55
#:
#: The corridors hold on that pair, several of them with little room: the lake
#: water sits two levels under its 28 and the mirror rock three under its 33.
#: They were measured on the developer pair, so nothing here is tightened
#: without a measurement - a corridor moved by guesswork blocks merges.
#:
#: per-scene comparison profiles: named regions as frame fractions
#: (fx0, fy0, fx1, fy1, corridor) plus whether the scene carries the
#: sun-streak contract. Corridors are tolerance-parity: measured on the
#: deterministic capture recipe, wide enough for the documented residual
#: flavor differences and tight enough that a black/inverted/washed region
#: on one flavor (deltas 120+) or a mirror-strength drift fails.
PROFILES = {
    # the refraction-only lake framing: the camera looks low ACROSS the water
    # toward the sun - sky above, the shore island band mid-frame (sampled
    # left of the sun-streak column, whose breadth differs legitimately
    # between the flavors' specular models), open water across the lower
    # half. The 28 corridor's history: the classic atmospheric object fog
    # (the generated materials' fog stage + the water programs' fog block
    # now run the default backend's exact haze-colour/transmittance
    # formulas) dropped the shore band from its historical ~32 delta to a
    # measured 10 and the sky to 7; the ratio-true HDR water mirror dropped
    # the water band from its ~20 to a measured 5. The showcase lake then
    # opened its water to opacity 0.55 (the refracted scene carries 45% of
    # the compose), which exposes the remaining flavor seams at 3x their
    # former weight; the specular-hemisphere lane + the shared mirror
    # source trimmed the water band to a measured 25 (sky 7, terrain 4).
    # The residual is the flavors' ENV-FILL sampling of the environment
    # chain's deep mips (the classic stage reads the mathematical face
    # average, the default backend's native env sample reads measurably
    # darker from the SAME chain bytes - the named successor task). The
    # corridor tracks that honestly and re-tightens when that seam closes.
    "lake.oscene": {
        "regions": {
            # the open sky above the horizon
            "sky": (0.35, 0.10, 0.95, 0.24, 28.0),
            # the shore island band (left half, clear of the streak column)
            "terrain": (0.25, 0.31, 0.44, 0.41, 28.0),
            # the open water foreground, flanking the streak
            "water": (0.05, 0.55, 0.35, 0.85, 28.0),
        },
        "streak": True,
    },
    # the planar-mirror sibling: a low, close camera over a calm surface, the
    # widened shore ridge spanning the far edge, waterline rocks. The tight
    # gates are where BOTH flavors mirror the SAME content: the waterline
    # strips (the mirrored ridge, measured delta 6-8) and the rock-mirror
    # band. That band sits in the streak column, and with BOTH flavors
    # carrying the sun glint it measures delta 30 on the CI pair - moving
    # the box off the streak is worse, because the open water flanking it
    # carries the raw body-brightness seam at 36-47 (classic's unlit
    # painted body against the default backend's lit surface, the named
    # open item). The 33 corridor sits between that measured 30 and the
    # old 3x-strong mirror calibration's 35, which was measured WITHOUT
    # the streak and only rises with it - so a mirror-strength drift still
    # breaches, and the corridor re-tightens when the body seam closes.
    # The open lower water mirrors the MID/HIGH sky, where two documented
    # approximations diverge: the flavors' sky-dome colour away from the
    # horizon (a seam nothing but a mirror ever sees - the direct sky bands
    # match at delta 9) and the classic mirror's screen-UV paint, which
    # stretches the mirrored ridge further down than the default backend's
    # true projective mapping. Measured 44 healthy vs 69 under the
    # 3x-strong calibration - the 55 corridor bites on strength drift and
    # re-tightens when the sky-dome seam closes. The streak contract HOLDS
    # here: the glint is a DIRECT sun specular on both flavors (classic's
    # analytic Blinn term, next's GGX lobe at the shared water roughness),
    # so it renders regardless of what the mirror shows - ridge occlusion
    # applies to the mirrored sun, never to the glint. This gate is what
    # catches the glint collapsing when something narrows the lobe.
    "mirrorlake.oscene": {
        "regions": {
            "sky": (0.30, 0.02, 0.95, 0.08, 22.0),
            "shore": (0.15, 0.12, 0.85, 0.22, 20.0),
            "watermirror_l": (0.08, 0.25, 0.40, 0.28, 20.0),
            "watermirror_r": (0.60, 0.25, 0.92, 0.28, 20.0),
            "rockmirror": (0.38, 0.38, 0.52, 0.50, 33.0),
            "water_open": (0.05, 0.36, 0.35, 0.52, 55.0),
        },
        "streak": True,
    },
}


def fail(message):
    print("crossflavor_parity: FAIL: " + message)
    sys.exit(1)


def skip(message):
    print("crossflavor_parity: SKIP: " + message)
    sys.exit(77)


def capture(player, repo, scene, shot, out_dir, frames):
    env = dict(os.environ)
    env.update({
        "ORKIGE_DEMO_FRAMES": str(frames),
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": out_dir,
        # deterministic frame: freeze the wall-time orbit, un-cap the ramp
        "ORKIGE_CVARS": "benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0",
    })
    result = subprocess.run(
        [player, scene, "--project", "projects/benchmark"],
        cwd=repo, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=420)
    if result.returncode != 0:
        fail(f"{player} exited {result.returncode}:\n" +
             result.stdout.decode("utf-8", "replace")[-2000:])
    if not os.path.exists(shot):
        fail(f"{player} wrote no screenshot to {shot}")
    return decode_png(shot)


def region_mean(img, x0, y0, x1, y1, step=4):
    width, height, channels, data = img
    total = [0.0, 0.0, 0.0]
    count = 0
    for y in range(y0, min(y1, height), step):
        for x in range(x0, min(x1, width), step):
            r, g, b = pixel(data, channels, width, x, y)
            total[0] += r
            total[1] += g
            total[2] += b
            count += 1
    return tuple(t / max(count, 1) for t in total)


def region_max_luma(img, x0, y0, x1, y1, step=2):
    width, height, channels, data = img
    best = 0.0
    for y in range(y0, min(y1, height), step):
        for x in range(x0, min(x1, width), step):
            r, g, b = pixel(data, channels, width, x, y)
            luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
            best = max(best, luma)
    return best


def load_capture(label, path):
    """Decode a capture taken elsewhere; refuse when there is nothing there.

    A comparison handed an absent or truncated frame would otherwise either
    crash obscurely or - worse - be skipped past. Silence is not parity.
    """
    if not os.path.exists(path):
        fail(f"the {label} capture does not exist: {path}")
    if os.path.getsize(path) == 0:
        fail(f"the {label} capture is empty: {path}")
    try:
        return decode_png(path)
    except Exception as error:
        # any decode complaint at all is reported verbatim - the point is that
        # an unreadable capture ends the run, not which byte was wrong
        fail(f"the {label} capture is unreadable ({path}): {error}")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo")
    parser.add_argument("--player-next")
    parser.add_argument("--player-classic")
    parser.add_argument("--dir")
    parser.add_argument("--scene", default="scenes/lake.oscene")
    parser.add_argument("--frames", type=int, default=90)
    parser.add_argument("--capture", choices=("next", "classic"),
                        help="boot that flavor's player and write its frame "
                             "to <dir>/<flavor>.png, then stop")
    parser.add_argument("--compare-shots", action="store_true",
                        help="compare two captures taken elsewhere; no "
                             "player is run")
    parser.add_argument("--shot-next",
                        help="the next flavor's capture (--compare-shots)")
    parser.add_argument("--shot-classic",
                        help="the classic flavor's capture (--compare-shots)")
    parser.add_argument("--diff-dir",
                        help="where a failing comparison's diff image lands "
                             "(default: beside the next capture)")
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the pure parts and exit")
    return parser.parse_args(argv)


def capture_one(args):
    """--capture: one flavor's frame, written where a build job can carry it."""
    player = (args.player_next if args.capture == "next"
              else args.player_classic)
    if not player:
        fail(f"--capture {args.capture} needs --player-{args.capture}")
    if not args.repo or not args.dir:
        fail("--capture needs --repo and --dir")
    if not os.path.exists(player):
        # asked for explicitly, so absence is a failure rather than a skip -
        # the caller wanted this capture and there is none
        fail(f"{args.capture} player not built: {player}")
    os.makedirs(args.dir, exist_ok=True)
    shot = os.path.join(args.dir, args.capture + ".png")
    capture(player, args.repo, args.scene, shot, args.dir, args.frames)
    print(f"crossflavor_parity: captured {args.capture} {args.scene} -> {shot}")
    return 0


def main(argv=None):
    args = parse_args(argv)
    if args.selftest:
        return selftest()
    if args.capture:
        return capture_one(args)

    if args.compare_shots:
        if not (args.shot_next and args.shot_classic):
            fail("--compare-shots needs --shot-next and --shot-classic")
        img_next = load_capture("next", args.shot_next)
        img_classic = load_capture("classic", args.shot_classic)
        kept_in = os.path.dirname(os.path.abspath(args.shot_next))
        shot_next = args.shot_next
    else:
        for name in ("repo", "player_next", "player_classic", "dir"):
            if not getattr(args, name):
                fail("--" + name.replace("_", "-") + " is required")
        if not os.path.exists(args.player_next):
            skip("next player not built: " + args.player_next)
        if not os.path.exists(args.player_classic):
            skip("classic player not built: " + args.player_classic)

        os.makedirs(args.dir, exist_ok=True)
        shot_next = os.path.join(args.dir, "next.png")
        shot_classic = os.path.join(args.dir, "classic.png")
        img_next = capture(args.player_next, args.repo, args.scene, shot_next,
                           args.dir, args.frames)
        img_classic = capture(args.player_classic, args.repo, args.scene,
                              shot_classic, args.dir, args.frames)
        kept_in = args.dir

    return compare_captures(img_next, img_classic, args.scene, kept_in,
                            shot_next, args.diff_dir)


def compare_captures(img_next, img_classic, scene, kept_in,
                     shot_next=None, diff_dir=None):
    if img_next[0] != img_classic[0] or img_next[1] != img_classic[1]:
        fail(f"capture sizes differ: {img_next[0]}x{img_next[1]} vs "
             f"{img_classic[0]}x{img_classic[1]}")
    width, height = img_next[0], img_next[1]

    # the SHAPE of the whole-frame disagreement, printed on every run - a
    # green log records the healthy value, which is what a corridor would
    # have to be measured from. Reported, never gated (parity_diff).
    dmap = parity_diff.delta_map(img_classic, img_next)
    spatial = parity_diff.spatial_summary(dmap)
    print("crossflavor_parity: whole frame: "
          + parity_diff.describe(spatial, width * height))

    destination = parity_diff.diff_path(
        shot_next or os.path.join(kept_in, "next.png"), diff_dir)
    # the picture is written whenever there is structure, ON GREEN TOO: the
    # probe verdicts can all sit inside their corridors while the whole-frame
    # report above names a large differing region, and the diff image is how
    # that region gets LOOKED AT instead of discovered later. A frame with no
    # pixel over the threshold writes nothing
    if spatial.over:
        written = parity_diff.try_write_diff(destination, dmap, img_classic)
        if written:
            print(f"crossflavor_parity: diff image {written}")
    else:
        parity_diff.drop_stale_diff(destination)

    def fail_with_diff(message):
        """Refuse; the picture that explains the refusal is already beside
        the capture (written above for any frame with structure)."""
        fail(message + (f" - diff image {destination}"
                        if os.path.exists(destination) else ""))

    def sx(fraction):
        return int(width * fraction)

    def sy(fraction):
        return int(height * fraction)

    profile = PROFILES.get(os.path.basename(scene))
    if profile is None:
        fail(f"no region profile for scene '{scene}' - add one to "
             "PROFILES with measured corridors")
    regions = {name: (sx(fx0), sy(fy0), sx(fx1), sy(fy1), tolerance)
               for name, (fx0, fy0, fx1, fy1, tolerance)
               in profile["regions"].items()}
    for name, (x0, y0, x1, y1, tolerance) in regions.items():
        mean_next = region_mean(img_next, x0, y0, x1, y1)
        mean_classic = region_mean(img_classic, x0, y0, x1, y1)
        deltas = [abs(a - b) for a, b in zip(mean_next, mean_classic)]
        print(f"crossflavor_parity: {name}: next=({mean_next[0]:.0f},"
              f"{mean_next[1]:.0f},{mean_next[2]:.0f}) classic="
              f"({mean_classic[0]:.0f},{mean_classic[1]:.0f},"
              f"{mean_classic[2]:.0f}) delta=({deltas[0]:.0f},"
              f"{deltas[1]:.0f},{deltas[2]:.0f}) tol={tolerance:.0f}")
        if max(deltas) > tolerance:
            fail_with_diff(f"region '{name}' diverges between flavors: max "
                           f"channel delta {max(deltas):.0f} > {tolerance} - "
                           "the flavors no longer show the same scene "
                           f"(capture pair kept in {kept_in})")

    if profile.get("streak"):
        # the sun's specular streak on the water: both flavors must carry a
        # bright highlight down the frame centre (its absence on one flavor
        # was the flat-lifeless-water regression)
        streak_box = (sx(0.40), sy(0.35), sx(0.62), sy(0.75))
        STREAK_MIN = 200.0
        for label, img in (("next", img_next), ("classic", img_classic)):
            luma = region_max_luma(img, *streak_box)
            print(f"crossflavor_parity: sun-streak max luma [{label}] = "
                  f"{luma:.0f}")
            if luma < STREAK_MIN:
                fail_with_diff(f"{label} shows no bright sun highlight on the "
                               f"water (max luma {luma:.0f} < {STREAK_MIN}) - "
                               "the specular streak is missing on this flavor")

    parity_diff.drop_stale_diff(destination)
    print("crossflavor_parity: PASS")
    return 0


# --- selftest ---------------------------------------------------------------

def write_png(path, width, height, fill, poke=None):
    """Write a minimal 8-bit RGB PNG of one colour (selftest fixture).

    poke=(x, y, colour) recolours a 2x2 block - the selftest's stand-in
    for the sun highlight the streak contract requires inside its centre
    band. A block rather than a pixel: the streak reader samples every
    second pixel, and a block covers both parities by construction.
    """
    raw = bytearray()
    for row in range(height):
        raw.append(0)                       # filter type None
        raw.extend(bytes(fill) * width)
        if poke is not None and row in (poke[1], poke[1] + 1):
            for dx in (0, 1):
                base = len(raw) - (width - (poke[0] + dx)) * 3
                raw[base:base + 3] = bytes(poke[2])

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw))))
        handle.write(chunk(b"IEND", b""))


def run_quiet(argv):
    """Run main() swallowing its report - a passing selftest logs no FAIL."""
    captured = io.StringIO()
    code = 0
    try:
        with contextlib.redirect_stdout(captured):
            code = main(argv)
    except SystemExit as exit_code:
        code = exit_code.code
    return code, captured.getvalue()


def expect_refusal(what, argv, names):
    code, said = run_quiet(argv)
    if code != 1:
        raise AssertionError(f"{what} returned {code}, must refuse with 1")
    if names not in said:
        raise AssertionError(f"{what} refused without naming {names}: {said}")


def selftest():
    scratch = tempfile.mkdtemp(prefix="orkige_crossflavor_selftest_")
    shot_next = os.path.join(scratch, "next.png")
    shot_classic = os.path.join(scratch, "classic.png")

    # the shared diagnosis (clustering, the heat ramp, the writer read back
    # through this driver's own decoder)
    parity_diff.selftest_pure()
    parity_diff.selftest_roundtrip(decode_png, scratch)

    # the mirror scene carries the streak contract like the lake does, so
    # every fixture pokes a bright "sun" pixel into the streak band; the
    # flat fill exercises the region comparison around it
    mirror = "scenes/mirrorlake.oscene"
    SUN = (32, 32, (255, 255, 255))
    diff_image = os.path.join(scratch, "next.diff.png")
    write_png(shot_next, 64, 64, (80, 90, 100), poke=SUN)
    write_png(shot_classic, 64, 64, (80, 90, 100), poke=SUN)
    code, said = run_quiet(["--compare-shots", "--scene", mirror,
                            "--shot-next", shot_next,
                            "--shot-classic", shot_classic])
    assert code == 0, said
    # matching frames still report their shape, and leave no picture behind
    assert "whole frame: no pixel over" in said, said
    assert not os.path.exists(diff_image), "a clean pair wrote a diff image"

    # a region that diverges beyond its corridor fails, names the region and
    # leaves the diff image beside the next capture
    write_png(shot_classic, 64, 64, (200, 90, 100), poke=SUN)
    code, said = run_quiet(["--compare-shots", "--scene", mirror,
                            "--shot-next", shot_next,
                            "--shot-classic", shot_classic])
    assert code == 1 and "diverges between flavors" in said, said
    # the whole frame minus the 2x2 sun block the two fixtures share
    assert "largest region 4092px" in said, said
    assert os.path.exists(diff_image) and diff_image in said, said
    assert decode_png(diff_image)[0] == 64

    # ... into a directory of its own when asked
    elsewhere = os.path.join(scratch, "diffs")
    assert run_quiet(["--compare-shots", "--scene", mirror,
                      "--shot-next", shot_next,
                      "--shot-classic", shot_classic,
                      "--diff-dir", elsewhere])[0] == 1
    assert os.path.exists(os.path.join(elsewhere, "next.diff.png"))

    # agreeing again takes the stale picture of the old divergence away
    write_png(shot_classic, 64, 64, (80, 90, 100), poke=SUN)
    assert run_quiet(["--compare-shots", "--scene", mirror,
                      "--shot-next", shot_next,
                      "--shot-classic", shot_classic])[0] == 0
    assert not os.path.exists(diff_image), "a passing pair kept a stale diff"

    # captures of different sizes are not comparable
    odd = os.path.join(scratch, "odd.png")
    write_png(odd, 32, 32, (80, 90, 100), poke=(16, 16, (255, 255, 255)))
    expect_refusal("captures of different sizes",
                   ["--compare-shots", "--scene", mirror,
                    "--shot-next", shot_next, "--shot-classic", odd],
                   "capture sizes differ")

    # a scene with no measured profile is refused rather than guessed at
    expect_refusal("an unprofiled scene",
                   ["--compare-shots", "--scene", "scenes/nowhere.oscene",
                    "--shot-next", shot_next, "--shot-classic", shot_classic],
                   "no region profile")

    # THE refusals: comparing nothing must never read as parity
    absent = os.path.join(scratch, "absent.png")
    expect_refusal("a missing capture",
                   ["--compare-shots", "--scene", mirror,
                    "--shot-next", shot_next, "--shot-classic", absent],
                   absent)
    blank = os.path.join(scratch, "blank.png")
    open(blank, "wb").close()
    expect_refusal("an empty capture",
                   ["--compare-shots", "--scene", mirror,
                    "--shot-next", blank, "--shot-classic", shot_classic],
                   "is empty")
    garbage = os.path.join(scratch, "garbage.png")
    with open(garbage, "wb") as handle:
        handle.write(b"not a png at all")
    expect_refusal("an unreadable capture",
                   ["--compare-shots", "--scene", mirror,
                    "--shot-next", garbage, "--shot-classic", shot_classic],
                   "unreadable")
    expect_refusal("one capture without the other",
                   ["--compare-shots", "--shot-next", shot_next],
                   "--shot-classic")

    # an explicitly asked-for capture with no player is a failure, not a skip
    expect_refusal("a capture with no player",
                   ["--capture", "classic", "--repo", scratch,
                    "--dir", scratch,
                    "--player-classic", "/nonexistent/player"],
                   "not built")

    # the run road keeps its honest skip when a sibling tree is unbuilt
    assert run_quiet(["--repo", scratch, "--dir", scratch,
                      "--player-next", "/nonexistent/next",
                      "--player-classic", "/nonexistent/classic"])[0] == 77

    # argument routing
    parsed = parse_args(["--compare-shots", "--shot-next", "a",
                         "--shot-classic", "b"])
    assert parsed.compare_shots and parsed.shot_next == "a"
    assert parse_args(["--capture", "next"]).capture == "next"
    assert parse_args(["--selftest"]).selftest is True

    shutil.rmtree(scratch, ignore_errors=True)
    print("run_crossflavor_parity_test: selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
