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

  2. The stamp SCOPING is correct (both directions): an ENGINE-source change
     bumps the stamp (so it would be refused), while a NON-engine change - a
     game file, a doc - does NOT, so the guard stays silent through the ordinary
     edit-your-game-and-replay loop instead of firing on every edit.

Configure-only and hermetic: it uses throwaway trees, and any source edit it
makes to probe the scoping is restored byte-for-byte. Exit 0 = all held; non-zero
(with a diagnostic) = a guard/scoping property regressed.
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
# hermetic synthetic repo (below): editing the first must move the stamp, the
# second must not
ENGINE_PROBE_FILE = os.path.join("orkige_engine", "engine_graphic", "Engine.h")
NON_ENGINE_PROBE_FILE = os.path.join(
    "projects", "jumper-native", "native", "main.cpp")
# the minimal tree the scoping probe commits: one file inside each engine layer
# + one cmake link file, plus a game file and a doc OUTSIDE the surface
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


def compute_stamp(abi_cmake_dir, git_root, cmake):
    """the ABI version orkige_compute_abi_stamp (the REAL function, included from
    <abi_cmake_dir>/OrkigeAbiStamp.cmake) derives for the git tree at <git_root>
    - run through a throwaway cmake -P so it is the EXACT code the module and the
    engine both use"""
    script = ('include("%s/OrkigeAbiStamp.cmake")\n'
              'orkige_compute_abi_stamp("%s" _v _s)\n'
              'message("%s${_v}")\n'
              % (abi_cmake_dir.replace("\\", "/"),
                 git_root.replace("\\", "/"), STAMP_MARKER))
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


def git(root, *args):
    subprocess.run(["git", "-C", root] + list(args),
                   check=True, capture_output=True, text=True)


def build_synthetic_repo(root):
    """a tiny committed git repo mirroring the paths the ABI pathspec cares about
    (and some it must NOT) - so the scoping probe never edits the real working
    tree and can run alongside any other test"""
    for rel, text in SYNTH_FILES.items():
        write_file(root, rel, text)
    git(root, "init", "-q")
    git(root, "-c", "user.email=t@t", "-c", "user.name=t",
        "add", "-A")
    git(root, "-c", "user.email=t@t", "-c", "user.name=t",
        "commit", "-q", "-m", "init")


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
        # the message must name BOTH the found and expected versions and the fix
        for needed in (BOGUS_VERSION, "rebuild the engine tree"):
            if needed not in output:
                sys.stderr.write(output)
                fail("ABI-mismatch message is missing '%s'" % needed)
        print("ABI-stamp guard fired as designed (module configure refused a "
              "version-mismatched engine package)")
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)

    # 2. the stamp is scoped to the engine surface (both directions), probed in
    # a hermetic synthetic repo so no real working-tree file is ever touched
    abi_cmake_dir = os.path.join(args.repo, "cmake")
    synth = tempfile.mkdtemp(prefix="orkige_abi_scope_")
    try:
        build_synthetic_repo(synth)
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
    finally:
        shutil.rmtree(synth, ignore_errors=True)
    print("ABI stamp is engine-scoped (an engine-source edit moves it, a game "
          "edit does not)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
