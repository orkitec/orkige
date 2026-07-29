#!/usr/bin/env python3
"""ctest driver for the benchmark showcase (projects/benchmark).

Runs the standalone player over the whole scene sequence, ARMED
(ORKIGE_BENCHMARK) and driven fast + deterministically: a tiny
benchmark.sceneScale (through the ORKIGE_CVARS env the player forwards to the
cvar system) shrinks every attract-mode scene to a handful of frames, the wipe
transition is disabled, and ORKIGE_DEMO_FRAMES caps the run past a full loop.
Then it asserts the JSONL results artifact parses and carries EVERY scene of
the sequence (by its recorder label) plus a clean summary.

    run_benchmark_test.py --repo <root> --player <path> --dir <scratch>

The autonomous director (no input) is what makes this headless-checkable: the
sequence advances itself, so a clean exit + a complete artifact is the whole
contract.

Exit codes: 0 pass, 1 fail.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchmark_breadcrumbs  # noqa: E402
from benchmark_skip import TOUR_ORDER, resolve_skip_scenes  # noqa: E402


def log(msg):
    print("run_benchmark_test: " + msg, flush=True)


def fail(msg):
    print("run_benchmark_test: FAILED - " + msg, flush=True)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--frames", type=int, default=400)
    parser.add_argument("--wipe", type=int, default=0,
                        help="1 = the interactive fade-wipe scene switch "
                             "(the default path a human sees); 0 = the bare "
                             "switch (fastest deterministic traversal)")
    args = parser.parse_args()

    repo = Path(args.repo)
    out = Path(args.dir)
    if out.exists():
        for f in out.glob("*.jsonl"):
            f.unlink()
    out.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.update({
        "ORKIGE_BENCHMARK": "1",
        "ORKIGE_BENCHMARK_DIR": str(out),
        "ORKIGE_BENCHMARK_MODE": "smoke",
        # tiny scene scale => the whole loop traverses in a deterministic,
        # small number of frames regardless of headless fps; --wipe 1 keeps
        # the interactive fade-wipe switch on so BOTH switch paths stay covered
        "ORKIGE_CVARS": "benchmark.sceneScale=0.02,benchmark.wipe=%d" % args.wipe,
        "ORKIGE_DEMO_FRAMES": str(args.frames),
        # keep the progression/save files out of the user dir
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": str(out),
    })
    # the per-host scene-skip quarantine (win32 default + any manual env); writes
    # the resolved ORKIGE_CVAR_benchmark_skipScenes back into env for the player
    skip_scenes = resolve_skip_scenes(env)
    if skip_scenes:
        log("benchmark.skipScenes = %s (tour walks straight past these)"
            % ",".join(sorted(skip_scenes)))
    # the labels the artifact must carry: the whole tour minus any skipped scene
    expected_labels = [label for base, label in TOUR_ORDER
                       if base not in skip_scenes]
    # the crash-breadcrumb proof scene (a mid-tour scene load the trail must
    # name): mirrorlake by default; when it is quarantined, the scene loaded in
    # its place (lumens) is the proof, and a wider custom skip set falls back to
    # the first surviving mid-tour scene
    crumb_scene = "mirrorlake"
    if crumb_scene in skip_scenes:
        crumb_scene = next((b for b, _ in TOUR_ORDER[1:]
                            if b not in skip_scenes), TOUR_ORDER[0][0])
    # NOTE: no SDL_VIDEODRIVER override - the player needs a real render context
    # (a window on the dev macOS display, xvfb/llvmpipe on CI), exactly like the
    # other player selfchecks. Forcing the dummy driver breaks classic GL setup.
    # crash-breadcrumb trail beside the run: flushed to disk per entry, so a
    # hard abort (this driver's vista_wipe leg has twice hit a silent exit 3 on
    # the mirrorlake switch) still names the last scene reached
    benchmark_breadcrumbs.arm(env, out)

    cmd = [args.player, "--project", str(repo / "projects/benchmark")]
    log("running: " + " ".join(cmd))
    # wall budget, not a pace assertion: the run is frame-paced (2600 frames on
    # the wipe leg), so its wall time scales with the machine - a loaded hosted
    # CI runner has measured 4x slower than a dev laptop on the same commit
    result = subprocess.run(cmd, cwd=str(repo), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=480)
    tail = result.stdout.decode("utf-8", "replace")[-1500:]
    if result.returncode != 0:
        # a hard abort (exit 3 = a Debug abort/assert on Windows) may lose the
        # buffered stdout - dump the flushed breadcrumb trail so the last scene
        # reached (the mirrorlake switch) is named even then
        benchmark_breadcrumbs.dump_on_failure(out, log)
        log(tail)
        fail("player exited %d" % result.returncode)

    artifacts = sorted(out.glob("*.jsonl"))
    if not artifacts:
        log(tail)
        fail("no benchmark-*.jsonl artifact written")
    artifact = artifacts[-1]
    log("artifact: " + artifact.name)

    meta = None
    summary = None
    scene_frames = {}
    for lineno, line in enumerate(artifact.read_text().splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as exc:
            fail("line %d is not valid JSON: %s" % (lineno, exc))
        kind = obj.get("type")
        if kind == "meta":
            meta = obj
        elif kind == "summary":
            summary = obj
        elif kind == "scene":
            name = obj.get("name", "")
            frames = int(obj.get("frames", 0))
            scene_frames[name] = max(scene_frames.get(name, 0), frames)
            # a real scene record carries a measured average frame time
            if frames >= 2 and "frameMs" not in obj:
                fail("scene '%s' has no frameMs block" % name)

    if meta is None:
        fail("no meta line in the artifact")
    if meta.get("project") != "Benchmark":
        fail("meta.project is '%s', expected 'Benchmark'" % meta.get("project"))
    if summary is None:
        fail("no summary line (run did not finalize cleanly)")
    if summary.get("aborted") is not False:
        fail("summary.aborted is %r, expected false" % summary.get("aborted"))

    # every EXPECTED scene (the tour minus any skipped vignette) must have been
    # visited with a real record; a skipped scene must be ABSENT (proving the
    # skip took, not that it silently rendered anyway)
    missing = [lbl for lbl in expected_labels
               if scene_frames.get(lbl, 0) < 2]
    if missing:
        log("recorded scenes: " + ", ".join(
            "%s=%d" % (k, v) for k, v in sorted(scene_frames.items())))
        fail("scenes never recorded (>=2 frames): " + ", ".join(missing))
    skipped_labels = [label for base, label in TOUR_ORDER
                      if base in skip_scenes]
    present_but_skipped = [lbl for lbl in skipped_labels
                           if scene_frames.get(lbl, 0) >= 2]
    if present_but_skipped:
        fail("scene(s) recorded despite benchmark.skipScenes (skip did not "
             "take): " + ", ".join(present_but_skipped))

    # the breadcrumb trail is present and names a mid-tour scene switch: proves
    # the plumbing a future silent exit-3 relies on is live on this exact leg
    benchmark_breadcrumbs.assert_present(out, fail, expect_scene=crumb_scene)

    log("OK: %d/%d scenes recorded, summary clean (%d total scene lines)%s" %
        (len(expected_labels), len(expected_labels), len(scene_frames),
         "" if not skip_scenes
         else "; skipped " + ",".join(sorted(skip_scenes))))


if __name__ == "__main__":
    main()
