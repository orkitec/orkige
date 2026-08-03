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
the project manifest; the certificates and keystores stay on your machine, and
the passwords stay in its credential store or its environment.

The pieces that need no credential (the Android bundle module structure, the
version/keystore config validation, the entitlements composition) are covered by
`ctest` — `export_android_aab` and the `tests/exporter` units — so
the pipeline stays honest even on a machine (like CI) with no signing material.

---

## Build settings in the editor

**Build ▸ Project Settings…** edits both kinds of build setting, in two tabs that
look different because they *are* different.

### Project — committed

The **Project** tab writes manifest `export.*` Settings: screen orientation, app
icon, launch background, bundle ids, Team ID, Android package name, version code
and name, asset packaging. They describe the app, everyone working on the game
needs the same ones, and they are committed with the `.orkproj`. The same keys
are reachable by hand in the manifest and over MCP (`set_project_setting`) —
there is one vocabulary, not three.

### Signing — this machine only

The **Signing** tab is a platform × purpose matrix, because **development and
distribution use different credentials**: an iOS development identity does not
sign an App Store upload, and a distribution identity installs on no development
device. Each platform tab shows both purposes:

| | Development | Distribution |
|---|---|---|
| **iOS** | signing identity + development provisioning profile (Play on a device, a development install) | distribution identity + App Store/ad-hoc profile |
| **Android** | nothing to configure — a debug APK is signed with the shared debug keystore, created on this machine on demand | release keystore, key alias, bundletool jar, plus the two passwords (which go to the credential store, never to a file) |
| **macOS** | nothing to configure — an export is signed ad-hoc, which runs on this machine | shown, not wired: distributing to other Macs needs a Developer ID identity and notarization |
| **Windows** | nothing to configure — an exported executable is unsigned and runs here | shown, not wired: removing the SmartScreen warning needs your own code-signing certificate |

A cell with nothing to configure says so rather than showing an empty field, and
a cell whose credentials are not applied yet says that too — three honest states
instead of one hopeful one.

What you fill in is kept in a **per-project file under the editor's own writable
state directory**, readable by you alone, never inside a project directory. The
window shows the exact path. That location is the whole point: a signing setting
that landed in a repository is the failure this split exists to prevent, so
there is no fallback that puts one near a project — on a machine with no per-user
application directory the editor stores nothing and says so.

Each field falls back to its environment variable when left empty, so a machine
already configured for the CLI needs no editing at all, and CI (which sets only
the variables) is unaffected.

#### What "readable by you alone" rests on

The file is created empty, restricted while it still holds nothing, written, and
then renamed onto its final name — so the credentials are never in a file
another account can open, not even for the instant between a write and a
restriction applied afterwards. It is the same sink every secret the editor
writes goes through (`core_filesystem/FileWriter`, `Docs/security.md`). The
restriction is *owner read and write, nobody else named*, stated in each
platform's own access-control vocabulary:

| Platform | The restriction |
|---|---|
| **macOS**, **Linux** | POSIX mode bits, enforced by the kernel: the file is `0600` |
| **Windows** | a **protected** (non-inheriting) DACL granting this user and SYSTEM, and naming nobody else — written through the platform's own security API, since access control there is an ACL the standard filesystem library cannot express |

What the restriction buys is bounded: it stops another account on the machine,
never code running as this user. It also is not the last line here — **no
password is ever in this file.** Those live in the operating system's own
credential store (below), which is why the worst this file can expose is a
keystore *path* and a key *alias*.

### Passwords go to the operating system's credential store

**No password is ever written into a file the editor owns.** A keystore password
kept in an editor settings file would be a plaintext secret sitting there for the
lifetime of the project, backed up as readable text with everything else. So a
password takes a different road from every other credential: the platform's own
vault, keyed per project and per slot.

| Platform | Where a password goes |
|---|---|
| **macOS** | the **Keychain** (a generic-password item under the service `com.orkitec.orkige.signing`) |
| **Windows** | the **Credential Manager** (a generic credential under the same service name, stored for this user on this machine) |
| **Linux** | **nowhere** — see below |

The account name is `<project>-<digest>/<slot>`, so two projects on one machine
never share a password, a moved project starts from none rather than silently
inheriting one, and the whole set is findable — and revocable — in the system's
own credential UI.

**The order is environment, then vault, then not set.** An `export`ed variable
always wins: CI, headless runs and scripted builds must not depend on a desktop
keyring, and someone debugging a signing problem must be able to override what is
stored without emptying it first. When neither has one, the row says so and names
*both* ways to provide it.

**On Linux the editor keeps no password at all.** The desktop keyring is a
session D-Bus service reached through libsecret, which would pull glib into every
Linux build for a case that degrades to the environment on the same machine — and
the Linux builds that run unattended have no session bus and no unlocked keyring
anyway. There is no file-based substitute, because a file is the exposure this
whole arrangement removes. The field says so and names the variable.

#### What a vault protects, and what it does not

It removes three real exposures: a secret that can be committed, a secret that
rides into backups as readable text, and a secret readable by glancing at a file.
It does **not** make a secret unreadable by a process you have already
authorised — the same user, on the same machine, can ask the vault for it, which
is exactly how the editor reads it back. Treat it as "not lying around", not as
"locked away".

The editor also does not push a stored password into its own process
environment: everything it launches — the embedded terminal included — would
inherit it. A build started from a shell therefore reads the variable from that
shell, and the settings window says so rather than reporting a readiness the
build would not agree with.

Automated editor runs (the `ORKIGE_*_TEST` / `ORKIGE_DEMO_*` probes every ctest
sets) install **no vault at all**, so a test run cannot prompt for keychain
access or read the credentials of whoever is running it. For the same reason
there is **no MCP verb** that reads or writes one: an agent that could would
launder exactly the rule this split exists to enforce.

### Refusals

A release the machine cannot sign refuses before it starts, naming each missing
piece: the settings window and the export gate ask the *same* function, so what
the window promises and what a build refuses cannot drift apart.

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
the `tests/exporter` units; on-device upright rendering is a manual check.

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

#### Assets: stored vs compressed

`export.android.assets` decides how the game's media rides in the APK/AAB — a
**performance vs. buildsize** choice:

```xml
<Setting key="export.android.assets" value="stored"/>     <!-- the default -->
```

- **`stored`** (default): the media stays **uncompressed** in the package, so the
  installed app **mounts its own APK and reads the media in place** — no
  first-launch extraction, and OGG music streams seekably straight from the APK.
  The `.aab` keeps the assets uncompressed in the APKs Play generates via a
  bundletool `BundleConfig` `uncompressedGlob`. Larger download, faster and
  leaner at runtime.
- **`compressed`**: the media is **deflated** for a smaller download and
  extracted to the app's files dir on first launch. Smaller download, a one-time
  extraction cost and a second on-disk copy.

See [filesystem.md](filesystem.md#android-mount-the-apk-instead-of-extracting-everything)
for the mount mechanics.

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

The keystore path, the alias and the bundletool jar are settable in **Build ▸
Project Settings ▸ Signing ▸ Android** (see
[Build settings in the editor](#build-settings-in-the-editor)). The two
**passwords** are settable there too, and go to this machine's credential store
rather than to any file
([Passwords go to the operating system's credential store](#passwords-go-to-the-operating-systems-credential-store)).
A build started from a shell reads them from that shell — `jarsigner` takes them
through `-storepass:env`, so they never appear on a command line:

```sh
export ORKIGE_ANDROID_KEYSTORE="$HOME/keys/yourgame-upload.jks"
export ORKIGE_ANDROID_KEY_ALIAS="yourgame"
export ORKIGE_ANDROID_KEYSTORE_PASS="…"        # keystore (store) password
export ORKIGE_ANDROID_KEY_PASS="…"             # key password (omit if same)
```

Every variable above is also the fallback for the matching editor field: a value
set in the editor wins, an empty one falls through.

### The repeatable release flow

1. Build an **optimized** player once (a Debug `libmain.so` packages too, but the
   exporter warns — Debug runs far slower):

   ```sh
   VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset android-release
   cmake --build --preset android-release --target orkige_player
   ```

2. Export the signed bundle:

   ```sh
   orkige_export --project projects/yourgame \
       --platform android-aab --engine-build build/android-release
   ```

   The exporter validates the version and package, stages the payload, then
   assembles the bundle in process: `aapt2 link --proto-format` → the bundle
   module → `bundletool build-bundle` → `jarsigner`. The result is
   `<project>/builds/android-aab/<Game>.aab`.

3. Upload `<Game>.aab` to the Play Console (Internal testing → Production). Play
   splits it into per-device APKs on its servers.

To inspect the pipeline without a keystore or bundletool — what CI does — add
`--aab-unsigned-module`: it builds only the unsigned proto **bundle module**
(`<Game>.aab.module.zip`), clearly labelled as **not submittable**.

### Notes

- **Target SDK.** Google Play requires new-app and app-update uploads to target a
  recent API (currently 35). The player manifest targets 35, and the export
  warns loudly if a build ever drops below the floor.
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

Separate from the development pair, so both can coexist — which is exactly why
they are a separate row in **Build ▸ Project Settings ▸ Signing ▸ iOS ▸
Distribution** and a separate pair of variables:

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
   orkige_export --project projects/yourgame \
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
the privacy-manifest builder in `tools/exporter/ExportPlist.h`. The
declaration is verified by `ExportPlistTests` and the `export_ios_simulator`
structure test
(presence, plist parse, no-tracking, the two categories).

### This machine

This development machine holds **no** Apple distribution certificate, so
`--platform ios-ipa` refuses with a clear message here — the honest gate. The
`.ipa` layout and the distribution-entitlements composition are still verified
cert-free by `ExportSettingsTests`.
