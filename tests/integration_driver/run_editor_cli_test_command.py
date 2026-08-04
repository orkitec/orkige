#!/usr/bin/env python3
"""`orkige_editor test`, driven as a build server would drive it.

The pure argument router is unit-tested (tests/editor_core/EditorCliTests.cpp).
What only the REAL binary can prove is the part this door exists for:

  * it finds the player THIS installation has and runs the project's suite in
    it - so the whole road from argv to a Lua verdict actually connects,
  * a test that declares a scene runs, which is the reason the run is delegated
    to the runtime at all rather than attempted in the editor process,
  * the suite's verdict IS the exit code, in both directions: a passing suite
    comes back 0, a failing one comes back non-zero. A door that could only
    ever report success would be worse than no door.
  * `--test-filter` reaches the runner, and `--report-dir` puts the JSONL where
    the caller asked, with the `OK` line naming that exact file.
  * the refusals name what is missing, and none of them opens a window.

Exit code is this driver's own contract: 0 = every case passed.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

# a refusal decides before anything boots; anything slower means it went down
# the window road
QUICK_SECONDS = 60.0
# a real run boots the engine, loads scenes and advances frames
RUN_SECONDS = 600.0


def log(message):
    print("[clitest] %s" % message, flush=True)


def fail(message):
    print("[clitest] FAILED: %s" % message, flush=True)
    sys.exit(1)


def expect(condition, what):
    if not condition:
        fail(what)
    log("  ok: %s" % what)


def run(editor, argv, seconds):
    """run the editor with `argv` and return (exit code, merged output)"""
    log("$ orkige_editor " + " ".join(argv))
    started = time.time()
    process = subprocess.Popen([editor] + argv, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               errors="replace")
    try:
        output = process.communicate(timeout=seconds)[0]
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()
        fail("'%s' did not finish in %.0fs" % (" ".join(argv), seconds))
    log("  exit %d in %.1fs" % (process.returncode, time.time() - started))
    return (process.returncode, output)


def ok_line(output):
    """the machine-readable last line, or "" when the run printed none"""
    for line in output.splitlines():
        if line.startswith("orkige_editor: OK "):
            return line[len("orkige_editor: OK "):].strip()
    return ""


def make_fixture(template, target, tests):
    """a throwaway project: a known-good manifest plus the given test files

    Copying an existing fixture project rather than writing a manifest here
    keeps this driver out of the business of knowing what a valid one looks
    like - that is the manifest reader's job, and it has its own tests.
    """
    shutil.rmtree(target, ignore_errors=True)
    # the template's OWN suite is left behind: this fixture asserts exact
    # pass/fail counts, so inheriting whatever tests the template happens to
    # ship makes those counts drift the moment it gains one - which is how
    # this broke when the store tier gave projects/example a suite.
    shutil.copytree(template, target,
                    ignore=shutil.ignore_patterns("tests", "builds", ".orkige"))
    tests_dir = os.path.join(target, "tests")
    os.makedirs(tests_dir, exist_ok=True)
    for name, body in tests.items():
        with open(os.path.join(tests_dir, name), "w") as handle:
            handle.write(body)
    return target


def read_records(path):
    """the JSONL artifact as a list of records"""
    records = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True)
    # the project whose REAL suite is run, including its play-mode test
    parser.add_argument("--project", required=True)
    # the project the fixtures are cloned from, and the no-suite refusal case
    parser.add_argument("--template-project", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    editor = args.editor
    if not os.path.isfile(editor):
        fail("no editor executable at " + editor)
    shutil.rmtree(args.output, ignore_errors=True)
    os.makedirs(args.output, exist_ok=True)

    # --- the usage refusals: decided before anything boots ---------------
    code, output = run(editor, ["test"], QUICK_SECONDS)
    expect(code == 2, "'test' with no --project is a usage error (exit 2)")
    expect("--project" in output, "the refusal names the missing option")

    code, output = run(editor, ["test", "--project", args.project,
                                "--nonsense", "1"], QUICK_SECONDS)
    expect(code == 2, "an unknown test flag is a usage error (exit 2)")

    # --- the refusals that need a look at the project --------------------
    missing = os.path.join(args.output, "no-such-project")
    code, output = run(editor, ["test", "--project", missing], QUICK_SECONDS)
    expect(code == 1, "a project that is not there fails (exit 1)")
    expect("project.orkproj" in output,
           "the refusal names the manifest it looked for")

    # A project with NO suite, built here rather than borrowed from the tree:
    # depending on a shipped project having no tests/ makes this case hostage
    # to that project gaining one, which is exactly what happened when the
    # store tier gave projects/example a suite of its own.
    suiteless = os.path.join(args.output, "suiteless")
    shutil.copytree(args.template_project, suiteless,
                    ignore=shutil.ignore_patterns("tests", "builds", ".orkige"))
    code, output = run(editor, ["test", "--project", suiteless], QUICK_SECONDS)
    expect(code == 1, "a project with no tests/ directory fails (exit 1)")
    expect(".test.lua" in output and "tests" in output,
           "the refusal names the suffix and the directory that would fix it")

    # --- the real suite, including its play-mode test ---------------------
    # This is the case that justifies delegating to the runtime: a test
    # declaring a scene needs a live world, and only the player has one.
    reports = os.path.join(args.output, "reports")
    code, output = run(editor, ["test", "--project", args.project,
                                "--report-dir", reports], RUN_SECONDS)
    # The suite runs in the PLAYER, which needs a window. A process on a login
    # session that owns no screen (macOS fast user switching) cannot open one
    # and answers 77, which the editor relays. That is not a failed suite -
    # there is nothing here to run - so skip rather than report a red test that
    # would sit on top of the real ones.
    if code == 77:
        log("the runner reports no display session - skipping")
        sys.exit(77)
    expect(code == 0, "a passing suite comes back 0")
    artifact = ok_line(output)
    expect(artifact.endswith(".jsonl") and os.path.isfile(artifact),
           "the OK line names the JSONL artifact that exists: %s" % artifact)
    expect(os.path.dirname(os.path.realpath(artifact)) ==
           os.path.realpath(reports),
           "the artifact landed in the requested --report-dir")
    records = read_records(artifact)
    summary = [r for r in records if r.get("record") == "summary"]
    expect(len(summary) == 1, "the artifact carries exactly one summary")
    expect(summary[0]["passed"] > 0 and summary[0]["exitCode"] == 0,
           "the summary agrees with the exit code (%d passed)"
           % summary[0]["passed"])
    # the play-mode test is the one that could only have run in a live world
    play = [r for r in records
            if r.get("record") == "test" and "playthrough" in r.get("file", "")]
    expect(len(play) > 0 and all(r["status"] == "pass" for r in play),
           "the scene-declaring test ran and passed - a world was there")

    # --- a REUSED report directory still names this run's artifact ---------
    # a build server points --report-dir at the same collected directory every
    # time, so the OK line must never hand back a previous run's file
    first = artifact
    code, output = run(editor, ["test", "--project", args.project,
                                "--report-dir", reports], RUN_SECONDS)
    expect(code == 0, "a second run into the same --report-dir passes")
    again = ok_line(output)
    expect(os.path.isfile(again), "the second run names an artifact too")
    expect(again != first or
           os.path.getmtime(again) >= os.path.getmtime(first),
           "the named artifact is this run's, not the earlier one's")
    summary = [r for r in read_records(again)
               if r.get("record") == "summary"][0]
    expect(summary["exitCode"] == 0, "and it is a report of a passing run")

    # --- the filter reaches the runner ------------------------------------
    filtered = os.path.join(args.output, "filtered")
    code, output = run(editor, ["test", "--project", args.project,
                                "--test-filter", "tuning",
                                "--report-dir", filtered], RUN_SECONDS)
    expect(code == 0, "a filtered run passes")
    records = read_records(ok_line(output))
    summary = [r for r in records if r.get("record") == "summary"][0]
    expect(summary["filtered"] > 0 and summary["total"] > 0,
           "the filter selected a subset (%d run, %d filtered out)"
           % (summary["total"], summary["filtered"]))

    # --- a FAILING suite comes back non-zero ------------------------------
    # the direction that matters most: a door that always reported success
    # would let a broken game through every build server that trusted it
    broken = make_fixture(args.template_project,
                          os.path.join(args.output, "broken-project"),
                          {"broken.test.lua":
                           'test("this one passes", function(t)\n'
                           '    t.eq(2 + 2, 4)\n'
                           'end)\n'
                           '\n'
                           'test("this one does not", function(t)\n'
                           '    t.eq(2 + 2, 5, "arithmetic still works")\n'
                           'end)\n'})
    failing = os.path.join(args.output, "failing")
    code, output = run(editor, ["test", "--project", broken,
                                "--report-dir", failing], RUN_SECONDS)
    expect(code != 0, "a failing suite comes back non-zero (exit %d)" % code)
    expect("orkige_editor: ERROR" in output,
           "the failure is reported on the editor's own prefix")
    expect(output.count("orkige_editor: OK ") == 0,
           "a failing run prints no OK line")
    artifacts = [f for f in os.listdir(failing) if f.endswith(".jsonl")]
    expect(len(artifacts) == 1, "the failing run still wrote its artifact")
    records = read_records(os.path.join(failing, artifacts[0]))
    summary = [r for r in records if r.get("record") == "summary"][0]
    expect(summary["passed"] == 1 and summary["failed"] == 1,
           "the artifact names which one failed and which passed")

    log("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
