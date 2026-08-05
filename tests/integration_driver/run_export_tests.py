#!/usr/bin/env python3
"""ctest driver for running a project's own Lua suite INSIDE A PACKAGE.

    run_export_tests.py --repo <root> --exporter <orkige_export>
                        --project <dir>
                        --platform macos|linux|windows|ios-simulator
                        --engine-build <dir> --output <dir>
                        [--test-filter <substring>] [--deadline 600]

`orkige_player --run-tests` runs a suite against a loose project folder on a
development machine. That answers "does the game code work"; it cannot answer
"does the game that SHIPS work", because between the two lies the whole export -
the texture cook, the sampler bake, the media staging, the payload subdirectory
vocabulary. A file the payload silently drops is invisible to a loose run and
fatal to a player.

So this drives the other half: `--with-tests` packages a TEST BUILD (the
project's tests/ tree rides in the payload and the marker tells the artifact to
run it), the artifact is installed and launched exactly as a shipped one is, and
the suite runs against the packaged content on the packaged runtime.

THE VERDICT COMES FROM THE RUN ARTIFACT, never from log text: the runner's
flush-per-record JSONL (Docs/testing.md) carries a `summary` record with the
exit code, and its absence is the separate, named fact that the run DIED. A
report with zero tests in it fails here too - a harness that reports success
when nothing ran is worse than no harness.

Exit codes: 0 pass, 77 skip (no built player, no booted simulator - the ctest
SKIP_RETURN_CODE), anything else fail.
"""

import argparse
import json
import os
import plistlib
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# the packaging, device and console plumbing is the sibling driver's - one
# definition of "export this", "find a simulator", "follow the app's stdout"
from run_export_device_test import (  # noqa: E402
    ConsoleTail, export, fail, first_simulator, log, project_names, require,
    run, skip)


# --- the verdict ------------------------------------------------------------

def read_report(report_dir):
    """the newest `tests-*.jsonl` in @p report_dir, decoded line by line into
    (meta, records, summary). Any of the three may be None: a run that died
    leaves the file it had written so far, and that is exactly the fact the
    caller has to be able to tell apart from a failing suite."""
    if not os.path.isdir(report_dir):
        return None, [], None
    artifacts = sorted(name for name in os.listdir(report_dir)
                       if name.startswith("tests-") and name.endswith(".jsonl"))
    if not artifacts:
        return None, [], None
    path = os.path.join(report_dir, artifacts[-1])
    meta = None
    summary = None
    records = []
    with open(path, errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
            except ValueError:
                continue  # a torn last line: the process died mid-write
            kind = entry.get("record")
            if kind == "meta":
                meta = entry
            elif kind == "summary":
                summary = entry
            elif kind == "test":
                records.append(entry)
    return meta, records, summary


def report_settled(report_dir):
    """has the run reached a verdict? (a `summary` record has landed)"""
    return read_report(report_dir)[2] is not None


# The runner opens its report and writes the `meta` line BEFORE it runs a
# single test, so the file appearing is the fact "the packaged artifact entered
# the test runner AT ALL". That is a different failure from a slow or a wedged
# suite, and it must not be paid for at the deadline: an artifact whose player
# does not act on the marker's `run-tests` directive simply plays the GAME, and
# a game runs forever. Naming it here turns ten silent minutes into one line.
#
# The window has to sit ABOVE any legitimate boot, because it accuses the
# artifact rather than timing it: the sibling device driver budgets a whole
# simulator app boot at 300s on a hosted runner, and a desktop bundle boots in
# seconds. So it is generous, and its value is the NAME it fails with.
RUNNER_START_GRACE = 240


def report_started(report_dir):
    """has the packaged artifact entered the runner? (a `meta` record landed)"""
    return read_report(report_dir)[0] is not None


def never_started(seconds):
    fail("the packaged app never entered the test runner: no test report "
         "appeared within %ds, so not one record was written. The artifact is "
         "running the GAME instead of its suite - the packaged player does not "
         "act on the project marker's run-tests directive (an engine tree "
         "older than the packaged test build would do exactly this)" % seconds)


def judge(report_dir, exit_code):
    """turn the artifact into this driver's own exit status.

    Every refusal below is a distinct fact, because they need distinct fixes: a
    missing report is a runner that never started, a report with no summary is
    a run that DIED, a summary over zero tests is a package that carried no
    suite, and a nonzero summary is a game whose tests failed."""
    meta, records, summary = read_report(report_dir)
    if meta is None and summary is None:
        fail("the packaged run left no test report in '%s' - the runner never "
             "started (exit code %s)" % (report_dir, exit_code))
    if summary is None:
        last = records[-1] if records else None
        fail("the packaged run DIED without finishing: the report has no "
             "summary record%s (exit code %s)"
             % ("" if last is None else ", last test reached was '%s :: %s'"
                % (last.get("file", "?"), last.get("name", "?")), exit_code))
    total = int(summary.get("total", 0))
    log("report: %d file(s), %d test(s), %d passed, %d failed, %d error(s)"
        % (int(summary.get("files", 0)), total, int(summary.get("passed", 0)),
           int(summary.get("failed", 0)), int(summary.get("errors", 0))))
    # THE guard this harness exists to hold: a green verdict must come from
    # tests that actually ran. A package whose payload lost the suite would
    # otherwise install, launch, report nothing and be called a pass.
    require(total > 0,
            "the packaged run executed at least one test (it reported %d - "
            "the package carried no suite)" % total)
    for record in records:
        if record.get("status") != "pass":
            log("  %s %s :: %s\n    %s"
                % (record.get("status", "?").upper(), record.get("file", "?"),
                   record.get("name", "?"), record.get("message", "")))
    require(int(summary.get("exitCode", 1)) == 0,
            "the packaged suite passed (%d failed, %d error(s))"
            % (int(summary.get("failed", 0)), int(summary.get("errors", 0))))
    if exit_code is not None:
        # the process's own code is the runner's verdict too; a disagreement
        # would mean one of the two roads is lying about the same run
        require(exit_code == 0,
                "the packaged runtime exited 0 to match its report (got %s)"
                % exit_code)


# --- macOS: the packaged run that needs no device ---------------------------

def run_macos(args, report_dir):
    app = os.path.join(args.output, "package",
                       project_names(args.project)[0] + ".app")
    export(args, os.path.join(args.output, "package"))
    require(os.path.isdir(app), "the exporter produced an app bundle")
    executable = os.path.join(app, "Contents", "MacOS",
                              project_names(args.project)[1])
    require(os.path.isfile(executable), "the bundle carries its executable")
    # the suite ships INSIDE the payload, which is the whole point
    require(os.path.isdir(os.path.join(app, "Contents", "Resources",
                                       "project", "tests")),
            "the test build carries the project's tests/ directory")
    return run_desktop_package(args, report_dir, executable)


def run_linux(args, report_dir):
    """the portable directory's test build - the same package a player would
    receive, running its own suite instead of the game"""
    if not sys.platform.startswith("linux"):
        skip("a Linux package is produced on Linux; this host is "
             + sys.platform)
    exe_name = project_names(args.project)[1]
    package = os.path.join(args.output, "package", exe_name)
    export(args, os.path.join(args.output, "package"))
    require(os.path.isdir(package), "the exporter produced a package directory")
    executable = os.path.join(package, exe_name)
    require(os.path.isfile(executable) and os.access(executable, os.X_OK),
            "the package carries its executable")
    require(os.path.isdir(os.path.join(package, "project", "tests")),
            "the test build carries the project's tests/ directory")
    return run_desktop_package(args, report_dir, executable)


def run_windows(args, report_dir):
    """the portable directory's test build - the same package a player would
    receive, running its own suite instead of the game"""
    if not sys.platform.startswith("win"):
        skip("a Windows package is produced on Windows; this host is "
             + sys.platform)
    exe_name = project_names(args.project)[1]
    package = os.path.join(args.output, "package", exe_name)
    export(args, os.path.join(args.output, "package"))
    require(os.path.isdir(package), "the exporter produced a package directory")
    # executability is the extension on Windows, not a permission bit
    executable = os.path.join(package, exe_name + ".exe")
    require(os.path.isfile(executable), "the package carries its executable")
    require(os.path.isdir(os.path.join(package, "project", "tests")),
            "the test build carries the project's tests/ directory")
    return run_desktop_package(args, report_dir, executable)


def run_desktop_package(args, report_dir, executable):
    """run a packaged desktop artifact's own suite and return its exit code.
    ONE implementation for both desktop shapes: what differs between them is
    where the executable sits, which the caller already resolved."""
    environment = dict(os.environ)
    environment["ORKIGE_TEST_REPORT_DIR"] = report_dir
    environment["ALSOFT_DRIVERS"] = "null"
    # the two hooks that would cut a suite short: a player that exits after N
    # frames would end the run early and call it a pass
    environment.pop("ORKIGE_DEMO_FRAMES", None)
    environment.pop("ORKIGE_DEMO_SCREENSHOT", None)
    log("$ %s   (cwd = %s)" % (executable, args.output))
    # a NEUTRAL cwd: the source tree's files would mask a payload that lost one
    process = subprocess.Popen([executable], cwd=args.output, env=environment)
    started = False
    window = min(RUNNER_START_GRACE, args.deadline)
    grace = time.monotonic() + window
    end = time.monotonic() + args.deadline
    try:
        while True:
            code = process.poll()
            if code is not None:
                return code
            if not started:
                started = report_started(report_dir)
            now = time.monotonic()
            if not started and now > grace:
                never_started(window)
            if now > end:
                fail("the packaged suite never reached a verdict within %ds"
                     % args.deadline)
            time.sleep(0.5)
    finally:
        # the app is a game loop when this goes wrong, so it is never left
        # running behind a failed driver
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()


# --- iOS Simulator: the packaged run on the device it ships to --------------

def run_ios_simulator(args, report_dir):
    if sys.platform != "darwin" or not shutil.which("xcrun"):
        skip("the iOS Simulator flow needs macOS with the Xcode command line "
             "tools")
    player_app = os.path.join(args.engine_build, "tools", "player",
                              "OrkigePlayer.app")
    if not os.path.isdir(player_app):
        skip("no built iOS Simulator player at '%s' - build the matching "
             "ios-simulator preset to enable this test" % player_app)
    udid = first_simulator()
    if not udid:
        skip("no BOOTED iPhone simulator (boot one with 'xcrun simctl boot')")
    log("using simulator " + udid)

    name = project_names(args.project)[0]
    export(args, os.path.join(args.output, "package"))
    app = os.path.join(args.output, "package", name + ".app")
    require(os.path.isdir(app), "the exporter produced an app bundle")
    require(os.path.isdir(os.path.join(app, "project", "tests")),
            "the test build carries the project's tests/ directory")
    with open(os.path.join(app, "Info.plist"), "rb") as handle:
        bundle_id = plistlib.load(handle)["CFBundleIdentifier"]

    run(["xcrun", "simctl", "uninstall", udid, bundle_id], timeout=120,
        check=False)
    run(["xcrun", "simctl", "install", udid, app], timeout=300)
    log("installed " + bundle_id)
    tail = None
    try:
        # the report has to land somewhere the HOST can read. The app's own
        # data container is a real host directory, so naming it through the
        # runner's existing ORKIGE_TEST_REPORT_DIR seam needs no new channel:
        # the app writes where it always writes, and the driver knows where.
        code, container = run(["xcrun", "simctl", "get_app_container", udid,
                               bundle_id, "data"], timeout=120)
        device_report = os.path.join(container.strip(), "orkige-test-report")
        environment = dict(os.environ)
        environment["SIMCTL_CHILD_ORKIGE_TEST_REPORT_DIR"] = device_report
        environment["SIMCTL_CHILD_ALSOFT_DRIVERS"] = "null"
        # never a frame cap on a test run: it would end the suite mid-way and
        # leave a report that looks like a clean, short pass
        environment.pop("SIMCTL_CHILD_ORKIGE_DEMO_FRAMES", None)
        environment.pop("SIMCTL_CHILD_ORKIGE_DEMO_FPS_LOG", None)
        tail = ConsoleTail(["xcrun", "simctl", "launch", "--console-pty",
                            udid, bundle_id], environment)
        started = False
        window = min(RUNNER_START_GRACE, args.deadline)
        grace = time.monotonic() + window
        end = time.monotonic() + args.deadline
        while time.monotonic() < end:
            if report_settled(device_report):
                break
            # the same distinction the macOS run draws: an app that never
            # entered the runner is not a slow suite, and says so at the grace
            # rather than at the deadline
            started = started or report_started(device_report)
            if not started and time.monotonic() > grace:
                print(tail.text()[-8000:], flush=True)
                never_started(window)
            time.sleep(1.0)
        else:
            print(tail.text()[-8000:], flush=True)
            fail("the packaged suite never reached a verdict within %ds"
                 % args.deadline)
        # bring the artifact home before the container is uninstalled
        shutil.rmtree(report_dir, ignore_errors=True)
        shutil.copytree(device_report, report_dir)
        print(tail.text()[-8000:], flush=True)
    finally:
        if tail is not None:
            tail.stop()
        run(["xcrun", "simctl", "terminate", udid, bundle_id], timeout=120,
            check=False)
        run(["xcrun", "simctl", "uninstall", udid, bundle_id], timeout=120,
            check=False)
    # an iOS app's process outlives its main function inside UIKit's run loop,
    # so there is no exit code to read - the artifact is the whole verdict
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--platform", required=True,
                        choices=["macos", "linux", "windows", "ios-simulator"])
    parser.add_argument("--engine-build", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--test-filter", default="")
    parser.add_argument("--deadline", type=int, default=600)
    args = parser.parse_args()
    # the sibling's export() reads these two off the same namespace
    args.engine_source = "build-tree"
    args.with_tests = True

    report_dir = os.path.join(args.output, "report")
    shutil.rmtree(report_dir, ignore_errors=True)
    os.makedirs(report_dir, exist_ok=True)
    if args.platform == "macos":
        exit_code = run_macos(args, report_dir)
    elif args.platform == "linux":
        exit_code = run_linux(args, report_dir)
    elif args.platform == "windows":
        exit_code = run_windows(args, report_dir)
    else:
        exit_code = run_ios_simulator(args, report_dir)
    judge(report_dir, exit_code)
    log("PASS")


if __name__ == "__main__":
    sys.exit(main() or 0)
