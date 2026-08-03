#!/usr/bin/env python3
"""check_doc_links.py - every doc the CODE points a person at must exist.

The published portal (Util/make_help_portal.py) fails its build on a broken
link BETWEEN docs, which is what keeps the corpus honest. It cannot see a
reference that lives inside a C++ string: a refusal that ends "see
Docs/device-payloads.md", or a help link the editor composes from a page name.
Rename that doc and the portal stays green while the editor ships a dead
pointer, with nothing to notice it.

So this walks the tree's own sources for the two shapes a code-side doc
reference takes and checks each against `Docs/` on disk:

    Docs/<name>.md              a doc named in a message or a comment
    helpUrl("<name>")           a published page the editor links to
                                (tools/editor/EditorHelpLinks.h composes the
                                URL from this stem plus the ONE portal root)

No network: the published site is not a test dependency, and it does not have
to be - a page exists on the site exactly when its doc exists in the corpus.

    python3 Util/check_doc_links.py [--repo <root>]

Exit 0 when every reference resolves, 1 with the file:line of each one that
does not.
"""

import argparse
import os
import re
import sys

#: where a code-side reference must resolve to
DOCS_DIR = "Docs"

#: the trees whose sources are scanned. Docs referencing docs are the portal
#: generator's job, and the archive/ports carry other projects' text.
SCAN_DIRS = ("orkige_core", "orkige_engine", "tools", "samples", "tests",
             "Util", "cmake", "projects")

#: file kinds that can carry a reference a person will read
SCAN_SUFFIXES = (".h", ".cpp", ".mm", ".c", ".py", ".lua", ".sh", ".cmake",
                 ".txt", ".in", ".yml")

#: directories that are never ours to police
SKIP_DIRS = {"build", "builds", ".git", "node_modules", "__pycache__",
             "vcpkg_installed", "linux_rig"}

#: repo-relative files exempt by nature. The portal generator's own selftest
#: builds a corpus with DELIBERATELY broken links to prove it catches them, so
#: reading its fixtures as real references would invert its meaning.
SKIP_FILES = {os.path.join("Util", "make_help_portal.py"),
              os.path.join("Util", "check_doc_links.py")}

#: `Docs/<name>.md`, optionally with a `#anchor` this check ignores (anchors
#: inside the corpus are the portal generator's gate)
DOC_REFERENCE = re.compile(r"Docs/([A-Za-z0-9_./-]+\.md)")
#: the editor's page-name composition, and the accessor that returns one
HELP_PAGE = re.compile(r'helpUrl\(\s*"([A-Za-z0-9_-]+)"\s*\)')
#: a `return "<stem>";` inside a function whose name ends in HelpPage - the
#! indirection EditorPayloads uses so the stem is declared once
HELP_PAGE_ACCESSOR = re.compile(
    r"HelpPage\(\)\s*\{[^}]*?return\s+\"([A-Za-z0-9_-]+)\"", re.DOTALL)


def scan_files(repo):
    for top in SCAN_DIRS:
        root_dir = os.path.join(repo, top)
        if not os.path.isdir(root_dir):
            continue
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [name for name in dirs if name not in SKIP_DIRS]
            for name in sorted(files):
                path = os.path.join(root, name)
                if not name.endswith(SCAN_SUFFIXES):
                    continue
                if os.path.relpath(path, repo) in SKIP_FILES:
                    continue
                yield path


def references(path):
    """(line number, doc-relative path) for every reference in `path`"""
    try:
        with open(path, "r", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return
    for number, line in enumerate(text.splitlines(), start=1):
        for match in DOC_REFERENCE.finditer(line):
            yield number, match.group(1)
        for match in HELP_PAGE.finditer(line):
            yield number, match.group(1) + ".md"
    for match in HELP_PAGE_ACCESSOR.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        yield line, match.group(1) + ".md"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))))
    args = parser.parse_args()

    docs_root = os.path.join(args.repo, DOCS_DIR)
    if not os.path.isdir(docs_root):
        print("check_doc_links: no %s in '%s'" % (DOCS_DIR, args.repo),
              file=sys.stderr)
        return 1

    broken = []
    checked = 0
    for path in scan_files(args.repo):
        for number, relative in references(path):
            checked += 1
            if not os.path.isfile(os.path.join(docs_root, relative)):
                broken.append("%s:%d: Docs/%s does not exist"
                              % (os.path.relpath(path, args.repo), number,
                                 relative))
    if broken:
        print("check_doc_links: %d code-side reference(s) name a doc that is "
              "not in %s/:" % (len(broken), DOCS_DIR), file=sys.stderr)
        for problem in sorted(set(broken)):
            print("  " + problem, file=sys.stderr)
        print("A doc named in a message or a help link is a link a person "
              "follows; rename the doc and this fails instead of the link.",
              file=sys.stderr)
        return 1
    print("check_doc_links: %d code-side doc reference(s), all resolved"
          % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
