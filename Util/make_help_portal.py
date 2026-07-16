#!/usr/bin/env python3
# make_help_portal.py - assemble the offline, searchable help portal from the
# documentation corpus.
#
# The editor's Help > "Orkige Help" serves the output of this script on a
# loopback HTTP port. The corpus is the repository's committed prose exactly
# as written (this script PRESENTS the docs, it never rewrites them):
#
#   README.md            -> the Overview page
#   Docs/*.md            -> one page each (incl. their GENERATED blocks)
#   projects/*/README.md -> one page each (none exist yet; picked up when
#                           a project documents itself)
#
#   make_help_portal.py --output <dir>              build the site
#   make_help_portal.py --output <dir> --if-stale   rebuild only when a source
#                                                   (or this script) changed -
#                                                   a sha256 stamp, the
#                                                   update_docs --check idea
#   make_help_portal.py --selftest                  render a synthetic corpus
#                                                   AND the real one, assert
#                                                   pages/index/links/stamp
#
# The site is fully self-contained (hand-written help.css + help.js, a
# generated search-index.json - no vendored libraries, no network). Python has
# no stdlib markdown library, so rendering is a markdown-SUBSET renderer
# tailored to what the corpus actually uses (audited): ATX headings, nested
# unordered/ordered lists with continuation lines, fenced code blocks
# (including fences indented inside list items; mermaid renders as a plain
# code block), pipe tables with \| cell escapes, horizontal rules, inline
# code/bold/italic/links/images. Internal links between corpus pages are
# rewritten .md -> .html and VERIFIED (a broken page or anchor target fails
# the build, named as file:line so docs authors can act on it); links to
# repository files outside the corpus degrade to inline code and external
# images to their alt text - the portal works offline.

import argparse
import hashlib
import html
import html.parser
import json
import io
import os
import re
import sys
import tempfile

SCRIPT_PATH = os.path.abspath(__file__)
ROOT = os.path.dirname(os.path.dirname(SCRIPT_PATH))

# preferred reading order for the sidebar; corpus pages not listed here are
# appended alphabetically, so a new doc shows up without touching this script
PREFERRED_ORDER = [
    "getting-started", "lua-api", "gui", "materials", "particles",
    "vector-animation", "localisation", "logging", "benchmark",
    "mcp", "mcp-workflows", "render-abstraction", "web-export",
    "device-session", "ios-signing", "store-release", "ports",
]

GENERATED_NOTE = ("Generated from the repository docs by "
                  "Util/make_help_portal.py - edit the source .md files, "
                  "not this site.")


# ---------------------------------------------------------------------------
# corpus discovery
# ---------------------------------------------------------------------------
class Page:
    def __init__(self, page_id, source, group):
        self.page_id = page_id        # output stem, e.g. "lua-api"
        self.source = source          # repo-relative source path
        self.group = group            # sidebar group name
        self.title = page_id          # replaced by the first H1
        self.html = ""                # rendered article body
        self.toc = []                 # [(anchor, text)] for the h2 rail
        self.anchors = set()          # every heading slug on the page
        self.sections = []            # search records for this page


def discover_corpus(root):
    """The corpus pages in sidebar order: Overview, the Docs guides, the
    project READMEs. Returns ({repo path -> Page}, [Page in order])."""
    pages = []
    readme = os.path.join(root, "README.md")
    if os.path.isfile(readme):
        pages.append(Page("overview", "README.md", "Overview"))
    docs_dir = os.path.join(root, "Docs")
    stems = sorted(os.path.splitext(fn)[0] for fn in os.listdir(docs_dir)
                   if fn.endswith(".md"))
    ordered = [s for s in PREFERRED_ORDER if s in stems]
    ordered += [s for s in stems if s not in PREFERRED_ORDER]
    for stem in ordered:
        pages.append(Page(stem, "Docs/%s.md" % stem, "Guides"))
    projects_dir = os.path.join(root, "projects")
    if os.path.isdir(projects_dir):
        for name in sorted(os.listdir(projects_dir)):
            candidate = os.path.join(projects_dir, name, "README.md")
            if os.path.isfile(candidate):
                pages.append(Page("project-" + name,
                                  "projects/%s/README.md" % name, "Projects"))
    by_source = {p.source: p for p in pages}
    return by_source, pages


# ---------------------------------------------------------------------------
# slugs (the anchor vocabulary internal links target)
# ---------------------------------------------------------------------------
def slugify(heading_text, taken):
    """Heading text -> anchor slug: markdown stripped, lower-cased, spaces to
    dashes, punctuation dropped, duplicates suffixed -1, -2, ..."""
    text = re.sub(r'`([^`]*)`', r'\1', heading_text)
    text = re.sub(r'\*\*([^*]*)\*\*', r'\1', text)
    text = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', text)
    text = text.strip().lower()
    text = re.sub(r'[^\w\s-]', '', text, flags=re.UNICODE)
    # every whitespace character becomes a dash WITHOUT collapsing runs: a
    # heading like "Show / hide transitions" anchors as
    # "show--hide-transitions" (the vocabulary the corpus's own #links use)
    slug = re.sub(r'\s', '-', text)
    if not slug:
        slug = "section"
    base = slug
    counter = 1
    while slug in taken:
        slug = "%s-%d" % (base, counter)
        counter += 1
    taken.add(slug)
    return slug


# ---------------------------------------------------------------------------
# inline rendering
# ---------------------------------------------------------------------------
class LinkIssue:
    def __init__(self, source, line, target, reason):
        self.source = source
        self.line = line
        self.target = target
        self.reason = reason

    def __str__(self):
        return "%s:%d -> %s (%s)" % (self.source, self.line, self.target,
                                     self.reason)


class RenderContext:
    """Everything inline rendering needs: which page it is on (for relative
    link resolution), where links may point (the corpus page map), and the
    collectors for link verification."""

    def __init__(self, page, by_source, root):
        self.page = page
        self.by_source = by_source
        self.root = root
        self.line = 0                 # source line of the block being rendered
        self.pending_links = []       # (line, target_page, fragment) to verify
        self.issues = []              # LinkIssue list

    def resolve_link(self, text_html, target):
        """One [text](target) -> HTML, offline discipline: corpus .md links
        become page links (verified later), repository files degrade to code,
        genuinely missing targets are reported as broken."""
        if re.match(r'^[a-z][a-z0-9+.-]*:', target):  # http:, https:, mailto:
            return '<a class="external" href="%s">%s</a>' % (
                html.escape(target, quote=True), text_html)
        if target.startswith("#"):
            self.pending_links.append((self.line, self.page, target[1:]))
            return '<a href="#%s">%s</a>' % (
                html.escape(target[1:], quote=True), text_html)
        path, _, fragment = target.partition("#")
        source_dir = os.path.dirname(self.page.source)
        repo_path = os.path.normpath(os.path.join(source_dir, path))
        repo_path = repo_path.replace(os.sep, "/")
        if repo_path in self.by_source:
            dest = self.by_source[repo_path]
            if fragment:
                self.pending_links.append((self.line, dest, fragment))
            href = dest.page_id + ".html" + ("#" + fragment if fragment else "")
            return '<a href="%s">%s</a>' % (html.escape(href, quote=True),
                                            text_html)
        if os.path.exists(os.path.join(self.root, repo_path)):
            # a repository file outside the corpus (a header, LICENSE, an
            # asset) - there is no page to link to, so present the reference
            # as code; the prose already names the path
            return "<code>%s</code>" % text_html
        self.issues.append(LinkIssue(self.page.source, self.line, target,
                                     "no such file or corpus page"))
        return text_html


_CODE_SPAN_RE = re.compile(r'(`+)(.+?)\1')
_IMAGE_RE = re.compile(r'!\[([^\]]*)\]\(([^)\s]+)\)')
_LINK_RE = re.compile(r'\[([^\]]+)\]\(([^)\s]+)\)')
_BOLD_RE = re.compile(r'\*\*(.+?)\*\*')
_ITALIC_STAR_RE = re.compile(r'(?<![\w*])\*([^*\s](?:[^*]*[^*\s])?)\*(?![\w*])')
_ITALIC_UNDER_RE = re.compile(r'(?<![\w_])_([^_\s](?:[^_]*[^_\s])?)_(?![\w_])')


def render_inline(text, ctx):
    """Markdown inline -> HTML. Code spans are lifted out first so nothing
    inside them is interpreted; links resolve through the context."""
    spans = []

    def lift(match):
        spans.append("<code>%s</code>" % html.escape(match.group(2).strip()))
        return "\x00%d\x00" % (len(spans) - 1)

    text = _CODE_SPAN_RE.sub(lift, text)
    text = html.escape(text, quote=False)

    # images degrade to their alt text - the portal never fetches remote
    # artwork (README badges) and bundles no image files
    text = _IMAGE_RE.sub(lambda m: html.escape(m.group(1)) if m.group(1)
                         else "", text)

    def link(match):
        inner = _BOLD_RE.sub(r'<strong>\1</strong>', match.group(1))
        return ctx.resolve_link(inner, match.group(2))

    text = _LINK_RE.sub(link, text)
    text = _BOLD_RE.sub(r'<strong>\1</strong>', text)
    text = _ITALIC_STAR_RE.sub(r'<em>\1</em>', text)
    text = _ITALIC_UNDER_RE.sub(r'<em>\1</em>', text)

    def restore(match):
        return spans[int(match.group(1))]

    return re.sub(r'\x00(\d+)\x00', restore, text)


def plain_text(text):
    """Markdown inline -> plain text for the search index."""
    text = _CODE_SPAN_RE.sub(lambda m: m.group(2), text)
    text = _IMAGE_RE.sub(lambda m: m.group(1), text)
    text = _LINK_RE.sub(lambda m: m.group(1), text)
    text = re.sub(r'\*\*(.+?)\*\*', r'\1', text)
    return re.sub(r'\s+', ' ', text).strip()


# ---------------------------------------------------------------------------
# block rendering (the markdown-subset state machine)
# ---------------------------------------------------------------------------
_HEADING_RE = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')
_FENCE_RE = re.compile(r'^(\s*)(```|~~~)\s*(\S*)\s*$')
_HR_RE = re.compile(r'^(?:---+|\*\*\*+|___+)\s*$')
_LIST_ITEM_RE = re.compile(r'^(\s*)([-*+]|\d+[.)])\s+(.*)$')
_TABLE_ROW_RE = re.compile(r'^\s*\|.*$')
_TABLE_SEPARATOR_RE = re.compile(r'^\s*\|(?:\s*:?-+:?\s*\|)+\s*$')
_COMMENT_RE = re.compile(r'<!--.*?-->', re.DOTALL)


def split_table_row(line):
    """Split one |-delimited row into cells, honouring the \\| escape."""
    body = line.strip()
    if body.startswith("|"):
        body = body[1:]
    if body.endswith("|") and not body.endswith("\\|"):
        body = body[:-1]
    cells = re.split(r'(?<!\\)\|', body)
    return [c.replace("\\|", "|").strip() for c in cells]


class SectionCollector:
    """Accumulates the search-index records: one per heading, carrying the
    plain body text that follows it."""

    def __init__(self, page):
        self.page = page
        self.records = []
        self.current = None

    def start(self, heading, anchor):
        self.flush()
        self.current = {"page": self.page.page_id + ".html",
                        "title": self.page.title,
                        "heading": heading, "anchor": anchor, "body": []}

    def add_text(self, text):
        if self.current is None:
            self.start(self.page.title, "")
        if text:
            self.current["body"].append(text)

    def flush(self):
        if self.current is not None:
            body = " ".join(self.current["body"])
            # cap one section's contribution so a giant generated block does
            # not dominate the index size (search still sees its first part)
            self.current["body"] = body[:4000]
            self.current["title"] = self.page.title
            self.records.append(self.current)
            self.current = None


class MarkdownRenderer:
    """Renders one page's markdown lines into article HTML + section records.
    Line-oriented: fences (also indented inside list items), headings, lists,
    tables, rules, paragraphs. Anything the subset does not know renders as a
    paragraph - honest, visible output rather than silent loss."""

    def __init__(self, ctx, collector):
        self.ctx = ctx
        self.collector = collector
        self.out = []
        self.paragraph = []           # buffered raw lines of the open paragraph
        self.paragraph_line = 0
        self.lists = []               # stack of (indent, tag), the open lists
        self.item_open = False        # is a <li> element open on the deepest list
        self.table = []               # buffered raw table rows
        self.fence = None             # (indent, language, start line) or None
        self.fence_lines = []
        self.blank_pending = False    # a blank line separated list content

    # --- flushing ---------------------------------------------------------
    def flush_paragraph(self):
        if not self.paragraph:
            return
        self.ctx.line = self.paragraph_line
        text = " ".join(self.paragraph)
        rendered = render_inline(text, self.ctx)
        self.collector.add_text(plain_text(text))
        if self.lists and self.item_open:
            self.out.append("<p>%s</p>" % rendered)
        else:
            self.close_lists()
            self.out.append("<p>%s</p>" % rendered)
        self.paragraph = []

    def flush_table(self):
        if not self.table:
            return
        rows = self.table
        self.table = []
        aligns = []
        if len(rows) >= 2 and _TABLE_SEPARATOR_RE.match(rows[1][1]):
            for cell in split_table_row(rows[1][1]):
                left, right = cell.startswith(":"), cell.endswith(":")
                aligns.append("center" if left and right
                              else "right" if right else "")
            header, body = rows[0:1], rows[2:]
        else:
            header, body = [], rows
        parts = ["<table>"]
        for group, tag, group_rows in (("thead", "th", header),
                                       ("tbody", "td", body)):
            if not group_rows:
                continue
            parts.append("<%s>" % group)
            for line_no, row in group_rows:
                self.ctx.line = line_no
                cells = split_table_row(row)
                self.collector.add_text(plain_text(" ".join(cells)))
                parts.append("<tr>")
                for index, cell in enumerate(cells):
                    align = aligns[index] if index < len(aligns) else ""
                    style = ' style="text-align:%s"' % align if align else ""
                    parts.append("<%s%s>%s</%s>" % (
                        tag, style, render_inline(cell, self.ctx), tag))
                parts.append("</tr>")
            parts.append("</%s>" % group)
        parts.append("</table>")
        self.close_lists()
        self.out.append("".join(parts))

    def close_item(self):
        if self.item_open:
            self.out.append("</li>")
            self.item_open = False

    def close_lists(self, to_indent=-1):
        while self.lists and self.lists[-1][0] > to_indent:
            self.close_item()
            self.out.append("</%s>" % self.lists[-1][1])
            self.lists.pop()
            self.item_open = bool(self.lists)   # the parent's <li> is open

    def flush_all(self):
        self.flush_paragraph()
        self.flush_table()
        self.close_lists()

    # --- per-line intake ----------------------------------------------------
    def feed(self, line, line_no):
        self.ctx.line = line_no
        if self.fence is not None:
            if _FENCE_RE.match(line) and not _FENCE_RE.match(line).group(3):
                self.emit_fence()
            else:
                self.fence_lines.append(line)
            return
        fence = _FENCE_RE.match(line)
        if fence:
            self.flush_paragraph()
            self.flush_table()
            self.fence = (len(fence.group(1)), fence.group(3), line_no)
            self.fence_lines = []
            return
        if not line.strip():
            self.flush_paragraph()
            self.flush_table()
            self.blank_pending = True
            return
        heading = _HEADING_RE.match(line)
        if heading:
            self.flush_all()
            self.emit_heading(len(heading.group(1)), heading.group(2))
            return
        if _TABLE_ROW_RE.match(line):
            self.flush_paragraph()
            self.table.append((line_no, line))
            return
        self.flush_table()
        item = _LIST_ITEM_RE.match(line)
        if item:
            self.flush_paragraph()
            self.emit_list_item(len(item.group(1)),
                                "ol" if item.group(2)[0].isdigit() else "ul",
                                item.group(3), line_no)
            self.blank_pending = False
            return
        if _HR_RE.match(line.strip()) and not self.lists:
            self.flush_all()
            self.out.append("<hr>")
            return
        # plain text: a continuation of the open paragraph / list item, or a
        # fresh paragraph (a blank line after a list ends it only when the
        # text returns to the margin)
        indent = len(line) - len(line.lstrip())
        if self.blank_pending and self.lists and indent == 0:
            self.close_lists()
        self.blank_pending = False
        if not self.paragraph:
            self.paragraph_line = line_no
        self.paragraph.append(line.strip())

    # --- emitters -----------------------------------------------------------
    def emit_heading(self, level, text):
        anchor = slugify(text, self.ctx.page.anchors)
        rendered = render_inline(text, self.ctx)
        plain = plain_text(text)
        if level == 1 and self.ctx.page.title == self.ctx.page.page_id:
            self.ctx.page.title = plain
        if level == 2:
            self.ctx.page.toc.append((anchor, plain))
        self.collector.start(plain, anchor)
        self.out.append('<h%d id="%s">%s</h%d>' % (level, anchor, rendered,
                                                   level))

    def emit_list_item(self, indent, tag, text, line_no):
        if self.lists and indent < self.lists[-1][0]:
            self.close_lists(indent)
        if not self.lists or indent > self.lists[-1][0]:
            # a deeper item opens a nested list inside the open <li>
            self.out.append("<%s>" % tag)
            self.lists.append((indent, tag))
        else:
            self.close_item()
            if tag != self.lists[-1][1]:
                self.out.append("</%s><%s>" % (self.lists[-1][1], tag))
                self.lists[-1] = (indent, tag)
        self.out.append("<li>")
        self.item_open = True
        self.ctx.line = line_no
        self.out.append(render_inline(text, self.ctx))
        self.collector.add_text(plain_text(text))

    def emit_fence(self):
        indent, language, _start = self.fence
        self.fence = None
        # strip only the fence's own indent (a fence inside a list item);
        # interior code indentation stays intact
        lines = [l[indent:] if l[:indent].strip() == "" else l
                 for l in self.fence_lines]
        code = html.escape("\n".join(lines))
        self.collector.add_text(re.sub(r'\s+', ' ', "\n".join(lines)).strip())
        css = ' class="lang-%s"' % html.escape(language) if language else ""
        block = "<pre><code%s>%s</code></pre>" % (css, code)
        # inside an open list item the code block belongs to the item
        self.out.append(block)

    def render(self, text):
        text = _COMMENT_RE.sub("", text)
        for line_no, line in enumerate(text.splitlines(), 1):
            self.feed(line.rstrip("\n"), line_no)
        self.flush_all()
        self.collector.flush()
        return "\n".join(self.out)


def render_page(page, by_source, root):
    """Render one corpus page; returns the shared link-verification data."""
    with open(os.path.join(root, page.source), "r", encoding="utf-8") as f:
        text = f.read()
    ctx = RenderContext(page, by_source, root)
    collector = SectionCollector(page)
    page.html = MarkdownRenderer(ctx, collector).render(text)
    page.sections = collector.records
    return ctx


# ---------------------------------------------------------------------------
# site assembly
# ---------------------------------------------------------------------------
def page_shell(page, pages, body, extra_head=""):
    groups = []
    for group in ("Overview", "Guides", "Projects"):
        members = [p for p in pages if p.group == group]
        if not members:
            continue
        items = []
        for member in members:
            current = ' class="current"' if member is page else ""
            items.append('<li%s><a href="%s.html">%s</a></li>' % (
                current, member.page_id, html.escape(member.title)))
        groups.append('<h2>%s</h2><ul>%s</ul>' % (group, "".join(items)))
    toc = ""
    if page is not None and page.toc:
        entries = "".join('<li><a href="#%s">%s</a></li>' % (
            anchor, html.escape(text)) for anchor, text in page.toc)
        toc = '<aside class="toc"><h2>On this page</h2><ul>%s</ul></aside>' \
            % entries
    title = html.escape(page.title) if page is not None else "Orkige Help"
    return ("<!DOCTYPE html>\n"
            '<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, '
            'initial-scale=1">\n'
            "<title>%s - Orkige Help</title>\n"
            '<link rel="stylesheet" href="help.css">\n%s</head>\n<body>\n'
            '<header>\n<a class="home" href="index.html">Orkige Help</a>\n'
            '<div class="searchbox"><input id="search" type="search" '
            'placeholder="Search the docs..." autocomplete="off">\n'
            '<div id="results" hidden></div></div>\n</header>\n'
            '<div class="shell">\n<nav>%s</nav>\n'
            "<main><article>\n%s\n</article></main>\n%s</div>\n"
            '<footer>%s</footer>\n'
            '<script src="help.js"></script>\n</body>\n</html>\n') % (
        title, extra_head, "".join(groups), body, toc,
        html.escape(GENERATED_NOTE))


def index_body(pages):
    parts = ["<h1>Orkige Help</h1>",
             "<p>The engine, editor and scripting documentation, generated "
             "from the repository's committed docs. Use the search box or "
             "pick a page.</p>"]
    for group in ("Overview", "Guides", "Projects"):
        members = [p for p in pages if p.group == group]
        if not members:
            continue
        parts.append("<h2>%s</h2>" % group)
        entries = []
        for page in members:
            first = next((s for s in page.sections if s["body"]), None)
            snippet = html.escape(first["body"][:160]) + "&hellip;" \
                if first else ""
            entries.append('<li><a href="%s.html">%s</a>'
                           '<span class="snippet">%s</span></li>' % (
                               page.page_id, html.escape(page.title),
                               snippet))
        parts.append('<ul class="directory">%s</ul>' % "".join(entries))
    return "\n".join(parts)


HELP_CSS = """\
/* Orkige Help - hand-written, self-contained (no vendored frameworks). */
:root {
	--bg: #ffffff; --fg: #1c2733; --muted: #5c6c7c; --line: #dde4ea;
	--accent: #0a6aa8; --code-bg: #f2f5f8; --nav-bg: #f7f9fb;
	--hit: #fff3c2;
}
@media (prefers-color-scheme: dark) {
	:root {
		--bg: #14181d; --fg: #dbe3ea; --muted: #93a2b1; --line: #2b333c;
		--accent: #5fb2e6; --code-bg: #1d232a; --nav-bg: #181e24;
		--hit: #4d431a;
	}
}
* { box-sizing: border-box; }
body {
	margin: 0; background: var(--bg); color: var(--fg);
	font: 15px/1.55 -apple-system, "Segoe UI", Roboto, "Helvetica Neue",
		Arial, sans-serif;
}
header {
	display: flex; align-items: center; gap: 1.2rem;
	padding: 0.6rem 1.2rem; border-bottom: 1px solid var(--line);
	position: sticky; top: 0; background: var(--bg); z-index: 10;
}
header .home { font-weight: 700; color: var(--fg); text-decoration: none; }
.searchbox { position: relative; flex: 1; max-width: 34rem; }
#search {
	width: 100%; padding: 0.4rem 0.7rem; border: 1px solid var(--line);
	border-radius: 6px; background: var(--nav-bg); color: var(--fg);
	font: inherit;
}
#results {
	position: absolute; left: 0; right: 0; top: 2.4rem; max-height: 70vh;
	overflow: auto; background: var(--bg); border: 1px solid var(--line);
	border-radius: 6px; box-shadow: 0 8px 24px rgba(0,0,0,0.18);
}
#results a {
	display: block; padding: 0.5rem 0.8rem; color: var(--fg);
	text-decoration: none; border-bottom: 1px solid var(--line);
}
#results a:last-child { border-bottom: none; }
#results a:hover, #results a.selected { background: var(--nav-bg); }
#results .where { font-weight: 600; }
#results .where .page { color: var(--muted); font-weight: 400; }
#results .excerpt { color: var(--muted); font-size: 0.9em; }
#results .excerpt mark { background: var(--hit); color: inherit; }
#results .none { padding: 0.6rem 0.8rem; color: var(--muted); }
.shell { display: flex; align-items: flex-start; }
nav {
	width: 15.5rem; flex: none; padding: 1rem 1.2rem; position: sticky;
	top: 3.2rem; max-height: calc(100vh - 3.2rem); overflow: auto;
	background: var(--nav-bg); border-right: 1px solid var(--line);
	min-height: calc(100vh - 3.2rem);
}
nav h2, .toc h2 {
	font-size: 0.72rem; letter-spacing: 0.08em; text-transform: uppercase;
	color: var(--muted); margin: 1rem 0 0.3rem;
}
nav ul, .toc ul { list-style: none; margin: 0; padding: 0; }
nav li a, .toc li a {
	display: block; padding: 0.18rem 0.4rem; color: var(--fg);
	text-decoration: none; border-radius: 4px; font-size: 0.92rem;
}
nav li a:hover, .toc li a:hover { background: var(--code-bg); }
nav li.current a { color: var(--accent); font-weight: 600; }
main { flex: 1; min-width: 0; }
article { max-width: 50rem; padding: 1.4rem 2.2rem 4rem; }
.toc {
	width: 14rem; flex: none; padding: 1rem 1.2rem; position: sticky;
	top: 3.2rem; max-height: calc(100vh - 3.2rem); overflow: auto;
	font-size: 0.9rem;
}
h1, h2, h3, h4 { line-height: 1.25; scroll-margin-top: 3.6rem; }
h1 { font-size: 1.7rem; }
h2 { border-bottom: 1px solid var(--line); padding-bottom: 0.25rem; }
a { color: var(--accent); }
code {
	background: var(--code-bg); border-radius: 4px; padding: 0.08em 0.32em;
	font: 0.88em ui-monospace, "SF Mono", Menlo, Consolas, monospace;
}
pre {
	background: var(--code-bg); border: 1px solid var(--line);
	border-radius: 6px; padding: 0.8rem 1rem; overflow-x: auto;
}
pre code { background: none; padding: 0; font-size: 0.85rem; }
table { border-collapse: collapse; margin: 1rem 0; display: block;
	overflow-x: auto; }
th, td { border: 1px solid var(--line); padding: 0.35rem 0.6rem;
	text-align: left; vertical-align: top; }
th { background: var(--nav-bg); }
hr { border: none; border-top: 1px solid var(--line); margin: 2rem 0; }
li { margin: 0.15rem 0; }
li p { margin: 0.4rem 0; }
footer {
	padding: 1rem 1.2rem; color: var(--muted); font-size: 0.8rem;
	border-top: 1px solid var(--line);
}
.directory { list-style: none; padding: 0; }
.directory li { margin: 0.55rem 0; }
.directory .snippet { display: block; color: var(--muted);
	font-size: 0.88em; }
@media (max-width: 60rem) { nav, .toc { display: none; } }
"""

HELP_JS = """\
// Orkige Help - the search box. Plain hand-written JS, no dependencies.
// search-index.json holds one record per heading section:
//   { page, title, heading, anchor, body }
// Ranking: a query token matching the page title beats one matching the
// heading beats body occurrences; every token must match somewhere.
(function () {
	"use strict";
	var input = document.getElementById("search");
	var resultsBox = document.getElementById("results");
	if (!input || !resultsBox) { return; }
	var index = null;        // loaded lazily on the first keystroke
	var loading = false;
	var selected = -1;

	function tokenize(text) {
		return text.toLowerCase().split(/[^a-z0-9_.]+/).filter(Boolean);
	}

	function countOccurrences(haystack, needle) {
		var count = 0, at = haystack.indexOf(needle);
		while (at !== -1 && count < 5) {
			count += 1;
			at = haystack.indexOf(needle, at + needle.length);
		}
		return count;
	}

	function scoreRecord(record, tokens) {
		var title = record.title.toLowerCase();
		var heading = record.heading.toLowerCase();
		var body = record.body.toLowerCase();
		var score = 0;
		for (var i = 0; i < tokens.length; i += 1) {
			var token = tokens[i];
			var tokenScore = 0;
			if (title.indexOf(token) !== -1) { tokenScore += 8; }
			if (heading.indexOf(token) !== -1) { tokenScore += 5; }
			tokenScore += countOccurrences(body, token);
			if (tokenScore === 0) { return 0; }   // every token must match
			score += tokenScore;
		}
		return score;
	}

	function excerpt(record, tokens) {
		var body = record.body;
		var lower = body.toLowerCase();
		var at = -1;
		for (var i = 0; i < tokens.length && at === -1; i += 1) {
			at = lower.indexOf(tokens[i]);
		}
		if (at === -1) { return body.slice(0, 120); }
		var start = Math.max(0, at - 40);
		var slice = (start > 0 ? "\\u2026" : "") +
			body.slice(start, at + 90);
		return slice;
	}

	function highlight(text, tokens) {
		var holder = document.createElement("span");
		holder.textContent = text;
		var escaped = holder.innerHTML;
		for (var i = 0; i < tokens.length; i += 1) {
			var pattern = tokens[i].replace(/[.*+?^${}()|[\\]\\\\]/g, "\\\\$&");
			escaped = escaped.replace(new RegExp("(" + pattern + ")", "ig"),
				"<mark>$1</mark>");
		}
		return escaped;
	}

	function renderResults(hits, tokens) {
		resultsBox.innerHTML = "";
		selected = -1;
		if (hits.length === 0) {
			var none = document.createElement("div");
			none.className = "none";
			none.textContent = "No matches.";
			resultsBox.appendChild(none);
		}
		hits.forEach(function (hit) {
			var link = document.createElement("a");
			link.href = hit.record.page +
				(hit.record.anchor ? "#" + hit.record.anchor : "");
			var where = document.createElement("div");
			where.className = "where";
			where.innerHTML = highlight(hit.record.heading, tokens) +
				' <span class="page">\\u2014 ' + hit.record.title +
				"</span>";
			var body = document.createElement("div");
			body.className = "excerpt";
			body.innerHTML = highlight(excerpt(hit.record, tokens), tokens);
			link.appendChild(where);
			link.appendChild(body);
			resultsBox.appendChild(link);
		});
		resultsBox.hidden = false;
	}

	function search() {
		var tokens = tokenize(input.value);
		if (tokens.length === 0) {
			resultsBox.hidden = true;
			return;
		}
		var hits = [];
		for (var i = 0; i < index.length; i += 1) {
			var score = scoreRecord(index[i], tokens);
			if (score > 0) { hits.push({ record: index[i], score: score }); }
		}
		hits.sort(function (a, b) { return b.score - a.score; });
		renderResults(hits.slice(0, 20), tokens);
	}

	function ensureIndexThenSearch() {
		if (index !== null) { search(); return; }
		if (loading) { return; }
		loading = true;
		fetch("search-index.json").then(function (response) {
			return response.json();
		}).then(function (data) {
			index = data;
			search();
		});
	}

	input.addEventListener("input", ensureIndexThenSearch);
	input.addEventListener("keydown", function (event) {
		var links = resultsBox.querySelectorAll("a");
		if (event.key === "Escape") {
			resultsBox.hidden = true;
			input.blur();
		} else if (event.key === "ArrowDown" && links.length) {
			selected = Math.min(selected + 1, links.length - 1);
		} else if (event.key === "ArrowUp" && links.length) {
			selected = Math.max(selected - 1, 0);
		} else if (event.key === "Enter" && links.length) {
			links[Math.max(selected, 0)].click();
			return;
		} else {
			return;
		}
		event.preventDefault();
		links.forEach(function (link, i) {
			link.classList.toggle("selected", i === selected);
		});
	});
	document.addEventListener("click", function (event) {
		if (!resultsBox.contains(event.target) && event.target !== input) {
			resultsBox.hidden = true;
		}
	});
	// a shareable search: index.html?q=terms prefills the box and searches
	var prefill = new URLSearchParams(window.location.search).get("q");
	if (prefill) {
		input.value = prefill;
		ensureIndexThenSearch();
	}
})();
"""


# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------
def corpus_stamp(root, by_source):
    digest = hashlib.sha256()
    with open(SCRIPT_PATH, "rb") as f:
        digest.update(f.read())
    for source in sorted(by_source):
        digest.update(source.encode("utf-8"))
        with open(os.path.join(root, source), "rb") as f:
            digest.update(f.read())
    return digest.hexdigest()


def verify_links(contexts):
    """Anchor targets can only be checked once every page rendered; returns
    the full issue list (broken files AND broken anchors), file:line each."""
    issues = []
    for ctx in contexts:
        issues.extend(ctx.issues)
        for line, dest, fragment in ctx.pending_links:
            if fragment not in dest.anchors:
                issues.append(LinkIssue(ctx.page.source, line,
                                        dest.source + "#" + fragment,
                                        "no such heading anchor"))
    return issues


def build(root, output_dir, if_stale=False):
    by_source, pages = discover_corpus(root)
    stamp = corpus_stamp(root, by_source)
    stamp_path = os.path.join(output_dir, ".stamp")
    index_path = os.path.join(output_dir, "index.html")
    if if_stale and os.path.isfile(stamp_path) and os.path.isfile(index_path):
        with open(stamp_path, "r") as f:
            if f.read().strip() == stamp:
                print("make_help_portal: up to date (%d pages)" % len(pages))
                print("make_help_portal: OK %s" % output_dir)
                return 0

    contexts = [render_page(page, by_source, root) for page in pages]
    issues = verify_links(contexts)
    if issues:
        sys.stderr.write("make_help_portal: %d broken internal link(s):\n"
                         % len(issues))
        for issue in issues:
            sys.stderr.write("  BROKEN LINK %s\n" % issue)
        return 1

    os.makedirs(output_dir, exist_ok=True)
    written = set()

    def write(name, content):
        with open(os.path.join(output_dir, name), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write(content)
        written.add(name)

    for page in pages:
        write(page.page_id + ".html", page_shell(page, pages, page.html))
    write("index.html", page_shell(None, pages, index_body(pages)))
    write("help.css", HELP_CSS)
    write("help.js", HELP_JS)
    records = [record for page in pages for record in page.sections]
    write("search-index.json",
          json.dumps(records, ensure_ascii=False, separators=(",", ":")))
    write(".stamp", stamp + "\n")
    # a renamed/removed doc must not leave its stale page behind
    for name in os.listdir(output_dir):
        if name.endswith(".html") and name not in written:
            os.remove(os.path.join(output_dir, name))
    print("make_help_portal: wrote %d pages, %d search records"
          % (len(pages), len(records)))
    print("make_help_portal: OK %s" % output_dir)
    return 0


# ---------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------
class _TagBalanceChecker(html.parser.HTMLParser):
    """Asserts every emitted page nests its tags correctly - the guard that a
    renderer bug (an unclosed <li>, a stray </ul>) cannot ship silently."""
    VOID = {"meta", "link", "br", "hr", "input", "img"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.problems = []

    def handle_starttag(self, tag, attrs):
        if tag not in self.VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if not self.stack or self.stack[-1] != tag:
            self.problems.append("unbalanced </%s> (open: %s)"
                                 % (tag, self.stack[-5:]))
        else:
            self.stack.pop()

    def check(self, text):
        self.feed(text)
        self.close()
        if self.stack:
            self.problems.append("unclosed tags at EOF: %s" % self.stack)
        return self.problems


SELFTEST_DOC = """\
# Synthetic page

Intro paragraph with `inline code`, **bold**, *italic* and a
[link](other.md#target-section) plus [outside](../README.md).

## Lists and fences

- first item with `code`
- second item
  continuation of the second item
  ```lua
  print("fenced inside a list item")
  ```
- third item
  1. nested ordered
  2. nested ordered two

## A table

| Name | Value | Notes |
| --- | :---: | --- |
| `a` | 1 | pipe escape: a \\| b |
| b | 2 | **bold cell** |

---

Closing paragraph after a rule.
"""

SELFTEST_OTHER = """\
# Other page

## Target section

Body of the target section mentioning zanzibar exactly once.
"""


def _selftest_synthetic(temp_root):
    os.makedirs(os.path.join(temp_root, "Docs"))
    with open(os.path.join(temp_root, "README.md"), "w") as f:
        f.write("# Tiny\n\nOverview body.\n")
    with open(os.path.join(temp_root, "Docs", "synthetic.md"), "w") as f:
        f.write(SELFTEST_DOC)
    with open(os.path.join(temp_root, "Docs", "other.md"), "w") as f:
        f.write(SELFTEST_OTHER)
    out = os.path.join(temp_root, "site")
    assert build(temp_root, out) == 0
    with open(os.path.join(out, "synthetic.html")) as f:
        page = f.read()
    assert '<a href="other.html#target-section">link</a>' in page, page
    assert "<code>../README.md</code>" not in page   # resolves to overview
    assert '<a href="overview.html">outside</a>' in page, page
    assert '<pre><code class="lang-lua">print(&quot;fenced inside' in page
    assert "<ol>" in page and "nested ordered two" in page
    assert "pipe escape: a | b" in page
    assert '<td style="text-align:center">1</td>' in page
    assert '<h2 id="lists-and-fences">' in page
    for name in os.listdir(out):
        if name.endswith(".html"):
            with open(os.path.join(out, name)) as f:
                problems = _TagBalanceChecker().check(f.read())
            assert not problems, "%s: %s" % (name, problems)
    with open(os.path.join(out, "search-index.json")) as f:
        records = json.load(f)
    target = [r for r in records if r["anchor"] == "target-section"]
    assert target and "zanzibar" in target[0]["body"]

    # staleness: an unchanged corpus is a no-op, a touched source rebuilds
    stamp_file = os.path.join(out, ".stamp")
    before = open(stamp_file).read()
    assert build(temp_root, out, if_stale=True) == 0
    assert open(stamp_file).read() == before
    with open(os.path.join(temp_root, "Docs", "other.md"), "a") as f:
        f.write("\nMore prose.\n")
    assert build(temp_root, out, if_stale=True) == 0
    assert open(stamp_file).read() != before

    # a broken link (missing file AND missing anchor) fails the build and
    # names file:line - the actionable report docs authors get
    with open(os.path.join(temp_root, "Docs", "broken.md"), "w") as f:
        f.write("# Broken\n\nSee [gone](missing.md) and "
                "[bad](other.md#no-such-anchor).\n")
    captured = io.StringIO()
    real_stderr = sys.stderr
    sys.stderr = captured
    try:
        result = build(temp_root, out)
    finally:
        sys.stderr = real_stderr
    assert result == 1
    report = captured.getvalue()
    assert "Docs/broken.md:3 -> missing.md" in report, report
    assert "Docs/broken.md:3 -> Docs/other.md#no-such-anchor" in report, report


def _selftest_real_corpus(temp_root):
    out = os.path.join(temp_root, "real_site")
    assert build(ROOT, out) == 0, "real corpus must render with 0 broken links"
    by_source, pages = discover_corpus(ROOT)
    assert any(p.page_id == "lua-api" for p in pages)
    assert any(p.page_id == "overview" for p in pages)
    for page in pages:
        path = os.path.join(out, page.page_id + ".html")
        assert os.path.getsize(path) > 0, page.page_id
        with open(path) as f:
            problems = _TagBalanceChecker().check(f.read())
        assert not problems, "%s: %s" % (page.page_id, problems)
    with open(os.path.join(out, "search-index.json")) as f:
        records = json.load(f)
    assert len(records) > 100, "the corpus should index >100 sections"
    hits = [r for r in records if "script components" in r["heading"].lower()]
    assert hits, "lua-api's Script components section must be indexed"
    with open(os.path.join(out, "lua-api.html")) as f:
        lua_page = f.read()
    assert 'id="script-components"' in lua_page
    print("make_help_portal selftest: real corpus OK (%d pages, %d records)"
          % (len(pages), len(records)))


def cmd_selftest():
    with tempfile.TemporaryDirectory() as temp_root:
        _selftest_synthetic(os.path.join(temp_root, "synthetic"))
        _selftest_real_corpus(temp_root)
    print("make_help_portal selftest OK")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(
        description="build the offline help portal from the docs corpus")
    parser.add_argument("--output", help="site output directory")
    parser.add_argument("--if-stale", action="store_true",
                        help="skip the build when no source changed")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)
    if args.selftest:
        return cmd_selftest()
    if not args.output:
        parser.error("--output is required (or --selftest)")
    return build(ROOT, os.path.abspath(args.output), if_stale=args.if_stale)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
