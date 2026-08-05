#!/usr/bin/env python3
"""Browser render parity: the SAME flavor, one platform apart.

The browser runs the CLASSIC flavor through WebGL2/GLES3 - there is no
next-flavor browser build - so a "desktop-next vs browser" comparison would
span TWO seams at once: the FLAVOR seam (next vs classic) and the PLATFORM
seam (desktop GL vs WebGL/Emscripten). This gate takes the second seam alone:

    browser classic  vs  desktop classic  -  same flavor, same scene,
                                             same simulated frame

so a divergence names the WebGL/GLES3/Emscripten tier and nothing else. The
flavor seam already has its own gate (run_crossflavor_parity_test.py), and
next-vs-browser is the COMPOSITION of the two. Gating that composition
directly was considered and rejected: its corridor would have to absorb both
seams' documented residuals at once, which is wide enough to pass a real
regression in either one, while telling a reader nothing about which side
moved. Two narrow gates that compose beat one wide gate that cannot attribute.

What is compared, and why it is comparable:

  * ONE SCENE, one project, one flavor. The browser boots an export of the
    benchmark project whose main scene is the vignette under test (a page has
    no argv, so the boot scene is the manifest's); the desktop player boots
    that same scene file directly.
  * THE SAME SIMULATED INSTANT. An automated run advances the world by the
    fixed AppHost::AUTOMATED_FRAME_DELTA tick, so frame N always lands at the
    same simulated time whatever the host's pace. The desktop capture is
    frame-pinned (the player's frame-60 framebuffer dump). The browser one is
    NOT: the page's canvas goes black the moment the run exits, and a page
    carries no frame counter to wait on, so the browser frame is taken LATE,
    at an arbitrary frame of a still-running page. What that costs is
    measured, not assumed - see WEB_PROFILES.
  * THE SAME PIXELS. The viewport, the device scale factor and the player's
    ORKIGE_WINDOW_SIZE are all pinned to CAPTURE_SIZE, so the canvas backing
    store, its CSS box and the captured page are one and the same rectangle:
    no browser scaling sits between the render and the comparison.
  * SHADOWS OFF ON BOTH SIDES. The classic backend refuses the RTSS shadow
    pass on a SOFTWARE WebGL rasterizer (@see Docs/web-export.md), which is
    what a GPU-less CI browser always gets and a developer's GPU-backed
    browser never does. Left at its default the compared image would therefore
    depend on the machine, not on the code. Pinning r.shadowQuality=off makes
    both sides render the same feature set everywhere; the shadow tier is
    gated on the desktop suites and, on the browser, by the capability
    refusal itself.

Capturing and comparing are separable, because no machine holds both sides at
once (a build tree carries one flavor, and the browser side needs a wasm tree
plus a browser):

  * BOTH SIDES (--engine-build + --player-desktop): capture both, compare, one
    run. The developer road, on a machine carrying a wasm tree, the host's
    classic tree and a headless browser; SKIPS (exit 77) when any of the three
    is missing.
  * CAPTURE ONE (--capture web|desktop): boot that side, write its frame,
    stop. What a per-platform CI job can do, and what the web tree registers
    as a ctest. The desktop capture takes whichever player the job built - the
    classic one is the GATE's counterpart, the next one is the report-only
    pair below. A browser capture SKIPS without a browser (the web job refuses
    a skipped web test, so that skip is a failure exactly where it matters); a
    desktop capture asked for with no player FAILS, because the job that asks
    for it is the one that built it.
  * COMPARE TWO CAPTURES (--compare-shots --shot-web + --shot-desktop): the
    comparison alone, on frames captured elsewhere - the CI road, and the one
    that makes a skipped capture harmless: a missing or empty capture FAILS
    here. A parity gate that compared nothing must not report parity.

DESKTOP-NEXT VERSUS THE BROWSER is the pair a RELEASE cares about, since the
browser ships and the desktop classic flavor does not. It is measured and
PICTURED on the same captures (--report-only, --pair-image) and deliberately
not gated: its deltas ARE the flavor seam's - the platform seam contributes
about nothing - so gating it would either duplicate the cross-flavor gate at a
corridor four times looser or block on a difference the flavor gate already
adjudicates band by band. The report runs on every CI commit and its
side-by-side pictures ride out as artifacts, so the pair stays looked at.

Every comparison also reports the largest 8-connected region of differing
pixels (parity_diff) and leaves a DIFF IMAGE beside the web capture whenever
there is structure, green runs included. Reported, never gated - see
parity_diff for why.

Pure stdlib (the pixel test's PNG decoder, the web suite's export + browser
helpers). Exit codes: 0 pass, 1 fail, 77 skip.
"""

import argparse
import base64
import contextlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parity_diff  # noqa: E402
from run_benchmark_pixel_test import decode_png, pixel  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tests", "web"))

#: the one capture rectangle, pinned on both sides (see the module docstring).
#: 1280x720 is the player's own default window, so the desktop side renders
#: exactly what it renders everywhere else.
CAPTURE_SIZE = (1280, 720)

#: the cvars BOTH sides boot with. The first two freeze the benchmark tour's
#: wall-clock camera orbit and un-cap its ramp (the same deterministic recipe
#: the cross-flavor gate uses); the third is the shadow pin argued above.
CAPTURE_CVARS = ("benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0,"
                 "r.shadowQuality=off")

#: how long the browser page keeps rendering before its frame is taken. Long
#: enough that the scene has loaded, settled and drawn many frames; the exact
#: frame is not controllable (see the docstring), which is what the water
#: band's corridor pays for.
SETTLE_SECONDS = 3.0

#: the browser run's wall-clock ceiling, from the scene-ready marker backwards:
#: CI's software-rasterized WebGL boots the lake vignette several times slower
#: than local hardware does.
BROWSER_READY_TIMEOUT = 600

#: per-scene comparison profiles: named regions as frame fractions
#: (fx0, fy0, fx1, fy1, corridor). The BANDS are deliberately the cross-flavor
#: gate's bands for the same scene, so the two gates' numbers are read in one
#: unit and a reader can compare "how far apart are the flavors" with "how far
#: apart are the platforms". The CORRIDORS are this seam's own and are measured
#: here.
#:
#: MEASURED, and the corridors are TIGHT because the measurement said they
#: could be. Two numbers stand behind every corridor below:
#:
#:  * THE PLATFORM DELTA - the browser frame against a desktop classic frame,
#:    per band, max channel delta:
#:
#:      lake        sky 0    terrain 1   water 0
#:      mirrorlake  sky 0    shore 3     watermirror 1, 1
#:                  rockmirror 2         water_open 1
#:
#:    measured against the linux-classic job's OWN uploaded lake/mirrorlake
#:    captures (Mesa llvmpipe, GL3+, 1280x720) - the desktop half of the CI
#:    pair itself, not a stand-in.
#:  * THE TEMPORAL DELTA - two browser frames of the SAME run, one settle
#:    apart (--measure-spread), which is what the unpinned browser frame
#:    costs:
#:
#:      lake        sky 0.0  terrain 0.9  water 1.1
#:      mirrorlake  sky 0.0  shore 0.0    watermirror 1.5, 1.2
#:                  rockmirror 7.9        water_open 0.3
#:
#: The corridors are those two summed and rounded up with roughly a 4x factor,
#: floored at 12 - room for the one axis the local pair could not hold fixed
#: (a Linux SwiftShader browser rather than a macOS one) and for ordinary
#: commit drift. The rockmirror band gets 20 because its mirrored ripple is
#: the one genuinely time-variant region.
#:
#: The rasterizer contributes NOTHING at band level: the same page captured
#: through a GPU-backed WebGL context and through forced SwiftShader agrees
#: to 0 on every band of both scenes. That is why this gate does not pin the
#: browser's rasterizer, only its viewport and its cvars.
#:
#: A corridor is never tightened or widened without a measurement written
#: beside it - a corridor moved by guesswork blocks merges.
WEB_PROFILES = {
    # the refraction-only lake framing: sky above the horizon, the shore
    # island band mid-frame, open water across the lower half. The water band
    # holds the wave train and its specular streak; the bands flank the streak
    # column, which is why even the water band's temporal spread stays near 1.
    "lake.oscene": {
        "regions": {
            "sky": (0.35, 0.10, 0.95, 0.24, 12.0),
            "terrain": (0.25, 0.31, 0.44, 0.41, 12.0),
            "water": (0.05, 0.55, 0.35, 0.85, 12.0),
        },
    },
    # the planar-mirror sibling: the waterline strips and the rock-mirror band
    # are where the browser's ES-300 mirror pass is actually read - a mirror
    # that failed to render on the WebGL2 context (or fell back to the flat
    # shimmer) moves these bands by tens of levels, not by ones.
    "mirrorlake.oscene": {
        "regions": {
            "sky": (0.30, 0.02, 0.95, 0.08, 12.0),
            "shore": (0.15, 0.12, 0.85, 0.22, 12.0),
            "watermirror_l": (0.08, 0.25, 0.40, 0.28, 12.0),
            "watermirror_r": (0.60, 0.25, 0.92, 0.28, 12.0),
            "rockmirror": (0.38, 0.38, 0.52, 0.50, 20.0),
            "water_open": (0.05, 0.36, 0.35, 0.52, 12.0),
        },
    },
}


def fail(message):
    print("web_parity: FAIL: " + message)
    sys.exit(1)


def skip(message):
    print("web_parity: SKIP: " + message)
    sys.exit(77)


def scene_base(scene):
    """'scenes/lake.oscene' -> 'lake' (the director's own console tag)."""
    return os.path.splitext(os.path.basename(scene))[0]


# --- the desktop side -------------------------------------------------------

def capture_desktop(player, repo, scene, shot, out_dir):
    """Boot the desktop classic player and dump its frame-60 framebuffer."""
    env = dict(os.environ)
    env.update({
        "ORKIGE_DEMO_FRAMES": "90",
        "ORKIGE_DEMO_SCREENSHOT": shot,
        "ORKIGE_WINDOW_SIZE": "%dx%d" % CAPTURE_SIZE,
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": out_dir,
        "ORKIGE_CVARS": CAPTURE_CVARS,
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


# --- the browser side -------------------------------------------------------

def stage_web_project(repo, scene, dest):
    """Copy the benchmark project with its MAIN SCENE set to `scene`.

    A page is launched with no arguments, so the only way to tell the browser
    player which scene to boot is the manifest it packages. The copy is what
    keeps the repository's own project untouched by a test run.
    """
    source = os.path.join(repo, "projects", "benchmark")
    if not os.path.isdir(source):
        fail("the benchmark project is missing: " + source)
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(source, dest)
    manifest = os.path.join(dest, "project.orkproj")
    with open(manifest, encoding="utf-8") as handle:
        text = handle.read()
    replaced, count = re.subn(r"<MainScene>[^<]*</MainScene>",
                              "<MainScene>%s</MainScene>" % scene, text)
    if count != 1:
        fail("the benchmark manifest has no single <MainScene> to point at "
             + scene)
    with open(manifest, "w", encoding="utf-8") as handle:
        handle.write(replaced)
    return dest


def browser_page_url(port):
    """The shell page's automation query for one parity capture.

    Every knob rides the documented ?env.NAME=VALUE mapping, so the browser
    player reads exactly the environment the desktop player was given.
    """
    query = {
        "env.ORKIGE_WINDOW_SIZE": "%dx%d" % CAPTURE_SIZE,
        "env.ORKIGE_CVARS": CAPTURE_CVARS,
    }
    return ("http://127.0.0.1:%d/index.html?%s"
            % (port, urllib.parse.urlencode(query)))


def open_devtools(browser_process, port, timeout=30):
    """Wait for the browser's DevTools page target and return a CDP session."""
    from run_embed_click import WS, CDP           # the web suite's tiny client
    deadline = time.time() + timeout
    while time.time() < deadline:
        if browser_process.poll() is not None:
            fail("the headless browser exited before its DevTools endpoint "
                 f"answered (code {browser_process.returncode})")
        try:
            listing = json.loads(urllib.request.urlopen(
                "http://127.0.0.1:%d/json" % port, timeout=1).read())
            for target in listing:
                if target.get("type") == "page":
                    parts = re.match(r"ws://([^:/]+):(\d+)(/.*)",
                                     target["webSocketDebuggerUrl"])
                    return CDP(WS(parts.group(1), int(parts.group(2)),
                                  parts.group(3)))
        except Exception:
            time.sleep(0.2)
    fail("the headless browser never answered on its DevTools port")


def capture_web(repo, engine_build, scene, shot, work_dir,
                settle=SETTLE_SECONDS, extra_shot=None):
    """Export the scene for the browser, boot it headless, take its frame.

    `extra_shot`, when given, takes a SECOND frame one settle later: two
    captures of the same run, which is how the temporal share of a corridor is
    measured (the module docstring's unpinned-frame cost).
    """
    from run_export_web import export, find_browser, serve  # noqa: E402
    from run_embed_click import free_port                   # noqa: E402

    browser = find_browser()
    if not browser:
        # the same honest skip every browser-driving web test takes. It cannot
        # launder a missing capture into parity: the CI web job refuses a
        # skipped web test outright, and the comparison FAILS on the artifact
        # that was then never written
        skip("no headless Chrome/Chromium on this machine (set ORKIGE_CHROME "
             "to override)")
    project = stage_web_project(repo, scene, os.path.join(work_dir, "project"))
    export_dir = os.path.join(work_dir, "export")
    export(os.path.abspath(engine_build), export_dir, project)

    server, port = serve(export_dir)
    profile = os.path.join(work_dir, "chrome_profile")
    shutil.rmtree(profile, ignore_errors=True)
    debug_port = free_port()
    process = subprocess.Popen(
        [browser, "--headless=new", "--no-first-run",
         "--no-default-browser-check", "--user-data-dir=" + profile,
         # the software-WebGL fallback a GPU-less runner takes anyway, asked
         # for explicitly so a machine WITH a GPU is not a different test
         "--enable-unsafe-swiftshader",
         "--remote-debugging-port=%d" % debug_port, "--remote-allow-origins=*",
         "--window-size=%d,%d" % CAPTURE_SIZE, "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        cdp = open_devtools(process, debug_port)
        cdp.call("Runtime.enable")
        cdp.call("Log.enable")
        cdp.call("Page.enable")
        # the exact capture rectangle: --window-size leaves the CONTENT
        # viewport short of the window, and the canvas is sized to the content
        cdp.call("Emulation.setDeviceMetricsOverride",
                 {"width": CAPTURE_SIZE[0], "height": CAPTURE_SIZE[1],
                  "deviceScaleFactor": 1, "mobile": False})
        cdp.call("Page.navigate", {"url": browser_page_url(port)})
        marker = r"director\[%s\]: .* ready" % re.escape(scene_base(scene))
        try:
            if not cdp.wait_console(marker, BROWSER_READY_TIMEOUT):
                fail(f"the browser never reported '{scene}' ready - the scene "
                     "did not load in the page")
            time.sleep(settle)
            write_page_capture(cdp, shot)
            image = load_capture("web", shot)
            assert_rendered(image, shot)
            if extra_shot:
                time.sleep(settle)
                write_page_capture(cdp, extra_shot)
                assert_rendered(load_capture("web (second)", extra_shot),
                                extra_shot)
        except SystemExit:
            # whatever went wrong in the page, the page's own log is what says
            # why - a browser refusal is undiagnosable from the harness alone
            console_tail(cdp)
            raise
        return image
    finally:
        process.terminate()
        server.shutdown()


def write_page_capture(cdp, path):
    """One composited page frame, straight to disk.

    The page IS the canvas at this viewport (the shell's stage fills it and the
    status line is empty while a run is live), so the composited page and the
    render target are the same rectangle of pixels.
    """
    data = base64.b64decode(cdp.call("Page.captureScreenshot",
                                     {"format": "png"}, timeout=60)["data"])
    with open(path, "wb") as handle:
        handle.write(data)
    return path


def console_tail(cdp, count=25):
    """Print the page's last console lines - a browser failure is undiagnosable
    from the harness log alone."""
    with cdp.lock:
        for line in cdp.console[-count:]:
            print("web_parity: console| " + line)


def assert_rendered(image, path):
    """A captured frame must hold a SCENE, not a blank page.

    The blank shapes this refuses are real: a page whose module aborted, and a
    canvas captured after the run exited (the context is gone and the canvas
    composites black). Either would otherwise be compared as if it were a
    render.
    """
    width, height, channels, data = image
    if (width, height) != CAPTURE_SIZE:
        fail(f"the browser capture is {width}x{height}, not "
             f"{CAPTURE_SIZE[0]}x{CAPTURE_SIZE[1]} ({path}) - the canvas, the "
             "viewport and the player's window size are not the same rectangle")
    colours = set()
    for y in range(0, height, 13):
        for x in range(0, width, 13):
            colours.add(pixel(data, channels, width, x, y))
            if len(colours) > 16:
                return
    fail(f"the browser capture is near-uniform ({len(colours)} sampled "
         f"colours, {path}) - the scene did not render in the page")


# --- comparison -------------------------------------------------------------

def region_mean(img, x0, y0, x1, y1, step=4):
    width, height, channels, data = img
    total = [0.0, 0.0, 0.0]
    count = 0
    for y in range(y0, min(y1, height), step):
        for x in range(x0, min(x1, width), step):
            red, green, blue = pixel(data, channels, width, x, y)
            total[0] += red
            total[1] += green
            total[2] += blue
            count += 1
    return tuple(value / max(count, 1) for value in total)


def load_capture(label, path):
    """Decode a capture taken elsewhere; refuse when there is nothing there.

    Silence is not parity: an absent, empty or unreadable frame ends the run
    rather than being skipped past.
    """
    if not os.path.exists(path):
        fail(f"the {label} capture does not exist: {path}")
    if os.path.getsize(path) == 0:
        fail(f"the {label} capture is empty: {path}")
    try:
        return decode_png(path)
    except Exception as error:
        fail(f"the {label} capture is unreadable ({path}): {error}")


def region_boxes(profile, width, height):
    return {name: (int(width * fx0), int(height * fy0),
                   int(width * fx1), int(height * fy1), tolerance)
            for name, (fx0, fy0, fx1, fy1, tolerance)
            in profile["regions"].items()}


def report_spread(shot_a, shot_b, scene):
    """Print the per-region delta between two frames of the SAME browser run.

    This is the temporal share of every corridor below: the scene's own motion
    between two arbitrary frames, measured rather than assumed. Reported, never
    gated - it is a property of the content's clock, not of the render tier.
    """
    profile = WEB_PROFILES[os.path.basename(scene)]
    image_a = load_capture("web", shot_a)
    image_b = load_capture("web (second)", shot_b)
    for name, (x0, y0, x1, y1, _tolerance) in region_boxes(
            profile, image_a[0], image_a[1]).items():
        deltas = [abs(a - b) for a, b in
                  zip(region_mean(image_a, x0, y0, x1, y1),
                      region_mean(image_b, x0, y0, x1, y1))]
        print(f"web_parity: temporal spread {name}: "
              f"max channel delta {max(deltas):.1f}")


def side_by_side(image_left, image_right, path, gap=12):
    """Write the two frames into ONE picture, browser LEFT, desktop RIGHT.

    A number says two frames differ; a person deciding whether a difference
    matters looks at the frames. This is the artifact for that - one file to
    open, no toggling between two windows, and the diff image (parity_diff)
    stays beside it for the where.
    """
    sys.path.insert(0, os.path.join(REPO_ROOT, "Util"))
    from orkige_png import Image, encode_png       # the repo's ONE PNG writer
    left_w, left_h, left_ch, left_data = image_left
    right_w, right_h, right_ch, right_data = image_right
    image = Image(left_w + gap + right_w, max(left_h, right_h))
    pixels = image.pixels
    for index in range(0, len(pixels), 4):
        pixels[index] = pixels[index + 1] = pixels[index + 2] = 40
        pixels[index + 3] = 255
    for source, channels, width, height, offset in (
            (left_data, left_ch, left_w, left_h, 0),
            (right_data, right_ch, right_w, right_h, left_w + gap)):
        for y in range(height):
            row = (y * image.width + offset) * 4
            base = y * width * channels
            for x in range(width):
                out = row + x * 4
                src = base + x * channels
                pixels[out] = source[src]
                pixels[out + 1] = source[src + 1]
                pixels[out + 2] = source[src + 2]
                pixels[out + 3] = 255
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    encode_png(image, path)
    return path


def compare_captures(img_web, img_desktop, scene, kept_in, shot_web=None,
                     diff_dir=None, gate=True, pair_image=None):
    """Compare the two frames; GATE on the corridors unless told not to.

    `gate=False` is the honest shape for a pair whose divergence is NOT this
    driver's to judge - the desktop-NEXT comparison, which spans the flavor
    seam as well and is already gated band by band by the cross-flavor driver.
    Every number is still printed and every picture still written; only the
    corridor verdict is withheld. The refusals that keep an absent frame from
    reading as agreement are NOT withheld - they run either way.
    """
    profile = WEB_PROFILES.get(os.path.basename(scene))
    if profile is None:
        fail(f"no region profile for scene '{scene}' - add one to "
             "WEB_PROFILES with measured corridors")
    if not gate:
        print("web_parity: REPORT ONLY - this pair is measured and pictured, "
              "not gated (the corridors below are the browser-vs-desktop-"
              "CLASSIC gate's, shown for scale)")
    width, height = img_web[0], img_web[1]
    same_size = (width, height) == (img_desktop[0], img_desktop[1])
    destination = parity_diff.diff_path(
        shot_web or os.path.join(kept_in, "web.png"), diff_dir)
    if same_size:
        # the SHAPE of the whole-frame disagreement, printed on every run - a
        # green log records the healthy value, which is what a corridor would
        # have to be measured from. Reported, never gated (parity_diff).
        dmap = parity_diff.delta_map(img_desktop, img_web)
        spatial = parity_diff.spatial_summary(dmap)
        print("web_parity: whole frame: "
              + parity_diff.describe(spatial, width * height))
        if spatial.over:
            written = parity_diff.try_write_diff(destination, dmap,
                                                 img_desktop)
            if written:
                print(f"web_parity: diff image {written}")
        else:
            parity_diff.drop_stale_diff(destination)
    else:
        # the region gate is fractional, so it still MEANS something across
        # two densities; the per-pixel diagnosis does not, and inventing a
        # correspondence by resampling would report a difference nothing
        # rendered
        print(f"web_parity: capture sizes differ ({width}x{height} vs "
              f"{img_desktop[0]}x{img_desktop[1]}) - region means still "
              "compare, no diff image")

    # the SAME fractional bands on each side, resolved against each capture's
    # own size - which is what keeps the verdict meaningful when the two
    # densities differ
    desktop_boxes = region_boxes(profile, img_desktop[0], img_desktop[1])
    worst = 0.0
    breaches = []
    for name, (x0, y0, x1, y1, tolerance) in region_boxes(
            profile, width, height).items():
        mean_web = region_mean(img_web, x0, y0, x1, y1)
        mean_desktop = region_mean(img_desktop, *desktop_boxes[name][:4])
        deltas = [abs(a - b) for a, b in zip(mean_web, mean_desktop)]
        print(f"web_parity: {name}: web=({mean_web[0]:.0f},{mean_web[1]:.0f},"
              f"{mean_web[2]:.0f}) desktop=({mean_desktop[0]:.0f},"
              f"{mean_desktop[1]:.0f},{mean_desktop[2]:.0f}) delta="
              f"({deltas[0]:.0f},{deltas[1]:.0f},{deltas[2]:.0f}) "
              f"tol={tolerance:.0f}")
        worst = max(worst, max(deltas))
        if max(deltas) > tolerance:
            breaches.append((name, max(deltas), tolerance))
            if gate:
                fail(f"region '{name}' diverges between the browser and the "
                     f"desktop: max channel delta {max(deltas):.0f} > "
                     f"{tolerance} - the WebGL tier no longer shows the same "
                     "scene as the desktop classic flavor (capture pair kept "
                     f"in {kept_in})"
                     + (f" - diff image {destination}"
                        if os.path.exists(destination) else ""))

    if pair_image:
        written = side_by_side(img_web, img_desktop, pair_image)
        print(f"web_parity: side-by-side (browser left, desktop right) "
              f"{written}")

    if not gate:
        over = ", ".join(f"{name} {value:.0f}>{limit:.0f}"
                         for name, value, limit in breaches) or "none"
        print(f"web_parity: REPORTED - worst band delta {worst:.0f}; over the "
              f"browser-vs-classic corridors: {over}")
        return 0

    parity_diff.drop_stale_diff(destination)
    print("web_parity: PASS")
    return 0


# --- entry points -----------------------------------------------------------

def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo")
    parser.add_argument("--engine-build",
                        help="the web-release build tree holding the wasm "
                             "player (the browser side)")
    parser.add_argument("--player-desktop",
                        help="the desktop player to capture with: the CLASSIC "
                             "one for the gate, the next one for the "
                             "report-only next-vs-browser pair")
    parser.add_argument("--dir")
    parser.add_argument("--scene", default="scenes/lake.oscene")
    parser.add_argument("--capture", choices=("web", "desktop"),
                        help="capture that side into <dir>/<side>.png and stop")
    parser.add_argument("--measure-spread", action="store_true",
                        help="with --capture web: take a SECOND frame one "
                             "settle later and report the per-region temporal "
                             "spread (corridor calibration)")
    parser.add_argument("--compare-shots", action="store_true",
                        help="compare two captures taken elsewhere; nothing "
                             "is booted")
    parser.add_argument("--shot-web", help="the browser capture")
    parser.add_argument("--shot-desktop", help="the desktop classic capture")
    parser.add_argument("--diff-dir",
                        help="where a comparison's diff image lands "
                             "(default: beside the browser capture)")
    parser.add_argument("--report-only", action="store_true",
                        help="print every number and write every picture, but "
                             "do not gate on the corridors - for the "
                             "desktop-NEXT pair, which spans the flavor seam "
                             "too and is gated band by band elsewhere")
    parser.add_argument("--pair-image",
                        help="write the two frames as ONE side-by-side "
                             "picture (browser left, desktop right)")
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the pure parts and exit")
    return parser.parse_args(argv)


def capture_one(args):
    """--capture: one side's frame, written where a build job can carry it."""
    if not args.repo or not args.dir:
        fail("--capture needs --repo and --dir")
    os.makedirs(args.dir, exist_ok=True)
    shot = os.path.join(args.dir, args.capture + ".png")
    if args.capture == "desktop":
        if not args.player_desktop:
            fail("--capture desktop needs --player-desktop")
        if not os.path.exists(args.player_desktop):
            # asked for explicitly, so absence is a failure rather than a skip
            fail("the desktop player is not built: "
                 + args.player_desktop)
        capture_desktop(args.player_desktop, args.repo, args.scene, shot,
                        args.dir)
    else:
        if not args.engine_build:
            fail("--capture web needs --engine-build (the web-release tree)")
        second = os.path.join(args.dir, "web_second.png") \
            if args.measure_spread else None
        capture_web(args.repo, args.engine_build, args.scene, shot, args.dir,
                    extra_shot=second)
        if second:
            report_spread(shot, second, args.scene)
    print(f"web_parity: captured {args.capture} {args.scene} -> {shot}")
    return 0


def main(argv=None):
    args = parse_args(argv)
    if args.selftest:
        return selftest()
    if args.capture:
        return capture_one(args)

    if args.compare_shots:
        if not (args.shot_web and args.shot_desktop):
            fail("--compare-shots needs --shot-web and --shot-desktop")
        img_web = load_capture("web", args.shot_web)
        img_desktop = load_capture("desktop", args.shot_desktop)
        kept_in = os.path.dirname(os.path.abspath(args.shot_web))
        shot_web = args.shot_web
    else:
        for name in ("repo", "engine_build", "player_desktop", "dir"):
            if not getattr(args, name):
                fail("--" + name.replace("_", "-") + " is required")
        if not os.path.exists(args.player_desktop):
            skip("the desktop classic player is not built: "
             + args.player_desktop)
        from run_export_web import find_browser  # noqa: E402
        if not find_browser():
            skip("no headless Chrome/Chromium on this machine (set "
                 "ORKIGE_CHROME to override)")
        os.makedirs(args.dir, exist_ok=True)
        shot_web = os.path.join(args.dir, "web.png")
        shot_desktop = os.path.join(args.dir, "desktop.png")
        img_desktop = capture_desktop(args.player_desktop, args.repo,
                                      args.scene, shot_desktop, args.dir)
        img_web = capture_web(args.repo, args.engine_build, args.scene,
                              shot_web, args.dir)
        kept_in = args.dir

    return compare_captures(img_web, img_desktop, args.scene, kept_in,
                            shot_web, args.diff_dir,
                            gate=not args.report_only,
                            pair_image=args.pair_image)


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


def write_noisy_png(path, width, height, base):
    """A PNG with per-pixel variation - what 'a scene rendered' looks like to
    assert_rendered, as opposed to a flat page."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(bytes(((base[0] + x) % 256, (base[1] + y) % 256,
                              (base[2] + x + y) % 256)))

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
    scratch = tempfile.mkdtemp(prefix="orkige_webparity_selftest_")
    shot_web = os.path.join(scratch, "web.png")
    shot_desktop = os.path.join(scratch, "desktop.png")
    lake = "scenes/lake.oscene"
    diff_image = os.path.join(scratch, "web.diff.png")

    # the shared diagnosis (clustering, the heat ramp, the writer read back
    # through this driver's own decoder)
    parity_diff.selftest_pure()
    parity_diff.selftest_roundtrip(decode_png, scratch)

    # matching frames pass, report their shape and leave no picture behind
    write_png(shot_web, 64, 64, (80, 90, 100))
    write_png(shot_desktop, 64, 64, (80, 90, 100))
    code, said = run_quiet(["--compare-shots", "--scene", lake,
                            "--shot-web", shot_web,
                            "--shot-desktop", shot_desktop])
    assert code == 0, said
    assert "whole frame: no pixel over" in said, said
    assert not os.path.exists(diff_image), "a clean pair wrote a diff image"

    # a region beyond its corridor fails, names the region and leaves the diff
    # image beside the browser capture
    write_png(shot_desktop, 64, 64, (200, 90, 100))
    code, said = run_quiet(["--compare-shots", "--scene", lake,
                            "--shot-web", shot_web,
                            "--shot-desktop", shot_desktop])
    assert code == 1 and "diverges between the browser and the desktop" in said
    assert "largest region 4096px" in said, said
    assert os.path.exists(diff_image) and diff_image in said, said
    assert decode_png(diff_image)[0] == 64

    # ... into a directory of its own when asked
    elsewhere = os.path.join(scratch, "diffs")
    assert run_quiet(["--compare-shots", "--scene", lake,
                      "--shot-web", shot_web, "--shot-desktop", shot_desktop,
                      "--diff-dir", elsewhere])[0] == 1
    assert os.path.exists(os.path.join(elsewhere, "web.diff.png"))

    # agreeing again takes the stale picture of the old divergence away
    write_png(shot_desktop, 64, 64, (80, 90, 100))
    assert run_quiet(["--compare-shots", "--scene", lake,
                      "--shot-web", shot_web,
                      "--shot-desktop", shot_desktop])[0] == 0
    assert not os.path.exists(diff_image), "a passing pair kept a stale diff"

    # differing sizes still compare region-wise (the gate is fractional) and
    # say so, but write no diff image - there is no pixel correspondence
    odd = os.path.join(scratch, "odd.png")
    write_png(odd, 32, 32, (80, 90, 100))
    code, said = run_quiet(["--compare-shots", "--scene", lake,
                            "--shot-web", shot_web, "--shot-desktop", odd])
    assert code == 0 and "capture sizes differ" in said, said
    assert not os.path.exists(diff_image), said

    # --report-only prints the numbers and returns 0 even where the gate would
    # refuse - and says plainly which bands were over, so nobody reads the exit
    # code as agreement
    write_png(shot_desktop, 64, 64, (200, 90, 100))
    code, said = run_quiet(["--compare-shots", "--scene", lake, "--report-only",
                            "--shot-web", shot_web,
                            "--shot-desktop", shot_desktop])
    assert code == 0, said
    assert "REPORT ONLY" in said and "REPORTED - worst band delta" in said, said
    assert "sky 120>12" in said, said           # the band, its delta, its limit
    assert "PASS" not in said, said             # a report is not a verdict

    # ... and the refusals still hold under --report-only: nothing about a
    # missing frame becomes acceptable because the corridors are not gated
    expect_refusal("a missing capture under --report-only",
                   ["--compare-shots", "--scene", lake, "--report-only",
                    "--shot-web", shot_web,
                    "--shot-desktop", os.path.join(scratch, "absent.png")],
                   "does not exist")

    # the side-by-side picture: both frames in one image, in order
    pair = os.path.join(scratch, "pair.png")
    code, said = run_quiet(["--compare-shots", "--scene", lake, "--report-only",
                            "--shot-web", shot_web,
                            "--shot-desktop", shot_desktop,
                            "--pair-image", pair])
    assert code == 0 and os.path.exists(pair), said
    pair_width, pair_height, pair_ch, pair_data = decode_png(pair)
    assert (pair_width, pair_height) == (64 + 12 + 64, 64), (pair_width,
                                                             pair_height)
    assert pixel(pair_data, pair_ch, pair_width, 10, 10) == (80, 90, 100)
    assert pixel(pair_data, pair_ch, pair_width, 64 + 12 + 10, 10) == \
        (200, 90, 100)
    write_png(shot_desktop, 64, 64, (80, 90, 100))

    # a scene with no measured profile is refused rather than guessed at
    expect_refusal("an unprofiled scene",
                   ["--compare-shots", "--scene", "scenes/nowhere.oscene",
                    "--shot-web", shot_web, "--shot-desktop", shot_desktop],
                   "no region profile")

    # THE refusals: comparing nothing must never read as parity
    absent = os.path.join(scratch, "absent.png")
    expect_refusal("a missing capture",
                   ["--compare-shots", "--scene", lake,
                    "--shot-web", shot_web, "--shot-desktop", absent],
                   absent)
    blank = os.path.join(scratch, "blank.png")
    open(blank, "wb").close()
    expect_refusal("an empty capture",
                   ["--compare-shots", "--scene", lake,
                    "--shot-web", blank, "--shot-desktop", shot_desktop],
                   "is empty")
    garbage = os.path.join(scratch, "garbage.png")
    with open(garbage, "wb") as handle:
        handle.write(b"not a png at all")
    expect_refusal("an unreadable capture",
                   ["--compare-shots", "--scene", lake,
                    "--shot-web", garbage, "--shot-desktop", shot_desktop],
                   "unreadable")
    expect_refusal("one capture without the other",
                   ["--compare-shots", "--shot-web", shot_web],
                   "--shot-desktop")

    # a browser frame is only a capture when it holds a SCENE: the blank page
    # and the after-exit black canvas are refused by name, at the right size,
    # and a real render at the wrong size is refused too
    def expect_blank_refusal(what, path, names):
        captured = io.StringIO()
        try:
            with contextlib.redirect_stdout(captured):
                assert_rendered(decode_png(path), path)
        except SystemExit as exit_code:
            assert exit_code.code == 1, what
            assert names in captured.getvalue(), captured.getvalue()
            return
        raise AssertionError(what + " passed as a render")

    flat = os.path.join(scratch, "flat.png")
    write_png(flat, *CAPTURE_SIZE, fill=(0, 0, 0))
    expect_blank_refusal("a black canvas", flat, "near-uniform")
    rendered = os.path.join(scratch, "rendered.png")
    write_noisy_png(rendered, *CAPTURE_SIZE, base=(10, 20, 30))
    assert_rendered(decode_png(rendered), rendered)      # the passing shape
    small = os.path.join(scratch, "small.png")
    write_noisy_png(small, 640, 360, base=(10, 20, 30))
    expect_blank_refusal("a wrongly sized capture", small, "not 1280x720")

    # an explicitly asked-for capture with no player is a failure, not a skip
    expect_refusal("a desktop capture with no player",
                   ["--capture", "desktop", "--repo", scratch,
                    "--dir", scratch, "--player-desktop", "/nonexistent"],
                   "not built")

    # the run road keeps its honest skip when the sibling tree is unbuilt
    assert run_quiet(["--repo", scratch, "--dir", scratch,
                      "--engine-build", scratch,
                      "--player-desktop", "/nonexistent/classic"])[0] == 77

    # the manifest rewrite points the browser at the scene under test, and
    # refuses rather than exporting the wrong scene silently
    staged = stage_web_project(REPO_ROOT, lake, os.path.join(scratch, "proj"))
    with open(os.path.join(staged, "project.orkproj"), encoding="utf-8") as f:
        assert "<MainScene>scenes/lake.oscene</MainScene>" in f.read()

    # the page URL carries the pinned window size and the shared cvar set
    url = browser_page_url(1234)
    assert "env.ORKIGE_WINDOW_SIZE=1280x720" in urllib.parse.unquote(url)
    assert "r.shadowQuality=off" in urllib.parse.unquote(url)

    # argument routing
    parsed = parse_args(["--compare-shots", "--shot-web", "a",
                         "--shot-desktop", "b"])
    assert parsed.compare_shots and parsed.shot_web == "a"
    assert parse_args(["--capture", "web"]).capture == "web"
    assert parse_args(["--selftest"]).selftest is True

    shutil.rmtree(scratch, ignore_errors=True)
    print("run_web_parity_test: selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
