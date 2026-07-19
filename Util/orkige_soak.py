#!/usr/bin/env python3
"""Long-run soak driver for the benchmark showcase (projects/benchmark).

A 3-second unit run cannot surface a SLOW leak or a long-run fault - those need
the game to run for minutes, cycling its scenes. This driver runs the standalone
player over the benchmark tour in ATTRACT-LOOP mode (the exported
`benchmark.loop=1` cvar) for a BOUNDED frame budget, samples the process's
resident set size (RSS) across the run, and FAILS if the player crashes/exits
non-zero OR if RSS trends upward past a documented slope ceiling.

    orkige_soak.py --repo <root> --player <path> [--frames N] [options]

Reuse, not new telemetry: the run harness is the same env the benchmark ctest
driver uses (ORKIGE_BENCHMARK + benchmark.sceneScale, plus benchmark.loop=1 to
keep the attract tour cycling), and RSS is the operating system's own accounting
- the SAME number engine_core's MemorySampler queries (task_info / /proc), read
here from the child process with `ps` so the driver needs nothing but the
standard library and adds no in-engine hooks. A leak in a scene's load/unload
shows as a rising floor across the loop; a flat trend is the pass.

Because it is a scheduled (nightly) job, the whole run is minutes, not hours:
long enough to expose a per-cycle leak, short enough for a hosted runner.

    --selftest   validate the pure RSS-trend math on synthetic samples and exit
                 (no player needed; the stdlib-only local check)

Exit codes: 0 pass, 1 fail (crash, non-zero exit, or an RSS slope over ceiling).
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


def log(msg):
    print("orkige_soak: " + msg, flush=True)


def fail(msg):
    print("orkige_soak: FAILED - " + msg, flush=True)
    sys.exit(1)


def linear_slope_kb_per_sec(samples):
    """Least-squares slope of rss(kb) over time(sec). samples: [(t, kb), ...].

    Returns the slope in KB/second, or 0.0 when fewer than two distinct times.
    Pure arithmetic - no numpy, so the driver stays stdlib-only.
    """
    n = len(samples)
    if n < 2:
        return 0.0
    sum_t = sum(t for t, _ in samples)
    sum_v = sum(v for _, v in samples)
    mean_t = sum_t / n
    mean_v = sum_v / n
    num = sum((t - mean_t) * (v - mean_v) for t, v in samples)
    den = sum((t - mean_t) ** 2 for t, _ in samples)
    if den == 0.0:
        return 0.0
    return num / den


def analyse(samples, warmup_frac, max_slope_mb_per_min, max_peak_mb):
    """Verdict over the RSS samples. Returns (ok, report_lines)."""
    lines = []
    peak_mb = max(v for _, v in samples) / 1024.0
    lines.append("samples: %d, peak RSS: %.1f MB" % (len(samples), peak_mb))

    # discard the warmup: the first frames pay one-time boot + first-scene
    # allocations (shader compile, texture uploads) that are NOT a leak; the
    # trend that matters is the steady-state floor across the attract loop
    keep_from = int(len(samples) * warmup_frac)
    steady = samples[keep_from:]
    if len(steady) < 3:
        # a short local dry-run: too few steady samples to judge a trend, so
        # the slope gate is inapplicable - a clean exit is the whole verdict
        lines.append("steady-state samples: %d (< 3) - slope gate skipped "
                     "(short run); clean exit is the pass" % len(steady))
        ok = True
    else:
        # rebase time to the first steady sample so the slope reads honestly
        base_t = steady[0][0]
        rebased = [(t - base_t, v) for t, v in steady]
        slope_kb_s = linear_slope_kb_per_sec(rebased)
        slope_mb_min = slope_kb_s * 60.0 / 1024.0
        lines.append("steady-state RSS slope: %.3f MB/min (ceiling %.3f MB/min)"
                     % (slope_mb_min, max_slope_mb_per_min))
        ok = slope_mb_min <= max_slope_mb_per_min
        if not ok:
            lines.append("RSS is trending UP past the ceiling - a leak suspect")

    if max_peak_mb > 0 and peak_mb > max_peak_mb:
        lines.append("peak RSS %.1f MB over the %.1f MB ceiling"
                     % (peak_mb, max_peak_mb))
        ok = False
    return ok, lines


def run_selftest():
    """Exercise the trend math on synthetic samples - the stdlib-only gate."""
    # a flat trace (noise only) must PASS the slope gate
    flat = [(float(i), 100000.0 + (i % 3) * 50.0) for i in range(40)]
    ok, lines = analyse(flat, 0.3, 8.0, 0.0)
    for line in lines:
        log("  flat: " + line)
    if not ok:
        fail("selftest: a flat RSS trace was wrongly flagged as leaking")

    # a steadily rising trace (~30 MB/min) must FAIL the 8 MB/min gate
    rising = [(float(i), 100000.0 + i * 512.0) for i in range(40)]  # 512 KB/s
    ok, lines = analyse(rising, 0.3, 8.0, 0.0)
    for line in lines:
        log("  rising: " + line)
    if ok:
        fail("selftest: a rising RSS trace was not flagged as leaking")

    # the peak ceiling independently fails an otherwise-flat trace
    ok, _ = analyse(flat, 0.3, 8.0, 50.0)  # 50 MB ceiling, ~97 MB peak
    if ok:
        fail("selftest: the peak ceiling did not fire on an over-peak trace")

    log("selftest OK: flat passes, rising fails, peak ceiling fires")
    sys.exit(0)


def sample_rss_kb(pid):
    """The child's resident set size in KB via `ps`, or None if unavailable.

    ps reports RSS in kilobytes on both macOS and Linux - the process-RSS
    metric MemorySampler queries in-engine, read from outside so no telemetry
    plumbing is needed.
    """
    try:
        out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    text = out.stdout.decode("ascii", "replace").strip()
    if not text:
        return None
    try:
        return float(text.split()[0])
    except (ValueError, IndexError):
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true",
                        help="validate the RSS-trend math and exit (no player)")
    parser.add_argument("--repo")
    parser.add_argument("--player")
    parser.add_argument("--project", default="projects/benchmark",
                        help="project the player loops (relative to --repo)")
    parser.add_argument("--frames", type=int, default=20000,
                        help="frame budget: the attract loop runs this many "
                             "frames then exits (bounds the soak)")
    parser.add_argument("--scene-scale", type=float, default=0.05,
                        help="benchmark.sceneScale - smaller = more scene "
                             "load/unload cycles per frame budget (better leak "
                             "coverage)")
    parser.add_argument("--interval", type=float, default=2.0,
                        help="RSS sampling period in seconds")
    parser.add_argument("--warmup-frac", type=float, default=0.3,
                        help="fraction of leading samples discarded before the "
                             "slope is measured (boot/first-scene one-offs)")
    parser.add_argument("--max-slope-mb-per-min", type=float, default=8.0,
                        help="fail if steady-state RSS climbs faster than this")
    parser.add_argument("--max-peak-mb", type=float, default=0.0,
                        help="optional absolute peak-RSS ceiling (0 = off)")
    parser.add_argument("--timeout", type=float, default=1800.0,
                        help="hard wall-time cap for the whole run (seconds)")
    parser.add_argument("--dir",
                        help="scratch dir for benchmark artifacts (default: a "
                             "'soak' dir beside the player)")
    args = parser.parse_args()

    if args.selftest:
        run_selftest()

    if not args.repo or not args.player:
        fail("--repo and --player are required (or pass --selftest)")

    repo = Path(args.repo)
    player = Path(args.player)
    if not player.exists():
        fail("player binary not found: %s" % player)
    project = repo / args.project
    if not project.exists():
        fail("project not found: %s" % project)

    out = Path(args.dir) if args.dir else player.parent / "soak"
    out.mkdir(parents=True, exist_ok=True)
    for f in out.glob("*.jsonl"):
        f.unlink()

    env = dict(os.environ)
    env.update({
        "ORKIGE_BENCHMARK": "1",
        "ORKIGE_BENCHMARK_DIR": str(out),
        "ORKIGE_BENCHMARK_MODE": "smoke",
        # benchmark.loop=1 keeps the attract tour cycling instead of holding on
        # the results card; the small sceneScale packs many load/unload cycles
        # into the frame budget, and ORKIGE_DEMO_FRAMES bounds the whole soak
        "ORKIGE_CVARS": "benchmark.loop=1,benchmark.sceneScale=%g,benchmark.wipe=0"
                        % args.scene_scale,
        "ORKIGE_DEMO_FRAMES": str(args.frames),
        # keep progression/save files out of the user's home dir
        "ORKIGE_PROGRESS_RESET": "1",
        "ORKIGE_PROGRESS_DIR": str(out),
    })
    # no SDL_VIDEODRIVER override: the player needs a real render context (a
    # window on the display, xvfb/lavapipe on CI) - the same rule the benchmark
    # ctest driver documents.

    cmd = [str(player), "--project", str(project)]
    log("running: " + " ".join(cmd))
    log("frame budget %d, sceneScale %g, sampling RSS every %.1fs"
        % (args.frames, args.scene_scale, args.interval))

    proc = subprocess.Popen(cmd, cwd=str(repo), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    samples = []
    start = time.monotonic()
    try:
        while True:
            code = proc.poll()
            now = time.monotonic() - start
            if code is not None:
                break
            if now > args.timeout:
                proc.kill()
                proc.wait()
                fail("wall-time cap %.0fs exceeded - killed the player"
                     % args.timeout)
            rss = sample_rss_kb(proc.pid)
            if rss is not None:
                samples.append((now, rss))
            time.sleep(args.interval)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    tail = proc.stdout.read().decode("utf-8", "replace")[-2000:]
    if proc.returncode != 0:
        log(tail)
        fail("player exited %d (crash or self-check failure)" % proc.returncode)

    if not samples:
        log(tail)
        fail("no RSS samples captured - the run was too short to sample "
             "(raise --frames or lower --interval)")

    log("RSS trace (%d samples over %.1fs):" % (len(samples), samples[-1][0]))
    for t, v in samples:
        log("  t=%6.1fs  rss=%8.1f MB" % (t, v / 1024.0))

    ok, lines = analyse(samples, args.warmup_frac,
                        args.max_slope_mb_per_min, args.max_peak_mb)
    for line in lines:
        log(line)
    if not ok:
        log(tail)
        fail("RSS trend/peak over ceiling - a leak or runaway growth")

    log("PASS: %d frames looped, RSS trend within ceiling, clean exit"
        % args.frames)


if __name__ == "__main__":
    main()
