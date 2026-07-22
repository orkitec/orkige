#!/usr/bin/env python3
"""Cross-flavor look-parity gate for the shared output grade.

The output grade (RenderWorld::setOutputGrade) is the ONE authored look stage
both render flavors run IDENTICALLY - the shared curve is core_util/GradeMath.
The render_facade_selfcheck grade leg boots the SAME deterministic scene grade
OFF and grade ON (a strong contrast 0.6 / saturation 1.4 setting) and prints a
machine-parseable metrics line per flavor:

    grade-metrics satOff=.. satOn=.. contrastOff=.. contrastOn=..

(satX = mean chroma deviation from luma; contrastX = a p90-p10 luminance
spread, over a grid on the graded 3D content). This driver runs both flavors'
selfcheck binaries and asserts:

  (a) each flavor's grade raises saturation AND contrast measurably (the grade
      actually did something on both);
  (b) the OFF metrics agree across flavors (WYSIWYG - the un-graded scene is
      already the same image, the render_backend_parity guarantee);
  (c) the ON-vs-OFF DELTAS match across flavors within tolerance - the
      cross-flavor look-parity guarantee: the shared curve moves both flavors
      the same way, so whatever look the owner dials stays matched by
      construction.

Cross-preset like render_backend_parity: the classic binary lives in another
build tree; when absent the test SKIPs (exit 77) instead of failing. Pure
stdlib (parses the selfcheck's stdout).
"""

import argparse
import os
import subprocess
import sys

SKIP_EXIT_CODE = 77

#: each flavor's grade must move saturation + contrast at least this much
#: (2x the ~0.004 8-bit readback noise floor - an honest "measurable" bar)
MIN_SAT_DELTA = 0.02
MIN_CONTRAST_DELTA = 0.008
#: the OFF metrics must agree across flavors this closely (WYSIWYG: the
#: un-graded scene is byte-parity content, so only readback noise separates them)
OFF_MATCH_TOLERANCE = 0.02
#: the induced DELTAS must match across flavors this closely (the shared-curve
#: guarantee; the two flavors' shading/colour-space paths differ slightly, so
#: the band is a few readback levels wide, not zero)
DELTA_MATCH_TOLERANCE = 0.03

METRICS_MARKER = "grade-metrics"


def parse_metrics(output):
    """Pull satOff/satOn/contrastOff/contrastOn from a selfcheck's stdout."""
    for line in output.splitlines():
        if METRICS_MARKER not in line:
            continue
        fields = {}
        for token in line.split():
            if "=" in token:
                key, _, value = token.partition("=")
                try:
                    fields[key] = float(value)
                except ValueError:
                    pass
        if {"satOff", "satOn", "contrastOff", "contrastOn"} <= set(fields):
            return fields
    return None


def run_selfcheck(binary, out_dir, cwd):
    os.makedirs(out_dir, exist_ok=True)
    environment = dict(os.environ)
    environment["ORKIGE_SELFCHECK_OUT"] = out_dir
    result = subprocess.run([binary], cwd=cwd, env=environment,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=120)
    text = result.stdout.decode("utf-8", "replace")
    if result.returncode != 0:
        sys.stdout.write(text)
        raise RuntimeError(f"{binary} exited with {result.returncode}")
    return text


def check_flavor(name, metrics):
    """Assert one flavor's grade moved the look measurably. Returns failures."""
    if metrics is None:
        print(f"FAIL {name}: no grade-metrics line "
              f"(is RenderCaps::OutputGrade supported on this flavor?)")
        return 1, None
    sat_delta = metrics["satOn"] - metrics["satOff"]
    contrast_delta = metrics["contrastOn"] - metrics["contrastOff"]
    failures = 0
    ok = sat_delta >= MIN_SAT_DELTA
    print(f"{'ok  ' if ok else 'FAIL'} {name} saturation delta "
          f"{sat_delta:+.4f} (>= {MIN_SAT_DELTA})")
    failures += 0 if ok else 1
    ok = contrast_delta >= MIN_CONTRAST_DELTA
    print(f"{'ok  ' if ok else 'FAIL'} {name} contrast delta "
          f"{contrast_delta:+.4f} (>= {MIN_CONTRAST_DELTA})")
    failures += 0 if ok else 1
    return failures, (sat_delta, contrast_delta)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--next-binary", required=True,
                        help="this build tree's render_facade_selfcheck")
    parser.add_argument("--classic-binary", required=True,
                        help="the classic tree's render_facade_selfcheck "
                             "(SKIP when absent)")
    parser.add_argument("--out", required=True,
                        help="working directory for both runs")
    parser.add_argument("--repo", required=True,
                        help="repo root (the selfcheck's working directory)")
    args = parser.parse_args()

    if not os.path.exists(args.classic_binary):
        print(f"SKIP: classic selfcheck binary not built "
              f"({args.classic_binary}) - configure + build the classic "
              f"preset to enable the cross-flavor grade look-parity gate")
        return SKIP_EXIT_CODE

    print(f"running classic selfcheck: {args.classic_binary}")
    classic_text = run_selfcheck(
        args.classic_binary, os.path.join(args.out, "classic"), args.repo)
    print(f"running next selfcheck: {args.next_binary}")
    next_text = run_selfcheck(
        args.next_binary, os.path.join(args.out, "next"), args.repo)

    classic = parse_metrics(classic_text)
    nxt = parse_metrics(next_text)

    failures = 0
    classic_fail, classic_deltas = check_flavor("classic", classic)
    next_fail, next_deltas = check_flavor("next", nxt)
    failures += classic_fail + next_fail

    if classic is not None and nxt is not None:
        # (b) the un-graded scene already matches across flavors (WYSIWYG)
        for key in ("satOff", "contrastOff"):
            diff = abs(classic[key] - nxt[key])
            ok = diff <= OFF_MATCH_TOLERANCE
            print(f"{'ok  ' if ok else 'FAIL'} grade-off {key} agrees "
                  f"(classic {classic[key]:.4f} vs next {nxt[key]:.4f}, "
                  f"diff {diff:.4f} <= {OFF_MATCH_TOLERANCE})")
            failures += 0 if ok else 1

    if classic_deltas is not None and next_deltas is not None:
        # (c) the INDUCED deltas match across flavors - the shared-curve guarantee
        labels = ("saturation", "contrast")
        for label, cd, nd in zip(labels, classic_deltas, next_deltas):
            diff = abs(cd - nd)
            ok = diff <= DELTA_MATCH_TOLERANCE
            print(f"{'ok  ' if ok else 'FAIL'} {label} grade delta matches "
                  f"across flavors (classic {cd:+.4f} vs next {nd:+.4f}, "
                  f"diff {diff:.4f} <= {DELTA_MATCH_TOLERANCE})")
            failures += 0 if ok else 1

    if failures:
        print(f"grade_look_parity: {failures} check(s) failed - the shared "
              f"grade curve must move both flavors the same way "
              f"(Docs/render-abstraction.md, the look-parity guarantee)")
        return 1
    print("grade_look_parity: both flavors grade identically within tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())
