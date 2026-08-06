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

An absent report is where a harness is most tempted to guess, so it does not:
before it accuses the package of running the game instead of its suite, it
establishes whether the app RAN AT ALL. The two answers need opposite fixes
and must never wear each other's name.

Exit codes: 0 pass, 77 skip (no built player, no booted simulator - the ctest
SKIP_RETURN_CODE), anything else fail.
"""

import argparse
import contextlib
import io
import json
import os
import plistlib
import re
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

#: how long a device command may take before it stops being a command and
#: becomes a symptom. simctl talks to a daemon on the simulator host, so a
#: device that has wedged makes every one of them sit until its own timeout.
DEVICE_STEP_BUDGET = 30.0

#: the budget for the ONE question asked at the moment of diagnosis - does a
#: process for the app exist on that device. Short on purpose: an answer that
#: takes longer than this IS the answer.
DEVICE_PROBE_TIMEOUT = 30

#: the artifact ran, so an absent report is the PACKAGE's fault
RAN = "ran"
#: the artifact never ran, so an absent report says nothing about the package
NEVER_RAN = "never-ran"


def report_started(report_dir):
    """has the packaged artifact entered the runner? (a `meta` record landed)"""
    return read_report(report_dir)[0] is not None


class RunEvidence(object):
    """what this driver knows about an artifact at the moment its start window
    expires with no report in it.

    Every field is something the run already collected - the launch's own
    answer, the artifact's own output, how long the device's commands took -
    except @ref device_state, which is one short question asked at the end.

    @param launched      did the platform acknowledge a STARTED process? True
                         or False where it can say, None where it cannot
    @param output        everything the artifact itself printed
    @param device_state  "running", "absent", "unresponsive", or "" when no
                         device was asked (a desktop package has none)
    @param slow_steps    (command, seconds) for each device command that
                         overran @ref DEVICE_STEP_BUDGET
    """

    def __init__(self, launched=None, output="", device_state="",
                 slow_steps=None):
        self.launched = launched
        self.output = output or ""
        self.device_state = device_state or ""
        self.slow_steps = list(slow_steps or ())


def absence_verdict(evidence):
    """(outcome, the sentence naming the evidence) for a run that wrote no
    report - PURE, so the wedged-device case is decidable without a device.

    POSITIVE proof that a process existed outranks every other fact: a loaded
    machine makes commands slow, and only the process itself settles whether
    the app ran. With no such proof, each remaining fact points the other way,
    and the harness says so instead of accusing the package."""
    if evidence.launched is True:
        why = "the launch was acknowledged with a process id"
        if evidence.device_state == "absent":
            why += ", though the device no longer lists that process"
        return RAN, why
    if evidence.device_state == "running":
        return RAN, "the device lists a process for the app"
    if evidence.device_state == "unresponsive":
        return NEVER_RAN, ("the device did not answer a process listing "
                           "within %ds" % DEVICE_PROBE_TIMEOUT)
    if evidence.device_state == "absent":
        return NEVER_RAN, "the device lists no process for the app"
    if evidence.slow_steps:
        return NEVER_RAN, ("the device stopped answering: "
                           + ", ".join("'%s' took %ds" % (name, round(seconds))
                                       for name, seconds
                                       in evidence.slow_steps))
    if evidence.launched is False:
        return NEVER_RAN, "the launch command named no process id"
    if evidence.output.strip():
        return RAN, ("the artifact printed %d line(s) of its own output"
                     % len(evidence.output.strip().splitlines()))
    return NEVER_RAN, ("nothing acknowledged the app starting and it printed "
                       "not one line")


def never_started(why, seconds):
    """(a) the artifact RAN and never reached the runner - the package is what
    is wrong, and this is the one sentence that may say so."""
    fail("the packaged app RAN but never entered the test runner: %s, and no "
         "test report appeared within %ds - not one record was written. The "
         "artifact is running the GAME instead of its suite - the packaged "
         "player does not act on the project marker's run-tests directive (an "
         "engine tree older than the packaged test build would do exactly "
         "this)" % (why, seconds))


def never_ran(why, seconds=None):
    """(b) the app never started, or the device stopped answering. The report
    is missing because nothing wrote one, which is a fact about the MACHINE -
    so this refuses to name the package, the payload or the engine."""
    fail("simulator infrastructure failure - the device never ran the app: "
         "%s%s" % (why, "" if seconds is None else
                   (". No test report appeared within %ds, and that absence "
                    "is the DEVICE's rather than the package's: nothing here "
                    "judges the artifact, its payload or the engine"
                    % seconds)))


def no_report_failure(evidence, seconds):
    """route an absent report to the ONE outcome its evidence supports. The
    third shape - a report that appeared and stayed incomplete - is not here:
    it is @ref judge's no-summary refusal, because by then a report exists."""
    outcome, why = absence_verdict(evidence)
    if outcome == RAN:
        never_started(why, seconds)
    else:
        never_ran(why, seconds)


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
                # a desktop spawn needs no diagnosis: this loop polls the
                # process itself and returns the moment it exits, so reaching
                # here means it is STILL ALIVE - the app ran, and the missing
                # report is the package's to answer for
                no_report_failure(RunEvidence(launched=True), window)
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

def device_step(argv, timeout, slow_steps):
    """a device command, TIMED. One that overruns its budget - or never
    returns - is remembered, because a wedged device is diagnosed from the
    commands that were already going to be issued.

    Returns (exit code, output), or (None, "") when it never returned."""
    started = time.monotonic()
    try:
        code, output = run(argv, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        slow_steps.append((" ".join(argv[1:3]), float(timeout)))
        return None, ""
    seconds = time.monotonic() - started
    if seconds > DEVICE_STEP_BUDGET:
        slow_steps.append((" ".join(argv[1:3]), seconds))
    return code, output


def simulator_process_state(udid, bundle_id):
    """ask the DEVICE whether a process for the app exists: "running",
    "absent", or "unresponsive" when it cannot answer in time.

    This is the one fact nothing else in the run holds, and it is asked of the
    device itself rather than inferred - the whole point is not to conclude
    from silence."""
    try:
        result = subprocess.run(["xcrun", "simctl", "spawn", udid,
                                 "launchctl", "list"],
                                capture_output=True, text=True,
                                errors="replace", timeout=DEVICE_PROBE_TIMEOUT)
    except (subprocess.TimeoutExpired, OSError):
        return "unresponsive"
    if result.returncode != 0:
        return "unresponsive"
    # a running app is a launchd job whose label carries its bundle id
    return "running" if bundle_id in (result.stdout or "") else "absent"


def launch_acknowledged(console_text, bundle_id):
    """did `simctl launch` name a process id for the app? That line is the
    DEVICE saying it started the app, and no amount of app silence undoes it."""
    return re.search(re.escape(bundle_id) + r":\s*\d+",
                     console_text) is not None


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

    # every device command from here is TIMED: when this test fails with no
    # report, how long the machine took to answer is part of the evidence
    slow_steps = []
    device_step(["xcrun", "simctl", "uninstall", udid, bundle_id], 120,
                slow_steps)
    code, output = device_step(["xcrun", "simctl", "install", udid, app], 300,
                               slow_steps)
    if code is None:
        never_ran("installing the app did not return within 300s")
    if code != 0:
        fail("$ xcrun simctl install " + udid + "\n" + output)
    log("installed " + bundle_id)
    tail = None
    try:
        # the report has to land somewhere the HOST can read. The app's own
        # data container is a real host directory, so naming it through the
        # runner's existing ORKIGE_TEST_REPORT_DIR seam needs no new channel:
        # the app writes where it always writes, and the driver knows where.
        code, container = device_step(["xcrun", "simctl", "get_app_container",
                                       udid, bundle_id, "data"], 120,
                                      slow_steps)
        if code is None:
            never_ran("asking for the app's data container did not return "
                      "within 120s")
        if code != 0:
            fail("$ xcrun simctl get_app_container " + udid + "\n" + container)
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
                console = tail.text()
                # ...and here the two shapes part. A wedged device produces
                # exactly this silence while never having run the app at all,
                # so the accusation is made only against evidence that it did.
                no_report_failure(RunEvidence(
                    launched=launch_acknowledged(console, bundle_id),
                    output=console,
                    device_state=simulator_process_state(udid, bundle_id),
                    slow_steps=slow_steps), window)
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
        # teardown goes through the timed step too, so a device that has
        # stopped answering cannot bury the verdict above under a traceback
        # from its own cleanup - which is exactly how a wedged device looks
        device_step(["xcrun", "simctl", "terminate", udid, bundle_id], 120,
                    slow_steps)
        device_step(["xcrun", "simctl", "uninstall", udid, bundle_id], 120,
                    slow_steps)
    # an iOS app's process outlives its main function inside UIKit's run loop,
    # so there is no exit code to read - the artifact is the whole verdict
    return None


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    if "--selftest" in argv:
        return selftest()
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the pure parts and exit")
    parser.add_argument("--repo", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--platform", required=True,
                        choices=["macos", "linux", "windows", "ios-simulator"])
    parser.add_argument("--engine-build", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--test-filter", default="")
    parser.add_argument("--deadline", type=int, default=600)
    args = parser.parse_args(argv)
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


# --- selftest ---------------------------------------------------------------

def said(reporter, *arguments):
    """run one of the refusals above, returning what it printed. They end the
    process, which is the contract - so the selftest reads them the way ctest
    would rather than restating their sentences."""
    buffer = io.StringIO()
    code = 0
    try:
        with contextlib.redirect_stdout(buffer):
            reporter(*arguments)
    except SystemExit as stop:
        code = stop.code
    return code, buffer.getvalue()


def selftest():
    """the pure decisions: which shape an absent report is, and which sentence
    each shape earns. No device, no package, no export."""
    import tempfile

    # (a) THE APP RAN. Every road to that conclusion is a POSITIVE fact about
    # a process, never the absence of one.
    for evidence in (RunEvidence(launched=True),
                     RunEvidence(launched=True, device_state="running"),
                     RunEvidence(device_state="running"),
                     # positive proof outranks a slow machine: commands drag
                     # under load, and the process is what settles it
                     RunEvidence(launched=True,
                                 slow_steps=[("simctl install", 300.0)]),
                     RunEvidence(output="orkige_player: booted\n")):
        assert absence_verdict(evidence)[0] == RAN, vars(evidence)

    # an app the device started and no longer lists still RAN - the report is
    # the package's to answer for, and the sentence carries the nuance
    outcome, why = absence_verdict(RunEvidence(launched=True,
                                               device_state="absent"))
    assert outcome == RAN and "no longer lists that process" in why, why

    # (b) THE APP NEVER RAN. The wedged CI simulator is the first one: every
    # command timed out and the device answers nothing.
    wedged = RunEvidence(launched=False, device_state="unresponsive",
                         slow_steps=[("simctl terminate", 120.0),
                                     ("simctl uninstall", 120.0)])
    assert absence_verdict(wedged) == (
        NEVER_RAN, "the device did not answer a process listing within 30s")
    for evidence in (RunEvidence(launched=False),
                     RunEvidence(device_state="absent"),
                     RunEvidence(device_state="unresponsive"),
                     RunEvidence(slow_steps=[("simctl launch", 120.0)]),
                     RunEvidence()):
        assert absence_verdict(evidence)[0] == NEVER_RAN, vars(evidence)

    # a device whose commands merely dragged is named with its numbers
    assert "'simctl launch' took 120s" in absence_verdict(
        RunEvidence(slow_steps=[("simctl launch", 120.0)]))[1]

    # ...and the two verdicts wear DIFFERENT sentences, which is the whole
    # point: one accuses the package, the other refuses to
    code, text = said(no_report_failure, RunEvidence(launched=True), 240)
    assert code == 1 and "the packaged app RAN but never entered the test " \
        "runner" in text, text
    assert "run-tests directive" in text, text
    code, text = said(no_report_failure, wedged, 240)
    assert code == 1, text
    assert "simulator infrastructure failure - the device never ran the app" \
        in text, text
    # the false-red shape must not name the things it cannot judge
    assert "run-tests directive" not in text, text
    assert "the DEVICE's rather than the package's" in text, text

    # a device that never got as far as the start window says so with no
    # report window in the sentence at all
    code, text = said(never_ran, "installing the app did not return within "
                                 "300s")
    assert code == 1 and "No test report appeared" not in text, text

    # the launch acknowledgement: simctl naming a pid for THIS bundle
    assert launch_acknowledged("com.orkitec.jumperlua: 41321", "com.orkitec."
                               "jumperlua")
    assert not launch_acknowledged("com.orkitec.jumperlua: 41321",
                                   "com.orkitec.other")
    assert not launch_acknowledged(
        "An error was encountered processing the command", "com.orkitec.x")

    # the report reader, which decides all three shapes upstream of the above
    scratch = tempfile.mkdtemp(prefix="orkige_exporttests_selftest_")
    try:
        assert read_report(scratch) == (None, [], None)
        assert not report_started(scratch) and not report_settled(scratch)
        path = os.path.join(scratch, "tests-1.jsonl")
        with open(path, "w") as handle:
            handle.write(json.dumps({"record": "meta"}) + "\n")
        # a run that entered the runner but has not finished: STARTED, not
        # SETTLED - the third shape, and judge() is what names it
        assert report_started(scratch) and not report_settled(scratch)
        with open(path, "a") as handle:
            handle.write(json.dumps({"record": "test", "status": "pass"})
                         + "\n")
            handle.write('{"record": "summ')  # a process that died mid-write
        meta, records, summary = read_report(scratch)
        assert meta is not None and len(records) == 1 and summary is None
        with open(path, "a") as handle:
            handle.write("\n" + json.dumps({"record": "summary", "total": 1})
                         + "\n")
        assert report_settled(scratch)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    print("run_export_tests: selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
