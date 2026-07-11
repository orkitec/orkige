# iOS device signing

Installing an Orkige game on a **physical iPhone/iPad** (as opposed to the
simulator) requires the app to be code-signed with an Apple Developer identity
and a matching provisioning profile. This is the one-time, owner-facing setup.
Scope here is **development device installs only** - not App Store submission.

The split is deliberate: the **Team ID** identifies the project and is safe to
commit; the **signing certificate** and **provisioning profile** are specific to
your machine/account and must never be committed. The exporter reads the latter
two from CLI arguments or the environment, so a shared checked-in project stays
machine-neutral.

## One-time Apple-side setup

1. **Enroll in the Apple Developer Program** and note your 10-character
   **Team ID** (Membership details). Put it in the project manifest:

   ```xml
   <Setting key="export.ios.teamId" value="ABCDE12345"/>
   ```

   Optionally set the bundle id (defaults to `com.orkitec.<slug>`):

   ```xml
   <Setting key="export.ios.bundleId" value="com.yourstudio.yourgame"/>
   ```

2. **Create an Apple Development signing certificate.** In Keychain Access →
   Certificate Assistant, request a certificate from a certificate authority,
   upload the request in the developer portal (Certificates → Development), then
   download and install the issued certificate. Confirm it is present:

   ```sh
   security find-identity -v -p codesigning
   ```

   The identity name or SHA-1 from that listing is your signing identity.

3. **Register the device and create a Development provisioning profile.** Add the
   target device's UDID (Devices), create a Development profile (Profiles) bound
   to the app id and that device, and download the `.mobileprovision` file.

## Pointing the tools at your identity and profile

Set these on your machine (they are never read from the manifest):

```sh
export ORKIGE_IOS_SIGNING_IDENTITY="Apple Development: you@example.com (XXXXXXXXXX)"
export ORKIGE_IOS_PROVISIONING_PROFILE="$HOME/path/to/YourGame_Development.mobileprovision"
```

Then export a signed device app:

```sh
python3 Util/orkige_export.py --project projects/yourgame --platform ios \
    --engine-build build/ios-device-debug \
    --signing-identity "$ORKIGE_IOS_SIGNING_IDENTITY" \
    --provisioning-profile "$ORKIGE_IOS_PROVISIONING_PROFILE"
```

The `--engine-build` tree is an **arm64-ios (physical-device) player build**, not
the simulator one. Build it once (either render flavor):

```sh
VCPKG_ROOT=$HOME/Development/vcpkg cmake --preset ios-device-debug   # or -release
cmake --build --preset ios-device-debug --target orkige_player
```

The `ios-device-*` presets cross-build `tools/player/OrkigePlayer.app` for
arm64 iPhoneOS (Ogre-Next/Metal by default, GLES2 on the `-classic` variants).
Compiling and linking need **no certificate** - only installing on a device
does - so the device tree builds on any machine; the presets use the Ninja
generator, which never invokes `codesign` at build time. Real signing happens at
export time through the seam above.

(The `--signing-identity` / `--provisioning-profile` arguments override the
environment variables when both are given.) The exporter signs the bundle
inside-out and embeds the profile, then prints the install command:

```sh
xcrun devicectl device install app --device <udid> <YourGame.app>
xcrun devicectl device process launch --device <udid> <bundle-id>
```

When neither an identity nor a profile resolves, `--platform ios` refuses with a
clear message and produces nothing - the honest gate. There is deliberately no
`--unsigned` escape hatch: an unsigned device `.app` installs on no
non-jailbroken device, so it would only mislead (the unsigned bundle already
sits in the build tree for anyone who wants to inspect it).
`--platform ios-simulator` never needs signing.

## Editor Play-on-iPhone (deploy-and-run)

Once both `ORKIGE_IOS_SIGNING_IDENTITY` (a valid keychain identity) and
`ORKIGE_IOS_PROVISIONING_PROFILE` (an existing profile file) are set, the
editor's Play target picker enumerates connected iOS hardware and the device
entry becomes **selectable**. The one-time owner path is therefore:

1. Do the Apple-side setup above and `export` the two environment variables.
2. Build the device player once (`cmake --build --preset ios-device-debug
   --target orkige_player`).
3. Open the project, plug in the iPhone, pick it in the Play target picker, and
   press **Play**.

Play on a device is a **deploy-and-run**, not a live play session: the editor
runs an `ios` export (build the current project into a signed `.app`, sign it,
embed the profile), then `devicectl device install app` and `devicectl device
process launch`, streaming `[export]` / `[deploy]` lines into the Console. The
game then runs standalone from its bundled scene.

### The live-debug gap over USB

Unlike the simulator (which shares the host filesystem and loopback) and Android
(where `adb forward tcp` bridges the debug port), a USB-connected iPhone exposes
**no dependency-free TCP tunnel** for the editor's debug protocol. `xcrun
devicectl` has no port-forward verb, and Xcode's own tunnel rides private
usbmuxd / RemoteXPC machinery that is not a CLI. The dependency-free
alternatives are either heavyweight third-party tooling (`libimobiledevice`'s
`iproxy`, `pymobiledevice3`) or a hand-rolled usbmuxd client that would not even
cover the RemoteXPC transport iOS 17+ devices use. The engine takes on **no such
dependency**, so the editor opens no remote hierarchy/inspector link and offers
no pause/step when a game runs on hardware - the game runs standalone. Use a
booted **simulator** target for the live debug loop; device deploy is for
seeing the real thing on the real screen. If a first-party `devicectl` port
tunnel ships, wiring the live link is the only remaining step.
