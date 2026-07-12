# Device session — plug in a phone, one command

`Util/orkige_device.py` is the owner-facing front door for running an Orkige
game on a real phone (or the `orkige_test` emulator / an iOS simulator). It
reuses the existing tooling — `Util/orkige_export.py`, which itself drives
`tools/player/android/package_apk.sh` and the iOS signing seam — and adds the
glue a session needs: a readiness check, the build-if-stale step, install,
launch and log streaming, so a phone session needs zero archaeology.

Stdlib-only Python (no pip install); `python3 Util/orkige_device.py --selftest`
is a headless unit ctest (`orkige_device_selftest`).

## 1. Check readiness

```sh
python3 Util/orkige_device.py doctor
```

An honest report, never a hard failure. It lists:

- **adb** and the connected Android devices/emulators;
- **NDK + JDK** for APK packaging (`ANDROID_NDK_HOME`, `JAVA_HOME`);
- **xcrun/simctl** and the available iOS simulators (macOS only);
- **iOS device signing** — the signing identity + provisioning profile. When
  absent it is reported as **ACTION NEEDED** with the exact doc pointer, not a
  failure (simulators need no certificate);
- **build trees** (`android-debug`, `ios-device-*`, `ios-simulator-debug`) and
  their staleness — a binary built before the newest source commit is called
  out as `STALE` so you rebuild before deploying.

## 2. Android — one command, end to end

```sh
python3 Util/orkige_device.py android                       # projects/benchmark
python3 Util/orkige_device.py android --project projects/roller
python3 Util/orkige_device.py android --scene scenes/lake.oscene
python3 Util/orkige_device.py android --serial emulator-5554  # disambiguate
```

It builds the `android-debug` player only if the tree is stale, packages the APK
(project baked in, so it boots with no arguments), `adb install`s it, launches
it, and streams the app's `logcat` until `Ctrl-C`. It works identically for a
physical phone and the `orkige_test` emulator — adb picks the device; use
`--serial` when more than one is attached. Default project: `projects/benchmark`
(the autonomous feature-tour showcase). Native-module projects are desktop-only
and are refused with a clear message.

## 3. iOS

```sh
python3 Util/orkige_device.py ios              # signed physical-device deploy
python3 Util/orkige_device.py ios --simulator  # no certificate needed
```

- **Physical device** — when a signing identity and provisioning profile are
  configured, this builds the `ios-device-debug` player if stale, runs the
  signed `ios` export, then `devicectl install` + `process launch`. The game
  runs standalone: a USB device has no dependency-free live-debug tunnel (see
  [`ios-signing.md`](ios-signing.md)).
- **No certificate** — the command explains exactly what to do (set
  `ORKIGE_IOS_SIGNING_IDENTITY` and `ORKIGE_IOS_PROVISIONING_PROFILE`, one-time
  Apple-side setup in [`ios-signing.md`](ios-signing.md)) and offers the
  simulator fallback.
- **Simulator** (`--simulator`) — builds `ios-simulator-debug`, exports the
  simulator `.app`, boots a simulator and `simctl install` + `launch`. No
  certificate required; this is also the target for the live-debug loop.

## What it needs once (iOS hardware)

A signed device install requires an Apple Developer signing identity and a
matching provisioning profile — enroll, create a Development certificate,
register the device, download the `.mobileprovision`, then export
`ORKIGE_IOS_SIGNING_IDENTITY` and `ORKIGE_IOS_PROVISIONING_PROFILE`. Only
`export.ios.teamId` is ever committed. The full step-by-step is in
[`ios-signing.md`](ios-signing.md). Android needs no such one-time setup —
`package_apk.sh` signs with a shared debug keystore it creates on demand.
