#!/usr/bin/env python3
"""The standing structural budget gate: per-scene, per-flavor draw-call and
triangle budgets over the benchmark tour.

Walks every benchmark scene in ONE deterministic boot each (ramps forced to
their ceiling, camera orbit frozen - the run_benchmark_scene_probe recipe),
reads the BenchmarkRecorder scene line and compares the structural numbers
against the checked-in budget table (benchmark_budgets.json):

  * batchesMax - the draw-batch ceiling. A breach means a batching/facade
    regression (something started costing draws it should not).
  * batchesMin - the floor. Dramatically under budget on one flavor means
    something STOPPED rendering (the silent-black regression class) - worth
    failing for, not celebrating.
  * trisMax - the triangle ceiling (content or LOD regressions).

Frame-ms is REPORTED but never gated (machine noise). The budgets update
DELIBERATELY: an optimization or content change that moves a number edits
benchmark_budgets.json in the same commit, with the reason in the message
(the pixel-probe baseline discipline; rationale in Docs/performance.md).

Pure stdlib. Exit codes: 0 pass, 1 fail.
"""

import argparse
import json
import os
import subprocess
import sys


def log(msg):
    print("run_benchmark_budget_test: " + msg, flush=True)


def fail(msg):
    print("run_benchmark_budget_test: FAILED - " + msg, flush=True)
    sys.exit(1)


def run_scene(args, scene_file, outdir):
    os.makedirs(outdir, exist_ok=True)
    for old in os.listdir(outdir):
        if old.startswith("benchmark-"):
            os.unlink(os.path.join(outdir, old))
    env = dict(os.environ)
    env.update({
        "ORKIGE_BENCHMARK": "1",
        "ORKIGE_BENCHMARK_DIR": outdir,
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": outdir,
        "ORKIGE_DEMO_FRAMES": "240",
        # deterministic structure: ramps to their ceiling, no wall-clock
        # camera dependence (the director's automation seams)
        "ORKIGE_CVARS": "benchmark.rampBudgetMs=100000,benchmark.cameraOrbit=0",
    })
    cmd = [args.player, "scenes/" + scene_file, "--project",
           os.path.join(args.repo, "projects/benchmark")]
    result = subprocess.run(cmd, cwd=args.repo, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=300)
    if result.returncode != 0:
        log(result.stdout.decode("utf-8", "replace")[-1200:])
        fail("%s: player exited %d" % (scene_file, result.returncode))
    artifact = [f for f in os.listdir(outdir) if f.startswith("benchmark-")]
    if not artifact:
        fail("%s: no benchmark artifact" % scene_file)
    with open(os.path.join(outdir, artifact[0])) as fh:
        for line in fh:
            record = json.loads(line)
            if record.get("type") == "scene":
                return record
    fail("%s: no scene record in the artifact" % scene_file)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--player", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--flavor", required=True, choices=("next", "classic"))
    parser.add_argument("--budgets", required=True,
                        help="the checked-in benchmark_budgets.json")
    args = parser.parse_args()

    with open(args.budgets) as fh:
        budgets = json.load(fh)

    failures = []
    for scene_file, per_flavor in sorted(budgets["scenes"].items()):
        budget = per_flavor.get(args.flavor)
        if budget is None:
            continue
        record = run_scene(args, scene_file,
                           os.path.join(args.dir, scene_file.split(".")[0]))
        batches = record["gpu"]["batchesAvg"]
        tris = record["gpu"]["trisAvg"]
        frame_ms = record["frameMs"]["avg"]
        verdict = "ok"
        if batches > budget["batchesMax"]:
            verdict = "OVER draw budget"
            failures.append("%s: %.1f batches > max %d (a batching/facade "
                            "regression)" % (scene_file, batches,
                                             budget["batchesMax"]))
        elif batches < budget["batchesMin"]:
            verdict = "UNDER draw floor"
            failures.append("%s: %.1f batches < min %d (something stopped "
                            "rendering)" % (scene_file, batches,
                                            budget["batchesMin"]))
        if tris > budget["trisMax"]:
            verdict = "OVER triangle budget"
            failures.append("%s: %.0f tris > max %d" % (scene_file, tris,
                                                        budget["trisMax"]))
        log("%-18s batches %7.1f (%d..%d)  tris %9.0f (<=%d)  "
            "frameMs %6.2f [reported only]  %s"
            % (scene_file, batches, budget["batchesMin"],
               budget["batchesMax"], tris, budget["trisMax"], frame_ms,
               verdict))
    if failures:
        for each in failures:
            log("BREACH: " + each)
        fail("%d structural budget breach(es)" % len(failures))
    log("OK")


if __name__ == "__main__":
    main()
