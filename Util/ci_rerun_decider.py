#!/usr/bin/env python3
"""Decide whether a failed CI run should be auto-rerun once.

The emulator/simulator CI jobs occasionally fail for reasons that have nothing
to do with the change under test - a hosted runner that never finishes booting
its Android emulator, a CoreSimulator that wedges, an empty-log hang. Those are
worth exactly ONE automatic rerun. A failure anywhere else (a real test break, a
compile error) must stay red and visible.

This module is the pure decision at the heart of the ci-rerun workflow: given
the list of jobs in a completed run and the run's attempt number, it answers
"rerun" only when EVERY failed job is in the known-brittle allowlist AND this is
the first attempt (so the rerun can never loop). Kept pure and stdlib-only so
the workflow can unit-check it with --selftest before trusting its verdict.

Usage in the workflow:
    gh api .../actions/runs/<id>/jobs --paginate \\
      | python3 Util/ci_rerun_decider.py --attempt "$RUN_ATTEMPT"
    # prints a final line "DECISION: rerun" or "DECISION: skip"; exit 0 always,
    # the reasoning goes to stderr.

    python3 Util/ci_rerun_decider.py --selftest   # pure decision-table check
"""

import json
import sys

# The jobs whose failures are worth one automatic rerun. These MUST match the
# `name:` fields of the corresponding jobs in .github/workflows/ci.yml exactly
# (the workflow_run event carries job names, not ids). Keep this list in sync
# when a device job is renamed. Everything NOT listed here is treated as a real
# failure that stays red.
BRITTLE_JOBS = frozenset({
    "Android emulator next - build + Play test",
    "Android emulator classic - build + Play test",
    "iOS simulator next - build + device tests",
    "iOS simulator classic - player build + export structure",
})

# GitHub job `conclusion` values that count as "this job failed" for the
# allowlist decision. A cancelled/skipped job is not a failure to heal (a
# cancel is usually a superseding push); success/neutral obviously are not.
FAILED_CONCLUSIONS = frozenset({"failure", "timed_out"})


def decide(jobs, attempt, brittle=BRITTLE_JOBS):
    """Return (should_rerun: bool, reason: str) for a completed run.

    `jobs` is the list of job objects from the run (each a dict with at least
    "name" and "conclusion"). `attempt` is the run's attempt count (1 for the
    original run). Pure - no I/O, no gh calls - so --selftest can pin the whole
    decision table.
    """
    try:
        attempt = int(attempt)
    except (TypeError, ValueError):
        return False, f"unparseable attempt {attempt!r}; refusing to rerun"

    if attempt != 1:
        return False, (
            f"run is on attempt {attempt}, not 1 - the once-only guard forbids "
            "a second automatic rerun"
        )

    failed = [j for j in jobs
              if (j.get("conclusion") or "").lower() in FAILED_CONCLUSIONS]
    if not failed:
        return False, "no failed jobs in the run - nothing to rerun"

    non_brittle = sorted({j.get("name", "?") for j in failed
                          if j.get("name") not in brittle})
    if non_brittle:
        return False, (
            "a non-brittle job failed, keeping the run red: "
            + ", ".join(non_brittle)
        )

    names = sorted({j.get("name", "?") for j in failed})
    return True, "every failed job is a known-brittle device job: " + ", ".join(names)


def _load_jobs(payload):
    """Accept either the raw `gh api .../jobs` object ({"jobs": [...]}) or a
    bare list of job objects."""
    data = json.loads(payload)
    if isinstance(data, dict):
        return data.get("jobs", [])
    if isinstance(data, list):
        return data
    raise ValueError("expected a jobs object or a list of jobs")


def selftest():
    A = "Android emulator next - build + Play test"
    B = "iOS simulator classic - player build + export structure"
    REAL = "Linux next - desktop suites"

    def job(name, conclusion):
        return {"name": name, "conclusion": conclusion}

    cases = [
        # all failed jobs are brittle, first attempt -> rerun
        ("all brittle failed",
         [job(A, "failure"), job(B, "timed_out"), job(REAL, "success")], 1, True),
        # a real job failed alongside a brittle one -> stay red
        ("mixed failure",
         [job(A, "failure"), job(REAL, "failure")], 1, False),
        # only a real job failed -> stay red
        ("real failure only",
         [job(REAL, "failure")], 1, False),
        # brittle failed but this is a rerun already -> no loop
        ("attempt 2",
         [job(A, "failure")], 2, False),
        # nothing failed (a stray completed event) -> nothing to do
        ("no failures",
         [job(A, "success"), job(REAL, "success")], 1, False),
        # a brittle job was cancelled (superseded), not failed -> no rerun
        ("brittle cancelled",
         [job(A, "cancelled")], 1, False),
        # timed_out brittle job (our step timeout fired) -> rerun
        ("brittle timed out",
         [job(A, "timed_out")], 1, True),
        # attempt passed as a string, as gh env gives it -> parsed
        ("string attempt one",
         [job(A, "failure")], "1", True),
    ]

    failures = 0
    for label, jobs, attempt, expected in cases:
        got, reason = decide(jobs, attempt)
        ok = got == expected
        print(f"  [{'ok' if ok else 'FAIL'}] {label}: rerun={got} ({reason})")
        if not ok:
            failures += 1

    # _load_jobs accepts both shapes
    assert _load_jobs('{"jobs": [{"name": "x", "conclusion": "failure"}]}')
    assert _load_jobs('[{"name": "x", "conclusion": "failure"}]')

    if failures:
        print(f"SELFTEST FAILED: {failures} case(s)")
        return 1
    print("SELFTEST OK")
    return 0


def main(argv):
    attempt = "1"
    do_selftest = False
    args = list(argv)
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--selftest":
            do_selftest = True
        elif a == "--attempt":
            i += 1
            attempt = args[i] if i < len(args) else "1"
        elif a.startswith("--attempt="):
            attempt = a.split("=", 1)[1]
        else:
            print(f"unknown argument {a!r}", file=sys.stderr)
            return 2
        i += 1

    if do_selftest:
        return selftest()

    payload = sys.stdin.read()
    try:
        jobs = _load_jobs(payload)
    except (ValueError, json.JSONDecodeError) as exc:
        print(f"could not parse jobs JSON: {exc}", file=sys.stderr)
        # a parse failure must NOT trigger a rerun
        print("DECISION: skip")
        return 0

    should_rerun, reason = decide(jobs, attempt)
    print(reason, file=sys.stderr)
    print("DECISION: rerun" if should_rerun else "DECISION: skip")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
