#!/usr/bin/env python3
"""Web-export verification (python3 stdlib only, like every Util/ tool).

Two modes, registered as two ctests in the web build tree:

  --mode structure   export the reference Lua project for the web and assert
                     the artifact set: the shell page (with the project's
                     title baked in), the wasm player pair, the game pak + its
                     data loader, and the per-project icon.
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

A BROWSER WITH NO GPU CONTEXT IS ITS OWN OUTCOME. A headless browser can
start, load the page and run the wasm module while handing it no WebGL context
at all (the GPU process fails to come up; the page's context request is
refused). Every rendered thing the run was supposed to prove is then absent -
which looks exactly like a feature gate that never opened, because the marker
a probe prints is missing either way. So no leg here infers a cause from an
absence: before a missing marker is reported as a verdict on a feature, the
browser is ASKED whether it hands a page a GPU context at all
(browser_gpu_context), and a browser that does not is reported under its own
name and skipped - a run that cannot reach a GPU has nothing to say about what
a GPU would have drawn. Where that absence must not be tolerated, the CI web
job's may-not-skip guard turns the skip into a loud failure - with the cause
named rather than a feature blamed.

--selftest exercises the pure parts (the probe verdict, the browser's own
GPU-absence words, and what a failed leg reports for each) and exits.
"""

import argparse
import glob
import http.server
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import urllib.parse
import zipfile

REPO_ROOT = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "Util"))
import orkige_png  # noqa: E402  (sibling Util helper - stdlib-only PNG codec)
import make_benchmark_assets as bench  # noqa: E402  (SceneWriter - format-current)

PROJECT = os.path.join(REPO_ROOT, "projects", "jumper-lua")
ROLLER_PROJECT = os.path.join(REPO_ROOT, "projects", "roller")

# what a web export must contain (see tools/exporter/ExportWeb.h). game.pak is
# the whole payload - engine media, the project, the orkige_project.txt marker
# - in ONE engine pak; game.js is the loader that hands it to the module
# filesystem.
# THIRD-PARTY-NOTICES.md sits BESIDE the page rather than inside game.pak: a
# web build is a served directory, so the notices the bundled libraries require
# have to be a file a visitor can open, not an archive entry
ARTIFACT_FILES = ("index.html", "orkige_player.js", "orkige_player.wasm",
                  "game.pak", "game.js", "icon.png",
                  "THIRD-PARTY-NOTICES.md")

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


# the bloom fixture's own boot log (bloom_probe.component.lua): the classic
# backend's LDR-bloom capability answers true only where the compositor gate
# opened - on web that is the GLSL ES 3.0 profile on a real WebGL2/GLES3
# context (the desktop demo_bloom selfcheck covers the GL core one). The
# GLES2/WebGL1 floor answers false and logs an honest refusal.
BLOOM_SUPPORTED_MARKER = "bloom_probe: bloom=true"
# the classic backend's honest refusals when bloom is unsupported or the
# compositor cannot be built (@see RenderBackend::applyBloomConfig) - their
# ABSENCE proves the ES-300 quad-pass compositor built on the WebGL2 driver.
BLOOM_SETUP_FAILED = ("bloom post-process is not supported",
                      "compositor could not be created")

# a single-scene bloom project generated on the fly: a bright emissive cube
# framed against a dark field with a probe that logs the capability and opts the
# scene into bloom. The emissive surface (luminance ~0.9, above the 0.75
# bright-pass threshold) is exactly what the classic bloom bright-pass picks up
# and blurs into a halo, so the boot both flips the cap and renders the glow.
BLOOM_PROBE_SCRIPT = """\
-- bloom_probe.component.lua - the web (WebGL2/GLES3) LDR-bloom proof.
-- Frames a bright emissive cube and reports the classic backend's bloom
-- capability, then opts the scene in, so the web ctest can confirm the GLSL
-- ES 3.0 bloom compositor activated on a real WebGL2 context (not the honest
-- refusal the GLES2/WebGL1 floor logs).
local TS = RenderNode.TransformSpace

function init(self)
\tlocal engine = Engine.getSingleton()
\tengine:setCameraPerspective()
\tlocal cam = engine:getCamera()
\tlocal node = cam ~= nil and cam:getNode() or nil
\tif node ~= nil then
\t\tnode:setPosition(Vector3(0, 0, 6))
\t\tnode:lookAt(Vector3(0, 0, 0), TS.TS_WORLD, Vector3(0, 0, -1))
\tend
\t-- per-scene opt-in: a low threshold + strong intensity so the emissive
\t-- cube's highlights bloom into a visible halo (the r.bloomQuality knob is
\t-- on by default; the ctest's baseline run turns it off via ORKIGE_CVARS)
\tengine:setBloom(true, 0.6, 1.5)
\tprint("bloom_probe: bloom=" .. tostring(engine:supports("bloom")))
end

function update(self, dt)
end
"""


def build_bloom_fixture(dest):
    """write a minimal single-scene emissive-cube project into `dest` (manifest
    + scene + probe script + a generated cube mesh and a strong emissive .omat).
    Reuses the benchmark SceneWriter (so the .oscene tracks the live format) and
    the material demo's cube geometry."""
    sys.path.insert(0, os.path.join(REPO_ROOT, "Util"))
    import make_material_demo as matdemo  # noqa: E402  (cube geometry + .glb)
    scenes = os.path.join(dest, "scenes")
    scripts = os.path.join(dest, "scripts")
    assets = os.path.join(dest, "assets")
    for directory in (scenes, scripts, assets):
        os.makedirs(directory, exist_ok=True)
    # the emissive cube: a 24-vertex unit cube mesh + a near-white emissive
    # material (an unlit-bright surface via RTSS self-illumination, luminance
    # ~0.9). Both are plain project assets resolved by name at runtime.
    with open(os.path.join(assets, "bloom_cube.glb"), "wb") as f:
        f.write(matdemo.build_glb("bloom_cube", matdemo.build_cube_geometry()))
    with open(os.path.join(assets, "glow.omat"), "w", encoding="utf-8") as f:
        f.write("# a bright emissive glow source for the bloom bright-pass\n"
                "version 1\n"
                "albedo 0.10 0.10 0.12 1.0\n"
                "metalness 0.0\n"
                "roughness 0.6\n"
                "emissive 1.0 0.9 0.75\n")
    scene = bench.SceneWriter()
    # a dim sun so the dark cube body barely lifts off black - the emissive
    # channel dominates, keeping the surrounding field dark for the halo
    scene.add("Sun",
              scene.transform(0.0, 20.0, 0.0, quat=(0.9659, -0.2588, 0.0, 0.0)),
              scene.light(light_type=0, colour=(0.3, 0.3, 0.35), intensity=0.5),
              tags=("sun",))
    scene.add("Glow",
              scene.transform(0.0, 0.0, 0.0),
              scene.model("bloom_cube.glb", "glow.omat"),
              tags=("glow",))
    scene.add("Probe",
              scene.script("bloom_probe", "scripts/bloom_probe.component.lua"))
    with open(os.path.join(scenes, "bloom.oscene"), "w",
              encoding="utf-8") as f:
        f.write(scene.to_text())
    with open(os.path.join(scripts, "bloom_probe.component.lua"), "w",
              encoding="utf-8") as f:
        f.write(BLOOM_PROBE_SCRIPT)
    with open(os.path.join(dest, "project.orkproj"), "w",
              encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n'
                '<OrkigeProject version="1">\n'
                '    <Name>Bloom Fixture</Name>\n'
                '    <MainScene>scenes/bloom.oscene</MainScene>\n'
                '</OrkigeProject>\n')
    return dest


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


# --- did this browser hand the page a GPU context at all? -------------------

#: the whole probe, handed to the browser as its URL - no server, no file. It
#: asks for the same WebGL context the player asks for and PRINTS the answer,
#: so the question is answered rather than inferred. Chrome mirrors a page's
#: console into its stderr log, which is the same channel every leg here reads.
WEBGL_PROBE_MARKER = "ORKIGE_WEBGL: "
WEBGL_PROBE_NONE = "none"
WEBGL_PROBE_URL = (
    "data:text/html,<script>"
    "var c=document.createElement('canvas');"
    "var g=c.getContext('webgl2')||c.getContext('webgl');"
    "console.log('" + WEBGL_PROBE_MARKER + "'+(g?"
    "(g.getParameter(g.RENDERER)||'context'):'" + WEBGL_PROBE_NONE + "'));"
    "</script>")

#: the browser's OWN words for a GPU that never came up for this page. They
#: only ever enrich a verdict the probe already reached, never stand in for
#: one: a transient line can be followed by a working context, so a log match
#: alone would be another inference.
GPU_ABSENT_PHRASES = (
    "GPU process exited unexpectedly",
    "The GPU process has crashed",
    "ContextResult::kTransientFailure",
    "ContextResult::kFatalFailure",
    "Failed to send GpuControl.CreateCommandBuffer",
    "Failed to establish GPU channel",
    "Failed to create shared context",
)

#: the name of the outcome, kept in one place so every surface spells it alike
NO_GPU_CONTEXT = "the browser provided no GPU context"


def gpu_absence_detail(log):
    """The browser's own line about a GPU that never came up, or ""."""
    for line in log.splitlines():
        for phrase in GPU_ABSENT_PHRASES:
            if phrase in line:
                return line.strip()
    return ""


def probe_verdict(log):
    """PURE: a probe run's captured browser log -> (verdict, detail).

    "gpu"        a context was handed out; detail is the renderer string
    "none"       the page was refused a context; detail is the browser's own
                 explanation where it gave one
    "unanswered" the probe page never reported - a browser that cannot run a
                 script says nothing about GPUs, and is not made to
    """
    for line in log.splitlines():
        index = line.find(WEBGL_PROBE_MARKER)
        if index < 0:
            continue
        # the console line is quoted and carries the page's source after it
        renderer = line[index + len(WEBGL_PROBE_MARKER):].split('"')[0].strip()
        if not renderer or renderer == WEBGL_PROBE_NONE:
            return "none", (gpu_absence_detail(log) or
                            "the page was refused a WebGL context")
        return "gpu", renderer
    return "unanswered", ""


def browser_gpu_context(browser):
    """Ask THIS browser, in a page of its own, for a WebGL context."""
    return probe_verdict(run_browser(browser, WEBGL_PROBE_URL,
                                     deadline_seconds=90, budget_ms=4000,
                                     needed_markers=(WEBGL_PROBE_MARKER,)))


def no_gpu_context_sentence(subject, detail):
    """The ONE wording of this outcome, wherever a browser driver reports it."""
    return ("%s (%s) - the page never reached WebGL, so this run has no "
            "verdict on %s" % (NO_GPU_CONTEXT, detail, subject))


def render_failure_report(subject, message, verdict, detail, log):
    """PURE: what a leg that rendered nothing reports -> (exit code, text).

    The GPU-context answer decides which of three different things happened,
    and each gets its own sentence instead of the feature carrying the blame
    for all of them.
    """
    if verdict == "none":
        return 77, ("run_export_web: SKIPPED - %s - full log:\n%s"
                    % (no_gpu_context_sentence(subject, detail), log))
    if verdict == "gpu":
        return 1, ("run_export_web: FAILED - %s - the browser provided a GPU "
                   "context (%s), so the page's silence about %s is not a "
                   "missing GPU - full log:\n%s"
                   % (message, detail, subject, log))
    return 1, ("run_export_web: FAILED - %s - the GPU-context probe did not "
               "answer, so whether the page had a GPU is unknown - full "
               "log:\n%s" % (message, log))


def fail_render(browser, subject, message, log):
    """Report a leg that rendered nothing - after asking why it did not.

    Used ONLY where the finding is an ABSENCE (a marker that never appeared, a
    frame with nothing in it). A leg that read a positive refusal out of the
    engine's own log keeps reporting that refusal: it is evidence, not a gap.
    """
    verdict, detail = browser_gpu_context(browser)
    if verdict == "none":
        # the leg's own log ran far longer than the probe's and usually
        # carries more of the browser's words about the GPU it never had
        detail = gpu_absence_detail(log) or detail
    code, text = render_failure_report(subject, message, verdict, detail, log)
    print(text, flush=True)
    sys.exit(code)


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


def exporter_command():
    """the exporter this run drives, as an argv prefix.

    A browser export is packaged by the HOST exporter (tools/exporter), which a
    cross-compiled web tree cannot build - so it is resolved from
    ORKIGE_EXPORT_BINARY, else an `orkige_export` in any configured host build
    tree."""
    binary = os.environ.get("ORKIGE_EXPORT_BINARY", "")
    if binary and os.access(binary, os.X_OK):
        return [binary]
    for candidate in sorted(glob.glob(os.path.join(
            REPO_ROOT, "build", "*", "tools", "exporter", "orkige_export"))):
        if os.access(candidate, os.X_OK):
            return [candidate]
    fail("no orkige_export binary in any host build tree - build one "
         "(cmake --build --preset macos-debug --target orkige_export) or set "
         "ORKIGE_EXPORT_BINARY")


def export(engine_build, output_dir, project=PROJECT):
    # --repo names the engine source explicitly rather than leaning on the one
    # baked into the binary at build time: the exporter that packages a browser
    # export is a host tool, and it may well have been built somewhere else
    # (CI hands this job a binary from another build tree entirely).
    result = subprocess.run(
        exporter_command() +
        ["--project", project, "--platform", "web",
         "--engine-build", engine_build, "--output", output_dir,
         "--repo", REPO_ROOT],
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
    # the payload archive must be substantial (engine media + project)
    pak_path = os.path.join(output_dir, "game.pak")
    if os.path.getsize(pak_path) < 100 * 1024:
        fail("game.pak is implausibly small")
    # a packaged payload carries NO .orkmeta: sidecars are editor bookkeeping,
    # and the one answer a runtime reads out of them (how a texture is sampled)
    # is baked into the payload manifest at export time. A browser build cannot
    # read one anyway - the assets it mounts are archive entries.
    with zipfile.ZipFile(pak_path) as pak:
        strays = [name for name in pak.namelist()
                  if name.endswith(".orkmeta")]
    if strays:
        fail("game.pak carries .orkmeta sidecars: %s" % sorted(strays)[:5])
    print("run_export_web: structure OK (%d files, no sidecars in the pak)"
          % len(ARTIFACT_FILES), flush=True)


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
        # every report below carries the WHOLE captured console: a shader/GL
        # failure's cause (the context version + supported-profile lines at GL
        # init) sits thousands of lines before its symptom, so a tail is
        # useless
        subject = "the browser boot"
        if BOOT_MARKER not in log:
            fail_render(browser, subject,
                        "player did not report the bundled project", log)
        if EXIT_MARKER not in log:
            fail_render(browser, subject,
                        "player did not reach the orderly shutdown (no "
                        "frame-stats line)", log)
        print("run_export_web: boot + clean shutdown OK", flush=True)

        # leg 2: a mid-run screenshot must show an actual rendered scene
        shot = os.path.join(output_dir, "boot_screenshot.png")
        shot_log = run_browser(
            browser, "http://127.0.0.1:%d/index.html" % port,
            deadline_seconds=180, screenshot=shot, budget_ms=8000)
        if not os.path.isfile(shot) or os.path.getsize(shot) == 0:
            fail_render(browser, subject, "no screenshot written", shot_log)
        image = orkige_png.decode_png(shot)
        colours = set()
        stride = 4 * 13  # sample a pixel grid - counting all is wasteful
        for offset in range(0, len(image.pixels) - 4, stride):
            colours.add(bytes(image.pixels[offset:offset + 3]))
            if len(colours) > 16:
                break
        if len(colours) <= 4:
            fail_render(browser, subject,
                        "screenshot is near-uniform (%d sampled colours) - "
                        "the scene did not render" % len(colours), shot_log)
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
            # the selfcheck's own verdict, not an absence - reported as it is
            fail("the in-browser roller selfcheck FAILED - full log:\n%s"
                 % log)
        if complete not in log:
            fail_render(browser, "the in-browser roller selfcheck",
                        "the in-browser roller selfcheck never completed",
                        log)
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
        subject = "the advanced (screen-space refraction) water on WebGL2"
        for failed in WATER_SETUP_FAILED:
            if failed in log:
                # the backend SAID a program failed to build: evidence, not a
                # gap, so it is reported as the verdict it is
                fail("a water program failed to build on WebGL2 ('%s') - the "
                     "ES-300 variant did not compile/link - full log:\n%s"
                     % (failed, log))
        if WATER_SUPPORTED_MARKER not in log:
            fail_render(browser, subject,
                        "the fixture did not report screenSpaceRefraction "
                        "supported on WebGL2 - the ES-300 water gate did not "
                        "open", log)
        if EXIT_MARKER not in log:
            fail_render(browser, subject,
                        "the water fixture did not reach the orderly shutdown "
                        "- the ES-300 water may have faulted at render", log)
        print("run_export_web: advanced water activated on WebGL2 (ES-300 "
              "grab-pass, no fallback)", flush=True)
    finally:
        server.shutdown()


def _screenshot_glow_stats(output_dir, browser, port, name, extra_query=""):
    """capture one screenshot of the static bloom scene and return
    (total sampled luminance, count of mid-band 'halo' pixels, sample count).

    The scene is static, so any post-boot frame is identical - the only thing
    that varies between calls is the bloom toggle. The discriminator is the
    MID-BAND pixel count: the emissive cube core is saturated bright, the
    background is black. With bloom OFF the transition is a hard anti-aliased
    edge (very few mid-luminance pixels); with bloom ON the additive blurred
    highlights lay a soft glow GRADIENT around the silhouette - a large
    population of mid-luminance pixels that did not exist before. That gradient
    is the halo, so its pixel count is a robust, localised proxy for the glow."""
    shot = os.path.join(output_dir, name)
    url = "http://127.0.0.1:%d/index.html%s" % (port, extra_query)
    log = run_browser(browser, url, deadline_seconds=180, screenshot=shot,
                      budget_ms=9000)
    if not os.path.isfile(shot) or os.path.getsize(shot) == 0:
        fail_render(browser, "the LDR bloom compositor on WebGL2",
                    "no screenshot written (%s)" % name, log)
    image = orkige_png.decode_png(shot)
    total = 0.0
    halo = 0
    samples = 0
    stride = 4  # every pixel - the halo band is thin, do not undersample it
    for off in range(0, len(image.pixels) - 4, stride):
        r = image.pixels[off]
        g = image.pixels[off + 1]
        b = image.pixels[off + 2]
        lum = 0.299 * r + 0.587 * g + 0.114 * b
        total += lum
        if 25.0 < lum < 205.0:  # the soft glow gradient (0..255 scale)
            halo += 1
        samples += 1
    return total, halo, samples


def assert_bloom(output_dir, browser):
    """boot the exported bloom fixture and prove the LDR bloom compositor
    activated on the real WebGL2/GLES3 context: the probe reports the capability
    true, the ES-300 quad-pass chain builds without a fallback refusal, the run
    renders through to the orderly shutdown (a shader link error would abort
    first), AND a rendered-glow pixel proof - the same static scene is brighter
    with bloom on than off (bloom's additive combine can only ADD luminance, so
    the surplus is the halo spilling around the emissive cube)."""
    server, port = serve(output_dir)
    try:
        # leg 1: capability + clean shutdown. Frame-capped so the bloom chain
        # actually renders (the ES-300 quad programs link on first use).
        url = ("http://127.0.0.1:%d/index.html"
               "?env.ORKIGE_DEMO_FRAMES=90&env.ORKIGE_DEMO_FPS_LOG=1" % port)
        log = run_browser(browser, url, deadline_seconds=180,
                          needed_markers=(BLOOM_SUPPORTED_MARKER, EXIT_MARKER))
        subject = "the LDR bloom compositor on WebGL2"
        for failed in BLOOM_SETUP_FAILED:
            if failed in log:
                # the backend's own refusal: evidence, not a gap
                fail("bloom reported unsupported/failed on WebGL2 ('%s') - the "
                     "GLSL ES 3.0 compositor gate did not open - full log:\n%s"
                     % (failed, log))
        if BLOOM_SUPPORTED_MARKER not in log:
            fail_render(browser, subject,
                        "the fixture did not report bloom supported on WebGL2 "
                        "- the bloom gate did not open", log)
        if EXIT_MARKER not in log:
            fail_render(browser, subject,
                        "the bloom fixture did not reach the orderly shutdown "
                        "- the bloom compositor may have faulted at render",
                        log)
        print("run_export_web: bloom capability + clean shutdown OK",
              flush=True)

        # leg 2: the rendered-glow pixel proof. Two screenshots of the SAME
        # static scene - bloom on (default r.bloomQuality) vs off (via
        # ORKIGE_CVARS). Bloom lays a soft halo gradient around the emissive
        # cube: a large population of mid-luminance pixels the hard-edged
        # bloom-off frame does not have. That halo-pixel surplus (and the extra
        # total luminance) is the additive glow the compositor produced.
        lum_on, halo_on, samples = _screenshot_glow_stats(
            output_dir, browser, port, "bloom_on.png")
        off_query = "?env.ORKIGE_CVARS=" + urllib.parse.quote(
            "r.bloomQuality=off")
        lum_off, halo_off, _ = _screenshot_glow_stats(
            output_dir, browser, port, "bloom_off.png", extra_query=off_query)
        # the halo must be a clear population (guards against a stray edge
        # pixel passing a bare inequality) AND clearly exceed the bloom-off
        # baseline, and the frame must gain total luminance (additive combine)
        if not (halo_on > halo_off * 1.5 and halo_on - halo_off > 500 and
                lum_on > lum_off * 1.01):
            fail("no measurable glow: bloom-on halo pixels %d vs bloom-off %d "
                 "(of %d sampled), luminance on=%.0f off=%.0f - the compositor "
                 "rendered but added no halo - check bloom_on.png / "
                 "bloom_off.png"
                 % (halo_on, halo_off, samples, lum_on, lum_off))
        print("run_export_web: rendered glow proven (halo pixels on=%d off=%d, "
              "+%d; total luminance on=%.0f off=%.0f, +%.1f%%)"
              % (halo_on, halo_off, halo_on - halo_off, lum_on, lum_off,
                 100.0 * (lum_on - lum_off) / max(lum_off, 1.0)), flush=True)
    finally:
        server.shutdown()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine-build")
    parser.add_argument("--output")
    parser.add_argument("--mode",
                        choices=["structure", "boot", "roller", "water",
                                 "bloom"],
                        default="structure")
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the pure parts and exit")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    for name in ("engine_build", "output"):
        if not getattr(args, name):
            parser.error("--" + name.replace("_", "-") + " is required")

    browser = ""
    if args.mode in ("boot", "roller", "water", "bloom"):
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
    elif args.mode == "bloom":
        project = build_bloom_fixture(output + "_project")
        title = "Bloom Fixture"
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
    elif args.mode == "bloom":
        assert_bloom(output, browser)
    print("run_export_web: PASSED (%s)" % args.mode, flush=True)
    return 0


# --- selftest ---------------------------------------------------------------

#: the two shapes a real Chrome writes to its stderr log for the probe page,
#: captured from a headless run with the GPU allowed and with it denied. They
#: are the fixtures because the parsing has to survive the console line's own
#: framing (the quoting and the trailing source reference), not a tidied twin.
PROBE_LOG_GPU = (
    '[1:2:0101/000000.000000:INFO:CONSOLE:1] "ORKIGE_WEBGL: WebKit WebGL", '
    'source: data:text/html... (1)\n')
PROBE_LOG_NONE = (
    '[1:2:0101/000000.000000:INFO:CONSOLE:1] "ORKIGE_WEBGL: none", '
    'source: data:text/html... (1)\n'
    '[1:2:0101/000000.000001:ERROR:content/browser/gpu/gpu_process_host.cc'
    ':1035] GPU process exited unexpectedly: exit_code=15\n')


def selftest():
    # the probe answers all three ways, and each answer carries its detail
    verdict, detail = probe_verdict(PROBE_LOG_GPU)
    assert (verdict, detail) == ("gpu", "WebKit WebGL"), (verdict, detail)
    verdict, detail = probe_verdict(PROBE_LOG_NONE)
    assert verdict == "none", verdict
    assert "GPU process exited unexpectedly" in detail, detail
    # a page refused a context with the browser saying nothing about why is
    # still a refusal, and says so in its own words
    verdict, detail = probe_verdict('x "ORKIGE_WEBGL: none", source: y\n')
    assert verdict == "none" and "refused a WebGL context" in detail, detail
    # a browser that never ran the page is NOT turned into a GPU verdict
    assert probe_verdict("")[0] == "unanswered"
    assert probe_verdict("some unrelated chatter\n")[0] == "unanswered"

    # the browser's own words are read where they exist and nowhere else -
    # a feature's absence must never be mistaken for a missing GPU
    assert "kTransientFailure" in gpu_absence_detail(
        "[ERROR:command_buffer_proxy_impl.cc(129)] "
        "ContextResult::kTransientFailure: Failed to send "
        "GpuControl.CreateCommandBuffer.\n")
    assert gpu_absence_detail(
        "water_probe: screenSpaceRefraction=false\n"
        "refraction setup failed\n") == ""

    # what a leg that rendered nothing reports, per answer. The absent GPU is
    # its OWN outcome and skips; a context that WAS handed out makes the
    # feature verdict earned; an unanswered probe claims neither.
    code, said = render_failure_report(
        "the advanced water on WebGL2", "the ES-300 water gate did not open",
        "none", "GPU process exited unexpectedly", "boot log")
    assert code == 77, said
    assert NO_GPU_CONTEXT in said and "SKIPPED" in said, said
    assert "no verdict on the advanced water on WebGL2" in said, said
    assert "ES-300" not in said, said         # the feature is not blamed

    code, said = render_failure_report(
        "the advanced water on WebGL2", "the ES-300 water gate did not open",
        "gpu", "ANGLE (SwiftShader)", "boot log")
    assert code == 1 and "FAILED" in said, said
    assert "ES-300 water gate did not open" in said, said
    assert "provided a GPU context (ANGLE (SwiftShader))" in said, said
    assert "is not a missing GPU" in said, said
    assert NO_GPU_CONTEXT not in said, said

    code, said = render_failure_report(
        "the advanced water on WebGL2", "the ES-300 water gate did not open",
        "unanswered", "", "boot log")
    assert code == 1 and "probe did not answer" in said, said
    assert NO_GPU_CONTEXT not in said, said

    # every report carries the whole captured log, which is where a real cause
    # is read from
    for verdict in ("none", "gpu", "unanswered"):
        assert "boot log" in render_failure_report(
            "s", "m", verdict, "d", "boot log")[1]

    # the probe URL is the page the fixtures came from: it asks for the same
    # context the player asks for, and prints the marker the parsing reads
    assert "getContext('webgl2')" in WEBGL_PROBE_URL
    assert WEBGL_PROBE_MARKER in WEBGL_PROBE_URL
    assert WEBGL_PROBE_NONE in WEBGL_PROBE_URL

    print("run_export_web: selftest OK", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
