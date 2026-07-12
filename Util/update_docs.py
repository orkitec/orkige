#!/usr/bin/env python3
# update_docs.py - regenerate the machine-extractable sections of the docs.
#
# The Lua API reference (Docs/lua-api.md) and the gui widget tree (Docs/gui.md)
# carry GENERATED blocks that are derived from the binding sources, so they never
# drift from the code by hand. This script owns those blocks.
#
#   update_docs.py --write        regenerate every generated block in place
#   update_docs.py --check        regenerate to memory and diff; nonzero + a
#                                  report if any committed block is stale
#   update_docs.py --oui <file>   print a mermaid widget tree for one .oui layout
#   update_docs.py --selftest     parse a known .oui and assert the rendered tree
#
# The generated blocks are fenced by HTML-comment markers:
#     <!-- GENERATED:<id> - edit Util/update_docs.py / lua_api_annotations.json -->
#     ... machine-written, do not hand-edit ...
#     <!-- /GENERATED:<id> -->
#
# WHAT IS EXTRACTED vs. HAND-MAINTAINED
# The C++ registration macros carry the Lua SYMBOL names and their receivers, but
# not the Lua argument names or return types (a lambda/macro can't). So this
# script extracts the symbol inventory mechanically and enriches each symbol with
# a signature + one-line purpose from the hand-maintained sidecar
# Util/lua_api_annotations.json (keyed by the canonical symbol id). Editing that
# JSON - never the generated text - is how a signature is corrected. A symbol with
# no annotation renders with a '(...)' placeholder and is reported by --check; a
# stale annotation (no matching symbol) is reported too.

import json
import os
import re
import sys
import io

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)

SCRIPT_COMPONENT = os.path.join(
    ROOT, "orkige_engine", "engine_gocomponent", "ScriptComponent.cpp")
CORE_MODULE = os.path.join(ROOT, "orkige_core", "core_module", "module.cpp")
ENGINE_MODULE = os.path.join(ROOT, "orkige_engine", "engine_module", "module.cpp")
GUI_DIR = os.path.join(ROOT, "orkige_engine", "engine_gui")
ANNOTATIONS = os.path.join(SCRIPT_DIR, "lua_api_annotations.json")

LUA_API_DOC = os.path.join(ROOT, "Docs", "lua-api.md")
GUI_DOC = os.path.join(ROOT, "Docs", "gui.md")
GENERATED_HEADER = os.path.join(ROOT, "tools", "editor", "GeneratedLuaApi.h")

# ---------------------------------------------------------------------------
# the global-table order the index presents (the script-author cheat sheet).
# Tables discovered in ScriptComponent.cpp but not listed here still render,
# appended after these in discovery order.
INDEX_TABLE_ORDER = [
    "world", "screen", "sound", "music", "tween", "guitween", "screens",
    "haptics", "cvar", "save", "events",
]
# the value / singleton usertypes that belong in the compact TOP index (the
# rest of the usertypes go to the fuller Type reference block lower down).
INDEX_TYPES = [
    "Vector3", "Vector2", "Quaternion",
    "InputActions", "TweenHandle", "EventSubscription", "SafeAreaInsets", "RayHit",
]
# core-plumbing usertypes a game SCRIPT never drives (serialization, the event
# bus, the base Object/Interface, the classic debug console): registered for the
# type system but not part of the scripting surface, so the reference omits them.
EXCLUDE_TYPES = {
    "TypeInfo", "Interface", "ISerializeable", "IArchive", "Object",
    "ObjectAttributeHolder", "XMLArchive", "Event", "EventType",
    "EventManager", "GlobalEventManager",
    "GameObjectComponent", "IngameConsole",
}


# ---------------------------------------------------------------------------
# annotation sidecar
# ---------------------------------------------------------------------------
def load_annotations():
    with open(ANNOTATIONS, "r") as f:
        data = json.load(f)
    return {k: v for k, v in data.items() if not k.startswith("_")}


# ---------------------------------------------------------------------------
# a bound Lua symbol
# ---------------------------------------------------------------------------
class Symbol:
    # kind: tablefn | global | ctor | method | static | field | enum
    def __init__(self, kind, receiver, name, comment="", enum_values=None):
        self.kind = kind
        self.receiver = receiver      # table name or Lua type name ("" for global)
        self.name = name
        self.comment = comment        # inline // comment from the source, if any
        self.enum_values = enum_values or []

    def key(self):
        if self.kind == "global":
            return self.name
        if self.kind == "ctor":
            return self.receiver
        if self.kind == "tablefn":
            return "%s.%s" % (self.receiver, self.name)
        if self.kind == "method":
            return "%s:%s" % (self.receiver, self.name)
        # static | field | enum
        return "%s.%s" % (self.receiver, self.name)

    def render(self, annotations):
        ann = annotations.get(self.key(), {})
        sig = ann.get("sig")
        doc = ann.get("doc") or clean_comment(self.comment)
        if self.kind == "ctor":
            head = "%s%s" % (self.receiver, sig if sig is not None else "(...)")
        elif self.kind == "global":
            head = "%s%s" % (self.name, sig if sig is not None else "(...)")
        elif self.kind == "tablefn":
            head = "%s.%s%s" % (self.receiver, self.name,
                                sig if sig is not None else "(...)")
        elif self.kind == "method":
            head = "%s:%s%s" % (self.receiver, self.name,
                                sig if sig is not None else "(...)")
        elif self.kind == "field":
            head = "%s.%s" % (self.receiver, self.name)
        elif self.kind == "enum":
            head = "%s.%s = { %s }" % (self.receiver, self.name,
                                       ", ".join(self.enum_values))
        else:  # static
            head = "%s.%s%s" % (self.receiver, self.name,
                                sig if sig is not None else "(...)")
        if doc:
            return "%s  -- %s" % (head, doc)
        return head

    def wants_annotation(self):
        # fields/enums document themselves adequately from the name; functions
        # and constructors are the ones whose signature must be curated
        return self.kind in ("tablefn", "global", "ctor", "method", "static")


def clean_comment(comment):
    if not comment:
        return ""
    # collapse the "fnname(args) - purpose" inline style down to the purpose,
    # falling back to the whole comment when there is no dash separator
    text = comment.strip()
    m = re.match(r'^[A-Za-z_][\w]*\s*\([^)]*\)\s*[-:]\s*(.*)$', text)
    if m:
        text = m.group(1).strip()
    return re.sub(r'\s+', ' ', text)


# ---------------------------------------------------------------------------
# ScriptComponent.cpp - the global tables + globals
# ---------------------------------------------------------------------------
def parse_script_component(text):
    """Return an ordered dict: table name -> [Symbol(tablefn)], plus a
    'globals' list under the key '' for registerGlobalFunction."""
    tables = {}
    order = []
    for m in re.finditer(
            r'registerFunction\(\s*"([^"]+)"\s*,\s*"([^"]+)"', text):
        table, fn = m.group(1), m.group(2)
        if table not in tables:
            tables[table] = []
            order.append(table)
        tables[table].append(Symbol("tablefn", table, fn))
    globals_ = []
    for m in re.finditer(r'registerGlobalFunction\(\s*"([^"]+)"', text):
        globals_.append(Symbol("global", "", m.group(1)))
    return order, tables, globals_


# ---------------------------------------------------------------------------
# module.cpp - OSIMPLEEXPORT blocks + OEXPORT references
# ---------------------------------------------------------------------------
MEMBER_RE = re.compile(
    r'^\s*(OFUNCWEAK|OFUNCIR|OFUNCCR|OFUNCR|OFUNC_REN|OFUNC|OCONSTVAR|'
    r'OSTATICVAR|OVAR|OPROPERTY_REF|OPROPERTY_RO|OPROPERTY_ENUM|'
    r'OPROPERTY_META|OPROPERTY|OSINGLETON|OCONSTRUCTOR([0-9])|'
    r'OENUM_START|OENUM_VALUE|OENUM_END)\b(.*)$')


def strip_inline_comment(rest):
    m = re.search(r'//\s*(.*)$', rest)
    return m.group(1).strip() if m else ""


def parse_member_line(type_name, line, enum_ctx):
    """Parse one macro line inside a usertype block. enum_ctx is a mutable
    [name, values] holder for an open OENUM. Returns a Symbol or None."""
    m = MEMBER_RE.match(line)
    if not m:
        return None
    macro = m.group(1)
    rest = m.group(3) if m.group(3) is not None else ""
    comment = strip_inline_comment(rest)

    if macro == "OENUM_START":
        nm = re.search(r'\(\s*([A-Za-z_]\w*)\s*\)', rest)
        enum_ctx[0] = nm.group(1) if nm else "Enum"
        enum_ctx[1] = []
        return None
    if macro == "OENUM_VALUE":
        nm = re.search(r'\(\s*([A-Za-z_]\w*)\s*\)', rest)
        if nm:
            enum_ctx[1].append(nm.group(1))
        return None
    if macro == "OENUM_END":
        sym = Symbol("enum", type_name, enum_ctx[0], enum_values=list(enum_ctx[1]))
        enum_ctx[0] = None
        enum_ctx[1] = []
        return sym
    if macro == "OSINGLETON":
        return Symbol("static", type_name, "getSingleton", comment)
    if macro.startswith("OCONSTRUCTOR"):
        # a ctor is folded into the type header (kind ctor keyed by type name);
        # emit at most one, the generator dedupes by key
        return Symbol("ctor", type_name, type_name, comment)
    # functions / properties / vars -> a member name from the first ( ... ) arg
    nm = re.search(r'\(\s*"?([A-Za-z_]\w*)"?', rest)
    if not nm:
        return None
    member = nm.group(1)
    if macro == "OFUNC_REN":
        # OFUNC_REN(cppName, luaName) - the Lua name is the second argument
        nm2 = re.search(r'\(\s*[A-Za-z_]\w*\s*,\s*([A-Za-z_]\w*)', rest)
        if nm2:
            member = nm2.group(1)
        return Symbol("method", type_name, member, comment)
    if macro.startswith("OFUNC"):
        return Symbol("method", type_name, member, comment)
    if macro in ("OVAR", "OCONSTVAR", "OSTATICVAR"):
        return Symbol("field", type_name, member, comment)
    if macro.startswith("OPROPERTY"):
        # OPROPERTY* names are quoted string properties -> Lua fields
        return Symbol("field", type_name, member, comment)
    return None


def parse_usertype_block(type_name, lines):
    symbols = []
    seen_ctor = False
    enum_ctx = [None, []]
    for line in lines:
        sym = parse_member_line(type_name, line, enum_ctx)
        if sym is None:
            continue
        if sym.kind == "ctor":
            if seen_ctor:
                continue
            seen_ctor = True
        symbols.append(sym)
    return symbols


def parse_simple_exports(text):
    """OSIMPLEEXPORT(Cxx, LuaName) ... OSIMPLEEXPORT_END and the _BASED form."""
    types = {}
    order = []
    pattern = re.compile(
        r'OSIMPLEEXPORT(?:_BASED)?\s*\(([^)]*)\)(.*?)OSIMPLEEXPORT_END',
        re.DOTALL)
    for m in pattern.finditer(text):
        args = [a.strip() for a in m.group(1).split(",")]
        lua_name = args[-1]           # last arg is always the Lua export name
        body = m.group(2)
        syms = parse_usertype_block(lua_name, body.splitlines())
        types[lua_name] = syms
        order.append(lua_name)
    return order, types


IMPL_RE_TMPL = (
    r'(?:OOBJECT_IMPL|OABSTRACT_IMPL|OINTERFACE_IMPL|OVIRTUAL_OBJECT_IMPL)'
    r'\(\s*%s\s*\)(.*?)OOBJECT_END')


def find_object_impl(class_name, source_index):
    """Locate class_name's OOBJECT_IMPL/OABSTRACT_IMPL block anywhere in the
    indexed sources; return its member Symbols (Lua type name == class_name)."""
    rx = re.compile(IMPL_RE_TMPL % re.escape(class_name), re.DOTALL)
    for text in source_index:
        m = rx.search(text)
        if m:
            return parse_usertype_block(class_name, m.group(1).splitlines())
    return None


def parse_oexports(text):
    """OEXPORT(Name) references; only simple identifiers are resolvable."""
    names = []
    for m in re.finditer(r'OEXPORT\(\s*([A-Za-z_]\w*)\s*\)', text):
        names.append(m.group(1))
    return names


def build_source_index():
    """Every .cpp that could carry an OOBJECT_IMPL block (for OEXPORT resolve)."""
    texts = []
    for base in (os.path.join(ROOT, "orkige_engine"),
                 os.path.join(ROOT, "orkige_core")):
        for dirpath, _dirs, files in os.walk(base):
            for fn in files:
                if fn.endswith(".cpp"):
                    try:
                        with open(os.path.join(dirpath, fn), "r",
                                  errors="ignore") as f:
                            texts.append(f.read())
                    except OSError:
                        pass
    return texts


# ---------------------------------------------------------------------------
# full model assembly
# ---------------------------------------------------------------------------
class ApiModel:
    def __init__(self):
        self.table_order = []
        self.tables = {}          # name -> [Symbol]
        self.globals = []         # [Symbol]
        self.type_order = []
        self.types = {}           # Lua name -> [Symbol]


def build_model():
    model = ApiModel()
    with open(SCRIPT_COMPONENT, "r") as f:
        sc = f.read()
    model.table_order, model.tables, model.globals = parse_script_component(sc)

    source_index = build_source_index()
    for module_path in (CORE_MODULE, ENGINE_MODULE):
        with open(module_path, "r") as f:
            mt = f.read()
        order, types = parse_simple_exports(mt)
        for name in order:
            if name not in model.types:
                model.type_order.append(name)
            model.types[name] = types[name]
        for cls in parse_oexports(mt):
            if cls in model.types:
                continue
            syms = find_object_impl(cls, source_index)
            if syms is None:
                continue      # container/template/internal export - not usertyped
            model.types[cls] = syms
            model.type_order.append(cls)
    return model


# ---------------------------------------------------------------------------
# rendering the Lua API index (TOP, compact) and the type reference (below)
# ---------------------------------------------------------------------------
def render_index(model, annotations):
    out = []
    out.append("```text")
    out.append("# GLOBAL TABLES  (reach any object by id; ? = may be nil)")

    ordered = [t for t in INDEX_TABLE_ORDER if t in model.tables]
    ordered += [t for t in model.table_order if t not in INDEX_TABLE_ORDER]
    for table in ordered:
        out.append("")
        out.append("## %s" % table)
        for sym in model.tables[table]:
            out.append(sym.render(annotations))
    if model.globals:
        out.append("")
        out.append("## globals")
        for sym in model.globals:
            out.append(sym.render(annotations))

    out.append("")
    out.append("# CORE TYPES  (Type(ctor); Type:method; Type.field)")
    for tname in INDEX_TYPES:
        if tname not in model.types:
            continue
        out.append("")
        out.append("## %s" % tname)
        out.extend(render_type_lines(tname, model.types[tname], annotations))
    out.append("```")
    return "\n".join(out)


def render_type_lines(tname, symbols, annotations):
    lines = []
    ctor = next((s for s in symbols if s.kind == "ctor"), None)
    if ctor:
        lines.append(ctor.render(annotations))
    statics = [s for s in symbols if s.kind == "static"]
    methods = [s for s in symbols if s.kind == "method"]
    fields = [s for s in symbols if s.kind == "field"]
    enums = [s for s in symbols if s.kind == "enum"]
    for s in statics + methods + fields + enums:
        lines.append(s.render(annotations))
    return lines


def render_type_reference(model, annotations):
    out = []
    out.append("```text")
    top = set(INDEX_TYPES)
    first = True
    for tname in model.type_order:
        if tname in top or tname in EXCLUDE_TYPES:
            continue
        syms = model.types.get(tname)
        if not syms:
            continue
        if not first:
            out.append("")
        first = False
        out.append("## %s" % tname)
        out.extend(render_type_lines(tname, syms, annotations))
    out.append("```")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# gui widget class hierarchy (mermaid) from the engine_gui headers
# ---------------------------------------------------------------------------
def parse_gui_hierarchy():
    """Return (edges, briefs): edges list of (base, derived); briefs name->tag."""
    edges = []
    briefs = {}
    class_re = re.compile(
        r'class\s+(?:ORKIGE_ENGINE_DLL\s+)?([A-Za-z_]\w*)\s*:\s*public\s+'
        r'([A-Za-z_]\w*)')
    for fn in sorted(os.listdir(GUI_DIR)):
        if not fn.endswith(".h"):
            continue
        with open(os.path.join(GUI_DIR, fn), "r", errors="ignore") as f:
            text = f.read()
        for m in class_re.finditer(text):
            derived, base = m.group(1), m.group(2)
            if not derived.startswith("Gui") and derived != "GuiWidget":
                # only the widget family (Gui*) and its IGuiObject root
                if derived not in ("GuiWidget",):
                    continue
            edges.append((base, derived))
            briefs[derived] = extract_brief(text, derived)
    return edges, briefs


def extract_brief(text, class_name):
    """Short capability tag: the @brief (or the comment line) above the class."""
    idx = text.find("class ")
    # find the specific class decl
    decl = re.search(r'class\s+(?:ORKIGE_ENGINE_DLL\s+)?%s\b'
                     % re.escape(class_name), text)
    if not decl:
        return ""
    head = text[:decl.start()]
    # last doxygen @brief before the decl
    briefs = re.findall(r'@brief\s+(.+)', head)
    tag = briefs[-1].strip() if briefs else ""
    if not tag:
        # fall back to the last // comment line just above
        lines = [l.strip().lstrip("/!*").strip()
                 for l in head.splitlines() if l.strip().startswith("//")]
        tag = lines[-1] if lines else ""
    tag = re.sub(r'\s+', ' ', tag)
    tag = tag.split(". ")[0].split(" - ")[0].rstrip(".")
    if len(tag) > 52:
        tag = tag[:49].rstrip() + "..."
    return tag


def render_gui_mermaid():
    edges, briefs = parse_gui_hierarchy()
    # keep only edges reachable in the Gui* family, rooted at IGuiObject
    keep = [(b, d) for (b, d) in edges
            if d.startswith("Gui") and (b == "IGuiObject" or b.startswith("Gui"))]
    keep.sort()
    out = ["```mermaid", "graph TD"]
    seen_nodes = set()

    def node(name):
        if name not in seen_nodes:
            seen_nodes.add(name)
            tag = briefs.get(name, "")
            if tag:
                out.append('    %s["%s<br/><i>%s</i>"]' % (name, name, tag))
            else:
                out.append('    %s["%s"]' % (name, name))

    for base, derived in keep:
        node(base)
        node(derived)
    for base, derived in keep:
        out.append("    %s --> %s" % (base, derived))
    out.append("```")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# .oui -> mermaid widget tree (the ad-hoc mode + selftest)
# ---------------------------------------------------------------------------
def parse_oui(text):
    """Return list of (index, type, id, parent, modal). Order preserved."""
    widgets = []
    cur = None
    idx = 0
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r'^\[(\w+)\s+([\w.\-]+)\]$', line)
        if m:
            cur = {"index": idx, "type": m.group(1), "id": m.group(2),
                   "parent": None, "modal": None}
            widgets.append(cur)
            idx += 1
            continue
        m = re.match(r'^\[(\w+)\]$', line)   # a section like [Layout] - not a widget
        if m:
            cur = None
            continue
        if cur is None:
            continue
        kv = re.match(r'^(\w+)\s*=\s*(.*)$', line)
        if kv:
            key, val = kv.group(1), kv.group(2).strip()
            if key == "parent":
                cur["parent"] = val
            elif key == "modal":
                cur["modal"] = val
    return widgets


def oui_to_mermaid(text):
    widgets = parse_oui(text)
    ids = {w["id"] for w in widgets}
    out = ["```mermaid", "graph TD"]
    for w in widgets:
        label = "%s<br/><i>%s</i>" % (w["id"], w["type"])
        out.append('    %s["%s"]' % (w["id"], label))
    for w in widgets:
        parent = w["parent"]
        if parent and parent in ids:
            out.append("    %s --> %s" % (parent, w["id"]))
        elif w["modal"]:
            out.append('    modal_%s(["modal: %s"]) --> %s'
                       % (w["modal"], w["modal"], w["id"]))
        else:
            out.append("    root([root]) --> %s" % w["id"])
    out.append("```")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# generated-block substitution
# ---------------------------------------------------------------------------
def block_markers(block_id):
    begin = ("<!-- GENERATED:%s - edit Util/update_docs.py / "
             "lua_api_annotations.json; do not hand-edit -->" % block_id)
    end = "<!-- /GENERATED:%s -->" % block_id
    return begin, end


def replace_block(text, block_id, new_body):
    begin, end = block_markers(block_id)
    pattern = re.compile(
        re.escape(begin) + r'.*?' + re.escape(end), re.DOTALL)
    replacement = "%s\n%s\n%s" % (begin, new_body, end)
    if not pattern.search(text):
        raise SystemExit(
            "update_docs: marker for block '%s' not found in the doc" % block_id)
    return pattern.sub(lambda _m: replacement, text)


# ---------------------------------------------------------------------------
# generated header (for the MCP get_lua_api verb)
# ---------------------------------------------------------------------------
def render_generated_header(index_text):
    guard = "__GeneratedLuaApi_h__"
    body = []
    body.append("// GENERATED by Util/update_docs.py - do not hand-edit.")
    body.append("// The Lua API signature index (Docs/lua-api.md TOP block),")
    body.append("// embedded so the editor's MCP get_lua_api verb is self-")
    body.append("// contained. Regenerate with: python3 Util/update_docs.py --write")
    body.append("#ifndef %s" % guard)
    body.append("#define %s" % guard)
    body.append("")
    body.append("namespace Orkige")
    body.append("{")
    body.append("\t//! the generated Lua API signature index (raw text)")
    body.append("\tinline const char* const kGeneratedLuaApiIndex =")
    for line in index_text.splitlines():
        body.append("\t\t%s" % c_string_literal(line + "\n"))
    body.append("\t\t;")
    body.append("}")
    body.append("")
    body.append("#endif // %s" % guard)
    body.append("")
    return "\n".join(body)


def c_string_literal(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    s = s.replace("\n", "\\n").replace("\t", "\\t")
    return '"%s"' % s


# ---------------------------------------------------------------------------
# targets: each is (path, regenerator producing full-file content)
# ---------------------------------------------------------------------------
def compute_outputs():
    annotations = load_annotations()
    model = build_model()
    index_text = render_index(model, annotations)
    ref_text = render_type_reference(model, annotations)
    gui_tree = render_gui_mermaid()

    outputs = {}   # path -> new content

    with open(LUA_API_DOC, "r") as f:
        lua_doc = f.read()
    lua_doc = replace_block(lua_doc, "lua-api-index", index_text)
    lua_doc = replace_block(lua_doc, "lua-api-types", ref_text)
    outputs[LUA_API_DOC] = lua_doc

    with open(GUI_DOC, "r") as f:
        gui_doc = f.read()
    gui_doc = replace_block(gui_doc, "gui-widget-tree", gui_tree)
    outputs[GUI_DOC] = gui_doc

    outputs[GENERATED_HEADER] = render_generated_header(index_text)

    drift = annotation_drift(model, annotations)
    return outputs, drift


def annotation_drift(model, annotations):
    """Symbols that want a signature but have none, and annotations that match
    no discovered symbol. Advisory (not a hard failure) - drift to act on."""
    keys = set()
    missing = []
    for table in model.table_order:
        for s in model.tables[table]:
            keys.add(s.key())
            if s.wants_annotation() and s.key() not in annotations:
                missing.append(s.key())
    for s in model.globals:
        keys.add(s.key())
        if s.wants_annotation() and s.key() not in annotations:
            missing.append(s.key())
    for tname in model.type_order:
        for s in model.types[tname]:
            keys.add(s.key())
            if tname in INDEX_TYPES and s.wants_annotation() \
                    and s.key() not in annotations:
                missing.append(s.key())
    stale = [k for k in annotations if k not in keys]
    return missing, stale


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def cmd_write():
    outputs, drift = compute_outputs()
    for path, content in outputs.items():
        with open(path, "w") as f:
            f.write(content)
        print("wrote %s" % os.path.relpath(path, ROOT))
    report_drift(drift)
    return 0


def cmd_check():
    outputs, drift = compute_outputs()
    stale = []
    for path, content in outputs.items():
        with open(path, "r") as f:
            current = f.read()
        if current != content:
            stale.append(os.path.relpath(path, ROOT))
    report_drift(drift)
    if stale:
        sys.stderr.write(
            "\nDOC CURRENCY: these generated files are STALE:\n")
        for p in stale:
            sys.stderr.write("  - %s\n" % p)
        sys.stderr.write(
            "Run:  python3 Util/update_docs.py --write   (then commit)\n")
        return 1
    print("docs are current (%d generated files checked)" % len(outputs))
    return 0


def report_drift(drift):
    missing, stale = drift
    if missing:
        sys.stderr.write(
            "note: %d bound symbol(s) lack a signature annotation "
            "(rendered with '(...)'):\n" % len(missing))
        for k in sorted(set(missing)):
            sys.stderr.write("      %s\n" % k)
        sys.stderr.write(
            "      add them to Util/lua_api_annotations.json\n")
    if stale:
        sys.stderr.write(
            "note: %d annotation(s) match no bound symbol (stale):\n"
            % len(stale))
        for k in sorted(stale):
            sys.stderr.write("      %s\n" % k)


def cmd_oui(path):
    with open(path, "r") as f:
        text = f.read()
    print(oui_to_mermaid(text))
    return 0


SELFTEST_OUI = """
[Layout]
atlas = gui_default

[DecorWidget panel]
sprite = panel

[Label title]
parent = panel
text = Hi

[Modal confirm]
scrim = 0 0 0 0.5

[Button yes]
modal = confirm
text = Yes
"""


def cmd_selftest():
    # (1) .oui -> mermaid tree shape
    tree = oui_to_mermaid(SELFTEST_OUI)
    assert "panel --> title" in tree, tree
    assert 'modal_confirm(["modal: confirm"]) --> yes' in tree, tree
    assert "root([root]) --> panel" in tree, tree
    # (2) the model parses the real binding sources into a non-empty API
    annotations = load_annotations()
    model = build_model()
    assert "world" in model.tables, "world table not discovered"
    assert any(s.name == "get" for s in model.tables["world"])
    assert any(s.name == "play" for s in model.tables.get("music", []))
    assert "Vector3" in model.types, "Vector3 usertype not discovered"
    index_text = render_index(model, annotations)
    assert "music.play(id, file [, loop]) -> bool" in index_text, index_text
    assert "world.get(id) -> GameObject?" in index_text
    # (3) index size budget (agent one-shot ingest): a single comfortable read.
    # Grows as the scripting surface does (the `events` message bus added its
    # table + the EventSubscription handle; the `locale` table added its four
    # entries); kept well inside one context read.
    size = len(index_text.encode("utf-8"))
    assert size < 8700, "index is %d bytes, over the budget" % size
    # (4) gui hierarchy tree includes the root chain
    gui_tree = render_gui_mermaid()
    assert "IGuiObject --> GuiWidget" in gui_tree, gui_tree
    assert "GuiWidget --> GuiButton" in gui_tree, gui_tree
    print("update_docs selftest OK (index %d bytes, under budget)" % size)
    return 0


def main(argv):
    if "--write" in argv:
        return cmd_write()
    if "--check" in argv:
        return cmd_check()
    if "--selftest" in argv:
        return cmd_selftest()
    if "--oui" in argv:
        i = argv.index("--oui")
        if i + 1 >= len(argv):
            sys.stderr.write("--oui needs a file path\n")
            return 2
        return cmd_oui(argv[i + 1])
    sys.stderr.write(__doc__ or "")
    sys.stderr.write("\nusage: update_docs.py --write | --check | "
                     "--oui <file> | --selftest\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
