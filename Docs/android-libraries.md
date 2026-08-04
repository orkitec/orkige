# Android library archives

An Android SDK - a store client, an analytics client, a push receiver, an
advertising mediator - ships as an **Android library archive**, a `.aar` file:
a zip carrying compiled Java, a manifest fragment, and optionally resources,
assets and per-ABI native libraries. A project lists the archives it depends on
and the Android export routes each part into the package it assembles.

This is the plumbing. It is not a provider: nothing here knows what any
particular SDK does, and the engine takes on no dependency on one. What a game
does with a library it depends on is the game's business.

## Declaring a dependency

One manifest Setting, a semicolon-separated list of **project-relative** paths:

```xml
<Setting key="export.android.libraries"
         value="libs/vendor-store.aar;libs/vendor-analytics.aar"/>
```

The archives live inside the project like any other content. **Nothing is
downloaded**: there is no dependency resolution, no artifact repository and no
transitive graph - a project points at files it already has, and getting them
there is a step somebody performs once and commits. An archive that depends on
another archive needs both listed.

The paths are jailed the way every path a manifest can name is: an absolute
path, a path leaving the project, and a name that is not an `.aar` are each
refused before anything is read.

The setting is reachable over MCP through the existing
`set_project_setting` / `get_project_setting` verbs, like every other `export.*`
key - see [mcp.md](mcp.md). Packaging is `export_project("android")` as before.

## What each part becomes

| In the archive | In the package |
|----------------|----------------|
| `classes.jar`, `libs/*.jar` | the Java compile classpath, and inputs to the dexer |
| `AndroidManifest.xml` | merged into the app manifest (below) |
| `res/` | merged into the resource tree the linker compiles |
| `assets/` | packaged under `assets/`, readable by the same relative name |
| `jni/<abi>/*.so` | `lib/<abi>/`, beside the engine's own |
| `R.txt` | the signal that this library's code resolves its own resource ids |

Everything else an archive carries - the shrinker rules, the lint jar, the
annotation archive, the public-resource list, the signature block, the native
build headers - has no consumer in this assembly and is left where it is. It is
not dropped from something that would otherwise have used it: there is no
shrinker run, no lint run and no native build here to hand it to.

### Resource ids

A library that reads its own resources compiles against an `R` class, and the
ids in it are not decided until the **whole** resource table is linked - which
only the app can do. The archive therefore ships its symbol list rather than
the class, and the export asks the resource linker to generate `R` sources for
each library package alongside the app's own, then compiles them with the rest
of the Java.

That is why the resource steps run **before** the Java steps. The whole command
set is still decided up front by the pure planner in
`tools/exporter/ExportAndroidAssemble.h`; the generated sources reach the
compiler as a list written between the two steps, because what they are is not
knowable until the linker has run.

### More classes than one dex file addresses

A dex image addresses a bounded number of methods, and a package carrying
library archives can pass it. The dexer answers with `classes2.dex`,
`classes3.dex` and so on, and every one of them is packaged - the platform
loads a package's dex images by that naming and ignores nothing.

## The manifest merge

A library declares what it needs to run: permissions, components, package
visibility. **A declaration that is silently dropped produces an app that
installs, launches, and then misbehaves on a player's phone** - a permission
denial, an intent that resolves to nothing, a component that cannot be started.
There is no build-time symptom.

So the merge supports a named subset and **refuses everything else by name**,
saying which archive and which element. Nothing is dropped quietly.

Merged, de-duplicated by `android:name`:

- `<uses-permission>`, `<uses-permission-sdk-23>`, `<permission>`,
  `<permission-group>`, `<permission-tree>`
- `<uses-feature>` - a feature two manifests disagree about ends up **required**,
  which is the platform's own rule: the other direction lets a device without
  the feature install an app half of which cannot run
- the children of `<queries>` - package visibility, without which an intent
  silently resolves to nothing on newer platforms
- inside `<application>`: `<activity>`, `<activity-alias>`, `<service>`,
  `<receiver>`, `<provider>`, `<meta-data>`, `<uses-library>`,
  `<uses-native-library>`, `<property>`

Read but not copied:

- `<uses-sdk>` - a library needing a newer `minSdkVersion` than the app
  declares is refused. The app would otherwise install on devices the library
  cannot run on.

Substituted:

- `${applicationId}` becomes the app's own package, which is how a library
  writes a provider authority or a callback host that has to be unique per app.
  **Any other placeholder is refused by name** rather than shipped literally.

Refused by name:

- any other `<manifest>` child, and any other `<application>` child
- attributes on the library's `<application>` element. `android:name` replaces
  the app's Application class and `android:theme` its look - decisions the
  project owns.
- the `tools:` merge-directive vocabulary (`tools:node`, `tools:replace`,
  `tools:remove`, ...). Applying it wrongly and ignoring it are both worse than
  saying so.
- two declarations of the same thing that are not the same declaration. Two
  identical ones are one declaration and merge silently; where they differ, the
  real merger resolves it with a directive somebody wrote, and nobody wrote one
  here.
- a resource or an asset two sources provide with different content under the
  same name. The platform's answer is an overlay order nobody here declared.
- a library carrying native code for other ABIs and not the one being packaged.

A project that depends on **no** library archive packages exactly the manifest
it always did, byte for byte: with nothing to merge there is no XML round trip
in the middle to reformat it.

## Both package shapes

An APK and an App Bundle differ in how the artifact is packed, not in the steps
in front of it. The library's jars, resources, assets, native libraries and
manifest declarations are folded in before either writer runs, so both carry
them. Store submission is otherwise unchanged - see
[store-release.md](store-release.md).

## What is not here

- **No dependency resolution.** No artifact repository, no version ranges, no
  transitive closure. Listing an archive is listing a file.
- **No build system.** The package is still assembled by spawning the Android
  SDK's own programs directly as argv, with nothing handed to a command
  interpreter.
- **No vendor SDK.** The engine ships none and links none.

## Where it lives

| File | What it owns |
|------|--------------|
| `tools/exporter/ExportAndroidLibrary.h` | routing an archive's entries, the manifest merge, unpacking |
| `tools/exporter/ExportAndroidAssemble.h` | the planner the routed parts enter |
| `tools/exporter/ExportSettings.h` | reading `export.android.libraries` |
| `tools/exporter/ExportZip.h` | the zip reader an archive is read with |

The prerequisites are unchanged: an Android package needs the platform's
toolchain and the platform's player, each reported on its own - see
[device-payloads.md](device-payloads.md). A library archive is a fourth thing
and stands in for none of them; a project with no compiled C++ of its own still
needs no engine SDK pack to depend on one.
