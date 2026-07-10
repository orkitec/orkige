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
    --engine-build build/macos-debug \
    --signing-identity "$ORKIGE_IOS_SIGNING_IDENTITY" \
    --provisioning-profile "$ORKIGE_IOS_PROVISIONING_PROFILE"
```

(The `--signing-identity` / `--provisioning-profile` arguments override the
environment variables when both are given.) Install with:

```sh
xcrun devicectl device install app --device <udid> <YourGame.app>
```

When neither an identity nor a profile resolves, `--platform ios` refuses with a
clear message and produces nothing - the honest gate. `--platform ios-simulator`
never needs signing.

## Editor Play-on-iPhone gate

The editor's Play target picker enumerates connected iOS hardware only once both
`ORKIGE_IOS_SIGNING_IDENTITY` (a valid identity in the keychain) and
`ORKIGE_IOS_PROVISIONING_PROFILE` (an existing profile file) are configured.
Until then the hardware entry stays disabled with an explanatory tooltip. Full
Play-on-hardware (scene transfer + a debug-port tunnel over USB, and an
`arm64-ios` device player build) is a further step; this document covers the
signing prerequisite it depends on.
