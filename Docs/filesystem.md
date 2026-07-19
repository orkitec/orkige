# Pak mounting (filesystem)

A **pak** is a zip mounted as read-only content: the engine registers its
entries with the resource system so a scene, texture, sound or script inside it
resolves **exactly like a loose file**. It is the backend-neutral successor to
the 2012 classic-only *BigZip* capability, and its original purpose — reading an
Android APK's assets in place — is now the default on mobile.

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
- `LT_ZIP` on `addResourceLocation` also works on both flavors now — it is the
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
  the app files dir on first launch — the older behaviour.

The App Bundle (`.aab`) path keeps the assets uncompressed in bundletool's
generated APKs via a `BundleConfig` `uncompressedGlob` (`build_aab.sh`).

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
A future `Docs/security.md` will collect the engine-wide posture; this section
is the filesystem boundary.

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

- `player_pak_selfcheck` (both flavors) — the reborn BigZip acceptance test: the
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
