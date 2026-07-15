#!/usr/bin/env python3
"""Assert a failed engine boot unwinds to a CLEAN exit, never a crash.

The engine boot must be exception-safe end to end: a failure anywhere between
the window coming up and the first frame (render-system init, RTShader init,
scene-manager creation, resource-group init, a render call a contended or broken
driver throws from) has to return a clean non-zero exit, NOT escape as an
uncaught throw. An uncaught throw terminates the process, and on some drivers
(MoltenVK on the CI hosts) segfaults while unwinding partial GPU state - the
crash this test guards against.

The boot app (hello_orkige) is driven with ORKIGE_TEST_FORCE_BOOT_FAILURE set to
each staged failure point. A CLEAN failure exits with a positive status; a crash
shows up as a negative return code (killed by a signal) and a hang as a timeout -
both fail this test. Stdlib only, per the toolchain policy.
"""

import argparse
import os
import subprocess
import sys

# the staged failure points, in boot order: a window-creation throw (caught in
# Engine::configure), a post-window engine throw (RTShader/scene manager region),
# and a resource-init throw (the last host step before the first frame)
STAGES = ["window", "postwindow", "resources"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True,
                        help="the boot app to drive (hello_orkige)")
    parser.add_argument("--timeout", type=int, default=60,
                        help="per-stage seconds before a hang is called a failure")
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"SKIP: boot app not built: {args.binary}")
        return 77  # ctest SKIP_RETURN_CODE

    failures = []
    for stage in STAGES:
        env = dict(os.environ)
        env["ORKIGE_TEST_FORCE_BOOT_FAILURE"] = stage
        # frame-limited + automated so a boot that (wrongly) SUCCEEDS still exits
        # fast instead of running the app forever
        env["ORKIGE_DEMO_FRAMES"] = "5"
        env["ORKIGE_AUTOMATED_RUN"] = "1"
        try:
            result = subprocess.run([args.binary], env=env,
                                    timeout=args.timeout,
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            print(f"FAIL: stage '{stage}' hung - boot failure did not exit")
            failures.append(stage)
            continue
        code = result.returncode
        if code < 0:
            print(f"FAIL: stage '{stage}' CRASHED (killed by signal {-code}) - "
                  "the failed boot did not unwind cleanly")
            failures.append(stage)
        elif code == 0:
            # the seam is deterministic once the window is up; a 0 exit means the
            # forced failure never took effect (a broken seam), so the test would
            # be silently vacuous - flag it rather than pass
            print(f"FAIL: stage '{stage}' exited 0 - the forced failure did not "
                  "fire (a machine with no render system fails earlier, which is "
                  "still a clean non-zero exit)")
            failures.append(stage)
        else:
            print(f"ok: stage '{stage}' failed cleanly (exit {code}, no crash)")

    if failures:
        print(f"boot failure test FAILED on stages: {', '.join(failures)}")
        return 1
    print("boot failure unwinds to a clean exit on every staged failure point")
    return 0


if __name__ == "__main__":
    sys.exit(main())
