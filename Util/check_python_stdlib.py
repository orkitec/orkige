#!/usr/bin/env python3
"""Python stdlib-only lint for the Util/ toolchain.

Policy (build requirements, CLAUDE.md): every Util/*.py asset generator and
build helper imports ONLY the Python standard library plus its sibling Util
modules - no third-party packages, ever. The shipped player never needs
Python, the tooling never needs a pip install, and orkige_png.py is the
deliberate no-imaging-package PNG codec that keeps it that way.

This lint walks every Util/*.py, parses its imports with the `ast` module and
FAILS on any imported top-level module that is neither in
`sys.stdlib_module_names` nor a sibling Util module (a `<name>.py` next to it).
The failure text names the file and the offending import.

It also staleness-gates the documented version floor: it greps CLAUDE.md for
the `python_stdlib_lint` enforcement line and the `python3 >= 3.10` floor and
fails if either has vanished, so the docs cannot silently drift from the rule
this lint enforces.

Stdlib-only and headless itself (it obeys its own policy). Wired into ctest as
`python_stdlib_lint` (LABELS unit -> runs in the unit AND desktop presets).
Needs python3 >= 3.10 for `sys.stdlib_module_names`, which is the floor it
documents.
"""

import ast
import sys
from pathlib import Path

UTIL_DIR = Path(__file__).resolve().parent
REPO_ROOT = UTIL_DIR.parent
CLAUDE_MD = REPO_ROOT / "CLAUDE.md"

# Docs-currency anchors: the build-requirements line must keep saying both of
# these, or this lint (which the same line names) has gone stale.
DOC_ANCHORS = ["python_stdlib_lint", "python3 >= 3.10"]


def top_level_imports(tree):
    """The set of top-level module names imported by a parsed module. A dotted
    import (`xml.etree.ElementTree`, `from pathlib import Path`) contributes its
    first component (`xml`, `pathlib`). Relative imports (`from . import x`) are
    ignored - they are same-package, not external."""
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                names.add(alias.name.split(".")[0])
        elif isinstance(node, ast.ImportFrom):
            if node.level == 0 and node.module:
                names.add(node.module.split(".")[0])
    return names


def main():
    if sys.version_info < (3, 10):
        print("python_stdlib_lint needs python3 >= 3.10 (for "
              "sys.stdlib_module_names); found "
              f"{sys.version_info.major}.{sys.version_info.minor}")
        return 1

    stdlib = set(sys.stdlib_module_names)
    # sibling Util modules: any <name>.py in this directory is importable by its
    # neighbours (orkige_png, make_roller_assets, ...) - allowed by policy.
    siblings = {path.stem for path in UTIL_DIR.glob("*.py")}

    violations = []
    for path in sorted(UTIL_DIR.glob("*.py")):
        try:
            tree = ast.parse(path.read_text(errors="replace"),
                             filename=str(path))
        except SyntaxError as error:
            violations.append(f"{path.name}: could not parse ({error})")
            continue
        rel = path.relative_to(REPO_ROOT)
        for name in sorted(top_level_imports(tree)):
            if name in stdlib or name in siblings:
                continue
            violations.append(
                f"{rel}: imports '{name}', which is neither Python stdlib nor a "
                "sibling Util module - Util/*.py must stay stdlib-only (no "
                "third-party packages); vendor the logic or add the helper as a "
                "Util/*.py sibling")

    # docs-currency gate: the floor + enforcement line must still be in CLAUDE.md
    if not CLAUDE_MD.is_file():
        violations.append("CLAUDE.md not found - cannot verify the documented "
                          "python3 >= 3.10 floor")
    else:
        doc_text = CLAUDE_MD.read_text(errors="replace")
        for anchor in DOC_ANCHORS:
            if anchor not in doc_text:
                violations.append(
                    f"CLAUDE.md no longer mentions '{anchor}' - the build "
                    "requirements must document the python3 >= 3.10 stdlib-only "
                    "policy enforced by python_stdlib_lint")

    if violations:
        print("python stdlib lint FAILED "
              f"({len(violations)} problem(s)):")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print("python stdlib lint OK: every Util/*.py is stdlib-only and the "
          "documented python3 >= 3.10 floor is present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
