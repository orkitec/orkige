#!/usr/bin/env python3
"""Assert the native-module ABI-stamp guard fires on a stale/mismatched engine,
AND that the stamp is scoped to the engine's source surface only.

A native game module resolves the engine as a find_package(Orkige) build-tree
package (cmake/OrkigePackage.cmake) and requires the package's ABI stamp match
the stamp of the engine sources the module compiles against
(cmake/OrkigeGameModule.cmake). Two things must hold:

  1. The guard MECHANISM fires: when the module's expected version differs from
     the package's recorded version, the module's cmake CONFIGURE fails with the
     ABI-mismatch error (the regression proof for the crash class where a module
     links a stale liborkige_engine.a whose object layout no longer matches the
     headers - the JumperNative null-deref).

  2. The stamp SCOPING is correct. The stamp is a git-INDEPENDENT content
     fingerprint of the engine source surface, so it must: bump on an ENGINE
     source edit; bump on a brand-new UNTRACKED engine file (a git-diff stamp
     would miss this - the very case the owner flagged); and NOT bump on a
     NON-engine change (a game file, a doc), so the guard stays silent through
     the ordinary edit-your-game-and-replay loop instead of firing on every edit.

Configure-only and hermetic: the scoping probe runs entirely inside a throwaway
synthetic source tree (no git, no real working-tree file touched). Exit 0 = all
held; non-zero (with a diagnostic) = a guard/scoping property regressed.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

# the deliberately-wrong expected version; the real engine stamp is
# "2.0.<big>.<big>" (a hash of the sources), never this value
BOGUS_VERSION = "2.0.1.1"
MISMATCH_SENTINEL = "Orkige engine ABI mismatch"
STAMP_MARKER = "ORKIGE_ABI_PROBE="

# a path INSIDE the engine ABI surface and one OUTSIDE it, exercised in a
# hermetic synthetic tree (below): editing the first must move the stamp, the
# second must not
ENGINE_PROBE_FILE = os.path.join("orkige_engine", "engine_graphic", "Engine.h")
NON_ENGINE_PROBE_FILE = os.path.join(
    "projects", "jumper-native", "native", "main.cpp")
# a brand-new engine file that does not exist in the baseline tree - adding it
# must move the stamp (the untracked-file case a git-diff stamp would miss)
NEW_ENGINE_FILE = os.path.join("orkige_engine", "engine_graphic", "NewApi.h")
# the minimal source tree the scoping probe builds: one file inside each engine
# layer + one cmake link file, plus a game file and a doc OUTSIDE the surface
SYNTH_FILES = {
    os.path.join("orkige_core", "Core.h"): "#pragma once\n",
    ENGINE_PROBE_FILE: "#pragma once\n",
    os.path.join("cmake", "OrkigeGameModule.cmake"): "# stub\n",
    NON_ENGINE_PROBE_FILE: "int main() { return 0; }\n",
    os.path.join("Docs", "readme.md"): "# doc\n",
}


def fail(message):
    sys.stderr.write("run_module_mismatch_test: %s\n" % message)
    sys.exit(1)


def compute_stamp(abi_cmake_dir, source_root, cmake):
    """the ABI version orkige_compute_abi_stamp (the REAL function, included from
    <abi_cmake_dir>/OrkigeAbiStamp.cmake) derives for the source tree at
    <source_root> - run through a throwaway cmake -P so it is the EXACT code the
    module and the engine both use"""
    script = ('include("%s/OrkigeAbiStamp.cmake")\n'
              'orkige_compute_abi_stamp("%s" _v _s)\n'
              'message("%s${_v}")\n'
              % (abi_cmake_dir.replace("\\", "/"),
                 source_root.replace("\\", "/"), STAMP_MARKER))
    handle, path = tempfile.mkstemp(suffix=".cmake")
    try:
        with os.fdopen(handle, "w") as script_file:
            script_file.write(script)
        result = subprocess.run([cmake, "-P", path],
                                capture_output=True, text=True)
        match = re.search(STAMP_MARKER + r"(\S+)",
                          result.stdout + result.stderr)
        if not match:
            sys.stderr.write(result.stdout + result.stderr)
            fail("could not compute the ABI stamp via cmake -P")
        return match.group(1)
    finally:
        os.remove(path)


def write_file(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as handle:
        handle.write(text)


def build_synthetic_tree(root):
    """a tiny NON-git source tree mirroring the paths the ABI surface covers (and
    some it must NOT) - the fingerprint is content-based, so no git is needed and
    the probe never touches the real working tree, racing nothing"""
    for rel, text in SYNTH_FILES.items():
        write_file(root, rel, text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, help="the engine source root")
    parser.add_argument("--engine-build", required=True,
                        help="the built engine tree carrying the package")
    parser.add_argument("--module-source", required=True,
                        help="the native module CMake source dir")
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake")
    parser.add_argument("--ninja", default=shutil.which("ninja") or "")
    args = parser.parse_args()

    if not os.path.isfile(os.path.join(args.module_source, "CMakeLists.txt")):
        fail("no CMakeLists.txt under module source '%s'" % args.module_source)
    if not os.path.isfile(os.path.join(args.engine_build, "OrkigeConfig.cmake")):
        fail("engine tree '%s' carries no OrkigeConfig.cmake - build the engine "
             "first" % args.engine_build)

    build_dir = tempfile.mkdtemp(prefix="orkige_abi_mismatch_")
    try:
        configure = [args.cmake, "-S", args.module_source, "-B", build_dir,
                     "-DORKIGE_ROOT=" + args.repo,
                     "-DORKIGE_ENGINE_BUILD_DIR=" + args.engine_build,
                     "-DORKIGE_EXPECTED_ABI_VERSION=" + BOGUS_VERSION,
                     "-DCMAKE_IGNORE_PREFIX_PATH=/usr/local"]
        if args.ninja:
            configure += ["-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + args.ninja]
        result = subprocess.run(configure, capture_output=True, text=True)
        output = result.stdout + result.stderr

        if result.returncode == 0:
            sys.stderr.write(output)
            fail("module configure SUCCEEDED against a version-mismatched "
                 "engine package - the ABI guard did not fire")
        if MISMATCH_SENTINEL not in output:
            sys.stderr.write(output)
            fail("module configure failed, but NOT with the ABI-mismatch guard "
                 "(sentinel '%s' absent) - it failed for another reason"
                 % MISMATCH_SENTINEL)
        # the message must name BOTH the found and expected versions and the fix.
        # CMake hard-wraps a FATAL_ERROR message at its own width, and that width
        # differs by platform/CMake version - so a multi-word phrase like "rebuild
        # the engine tree" can be split across lines. Match against a whitespace-
        # collapsed copy so the check is wrap-insensitive.
        normalized = " ".join(output.split())
        for needed in (BOGUS_VERSION, "rebuild the engine tree"):
            if needed not in normalized:
                sys.stderr.write(output)
                fail("ABI-mismatch message is missing '%s'" % needed)
        print("ABI-stamp guard fired as designed (module configure refused a "
              "version-mismatched engine package)")
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)

    # 2. the stamp is a correctly-scoped content fingerprint, probed entirely in
    # a throwaway synthetic source tree (git-independent, no real file touched)
    abi_cmake_dir = os.path.join(args.repo, "cmake")
    synth = tempfile.mkdtemp(prefix="orkige_abi_scope_")
    try:
        build_synthetic_tree(synth)
        baseline = compute_stamp(abi_cmake_dir, synth, args.cmake)

        # a NON-engine edit (a game file) must NOT move the stamp
        write_file(synth, NON_ENGINE_PROBE_FILE,
                   SYNTH_FILES[NON_ENGINE_PROBE_FILE] + "// edited game code\n")
        non_engine = compute_stamp(abi_cmake_dir, synth, args.cmake)
        if non_engine != baseline:
            fail("a NON-engine edit (%s) changed the ABI stamp (%s -> %s) - the "
                 "guard would fire on every ordinary game edit"
                 % (NON_ENGINE_PROBE_FILE, baseline, non_engine))
        write_file(synth, NON_ENGINE_PROBE_FILE,
                   SYNTH_FILES[NON_ENGINE_PROBE_FILE])  # restore

        # an ENGINE-source edit must move the stamp
        write_file(synth, ENGINE_PROBE_FILE,
                   SYNTH_FILES[ENGINE_PROBE_FILE] + "int added_member;\n")
        engine = compute_stamp(abi_cmake_dir, synth, args.cmake)
        if engine == baseline:
            fail("an ENGINE-source edit (%s) did NOT change the ABI stamp - a "
                 "stale engine library would go undetected" % ENGINE_PROBE_FILE)
        write_file(synth, ENGINE_PROBE_FILE,
                   SYNTH_FILES[ENGINE_PROBE_FILE])  # restore

        # a brand-NEW engine file (an untracked header a git-diff stamp would
        # miss entirely) must move the stamp
        new_path = os.path.join(synth, NEW_ENGINE_FILE)
        if os.path.exists(new_path):
            fail("probe file '%s' unexpectedly already exists" % NEW_ENGINE_FILE)
        write_file(synth, NEW_ENGINE_FILE, "#pragma once\nstruct NewApi {};\n")
        added = compute_stamp(abi_cmake_dir, synth, args.cmake)
        if added == baseline:
            fail("a NEW untracked engine file (%s) did NOT change the ABI stamp "
                 "- a new engine header could skew a module undetected"
                 % NEW_ENGINE_FILE)
    finally:
        shutil.rmtree(synth, ignore_errors=True)
    print("ABI stamp is an engine-scoped content fingerprint (engine edit AND a "
          "new untracked engine file move it; a game edit does not)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
