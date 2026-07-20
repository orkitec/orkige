#!/usr/bin/env python3
"""Assert the native-module ABI-stamp guard fires on a stale/mismatched engine.

A native game module resolves the engine as a find_package(Orkige) build-tree
package (cmake/OrkigePackage.cmake) and requires the package's ABI stamp match
the stamp of the engine sources the module compiles against
(cmake/OrkigeGameModule.cmake). This driver simulates a stale engine library -
the module's headers expect a DIFFERENT engine version than the one baked into
the archive - by overriding the expected stamp, then asserts the module's cmake
CONFIGURE fails with the ABI-mismatch error. This is the regression proof for
the crash class where a module links a stale liborkige_engine.a whose object
layout no longer matches the headers (the JumperNative null-deref).

Configure-only and hermetic: it builds into a throwaway tree and never touches
the real engine or module build trees. Exit 0 = the guard fired as designed;
non-zero (with a diagnostic) = the guard is missing or fired for the wrong
reason.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

# the deliberately-wrong expected version; the real engine stamp is
# "2.0.<big>.<big>" (a hash of the sources), never this value
BOGUS_VERSION = "2.0.1.1"
MISMATCH_SENTINEL = "Orkige engine ABI mismatch"


def fail(message):
    sys.stderr.write("run_module_mismatch_test: %s\n" % message)
    sys.exit(1)


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
        return 0
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
