#!/usr/bin/env python3
"""The editor's command-line front door, driven as a caller would drive it.

The pure argument router is unit-tested (tests/editor_core/EditorCliTests.cpp);
what this asserts is the part only the REAL binary can prove:

  * a subcommand run NEVER opens a window - every case here finishes on its own
    within a short deadline, on a machine that may have no display at all. That
    is the whole reason this door exists, and a regression would show up as a
    build job that hangs rather than as a failing assertion, so the deadline is
    the assertion.
  * `export` packages a project through the editor's own engine-source
    resolution and prints the one line callers grep for.
  * the exit codes are the contract: 0 worked, 1 ran and failed, 2 bad usage.

Exit code is this driver's own contract: 0 = every case passed.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

# a headless subcommand does no rendering and boots no engine; anything slower
# than this means it went down the window road
QUICK_SECONDS = 60.0


def log(message):
    print("[cli] %s" % message, flush=True)


def fail(message):
    print("[cli] FAILED: %s" % message, flush=True)
    sys.exit(1)


def run(editor, argv, seconds=QUICK_SECONDS, environment=None):
    """run the editor with `argv` and return (exit code, merged output)"""
    log("$ orkige_editor " + " ".join(argv))
    started = time.time()
    child_environment = None
    if environment:
        child_environment = dict(os.environ)
        child_environment.update(environment)
    process = subprocess.Popen([editor] + argv, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               errors="replace", env=child_environment)
    try:
        output = process.communicate(timeout=seconds)[0]
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()
        fail("'%s' did not finish in %.0fs - a subcommand must never open a "
             "window" % (" ".join(argv), seconds))
    log("  exit %d in %.1fs" % (process.returncode, time.time() - started))
    return (process.returncode, output)


def expect(condition, what):
    if not condition:
        fail(what)
    log("  ok: " + what)


def ok_line(output):
    """the machine-readable success line, or "" """
    for line in output.splitlines():
        if line.startswith("orkige_editor: OK "):
            return line[len("orkige_editor: OK "):].strip()
    return ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True,
                        help="the editor executable (inside the .app on macOS)")
    parser.add_argument("--project", required=True)
    parser.add_argument("--platform", default="macos")
    parser.add_argument("--output", required=True)
    # the tree packages desktop apps on macOS alone today, exactly like the
    # export_* tests; everywhere else the usage surface and the refusals are
    # still the contract and still checked
    parser.add_argument("--skip-export", action="store_true")
    args = parser.parse_args()

    editor = args.editor
    if not os.path.isfile(editor):
        fail("no editor executable at " + editor)

    # --- the usage surface ------------------------------------------------
    code, output = run(editor, ["help"])
    expect(code == 0, "`help` exits 0")
    expect("export" in output and "fetch-payload" in output,
           "the usage text names the subcommands")
    expect("Scene, asset and editor-script operations are NOT" in output,
           "the usage text is honest about what is NOT headless")

    code, output = run(editor, ["--help"])
    expect(code == 0, "`--help` exits 0 too")

    code, output = run(editor, ["version"])
    expect(code == 0 and "orkige_editor" in output,
           "`version` prints the build identity")

    # --- THE HAZARD -------------------------------------------------------
    # a mistyped subcommand used to be ignored and the editor opened a window,
    # which on a build server is a job that hangs until its timeout with
    # nothing in the log. It must refuse, by name, with exit 2.
    code, output = run(editor, ["exprot", "--project", args.project,
                                "--platform", args.platform])
    expect(code == 2, "a mistyped subcommand exits 2")
    expect("exprot" in output, "...and the refusal names what was typed")

    # --- the editor is a WINDOW application, and says so --------------------
    # ORKIGE_RENDERSYSTEM can name the deviceless render system the PLAYER
    # boots to hold a scene with no display. The editor cannot be that, so a
    # windowed launch has to refuse by name instead of failing somewhere inside
    # a render system - and it has to refuse FAST, which is what the deadline
    # on this call asserts (an editor that got as far as opening a window would
    # sit here until the timeout).
    code, output = run(editor, [],
                       environment={"ORKIGE_RENDERSYSTEM": "null"})
    expect(code == 1, "a deviceless windowed launch exits 1")
    expect("ORKIGE_RENDERSYSTEM=null" in output,
           "...and the refusal names the variable and its value")

    # ...while a SUBCOMMAND is exempt: it installs no render system, so a build
    # server that sets the variable machine-wide still packages its games
    code, output = run(editor, ["version"],
                       environment={"ORKIGE_RENDERSYSTEM": "null"})
    expect(code == 0, "a subcommand still runs with the variable set")

    code, output = run(editor, ["export", "--project", args.project])
    expect(code == 2, "export without --platform is a usage error (2)")

    code, output = run(editor, ["export", "--project", args.project,
                                "--platform", "macos", "--turbo", "1"])
    expect(code == 2, "an unknown export option is a usage error (2)")

    # --- an operation that RUNS and fails is a different code -------------
    code, output = run(editor, ["export", "--project", args.project,
                                "--platform", "nintendo"])
    expect(code == 1, "an unpackageable platform exits 1, not 2")

    code, output = run(editor, ["export", "--project",
                                os.path.join(args.output, "no-such-project"),
                                "--platform", args.platform])
    expect(code == 1, "a missing project exits 1")

    # --- the export itself ------------------------------------------------
    if args.skip_export:
        log("PASSED (usage + refusals; this host packages no desktop app)")
        return 0
    output_dir = os.path.join(args.output, "artifact")
    shutil.rmtree(output_dir, ignore_errors=True)
    os.makedirs(output_dir, exist_ok=True)
    # an export builds a dylib closure and copies a payload; give it room
    code, output = run(editor, ["export", "--project", args.project,
                                "--platform", args.platform,
                                "--output", output_dir], seconds=1800.0)
    if code != 0:
        print(output[-4000:], flush=True)
        fail("the headless export failed (%d)" % code)
    artifact = ok_line(output)
    expect(artifact != "", "the run ends with 'orkige_editor: OK <artifact>'")
    expect(os.path.exists(artifact), "the artifact exists: " + artifact)
    expect(os.path.abspath(artifact).startswith(os.path.abspath(output_dir)),
           "--output placed the artifact where it was asked to")
    if sys.platform == "darwin" and args.platform == "macos":
        expect(artifact.endswith(".app"), "the macOS artifact is an .app")
        expect(os.path.isdir(os.path.join(artifact, "Contents", "MacOS")),
               "the .app carries an executable directory")
    elif sys.platform.startswith("linux") and args.platform == "linux":
        # the portable directory: the binary carries the directory's own name,
        # which is what makes `cd <dir> && ./<dir>` the whole launch
        expect(os.path.isdir(artifact), "the Linux artifact is a directory")
        executable = os.path.join(artifact, os.path.basename(artifact))
        expect(os.path.isfile(executable) and os.access(executable, os.X_OK),
               "the directory carries its executable")

    log("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
