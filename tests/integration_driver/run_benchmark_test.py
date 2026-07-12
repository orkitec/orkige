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

# every scene's recorder label (the director's sceneLabel export); these are the
# lines the artifact must contain for the run to have visited the whole tour.
EXPECTED_LABELS = [
    "Terrace Vista", "Still Water", "Night Lumens", "Ember Swarm",
    "Instance Field", "Flatland", "Console", "Cascade", "Tally",
]


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
    # NOTE: no SDL_VIDEODRIVER override - the player needs a real render context
    # (a window on the dev macOS display, xvfb/llvmpipe on CI), exactly like the
    # other player selfchecks. Forcing the dummy driver breaks classic GL setup.

    cmd = [args.player, "--project", str(repo / "projects/benchmark")]
    log("running: " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=str(repo), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=180)
    tail = result.stdout.decode("utf-8", "replace")[-1500:]
    if result.returncode != 0:
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

    # every scene of the sequence must have been visited with a real record
    missing = [lbl for lbl in EXPECTED_LABELS
               if scene_frames.get(lbl, 0) < 2]
    if missing:
        log("recorded scenes: " + ", ".join(
            "%s=%d" % (k, v) for k, v in sorted(scene_frames.items())))
        fail("scenes never recorded (>=2 frames): " + ", ".join(missing))

    log("OK: %d/%d scenes recorded, summary clean (%d total scene lines)" %
        (len(EXPECTED_LABELS), len(EXPECTED_LABELS), len(scene_frames)))


if __name__ == "__main__":
    main()
