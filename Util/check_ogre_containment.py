#!/usr/bin/env python3
"""Renderer containment lint (WP-A1.5, Docs/render-abstraction.md).

Fails when an `Ogre::` spelling appears in CODE (comments are stripped)
outside the render backend:

  * allowed_dirs   - the backend directories (engine_graphic,
                     engine_render_classic, engine_render_next in A2): Ogre
                     IS their implementation language.
  * allowed_files  - the facade math alias header (RenderMath.h), THE
                     documented swap point of the math decision.
  * sanctioned_dirs        - whole-directory sanctions for the classic-only
                     zones (engine_fastgui, engine_filesystem's
                     Ogre::Archive subclasses); reason per entry.
  * sanctioned_files       - whole-file sanctions for the classic-only
                     helpers and the accepted math-alias residue; every entry
                     carries its reason and goes stale (= lint failure)
                     when the file no longer contains any Ogre spelling.
  * sanctioned_block_files - files whose Ogre spellings must sit inside
                     explicitly marked blocks:
                         // ORKIGE_SANCTIONED_OGRE_BEGIN(<tag>)
                         ...
                         // ORKIGE_SANCTIONED_OGRE_END
                     (the classic app boot blocks, the editor's overlay/
                     grid glue, the AnimationComponent root-motion
                     backdoor). Markers in files NOT on this list fail.

The sanction list lives in Util/ogre_containment.json - adding a new
Ogre-speaking file means editing that config in the same change, which is
exactly the review speed bump this lint exists for. Wired into ctest as
`render_containment_lint` (LABELS unit -> runs in the unit AND desktop
presets).
"""

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = Path(__file__).resolve().parent / "ogre_containment.json"

SCAN_ROOTS = ["orkige_core", "orkige_engine", "samples", "projects",
              "tools", "tests"]
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cxx", ".mm", ".m"}
# build trees (editor compile-on-Play, exporter) live inside the scan roots
SKIP_DIR_NAMES = {"build", "build-export"}

OGRE_TOKEN = re.compile(r"\bOgre\s*::")
MARKER_BEGIN = re.compile(r"ORKIGE_SANCTIONED_OGRE_BEGIN\(([^)]*)\)")
MARKER_END = re.compile(r"ORKIGE_SANCTIONED_OGRE_END")


def strip_comments(text):
    """Remove // and /* */ comments, preserving line structure."""
    out = []
    i = 0
    n = len(text)
    in_line = False
    in_block = False
    in_string = None  # the quote character, or None
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if in_line:
            if c == "\n":
                in_line = False
                out.append(c)
            i += 1
            continue
        if in_block:
            if c == "*" and nxt == "/":
                in_block = False
                i += 2
                continue
            if c == "\n":
                out.append(c)
            i += 1
            continue
        if in_string:
            out.append(c)
            if c == "\\" and nxt:
                out.append(nxt)
                i += 2
                continue
            if c == in_string:
                in_string = None
            i += 1
            continue
        if c == "/" and nxt == "/":
            in_line = True
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block = True
            i += 2
            continue
        if c in "\"'":
            in_string = c
        out.append(c)
        i += 1
    return "".join(out)


def marked_ranges(raw_lines):
    """1-based inclusive line ranges covered by sanction markers, plus a
    list of marker problems (unbalanced/nested)."""
    ranges = []
    problems = []
    begin_line = None
    for lineno, line in enumerate(raw_lines, 1):
        if MARKER_BEGIN.search(line):
            if begin_line is not None:
                problems.append(f"line {lineno}: nested "
                                "ORKIGE_SANCTIONED_OGRE_BEGIN")
            begin_line = lineno
        elif MARKER_END.search(line):
            if begin_line is None:
                problems.append(f"line {lineno}: ORKIGE_SANCTIONED_OGRE_END "
                                "without BEGIN")
            else:
                ranges.append((begin_line, lineno))
                begin_line = None
    if begin_line is not None:
        problems.append(f"line {begin_line}: unclosed "
                        "ORKIGE_SANCTIONED_OGRE_BEGIN")
    return ranges, problems


def main():
    config = json.loads(CONFIG_PATH.read_text())
    allowed_dirs = [Path(p) for p in config["allowed_dirs"]]
    allowed_files = {Path(p) for p in config["allowed_files"]}
    sanctioned_dirs = {Path(p): reason for p, reason
                       in config["sanctioned_dirs"].items()}
    sanctioned_files = {Path(p): reason for p, reason
                        in config["sanctioned_files"].items()}
    block_files = {Path(p): reason for p, reason
                   in config["sanctioned_block_files"].items()}

    violations = []
    seen_sanctioned = set()
    seen_block = set()

    for root in SCAN_ROOTS:
        root_path = REPO_ROOT / root
        if not root_path.is_dir():
            continue
        for path in sorted(root_path.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(REPO_ROOT)
            if any(part in SKIP_DIR_NAMES for part in rel.parts):
                continue
            if any(rel.is_relative_to(d) for d in allowed_dirs):
                continue
            if any(rel.is_relative_to(d) for d in sanctioned_dirs):
                continue
            if rel in allowed_files:
                continue

            raw = path.read_text(errors="replace")
            raw_lines = raw.splitlines()
            code_lines = strip_comments(raw).splitlines()
            token_lines = [lineno for lineno, line
                           in enumerate(code_lines, 1)
                           if OGRE_TOKEN.search(line)]
            ranges, marker_problems = marked_ranges(raw_lines)

            if rel in sanctioned_files:
                seen_sanctioned.add(rel)
                if not token_lines:
                    violations.append(
                        f"{rel}: STALE whole-file sanction - the file no "
                        "longer spells Ogre::; remove its entry from "
                        "Util/ogre_containment.json")
                continue

            if rel in block_files:
                seen_block.add(rel)
                for problem in marker_problems:
                    violations.append(f"{rel}: {problem}")
                if not ranges:
                    violations.append(
                        f"{rel}: STALE block sanction - no "
                        "ORKIGE_SANCTIONED_OGRE blocks left; remove its "
                        "entry from Util/ogre_containment.json")
                outside = [lineno for lineno in token_lines
                           if not any(a <= lineno <= b for a, b in ranges)]
                for lineno in outside:
                    violations.append(
                        f"{rel}:{lineno}: Ogre:: outside the sanctioned "
                        "blocks of this file")
                continue

            # unsanctioned file: no Ogre code and no markers allowed
            if ranges or marker_problems:
                violations.append(
                    f"{rel}: ORKIGE_SANCTIONED_OGRE markers in a file that "
                    "is not on the sanctioned_block_files list "
                    "(Util/ogre_containment.json)")
            for lineno in token_lines:
                violations.append(
                    f"{rel}:{lineno}: Ogre:: outside the render backend - "
                    "route through the engine_render facade (or sanction "
                    "the file with a reason in Util/ogre_containment.json)")

    # config hygiene: entries pointing at missing files are stale too
    for rel in sorted(set(sanctioned_files) - seen_sanctioned):
        violations.append(f"{rel}: sanctioned file does not exist (or was "
                          "not scanned) - remove the stale entry")
    for rel in sorted(set(block_files) - seen_block):
        violations.append(f"{rel}: block-sanctioned file does not exist (or "
                          "was not scanned) - remove the stale entry")

    if violations:
        print("render containment lint FAILED "
              f"({len(violations)} problem(s)):")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print("render containment lint OK: no unsanctioned Ogre:: spellings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
