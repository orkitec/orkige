#!/usr/bin/env python3
"""Web-export verification (python3 stdlib only, like every Util/ tool).

Two modes, registered as two ctests in the web build tree:

  --mode structure   run Util/orkige_export.py --platform web against the
                     reference Lua project and assert the artifact set: the
                     shell page (with the project's title baked in), the wasm
                     player pair, the packed .data payload image + its loader,
                     and the per-project icon.
  --mode boot        additionally BOOT the exported page in a headless
                     browser: serve the artifact directory over loopback HTTP
                     (stdlib http.server, ephemeral port), drive a headless
                     Chrome/Chromium through a frame-capped run and assert
                     (a) the player found and booted the bundled project,
                     (b) the run ENDED cleanly through the orderly teardown
                     (the frame-stats exit line is printed by the player's
                     shutdown path), and (c) a mid-run screenshot renders an
                     actual scene (many distinct colours, not a flat page).
                     Exits 77 (ctest SKIP) when no headless browser exists.

The headless browser is resolved from ORKIGE_CHROME, the macOS application
path, or google-chrome/chromium on PATH. The browser process may outlive its
usefulness (the page's timer loop keeps scheduling work), so every run is
deadline-killed and asserted on the output captured up to that point.
"""

import argparse
import http.server
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading

REPO_ROOT = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "Util"))
import orkige_png  # noqa: E402  (sibling Util helper - stdlib-only PNG codec)
import make_benchmark_assets as bench  # noqa: E402  (SceneWriter - format-current)

PROJECT = os.path.join(REPO_ROOT, "projects", "jumper-lua")
ROLLER_PROJECT = os.path.join(REPO_ROOT, "projects", "roller")
EXPORTER = os.path.join(REPO_ROOT, "Util", "orkige_export.py")

# what a web export must contain (see Util/orkige_export.py export_web)
ARTIFACT_FILES = ("index.html", "orkige_player.js", "orkige_player.wasm",
                  "game.data", "game.js", "icon.png")

BOOT_MARKER = "bundled project '/project'"
EXIT_MARKER = "frame stats - "  # printed by the player's orderly shutdown

# the water fixture's own boot log (water_probe.component.lua): the classic
# backend's screen-space-refraction capability answers true only where the
# advanced-water GLSL gate opened - on web that is the GLSL ES 3.0 variant on
# a real WebGL2/GLES3 context (the desktop water probes cover the GL core one).
WATER_SUPPORTED_MARKER = "water_probe: screenSpaceRefraction=true"
# the classic backend's honest refusal when a water program fails to build (the
# createWaterMaterial catch's oDebugError) - its ABSENCE proves the ES-300
# refraction/reflection programs compiled + linked on the WebGL2 driver.
WATER_SETUP_FAILED = ("refraction setup failed", "reflection setup failed")

# a single-scene water project generated on the fly (from the benchmark's
# SceneWriter, so it stays current with the .oscene format): a refractive water
# expanse framed by a tiny probe script that logs the capability. The advanced
# grab-pass path activates at scene load, so the boot both flips the cap and
# renders the ES-300 water surface.
WATER_PROBE_SCRIPT = """\
-- water_probe.component.lua - the web (WebGL2/GLES3) advanced-water proof.
-- Frames the refractive water surface and reports the classic backend's
-- screen-space-refraction capability, so the web ctest can confirm the GLSL
-- ES 3.0 water variant activated on a real WebGL2 context (not the byte-stable
-- Stage-1 fallback the GLES2/WebGL1 floor renders).
local TS = RenderNode.TransformSpace

function init(self)
\tlocal engine = Engine.getSingleton()
\tengine:setCameraPerspective()
\tlocal cam = engine:getCamera()
\tlocal node = cam ~= nil and cam:getNode() or nil
\tif node ~= nil then
\t\tnode:setPosition(Vector3(0, 3, 8))
\t\tnode:lookAt(Vector3(0, -1, -6), TS.TS_WORLD, Vector3(0, 0, -1))
\tend
\tprint("water_probe: screenSpaceRefraction=" ..
\t\ttostring(engine:supports("screenSpaceRefraction")))
end

function update(self, dt)
end
"""


def build_water_fixture(dest):
    """write a minimal single-scene refractive-water project into `dest`
    (manifest + scene + probe script). Reuses the benchmark SceneWriter so the
    serialized .oscene tracks the live format."""
    scenes = os.path.join(dest, "scenes")
    scripts = os.path.join(dest, "scripts")
    os.makedirs(scenes, exist_ok=True)
    os.makedirs(scripts, exist_ok=True)
    scene = bench.SceneWriter()
    # the sun: the first directional light the water's PBS/RTSS surface reflects
    scene.add("Sun",
              scene.transform(0.0, 20.0, 0.0, quat=(0.9659, -0.2588, 0.0, 0.0)),
              scene.light(light_type=0, colour=(1.0, 0.9, 0.8), intensity=1.0),
              tags=("sun",))
    # the refractive water expanse (the engine water plane + tiling normal, both
    # bundled engine media): screenSpaceRefraction on + a normal map is exactly
    # the trigger createWaterMaterial takes the advanced grab-pass path on
    scene.add("Lake",
              scene.transform(0.0, -1.0, -6.0),
              scene.water(size_x=40.0, size_z=40.0, wave_height=0.3,
                          screen_space_refraction=True, planar_reflection=False,
                          deep=(0.04, 0.20, 0.30, 1.0),
                          shallow=(0.22, 0.55, 0.62, 1.0),
                          normal_tex="water_normal.png"),
              tags=("water",))
    scene.add("Probe",
              scene.script("water_probe", "scripts/water_probe.component.lua"))
    scene.write_path = os.path.join(scenes, "water.oscene")
    with open(scene.write_path, "w", encoding="utf-8") as f:
        f.write(scene.to_text())
    with open(os.path.join(scripts, "water_probe.component.lua"), "w",
              encoding="utf-8") as f:
        f.write(WATER_PROBE_SCRIPT)
    manifest = os.path.join(dest, "project.orkproj")
    with open(manifest, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n'
                '<OrkigeProject version="1">\n'
                '    <Name>Water Fixture</Name>\n'
                '    <MainScene>scenes/water.oscene</MainScene>\n'
                '</OrkigeProject>\n')
    return dest


def fail(message):
    print("run_export_web: FAILED - %s" % message, flush=True)
    sys.exit(1)


def find_browser():
    candidates = []
    if os.environ.get("ORKIGE_CHROME"):
        candidates.append(os.environ["ORKIGE_CHROME"])
    candidates.append(
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
    for name in ("google-chrome", "google-chrome-stable", "chromium",
                 "chromium-browser"):
        found = shutil.which(name)
        if found:
            candidates.append(found)
    for candidate in candidates:
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return ""


def export(engine_build, output_dir, project=PROJECT):
    result = subprocess.run(
        [sys.executable, EXPORTER, "--project", project, "--platform", "web",
         "--engine-build", engine_build, "--output", output_dir],
        capture_output=True, text=True)
    if result.returncode != 0:
        fail("exporter failed:\n%s\n%s" % (result.stdout, result.stderr))
    if not re.search(r"^orkige_export: OK ", result.stdout, re.MULTILINE):
        fail("exporter did not report OK:\n%s" % result.stdout)


def assert_structure(output_dir, title="Jumper Lua"):
    for name in ARTIFACT_FILES:
        path = os.path.join(output_dir, name)
        if not os.path.isfile(path) or os.path.getsize(path) == 0:
            fail("artifact file '%s' missing or empty" % name)
    with open(os.path.join(output_dir, "index.html"), encoding="utf-8") as f:
        shell = f.read()
    if "<title>%s</title>" % title not in shell:
        fail("index.html does not carry the project title '%s'" % title)
    if "@" + "TITLE" + "@" in shell:
        fail("index.html still contains unexpanded placeholders")
    # the payload image must be substantial (engine media + project)
    if os.path.getsize(os.path.join(output_dir, "game.data")) < 100 * 1024:
        fail("game.data is implausibly small")
    print("run_export_web: structure OK (%d files)" % len(ARTIFACT_FILES),
          flush=True)


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve(directory):
    """serve directory on an ephemeral loopback port; returns (server, port)"""
    handler = lambda *args, **kwargs: QuietHandler(  # noqa: E731
        *args, directory=directory, **kwargs)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, server.server_address[1]


def run_browser(browser, url, deadline_seconds, screenshot="",
                budget_ms=30000, needed_markers=()):
    """drive one headless page load; returns captured stderr text. The
    process is killed at the deadline or as soon as every needed marker was
    seen (the page's timer loop can keep the browser alive indefinitely)."""
    # ignore_cleanup_errors: the deadline-killed browser's helper processes
    # can still be writing the profile while the context exits - a leftover
    # temp profile is harmless, a cleanup OSError would fail a passed test
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as profile:
        command = [browser, "--headless=new", "--no-first-run",
                   "--user-data-dir=" + profile,
                   "--enable-unsafe-swiftshader",
                   "--window-size=1280,800",
                   "--enable-logging=stderr", "--v=0",
                   "--virtual-time-budget=%d" % budget_ms]
        if screenshot:
            command.append("--screenshot=" + screenshot)
        command.append(url)
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True,
                                   errors="replace")
        captured = []
        remaining = set(needed_markers)

        def reap():
            try:
                process.wait(timeout=deadline_seconds)
            except subprocess.TimeoutExpired:
                process.kill()

        killer = threading.Timer(deadline_seconds, process.kill)
        killer.start()
        try:
            for line in process.stderr:
                captured.append(line)
                for marker in list(remaining):
                    if marker in line:
                        remaining.discard(marker)
                if needed_markers and not remaining and not screenshot:
                    process.kill()
                    break
        finally:
            killer.cancel()
            process.kill()
            process.wait()
        return "".join(captured)


def assert_boot(output_dir, browser):
    server, port = serve(output_dir)
    try:
        # leg 1: frame-capped run - boot marker + the orderly-shutdown frame
        # stats line (both are the player's own SDL_Log output, mirrored into
        # the page console and Chrome's stderr log)
        url = ("http://127.0.0.1:%d/index.html"
               "?env.ORKIGE_DEMO_FRAMES=90&env.ORKIGE_DEMO_FPS_LOG=1" % port)
        log = run_browser(browser, url, deadline_seconds=180,
                          needed_markers=(BOOT_MARKER, EXIT_MARKER))
        if BOOT_MARKER not in log:
            fail("player did not report the bundled project - full boot "
                 "log:\n%s" % log)
        if EXIT_MARKER not in log:
            # the WHOLE captured console: a shader/GL failure's cause (the
            # context version + supported-profile lines at GL init) sits
            # thousands of lines before its symptom, so a tail is useless
            fail("player did not reach the orderly shutdown (no frame-stats "
                 "line) - full log:\n%s" % log)
        print("run_export_web: boot + clean shutdown OK", flush=True)

        # leg 2: a mid-run screenshot must show an actual rendered scene
        shot = os.path.join(output_dir, "boot_screenshot.png")
        run_browser(browser, "http://127.0.0.1:%d/index.html" % port,
                    deadline_seconds=180, screenshot=shot, budget_ms=8000)
        if not os.path.isfile(shot) or os.path.getsize(shot) == 0:
            fail("no screenshot written")
        image = orkige_png.decode_png(shot)
        colours = set()
        stride = 4 * 13  # sample a pixel grid - counting all is wasteful
        for offset in range(0, len(image.pixels) - 4, stride):
            colours.add(bytes(image.pixels[offset:offset + 3]))
            if len(colours) > 16:
                break
        if len(colours) <= 4:
            fail("screenshot is near-uniform (%d sampled colours) - the "
                 "scene did not render" % len(colours))
        print("run_export_web: screenshot renders a scene (%dx%d, >%d "
              "colours)" % (image.width, image.height, len(colours) - 1),
              flush=True)
    finally:
        server.shutdown()


def assert_roller(output_dir, browser):
    """the whole 2D-tier gameplay selfcheck IN the browser: the exported
    roller runs its player selfcheck (tilt roll via the key simulation,
    move mode, tile slide, win path) - the wasm physics/input/render stack
    must pass the same bar the desktop ctest holds it to."""
    server, port = serve(output_dir)
    try:
        url = ("http://127.0.0.1:%d/index.html"
               "?env.ORKIGE_ROLLER_SELFCHECK=1" % port)
        complete = "roller selfcheck complete"
        failed = "ROLLER SELFCHECK FAILED"
        log = run_browser(browser, url, deadline_seconds=300,
                          needed_markers=(complete,))
        if failed in log:
            fail("the in-browser roller selfcheck FAILED - full log:\n%s"
                 % log)
        if complete not in log:
            fail("the in-browser roller selfcheck never completed - full "
                 "log:\n%s" % log)
        print("run_export_web: in-browser roller selfcheck complete",
              flush=True)
    finally:
        server.shutdown()


def assert_water(output_dir, browser):
    """boot the exported water fixture and prove the ADVANCED (screen-space
    refraction) water activated on the real WebGL2/GLES3 context: the probe
    script reports the capability true, the ES-300 grab-pass program builds
    without a fallback refusal, and the run renders the water surface through to
    the orderly shutdown (a shader link error would abort before it)."""
    server, port = serve(output_dir)
    try:
        # frame-capped so the water surface actually draws (the ES-300 programs
        # link on first use, so rendering is what proves they built on-driver)
        url = ("http://127.0.0.1:%d/index.html"
               "?env.ORKIGE_DEMO_FRAMES=90&env.ORKIGE_DEMO_FPS_LOG=1" % port)
        log = run_browser(browser, url, deadline_seconds=180,
                          needed_markers=(WATER_SUPPORTED_MARKER, EXIT_MARKER))
        for failed in WATER_SETUP_FAILED:
            if failed in log:
                fail("a water program failed to build on WebGL2 ('%s') - the "
                     "ES-300 variant did not compile/link - full log:\n%s"
                     % (failed, log))
        if WATER_SUPPORTED_MARKER not in log:
            fail("the fixture did not report screenSpaceRefraction supported on "
                 "WebGL2 - the ES-300 water gate did not open - full log:\n%s"
                 % log)
        if EXIT_MARKER not in log:
            fail("the water fixture did not reach the orderly shutdown - the "
                 "ES-300 water may have faulted at render - full log:\n%s" % log)
        print("run_export_web: advanced water activated on WebGL2 (ES-300 "
              "grab-pass, no fallback)", flush=True)
    finally:
        server.shutdown()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine-build", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--mode",
                        choices=["structure", "boot", "roller", "water"],
                        default="structure")
    args = parser.parse_args()

    browser = ""
    if args.mode in ("boot", "roller", "water"):
        browser = find_browser()
        if not browser:
            print("run_export_web: SKIPPED - no headless Chrome/Chromium on "
                  "this machine (set ORKIGE_CHROME to override)", flush=True)
            sys.exit(77)

    output = os.path.abspath(args.output)
    if args.mode == "water":
        # the water fixture is generated next to the export output, then
        # exported + booted like any project
        project = build_water_fixture(output + "_project")
        title = "Water Fixture"
    else:
        project = ROLLER_PROJECT if args.mode == "roller" else PROJECT
        title = "Roller" if args.mode == "roller" else "Jumper Lua"
    export(os.path.abspath(args.engine_build), output, project)
    assert_structure(output, title)
    if args.mode == "boot":
        assert_boot(output, browser)
    elif args.mode == "roller":
        assert_roller(output, browser)
    elif args.mode == "water":
        assert_water(output, browser)
    print("run_export_web: PASSED (%s)" % args.mode, flush=True)


if __name__ == "__main__":
    main()
