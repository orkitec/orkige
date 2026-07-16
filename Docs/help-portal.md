# Help portal

Help > **Orkige Help** in the editor opens a searchable, fully offline
documentation site in the default browser. The site is generated from the
repository's committed docs by `Util/make_help_portal.py` (python3 stdlib
only, like every `Util/` tool), cached under `<build>/help_portal` and served
on a loopback HTTP port owned by the editor. Nothing is vendored and nothing
is fetched from the network — one hand-written stylesheet, one hand-written
search script, a generated JSON search index.

- **Sources**: the repository `README.md` (the Overview page), every
  `Docs/*.md` (this page included), and any `projects/*/README.md` (picked up
  automatically once a project documents itself).
- **Search**: the box on every page ranks matches title > heading > body and
  jumps straight to the section anchor.
- **Freshness**: the generator stamps the site with a sha256 over all sources
  (and itself); reopening Help after the docs changed regenerates, otherwise
  serving is instant.

## How docs authors see their pages

The portal PRESENTS the corpus — write normal repository markdown and it
renders as written. Because python has no stdlib markdown library, the
renderer is a subset tailored to what the corpus actually uses:

- ATX headings (`#`–`######`), paragraphs, horizontal rules.
- Nested unordered/ordered lists, continuation lines, and fenced code blocks
  indented inside list items.
- Fenced code blocks with a language tag (rendered as plain preformatted
  code — `mermaid` blocks included, which appear as their source text).
- Pipe tables with the `\|` cell escape and `:---:` alignment.
- Inline code, `**bold**`, `*italic*`, links and images.
- HTML comments (the GENERATED block markers) are stripped from the output.

Heading anchors use the familiar slug rules (lower-case, punctuation dropped,
every space becomes a dash — `## Show / hide transitions` anchors as
`#show--hide-transitions`), so `[text](gui.md#section)` links written for the
repository browser work in the portal unchanged.

Link semantics keep the site offline and honest:

- A link to another corpus page (`lua-api.md`, `../README.md`, optionally
  with `#anchor`) becomes a portal page link — and is **verified**: a target
  page or heading anchor that does not exist **fails the build**, reported as
  `BROKEN LINK <file>:<line> -> <target>` so the author can fix the exact
  spot. The `make_help_portal_selftest` ctest keeps the real corpus at zero
  broken links.
- A link to a repository file outside the corpus (a header, `LICENSE`)
  renders as inline code — there is no page to link to.
- External `http(s)` links stay links; images degrade to their alt text (the
  portal ships no images and never fetches remote ones).

## The generator

```sh
python3 Util/make_help_portal.py --output <dir>              # build the site
python3 Util/make_help_portal.py --output <dir> --if-stale   # skip when current
python3 Util/make_help_portal.py --selftest                  # the unit ctest
```

Output: one `<page>.html` per source (shared header with the search box, a
grouped sidebar, an on-page h2 rail), `index.html` (the directory),
`help.css`, `help.js`, `search-index.json` (one record per heading section:
page, title, heading, anchor, body text) and the `.stamp` staleness hash.
New `Docs/*.md` files appear automatically, ordered by the `PREFERRED_ORDER`
list in the script (unlisted pages append alphabetically).

## Delivery in the editor

The Help menu item (native macOS menu and the ImGui menu bar) sets a request
flag; the frame loop preflights the python3 toolchain, runs the generator
asynchronously (`[help]` Console lines, the export-job pattern), then serves
the site on the editor's dedicated loopback `HttpServer` instance and opens
the browser via `SDL_OpenURL` — never on automated runs. The help server is
deliberately **not** the Play-in-Browser server: that port doubles as the
browser play session's debug-WebSocket front door and swaps its doc root per
play, while the help site must outlive any play session. Both share the same
static-file path jail and content-type table (`EditorBrowserServe.cpp`).

Verified by `make_help_portal_selftest` (unit: synthetic + real corpus,
balanced HTML, zero broken links, stamp logic) and `editor_help_portal`
(integration: the menu action's flow end to end — generate, serve, fetch
index/page/search index/assets off the port, path-jail 404s).
