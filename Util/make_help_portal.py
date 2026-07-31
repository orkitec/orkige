#!/usr/bin/env python3
# make_help_portal.py - assemble the engine's website: a landing page, the
# searchable documentation portal and the legal pages.
#
# CI regenerates and deploys the site to https://orkige.orkitec.com on every
# push to main (.github/workflows/pages.yml); the editor's Help > "Orkige
# Help" opens the published /help/ portal. The docs corpus is the
# repository's committed prose exactly as written (this script PRESENTS the
# docs, it never rewrites them). Output layout:
#
#   index.html           -> the landing page (the product front door), whose
#                           Downloads section links the nightly binaries;
#                           help/downloads.js only ever improves it
#   help/                -> the documentation portal:
#     overview.html        <- README.md
#     changelog.html       <- the FULL release history, rendered from the
#                             repository's git log at deploy time (never a
#                             committed file: git history is the record, and
#                             the ONE composition of it lives in
#                             Util/orkige_nightly_package.py, which the
#                             nightly's own changelog comes from too)
#     <doc>.html           <- Docs/*.md (one page each, incl. GENERATED blocks)
#     project-<name>.html  <- projects/*/README.md (picked up when a project
#                             documents itself)
#   imprint.html         -> Docs/legal/imprint.md (footer-linked, unindexed)
#   privacy.html         -> Docs/legal/privacy.md (footer-linked, unindexed)
#
# NOT generated here: /api/, the C++ class reference. The Pages workflow
# renders it from the engine headers with dedicated documentation tooling
# (Docs/api/Doxyfile) and assembles it NEXT TO this output - keeping this
# script stdlib-only and locally runnable with no extra installs. Links to
# /api/ are therefore the ONE target the link gate accepts on faith.
#
#   make_help_portal.py --output <dir>              build the site
#   make_help_portal.py --output <dir> --if-stale   rebuild only when a source
#                                                   (or this script) changed -
#                                                   a sha256 stamp, the
#                                                   update_docs --check idea
#   make_help_portal.py --selftest                  render synthetic corpora
#                                                   AND the real one, assert
#                                                   pages/index/links/stamp
#
# The site is fully self-contained and relative-linked (hand-written help.css
# + help.js, a generated search-index.json - no vendored libraries, no
# network), so a built directory also previews straight off the local disk.
# Python has no stdlib markdown library, so rendering is a markdown-SUBSET
# renderer tailored to what the corpus actually uses (audited): ATX headings,
# nested unordered/ordered lists with continuation lines, fenced code blocks
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
import shutil
import subprocess
import sys
import tempfile

SCRIPT_PATH = os.path.abspath(__file__)
UTIL_DIR = os.path.dirname(SCRIPT_PATH)
ROOT = os.path.dirname(UTIL_DIR)
if UTIL_DIR not in sys.path:
    sys.path.insert(0, UTIL_DIR)
import make_editor_icon  # noqa: E402  (sibling Util tool - the ONE icon drawing)
# the packaging tool owns the ONE reading of the repository's commit log into
# changelog prose (subject -> headline, day grouping, the ordered version per
# day); the portal RENDERS that text rather than parsing git a second way
import orkige_nightly_package  # noqa: E402

# preferred reading order for the sidebar; corpus pages not listed here are
# appended alphabetically, so a new doc shows up without touching this script
PREFERRED_ORDER = [
    "getting-started", "lua-api", "gui", "materials", "particles", "sound",
    "meshes", "vector-animation", "character-animation", "localisation",
    "logging", "benchmark",
    "mcp", "mcp-workflows", "render-abstraction", "web-export",
    "device-session", "ios-signing", "store-release", "nightly-builds",
    "ports", "vendored-libs",
]

GENERATED_NOTE = ("Generated from the repository docs by "
                  "Util/make_help_portal.py - edit the source .md files, "
                  "not this site.")

SITE_URL = "https://orkige.orkitec.com"
GITHUB_URL = "https://github.com/orkitec/orkige"

# the site identity: the editor's app icon (Util/make_editor_icon.py draws it),
# generated at build time into the help/ directory at the sizes each surface
# needs. Root-level pages reference them through "help/", portal pages directly
# (the same prefix rule as help.css).
FAVICON_PNG = "favicon-32.png"
APPLE_TOUCH_PNG = "apple-touch-180.png"
LOGO_PNG = "orkige-logo.png"        # header wordmark logo (64px, shown ~28px)
SITE_ICONS = ((32, FAVICON_PNG), (180, APPLE_TOUCH_PNG), (64, LOGO_PNG))


def head_icons(prefix):
    """the favicon + apple-touch <link> tags for a page head (`prefix` is ""
    for a portal page, "help/" for a root-level page)."""
    return ('<link rel="icon" type="image/png" sizes="32x32" href="%s%s">\n'
            '<link rel="apple-touch-icon" sizes="180x180" href="%s%s">\n'
            % (prefix, FAVICON_PNG, prefix, APPLE_TOUCH_PNG))


def home_link(prefix, href, text):
    """the header wordmark: the site logo next to the name, linking home."""
    return ('<a class="home" href="%s"><img class="logo" src="%s%s" alt="" '
            'width="28" height="28">%s</a>'
            % (html.escape(href, quote=True), prefix, LOGO_PNG,
               html.escape(text)))

# the sidebar/index groups, in order (the "Legal" group is deliberately
# absent: its pages are footer-linked only and stay out of nav and search)
NAV_GROUPS = ("Overview", "Guides", "Projects")

# the API reference is assembled at /api/ by the Pages workflow (rendered
# from the engine headers by CI-only tooling, see Docs/api/Doxyfile) - a
# local portal preview lacks it unless that tool is run by hand
API_SECTION_NOTE = ("The C++ class reference, generated from the engine "
                    "headers when the site deploys (not part of a local "
                    "portal preview).")


# ---------------------------------------------------------------------------
# corpus discovery
# ---------------------------------------------------------------------------
class Page:
    def __init__(self, page_id, source, group, directory="help",
                 unindexed=False):
        self.page_id = page_id        # output stem, e.g. "lua-api"
        self.source = source          # repo-relative source path
        self.group = group            # sidebar group name
        self.directory = directory    # output subdir: "help" or "" (root)
        self.title = page_id          # replaced by the first H1
        self.html = ""                # rendered article body
        self.toc = []                 # [(anchor, text)] for the h2 rail
        self.anchors = set()          # every heading slug on the page
        self.sections = []            # search records for this page
        # kept out of the search index (the legal pages by convention, the
        # changelog because a commit log would drown the documentation it
        # shares the box with)
        self.unindexed = unindexed


def page_href(from_directory, to_page):
    """Relative href from a page in `from_directory` to `to_page` - the whole
    site is relative-linked so it previews straight off the local disk."""
    name = to_page.page_id + ".html"
    if from_directory == to_page.directory:
        return name
    if from_directory == "help":
        return "../" + name
    return to_page.directory + "/" + name


# the full release history: a page with no source FILE, rendered from the
# repository's git log when the site is built. It is deliberately not part of
# the corpus map, so a doc cannot link to it as if it were a committed .md -
# there is no such file, and the link gate should say so.
CHANGELOG_PAGE_ID = "changelog"


def changelog_page():
    """the release-history Page: same shell, same nav, no source file."""
    return Page(CHANGELOG_PAGE_ID, "", "Overview", unindexed=True)


def changelog_markdown(root):
    """the history document the page renders, composed by the packaging tool
    (the ONE reading of the commit log) and degrading honestly there: a
    shallow checkout says the record is truncated and a machine with no git
    history says nothing is listed."""
    return orkige_nightly_package.collect_history("HEAD", root,
                                                  title="Changelog")


def discover_corpus(root):
    """The corpus pages in sidebar order: Overview (the README plus the
    generated release history), the Docs guides, the project READMEs, plus the
    root-level legal pages (footer-only).
    Returns ({repo path -> Page}, [Page in order]); the map holds only pages
    backed by a committed file, which is what an internal link may target."""
    pages = []
    readme = os.path.join(root, "README.md")
    if os.path.isfile(readme):
        pages.append(Page("overview", "README.md", "Overview"))
    pages.append(changelog_page())
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
    for stem in ("imprint", "privacy"):
        candidate = os.path.join(docs_dir, "legal", stem + ".md")
        if os.path.isfile(candidate):
            pages.append(Page(stem, "Docs/legal/%s.md" % stem, "Legal",
                              directory="", unindexed=True))
    by_source = {p.source: p for p in pages if p.source}
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

    def __init__(self, page, by_source, root, links=True):
        self.page = page
        self.by_source = by_source
        self.root = root
        self.line = 0                 # source line of the block being rendered
        self.pending_links = []       # (line, target_page, fragment) to verify
        self.issues = []              # LinkIssue list
        self.images = []              # (repo_path, out_dir, name) to copy
        # authored prose links to other pages and is held to it; GENERATED
        # prose (the release history, whose text is commit subjects) carries no
        # authored links, so bracket punctuation that happens to look like one
        # stays text instead of becoming a link nobody wrote and a gate failure
        # nobody can act on
        self.links = links

    def resolve_image(self, alt, target):
        """One ![alt](target) -> HTML. A committed repository image (the README
        mark) is COPIED into the site next to the page and shown as an <img>;
        remote images (README badges) and missing files degrade to their alt
        text - the portal ships no fetched artwork."""
        # a remote scheme (http:, data:, ...) - and every image reference on a
        # generated page - stays alt text
        if not self.links or re.match(r'^[a-z][a-z0-9+.-]*:', target):
            return html.escape(alt)
        path = target.partition("#")[0]
        source_dir = os.path.dirname(self.page.source)
        repo_path = os.path.normpath(
            os.path.join(source_dir, path)).replace(os.sep, "/")
        if os.path.isfile(os.path.join(self.root, repo_path)):
            name = os.path.basename(repo_path)
            self.images.append((repo_path, self.page.directory, name))
            return '<img class="doc-image" src="%s" alt="%s">' % (
                html.escape(name, quote=True), html.escape(alt))
        return html.escape(alt)

    def resolve_link(self, text_html, target):
        """One [text](target) -> HTML, offline discipline: corpus .md links
        become page links (verified later), repository files degrade to code,
        genuinely missing targets are reported as broken."""
        if not self.links:
            return text_html
        if re.match(r'^[a-z][a-z0-9+.-]*:', target):  # http:, https:, mailto:
            return '<a class="external" href="%s">%s</a>' % (
                html.escape(target, quote=True), text_html)
        if target == "/api/":
            # the API reference the Pages workflow assembles NEXT TO this
            # generator's output (rendered from the engine headers by
            # CI-only tooling, Docs/api/Doxyfile) - a site-absolute
            # target the link gate accepts without a corpus page behind it
            prefix = "../" if self.page.directory == "help" else ""
            return '<a href="%sapi/index.html">%s</a>' % (prefix, text_html)
        if target == "/play/":
            # the live browser benchmark the Pages workflow stages next to this
            # output (a wasm export of projects/benchmark) - like /api/, a
            # site-absolute target with no corpus page behind it, so the link
            # gate accepts it on faith
            prefix = "../" if self.page.directory == "help" else ""
            return '<a href="%splay/index.html">%s</a>' % (prefix, text_html)
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
            href = page_href(self.page.directory, dest) + \
                ("#" + fragment if fragment else "")
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

    # a committed repository image renders as <img> (copied into the site); a
    # remote image (README badge) or a missing one degrades to its alt text
    text = _IMAGE_RE.sub(lambda m: ctx.resolve_image(m.group(1), m.group(2)),
                         text)

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


def render_markdown(page, text, by_source, root, links=True):
    """Render markdown into one page; returns the link-verification data. The
    text comes from a committed file for a corpus page and from the generator
    for the release history - one renderer either way."""
    ctx = RenderContext(page, by_source, root, links=links)
    collector = SectionCollector(page)
    page.html = MarkdownRenderer(ctx, collector).render(text)
    page.sections = collector.records
    return ctx


def render_page(page, by_source, root):
    """Render one page: a corpus page reads its committed source, the release
    history is generated from the repository's git log."""
    if not page.source:
        return render_markdown(page, changelog_markdown(root), by_source, root,
                               links=False)
    with open(os.path.join(root, page.source), "r", encoding="utf-8") as f:
        return render_markdown(page, f.read(), by_source, root)


# ---------------------------------------------------------------------------
# site assembly
# ---------------------------------------------------------------------------
def footer_html(from_directory, pages):
    """The shared footer: the generated-site note plus the legal links -
    imprint and privacy notice appear on EVERY page, footer-only."""
    legal = [p for p in pages if p.group == "Legal"]
    links = ""
    if legal:
        links = '<span class="footer-links">%s</span>' % " &middot; ".join(
            '<a href="%s">%s</a>' % (
                html.escape(page_href(from_directory, p), quote=True),
                html.escape(p.title)) for p in legal)
    return "<footer><span>%s</span>%s</footer>" % (
        html.escape(GENERATED_NOTE), links)


def page_shell(page, pages, body, extra_head=""):
    groups = []
    for group in NAV_GROUPS:
        members = [p for p in pages if p.group == group]
        if not members:
            continue
        items = []
        for member in members:
            current = ' class="current"' if member is page else ""
            items.append('<li%s><a href="%s.html">%s</a></li>' % (
                current, member.page_id, html.escape(member.title)))
        groups.append('<h2>%s</h2><ul>%s</ul>' % (group, "".join(items)))
        if group == "Guides":
            # the /api/ reference lives beside the portal, assembled by the
            # Pages workflow - a static entry, no Page object behind it
            groups.append('<h2>Engine API</h2><ul><li>'
                          '<a href="../api/index.html">API Reference</a>'
                          "</li></ul>")
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
            '<link rel="stylesheet" href="help.css">\n'
            + head_icons("") +
            '%s</head>\n<body>\n'
            '<header>\n' + home_link("", "index.html", "Orkige Help") + '\n'
            '<div class="searchbox"><input id="search" type="search" '
            'placeholder="Search the docs..." autocomplete="off">\n'
            '<div id="results" hidden></div></div>\n'
            '<span class="header-links"><a href="../index.html">Orkige</a>'
            "</span>\n</header>\n"
            '<div class="shell">\n<nav>%s</nav>\n'
            "<main><article>\n%s\n</article></main>\n%s</div>\n"
            "%s\n"
            '<script src="help.js"></script>\n</body>\n</html>\n') % (
        title, extra_head, "".join(groups), body, toc,
        footer_html("help", pages))


def legal_shell(page, pages, body):
    """The minimal shell for a root-level legal page: no sidebar, no search
    (legal pages stay out of nav and the search index by convention)."""
    return ("<!DOCTYPE html>\n"
            '<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, '
            'initial-scale=1">\n'
            "<title>%s - Orkige</title>\n"
            '<link rel="stylesheet" href="help/help.css">\n'
            + head_icons("help/") +
            '</head>\n<body>\n'
            '<header>\n' + home_link("help/", "index.html", "Orkige") + '\n'
            '<span class="header-links">'
            '<a href="help/index.html">Documentation</a></span>\n</header>\n'
            '<div class="shell">\n<main><article>\n%s\n</article></main>\n'
            "</div>\n%s\n</body>\n</html>\n") % (
        html.escape(page.title), body, footer_html("", pages))


def index_body(pages):
    parts = ["<h1>Orkige Help</h1>",
             "<p>The engine, editor and scripting documentation, generated "
             "from the repository's committed docs and engine headers. Use "
             "the search box or pick a page.</p>"]
    for group in NAV_GROUPS:
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
        if group == "Guides":
            parts.append("<h2>Engine API</h2>")
            parts.append('<ul class="directory"><li>'
                         '<a href="../api/index.html">API Reference</a>'
                         '<span class="snippet">%s</span></li></ul>'
                         % html.escape(API_SECTION_NOTE))
    return "\n".join(parts)


# ---------------------------------------------------------------------------
# the downloads section (on the landing page)
# ---------------------------------------------------------------------------
# Nightly binaries are published as a rolling GitHub prerelease tagged
# `nightly` (Docs/nightly-builds.md). THERE IS NO STABLE ASSET URL: an asset's
# filename carries the version and the commit
# (`Orkige-macos-2.0.0-nightly.20260731_498a82b2a.dmg`), so it changes every
# night, while this site is static and regenerates only when the repository
# does. Three ways to bridge that, and why one of them wins:
#
#   * A hardcoded asset URL is stale within a day and 404s. Out.
#   * GitHub's own stable form, /releases/latest/download/<name>, does not
#     apply twice over: it needs a FIXED asset name, and `latest` skips
#     prereleases - every nightly is one. Out.
#   * Linking the release page always works and never rots, but hands the
#     visitor the whole asset list - six archives, a checksum sidecar beside
#     each and the changelog - to pick their platform's two out of.
#
# So: the release-page link IS the markup, and a small script resolves the
# night's real asset URLs from the releases API and rewrites the buttons in
# place. The enhancement can only ever improve the page - with no JavaScript,
# no network, an API shape we do not recognise, or the unauthenticated 60
# calls an hour per IP exhausted, nothing runs and every button still leads to
# the release page, which is the honest answer rather than a broken one.
#
# One table below drives both sides: the asset-name patterns and the
# user-agent rules are matched by Python in the selftest and by the browser
# from the same emitted strings, so the two can never disagree.
NIGHTLY_TAG = "nightly"
NIGHTLY_RELEASE_URL = "%s/releases/tag/%s" % (GITHUB_URL, NIGHTLY_TAG)
RELEASES_URL = "%s/releases" % GITHUB_URL
NIGHTLY_API_URL = "https://api.github.com/repos/%s/releases/tags/%s" % (
    GITHUB_URL.split("github.com/", 1)[1], NIGHTLY_TAG)
# the machine-readable version marker the release notes carry, which is what
# names the build rather than a version guessed out of a filename
NIGHTLY_VERSION_MARKER = "orkige-nightly-version"

# One entry per platform, in the order the cards appear. `assets` is
# (slot, button label, filename pattern) with the INSTALL shape first; the
# patterns are anchored so a `.sha256` sidecar never matches its archive.
# `note` is a small HTML fragment (it carries <code>/<strong>), so it is
# emitted verbatim - the tag-balance check in the selftest covers it.
DOWNLOAD_PLATFORMS = (
    {
        "id": "macos",
        "title": "macOS",
        "arch": "Apple silicon",
        "assets": (
            ("install", "Download .dmg", r"^Orkige-macos-.+\.dmg$"),
            ("portable", ".zip", r"^Orkige-macos-.+\.zip$"),
        ),
        "note": ("Open the disk image and drag Orkige to Applications. The "
                 "app is signed, notarized and stapled, so it opens like any "
                 "other app &mdash; no security prompt to click through. "
                 "Apple silicon only: the build is arm64."),
    },
    {
        "id": "windows",
        "title": "Windows",
        "arch": "x64",
        "assets": (
            ("install", "Download installer",
             r"^Orkige-windows-.+-setup\.exe$"),
            ("portable", ".zip", r"^Orkige-windows-.+\.zip$"),
        ),
        "note": ("The installer is per-user and asks for no administrator "
                 "rights &mdash; but it is <strong>unsigned</strong>, so "
                 "SmartScreen greets it with the full-screen "
                 "&ldquo;Windows protected your PC&rdquo;, whose default "
                 "button is <em>Don&rsquo;t run</em>. The way through is "
                 "<em>More info</em> &rarr; <em>Run anyway</em>. The "
                 "portable <code>.zip</code> needs no such confirmation. "
                 "Code signing for Windows is applied for through the "
                 "<a href=\"https://signpath.org\">SignPath Foundation</a>, "
                 "whose free certificates are provided by "
                 "<a href=\"https://signpath.io\">SignPath.io</a>; the "
                 "application is pending, and until a certificate is in "
                 "place these builds stay unsigned."),
    },
    {
        "id": "linux",
        "title": "Linux",
        "arch": "x86_64",
        "assets": (
            ("install", "Download .AppImage", r"^Orkige-linux-.+\.AppImage$"),
            ("portable", ".tar.gz", r"^Orkige-linux-.+\.tar\.gz$"),
        ),
        "note": ("One file: <code>chmod +x</code> it and run it. The "
                 "AppImage mounts itself through FUSE; where a distribution "
                 "no longer ships FUSE, run it with "
                 "<code>--appimage-extract-and-run</code> instead. The "
                 "<code>.tar.gz</code> is the portable alternative and "
                 "expects the distribution&rsquo;s own X, GL/Vulkan, audio "
                 "and D-Bus libraries to be installed &mdash; the AppImage "
                 "carries those itself."),
    },
)

# Which card to highlight, first rule wins, matched against the lower-cased
# user agent. A phone or tablet gets NO highlight: there is no desktop editor
# build for it, and an Android user agent also says "Linux". An iPad running
# a desktop-class user agent reads as macOS, which is why the highlight is
# only ever a highlight - every platform stays visible and clickable.
DOWNLOAD_DETECT_RULES = (
    ("android|iphone|ipad|ipod", ""),
    ("mac", "macos"),
    ("win", "windows"),
    ("linux|x11", "linux"),
)


def download_asset_slots():
    """[(platform id, slot, filename pattern)] flattened, in card order - the
    ONE list both the Python matcher and the browser's matcher read."""
    return [(platform["id"], slot, pattern)
            for platform in DOWNLOAD_PLATFORMS
            for slot, _label, pattern in platform["assets"]]


def match_download_asset(name):
    """A release asset filename -> "platform/slot", or "" when it is not one
    of the six downloads (a `.sha256` sidecar, the changelog asset, anything
    a later night adds)."""
    for platform_id, slot, pattern in download_asset_slots():
        if re.match(pattern, name):
            return "%s/%s" % (platform_id, slot)
    return ""


def detect_download_platform(user_agent):
    """A user agent -> the platform id to highlight, "" when nothing matches.
    An unknown agent must leave the section exactly as it shipped."""
    text = (user_agent or "").lower()
    for pattern, platform_id in DOWNLOAD_DETECT_RULES:
        if re.search(pattern, text):
            return platform_id
    return ""


def downloads_config():
    """The configuration the page's script is generated with: the API call to
    make, the asset patterns to match and the user-agent rules - the same
    strings the functions above use."""
    return {
        "api": NIGHTLY_API_URL,
        "assets": [{"platform": platform_id, "slot": slot,
                    "pattern": pattern}
                   for platform_id, slot, pattern in download_asset_slots()],
        "detect": [[pattern, platform_id]
                   for pattern, platform_id in DOWNLOAD_DETECT_RULES],
        "versionMarker": NIGHTLY_VERSION_MARKER,
    }


DOWNLOADS_HEADING = "Download the editor"

DOWNLOADS_LEAD = (
    "Nightly prereleases: the tip of <code>main</code>, built and tested by "
    "CI and replaced each night. Each platform ships an installable download "
    "and a portable archive of the same build.")

# the corpus pages the GENERATED pages link into. The markdown link gate only
# sees AUTHORED prose, so these are verified separately at build time - a
# renamed doc or a retitled heading has to fail the build here too, rather
# than 404 on the live site.
GENERATED_DOC_LINKS = (
    ("nightly-builds", ""),
    ("nightly-builds", "what-a-downloaded-build-cannot-do-yet"),
)


def landing_doc_href(page_id, anchor=""):
    """A root-level page's href into the portal (the landing page lives one
    directory above it)."""
    return "help/%s.html%s" % (page_id, ("#" + anchor) if anchor else "")


def download_card(platform):
    """One platform's card: heading, the two buttons, the caveats. The buttons
    ship pointing at the release page and are rewritten in place by the
    script when the API answers."""
    buttons = []
    for index, (slot, label, _pattern) in enumerate(platform["assets"]):
        buttons.append('<a class="action%s" data-asset="%s/%s" href="%s">%s'
                       "</a>"
                       % (" primary" if index == 0 else "", platform["id"],
                          slot, html.escape(NIGHTLY_RELEASE_URL, quote=True),
                          html.escape(label)))
    return ('<section class="download" data-platform="%s">'
            '<h3>%s <span class="download-arch">%s</span></h3>'
            '<p class="download-actions">%s</p>'
            '<p class="download-note">%s</p></section>'
            % (html.escape(platform["id"], quote=True),
               html.escape(platform["title"]), html.escape(platform["arch"]),
               "".join(buttons), platform["note"]))


def downloads_section():
    """The landing page's Downloads section. Everything a visitor needs is in
    this markup before any script runs."""
    cards = "".join(download_card(p) for p in DOWNLOAD_PLATFORMS)
    foot = ('Every download has a <code>.sha256</code> file beside it, and '
            'older builds stay on the <a class="external" href="%s">releases '
            "page</a> as dated prereleases for two weeks. "
            '<a href="%s">What a downloaded build cannot do yet</a> is part '
            'of the <a href="%s">nightly build documentation</a>.'
            % (html.escape(RELEASES_URL, quote=True),
               landing_doc_href("nightly-builds",
                                "what-a-downloaded-build-cannot-do-yet"),
               landing_doc_href("nightly-builds")))
    return ('<section class="downloads" id="downloads">\n'
            "<h2>%s</h2>\n"
            '<p class="downloads-lead">%s <span id="download-build"></span>'
            "</p>\n"
            '<div class="download-grid">%s</div>\n'
            '<p class="downloads-foot">%s</p>\n</section>'
            % (html.escape(DOWNLOADS_HEADING), DOWNLOADS_LEAD, cards, foot))


def _constant_line(name):
    """The line a module constant is declared on, so a generated-link failure
    reads file:line like every other broken-link report."""
    try:
        with open(SCRIPT_PATH, "r", encoding="utf-8") as f:
            for number, line in enumerate(f, 1):
                if line.startswith(name + " ="):
                    return number
    except OSError:
        pass
    return 0


def verify_generated_links(pages):
    """The link gate for pages this script writes itself: every corpus target
    a generated page names has to exist, anchor included."""
    by_id = {page.page_id: page for page in pages}
    line = _constant_line("GENERATED_DOC_LINKS")
    issues = []
    for page_id, anchor in GENERATED_DOC_LINKS:
        page = by_id.get(page_id)
        if page is None:
            issues.append(LinkIssue(os.path.relpath(SCRIPT_PATH, ROOT), line,
                                    page_id + ".html",
                                    "no such corpus page"))
        elif anchor and anchor not in page.anchors:
            issues.append(LinkIssue(os.path.relpath(SCRIPT_PATH, ROOT), line,
                                    page.source + "#" + anchor,
                                    "no such heading anchor"))
    return issues


# ---------------------------------------------------------------------------
# the landing page (the site root at https://orkige.orkitec.com)
# ---------------------------------------------------------------------------
LANDING_TAGLINE = (
    "A C++20 game engine for desktop, mobile and web games &mdash; macOS, "
    "Windows, Linux, iOS, Android and the browser via WebAssembly &mdash; "
    "with an AI-native editor that lets agents create, run, test and debug "
    "games over MCP.")

LANDING_INTRO = (
    "A full 3D engine with a first-class 2D layer on top. Originally "
    "written 2009&ndash;2012 and shipped on the App Store, revived and "
    "fully modernized in 2026. Open source under the Apache-2.0 license.")

LANDING_FEATURES = (
    ("Dual-backend rendering",
     "Two render backends behind one facade &mdash; Metal, Vulkan, GL3+, "
     "GLES2 and WebGL &mdash; with pixel-identical output enforced by a "
     "parity test. SDL3 windowing and input, glTF asset loading."),
    ("Six platforms, one project",
     "A game is a folder with a manifest; the Build menu exports a macOS "
     "app, iOS app, Android APK or a self-contained browser build &mdash; "
     "each with per-project icons and launch screens; store-submittable "
     "packages are a CLI flag away."),
    ("Live Lua scripting",
     "Game logic lives in per-object script components with "
     "designer-tunable properties, a sandbox per instance and hot-reload "
     "during Play &mdash; complete games ship with zero compiled code."),
    ("2D and organic vector art",
     "Sprites, flipbook animation, batched particles, and flat-colour "
     "vector shapes that squash, stretch, wobble and morph as soft bodies "
     "&mdash; resolution-independent art with cooked vector clip "
     "animation."),
    ("Physics and gameplay systems",
     "Jolt Physics with a planar 2D mode, data-driven collision layers and "
     "contact events into script; named input actions, an audio mixer with "
     "streamed music, tweens, localisation, typed saves, haptics and a "
     "defined mobile lifecycle."),
    ("Game UI",
     "A display-scale and safe-area aware UI system: runtime-baked TTF "
     "fonts, rect-anchor layout with groups and scroll views, nine-slice "
     "panels, and whole screens authored as declarative text files."),
    ("An editor that plays out of process",
     "Scene authoring with hierarchy, inspector, asset browser, prefabs "
     "and tile painting; Play spawns the standalone player over a debug "
     "protocol &mdash; on desktop, simulators, devices or in the browser "
     "&mdash; so a crashing game never takes the editor down."),
    ("AI-native by design",
     "The editor hosts a Model Context Protocol server: agents open "
     "projects, edit scenes, write assets, drive Play, run tests and read "
     "back state and screenshots."),
)


def landing_page(pages):
    """The product front door at the site root: the engine's story distilled
    from the repository README, linking into the portal and the repository -
    text-first, no claims the README does not make."""
    getting_started = next(
        (p for p in pages if p.page_id == "getting-started"), None)
    # the downloads section is on this page, so the first action jumps to it
    actions = ['<a class="action primary" href="#downloads">Download</a>',
               '<a class="action" href="help/index.html">Documentation</a>']
    if getting_started is not None:
        actions.append('<a class="action" href="help/%s.html">Getting '
                       "started</a>" % getting_started.page_id)
    # /api/ is assembled beside this output by the Pages workflow
    actions.append('<a class="action" href="api/index.html">'
                   "API Reference</a>")
    # the live browser benchmark (its own page; the /play/ export is staged by
    # the Pages workflow) - the landing page itself stays embed-free
    actions.append('<a class="action" href="benchmark.html">'
                   "Benchmark</a>")
    actions.append('<a class="action" href="%s">GitHub</a>' % GITHUB_URL)
    features = "".join(
        '<section class="feature"><h3>%s</h3><p>%s</p></section>'
        % (title, text) for title, text in LANDING_FEATURES)
    return ("<!DOCTYPE html>\n"
            '<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, '
            'initial-scale=1">\n'
            "<title>Orkige - the orkitec game engine</title>\n"
            '<link rel="stylesheet" href="help/help.css">\n'
            + head_icons("help/") +
            '</head>\n<body>\n'
            '<header>\n' + home_link("help/", "index.html", "Orkige") + '\n'
            '<span class="header-links">'
            '<a href="help/index.html">Documentation</a>'
            '<a href="benchmark.html">Benchmark</a>'
            '<a href="%s">GitHub</a></span>\n</header>\n'
            '<div class="hero">\n<h1>Orkige</h1>\n'
            '<p class="tagline">%s</p>\n'
            '<p class="actions">%s</p>\n</div>\n'
            '<main class="landing"><article>\n<p>%s</p>\n'
            '<div class="features">%s</div>\n'
            "%s\n</article></main>\n"
            "%s\n"
            # progressive enhancement only: the section above is complete and
            # correct before this file is fetched, parsed or run
            '<script src="help/%s"></script>\n'
            "</body>\n</html>\n") % (
        GITHUB_URL, LANDING_TAGLINE, "".join(actions), LANDING_INTRO,
        features, downloads_section(), footer_html("", pages),
        DOWNLOADS_JS_NAME)


# ---------------------------------------------------------------------------
# the live benchmark page (its own subpage; the LANDING page stays embed-free)
# ---------------------------------------------------------------------------
BENCHMARK_HEADING = "Benchmark"

# the intro/context prose that sits UNDER the embedded player. Plain and
# declarative, no claims the repository docs do not make (see Docs/benchmark.md).
BENCHMARK_INTRO = (
    "This is the Orkige benchmark, running in the browser. It is the same "
    "project the editor builds for every platform, compiled to WebAssembly. "
    "It runs on its own with no input: a sequence of scenes shows the "
    "engine's features and times each one. The scenes cover terrain with a "
    "day-and-night cycle and shadows, water, many point lights, particles, an "
    "instance field, animated characters, 2D vector art, a UI screen and a "
    "physics test. The tour ends on a results card; the Restart button there "
    "runs it again.")

BENCHMARK_NOTE = (
    "The player is a few megabytes, so it takes a moment to load. It needs a "
    "browser with WebGL2, which current desktop and mobile browsers have. The "
    "same build also runs on macOS, Windows, Linux, iOS and Android.")


def benchmark_page(pages):
    """The live in-browser benchmark: the site header, the embedded player
    directly below it, then the context prose. The iframe is 100% of the text
    column (never wider), a fixed 16:9 box that scales down with the column on
    narrow screens. The wasm export is staged at /play/ by the Pages workflow;
    a local portal preview lacks it (like /api/)."""
    frame = (
        '<div class="player-frame">\n'
        '<div class="player-loading">Loading the live benchmark&hellip;</div>\n'
        '<iframe class="player" src="play/index.html" '
        'title="The Orkige benchmark running in the browser" '
        'allow="autoplay; fullscreen" '
        "onload=\"var l=this.parentNode.querySelector('.player-loading');"
        "if(l){l.style.display='none';}\"></iframe>\n</div>")
    return ("<!DOCTYPE html>\n"
            '<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, '
            'initial-scale=1">\n'
            "<title>Benchmark - Orkige</title>\n"
            '<link rel="stylesheet" href="help/help.css">\n'
            + head_icons("help/") +
            '</head>\n<body>\n'
            '<header>\n' + home_link("help/", "index.html", "Orkige") + '\n'
            '<span class="header-links">'
            '<a href="help/index.html">Documentation</a>'
            '<a href="benchmark.html">Benchmark</a>'
            '<a href="%s">GitHub</a></span>\n</header>\n'
            '<main class="benchmark-page"><article>\n'
            "%s\n"
            "<h1>%s</h1>\n"
            "<p>%s</p>\n<p>%s</p>\n"
            "</article></main>\n"
            "%s\n</body>\n</html>\n") % (
        GITHUB_URL, frame, html.escape(BENCHMARK_HEADING), BENCHMARK_INTRO,
        BENCHMARK_NOTE, footer_html("", pages))


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
header .home {
	font-weight: 700; color: var(--fg); text-decoration: none;
	display: inline-flex; align-items: center; gap: 0.5rem;
}
header .home .logo { height: 1.6rem; width: auto; display: block; }
/* a committed doc image (the README mark): a small inline logo, not full-bleed */
.doc-image { max-width: 128px; height: auto; display: block; margin: 0.2rem 0 1rem; }
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
	display: flex; justify-content: space-between; gap: 1rem;
	flex-wrap: wrap;
}
footer a { color: var(--muted); }
.directory { list-style: none; padding: 0; }
.directory li { margin: 0.55rem 0; }
.directory .snippet { display: block; color: var(--muted);
	font-size: 0.88em; }
header .header-links { margin-left: auto; display: flex; gap: 1.1rem; }
header .header-links a { color: var(--fg); text-decoration: none; }
header .header-links a:hover { color: var(--accent); }
/* the landing page (site root) and the legal pages share this sheet */
.hero {
	padding: 3.4rem 1.6rem 2.6rem; text-align: center;
	border-bottom: 1px solid var(--line);
}
.hero h1 { font-size: 2.5rem; margin: 0 0 0.7rem; }
.hero .tagline {
	max-width: 44rem; margin: 0 auto 1.5rem; color: var(--muted);
	font-size: 1.05rem;
}
.hero .actions {
	display: flex; gap: 0.8rem; justify-content: center; flex-wrap: wrap;
	margin: 0;
}
.action {
	display: inline-block; padding: 0.5rem 1.1rem; border-radius: 6px;
	border: 1px solid var(--line); text-decoration: none; font-weight: 600;
	color: var(--fg);
}
.action:hover { border-color: var(--accent); color: var(--accent); }
.action.primary {
	background: var(--accent); border-color: var(--accent); color: #ffffff;
}
.action.primary:hover { color: #ffffff; }
.landing article { max-width: 58rem; margin: 0 auto; }
.features {
	display: grid; gap: 0.2rem 1.8rem;
	grid-template-columns: repeat(auto-fit, minmax(17rem, 1fr));
}
.feature h3 { margin-bottom: 0.2rem; }
.feature p { margin-top: 0.2rem; color: var(--muted); }
/* the downloads section (landing page): one card per platform, each shipping
   release-page buttons a script may rewrite to the night's real assets */
.downloads {
	margin-top: 2.6rem; padding-top: 1.6rem;
	border-top: 1px solid var(--line);
}
.downloads h2 { border-bottom: none; padding-bottom: 0; }
.downloads-lead, .downloads-foot { color: var(--muted); }
.downloads-foot { font-size: 0.9rem; }
.download-grid {
	display: grid; gap: 1.1rem; margin: 1.4rem 0;
	grid-template-columns: repeat(auto-fit, minmax(17rem, 1fr));
}
.download {
	border: 1px solid var(--line); border-radius: 8px; padding: 1rem 1.1rem;
}
.download.likely { border-color: var(--accent); }
.download h3 {
	margin: 0; display: flex; align-items: baseline; gap: 0.5rem;
	flex-wrap: wrap;
}
.download-arch { color: var(--muted); font-size: 0.82rem; font-weight: 400; }
.download-you {
	color: var(--accent); font-size: 0.7rem; font-weight: 700;
	text-transform: uppercase; letter-spacing: 0.07em;
}
.download-actions {
	display: flex; gap: 0.5rem; flex-wrap: wrap; margin: 0.9rem 0;
}
/* the pair fills the card's width: side by side where it fits, stacked full
   width where it does not - never one button ragged under the other */
.download-actions .action {
	flex: 1 1 auto; text-align: center; padding: 0.42rem 0.9rem;
	font-size: 0.93rem;
}
.download-size { font-weight: 400; opacity: 0.8; }
.download-note { margin: 0; color: var(--muted); font-size: 0.9rem; }
/* the live benchmark page: the embedded player sits in the text column and is
   never wider than the prose - a fixed 16:9 box that scales down with the
   column on narrow screens (aspect-ratio holds the height) */
.benchmark-page { max-width: 58rem; margin: 0 auto; }
.benchmark-page article { max-width: none; }
.player-frame {
	position: relative; width: 100%; aspect-ratio: 16 / 9;
	margin: 0 0 1.6rem; background: #000;
	border: 1px solid var(--line); border-radius: 8px; overflow: hidden;
}
.player-loading {
	position: absolute; inset: 0; display: flex; align-items: center;
	justify-content: center; text-align: center; padding: 1rem;
	color: var(--muted); font-size: 0.95rem;
	/* purely a backdrop message: the iframe paints over it and its onload
	   hides it, but it must never sit between the pointer and the player */
	pointer-events: none;
}
.player {
	position: absolute; inset: 0; width: 100%; height: 100%;
	border: 0; display: block;
}
@media (max-width: 60rem) { nav, .toc { display: none; } }
"""

DOWNLOADS_JS_NAME = "downloads.js"

# The landing page's download buttons, resolved. Everything here is an
# IMPROVEMENT on markup that is already complete: each button ships pointing
# at the release page, and if any step below does not happen - no JavaScript,
# no network, a rate-limited or unrecognised API answer - the page keeps
# exactly the links it was generated with. Nothing is ever emptied or hidden.
# __CONFIG__ is replaced at build time with downloads_config(), so the asset
# patterns and platform rules here are the same strings the Python side
# matches in its selftest. It is a RAW literal, so the regex escapes reach the
# browser as written - and a backslash is a backslash, never a python line
# continuation (the selftest runs the emitted file through a JS parser).
DOWNLOADS_JS = r"""// Orkige downloads - progressive enhancement.
// Hand-written, no dependencies.
// The release assets are named after the version and the commit, so their
// URLs move every night while this page does not. The markup therefore links
// the release page (always correct), and this script asks the releases API
// for tonight's asset URLs and rewrites the buttons in place. Every failure
// path is the same: leave the page as it shipped.
(function () {
	"use strict";
	var CONFIG = __CONFIG__;
	var section = document.getElementById("downloads");
	if (!section) { return; }

	// which card to highlight - never which cards to SHOW: a wrong guess or
	// no guess at all must still leave every platform reachable
	function detectPlatform(userAgent) {
		var text = (userAgent || "").toLowerCase();
		for (var i = 0; i < CONFIG.detect.length; i += 1) {
			if (new RegExp(CONFIG.detect[i][0]).test(text)) {
				return CONFIG.detect[i][1];
			}
		}
		return "";
	}

	function highlightLikelyPlatform() {
		var id = detectPlatform(navigator.userAgent);
		if (!id) { return; }
		var card = section.querySelector('[data-platform="' + id + '"]');
		var heading = card && card.querySelector("h3");
		if (!heading) { return; }
		card.className += " likely";
		var tag = document.createElement("span");
		tag.className = "download-you";
		tag.textContent = "your platform";
		heading.appendChild(tag);
	}

	// an asset filename -> "<platform>/<slot>", "" for everything else (the
	// .sha256 sidecars, the changelog asset, anything a later night adds)
	function matchAsset(name) {
		for (var i = 0; i < CONFIG.assets.length; i += 1) {
			var entry = CONFIG.assets[i];
			if (new RegExp(entry.pattern).test(name)) {
				return entry.platform + "/" + entry.slot;
			}
		}
		return "";
	}

	function megabytes(size) {
		if (!size || size < 1048576) { return ""; }
		return Math.round(size / 1048576) + " MB";
	}

	function describeBuild(release) {
		var pattern = new RegExp("<!--\\s*" + CONFIG.versionMarker +
			":\\s*([^\\s>]+)\\s*-->");
		var found = pattern.exec(release.body || "");
		if (!found) { return ""; }
		var text = "The current build is " + found[1];
		var published = new Date(release.published_at);
		if (release.published_at && !isNaN(published.getTime())) {
			text += ", published " + published.toLocaleDateString();
		}
		return text + ".";
	}

	function annotate(link, text) {
		if (!text) { return; }
		var span = document.createElement("span");
		span.className = "download-size";
		span.textContent = " " + text;
		link.appendChild(span);
	}

	function applyRelease(release) {
		var assets = release.assets || [];
		var byKey = {};
		for (var i = 0; i < assets.length; i += 1) {
			var key = matchAsset(assets[i].name || "");
			if (key && !byKey[key]) { byKey[key] = assets[i]; }
		}
		var links = section.querySelectorAll("a[data-asset]");
		for (var j = 0; j < links.length; j += 1) {
			var asset = byKey[links[j].getAttribute("data-asset")];
			if (!asset || !asset.browser_download_url) {
				// a platform whose build did not produce this shape
				// tonight. The button keeps its release-page link, which
				// is where the notes name what happened - but it stops
				// looking like the recommended action and stops promising
				// a file that is not there.
				links[j].className =
					links[j].className.replace(" primary", "");
				links[j].title = "Not part of this build";
				annotate(links[j], "not in this build");
				continue;
			}
			links[j].href = asset.browser_download_url;
			links[j].title = asset.name;
			annotate(links[j], megabytes(asset.size));
		}
		var build = section.querySelector("#download-build");
		var described = describeBuild(release);
		if (build && described) { build.textContent = described; }
	}

	highlightLikelyPlatform();
	try {
		fetch(CONFIG.api, {
			headers: { "Accept": "application/vnd.github+json" }
		}).then(function (response) {
			// a 403 is the unauthenticated rate limit (60 an hour per
			// address); a 404 is a repository with no nightly yet - both
			// leave the release-page links standing
			return response.ok ? response.json() : null;
		}).then(function (release) {
			if (release) { applyRelease(release); }
		}).catch(function () { /* offline: the page already works */ });
	} catch (error) { /* no fetch at all: the page already works */ }
})();
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
def history_head(root):
    """the commit the release-history page renders from - a build input that is
    not a file, so --if-stale notices a new commit as readily as a new doc.
    "" where there is no history (the page then says so and stays stable)."""
    text, ok = orkige_nightly_package.git_log_history(root, "HEAD", limit=1)
    return text.strip() if ok else ""


def corpus_stamp(root, by_source):
    digest = hashlib.sha256()
    with open(SCRIPT_PATH, "rb") as f:
        digest.update(f.read())
    # the icon drawing is a build input too (it renders the site's identity)
    with open(make_editor_icon.__file__, "rb") as f:
        digest.update(f.read())
    # the changelog composition, and the commit its page renders from
    with open(orkige_nightly_package.__file__, "rb") as f:
        digest.update(f.read())
    digest.update(history_head(root).encode("utf-8"))
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
    help_dir = os.path.join(output_dir, "help")
    if if_stale and os.path.isfile(stamp_path) \
            and os.path.isfile(os.path.join(output_dir, "index.html")) \
            and os.path.isfile(os.path.join(help_dir, "index.html")):
        with open(stamp_path, "r") as f:
            if f.read().strip() == stamp:
                print("make_help_portal: up to date (%d pages)" % len(pages))
                print("make_help_portal: OK %s" % output_dir)
                return 0

    contexts = [render_page(page, by_source, root) for page in pages]
    # the authored corpus AND the links this script writes into the pages it
    # generates itself - one gate, one report
    issues = verify_links(contexts) + verify_generated_links(pages)
    if issues:
        sys.stderr.write("make_help_portal: %d broken internal link(s):\n"
                         % len(issues))
        for issue in issues:
            sys.stderr.write("  BROKEN LINK %s\n" % issue)
        return 1

    os.makedirs(help_dir, exist_ok=True)
    written = {"": set(), "help": set()}

    def write(directory, name, content):
        with open(os.path.join(output_dir, directory, name), "w",
                  encoding="utf-8", newline="\n") as f:
            f.write(content)
        written[directory].add(name)

    for page in pages:
        if page.group == "Legal":
            write("", page.page_id + ".html",
                  legal_shell(page, pages, page.html))
        else:
            write("help", page.page_id + ".html",
                  page_shell(page, pages, page.html))
    write("help", "index.html", page_shell(None, pages, index_body(pages)))
    write("", "index.html", landing_page(pages))
    # the live in-browser benchmark: its own root-level page (the landing page
    # stays embed-free); the /play/ wasm export is staged by the Pages workflow
    write("", "benchmark.html", benchmark_page(pages))
    write("help", "help.css", HELP_CSS)
    write("help", "help.js", HELP_JS)
    # the landing page's download resolver, generated with the ONE asset and
    # platform table the Python matchers above read
    write("help", DOWNLOADS_JS_NAME,
          DOWNLOADS_JS.replace("__CONFIG__", json.dumps(
              downloads_config(), separators=(",", ":"))))
    # the index is the DOCUMENTATION: the legal pages are footer-only by
    # convention, and the release history would put a thousand commit lines in
    # front of the guide a search is looking for
    records = [record for page in pages if not page.unindexed
               for record in page.sections]
    write("help", "search-index.json",
          json.dumps(records, ensure_ascii=False, separators=(",", ":")))
    # the site identity icons (favicon / apple-touch / header logo): rendered
    # from the editor's app-icon drawing at build time into help/ - one master
    # render, scaled to each size the pages reference
    master = make_editor_icon.render_base()
    for size, name in SITE_ICONS:
        make_editor_icon.write_png(os.path.join(help_dir, name),
                                   make_editor_icon.scale_master(master, size),
                                   size)
    # committed repository images a page referenced (the README mark): copy
    # each next to its page so the <img> resolves in the offline site too
    for ctx in contexts:
        for repo_path, directory, name in ctx.images:
            shutil.copyfile(os.path.join(root, repo_path),
                            os.path.join(output_dir, directory, name))
    write("", ".stamp", stamp + "\n")
    # a renamed/removed doc must not leave its stale page behind (the /api/
    # sibling the Pages workflow assembles is not this script's to clean)
    for directory in written:
        for name in os.listdir(os.path.join(output_dir, directory)):
            if name.endswith(".html") and name not in written[directory]:
                os.remove(os.path.join(output_dir, directory, name))
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
[link](other.md#target-section) plus [outside](../README.md), the
[class reference](/api/) and the [live benchmark](/play/).

A committed image ![Logo](logo.png) renders inline; a
![remote badge](https://example.com/b.svg) degrades to its alt text.

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

# the landing page's Downloads section links this doc and one of its
# headings; the synthetic corpus carries a stand-in so the generated-link gate
# has something real to check (and something to take away again, below)
SELFTEST_NIGHTLY = """\
# Nightly builds

Synthetic stand-in for the nightly build documentation.

## What a downloaded build cannot do yet

Body of the limitations section.
"""


def _selftest_downloads():
    """The pure download decisions: which release asset belongs to which
    platform button, and which card a user agent highlights. Both tables are
    emitted into the page's script verbatim, so testing them here is testing
    what the browser runs."""
    token = "2.0.0-nightly.20260731_498a82b2a"
    for name, expected in (
            ("Orkige-macos-%s.dmg" % token, "macos/install"),
            ("Orkige-macos-%s.zip" % token, "macos/portable"),
            ("Orkige-windows-%s-setup.exe" % token, "windows/install"),
            ("Orkige-windows-%s.zip" % token, "windows/portable"),
            ("Orkige-linux-%s.AppImage" % token, "linux/install"),
            ("Orkige-linux-%s.tar.gz" % token, "linux/portable"),
            # an unstamped hand-built artifact still lands on its button
            ("Orkige-linux-unstamped.AppImage", "linux/install"),
            # a checksum sidecar is NOT its archive: the patterns are anchored
            ("Orkige-macos-%s.dmg.sha256" % token, ""),
            ("Orkige-linux-%s.tar.gz.sha256" % token, ""),
            ("Orkige-windows-%s-setup.exe.sha256" % token, ""),
            # the release's own full-history asset, and a stranger
            ("CHANGELOG.md", ""),
            ("Orkige-freebsd-%s.txz" % token, "")):
        assert match_download_asset(name) == expected, name
    # every button the markup ships has exactly one pattern behind it
    slots = ["%s/%s" % (p, s) for p, s, _ in download_asset_slots()]
    assert len(slots) == len(set(slots)) == 6, slots

    for agent, expected in (
            ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit",
             "macos"),
            ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit",
             "windows"),
            ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit", "linux"),
            ("Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:128.0) Gecko",
             "linux"),
            # a phone or tablet has no desktop build to highlight - and an
            # Android agent says "Linux", which must not win
            ("Mozilla/5.0 (Linux; Android 15; Pixel 9) AppleWebKit", ""),
            ("Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)", ""),
            ("Mozilla/5.0 (iPad; CPU OS 18_0 like Mac OS X)", ""),
            # nothing recognisable highlights nothing, and never throws
            ("curl/8.7.1", ""), ("", ""), (None, "")):
        assert detect_download_platform(agent) == expected, agent

    # the config the script is generated with carries those same strings
    config = downloads_config()
    assert config["api"].startswith("https://api.github.com/repos/"), config
    assert config["api"].endswith("/releases/tags/nightly"), config
    assert len(config["assets"]) == 6, config
    assert [rule[1] for rule in config["detect"]] == \
        ["", "macos", "windows", "linux"], config
    json.dumps(config)   # it has to survive the trip into the page verbatim


def _check_site_scripts(out):
    """The site's two hand-written scripts have to PARSE. Both are python
    string literals, so a stray escape (a raw string's backslash, an unbalanced
    quote) is invisible until a browser refuses the file and the page silently
    loses its enhancement - the exact failure this check exists for. The parse
    itself needs a JavaScript engine: where none is installed the shape check
    below still catches the leading-garbage case."""
    scripts = [os.path.join(out, "help", name)
               for name in ("help.js", DOWNLOADS_JS_NAME)]
    for path in scripts:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        assert text.startswith("// Orkige"), \
            "%s must start with its own comment, not %r" % (path, text[:12])
    node = shutil.which("node")
    if node is None:
        return
    for path in scripts:
        result = subprocess.run([node, "--check", path],
                                capture_output=True, text=True)
        assert result.returncode == 0, "%s: %s" % (path, result.stderr.strip())


def _check_site_tags(out):
    """Every emitted page - landing, legal, portal - must nest correctly."""
    for directory in ("", "help"):
        site_dir = os.path.join(out, directory)
        for name in sorted(os.listdir(site_dir)):
            if not name.endswith(".html"):
                continue
            with open(os.path.join(site_dir, name)) as f:
                problems = _TagBalanceChecker().check(f.read())
            assert not problems, "%s/%s: %s" % (directory, name, problems)


def _selftest_synthetic(temp_root):
    os.makedirs(os.path.join(temp_root, "Docs", "legal"))
    with open(os.path.join(temp_root, "README.md"), "w") as f:
        f.write("# Tiny\n\nOverview body.\n")
    with open(os.path.join(temp_root, "Docs", "synthetic.md"), "w") as f:
        f.write(SELFTEST_DOC)
    # a committed image beside the doc: it must be copied into the site and
    # rendered as <img>; a missing/remote one degrades to alt text
    with open(os.path.join(temp_root, "Docs", "logo.png"), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n synthetic image bytes")
    with open(os.path.join(temp_root, "Docs", "other.md"), "w") as f:
        f.write(SELFTEST_OTHER)
    nightly_doc = os.path.join(temp_root, "Docs", "nightly-builds.md")
    with open(nightly_doc, "w") as f:
        f.write(SELFTEST_NIGHTLY)
    with open(os.path.join(temp_root, "Docs", "legal", "imprint.md"),
              "w") as f:
        f.write("# Impressum\n\nSynthetic imprint body, see "
                "[privacy](privacy.md) and [a guide](../synthetic.md).\n")
    with open(os.path.join(temp_root, "Docs", "legal", "privacy.md"),
              "w") as f:
        f.write("# Privacy Notice\n\nSynthetic privacy body with the word "
                "xylophone.\n")
    out = os.path.join(temp_root, "site")
    assert build(temp_root, out) == 0
    with open(os.path.join(out, "help", "synthetic.html")) as f:
        page = f.read()
    assert '<a href="other.html#target-section">link</a>' in page, page
    assert "<code>../README.md</code>" not in page   # resolves to overview
    assert '<a href="overview.html">outside</a>' in page, page
    # the /api/ allowlist: the reference the Pages workflow assembles
    assert '<a href="../api/index.html">class reference</a>' in page, page
    # the /play/ allowlist: the live benchmark export the workflow stages
    assert '<a href="../play/index.html">live benchmark</a>' in page, page
    # a committed image renders as <img> and is copied next to the page; a
    # remote image degrades to its alt text (the portal fetches no artwork)
    assert '<img class="doc-image" src="logo.png" alt="Logo">' in page, page
    assert os.path.isfile(os.path.join(out, "help", "logo.png"))
    assert "remote badge" in page and "example.com" not in page, page
    # the site identity: the favicon links + the header logo, generated icons
    assert 'rel="apple-touch-icon"' in page and FAVICON_PNG in page, page
    assert '<img class="logo" src="%s"' % LOGO_PNG in page, page
    for _size, name in SITE_ICONS:
        icon_path = os.path.join(out, "help", name)
        assert os.path.getsize(icon_path) > 0, name
    assert '<pre><code class="lang-lua">print(&quot;fenced inside' in page
    assert "<ol>" in page and "nested ordered two" in page
    assert "pipe escape: a | b" in page
    assert '<td style="text-align:center">1</td>' in page
    assert '<h2 id="lists-and-fences">' in page
    # the legal pages: root-level, footer-linked from every page, unindexed
    assert '<a href="../imprint.html">Impressum</a>' in page, page
    assert '<a href="../privacy.html">Privacy Notice</a>' in page, page
    with open(os.path.join(out, "imprint.html")) as f:
        imprint = f.read()
    assert "Synthetic imprint body" in imprint
    assert '<a href="privacy.html">privacy</a>' in imprint, imprint
    assert '<a href="help/synthetic.html">a guide</a>' in imprint, imprint
    assert '<a href="imprint.html">Impressum</a>' in imprint  # own footer
    assert 'id="search"' not in imprint          # no search on legal pages
    # the landing page: the site front door linking portal, /api/, GitHub
    with open(os.path.join(out, "index.html")) as f:
        landing = f.read()
    assert 'href="help/index.html"' in landing
    assert 'href="api/index.html"' in landing
    assert GITHUB_URL in landing
    assert '<a href="imprint.html">Impressum</a>' in landing
    # the landing page links to the live benchmark subpage (a nav button) but
    # embeds NO player itself
    assert 'href="benchmark.html">Benchmark' in landing
    assert "player-frame" not in landing
    # the Downloads section: the first hero button jumps to it, and it is
    # COMPLETE in the shipped markup - one card per platform, every button
    # already pointing at the release page. No script has run at this point,
    # and this is the state a visitor without JavaScript keeps.
    assert '<a class="action primary" href="#downloads">Download</a>' \
        in landing
    assert 'id="downloads"' in landing and DOWNLOADS_HEADING in landing
    for platform in DOWNLOAD_PLATFORMS:
        assert 'data-platform="%s"' % platform["id"] in landing, platform["id"]
    buttons = re.findall(r'<a class="action[^"]*" data-asset="([^"]+)" '
                         r'href="([^"]+)"', landing)
    assert [key for key, _href in buttons] == \
        ["%s/%s" % (p, s) for p, s, _ in download_asset_slots()], buttons
    assert all(href == NIGHTLY_RELEASE_URL for _key, href in buttons), buttons
    # the caveats a visitor must not meet unprepared, in the markup itself
    assert "notarized" in landing and "no security prompt" in landing
    assert "Windows protected your PC" in landing, landing
    assert "Run anyway" in landing and "unsigned" in landing
    assert "--appimage-extract-and-run" in landing and "FUSE" in landing
    assert RELEASES_URL in landing and ".sha256" in landing
    # the section's links into the corpus, verified by the generated-link gate
    assert 'href="help/nightly-builds.html#' \
        'what-a-downloaded-build-cannot-do-yet"' in landing, landing
    assert 'href="help/nightly-builds.html"' in landing, landing
    # the resolver is a separate file the page loads LAST, carrying the one
    # asset/platform table the Python matchers above read
    assert '<script src="help/%s"></script>' % DOWNLOADS_JS_NAME in landing
    with open(os.path.join(out, "help", DOWNLOADS_JS_NAME)) as f:
        resolver = f.read()
    assert "__CONFIG__" not in resolver, "the config must be substituted"
    embedded = json.loads(re.search(r'var CONFIG = (\{.*?\});',
                                    resolver).group(1))
    assert embedded == downloads_config(), embedded
    _check_site_scripts(out)
    # the live benchmark page: header, then the 16:9 iframe pointing at /play/,
    # then the context prose
    with open(os.path.join(out, "benchmark.html")) as f:
        benchmark = f.read()
    assert 'src="play/index.html"' in benchmark, benchmark
    assert 'class="player-frame"' in benchmark, benchmark
    assert 'class="player-loading"' in benchmark, benchmark
    _check_site_tags(out)
    with open(os.path.join(out, "help", "search-index.json")) as f:
        records = json.load(f)
    target = [r for r in records if r["anchor"] == "target-section"]
    assert target and "zanzibar" in target[0]["body"]
    assert not any("xylophone" in r["body"] for r in records), \
        "legal pages must stay out of the search index"
    # the release history: a corpus with no git history behind it says so on
    # the page instead of rendering an empty one, and never claims a version
    with open(os.path.join(out, "help", CHANGELOG_PAGE_ID + ".html")) as f:
        changelog = f.read()
    assert "No commit history was available" in changelog, changelog
    assert "No commits to list." in changelog, changelog
    assert "nightly." not in changelog, changelog
    # it is in the nav (every portal page carries the sidebar) and OUT of the
    # search index - a commit log must not drown the documentation
    assert '<a href="changelog.html">Changelog</a>' in page, page
    assert not any(r["page"] == "changelog.html" for r in records), \
        "the release history must stay out of the search index"

    # staleness: an unchanged corpus is a no-op, a touched source rebuilds
    stamp_file = os.path.join(out, ".stamp")
    before = open(stamp_file).read()
    assert build(temp_root, out, if_stale=True) == 0
    assert open(stamp_file).read() == before
    with open(os.path.join(temp_root, "Docs", "other.md"), "a") as f:
        f.write("\nMore prose.\n")
    assert build(temp_root, out, if_stale=True) == 0
    assert open(stamp_file).read() != before

    # the GENERATED pages are held to the same gate as authored prose: retitle
    # the heading the Downloads section deep-links and the build fails naming
    # it, rather than shipping a link that 404s on the live site
    with open(nightly_doc, "w") as f:
        f.write(SELFTEST_NIGHTLY.replace(
            "## What a downloaded build cannot do yet", "## Limitations"))
    captured = io.StringIO()
    real_stderr = sys.stderr
    sys.stderr = captured
    try:
        result = build(temp_root, out)
    finally:
        sys.stderr = real_stderr
    assert result == 1
    report = captured.getvalue()
    assert "Util/make_help_portal.py:" in report, report
    assert "Docs/nightly-builds.md#what-a-downloaded-build-cannot-do-yet" \
        in report, report
    with open(nightly_doc, "w") as f:
        f.write(SELFTEST_NIGHTLY)
    assert build(temp_root, out) == 0

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
    assert any(p.page_id == "imprint" and p.group == "Legal" for p in pages)
    assert any(p.page_id == "privacy" and p.group == "Legal" for p in pages)
    for page in pages:
        path = os.path.join(out, page.directory, page.page_id + ".html")
        assert os.path.getsize(path) > 0, page.page_id
    _check_site_tags(out)
    with open(os.path.join(out, "help", "search-index.json")) as f:
        records = json.load(f)
    assert len(records) > 100, "the corpus should index >100 sections"
    hits = [r for r in records if "script components" in r["heading"].lower()]
    assert hits, "lua-api's Script components section must be indexed"
    assert not any(r["page"] in ("imprint.html", "privacy.html")
                   for r in records), "legal pages must not be indexed"
    with open(os.path.join(out, "help", "lua-api.html")) as f:
        lua_page = f.read()
    assert 'id="script-components"' in lua_page
    assert '<a href="../api/index.html">API Reference</a>' in lua_page
    # the deployed front door: landing -> portal / getting started / api
    with open(os.path.join(out, "index.html")) as f:
        landing = f.read()
    assert 'href="help/index.html"' in landing
    assert 'href="help/getting-started.html"' in landing
    assert 'href="api/index.html"' in landing
    assert 'href="benchmark.html">Benchmark' in landing
    assert '<a href="imprint.html">Impressum</a>' in landing
    # the Downloads section against the REAL corpus: the doc page and heading
    # it deep-links exist (the generated-link gate proved it by returning 0),
    # and every button ships as a release-page link
    assert 'id="downloads"' in landing
    assert 'href="help/nightly-builds.html#' \
        'what-a-downloaded-build-cannot-do-yet"' in landing
    assert landing.count('href="%s"' % NIGHTLY_RELEASE_URL) == 6, landing
    with open(os.path.join(out, "help", "nightly-builds.html")) as f:
        nightly = f.read()
    assert 'id="what-a-downloaded-build-cannot-do-yet"' in nightly
    _check_site_scripts(out)
    # the site identity: favicon + header logo on the landing and portal pages
    assert 'rel="apple-touch-icon"' in landing and '<img class="logo"' in landing
    # the README mark renders in the portal overview (copied beside the page)
    with open(os.path.join(out, "help", "overview.html")) as f:
        overview = f.read()
    assert '<img class="doc-image" src="orkige_icon.png"' in overview, overview
    assert os.path.getsize(os.path.join(out, "help", "orkige_icon.png")) > 0
    assert 'rel="apple-touch-icon"' in overview and '<img class="logo"' in overview
    # the live benchmark page: the 16:9 player embed at /play/ + the prose,
    # the landing page's fifth nav button links here
    with open(os.path.join(out, "benchmark.html")) as f:
        benchmark = f.read()
    assert 'src="play/index.html"' in benchmark
    assert 'class="player-frame"' in benchmark
    assert BENCHMARK_HEADING in benchmark
    # the release history off the REAL repository: every day is headed by the
    # ordered version a build of it carries, newest first, and each entry
    # names its commit
    with open(os.path.join(out, "help", CHANGELOG_PAGE_ID + ".html")) as f:
        changelog = f.read()
    days = re.findall(r'<h2 id="[^"]*">([^<]+)</h2>', changelog)
    assert days, changelog[:2000]
    # MORE than one day is only a fair expectation of a clone that carries the
    # history. The jobs that PUBLISH the site fetch the full log; every build
    # job checks out shallow, and a clone with one day in it is not a broken
    # page - the page says what it was generated from.
    if not orkige_nightly_package.git_is_shallow(ROOT):
        assert len(days) > 1, days[:5]
    assert re.match(r"^\d+\.\d+\.\d+-nightly\.\d{8}\+[0-9a-f]{7,}$", days[0]), \
        days[0]
    assert days == sorted(days, reverse=True), days[:5]
    assert re.search(r"\(<code>[0-9a-f]{7,}</code>\)", changelog), \
        changelog[:2000]
    assert "No commit history was available" not in changelog
    assert not any(r["page"] == "changelog.html" for r in records), \
        "the release history must stay out of the search index"
    with open(os.path.join(out, "imprint.html")) as f:
        imprint = f.read()
    assert "Impressum" in imprint and "orkitec" in imprint
    with open(os.path.join(out, "privacy.html")) as f:
        privacy = f.read()
    assert "Privacy Notice" in privacy
    print("make_help_portal selftest: real corpus OK (%d pages, %d records)"
          % (len(pages), len(records)))


def cmd_selftest():
    _selftest_downloads()
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
