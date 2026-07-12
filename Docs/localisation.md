# Localisation

Orkige localises game strings through XLIFF 1.2, the industry-standard
translation interchange format. A project keeps its strings in a `loc/`
directory of `.xlf` files; the runtime loads them into the string table and
resolves keys at UI-build time, and `Util/orkige_loc.py` keeps the registry in
sync with the code.

This is the current-state reference for the tooling and file format. The
runtime API (`loc()` and the `locale` table) is documented in
[lua-api.md](lua-api.md).

## What is translatable

Two sources of truth, both parsed grammar-aware:

- **`.oui` declarative UI**: a `text = @key` entry, or a `@`-prefixed part of a
  pipe-separated `items = ...` list. A leading `@` on those two properties means
  "look the rest up in the string table" (`GuiFactory::resolveText`); every
  other property is literal, so a stray `@` elsewhere is never mistaken for a
  key.
- **Lua under `scripts/`**: a `loc("key")` / `loc('key', arg…)` call with a
  string-literal first argument.

A key whose argument is dynamic (`loc(someVar)`) can not be extracted by nature;
the runtime's once-per-key miss log is the net for those. Engine and editor
tool strings are developer-facing English and are deliberately not scanned.

## File layout

```
<project>/
  loc/
    en.xlf        # source-language registry (source-only, the authoring surface)
    de.xlf        # one file per target language, BCP-47 names (de, pt-BR, …)
    en-XA.xlf     # generated pseudo-locale (committed like any target)
  project.orkproj # <Setting key="localisation" value="loc"/>  -> the directory
```

The manifest `localisation` setting names the `loc/` **directory** (the
config-asset convention, project-relative). Adding a language is dropping a file
in `loc/` — no manifest edit. The runtime scans the directory for `*.xlf` and
loads every language.

## Document contract (XLIFF 1.2)

One `<file>` per document, `urn:oasis:names:tc:xliff:document:1.2`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<xliff version="1.2" xmlns="urn:oasis:names:tc:xliff:document:1.2">
  <file original="orkige-strings" source-language="en" target-language="de"
        datatype="plaintext">
    <header>
      <tool tool-id="orkige_loc" tool-name="orkige_loc" tool-version="1"/>
    </header>
    <body>
      <trans-unit id="menu.play" resname="menu.play" xml:space="preserve">
        <source>Play</source>
        <target state="translated">Spielen</target>
      </trans-unit>
      <trans-unit id="hud.score" resname="hud.score" xml:space="preserve">
        <source>Score: <x id="0" ctype="x-orkige-arg"/></source>
        <target state="translated">Punkte: <x id="0" ctype="x-orkige-arg"/></target>
      </trans-unit>
    </body>
  </file>
</xliff>
```

- **`id` = `resname` = the key.** Dotted identifiers (`menu.play`) are valid.
  The loader keys on `resname`, falling back to `id`.
- **`xml:space="preserve"` on every trans-unit** so leading/trailing and
  internal spacing survives CAT-tool round-trips.
- **`original` and `datatype`** are required by the 1.2 schema and always
  emitted; their values are not load-bearing.
- **`loc/en.xlf`** is the same shape with `target-language` and `<target>`
  omitted — a valid 1.2 source-only file. It is the canonical key set every
  other file is synced against, and where source text and developer `<note>`s
  are authored. It also carries the occurrence locations
  (`<context-group purpose="location">`).

### Placeholders

`%%N%%` is the positional-argument syntax everywhere an author or the runtime
sees it (scripts and `.oui`). In the XLIFF each `%%N%%` is carried as an inline
`<x id="N" ctype="x-orkige-arg"/>` code — the representation CAT tools protect
from edit and tag-parity-QA automatically, so a dropped placeholder is a
tool-caught error rather than a runtime surprise. The conversion is total and
lossless (the tool converts on write, the loader converts on parse), and the
inline codes may be **reordered** in a target because the `id` carries the
argument index. Nothing an author writes ever contains an `<x/>` — `%%N%%` is
the only syntax on the authoring side.

### Translation state

A `<target>` is **usable** when it is present, non-empty, and its `state` is
absent or one of `translated`, `reviewed`, `needs-review-*`, `final`,
`signed-off`. `state` of `new` / `needs-translation` / `needs-adaptation` /
`needs-l10n`, or an absent/empty target, falls back to the `<source>` in the
same unit (self-contained), then to the key itself, so an untranslated or
unlocalised build stays readable.

## The `orkige_loc.py` tool

```sh
python3 Util/orkige_loc.py --write  <project>            # extract + refresh loc/
python3 Util/orkige_loc.py --write  <project> --pseudo   # …and refresh en-XA
python3 Util/orkige_loc.py --pseudo <project>            # regenerate en-XA only
python3 Util/orkige_loc.py --check  <project>            # the gate (CI)
python3 Util/orkige_loc.py --selftest                    # self-contained tests
```

`--write` scans the project, refreshes `loc/en.xlf` and every target file, and
is idempotent (a second pass produces byte-identical files). The output is
deterministic: units are emitted in sorted-key order, locations sorted.

### Merge semantics

| Event | `loc/en.xlf` | target files |
|---|---|---|
| New key referenced | add a unit with an empty `<source>` and a `TODO: source text` note, plus its `<context-group>` location(s) | mirror it in source-only (no `<target>`) |
| Key vanished from all sources | delete the unit (git history is the archive); listed in the output | delete; listed |
| Source text edited in `en.xlf` | (en.xlf **is** the edit) | rewrite the unit's `<source>` to match, **keep** the old `<target>`, set `state="needs-translation"` (the stale translation stays visible to the translator; the runtime falls back to the new source) |
| Occurrence moved | refresh the `<context-group>` location | — |
| Translator returns a file | — | taken as-is; `--check` validates key-set sync + placeholder parity |

### `--check` (the gate)

Fails, listing each offender, on: a referenced key missing from `en.xlf`; an
orphan unit; an empty `<source>` in `en.xlf`; a duplicate key; a target whose
key set is out of sync; a target whose `<source>` is stale relative to the
registry; and a placeholder-parity violation (a usable target whose inline-code
id set differs from its source's). This is the `loc_currency` ctest — the
`docs_currency` twin — run on the seeded `samples/hello_orkige` project.

### Pseudo-localisation (`--pseudo`)

Generates `loc/en-XA.xlf`, a committed pseudo-locale from the English sources:
letters are replaced with font-safe accented variants (Latin-1 / Latin
Extended-A only, all present in the default Nunito face — so pseudo never renders
tofu), the text is expanded ~35% with middle-dot padding, and end-fenced with
`«»` guards. Placeholders pass through untouched, so tag parity holds by
construction. Booting a game in `en-XA` reveals truncation, clipped ends and
non-elastic layouts at a glance.

## Boundaries

- **Plurals**: XLIFF 1.2 has no plural mechanism. The convention is one key per
  form (`hud.lives.one` / `hud.lives.other`), selected in Lua.
- **Fonts**: the default Nunito face covers Latin (including Vietnamese) and the
  common Cyrillic core, which ship working with no change. Greek / CJK need a
  per-project font registered via a `.ogui` `[Font.N] ttf` entry (lazy glyph
  paging bakes the needed glyphs on demand). Font fallback chains (mixed-script
  strings in one font entry) and CJK line-breaking are not provided; label wrap
  is space-based.
- **RTL and complex shaping** (Arabic, Hebrew, Indic) are out of scope: the text
  stack does no bidi reordering and no contextual shaping.
