#!/usr/bin/env python3
# check_doxyfile.py - validate Docs/api/Doxyfile by actually rendering the
# /api/ class reference from the engine headers. The tooling is CI-only (the
# Pages workflow installs it, see .github/workflows/pages.yml), so a machine
# without it SKIPS honestly (exit 77, the ctest convention) instead of
# failing. With the tool present, the check asserts what the deployed site
# depends on: a clean nonzero-exit-free run over the real tree, an
# index.html, a known engine class in the class list, and the footer's
# site-root + imprint/privacy links on the emitted pages.

import os
import re
import shutil
import subprocess
import sys
import tempfile

SCRIPT_PATH = os.path.abspath(__file__)
ROOT = os.path.dirname(os.path.dirname(SCRIPT_PATH))
DOXYFILE = os.path.join(ROOT, "Docs", "api", "Doxyfile")


def main():
    tool = shutil.which("doxygen")
    if not tool:
        print("check_doxyfile: SKIP - the documentation tool is not "
              "installed (CI-only tooling; the Pages workflow provides it)")
        return 77
    version = subprocess.run([tool, "--version"], capture_output=True,
                             text=True).stdout.strip()
    with open(DOXYFILE, "r", encoding="utf-8") as f:
        config = f.read()
    with tempfile.TemporaryDirectory() as temp:
        config += "\nOUTPUT_DIRECTORY = %s\n" % temp
        if not shutil.which("dot"):
            # graph rendering is optional for validation - the workflow's
            # runner installs it, a dev machine may not have it
            config += "HAVE_DOT = NO\n"
        result = subprocess.run([tool, "-"], input=config.encode("utf-8"),
                                cwd=ROOT, capture_output=True)
        if result.returncode != 0:
            sys.stderr.write("check_doxyfile: the run FAILED (exit %d):\n%s\n"
                             % (result.returncode,
                                result.stderr.decode("utf-8",
                                                     "replace")[-4000:]))
            return 1
        html_dir = os.path.join(temp, "html")
        index = os.path.join(html_dir, "index.html")
        if not os.path.isfile(index):
            sys.stderr.write("check_doxyfile: no html/index.html emitted\n")
            return 1
        with open(index, "r", encoding="utf-8", errors="replace") as f:
            page = f.read()
        for needle in ('href="../index.html"', 'href="../imprint.html"',
                       'href="../privacy.html"',
                       'href="../help/index.html"'):
            if needle not in page:
                sys.stderr.write("check_doxyfile: footer link %s missing "
                                 "from index.html\n" % needle)
                return 1
        if "$navpath" in page:
            sys.stderr.write("check_doxyfile: an unsubstituted footer "
                             "placeholder leaked into the output\n")
            return 1
        annotated = os.path.join(html_dir, "annotated.html")
        with open(annotated, "r", encoding="utf-8", errors="replace") as f:
            class_list = f.read()
        for name in ("ScriptRuntime", "GuiManager"):
            if name not in class_list:
                sys.stderr.write("check_doxyfile: expected class %s missing "
                                 "from the class list\n" % name)
                return 1
        page_count = len([n for n in os.listdir(html_dir)
                          if n.endswith(".html")])
    print("check_doxyfile: OK (tool %s, %d html pages)"
          % (version, page_count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
