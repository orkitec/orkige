# Pak mounting (filesystem)

A **pak** is a zip mounted as read-only content: the engine registers its
entries with the resource system so a scene, texture, sound or script inside it
resolves **exactly like a loose file**. It is backend-neutral, and its primary
purpose — reading an Android APK's assets in place — is the default on mobile.

## The one call

```cpp
render->mountPak(pakPath);                    // mount the whole zip
render->mountPak(apkPath, "assets/");         // mount a sub-tree, prefix stripped
render->mountPak(pakPath, "game/", group);    // ...into a named resource group
render->unmountPak(pakPath, "game/", group);  // idempotent
```

- `mountPak(path[, mountPoint][, group])` lives on the `engine_render`
  `RenderSystem` facade, so application code stays backend-free.
- `mountPoint` names a **sub-tree to expose with its prefix stripped**: an APK
  whose media lives under `assets/` mounts with `"assets/"` and every entry then
  resolves by its bare name (`Media/foo.png`, `scene.oscene`, …). An empty mount
  point mounts the whole zip. Multiple paks (and multiple sub-trees of one zip)
  can be mounted at once.
- `LT_ZIP` on `addResourceLocation` also works on both flavors — it is the
  whole-zip case without the sub-tree remap.

## Why a bespoke reader

The reuse rule is "don't reinvent the wheel — read zips with what the Ogres
ship." Classic OGRE does ship a zziplib-backed `Zip` archive. **The Ogre-Next
build ships no zip support** (it has no zziplib in its dependency closure, so its
`ZipArchiveFactory` is never registered). Rather than vendor a whole zip library
or force a heavy port rebuild, the engine wraps the **zlib that is already in the
closure** (classic pulls it through zziplib, Ogre-Next depends on it directly) in
a small reader used **uniformly on both flavors** — one code path, identical
behaviour, no new dependency.

- `engine_filesystem/MiniZip` — a renderer-free zip reader: it parses the
  central directory and reads any entry (**STORED** verbatim, **DEFLATE**
  inflated via zlib). It opens the file per read, so a large APK is never held
  whole in memory. Pure and headless-unit-tested (`MiniZipTests`).
- `engine_filesystem/PakArchive` — adapts `MiniZip` to the `Ogre::Archive`
  interface, applying the mount-point sub-tree remap. The one const-ness
  difference between the two Ogre flavors' `Archive` interface is bridged by a
  single alias, so the file compiles unchanged on both.
- `engine_filesystem/PakMount` — the facade-facing seam: `normalizeMountPoint`
  (pure) turns a mount point into a prefix, and `mount`/`unmount` register the
  pak archive factory with `Ogre::ArchiveManager` and add/remove the resource
  location. `engine_filesystem` is a sanctioned `Ogre::` zone
  (`Util/ogre_containment.json`).

Scenes and the project manifest normally load with `fopen` (tinyxml2), which a
zip entry has no handle for; `SceneSerializer::loadSceneFromString` reads a scene
**through the resource system** (`RenderSystem::readResourceText`) and parses it
in memory — the path a mounted scene takes.

## STORED vs DEFLATE

Mounting reads entries in place, so a mounted zip's content should be **STORED**
(uncompressed): a stored entry is read/streamed without inflating it into RAM,
and it is randomly seekable (the OGG-music streaming case). A DEFLATE entry still
*works* (MiniZip inflates it), but it must be fully inflated first, so mounting
loses its point. This is why the Android `stored` mode packs assets uncompressed.

## Android: mount the APK instead of extracting everything

The manifest Setting `export.android.assets` is a **buildsize vs. performance**
choice (see [store-release.md](store-release.md#assets-stored-vs-compressed)):

- **`stored`** (the default): the exporter packs the APK's `assets/`
  **uncompressed** and drops an `orkige_mount.txt` marker. At boot the player
  resolves its own APK path (JNI `Context.getPackageCodePath`) and **mounts** the
  APK's bulk game media (textures/audio/meshes — the majority of the bytes) in
  place, one flat mount per media directory so bare-name resolution matches the
  loose-file registration. Only the small `fopen`-consumed tree (project
  manifest, `scenes/`, `scripts/`, config) and the engine shader/font media
  (a directory tree the shader loaders want) are still materialised. If the APK
  path or the marker is missing, the run falls back to full extraction — the
  always-safe path.
- **`compressed`**: assets are deflated for a smaller download and extracted to
  the app files dir on first launch.

The App Bundle (`.aab`) path keeps the assets uncompressed in bundletool's
generated APKs via a `BundleConfig` `uncompressedGlob` (`build_aab.sh`).

## Script loading via the archive read

Lua **scripts load through the resource system**, so a script mounted inside a
pak/APK loads **in place** — no `fopen`. The seam is deliberately GENERAL:
scripts are the first consumer, scenes / the project manifest / the
localisation string table are the intended next ones, with no interface change.

- `core_filesystem/ResourceReader` — a PURE core interface
  (`bool readText(name, out)`) with **zero** dependency beyond core. A core
  loader (a script today) depends only on this; it never names the renderer.
- `core_filesystem/ResourceAccess` — the process-wide provider: a settable,
  **non-owning** `ResourceReader*`. A loader reaches the injected reader through
  it WITHOUT threading a pointer through every load call — the same
  process-wide-accessor shape the engine already uses for its singletons. This
  is why it is a provider and not per-loader injection: the design goal is MANY
  consumers, and the alternative (a `ResourceReader*` parameter on every load
  call plus every loader's constructor) would touch a large surface for no gain.
  **CONTRACT: when unset (`reader() == nullptr`), or when a reader MISSES a
  name, the caller falls back to its own `fopen` path** — so headless core tests
  and loose-file dev keep working with no provider installed.
- `engine_runtime/RenderResourceReader` — the engine implementation: it reads a
  name through `RenderSystem::readResourceText` (the archive-aware
  `ResourceGroupManager::openResource` across loose files AND mounted paks/APKs),
  so it is backend-neutral and works on both flavors. `AppHost` installs it into
  `ResourceAccess` at boot **after `initialiseResourceGroups`** (every mount a
  host registered is live) and de-registers it FIRST at teardown (no late read
  routes into a dying renderer).
- `ScriptRuntime::loadScriptInstance` (both the initial load and the play-time
  hot-reload) AND `ScriptRuntime::readExportedProperties` are **archive-first**:
  they try the reader by the script's name, and on a hit load the source from
  memory (`safe_script` with the per-instance sandbox); on no-reader / a miss
  they fall back to the on-disk `safe_script_file` path.
- For `readResourceText` to resolve a project-relative name like
  `scripts/player.lua` by sub-path, the player registers the **project content
  root as ONE resource location** at project load. This is deliberately ONE root
  location, not a hand-picked list of content subfolders (that list drifting
  from reality is exactly why `scripts/` was never registered) — so every
  content folder (`scripts/`, `scenes/`, config + the manifest, and anything
  added later) resolves by its project-relative path with no code change. A
  mounted pak/APK entry and a loose file then resolve by the SAME name. The root
  is registered **non-recursively**: Ogre resolves a sub-path name against a
  location on demand (a filesystem probe), so the content root is never walked
  or indexed — it never descends into derived / non-content dirs (`builds/`,
  `native/` build trees, `.git`), so no build junk is indexed and no exclusion
  list is needed. Bulk media (textures referenced by BARE name) keeps its own
  flat per-folder registration. On an exported bundle the payload root is already
  only content, so the same one-location registration applies cleanly.

**What routes through the archive vs. what still extracts.** Only scripts read
through the archive. **Scenes, the project manifest and the localisation string
table still load with `fopen`**, so the Android `stored` mode still extracts
that small "fopen tree" (manifest / `scenes/` / `scripts/` / config + the
shader/font media) — the bulk game media is already mounted in place (above).
Scripts READ in place from a mounted pak; the Android first-launch extraction
stays as a working fallback, and migrating scenes/config/localisation to the
same reader is what would let it be removed.

Verified by `player_pak_script_selfcheck` (both flavors — a path-bound
`ScriptComponent` whose script lives ONLY inside a mounted pak, with no
`--project` and no loose file, loads and runs) and the headless
`ResourceReaderTests` (the provider seam, the memory-load path, and the
fall-back-when-unset / on-miss paths; inert in `ORKIGE_SCRIPTING=OFF`).

## The mount-versus-extract rule

Two packaged platforms mount an archive instead of unpacking it — an Android
APK left uncompressed (`export.android.assets=stored`) and a browser export's
`game.pak`. They share ONE decision function,
`PlayerBundle::isMountedMediaPath` (`engine_runtime/PlayerRuntime.h`), so the
split cannot drift between them. It answers a package-relative path, and the
package root is also the extraction destination root, so both sides agree by
construction.

**A file may be mounted only when every runtime reader of it goes through the
resource system.** Two things disqualify it:

- **Opened by path** — `fopen` / tinyxml2 / `std::ifstream`. A zip entry has no
  file handle, so such a reader simply sees nothing.
- **Discovered by directory enumeration** — a mounted entry is not a directory
  entry, so a scanner walking the tree never finds it.

Only the bulk media sub-trees are mount candidates at all (`project/assets/`,
`assets/`, `jumper_media/` — the textures/audio/meshes referenced by resource
name, the majority of the bytes). Everything else in a package is written out
wholesale. The exclusions are keyed on the **extension**, not on where a kind
conventionally lives: a manifest points at its config assets by an arbitrary
path string, so convention must not be the only thing keeping a by-path reader
out of the archive.

| Extension | Reader | Silent failure if mounted |
|---|---|---|
| `.oprefab` | `PrefabSerializer` → `XMLArchive` (tinyxml2, by path) | every prefab instance loads childless |
| `.orkmeta` | `AssetDatabase::readMetaFile`/`readImportSettings` (by path) **and** `scanDirectory` (`recursive_directory_iterator`) | id resolution dies; texture import settings lost |
| `.oscene` | `SceneSerializer::loadScene` (by path); a mid-play level switch has no in-memory road | the level never loads |
| `.orkproj` | `Project::load` (tinyxml2, by path) | no project |
| `.olevels` `.oactions` `.olayers` | `LevelSequence` / `InputActionMap` / `PhysicsWorld::LayerConfig`, all `XMLArchive` by path | degrades into built-in defaults: one level, stock keybinds, collide-with-all |
| `.xlf` | `StringTable` — by path **and** `directory_iterator` | every string echoes its own key |
| everything else | `readResourceText` / `openResource` / a resource-name texture-mesh-sound load | — (**mount**) |

The mounted set is therefore textures, meshes, audio, and the text assets
`.omat` `.oshape` `.omesh` `.oanim` `.oatlas` `.osfx` `.sfs` `.oui` `.ogui`
`.lua`. In practice the excluded kinds below `.orkmeta` also sit outside the
media sub-trees by convention (`scenes/`, the project root, `loc/`), so listing
them changes nothing for a conventionally laid-out project — it removes the
dependence on that convention holding.

Scripts are the one kind that reads **either** way: `ScriptRuntime` tries the
resource reader first and falls back to `ifstream`, so a `.lua` mounts safely.
Note that `ScriptComponentRegistry::scanProject` discovers `*.component.lua`
kinds by walking `scripts/` — a component script placed under `assets/` would
mount fine but never register.

A new file kind read with `fopen` belongs in the exclusion list **in the same
change**, with a case in `PlayerBundleTests`.

**Asset ids under a mount.** Sidecars are materialised, but their assets are
not, so a directory walk finds `.orkmeta` files whose asset file does not exist
as a loose file. `AssetDatabase::refresh` reads that correctly per mode: the
editor's import mode owns the directory, so such a sidecar is a genuine orphan
and is dropped; a runtime's **read-only** refresh does not own the directory and
takes the SIDECAR as the declaration, registering the asset it names. Ids
therefore keep resolving in a packaged build — asset references survive renames,
and a sprite still reads its texture import settings (an authored `point` filter
stays point).

Verified by `PlayerBundleTests` (the rule, every extension named with its
reason), `AssetDatabaseTests` (sidecar-declared registration, the import-mode
orphan drop, the duplicate-id and unreadable-sidecar cases) and the
`player_pak_assetid_selfcheck` ctest on both flavors — a project split exactly
the way a package splits it (texture only inside a STORED pak, manifest + scene
+ sidecar on disk) whose sprite names a stale texture beside the asset id, so it
can only render if the id resolved.

## Security: zip-slip + the path jail

**Threat model.** In an AI-agent development setting an agent authors project
files and imports assets over MCP, and the content it handles — zips/paks,
scenes, textures — may come from an **untrusted source** (fetched off the web).
Neither the agent nor the content may write to or resolve a path **outside its
intended root**: not the pak's mount root (for zip/pak contents), not the
project root (for the MCP file-authoring jail). The classic attacks are
**zip-slip** (an archive entry named `../../foo` or `/abs/foo` that a naive
extractor writes outside the extraction root) and **path traversal** in the
file jail (`../x`, `/abs/x`, or a symlink component that resolves out of root).
[security.md](security.md) collects the engine-wide posture; this section is the
filesystem boundary.

**One containment primitive.** `core_util/PathJail` is the single, pure,
headless-unit-tested guard both boundaries call:

- `isSafeRelativeEntry(name)` — a pure lexical predicate over an archive entry
  name (or any untrusted relative path). It **refuses** an empty name, an
  absolute path (leading `/` or `\`), a drive/UNC root (`C:…`, `\\server`), and
  **any `..` traversal segment** (splitting on both `/` and `\`, so a
  Windows-style `..\..\evil` is caught too). A legitimate nested name
  (`assets/textures/foo.png`) passes; a filename that merely starts with dots
  (`..foo`, `.hidden`) passes.
- `escapesRoot(base, target)` — a pure containment test on two normalized paths
  (true when `target` relativizes to a `..`-led or empty form). This is the
  shared decision behind the MCP project-file jail (`jailProjectPath`) and
  `AssetDatabase::resolveInsideRoot`, so the two sites no longer duplicate the
  logic.
- `resolveExtractPath(root, name, out)` — the **extract-to-disk** guard: it
  runs `isSafeRelativeEntry` first, then joins under `root` and re-verifies
  containment against **symlinks** (`weakly_canonical`), so a hostile entry can
  never be written through a symlinked component out of the extraction root.
  Called before any write.

**Where the guard sits.** `MiniZip::open` validates every central-directory
entry name at parse time and **drops** the unsafe ones with an honest
`[warn][filesystem]` line — the ONE choke point, so a malicious entry can
never be resolved in memory (`contains`/`read`/`names` never see it) NOR handed
to any extract-to-disk consumer. `PakArchive` re-checks the prefix-stripped
remainder as defense in depth, so a mounted sub-tree resolution can never leave
the sub-tree. The Android first-launch extraction (`extractBundledAssets`, the
`compressed` path) routes every destination through `resolveExtractPath` and
refuses (fails closed) on an escape. The MCP file-authoring jail
(`write/read/list_project_file(s)`, `import_asset`'s `targetDir`) normalizes +
canonicalizes the requested path and verifies containment, refusing absolute
paths, `..` escapes and symlink-out components with an honest error
(see [mcp.md](mcp.md)).

**Verification.** `PathJailTests` (unit, `[security]`) — crafted `..`/absolute/
drive/backslash/symlink inputs refused, legitimate nested paths accepted, plus
a 20 000-iteration fuzz loop asserting "a resolved path is always contained,
never crashes". `MiniZipRejectsZipSlipTest` (unit) mounts a **malicious fixture
zip** (`../../evil`, `/etc/evil`, `foo/../../bar`, `..\win_evil` + one benign
nested entry) and asserts every escaper is dropped and only the benign entry
reads back. The `editor_control` self-test's jail leg refuses absolute + nested
`..` authoring paths and asserts nothing was written outside the project.

## Verification

- `player_pak_selfcheck` (both flavors) — the pak acceptance test: the
  player mounts a STORED pak, loads its scene through the resource system,
  resolves a texture from it and streams an OGG from it, all like loose files.
- `MiniZipTests` / the mount-point resolver unit tests — STORED + DEFLATE reads,
  nested paths, non-zip rejection, and `normalizeMountPoint`.
- `export_android` / `export_android_aab` — assert the APK/module carries the
  stored-mode marker and STORED asset entries.
- The Android emulator Play test is the on-device runtime gate for the in-place
  APK mount.

See also [render-abstraction.md](render-abstraction.md) (the facade/flavor
matrix) and [store-release.md](store-release.md) (the store-submittable AAB).
