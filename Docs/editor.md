# Editor panels

Reference for the Orkige editor's dockable panels beyond the core Scene /
Hierarchy / Inspector / Console set. Each panel opens from the **View** menu and
docks freely; this document covers the behaviours that are not obvious from the
UI.

What the editor app carries when it is copied to another machine, how it
resolves those resources and where it writes its own state:
[Editor distribution](editor-distribution.md). How it keeps itself current
afterwards — the setting, the once-a-day check, and the swap that happens on
restart rather than mid-session: [Keeping the editor current](editor-updates.md).

## Source Control

The **Source Control** panel drives the open project's git working tree. It
opens from the View menu and docks as a tab in the bottom group beside Console,
Stats and Debug. It talks to git through the command-line client — the same
tool the developer already uses — so it inherits the machine's git
configuration, hooks and credentials with no extra dependency.

### What it shows

The panel resolves the git repository that contains the open project (the
project may sit anywhere inside a larger repo) and shows:

- the current **branch** and, when an upstream is configured, the **ahead /
  behind** counts;
- the changed files, grouped into **Staged**, **Changes** (tracked files with
  unstaged edits), **Untracked** and **Conflicts**;
- a **commit message** box with a Commit button, and a **Push** button badged
  with the ahead count.

Each file row carries a status letter, a colour tint and a stage / unstage
control. Clicking a row opens that file in the embedded code editor, where the
change-marker gutter shows the very edits the row is reporting. Group headers
carry **Stage All** / **Unstage All**.

The status list refreshes when the panel gains focus, when the Refresh button is
pressed, after every operation the panel issues, and — event-driven, without any
background polling — at the moments the editor knows the tree changed: a document
or scene **save**, an Asset-browser **import / create / delete / rename**, and
**Play stop** (the game may have written a save file). A change made entirely
outside the editor (git on the command line) is picked up on the next focus or
Refresh.

### Operations are asynchronous

Commit and push can take real time (a pre-commit hook, a network round-trip), so
every operation runs on a worker thread with a "working…" indicator; the result
marshals back to the UI on the next frame. Only one operation runs at a time and
the controls disable while it is in flight. The commit and push subprocesses
capture stdout **and** stderr, so a commit-msg hook rejection or a push
authentication / network error is surfaced verbatim in the panel's status strip
rather than being swallowed.

The Commit button is disabled while the message is empty or nothing is staged.
A branch **with** an upstream shows a **Push** button (disabled when it is not
ahead); a branch **without** an upstream shows **Publish branch** instead, which
runs `git push -u origin <branch>` to upload it and set the upstream so later
pushes work. A detached HEAD or a branch with no commits yet shows a disabled
Push with an explanatory tooltip.

### Discarding changes

A tracked, modified file offers a **Discard changes** action (the trash icon).
This resets the file to its committed content (`git checkout HEAD -- <path>`),
which is destructive: saved-but-uncommitted edits are lost. A confirmation
dialog names the file first. If the file is open in the embedded editor, its
buffer reloads from disk after the discard so the open document never shows stale
content. Untracked files offer no discard action — deleting a brand-new file
from a source-control panel is a footgun, and file deletion belongs to the Asset
browser.

There are three distinct "undo the change" operations in the editor, and they
are deliberately different:

- **Editor Revert** (a code-editor document's Revert button) reloads the file
  from disk, discarding unsaved editor edits — it does not touch git.
- **Gutter Revert Hunk** (the change-marker gutter) rewinds one hunk of the live
  buffer to the git-index baseline; it is buffer-only and undoable with the
  editor's own undo, and writes nothing until the file is saved.
- **Source Control Discard changes** resets the file on disk to its committed
  version; it is destructive, always confirmed, and reloads any open document.

### Degradations

- When the open project is not inside a git repository, or git is not available
  on `PATH`, the panel shows a one-line empty state and does nothing else —
  silently, no error spam.
- During automated / headless runs the panel performs no git subprocess at all
  (the same pollution-hygiene rule the rest of the editor follows); the panel's
  self-check drives the git seam directly against a throwaway temporary repo.

### No MCP verbs — by design

Stage, commit, push and discard are **not** exposed as MCP tools. This is a
deliberate exception to the rule that every editor feature is reachable over
MCP: agents are forbidden from committing on the developer's behalf, and an MCP
tool would launder that prohibition. The read-only status could be exposed as a
tool in the future; the mutations will not be.

## Tile Palette and the grid-paint tool

Level authoring in 2D mode: the **Tile Palette** panel arms a paintable asset and
the **Paint** tool (`B`) paints and erases tiles snapped to a grid.

Two occupant kinds go through ONE seam
(`EditorCore::paintTileAtCell` / `findTileAtCell` / `eraseTileAtCell`, selected by
`EditorPaintDesc::kind`):

- a **prefab** tile instantiates its `.oprefab` subtree. Open edges become
  suppressed prefab wall children plus a `TileComponent.openEdges` stamp (the
  wall-local convention lives in `TileComponent::EDGE_WALL_LOCAL_IDS`).
- a **bare-asset** tile is painted straight from a **texture** (a grid-cell
  `SpriteComponent` quad) or an **`.oshape`** (a `VectorShapeComponent`), with no
  prefab file generated. The tile carries a `TileComponent` stamping the source
  asset's id (`TileComponent.sourceAssetId`).

The palette lists all three kinds (`wall_block (prefab)` beside a bare `grass`).
Look propagation for a bare tile is the shared asset itself: edit the texture or
shape and every painted tile follows — there is no per-tile prefab to re-apply.

The cell size comes from a scene `LevelComponent`, falling back to the translate
snap step. A whole stroke folds into ONE undo step
(`CompositeCommand::mergeWith`), and erase/replace is subtree-safe and works
across kinds (`DeleteSubtreeCommand`). **File > Add Scene to Level Sequence**
appends the open scene to `levels.olevels`.

Agents reach the same seam over MCP: `list_paintable_assets` (alias
`list_paint_prefabs`) / `paint_asset` (alias `paint_prefab`, which accepts a
texture or shape too) / `erase_cell` / `add_scene_to_levels`. Verified by the
`editor_level_paint` ctest (prefab and bare sprite/shape tiles, a mixed grid,
paint → save → reload → PLAYS) on both flavors, plus the `editor_control` MCP
bare-tile leg.

## Asset browser git badges

The Asset browser reads the **same** cached status snapshot the Source Control
panel computes — one git invocation feeds both surfaces. A dirty file shows a
small coloured dot at the trailing edge of its row: green for untracked, amber
for a staged or modified file, red for a conflict. A folder shows a neutral dot
when any file beneath it is dirty (aggregated purely from the snapshot's path
list). The badges follow the same cadence and degradations as the panel: no
repository, no git, or an automated run means no badges, silently.
