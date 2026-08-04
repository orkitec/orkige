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

Two roads reach the same comparison, like render_backend_parity:

  * RUN BOTH BINARIES (--next-binary/--classic-binary): the developer road on
    a machine carrying both build trees; when the classic binary is absent
    the test SKIPs (exit 77) instead of failing.
  * COMPARE CAPTURED DIRECTORIES (--classic-shots/--next-shots): each flavor
    ran its selfcheck elsewhere and its output directory was carried here.
    The selfcheck writes the same metrics line it prints into a
    `grade_metrics.txt` sidecar in that directory, so the comparison needs
    nothing but the captured output. A directory without that sidecar FAILS -
    a look-parity gate that measured nothing must not report parity.

Pure stdlib (parses the selfcheck's stdout, or the sidecar carrying the same
line).
"""

import argparse
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile

SKIP_EXIT_CODE = 77

#: the sidecar the selfcheck writes beside its screenshots, carrying the same
#: metrics line it prints (one formatting, two sinks)
METRICS_SIDECAR = "grade_metrics.txt"

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


class MetricsUnusable(Exception):
    """Nothing to measure - refuse, never report parity by silence."""


def read_metrics_dir(label, directory):
    """Read one flavor's metrics out of a captured selfcheck directory.

    Refuses (rather than returning None) when the directory or its sidecar is
    absent or unreadable: a missing measurement is not a matching one.
    """
    if not os.path.isdir(directory):
        raise MetricsUnusable(f"{label} selfcheck output directory does not "
                              f"exist: {directory}")
    path = os.path.join(directory, METRICS_SIDECAR)
    if not os.path.exists(path):
        raise MetricsUnusable(
            f"{label} selfcheck output carries no {METRICS_SIDECAR} "
            f"({path}) - the grade leg did not run, so there is nothing to "
            f"compare")
    with open(path) as handle:
        metrics = parse_metrics(handle.read())
    if metrics is None:
        raise MetricsUnusable(f"{label} {path} carries no readable "
                              f"{METRICS_MARKER} line")
    return metrics


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--next-binary",
                        help="this build tree's render_facade_selfcheck")
    parser.add_argument("--classic-binary",
                        help="the classic tree's render_facade_selfcheck "
                             "(SKIP when absent)")
    parser.add_argument("--out",
                        help="working directory for both runs")
    parser.add_argument("--repo",
                        help="repo root (the selfcheck's working directory)")
    parser.add_argument("--classic-shots",
                        help="a classic selfcheck output directory captured "
                             "elsewhere (read as-is, nothing is run)")
    parser.add_argument("--next-shots",
                        help="a next selfcheck output directory captured "
                             "elsewhere (read as-is, nothing is run)")
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the pure parts and exit")
    return parser.parse_args(argv)


def resolve_metrics(args):
    """Produce (classic, next) metrics, running the binaries if asked.

    Returns None when the run road is unavailable and a skip is the honest
    answer; raises MetricsUnusable when captured output cannot be measured.
    """
    if args.classic_shots or args.next_shots:
        if not (args.classic_shots and args.next_shots):
            raise MetricsUnusable("--classic-shots and --next-shots come as "
                                  "a pair - one alone compares nothing")
        return (read_metrics_dir("classic", args.classic_shots),
                read_metrics_dir("next", args.next_shots))

    missing = [name for name, value in (("--next-binary", args.next_binary),
                                        ("--classic-binary",
                                         args.classic_binary),
                                        ("--out", args.out),
                                        ("--repo", args.repo)) if not value]
    if missing:
        raise MetricsUnusable("either the captured directories "
                              "(--classic-shots + --next-shots) or the full "
                              "run arguments are required; missing "
                              + ", ".join(missing))
    if not os.path.exists(args.classic_binary):
        return None

    print(f"running classic selfcheck: {args.classic_binary}")
    classic_text = run_selfcheck(
        args.classic_binary, os.path.join(args.out, "classic"), args.repo)
    print(f"running next selfcheck: {args.next_binary}")
    next_text = run_selfcheck(
        args.next_binary, os.path.join(args.out, "next"), args.repo)
    return parse_metrics(classic_text), parse_metrics(next_text)


def main(argv=None):
    args = parse_args(argv)
    if args.selftest:
        return selftest()

    try:
        measured = resolve_metrics(args)
    except MetricsUnusable as refusal:
        print(f"grade_look_parity: FAIL: {refusal}")
        return 1
    if measured is None:
        print(f"SKIP: classic selfcheck binary not built "
              f"({args.classic_binary}) - configure + build the classic "
              f"preset to enable the cross-flavor grade look-parity gate")
        return SKIP_EXIT_CODE
    classic, nxt = measured

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


# --- selftest ---------------------------------------------------------------

def write_metrics_dir(directory, sat_off, sat_on, contrast_off, contrast_on):
    """Write the sidecar a selfcheck leaves behind (selftest fixture)."""
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, METRICS_SIDECAR), "w") as handle:
        handle.write("%s satOff=%.4f satOn=%.4f contrastOff=%.4f "
                     "contrastOn=%.4f\n" % (METRICS_MARKER, sat_off, sat_on,
                                            contrast_off, contrast_on))


def run_quiet(argv):
    """Run main() swallowing its report - a passing selftest logs no FAIL."""
    captured = io.StringIO()
    with contextlib.redirect_stdout(captured):
        code = main(argv)
    return code, captured.getvalue()


def expect_refusal(what, argv, names):
    code, said = run_quiet(argv)
    if code != 1:
        raise AssertionError(f"{what} returned {code}, must refuse with 1")
    if names not in said:
        raise AssertionError(f"{what} refused without naming {names}: {said}")


def selftest():
    scratch = tempfile.mkdtemp(prefix="orkige_grade_selftest_")
    classic = os.path.join(scratch, "classic")
    nxt = os.path.join(scratch, "next")

    # the sidecar carries the SAME line the selfcheck prints, so one parser
    # reads both sources
    printed = ("render_facade_selfcheck: grade-metrics satOff=0.1000 "
               "satOn=0.2000 contrastOff=0.3000 contrastOn=0.4000")
    assert parse_metrics(printed) == {"satOff": 0.1, "satOn": 0.2,
                                      "contrastOff": 0.3, "contrastOn": 0.4}
    write_metrics_dir(classic, 0.1, 0.2, 0.3, 0.4)
    assert read_metrics_dir("classic", classic) == parse_metrics(printed)

    # both flavors move the same way: pass
    write_metrics_dir(nxt, 0.11, 0.21, 0.31, 0.41)
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 0

    # one flavor's grade barely moves: the per-flavor bar catches it
    write_metrics_dir(nxt, 0.11, 0.115, 0.31, 0.41)
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 1

    # both move, but by different amounts: the shared-curve check catches it
    write_metrics_dir(nxt, 0.11, 0.31, 0.31, 0.41)
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 1

    # the un-graded scenes disagree: the WYSIWYG check catches it
    write_metrics_dir(nxt, 0.30, 0.40, 0.31, 0.41)
    assert run_quiet(["--classic-shots", classic, "--next-shots", nxt])[0] == 1
    write_metrics_dir(nxt, 0.11, 0.21, 0.31, 0.41)

    # THE refusals: measuring nothing must never read as parity
    absent = os.path.join(scratch, "absent")
    expect_refusal("a missing directory",
                   ["--classic-shots", absent, "--next-shots", nxt], absent)
    empty = os.path.join(scratch, "empty")
    os.makedirs(empty, exist_ok=True)
    expect_refusal("a directory with no metrics sidecar",
                   ["--classic-shots", empty, "--next-shots", nxt],
                   METRICS_SIDECAR)
    blank = os.path.join(scratch, "blank")
    os.makedirs(blank, exist_ok=True)
    with open(os.path.join(blank, METRICS_SIDECAR), "w") as handle:
        handle.write("nothing measurable here\n")
    expect_refusal("a sidecar carrying no metrics line",
                   ["--classic-shots", blank, "--next-shots", nxt],
                   METRICS_MARKER)
    expect_refusal("one directory without the other",
                   ["--classic-shots", classic], "--next-shots")
    expect_refusal("no arguments at all", [], "--classic-shots")

    # the run road keeps its honest skip when the sibling tree is unbuilt
    assert run_quiet(["--next-binary", "/nonexistent/next",
                      "--classic-binary", "/nonexistent/classic",
                      "--out", scratch,
                      "--repo", scratch])[0] == SKIP_EXIT_CODE

    # argument routing
    parsed = parse_args(["--classic-shots", "a", "--next-shots", "b"])
    assert parsed.classic_shots == "a" and parsed.next_shots == "b"
    assert parse_args(["--selftest"]).selftest is True

    shutil.rmtree(scratch, ignore_errors=True)
    print("run_grade_probe_test: selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
