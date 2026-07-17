# Store release

The device/dev exports (`macos`, `ios-simulator`, `ios`, `android`) install on
your own machines and hardware. **Submitting to a store** needs one more layer:
Google Play accepts only a release-signed **Android App Bundle** (`.aab`), and
the App Store needs a distribution-signed **`.ipa`** uploaded through App Store
Connect. Those are the `android-aab` and `ios-ipa` export platforms.

Both are gated the same way the iOS device path is: they need developer
credentials that are **machine-local and never committed**, and they **degrade
honestly** — absent a credential (or a required tool), the exporter refuses and
produces nothing, rather than a half-signed artifact that would only mislead.
The config that IS safe to commit (bundle/package ids, team id, version) lives in
the project manifest; the certificates, keystores and passwords stay on your
machine in environment variables.

The pieces that need no credential (the Android bundle module structure, the
version/keystore config validation, the entitlements composition) are covered by
`ctest` — `export_android_aab` and the `orkige_export.py --selftest` unit — so
the pipeline stays honest even on a machine (like CI) with no signing material.

---

## Screen orientation

`export.orientation` is a project manifest Setting (applies to **every** mobile
export — `ios-simulator`, `ios`, `ios-ipa`, `android`, `android-aab`):

```xml
<Setting key="export.orientation" value="landscape"/>   <!-- portrait | landscape | auto -->
```

| value | iOS `UISupportedInterfaceOrientations` | Android `android:screenOrientation` |
|---|---|---|
| `portrait` (default) | Portrait | `sensorPortrait` |
| `landscape` | LandscapeLeft + LandscapeRight | `sensorLandscape` |
| `auto` | Portrait + LandscapeLeft + LandscapeRight | *(unset — the OS default)* |

**Portrait is the default** (an absent or unrecognised value), so a mobile game
is portrait unless it says otherwise. This also keeps the boot orientation
deterministic: iOS picks the initial interface orientation from the *allowed* set
by the window aspect, and the engine creates a desktop-wide (w>h) window, so an
**unconstrained app boots landscape**. `auto` opts back into every orientation
(and accepts that landscape boot).

Set it in the editor (**Build ▸ Project Settings…**), over MCP
(`set_project_setting export.orientation …`), or by hand in the `.orkproj`. The
exporter writes it into the iOS `Info.plist` and the Android manifest, and the
player also reads it at boot to constrain the window orientation
(`SDL_HINT_ORIENTATIONS`) so the render surface matches the orientation the OS
presents — the lock and the drawable agree. Verified by
`orkige_export.py --selftest`; on-device upright rendering is a manual check.

---

## Google Play — release App Bundle (`.aab`)

### One-time setup

1. **Version the release** in the project manifest. `versionCode` is an integer
   Google Play requires to **strictly increase with every upload**; `versionName`
   is the human-facing marketing string (any format).

   ```xml
   <Setting key="export.android.versionCode" value="1"/>
   <Setting key="export.android.versionName" value="1.0.0"/>
   <Setting key="export.android.package"     value="com.yourstudio.yourgame"/>
   ```

   Bump `export.android.versionCode` by one for **every** build you upload — Play
   rejects a bundle whose version code it has already seen. `versionName` you bump
   when it matters to players (1.0.0 → 1.0.1 → 1.1.0).

2. **Create a release (upload) keystore.** One keystore, kept forever — losing it
   means you can no longer update the app (unless you enrol in Play App Signing
   key reset). Generate it once with the JDK's `keytool`:

   ```sh
   keytool -genkeypair -v \
       -keystore ~/keys/yourgame-upload.jks \
       -alias yourgame -keyalg RSA -keysize 2048 -validity 10000 \
       -dname "CN=Your Studio, O=Your Studio, C=US"
   ```

   Keep the `.jks` file and its passwords out of the repository (a `~/keys`
   directory, a password manager — never the project tree).

3. **Install bundletool.** It is *not* part of the Android SDK build-tools; it is
   a standalone jar from the Android tooling releases. Download
   `bundletool-all-<version>.jar`, then point the exporter at it:

   ```sh
   export ORKIGE_BUNDLETOOL="$HOME/tools/bundletool-all.jar"
   ```

   (A `bundletool` launcher on your `PATH` is picked up automatically.)

4. **Enrol in Play App Signing** in the Play Console when you create the app. You
   upload a bundle signed with your *upload* key (the keystore above); Google
   re-signs it with the *app signing* key it manages. This is the standard,
   recommended path.

### Point the tools at your keystore

Passwords are read straight from the environment by `jarsigner` — they never
appear on a command line or in a file the exporter writes:

```sh
export ORKIGE_ANDROID_KEYSTORE="$HOME/keys/yourgame-upload.jks"
export ORKIGE_ANDROID_KEY_ALIAS="yourgame"
export ORKIGE_ANDROID_KEYSTORE_PASS="…"        # keystore (store) password
export ORKIGE_ANDROID_KEY_PASS="…"             # key password (omit if same)
```

### The repeatable release flow

1. Build an **optimized** player once (a Debug `libmain.so` packages too, but the
   exporter warns — Debug runs far slower):

   ```sh
   VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset android-release
   cmake --build --preset android-release --target orkige_player
   ```

2. Export the signed bundle:

   ```sh
   python3 Util/orkige_export.py --project projects/yourgame \
       --platform android-aab --engine-build build/android-release
   ```

   The exporter validates the version and package, stages the payload, then drives
   `tools/player/android/build_aab.sh`: `aapt2 link --proto-format` →
   the bundle module → `bundletool build-bundle` → `jarsigner`. The result is
   `<project>/builds/android-aab/<Game>.aab`.

3. Upload `<Game>.aab` to the Play Console (Internal testing → Production). Play
   splits it into per-device APKs on its servers.

To inspect the pipeline without a keystore or bundletool — what CI does — add
`--aab-unsigned-module`: it builds only the unsigned proto **bundle module**
(`<Game>.aab.module.zip`), clearly labelled as **not submittable**.

### Notes

- **Target SDK.** Google Play requires new-app and app-update uploads to target a
  recent API (currently 35). The player manifest targets 35;
  `build_aab.sh` warns loudly if a build ever drops below the floor.
- **Non-debuggable.** The release bundle flips `android:debuggable` to `false`
  (the dev-player APK stays debuggable for the editor's Play-on-device drop).

---

## App Store — distribution `.ipa`

This builds on the iOS device-signing setup in
[`ios-signing.md`](ios-signing.md). Development signing installs on **your**
registered devices; **distribution** signing produces the `.ipa` you upload to
App Store Connect. They use a **separate** certificate and provisioning profile.

### One-time setup

1. Set the team + bundle id in the manifest (safe to commit):

   ```xml
   <Setting key="export.ios.teamId"   value="ABCDE12345"/>
   <Setting key="export.ios.bundleId" value="com.yourstudio.yourgame"/>
   ```

2. **Create an Apple Distribution certificate** (Certificates → *Apple
   Distribution*) and install it; confirm with
   `security find-identity -v -p codesigning`.

3. **Create an App Store provisioning profile** (Profiles → *App Store*) bound to
   the app id, and download the `.mobileprovision`.

### Point the tools at your distribution identity + profile

Separate from the development pair, so both can coexist:

```sh
export ORKIGE_IOS_DISTRIBUTION_IDENTITY="Apple Distribution: Your Studio (ABCDE12345)"
export ORKIGE_IOS_DISTRIBUTION_PROFILE="$HOME/path/to/YourGame_AppStore.mobileprovision"
```

### The repeatable release flow

1. Build the device player once (either flavor; needs no certificate to compile):

   ```sh
   VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset ios-device-release
   cmake --build --preset ios-device-release --target orkige_player
   ```

2. Export the distribution `.ipa`:

   ```sh
   python3 Util/orkige_export.py --project projects/yourgame \
       --platform ios-ipa --engine-build build/ios-device-release
   ```

   The exporter assembles the bundle, codesigns it with the distribution identity
   and **distribution entitlements** (`get-task-allow` cleared — the App Store
   rejects it otherwise), embeds the App Store profile, and wraps it into
   `Payload/<Game>.app` → `<project>/builds/ios-ipa/<Game>.ipa`.

3. **Upload** to App Store Connect. Uploading needs interactive or API-key
   authentication and is left as a manual step (no credential is stored by the
   engine). Options, current as of 2026:

   - **`xcrun altool --upload-package`** (the older `--upload-app` is deprecated).
     Prefer App Store Connect **API-key** auth over an app-specific password:

     ```sh
     xcrun altool --upload-package builds/ios-ipa/YourGame.ipa \
         --type ios --apple-id <app-apple-id> \
         --bundle-id com.yourstudio.yourgame \
         --bundle-version <versionCode> --bundle-short-version-string <versionName> \
         --apiKey <KEY_ID> --apiIssuer <ISSUER_ID>
     ```

   - **Transporter** (the Mac App Store app, or its `iTMSTransporter` CLI) — a
     drag-and-drop / JWT-authenticated alternative that shows delivery logs.

   `notarytool` is **not** used here — it notarizes macOS apps; App Store iOS
   uploads go through altool/Transporter.

### Privacy manifest (`PrivacyInfo.xcprivacy`)

App Store submission requires a **privacy manifest** in the app bundle declaring
collected data types, tracking, tracking domains, and any use of Apple's
"required reason" APIs. The exporter generates one into every iOS bundle —
`ios-simulator`, `ios`, and the `ios-ipa` `Payload` app (written before signing,
so the signature seals it). It declares:

- **No tracking** (`NSPrivacyTracking` false), **no tracking domains**, **no
  collected data types** — the engine is self-contained: every dependency is
  statically linked (no third-party SDK carrying its own manifest), it collects
  nothing and contacts no server.
- Exactly two **accessed API categories**, matching what the shipped player
  binary actually imports:

  | category | why | reason code |
  |---|---|---|
  | File timestamp | `stat`/`fstat` in the statically linked resource and file layers (archive/directory scanning, file sizes); the player reads only its bundle and its writable app dir | `C617.1` (files inside the app container) |
  | System boot time | `mach_absolute_time` in the high-resolution frame/performance timer | `35F9.1` (elapsed time between in-app events) |

Nothing else on Apple's required-reason list (disk space, active keyboard list,
user defaults) appears in the binary, so nothing else is declared — an over- or
under-declaring manifest is worse than none. Engine code that adopts one of
those APIs must add its category with an approved reason code to
`privacy_manifest()` in `Util/orkige_export.py`. The declaration is verified by
`orkige_export.py --selftest` and the `export_ios_simulator` structure test
(presence, plist parse, no-tracking, the two categories).

### This machine

This development machine holds **no** Apple distribution certificate, so
`--platform ios-ipa` refuses with a clear message here — the honest gate. The
`.ipa` layout and the distribution-entitlements composition are still verified
cert-free by `orkige_export.py --selftest`.
