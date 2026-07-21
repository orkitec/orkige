#!/usr/bin/env python3
"""Prove the fatal-signal crash marker end to end: crash, then detect on reboot.

The engine stamps a final "crash" breadcrumb when a fatal signal kills a run, so
a crashed session is machine-detectable even on a phone (no crash dialog). This
driver exercises the whole path with the real player:

  1. Run once with ORKIGE_CRASH_SELFCHECK=<frame>, which arms the crash marker
     and raises SIGSEGV at that frame. The run MUST die by a signal (a negative
     return code) - the marker writes its crumb, then re-raises so the OS still
     produces its report. NOTE: this deliberate crash drops a macOS
     DiagnosticReports (or a Linux core) entry per run - that is EXPECTED and
     acceptable for this test.
  2. Run again normally. Assert (a) the player logs the "previous run crashed"
     warning to stderr, and (b) the rotated breadcrumbs.prev.jsonl ends in a
     "crash" crumb naming the signal.

Under an AddressSanitizer build the marker stands down (ASan owns the fatal
handlers), so the first run prints "crash marker unavailable" and never
self-crashes; the driver then SKIPs (77). Stdlib only, per the toolchain policy.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import time


# --- deliberate-crash report hygiene ---------------------------------------
# This test crashes the player ON PURPOSE, which makes macOS ReportCrash write
# a DiagnosticReports .ips (and pop the "quit unexpectedly" dialog) every run -
# clutter that used to accumulate (dozens of stale reports) and made a REAL
# crash indistinguishable from this expected one. Since WE caused this crash,
# WE clean it up: snapshot the report dir before the crash, then delete only the
# NEW report(s) our own binary produced, and dismiss the dialog. Only ever
# touches reports named after the crashing binary that appeared during our run.
def _diag_report_dir():
    return os.path.expanduser("~/Library/Logs/DiagnosticReports")


def _diag_reports(binary):
    if sys.platform != "darwin":
        return set()
    stem = os.path.basename(binary)
    return set(glob.glob(os.path.join(_diag_report_dir(), stem + "*.ips")))


def _clean_own_crash_report(binary, baseline, wait_seconds=8):
    """Delete the DiagnosticReports entry our deliberate crash just produced.
    ReportCrash writes it asynchronously after the process dies, so poll."""
    if sys.platform != "darwin":
        return
    # dismiss the crash dialog if one popped (background test runs usually get
    # none, but be safe) - best effort, never fail the test on it
    for proc in ("Problem Reporter", "UserNotificationCenter"):
        subprocess.run(["pkill", "-x", proc],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + wait_seconds
    removed = 0
    while time.monotonic() < deadline:
        fresh = _diag_reports(binary) - baseline
        for path in fresh:
            try:
                os.unlink(path)
                removed += 1
            except OSError:
                pass
        if removed:
            break
        time.sleep(0.5)
    print("crash-marker: cleaned %d deliberate-crash DiagnosticReports entr%s"
          % (removed, "y" if removed == 1 else "ies"), flush=True)


def run_player(binary, project, env_overrides, timeout):
    env = dict(os.environ)
    env.update(env_overrides)
    return subprocess.run(
        [binary, "--project", project],
        env=env, timeout=timeout,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def last_nonempty_line(text):
    for line in reversed(text.splitlines()):
        if line.strip():
            return line
    return ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="orkige_player")
    parser.add_argument("--project", required=True,
                        help="a project the player boots and runs")
    parser.add_argument("--crash-dir", required=True,
                        help="an isolated breadcrumb dir (ORKIGE_BREADCRUMB_DIR)")
    parser.add_argument("--crash-frame", type=int, default=3,
                        help="the frame at which run 1 raises SIGSEGV")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"SKIP: player not built: {args.binary}")
        return 77

    # a clean, isolated breadcrumb dir so prev/live never carry stale state
    shutil.rmtree(args.crash_dir, ignore_errors=True)
    os.makedirs(args.crash_dir, exist_ok=True)

    # --- run 1: arm the marker and crash at the known frame -------------------
    # a frame cap well past the crash frame lets an UNARMED (sanitizer) run still
    # exit instead of spinning forever
    frame_cap = str(args.crash_frame + 40)
    # snapshot the crash-report dir so we delete ONLY the report the deliberate
    # crash below produces, never a pre-existing (possibly real) one
    crash_report_baseline = _diag_reports(args.binary)
    try:
        first = run_player(args.binary, args.project, {
            "ORKIGE_CRASH_SELFCHECK": str(args.crash_frame),
            "ORKIGE_DEMO_FRAMES": frame_cap,
            "ORKIGE_BREADCRUMB_DIR": args.crash_dir,
        }, args.timeout)
    except subprocess.TimeoutExpired:
        print("FAIL: run 1 hung - the deliberate crash never fired")
        return 1

    marker = first.stdout + first.stderr
    if "crash marker unavailable" in marker:
        print("SKIP: crash marker unavailable (sanitizer build - ASan owns the "
              "fatal handlers)")
        return 77
    if "crash marker armed" not in marker:
        print("FAIL: run 1 did not print the crash-marker arm/skip line")
        print(marker)
        return 1

    # POSIX reports death-by-signal as a negative returncode; Windows has no
    # signal exit - the CRT's default SIGSEGV path terminates with a small
    # positive code (3 observed) - so the platform-portable expectation is
    # simply "did not exit cleanly". The assertions that matter (the crash
    # crumb in the rotated trail + the next boot's warning) are below and
    # identical on every platform.
    if sys.platform == "win32":
        if first.returncode == 0:
            print("FAIL: run 1 exited 0 - the deliberate crash did not "
                  "terminate the run")
            return 1
        print(f"ok: run 1 terminated abnormally with code {first.returncode} "
              "(the deliberate crash, Windows CRT semantics)")
    else:
        if first.returncode >= 0:
            print(f"FAIL: run 1 exited {first.returncode} - it was expected "
                  "to die by a fatal signal (negative return code)")
            return 1
        print(f"ok: run 1 died by signal {-first.returncode} "
              "(the deliberate crash)")

    # our deliberate crash just wrote a macOS DiagnosticReports entry (and maybe
    # popped a dialog) - clean up our OWN artifact so it neither clutters the
    # report dir nor masquerades as a real crash to a watcher
    _clean_own_crash_report(args.binary, crash_report_baseline)

    # --- run 2: a clean reboot must detect the previous crash ------------------
    try:
        second = run_player(args.binary, args.project, {
            "ORKIGE_DEMO_FRAMES": "10",
            "ORKIGE_BREADCRUMB_DIR": args.crash_dir,
        }, args.timeout)
    except subprocess.TimeoutExpired:
        print("FAIL: run 2 (clean reboot) hung")
        return 1
    if second.returncode != 0:
        print(f"FAIL: run 2 exited {second.returncode} - a clean reboot should "
              "succeed")
        print(second.stderr)
        return 1

    failures = []

    # (a) the boot warning naming the previous crash
    if "the previous run crashed" not in second.stderr:
        failures.append("run 2 did not log the 'previous run crashed' warning")
    else:
        print("ok: run 2 warned about the previous crash on boot")

    # (b) the rotated file ends in a crash crumb
    prev_path = os.path.join(args.crash_dir, "breadcrumbs.prev.jsonl")
    if not os.path.exists(prev_path):
        failures.append(f"no rotated trail at {prev_path}")
    else:
        with open(prev_path, "r", encoding="utf-8", errors="replace") as handle:
            prev_text = handle.read()
        tail = last_nonempty_line(prev_text)
        if '"kind":"crash"' not in tail:
            failures.append(
                f"the rotated trail's last entry is not a crash crumb: {tail!r}")
        elif '"msg":"SIG' not in tail:
            failures.append(
                f"the crash crumb does not name a signal: {tail!r}")
        else:
            print(f"ok: the rotated trail ends in the crash crumb: {tail}")

    if failures:
        for message in failures:
            print("FAIL: " + message)
        return 1
    print("crash marker: crashed run stamped its crumb AND the reboot detected "
          "it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
