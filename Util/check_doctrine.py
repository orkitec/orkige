#!/usr/bin/env python3
"""Doctrine lint - four source-hygiene gates the project brief mandates but
that until now lived only in prose. Each is a sub-check with its own scope and
its own suppression section in Util/doctrine_lint.json; all four run from the
ONE ctest `doctrine_lint` (LABELS unit -> runs in the unit AND desktop
presets). Modelled on Util/check_ogre_containment.py: a pure source walk,
file:line reporting, and a data-file of sanctioned exceptions so a new offence
fails the build while the existing backlog stays visible as data.

Sub-checks (each individually suppressible; `--only <name>` runs one):

  meta_macro   - raw `#ifdef/#ifndef/#if/#elif ... ORKIGE_LUA/ORKIGE_NOSCRIPT`
                 and `defined(ORKIGE_LUA/ORKIGE_NOSCRIPT)` are forbidden
                 OUTSIDE the meta seam (Meta.h/Meta_Lua.h/Meta_None.h + the
                 ScriptRuntime implementation). The macro vocabulary is
                 complete in both backends by design; application code selects
                 a backend in Meta.h, never with its own #ifdef. The tree is
                 clean today - this locks it.

  banned_terms - competing game-engine and third-party product names must not
                 appear in code COMMENTS, string LITERALS or Docs/ prose
                 (identifiers are not scanned). The one sanctioned place for
                 the names is this lint's own term table in doctrine_lint.json.
                 Vendored third-party files and the factual DevicePreset device
                 table are exempt (encoded in the config).

  filesystem   - raw filesystem access (`fopen`, std::ifstream/ofstream/fstream,
                 std::filesystem) in the CORE + ENGINE runtime is forbidden:
                 the ratified funnel routes content reads through
                 core_filesystem/ResourceReader so a mounted APK/pak resolves
                 in place with no extraction. Migration is incomplete, so the
                 lint GATES: the funnel plumbing is allowed, every current
                 offender sits in the `legacy` backlog (visible data), and a
                 NEW raw-fs call in a not-listed file fails. Editor/tools/tests
                 are out of the runtime funnel's scope and not scanned here.

  copyright    - every .h/.cpp/.mm in the CORE + ENGINE libraries carries the
                 standard header block (the notice line + `copyright: (c)
                 2009-2026 orkitec`). Vendored third-party files carry their own
                 licence and are sanctioned by name. tools/ and tests/ never
                 adopted a per-file header block (a separate convention question
                 for the owner) and are not scanned here - see the report.

Stdlib-only and headless (obeys the python_stdlib policy). A `--selftest` runs
embedded fixtures (a passing tree, a violating tree and a suppressed tree) for
every sub-check and touches no real source.
"""

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = Path(__file__).resolve().parent / "doctrine_lint.json"

SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cxx", ".mm", ".m"}
# build trees (editor compile-on-Play, exporter) live inside the scan roots
SKIP_DIR_NAMES = {"build", "build-export", ".git"}

CFAMILY = {".h", ".hpp", ".cpp", ".cxx", ".mm", ".m"}


# --------------------------------------------------------------------------- #
# shared source walking + masking                                             #
# --------------------------------------------------------------------------- #
def iter_source_files(root, suffixes):
    """Yield (relpath, Path) for every file with a wanted suffix under root,
    skipping build/VCS trees. Deterministic order."""
    root_path = REPO_ROOT / root
    if not root_path.is_dir():
        return
    for path in sorted(root_path.rglob("*")):
        if path.suffix not in suffixes or not path.is_file():
            continue
        rel = path.relative_to(REPO_ROOT)
        if any(part in SKIP_DIR_NAMES for part in rel.parts):
            continue
        yield rel, path


def strip_comments_cfamily(text):
    """Blank out // and /* */ comments (keep string literals + code), preserving
    every newline so line numbers survive. The inverse of mask_prose."""
    out = []
    i, n = 0, len(text)
    in_line = in_block = False
    in_string = None
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if in_line:
            if c == "\n":
                in_line = False
                out.append(c)
            else:
                out.append(" ")
            i += 1
            continue
        if in_block:
            if c == "*" and nxt == "/":
                in_block = False
                out.append("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " ")
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
            out.append("  ")
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block = True
            out.append("  ")
            i += 2
            continue
        if c in "\"'":
            in_string = c
        out.append(c)
        i += 1
    return "".join(out)


def mask_prose(text, suffix):
    """Return a same-length string in which ONLY comment text and string-literal
    content is visible (everything else blanked to spaces, newlines kept), so a
    term search hits prose but never an identifier. Markdown is all prose."""
    if suffix == ".md":
        return text
    if suffix == ".py":
        return _mask_prose_generic(text, line_comment="#",
                                   triples=('"""', "'''"), quotes="\"'")
    if suffix == ".lua":
        return _mask_lua(text)
    if suffix in CFAMILY:
        return _mask_cfamily(text)
    return text


def _emit_blank(out, c):
    out.append("\n" if c == "\n" else " ")


def _mask_cfamily(text):
    out = []
    i, n = 0, len(text)
    in_line = in_block = False
    in_string = None
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if in_line:
            if c == "\n":
                in_line = False
                out.append(c)
            else:
                out.append(c)
            i += 1
            continue
        if in_block:
            if c == "*" and nxt == "/":
                in_block = False
                out.append("  ")
                i += 2
                continue
            out.append(c if c != "\n" else "\n")
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
            out.append("  ")
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block = True
            out.append("  ")
            i += 2
            continue
        if c in "\"'":
            in_string = c
            out.append(" ")
            i += 1
            continue
        _emit_blank(out, c)
        i += 1
    return "".join(out)


def _mask_prose_generic(text, line_comment, triples, quotes):
    out = []
    i, n = 0, len(text)
    in_line = False
    in_string = None  # a quote char
    in_triple = None  # a triple string, e.g. '"""'
    while i < n:
        c = text[i]
        three = text[i:i + 3]
        if in_line:
            if c == "\n":
                in_line = False
                out.append(c)
            else:
                out.append(c)
            i += 1
            continue
        if in_triple:
            if three == in_triple:
                out.append("   ")
                in_triple = None
                i += 3
                continue
            out.append(c)
            i += 1
            continue
        if in_string:
            if c == "\\" and i + 1 < n:
                out.append("  ")
                i += 2
                continue
            out.append(c)
            if c == in_string:
                in_string = None
            i += 1
            continue
        if c == line_comment:
            in_line = True
            out.append(" ")
            i += 1
            continue
        if three in triples:
            in_triple = three
            out.append("   ")
            i += 3
            continue
        if c in quotes:
            in_string = c
            out.append(" ")
            i += 1
            continue
        _emit_blank(out, c)
        i += 1
    return "".join(out)


def _mask_lua(text):
    out = []
    i, n = 0, len(text)
    in_line = False
    in_string = None
    in_long = False  # inside [[ ... ]] (level-0 long bracket)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if in_line:
            if c == "\n":
                in_line = False
                out.append(c)
            else:
                out.append(c)
            i += 1
            continue
        if in_long:
            if two == "]]":
                out.append("  ")
                in_long = False
                i += 2
                continue
            out.append(c)
            i += 1
            continue
        if in_string:
            if c == "\\" and i + 1 < n:
                out.append("  ")
                i += 2
                continue
            out.append(c)
            if c == in_string:
                in_string = None
            i += 1
            continue
        if two == "--":
            # long comment --[[ ... ]] or line comment
            if text[i + 2:i + 4] == "[[":
                in_long = True
                out.append("    ")
                i += 4
                continue
            in_line = True
            out.append("  ")
            i += 2
            continue
        if two == "[[":
            in_long = True
            out.append("  ")
            i += 2
            continue
        if c in "\"'":
            in_string = c
            out.append(" ")
            i += 1
            continue
        _emit_blank(out, c)
        i += 1
    return "".join(out)


# --------------------------------------------------------------------------- #
# sub-check: meta-macro containment                                           #
# --------------------------------------------------------------------------- #
META_DIRECTIVE = re.compile(
    r"^[ \t]*#[ \t]*(?:ifdef|ifndef|if|elif)\b.*"
    r"(?:ORKIGE_LUA|ORKIGE_NOSCRIPT)"
    r"|defined[ \t]*\([ \t]*(?:ORKIGE_LUA|ORKIGE_NOSCRIPT)")


def check_meta_macro(cfg, violations):
    allowed = {Path(p) for p in cfg["allowed_files"]}
    seen = set()
    for root in cfg["scan_roots"]:
        for rel, path in iter_source_files(root, SOURCE_SUFFIXES):
            if rel in allowed:
                seen.add(rel)
                continue
            code = strip_comments_cfamily(path.read_text(errors="replace"))
            for lineno, line in enumerate(code.splitlines(), 1):
                if META_DIRECTIVE.search(line):
                    violations.append(
                        f"[meta_macro] {rel}:{lineno}: raw ORKIGE_LUA/"
                        "ORKIGE_NOSCRIPT preprocessor use outside the meta seam "
                        "- select the backend in Meta.h; the OMETA/OUSERTYPE "
                        "macro vocabulary is complete in both backends")
    for rel in sorted(allowed - seen):
        violations.append(f"[meta_macro] {rel}: allowed_files entry does not "
                          "exist (or was not scanned) - remove the stale entry")


# --------------------------------------------------------------------------- #
# sub-check: banned third-party product names                                 #
# --------------------------------------------------------------------------- #
def check_banned_terms(cfg, violations):
    exempt = {Path(p) for p in cfg["exempt_files"]}
    patterns = [(entry["pattern"],
                 re.compile(r"\b" + re.escape(entry["pattern"]) + r"\b",
                            re.IGNORECASE),
                 entry.get("reason", ""))
                for entry in cfg["terms"]]
    scanned_exts = {".h", ".hpp", ".cpp", ".cxx", ".mm", ".m", ".py",
                    ".lua", ".md"}
    seen_exempt = set()
    for root in cfg["scan_roots"]:
        for rel, path in iter_source_files(root, scanned_exts):
            if rel in exempt:
                seen_exempt.add(rel)
                continue
            prose = mask_prose(path.read_text(errors="replace"), path.suffix)
            for lineno, line in enumerate(prose.splitlines(), 1):
                for term, rx, reason in patterns:
                    if rx.search(line):
                        why = f" ({reason})" if reason else ""
                        violations.append(
                            f"[banned_terms] {rel}:{lineno}: names the product "
                            f"'{term}'{why} in a comment/string/doc - describe "
                            "the behaviour directly, never a third-party product")
    for rel in sorted(exempt - seen_exempt):
        violations.append(f"[banned_terms] {rel}: exempt_files entry does not "
                          "exist (or was not scanned) - remove the stale entry")


# --------------------------------------------------------------------------- #
# sub-check: filesystem funnel                                                #
# --------------------------------------------------------------------------- #
FS_TOKEN = re.compile(
    r"\bfopen[ \t]*\(|\bfreopen[ \t]*\("
    r"|std::ifstream|std::ofstream|std::fstream"
    r"|std::filesystem")


def check_filesystem_funnel(cfg, violations):
    funnel = {Path(p): reason for p, reason in cfg["funnel_files"].items()}
    legacy = {Path(p): reason for p, reason in cfg["legacy"].items()}
    seen_funnel = set()
    seen_legacy = set()
    for root in cfg["scan_roots"]:
        for rel, path in iter_source_files(root, SOURCE_SUFFIXES):
            code = strip_comments_cfamily(path.read_text(errors="replace"))
            hits = [lineno for lineno, line in enumerate(code.splitlines(), 1)
                    if FS_TOKEN.search(line)]
            if rel in funnel:
                seen_funnel.add(rel)
                if not hits:
                    violations.append(
                        f"[filesystem] {rel}: STALE funnel-plumbing sanction - "
                        "no raw filesystem access left; remove its entry from "
                        "doctrine_lint.json")
                continue
            if rel in legacy:
                seen_legacy.add(rel)
                if not hits:
                    violations.append(
                        f"[filesystem] {rel}: STALE legacy backlog entry - the "
                        "file no longer does raw filesystem access; remove its "
                        "entry from doctrine_lint.json (the backlog shrank)")
                continue
            for lineno in hits:
                violations.append(
                    f"[filesystem] {rel}:{lineno}: raw filesystem access in the "
                    "core/engine runtime - route content reads through "
                    "core_filesystem/ResourceReader (or, for genuine plumbing, "
                    "add a reasoned entry to doctrine_lint.json)")
    for rel in sorted(set(funnel) - seen_funnel):
        violations.append(f"[filesystem] {rel}: funnel-plumbing entry does not "
                          "exist (or was not scanned) - remove the stale entry")
    for rel in sorted(set(legacy) - seen_legacy):
        violations.append(f"[filesystem] {rel}: legacy backlog entry does not "
                          "exist (or was not scanned) - remove the stale entry")


# --------------------------------------------------------------------------- #
# sub-check: copyright header block                                           #
# --------------------------------------------------------------------------- #
COPYRIGHT_NOTICE = "This source file is part of orkige"
COPYRIGHT_LINE = "(c) 2009-2026 orkitec"
HEADER_SCAN_LINES = 25


def _has_header(path):
    head = "\n".join(path.read_text(errors="replace").splitlines()
                     [:HEADER_SCAN_LINES])
    return COPYRIGHT_NOTICE in head and COPYRIGHT_LINE in head


def check_copyright(cfg, violations):
    third_party = {Path(p): reason for p, reason in cfg["third_party"].items()}
    seen_tp = set()
    for root in cfg["scan_roots"]:
        for rel, path in iter_source_files(root, {".h", ".cpp", ".mm"}):
            if rel in third_party:
                seen_tp.add(rel)
                if _has_header(path):
                    violations.append(
                        f"[copyright] {rel}: STALE third-party sanction - the "
                        "file now carries the standard header; remove its entry "
                        "from doctrine_lint.json")
                continue
            if not _has_header(path):
                violations.append(
                    f"[copyright] {rel}: missing the standard header block "
                    "(the '{}' notice + 'copyright: (c) 2009-2026 orkitec') - "
                    "add it verbatim, or sanction a vendored third-party file "
                    "in doctrine_lint.json".format(COPYRIGHT_NOTICE))
    for rel in sorted(set(third_party) - seen_tp):
        violations.append(f"[copyright] {rel}: third_party entry does not exist "
                          "(or was not scanned) - remove the stale entry")


SUBCHECKS = {
    "meta_macro": check_meta_macro,
    "banned_terms": check_banned_terms,
    "filesystem": check_filesystem_funnel,
    "copyright": check_copyright,
}


# --------------------------------------------------------------------------- #
# driver                                                                       #
# --------------------------------------------------------------------------- #
def run(config, only=None):
    violations = []
    for name, fn in SUBCHECKS.items():
        if only and name != only:
            continue
        fn(config[name], violations)
    return violations


def main(argv):
    if "--selftest" in argv:
        return selftest()
    only = None
    if "--only" in argv:
        only = argv[argv.index("--only") + 1]
        if only not in SUBCHECKS:
            print(f"unknown sub-check '{only}'; known: "
                  f"{', '.join(SUBCHECKS)}")
            return 2
    config = json.loads(CONFIG_PATH.read_text())
    violations = run(config, only)
    if violations:
        print(f"doctrine lint FAILED ({len(violations)} problem(s)):")
        for v in violations:
            print(f"  {v}")
        return 1
    scope = only or "all four sub-checks"
    print(f"doctrine lint OK: {scope} clean "
          "(meta-macro seam, no banned product names, filesystem funnel "
          "gate, copyright headers)")
    return 0


# --------------------------------------------------------------------------- #
# self-test: embedded fixtures, no real source touched                        #
# --------------------------------------------------------------------------- #
def _write(tmp, rel, text):
    p = tmp / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


HEADER = ("/****\n\tnotice:\tThis source file is part of orkige\n"
          "\tcopyright:\t(c) 2009-2026 orkitec\n****/\n")


def selftest():
    import tempfile
    global REPO_ROOT
    failures = []

    def expect(label, got, want_count):
        ok = len(got) == want_count
        print(f"  [{'ok' if ok else 'FAIL'}] {label}: "
              f"{len(got)} problem(s), expected {want_count}")
        if not ok:
            for g in got:
                print(f"        {g}")
            failures.append(label)

    with tempfile.TemporaryDirectory() as td:
        saved = REPO_ROOT
        try:
            tmp = Path(td)
            REPO_ROOT = tmp

            # ---- clean tree: every sub-check passes ----------------------
            _write(tmp, "core/Meta.h", HEADER + "#ifdef ORKIGE_LUA\nx\n#endif\n")
            _write(tmp, "core/Good.h",
                   HEADER + "// draws a textured quad\nint good;\n")
            _write(tmp, "core/Good.cpp",
                   HEADER + 'const char* s = "hello world";\n'
                   "int p = 0; // read via ResourceReader\n")
            _write(tmp, "docs/guide.md", "# Guide\nThe engine renders sprites.\n")
            _write(tmp, "core/tool.py",
                   "# a helper\nx = 'just a string'\n")
            cfg = {
                "meta_macro": {"scan_roots": ["core"],
                               "allowed_files": ["core/Meta.h"]},
                "banned_terms": {"scan_roots": ["core", "docs"],
                                 "terms": [{"pattern": "unity",
                                            "reason": "game engine"},
                                           {"pattern": "godot",
                                            "reason": "game engine"}],
                                 "exempt_files": []},
                "filesystem": {"scan_roots": ["core"],
                               "funnel_files": {}, "legacy": {}},
                "copyright": {"scan_roots": ["core"], "third_party": {}},
            }
            expect("clean tree (all sub-checks)", run(cfg), 0)

            # ---- meta_macro: a raw #ifdef outside the seam fails ---------
            _write(tmp, "core/Bad.cpp",
                   HEADER + "#ifdef ORKIGE_LUA\nint z;\n#endif\n")
            expect("meta_macro violation", run(cfg, only="meta_macro"), 1)
            cfg2 = json.loads(json.dumps(cfg))
            cfg2["meta_macro"]["allowed_files"].append("core/Bad.cpp")
            expect("meta_macro suppressed", run(cfg2, only="meta_macro"), 0)
            (tmp / "core/Bad.cpp").unlink()

            # ---- banned_terms: name a product in a comment + a string ----
            # (line 5 = comment hit, line 6 = string hit, line 7 = identifier
            # `unityBuild` which must NOT be flagged)
            _write(tmp, "core/Named.cpp",
                   HEADER + "// ported the look from Unity\n"
                   'const char* e = "feels like Godot";\n'
                   "int unityBuild = 0; // an identifier, not scanned\n")
            vb = run(cfg, only="banned_terms")
            expect("banned_terms two prose hits", vb, 2)
            expect("banned_terms ignores the identifier",
                   [x for x in vb if "unityBuild" in x
                    or x.rstrip().endswith(":7: names the product 'unity'")], 0)
            cfg3 = json.loads(json.dumps(cfg))
            cfg3["banned_terms"]["exempt_files"].append("core/Named.cpp")
            expect("banned_terms suppressed", run(cfg3, only="banned_terms"), 0)
            (tmp / "core/Named.cpp").unlink()

            # ---- filesystem: a raw fopen fails, legacy suppresses --------
            _write(tmp, "core/Raw.cpp",
                   HEADER + 'void f(){ FILE* h = fopen("a", "r"); }\n')
            expect("filesystem violation", run(cfg, only="filesystem"), 1)
            cfg4 = json.loads(json.dumps(cfg))
            cfg4["filesystem"]["legacy"]["core/Raw.cpp"] = "legacy pre-funnel"
            expect("filesystem legacy-suppressed",
                   run(cfg4, only="filesystem"), 0)
            cfg5 = json.loads(json.dumps(cfg))
            cfg5["filesystem"]["legacy"]["core/Good.h"] = "legacy pre-funnel"
            expect("filesystem stale-legacy detected",
                   [x for x in run(cfg5, only="filesystem") if "STALE" in x], 1)
            (tmp / "core/Raw.cpp").unlink()

            # ---- copyright: a headerless file fails, sanction suppresses -
            _write(tmp, "core/NoHead.cpp", "int noheader = 1;\n")
            expect("copyright violation",
                   [x for x in run(cfg, only="copyright") if "NoHead" in x], 1)
            cfg6 = json.loads(json.dumps(cfg))
            cfg6["copyright"]["third_party"]["core/NoHead.cpp"] = "vendored xyz"
            expect("copyright third-party-suppressed",
                   run(cfg6, only="copyright"), 0)
            _write(tmp, "core/NoHead.cpp", HEADER + "int nowheadered = 1;\n")
            expect("copyright stale-third-party detected",
                   [x for x in run(cfg6, only="copyright") if "STALE" in x], 1)
            (tmp / "core/NoHead.cpp").unlink()

        finally:
            REPO_ROOT = saved

    if failures:
        print(f"doctrine lint selftest FAILED: {', '.join(failures)}")
        return 1
    print("doctrine lint selftest OK: every sub-check passes on a clean tree, "
          "flags its violation and honours its suppression")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
