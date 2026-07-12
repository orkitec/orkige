#!/usr/bin/env python3
# orkige_loc.py - the localisation extraction / merge / check / pseudo tool.
#
# A project's translatable strings live in an XLIFF 1.2 registry under its
# loc/ directory: loc/en.xlf is the source-language registry (the authoring
# surface for source text) and loc/<lang>.xlf is one file per target language
# (the surface translators round-trip through a CAT tool). This tool keeps that
# registry in sync with the code:
#
#   orkige_loc.py --write  <project>   scan the project for translatable keys
#                                       (.oui @keys + Lua loc() calls), refresh
#                                       loc/en.xlf and every target file
#   orkige_loc.py --write  <project> --pseudo   ...and refresh the en-XA pseudo
#   orkige_loc.py --pseudo <project>   regenerate loc/en-XA.xlf from loc/en.xlf
#   orkige_loc.py --check  <project>   the gate: exit non-zero listing missing /
#                                       orphaned / stale keys and placeholder
#                                       parity violations (the loc_currency ctest)
#   orkige_loc.py --selftest           run the self-contained correctness suite
#
# Keys come from two sources of truth, parsed grammar-aware (never a blind
# regex over a file):
#   * .oui declarative UI (the GuiLayout grammar): a `text = @key` entry, or a
#     `@`-prefixed part of a pipe-separated `items = ...` list. These are the
#     ONLY two .oui properties GuiFactory resolves through the string table, so
#     a stray `@` in any other property can not false-positive.
#   * Lua under scripts/: a `loc("key")` / `loc('key', ...)` call with a string
#     literal first argument. Dynamic keys (loc(someVar)) are invisible by
#     nature - the runtime's once-per-key miss log is the net for those.
#
# Placeholders: `%%N%%` is the authoring/runtime syntax everywhere (scripts and
# .oui). In the XLIFF each `%%N%%` is carried as an inline `<x id="N"
# ctype="x-orkige-arg"/>` code (the professionally correct representation a CAT
# tool protects and tag-parity-QAs); the boundary conversion here is total and
# lossless. `%%N%%` is the only syntax an author or the runtime ever sees.
#
# Stdlib only (policy): xml.etree.ElementTree parses, a hand-written emitter
# controls the canonical document shape byte-for-byte (so --write is idempotent).

import argparse
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------------------
# document contract
# ---------------------------------------------------------------------------
XLIFF_NS = "urn:oasis:names:tc:xliff:document:1.2"
SOURCE_LANGUAGE = "en"          # the registry's source language
ORIGINAL = "orkige-strings"     # 1.2 requires file/@original; the loader ignores it
DATATYPE = "plaintext"
TOOL_ID = "orkige_loc"
PSEUDO_LANGUAGE = "en-XA"       # the committed pseudo-locale (BCP-47 private use)

# the .oui properties whose value GuiFactory resolves through the string table
# (GuiFactory::resolveText is called on exactly these); `items` is a
# pipe-separated list, each part resolved individually.
OUI_TEXT_KEY = "text"
OUI_ITEMS_KEY = "items"

# a `%%N%%` positional placeholder in authored text
PLACEHOLDER_RE = re.compile(r"%%(\d+)%%")
# a Lua loc() call with a string-literal first argument (single OR double quoted,
# with backslash escapes tolerated inside the literal)
LOC_CALL_RE = re.compile(
    r"""\bloc\s*\(\s*(['"])((?:\\.|(?!\1).)*)\1""")


# ---------------------------------------------------------------------------
# small XML helpers (tinyxml2-style: match by local name, not the prefix)
# ---------------------------------------------------------------------------
def localname(tag):
    return tag.rsplit("}", 1)[-1] if "}" in tag else tag


def xml_text_escape(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def xml_attr_escape(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def placeholder_ids(text):
    """the set of positional placeholder ids present in a %%N%% text."""
    return set(PLACEHOLDER_RE.findall(text))


# ---------------------------------------------------------------------------
# inline-code boundary: <x id="N"/> in the file  <->  %%N%% in memory
# ---------------------------------------------------------------------------
def reconstruct_inline(elem, warnings):
    """Walk a <source>/<target> element's children in order and rebuild the
    authored text: text nodes verbatim, <x id="N"/> back to %%N%%. Any other
    inline element (not emitted by us) contributes its text content and a
    once-per-run warning."""
    parts = [elem.text or ""]
    for child in elem:
        name = localname(child.tag)
        if name == "x":
            parts.append("%%%%%s%%%%" % (child.get("id", "")))
        else:
            warnings.add(name)
            parts.append(child.text or "")
            for sub in child:
                parts.append(sub.text or "")
        parts.append(child.tail or "")
    return "".join(parts)


def emit_inline(text):
    """authored %%N%% text -> escaped XML content with inline <x/> codes."""
    out = []
    for piece in re.split(r"(%%\d+%%)", text):
        m = re.fullmatch(r"%%(\d+)%%", piece)
        if m:
            out.append('<x id="%s" ctype="x-orkige-arg"/>' % m.group(1))
        elif piece:
            out.append(xml_text_escape(piece))
    return "".join(out)


# ---------------------------------------------------------------------------
# the in-memory model
# ---------------------------------------------------------------------------
class Unit:
    def __init__(self, key):
        self.key = key
        self.source = ""            # authored source text (%%N%% syntax)
        self.target = None          # translated text, or None (source-only)
        self.state = None           # target/@state, or None
        self.note = None            # developer note text, or None
        self.locations = []         # [(sourcefile, linenumber)] - en.xlf only


class Doc:
    def __init__(self, source_language=SOURCE_LANGUAGE, target_language=None):
        self.source_language = source_language
        self.target_language = target_language   # None => source registry
        self.units = []                          # ordered [Unit]

    def index(self):
        return {u.key: u for u in self.units}

    def keys(self):
        return [u.key for u in self.units]


# ---------------------------------------------------------------------------
# parse an .xlf into a Doc (returns (doc, duplicate_keys) or (None, error))
# ---------------------------------------------------------------------------
def parse_xliff(path, warnings):
    try:
        tree = ET.parse(path)
    except (ET.ParseError, OSError) as exc:
        return None, "%s: %s" % (os.path.basename(path), exc)
    root = tree.getroot()
    if localname(root.tag) != "xliff":
        return None, "%s: root element is not <xliff>" % os.path.basename(path)
    file_el = next((c for c in root if localname(c.tag) == "file"), None)
    if file_el is None:
        return None, "%s: no <file> element" % os.path.basename(path)
    body = next((c for c in file_el if localname(c.tag) == "body"), None)
    if body is None:
        return None, "%s: no <body> element" % os.path.basename(path)

    doc = Doc(file_el.get("source-language", SOURCE_LANGUAGE),
              file_el.get("target-language"))
    seen = set()
    duplicates = []
    for tu in body:
        if localname(tu.tag) != "trans-unit":
            continue
        key = tu.get("resname") or tu.get("id")
        if not key:
            continue
        if key in seen:
            duplicates.append(key)
            continue
        seen.add(key)
        unit = Unit(key)
        for child in tu:
            name = localname(child.tag)
            if name == "source":
                unit.source = reconstruct_inline(child, warnings)
            elif name == "target":
                unit.target = reconstruct_inline(child, warnings)
                unit.state = child.get("state")
            elif name == "note":
                unit.note = (child.text or "").strip()
            elif name == "context-group":
                sf = ln = None
                for ctx in child:
                    if localname(ctx.tag) != "context":
                        continue
                    ct = ctx.get("context-type")
                    if ct == "sourcefile":
                        sf = (ctx.text or "").strip()
                    elif ct == "linenumber":
                        ln = (ctx.text or "").strip()
                if sf is not None:
                    unit.locations.append((sf, ln or ""))
        doc.units.append(unit)
    return doc, duplicates


# ---------------------------------------------------------------------------
# emit a Doc as canonical XLIFF text (deterministic; --write is idempotent)
# ---------------------------------------------------------------------------
def emit_xliff(doc):
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>')
    out.append('<xliff version="1.2" xmlns="%s">' % XLIFF_NS)
    attrs = ['original="%s"' % ORIGINAL,
             'source-language="%s"' % doc.source_language]
    if doc.target_language:
        attrs.append('target-language="%s"' % doc.target_language)
    attrs.append('datatype="%s"' % DATATYPE)
    out.append("  <file %s>" % " ".join(attrs))
    out.append("    <header>")
    out.append('      <tool tool-id="%s" tool-name="%s" tool-version="1"/>'
               % (TOOL_ID, TOOL_ID))
    out.append("    </header>")
    out.append("    <body>")
    for unit in doc.units:
        key = xml_attr_escape(unit.key)
        out.append('      <trans-unit id="%s" resname="%s" xml:space="preserve">'
                   % (key, key))
        out.append("        <source>%s</source>" % emit_inline(unit.source))
        if unit.target is not None:
            state = ' state="%s"' % xml_attr_escape(unit.state) \
                if unit.state else ""
            out.append("        <target%s>%s</target>"
                       % (state, emit_inline(unit.target)))
        for sf, ln in unit.locations:
            out.append('        <context-group purpose="location">')
            out.append('          <context context-type="sourcefile">%s</context>'
                       % xml_text_escape(sf))
            if ln != "":
                out.append('          <context context-type="linenumber">'
                           "%s</context>" % xml_text_escape(str(ln)))
            out.append("        </context-group>")
        if unit.note:
            out.append('        <note from="developer">%s</note>'
                       % xml_text_escape(unit.note))
        out.append("      </trans-unit>")
    out.append("    </body>")
    out.append("  </file>")
    out.append("</xliff>")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# extraction: .oui @keys + Lua loc() sites
# ---------------------------------------------------------------------------
def _oui_trim(s):
    return s.strip(" \t\r")


def extract_oui(text):
    """Return [(key, linenumber)] for every @-prefixed translatable value in
    one .oui document, parsed with the GuiLayout line grammar."""
    found = []
    have_section = False
    for i, raw in enumerate(text.splitlines(), start=1):
        line = _oui_trim(raw)
        if not line or line[0] in "#;":
            continue
        if line[0] == "[":
            have_section = "]" in line
            continue
        if not have_section:
            continue
        sep = None
        for ch in "=:\t":
            idx = line.find(ch)
            if idx != -1 and (sep is None or idx < sep):
                sep = idx
        if sep is None:
            continue                 # a bare flag key, no value
        key = _oui_trim(line[:sep])
        value = _oui_trim(line[sep + 1:])
        if key == OUI_TEXT_KEY:
            if value.startswith("@"):
                found.append((value[1:], i))
        elif key == OUI_ITEMS_KEY:
            for part in value.split("|"):
                part = part.strip()
                if part.startswith("@"):
                    found.append((part[1:], i))
    return found


def extract_lua(text):
    """Return [(key, linenumber)] for every loc("literal") call in Lua source."""
    found = []
    for m in LOC_CALL_RE.finditer(text):
        literal = m.group(2)
        # unescape the common Lua string escapes so the key matches the runtime
        key = literal.replace("\\\\", "\\").replace("\\'", "'").replace('\\"', '"')
        line = text.count("\n", 0, m.start()) + 1
        found.append((key, line))
    return found


def extract_project(root):
    """Scan a project root and return {key: sorted [(relfile, line)]} plus the
    stable first-seen order of the keys. loc/ and build outputs are skipped."""
    occurrences = {}

    def note(key, rel, line):
        occurrences.setdefault(key, []).append((rel, line))

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames
                       if d not in ("loc", "build", "builds", ".git")]
        rel_dir = os.path.relpath(dirpath, root)
        under_scripts = rel_dir == "scripts" or rel_dir.startswith(
            "scripts" + os.sep)
        for fn in sorted(filenames):
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            try:
                with open(path, "r", encoding="utf-8", errors="ignore") as f:
                    text = f.read()
            except OSError:
                continue
            if fn.endswith(".oui"):
                for key, line in extract_oui(text):
                    note(key, rel, line)
            elif fn.endswith(".lua") and under_scripts:
                for key, line in extract_lua(text):
                    note(key, rel, line)

    for key in occurrences:
        occurrences[key] = sorted(set(occurrences[key]))
    return occurrences


# ---------------------------------------------------------------------------
# merge semantics
# ---------------------------------------------------------------------------
TODO_NOTE = "TODO: source text"
STALE_STATE = "needs-translation"


def merge_en(existing, occurrences):
    """Build the refreshed en.xlf Doc + the list of orphaned keys removed.
    Keys are emitted sorted for a stable file."""
    old = existing.index() if existing else {}
    doc = Doc()
    for key in sorted(occurrences):
        unit = Unit(key)
        prev = old.get(key)
        if prev is not None:
            unit.source = prev.source
            unit.note = prev.note
        if not unit.source and not unit.note:
            unit.note = TODO_NOTE
        unit.locations = [(sf, ln) for sf, ln in occurrences[key]]
        doc.units.append(unit)
    orphans = sorted(k for k in old if k not in occurrences)
    return doc, orphans


def merge_target(existing, en_doc):
    """Refresh one target Doc against the en registry: new keys added
    source-only, source-text edits keep the old target + flag needs-translation,
    orphaned keys deleted. Returns (doc, orphans, added)."""
    old = existing.index() if existing else {}
    doc = Doc(target_language=existing.target_language if existing else None)
    orphans = sorted(k for k in old if k not in en_doc.index())
    added = []
    for en_unit in en_doc.units:
        key = en_unit.key
        unit = Unit(key)
        unit.source = en_unit.source
        prev = old.get(key)
        if prev is None:
            added.append(key)                 # new key: source-only, no target
        else:
            unit.note = prev.note
            if prev.target is not None:
                unit.target = prev.target
                if prev.source == en_unit.source:
                    unit.state = prev.state    # source unchanged: keep as-is
                else:
                    unit.state = STALE_STATE   # source edited: keep stale target
        doc.units.append(unit)
    return doc, orphans, added


# ---------------------------------------------------------------------------
# pseudo-localisation (font-safe, Latin-1 / Latin Extended-A accents only)
# ---------------------------------------------------------------------------
# Every value below is inside U+00A0-U+017F and verified present in the default
# Nunito face, so a pseudo string never renders tofu on the shipped font.
PSEUDO_MAP = {
    "a": "á", "c": "č", "d": "đ", "e": "é", "g": "ğ",
    "h": "ĥ", "i": "í", "j": "ĵ", "k": "ķ", "l": "ļ",
    "n": "ñ", "o": "ó", "r": "ŕ", "s": "š", "t": "ť",
    "u": "ú", "w": "ŵ", "y": "ý", "z": "ž",
    "A": "Á", "C": "Č", "D": "Đ", "E": "É", "G": "Ğ",
    "H": "Ĥ", "I": "Í", "J": "Ĵ", "K": "Ķ", "L": "Ļ",
    "N": "Ñ", "O": "Ó", "R": "Ŕ", "S": "Š", "T": "Ť",
    "U": "Ú", "W": "Ŵ", "Y": "Ý", "Z": "Ž",
}
PSEUDO_OPEN = "«"      # << left guillemet   (U+00AB, Latin-1)
PSEUDO_CLOSE = "»"     # >> right guillemet  (U+00BB, Latin-1)
PSEUDO_PAD = "·"       # middle dot          (U+00B7, Latin-1)
PSEUDO_EXPANSION = 0.35     # ~35% length growth reveals non-elastic layouts


def pseudo_text(source):
    """Accent the letters, expand the length ~35% with middle-dot padding and
    fence with guillemets - passing %%N%% placeholders through untouched so
    tag parity holds by construction."""
    body = []
    for piece in re.split(r"(%%\d+%%)", source):
        if re.fullmatch(r"%%\d+%%", piece):
            body.append(piece)               # placeholder: verbatim
        else:
            body.append("".join(PSEUDO_MAP.get(ch, ch) for ch in piece))
    accented = "".join(body)
    letters = sum(1 for ch in source if ch.isalpha())
    pad = max(1, round(letters * PSEUDO_EXPANSION))
    return "%s%s %s%s" % (PSEUDO_OPEN, accented, PSEUDO_PAD * pad, PSEUDO_CLOSE)


def gen_pseudo(en_doc):
    """Build the en-XA pseudo Doc from the en registry (source-bearing units)."""
    doc = Doc(target_language=PSEUDO_LANGUAGE)
    for en_unit in en_doc.units:
        if not en_unit.source:
            continue
        unit = Unit(en_unit.key)
        unit.source = en_unit.source
        unit.target = pseudo_text(en_unit.source)
        unit.state = "final"
        doc.units.append(unit)
    return doc


# ---------------------------------------------------------------------------
# project file plumbing
# ---------------------------------------------------------------------------
def loc_dir(root):
    return os.path.join(root, "loc")


def en_path(root):
    return os.path.join(loc_dir(root), SOURCE_LANGUAGE + ".xlf")


def target_files(root):
    """every loc/*.xlf except the source registry, sorted by language."""
    d = loc_dir(root)
    if not os.path.isdir(d):
        return []
    out = []
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".xlf") and fn != SOURCE_LANGUAGE + ".xlf":
            out.append(os.path.join(d, fn))
    return out


def language_of(path):
    return os.path.splitext(os.path.basename(path))[0]


def load_doc(path, warnings):
    if not os.path.isfile(path):
        return None, []
    doc, dup = parse_xliff(path, warnings)
    return doc, dup


def write_doc(path, doc):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(emit_xliff(doc))


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------
def cmd_write(root, do_pseudo):
    warnings = set()
    occurrences = extract_project(root)
    en_existing, _ = load_doc(en_path(root), warnings)
    en_doc, en_orphans = merge_en(en_existing, occurrences)
    write_doc(en_path(root), en_doc)
    print("wrote %s (%d keys%s)" % (
        os.path.relpath(en_path(root), root), len(en_doc.units),
        ", %d orphan removed" % len(en_orphans) if en_orphans else ""))
    for orphan in en_orphans:
        print("  removed orphan key: %s" % orphan)

    for path in target_files(root):
        existing, _ = load_doc(path, warnings)
        if existing is not None and existing.target_language is None:
            existing.target_language = language_of(path)
        doc, orphans, added = merge_target(existing, en_doc)
        doc.target_language = language_of(path)
        write_doc(path, doc)
        print("wrote %s (%d keys%s%s)" % (
            os.path.relpath(path, root), len(doc.units),
            ", %d added" % len(added) if added else "",
            ", %d orphan removed" % len(orphans) if orphans else ""))

    if do_pseudo:
        _write_pseudo(root, en_doc)
    _report_inline_warnings(warnings)
    return 0


def _write_pseudo(root, en_doc=None):
    warnings = set()
    if en_doc is None:
        en_doc, _ = load_doc(en_path(root), warnings)
        if en_doc is None:
            sys.stderr.write(
                "orkige_loc: no loc/en.xlf - run --write first\n")
            return 1
    pseudo = gen_pseudo(en_doc)
    path = os.path.join(loc_dir(root), PSEUDO_LANGUAGE + ".xlf")
    write_doc(path, pseudo)
    print("wrote %s (%d keys)" % (os.path.relpath(path, root), len(pseudo.units)))
    return 0


def cmd_pseudo(root):
    return _write_pseudo(root)


def cmd_check(root):
    warnings = set()
    occurrences = extract_project(root)
    problems = []

    en_doc, en_dup = load_doc(en_path(root), warnings)
    if en_doc is None:
        problems.append("loc/en.xlf is missing (run --write to create it)")
        _report(problems, root)
        return 1
    for key in en_dup:
        problems.append("loc/en.xlf: duplicate key '%s'" % key)

    en_index = en_doc.index()
    for key in sorted(occurrences):
        if key not in en_index:
            locs = ", ".join("%s:%s" % (f, l) for f, l in occurrences[key])
            problems.append("key '%s' referenced (%s) but missing from "
                            "loc/en.xlf" % (key, locs))
    for unit in en_doc.units:
        if unit.key not in occurrences:
            problems.append("loc/en.xlf: orphan key '%s' (no longer "
                            "referenced)" % unit.key)
        if not unit.source.strip():
            problems.append("loc/en.xlf: key '%s' has an empty <source>"
                            % unit.key)

    for path in target_files(root):
        rel = os.path.relpath(path, root)
        doc, dup = load_doc(path, warnings)
        if doc is None:
            problems.append("%s: malformed XLIFF" % rel)
            continue
        for key in dup:
            problems.append("%s: duplicate key '%s'" % (rel, key))
        tindex = doc.index()
        for key in en_index:
            if key not in tindex:
                problems.append("%s: missing key '%s' (out of sync with "
                                "en.xlf)" % (rel, key))
        for unit in doc.units:
            en_unit = en_index.get(unit.key)
            if en_unit is None:
                problems.append("%s: orphan key '%s' (not in en.xlf)"
                                % (rel, unit.key))
                continue
            if unit.source != en_unit.source:
                problems.append("%s: key '%s' has a stale <source> (differs "
                                "from en.xlf - run --write)" % (rel, unit.key))
            if unit.target is not None:
                if placeholder_ids(unit.target) != placeholder_ids(en_unit.source):
                    problems.append("%s: key '%s' placeholder parity violation "
                                    "(target codes %s != source codes %s)" % (
                                        rel, unit.key,
                                        sorted(placeholder_ids(unit.target)),
                                        sorted(placeholder_ids(en_unit.source))))

    _report_inline_warnings(warnings)
    _report(problems, root)
    return 1 if problems else 0


def _report(problems, root):
    if not problems:
        print("localisation is current (loc/ in sync with the project)")
        return
    sys.stderr.write("\nLOCALISATION CURRENCY: %d problem(s):\n" % len(problems))
    for p in problems:
        sys.stderr.write("  - %s\n" % p)
    sys.stderr.write(
        "Fix with:  python3 Util/orkige_loc.py --write %s\n" % root)


def _report_inline_warnings(warnings):
    if warnings:
        sys.stderr.write(
            "note: unexpected inline element(s) in an .xlf (took text content): "
            "%s\n" % ", ".join(sorted(warnings)))


# ---------------------------------------------------------------------------
# self-test (self-contained; the orkige_export.py --selftest precedent)
# ---------------------------------------------------------------------------
def selftest():
    # (1) placeholder boundary is total and lossless, both directions
    assert emit_inline("Score: %%0%%") == \
        'Score: <x id="0" ctype="x-orkige-arg"/>', "emit inline"
    assert emit_inline("A < B & C") == "A &lt; B &amp; C", "text escaping"
    warned = set()
    xml_src = ('<source xmlns="%s">Score: <x id="0" ctype="x-orkige-arg"/> of '
               '<x id="1"/></source>' % XLIFF_NS)
    text = reconstruct_inline(ET.fromstring(xml_src), warned)
    assert text == "Score: %%0%% of %%1%%", text
    assert placeholder_ids(text) == {"0", "1"}, "placeholder ids"
    # reordered inline codes reconstruct to reordered %%N%% (word order differs)
    xml_rev = ('<target xmlns="%s"><x id="1"/> then <x id="0"/></target>'
               % XLIFF_NS)
    assert reconstruct_inline(ET.fromstring(xml_rev), warned) == \
        "%%1%% then %%0%%", "reordered codes"

    # (2) .oui extraction is grammar-aware: only text= and items= @values, never
    # a stray @ in another property or a commented line
    oui = (
        "# a comment @not.a.key\n"
        "[Layout]\n"
        "atlas = gui_default\n"
        "\n"
        "[Label a]\n"
        "text = @menu.play\n"
        "sprite = @not.text.prop\n"        # not a text property -> ignored
        "[DropDown d]\n"
        "text = Plain literal\n"           # literal, no @ -> ignored
        "items = @opt.one | Plain | @opt.two\n"
        "; text = @also.comment\n")        # comment line -> ignored
    keys = dict((k, ln) for k, ln in extract_oui(oui))
    assert set(keys) == {"menu.play", "opt.one", "opt.two"}, keys
    assert keys["menu.play"] == 6, keys                # 1-based line number

    # (3) Lua extraction: literal first args only, both quote styles
    lua = ("local t = loc('hud.score', v)\n"
           'print(loc("menu.quit"))\n'
           "local x = loc(dynamicKey)\n")             # dynamic -> invisible
    lkeys = dict((k, ln) for k, ln in extract_lua(lua))
    assert set(lkeys) == {"hud.score", "menu.quit"}, lkeys
    assert lkeys["hud.score"] == 1 and lkeys["menu.quit"] == 2, lkeys

    # (4) merge semantics + --check verdicts, end to end in a temp project
    with tempfile.TemporaryDirectory() as root:
        media = os.path.join(root, "media")
        scripts = os.path.join(root, "scripts")
        os.makedirs(media)
        os.makedirs(scripts)
        with open(os.path.join(media, "s.oui"), "w") as f:
            f.write("[Label a]\ntext = @menu.play\n"
                    "[Label b]\ntext = @hud.score\n")
        with open(os.path.join(scripts, "g.lua"), "w") as f:
            f.write("loc('menu.quit')\n")
        # a pre-existing target with a stale translation for a key whose source
        # we are about to change, plus an orphan key to be pruned
        os.makedirs(loc_dir(root))
        en0 = Doc()
        for k, s in (("menu.play", "Play"), ("hud.score", "Score: %%0%%"),
                     ("gone.key", "Obsolete")):
            u = Unit(k)
            u.source = s
            en0.units.append(u)
        write_doc(en_path(root), en0)
        de = Doc(target_language="de")
        for k, s, t in (("menu.play", "Play", "Spielen"),
                        ("hud.score", "Points: %%0%%", "Punkte: %%0%%"),
                        ("gone.key", "Obsolete", "Veraltet")):
            u = Unit(k)
            u.source = s
            u.target = t
            u.state = "translated"
            de.units.append(u)
        write_doc(os.path.join(loc_dir(root), "de.xlf"), de)

        assert cmd_write(root, do_pseudo=True) == 0

        w = set()
        en_doc, _ = load_doc(en_path(root), w)
        ei = en_doc.index()
        # NEW key referenced only in Lua was added source-only + TODO note
        assert "menu.quit" in ei
        assert ei["menu.quit"].note == TODO_NOTE, "new key TODO note"
        assert ei["menu.quit"].source == "", "new key has empty source"
        # a pre-existing authored source is preserved verbatim across a merge
        assert ei["menu.play"].source == "Play", "authored source preserved"
        # ORPHAN key vanished from sources -> deleted from en AND target
        assert "gone.key" not in ei, "orphan pruned from en"
        # locations recorded from the .oui occurrence
        assert ei["menu.play"].locations, "location recorded"
        assert ei["menu.play"].locations[0][0] == "media/s.oui", \
            ei["menu.play"].locations

        de_doc, _ = load_doc(os.path.join(loc_dir(root), "de.xlf"), w)
        di = de_doc.index()
        assert "gone.key" not in di, "orphan pruned from target"
        # SOURCE edited (hud.score en source 'Score: %%0%%' vs target's stale
        # 'Points: %%0%%') keeps the old target + flags needs-translation
        assert di["hud.score"].target == "Punkte: %%0%%", "stale target kept"
        assert di["hud.score"].state == STALE_STATE, "source edit flags state"
        # unchanged source keeps the translated state
        assert di["menu.play"].state == "translated", "unchanged keeps state"
        # the newly-referenced key mirrored in source-only (no target)
        assert di["menu.quit"].target is None, "new target key source-only"

        # pseudo file: accented, guarded, expanded, placeholder-preserving
        px, _ = load_doc(os.path.join(loc_dir(root), PSEUDO_LANGUAGE + ".xlf"), w)
        pi = px.index()
        pt = pi["hud.score"].target
        assert pt.startswith(PSEUDO_OPEN) and pt.endswith(PSEUDO_CLOSE), pt
        assert "%%0%%" in pt, "placeholder preserved in pseudo"
        assert placeholder_ids(pt) == placeholder_ids(ei["hud.score"].source)
        assert len(pt) > len(ei["hud.score"].source) * 1.3, "pseudo expands"

        # author the new key's source (the developer fills the TODO) so the
        # tree is fully in sync, then re-write to canonicalise
        en_doc.index()["menu.quit"].source = "Quit"
        en_doc.index()["menu.quit"].note = None
        write_doc(en_path(root), en_doc)
        assert cmd_write(root, do_pseudo=True) == 0

        # --write is idempotent (byte-for-byte on a second pass)
        before = {}
        for fn in sorted(os.listdir(loc_dir(root))):
            with open(os.path.join(loc_dir(root), fn)) as f:
                before[fn] = f.read()
        assert cmd_write(root, do_pseudo=True) == 0
        for fn, content in before.items():
            with open(os.path.join(loc_dir(root), fn)) as f:
                assert f.read() == content, "non-idempotent write: %s" % fn

        # --check is green on the freshly-written, in-sync tree
        assert cmd_check(root) == 0, "check should pass after write"

        # inject a placeholder-parity break and a missing-key drift, re-check
        de_doc, _ = load_doc(os.path.join(loc_dir(root), "de.xlf"), w)
        de_doc.index()["hud.score"].target = "Punkte ohne code"   # dropped %%0%%
        write_doc(os.path.join(loc_dir(root), "de.xlf"), de_doc)
        with open(os.path.join(media, "s.oui"), "a") as f:
            f.write("[Label c]\ntext = @brand.new\n")             # not in en.xlf
        assert cmd_check(root) == 1, "check should fail on drift"

    # (5) the pseudo accent map stays inside Latin-1 / Latin Extended-A
    for ch in PSEUDO_MAP.values():
        assert 0x00A0 <= ord(ch) <= 0x017F, "pseudo char out of font-safe range"
    for ch in (PSEUDO_OPEN, PSEUDO_CLOSE, PSEUDO_PAD):
        assert 0x00A0 <= ord(ch) <= 0x00FF, "pseudo guard/pad not in Latin-1"

    print("orkige_loc: selftest OK")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main(argv):
    parser = argparse.ArgumentParser(
        description="Orkige localisation extract / merge / check / pseudo tool.")
    parser.add_argument("project", nargs="?",
                        help="the project root (holds loc/ and the scanned "
                             ".oui / scripts)")
    parser.add_argument("--write", action="store_true",
                        help="extract keys and refresh loc/en.xlf + targets")
    parser.add_argument("--check", action="store_true",
                        help="the gate: fail on drift / parity violations")
    parser.add_argument("--pseudo", action="store_true",
                        help="(re)generate the en-XA pseudo-locale")
    parser.add_argument("--selftest", action="store_true",
                        help="run the self-contained correctness suite")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()
    if not (args.write or args.check or args.pseudo):
        parser.error("one of --write / --check / --pseudo / --selftest is required")
    if not args.project:
        parser.error("a project root is required")
    root = os.path.abspath(args.project)
    if not os.path.isdir(root):
        parser.error("project root not found: %s" % args.project)

    if args.check:
        return cmd_check(root)
    if args.write:
        return cmd_write(root, do_pseudo=args.pseudo)
    return cmd_pseudo(root)          # --pseudo alone


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
