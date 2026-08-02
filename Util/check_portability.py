#!/usr/bin/env python3
"""Portability lint (Docs/render-abstraction.md - the lints live beside the
containment lint): mechanical enforcement for two Windows-only compile
hazards that are INVISIBLE to a local clang/libc++ build and only surface as
red on the MSVC CI job.

Two hazards, two checks over the same source set the containment lint walks
(orkige_core, orkige_engine, samples, projects, tools, tests; vendored,
generated and build trees excluded):

  Check A - Windows-macro identifier ban.
    windef.h / rpcndr.h define `near`, `far`, `small`, `interface` as
    EMPTY-or-legacy macros. A C++ identifier spelled like one of them is
    silently erased (or redefined) by the Win32 preprocessor - a variable
    named `near` vanished into `error C2513` on the MSVC job while every
    clang build was green. The check flags the BARE token only (word
    boundary), so `nearest`/`farRect`/`smallFont`/`interfaces` are fine;
    a genuine member/variable named `near` IS a hazard and is flagged.

  Check B - std-symbol direct-include enforcement (the curated MSVC-leak set).
    libc++ (macOS/Linux clang) leaks many standard headers transitively;
    MSVC's STL does not. `std::adjacent_find` compiled on macOS without
    <algorithm> and only broke on Windows (error C2039). A file that uses
    one of the curated symbols must include its header DIRECTLY (or an
    umbrella header that GUARANTEES it - the alias table). This is a
    curated leak set, deliberately NOT a full include-what-you-use pass.

Suppression: a same-line or immediately-preceding-line comment
`// portability-ok: <reason>` skips a single finding (the containment lint's
exception spirit, moved inline because these findings are per-line).

Config is data tables at the top of this file (no separate json - the tables
are small and curated; a growing alias table would be the trigger to split
one out). Wired into ctest as `portability_lint` (LABELS unit -> runs in the
unit AND desktop presets, mirroring the containment lint).
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# The containment lint's walker, reused so the two lints cover the same
# source tree (change one, change the other). ONE deliberate difference: the
# Objective-C sources (.mm/.m) are Apple-only translation units that NEVER
# reach the MSVC compiler, so neither Windows hazard can bite there (and ObjC
# `@interface` would be a false positive for the identifier ban) - they are
# out of scope for this lint.
SCAN_ROOTS = ["orkige_core", "orkige_engine", "samples", "projects",
              "tools", "tests"]
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cxx"}
SKIP_DIR_NAMES = {"build", "build-export"}
# --- Check A: banned Windows-macro identifier names -----------------------
# token -> WHY it is a hazard (all are windef.h / rpcndr.h legacy macros).
BANNED_IDENTIFIERS = {
    "near": "windef.h defines `near` as an EMPTY legacy segment macro - an "
            "identifier named `near` is erased by the Win32 preprocessor",
    "far": "windef.h defines `far` as an EMPTY legacy segment macro - same "
           "erase-the-identifier hazard as `near`",
    "small": "rpcndr.h defines `small` as `char` - an identifier named "
             "`small` is rewritten to a type keyword",
    "interface": "rpcndr.h/combaseapi.h define `interface` as `struct` - an "
                 "identifier named `interface` is rewritten to a keyword",
}

# --- Check B: curated std symbols that libc++ leaks but MSVC does not ------
# Two match modes:
#   "qualified" - flag `std::<symbol>` only (a bare `array`/`function`/
#                 `count` is far too common as a plain identifier to match).
#   "bare"      - flag the bare token OR `std::<symbol>` (the C-library
#                 spellings memcpy/isalpha/uintptr_t are unambiguous enough
#                 to catch unqualified, which is how they are usually used).
# symbol -> (required header, match mode)
STD_SYMBOLS = {
    # <filesystem> - libc++ leaks it through several headers, libstdc++ and
    # MSVC do not: a TU using std::filesystem without its own include builds
    # on macOS and fails on Linux/Windows (the HttpScriptTests case)
    "filesystem::path": ("<filesystem>", "qualified"),
    "filesystem::exists": ("<filesystem>", "qualified"),
    "filesystem::remove": ("<filesystem>", "qualified"),
    "filesystem::file_size": ("<filesystem>", "qualified"),
    "filesystem::temp_directory_path": ("<filesystem>", "qualified"),
    "filesystem::create_directories": ("<filesystem>", "qualified"),
    "filesystem::directory_iterator": ("<filesystem>", "qualified"),
    # <algorithm>
    "sort": ("<algorithm>", "qualified"),
    "stable_sort": ("<algorithm>", "qualified"),
    "find": ("<algorithm>", "qualified"),
    "find_if": ("<algorithm>", "qualified"),
    "adjacent_find": ("<algorithm>", "qualified"),
    "max": ("<algorithm>", "qualified"),
    "min": ("<algorithm>", "qualified"),
    "max_element": ("<algorithm>", "qualified"),
    "min_element": ("<algorithm>", "qualified"),
    "clamp": ("<algorithm>", "qualified"),
    "all_of": ("<algorithm>", "qualified"),
    "any_of": ("<algorithm>", "qualified"),
    "none_of": ("<algorithm>", "qualified"),
    "count": ("<algorithm>", "qualified"),
    "count_if": ("<algorithm>", "qualified"),
    # NOTE: bare `std::remove` is deliberately NOT in this table - it is the
    # C file-delete `std::remove(const char*)` from <cstdio> just as often as
    # the <algorithm> range-remove, so it cannot be mechanically attributed to
    # one header. `remove_if` is unambiguous and stays.
    "remove_if": ("<algorithm>", "qualified"),
    "transform": ("<algorithm>", "qualified"),
    "copy": ("<algorithm>", "qualified"),
    "copy_if": ("<algorithm>", "qualified"),
    "fill": ("<algorithm>", "qualified"),
    "reverse": ("<algorithm>", "qualified"),
    "unique": ("<algorithm>", "qualified"),
    "lower_bound": ("<algorithm>", "qualified"),
    "upper_bound": ("<algorithm>", "qualified"),
    "search": ("<algorithm>", "qualified"),
    "rotate": ("<algorithm>", "qualified"),
    "partition": ("<algorithm>", "qualified"),
    "nth_element": ("<algorithm>", "qualified"),
    "shuffle": ("<algorithm>", "qualified"),
    "swap_ranges": ("<algorithm>", "qualified"),
    # <numeric>
    "accumulate": ("<numeric>", "qualified"),
    "iota": ("<numeric>", "qualified"),
    # <sstream>
    "ostringstream": ("<sstream>", "qualified"),
    "istringstream": ("<sstream>", "qualified"),
    "stringstream": ("<sstream>", "qualified"),
    # <functional>
    "function": ("<functional>", "qualified"),
    "bind": ("<functional>", "qualified"),
    # <memory>
    "unique_ptr": ("<memory>", "qualified"),
    "shared_ptr": ("<memory>", "qualified"),
    # <array>
    "array": ("<array>", "qualified"),
    # <cctype>
    "isalpha": ("<cctype>", "bare"),
    "isdigit": ("<cctype>", "bare"),
    "isspace": ("<cctype>", "bare"),
    "toupper": ("<cctype>", "bare"),
    "tolower": ("<cctype>", "bare"),
    # <cstring>
    "memcpy": ("<cstring>", "bare"),
    "memset": ("<cstring>", "bare"),
    "strlen": ("<cstring>", "bare"),
    "strcmp": ("<cstring>", "bare"),
    # <cstdint>  (only the pointer-width names; uint32_t etc. ride the
    # prerequisites headers - see the alias table)
    "uintptr_t": ("<cstdint>", "bare"),
    "intptr_t": ("<cstdint>", "bare"),
}

# Alias table: an umbrella/project header that GUARANTEES a set of standard
# headers, so a file including it is treated as covered for those. Kept
# MINIMAL and explicit - every entry is a header this repo genuinely routes a
# standard header through. Spelled the way the file includes it (the include
# scan normalises quotes/angles away, so bare relative path).
#   optr.h - the engine-wide smart-pointer alias (`optr` = std::shared_ptr);
#            its own translation includes <memory>, so a file that pulls
#            optr.h for the alias is covered for <memory>.
HEADER_ALIASES = {
    "core_util/optr.h": {"<memory>"},
}

SUPPRESS = "portability-ok:"


def strip_comments(text):
    """Remove // and /* */ comments and string/char literal CONTENTS,
    preserving line structure so line numbers stay accurate. (The containment
    lint's stripper preserves string contents; here we must also blank string
    contents so a banned word or std symbol inside a literal never matches.)"""
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
            if c == "\\" and nxt:
                i += 2
                continue
            if c == in_string:
                in_string = None
                out.append(c)
            elif c == "\n":
                out.append(c)  # keep line structure for unterminated cases
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
            continue
        out.append(c)
        i += 1
    return "".join(out)


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*(<[^>]+>|"[^"]+")')


def included_headers(raw_lines):
    """The set of headers a file includes directly, both `<...>` angle form
    (kept WITH angle brackets to match the STD_SYMBOLS header spelling) and
    `"..."` quote form (kept as the bare path for the alias table)."""
    angle = set()
    quote = set()
    for line in raw_lines:
        m = INCLUDE_RE.match(line)
        if not m:
            continue
        token = m.group(1)
        if token.startswith("<"):
            angle.add(token)
        else:
            quote.add(token.strip('"'))
    return angle, quote


def has_suppression(raw_lines, lineno):
    """True if the finding line or the line above carries the inline
    `// portability-ok:` marker (1-based lineno)."""
    for probe in (lineno, lineno - 1):
        if 1 <= probe <= len(raw_lines) and SUPPRESS in raw_lines[probe - 1]:
            return True
    return False


def compile_patterns():
    banned = {tok: re.compile(rf"\b{re.escape(tok)}\b")
              for tok in BANNED_IDENTIFIERS}
    std = {}
    for sym, (_, mode) in STD_SYMBOLS.items():
        if mode == "qualified":
            std[sym] = re.compile(rf"\bstd::{re.escape(sym)}\b")
        else:  # bare: the token itself, qualified or not
            std[sym] = re.compile(rf"\b{re.escape(sym)}\b")
    return banned, std


def covered_headers(angle, quote):
    """The set of angle-bracket standard headers a file is credited with,
    folding in the alias table for any umbrella header it includes."""
    covered = set(angle)
    for q in quote:
        covered |= HEADER_ALIASES.get(q, set())
    # an alias header can also be pulled in angle form in odd cases
    for a in angle:
        covered |= HEADER_ALIASES.get(a.strip("<>"), set())
    return covered


def check_source(rel, raw, banned_re, std_re):
    """Return a list of violation strings for one file's text."""
    violations = []
    raw_lines = raw.splitlines()
    code = strip_comments(raw)
    code_lines = code.splitlines()

    # Check A: banned Windows-macro identifiers (bare token in stripped code).
    for lineno, line in enumerate(code_lines, 1):
        for tok, pat in banned_re.items():
            if pat.search(line) and not has_suppression(raw_lines, lineno):
                violations.append(
                    f"{rel}:{lineno}: identifier `{tok}` collides with a "
                    f"Windows macro - {BANNED_IDENTIFIERS[tok]} (rename it)")

    # Check B: std symbols must have their header (or an alias) included.
    angle, quote = included_headers(raw_lines)
    covered = covered_headers(angle, quote)
    for sym, (header, _mode) in STD_SYMBOLS.items():
        if header in covered:
            continue
        pat = std_re[sym]
        first = None
        for lineno, line in enumerate(code_lines, 1):
            if pat.search(line):
                first = lineno
                break
        if first is None:
            continue
        if has_suppression(raw_lines, first):
            continue
        violations.append(
            f"{rel}:{first}: uses `{sym}` but does not include {header} "
            f"directly - libc++ leaks it transitively, MSVC does not "
            f"(add #include {header})")
    return violations


def iter_sources():
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
            yield rel, path


def run_lint():
    banned_re, std_re = compile_patterns()
    violations = []
    for rel, path in iter_sources():
        raw = path.read_text(errors="replace")
        violations.extend(check_source(str(rel), raw, banned_re, std_re))

    if violations:
        print(f"portability lint FAILED ({len(violations)} problem(s)):")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print("portability lint OK: no Windows-macro identifiers, "
          "no undeclared std-header leaks")
    return 0


# --------------------------------------------------------------------------
# Self-test: run both checks over embedded fixtures - each hazard class is
# caught, each legit pattern passes, suppression works. Mirrors the
# make_sprite_atlas.py --selftest pattern.
# --------------------------------------------------------------------------
SELFTEST_CASES = [
    # (name, source, expected_count, substrings)  - every substring must
    # appear somewhere in the joined findings; count is the exact finding
    # total. count 0 means "clean".
    ("banned_near_variable",
     "void f() {\n  int near = 3;\n  near++;\n}\n",
     2, ["identifier `near`"]),
    ("banned_far_member",
     "struct P { int far; };\n",
     1, ["identifier `far`"]),
    ("banned_small_and_interface",
     "int small = 1;\nvoid interface() {}\n",
     2, ["identifier `small`", "identifier `interface`"]),
    ("banned_word_boundary_ok",
     "int nearest = 1; float farRect = 2.0f; int smallFont = 3;\n"
     "struct Foo { int interfaces; };\n",
     0, []),
    ("banned_inside_string_ok",
     'const char * s = "near far small interface";\n',
     0, []),
    ("banned_inside_comment_ok",
     "// near far small interface are Windows macros\nint ok = 1;\n",
     0, []),
    ("banned_suppressed",
     "int near = 1; // portability-ok: matches an external C API field\n",
     0, []),
    ("std_adjacent_find_missing",
     '#include <vector>\nvoid f() { std::adjacent_find(a, b); }\n',
     1, ["uses `adjacent_find`", "<algorithm>"]),
    ("std_adjacent_find_included",
     '#include <algorithm>\nvoid f() { std::adjacent_find(a, b); }\n',
     0, []),
    ("std_ostringstream_missing",
     'void f() { std::ostringstream os; }\n',
     1, ["uses `ostringstream`", "<sstream>"]),
    ("std_memcpy_bare_missing",
     'void f() { memcpy(a, b, n); }\n',
     1, ["uses `memcpy`", "<cstring>"]),
    ("std_memcpy_included",
     '#include <cstring>\nvoid f() { memcpy(a, b, n); }\n',
     0, []),
    ("std_bare_array_not_flagged",
     'void f() { int array = 3; float count = count_val; }\n',
     0, []),
    ("std_qualified_array_missing",
     'void f() { std::array<int, 3> a; }\n',
     1, ["uses `array`", "<array>"]),
    ("std_shared_ptr_via_optr_alias",
     '#include "core_util/optr.h"\nvoid f() { std::shared_ptr<int> p; }\n',
     0, []),
    ("std_shared_ptr_missing",
     'void f() { std::shared_ptr<int> p; }\n',
     1, ["uses `shared_ptr`", "<memory>"]),
    ("std_suppressed",
     '// portability-ok: pulled via the pinned single-header lib\n'
     'void f() { std::sort(a, b); }\n',
     0, []),
    ("std_uintptr_missing",
     'void f() { uintptr_t p = 0; }\n',
     1, ["uses `uintptr_t`", "<cstdint>"]),
    ("clean_file",
     '#include <algorithm>\n#include <sstream>\n'
     'void f() { std::sort(a, b); std::ostringstream os; }\n',
     0, []),
]


def run_selftest():
    banned_re, std_re = compile_patterns()
    failures = []
    for name, source, expected_count, substrings in SELFTEST_CASES:
        findings = check_source(f"fixture/{name}.cpp", source,
                                banned_re, std_re)
        blob = "\n".join(findings)
        rendered = "\n".join(f"    {f}" for f in findings) or "    (none)"
        if len(findings) != expected_count:
            failures.append(
                f"{name}: expected {expected_count} finding(s), got "
                f"{len(findings)}:\n{rendered}")
        for needle in substrings:
            if needle not in blob:
                failures.append(
                    f"{name}: expected a finding containing {needle!r}, "
                    f"got:\n{rendered}")

    if failures:
        print(f"portability lint SELFTEST FAILED ({len(failures)} case(s)):")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print(f"portability lint selftest OK: {len(SELFTEST_CASES)} cases")
    return 0


def main(argv):
    if "--selftest" in argv[1:]:
        return run_selftest()
    return run_lint()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
