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

A profile may also PIN the capture: a scene whose behaviour differs between
the flavors by DESIGN gets that knob fixed to one value on both sides, so the
comparison reads the look and not the designed capability difference (which
stays gated per-flavor by its own probe). The pin travels with the recipe,
so every road captures under it.

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


#: Max channel delta measured on the DEVELOPER pair (Metal against desktop GL):
#:
#:   lake        sky 0      terrain 2      water 6
#:   mirrorlake  sky 0      shore 3        watermirror 7 left, 8 right
#:               rockmirror 13             water_open 35
#:   lumens      sky 0.0    terrain 0.2    pools 0.1   foreground 0.2
#:               (at the pinned equal lamp count - see the profile)
#:
#: The CI pair is a different software rasterizer on each side and measures its
#: own numbers on the parity job; the corridors below sit at roughly twice the
#: worst measurement rather than at it, so both pairs fit. Nothing here is
#: tightened without a measurement - a corridor moved by guesswork blocks
#: merges - and nothing is loosened to make a run pass.
#:
#: per-scene comparison profiles: named regions as frame fractions
#: (fx0, fy0, fx1, fy1, corridor) plus whether the scene carries the
#: sun-streak contract. Corridors are tolerance-parity: measured on the
#: deterministic capture recipe, wide enough for the documented residual
#: flavor differences and tight enough that a black/inverted/washed region
#: on one flavor (deltas 120+) or a mirror-strength drift fails.
#:
#: A profile may also carry `env`: extra environment a capture of THAT scene
#: boots under, applied on both roads (the run-both developer road and the
#: per-flavor `--capture` a build matrix uses), because a pin is part of the
#: recipe rather than of the comparison. It exists for one situation only - a
#: scene whose default behaviour differs between the flavors BY DESIGN, where
#: comparing the defaults would measure the designed difference instead of the
#: look. A scene with no `env` boots byte-identically to before.
PROFILES = {
    # the refraction-only lake framing: the camera looks low ACROSS the water
    # toward the sun - sky above, the shore island band mid-frame (sampled
    # left of the sun-streak column, whose breadth differs legitimately
    # between the flavors' specular models), open water across the lower
    # half. The water band is the tight one because the lake's opacity is
    # 0.55: the refracted scene carries 45% of the compose, so every seam in
    # the transmitted image lands there at nearly half weight. Both flavors
    # now transmit that scene at its true LINEAR radiance - the default
    # backend decodes the opaque scene target out of the pipeline's display
    # space before its water refracts it, the classic grab-pass squares its
    # display-space grab for the same reason - and the band measures 6.
    "lake.oscene": {
        "regions": {
            # the open sky above the horizon (measures 0 on the developer
            # Metal/GL pair, 7 in the CI record; corridor at 2x the worst)
            "sky": (0.35, 0.10, 0.95, 0.24, 14.0),
            # the shore island band (left half, clear of the streak column;
            # measures 2 today, 10 in the fog-work record - the corridor
            # clears the historical worst at 1.6x)
            "terrain": (0.25, 0.31, 0.44, 0.41, 16.0),
            # the open water foreground, flanking the streak: measures 6 with
            # both flavors transmitting true linear radiance (26 while the
            # default backend's water read its refraction source as linear
            # when the source was display-encoded). Corridor at 2.3x the
            # measurement, so the CI pair's own rasterizer spread fits
            "water": (0.05, 0.55, 0.35, 0.85, 14.0),
        },
        "streak": True,
    },
    # the planar-mirror sibling: a low, close camera over a calm surface, the
    # widened shore ridge spanning the far edge, waterline rocks. The tight
    # gates are where BOTH flavors mirror the SAME content: the waterline
    # strips (the mirrored ridge, measured delta 7-8) and the rock-mirror
    # band. That band sits in the streak column, and with BOTH flavors
    # carrying the sun glint it measures 13 on the developer pair - moving
    # the box off the streak is worse, because the open water flanking it
    # carries the raw body-brightness seam (classic's unlit painted body
    # against the default backend's lit surface, the named open item). The
    # corridor clears the CI pair's own spread over that measurement and
    # still breaches on a mirror-strength drift, which the 3x-strong
    # calibration measured at 35 WITHOUT the streak and only rises with it.
    # The open lower water mirrors the MID/HIGH sky, where two documented
    # approximations diverge: the flavors' sky-dome colour away from the
    # horizon (a seam nothing but a mirror ever sees - the direct sky bands
    # match at delta 0) and the classic mirror's screen-UV paint, which
    # stretches the mirrored ridge further down than the default backend's
    # true projective mapping. On top of those, classic's mirror program
    # paints an UNLIT body while the default backend's is a lit surface, so
    # this band stays the widest corridor on either scene: measured 35
    # against 69 under the 3x-strong calibration, so the corridor still
    # bites on strength drift and re-tightens when the body seam closes.
    # The streak contract HOLDS
    # here: the glint is a DIRECT sun specular on both flavors (classic's
    # analytic Blinn term, next's GGX lobe at the shared water roughness),
    # so it renders regardless of what the mirror shows - ridge occlusion
    # applies to the mirrored sun, never to the glint. This gate is what
    # catches the glint collapsing when something narrows the lobe.
    "mirrorlake.oscene": {
        "regions": {
            # direct bands: sky measures 0 today against the 8-9 record,
            # shore 3 against the 6-8 strip record; corridors at ~2x the worst
            "sky": (0.30, 0.02, 0.95, 0.08, 16.0),
            "shore": (0.15, 0.12, 0.85, 0.22, 16.0),
            # the mirrored-ridge strips: 7 left, 8 right
            "watermirror_l": (0.08, 0.25, 0.40, 0.28, 15.0),
            "watermirror_r": (0.60, 0.25, 0.92, 0.28, 18.0),
            # measures 13 (17 before both flavors transmitted the refracted
            # scene at true linear radiance)
            "rockmirror": (0.38, 0.38, 0.52, 0.50, 28.0),
            # measures 35 (39 before the same change); the body seam keeps it
            # the widest corridor here
            "water_open": (0.05, 0.36, 0.35, 0.52, 50.0),
        },
        "streak": True,
    },
    # the NIGHT vignette, and the only scene here whose look is built out of
    # MANY dynamic lights: a moonlit terrace under coloured lamp pools.
    #
    # THE PIN. The director's lamp ramp climbs to the flavor's OWN queried
    # light budget (engine:getLightBudget - next 96, classic 30), which is a
    # designed capability difference, not a look divergence. Left at their
    # defaults the two frames differ by the lamp COUNT alone: measured sky 0,
    # terrain 18, pools 29, foreground 21 - a corridor wide enough to pass
    # that would gate nothing. A look-parity gate measures LOOK, so this
    # profile pins BOTH flavors to an equal lamp count
    # (benchmark.lightCeiling 12, seeded through `env` below) and compares the
    # same picture on both sides. The capability itself stays covered
    # per-flavor by the lumens probe in run_benchmark_scene_probe.py, which
    # asserts each flavor ramps to its own queried budget and never past it -
    # so pinning here removes nothing from the suite.
    #
    # At the pinned count the frames agree to a max REGION delta of 0.2 on the
    # developer pair - sky 0.0, terrain 0.2, pools 0.1, foreground 0.2, whole
    # frame max channel delta 18 with no pixel over 48. Twice that would be a
    # sub-level corridor, and the pair this gate runs on in CI is a different
    # software rasterizer on each side, which the one directly comparable
    # reading in this file measures at 7 where the developer pair reads 0 (the
    # lake sky band). So each corridor here is twice the LARGER of the two:
    # twice its own measurement, floored at twice that recorded 7. All four
    # measurements sit under the floor, so all four corridors are 14.
    #
    # That is still a tight gate for this scene, and the pin is what makes it
    # so: the designed per-flavor difference the pin removes measures 17.9 /
    # 28.7 / 20.5 on the three lit bands, ABOVE the corridor - so if the seed
    # ever stopped landing on one side, this gate fails rather than quietly
    # comparing two different pictures. A black night, a lost moonlit fill or
    # an unlit pool (deltas 60-100+) breaches by a wide margin.
    # No streak contract: there is no sun and no water here.
    "lumens.oscene": {
        "regions": {
            # the night sky above the terrace - the darkest band, and the one
            # that says this is still a NIGHT (measures 0.0)
            "sky": (0.05, 0.06, 0.95, 0.28, 14.0),
            # the moonlit terrain band: the overhead fill's own reading
            # (measures 0.2)
            "terrain": (0.10, 0.46, 0.90, 0.53, 14.0),
            # the coloured lamp pools - the many-lights band the pin exists
            # for, and the brightest thing this scene has to say (measures 0.1)
            "pools": (0.20, 0.60, 0.80, 0.78, 14.0),
            # the near foreground, lit by the closest lamps (measures 0.2)
            "foreground": (0.10, 0.85, 0.90, 0.98, 14.0),
        },
        "streak": False,
        # equal lamp count on both flavors - see THE PIN above. Seeded as a
        # cvar boot override (underscores become dots), which is held and
        # re-applied when the director registers the cvar, so the seed lands
        # whatever order the script runs in
        "env": {"ORKIGE_CVAR_benchmark_lightCeiling": "12"},
    },
}


def fail(message):
    print("crossflavor_parity: FAIL: " + message)
    sys.exit(1)


def skip(message):
    print("crossflavor_parity: SKIP: " + message)
    sys.exit(77)


def capture_environment(scene, shot, out_dir, frames, base=None):
    """The environment one capture boots under (pure).

    Split out of `capture` so the recipe - the deterministic freeze every
    scene shares, plus the scene's own pins - can be read and tested without
    booting a player. The scene's `env` is applied LAST and keyed separately
    from the shared seed, so a profile pin can never silently drop the
    determinism the whole comparison rests on.
    """
    env = dict(os.environ if base is None else base)
    env.update({
        "ORKIGE_DEMO_FRAMES": str(frames),
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": out_dir,
        # deterministic frame: freeze the wall-time orbit, un-cap the ramp
        "ORKIGE_CVARS": "benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0",
    })
    profile = PROFILES.get(os.path.basename(scene))
    if profile:
        env.update(profile.get("env", {}))
    return env


def capture(player, repo, scene, shot, out_dir, frames):
    env = capture_environment(scene, shot, out_dir, frames)
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


def selftest_capture_recipe():
    """The recipe every capture boots under, and the scenes that pin it."""
    base = {"PATH": "/usr/bin"}
    shared = ("ORKIGE_DEMO_FRAMES", "ORKIGE_DEMO_SCREENSHOT",
              "ORKIGE_PROGRESS_RESET", "ORKIGE_PROGRESS_DIR", "ORKIGE_CVARS")

    # every scene gets the deterministic freeze, and the caller's environment
    # survives it
    for scene in ("scenes/lake.oscene", "scenes/lumens.oscene",
                  "scenes/nowhere.oscene"):
        env = capture_environment(scene, "/tmp/s.png", "/tmp/d", 90, base)
        assert env["PATH"] == "/usr/bin", scene
        for key in shared:
            assert key in env, (scene, key)
        assert env["ORKIGE_DEMO_FRAMES"] == "90", scene
        assert "benchmark.cameraOrbit=0" in env["ORKIGE_CVARS"], scene

    # the two water scenes carry NO pin: their recipe is byte-identical to
    # what it has always been, which is what keeps their captures comparable
    # with the ones already measured
    plain = capture_environment("scenes/lake.oscene", "/tmp/s.png", "/tmp/d",
                                90, base)
    mirror = capture_environment("scenes/mirrorlake.oscene", "/tmp/s.png",
                                 "/tmp/d", 90, base)
    reference = capture_environment("scenes/unprofiled.oscene", "/tmp/s.png",
                                    "/tmp/d", 90, base)
    assert plain == reference, plain
    assert mirror == reference, mirror

    # the night scene pins the lamp count on BOTH flavors, and the pin is an
    # addition - it never displaces the shared deterministic seed
    night = capture_environment("scenes/lumens.oscene", "/tmp/s.png", "/tmp/d",
                                90, base)
    assert night["ORKIGE_CVAR_benchmark_lightCeiling"] == "12", night
    assert night["ORKIGE_CVARS"] == reference["ORKIGE_CVARS"], night
    assert set(night) - set(reference) == {"ORKIGE_CVAR_benchmark_lightCeiling"}

    # and the profile itself: four measured regions, no streak to look for
    profile = PROFILES["lumens.oscene"]
    assert profile["streak"] is False
    assert set(profile["regions"]) == {"sky", "terrain", "pools", "foreground"}
    for name, (fx0, fy0, fx1, fy1, tolerance) in profile["regions"].items():
        assert 0.0 <= fx0 < fx1 <= 1.0, name
        assert 0.0 <= fy0 < fy1 <= 1.0, name
        # tight enough to breach on the designed per-flavor lamp-count
        # difference the pin removes (measured 17.9 on the least of the three
        # lit bands), so an unlanded pin fails this gate instead of quietly
        # comparing two different pictures
        assert 0.0 < tolerance < 17.0, name


def selftest():
    scratch = tempfile.mkdtemp(prefix="orkige_crossflavor_selftest_")
    shot_next = os.path.join(scratch, "next.png")
    shot_classic = os.path.join(scratch, "classic.png")

    # the shared diagnosis (clustering, the heat ramp, the writer read back
    # through this driver's own decoder)
    parity_diff.selftest_pure()
    parity_diff.selftest_roundtrip(decode_png, scratch)
    selftest_capture_recipe()

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

    # a scene with NO streak contract is compared on its regions alone: the
    # night vignette has no sun and no water, so a frame carrying no bright
    # highlight anywhere is a pass there and would be a failure on the water
    # scenes
    night = "scenes/lumens.oscene"
    dark_next = os.path.join(scratch, "night_next.png")
    dark_classic = os.path.join(scratch, "night_classic.png")
    write_png(dark_next, 64, 64, (20, 24, 28))
    write_png(dark_classic, 64, 64, (20, 24, 28))
    code, said = run_quiet(["--compare-shots", "--scene", night,
                            "--shot-next", dark_next,
                            "--shot-classic", dark_classic])
    assert code == 0, said
    assert "sun-streak" not in said, said
    # ... and its regions still gate
    write_png(dark_classic, 64, 64, (140, 24, 28))
    code, said = run_quiet(["--compare-shots", "--scene", night,
                            "--shot-next", dark_next,
                            "--shot-classic", dark_classic])
    assert code == 1 and "diverges between flavors" in said, said

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
