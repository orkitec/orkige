#!/usr/bin/env python3
"""ctest driver for RUNNING a mobile export on a real device: package the
project, install the artifact on an iOS Simulator / Android device, launch it
and wait for the bundled project to boot.

    run_export_device_test.py --repo <root> --project <dir>
                              --exporter <orkige_export>
                              --platform ios-simulator|android
                              --engine-build <dir> --output <dir>
                              [--deadline 300]

The structural export tests (run_export_test.py) assert what a package
CONTAINS. This one asserts that what it contains is enough: a payload missing a
scene, a shader tree or a texture is a package that installs perfectly and then
boots into nothing, which no amount of file-list checking catches.

Exit codes: 0 pass, 77 skip (no built player, no device, no SDK tool - the
ctest SKIP_RETURN_CODE), anything else fail.
"""

import argparse
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

SKIP = 77
#: the line the player prints once its project's scene is live - the proof the
#: payload was complete enough to reach a running game
BOOT_MARKER = re.compile(r"orkige_player: scene '.*' loaded \((\d+) GameObjects\)")


def log(message):
    print("run_export_device_test: " + message, flush=True)


def fail(message):
    print("run_export_device_test: FAILED - " + message, flush=True)
    sys.exit(1)


def skip(message):
    print("run_export_device_test: SKIP - " + message, flush=True)
    sys.exit(SKIP)


def require(condition, message):
    if not condition:
        fail(message)
    log("ok: " + message)


def run(argv, timeout=300, check=True):
    result = subprocess.run(argv, capture_output=True, text=True,
                            timeout=timeout, errors="replace")
    output = (result.stdout or "") + (result.stderr or "")
    if check and result.returncode != 0:
        fail("$ " + " ".join(argv) + "\n" + output)
    return result.returncode, output


def project_names(project_dir):
    """(display name, exe name) from the manifest, mirroring the exporter"""
    manifest = ET.parse(os.path.join(project_dir, "project.orkproj")).getroot()
    name = (manifest.findtext("Name") or "").strip()
    return name, re.sub(r"[^A-Za-z0-9]", "", name)


def export(exporter, project, platform, engine_build, output):
    if os.path.exists(output):
        shutil.rmtree(output)
    argv = [exporter, "--project", project, "--platform", platform,
            "--engine-build", engine_build, "--output", output]
    log("$ " + " ".join(argv))
    result = subprocess.run(argv, capture_output=True, text=True)
    print(result.stdout, end="", flush=True)
    if result.stderr:
        print(result.stderr, end="", flush=True)
    if result.returncode != 0:
        fail("exporter exited nonzero")


# --- iOS Simulator ----------------------------------------------------------

def first_simulator():
    """a booted iPhone simulator udid, else "" - this test never boots one
    itself: a cold simulator boot takes minutes and belongs to the tests that
    exist for it"""
    code, output = run(["xcrun", "simctl", "list", "devices", "available",
                        "-j"], timeout=120, check=False)
    if code != 0:
        return ""
    try:
        listing = json.loads(output)
    except ValueError:
        return ""
    for runtime, devices in listing.get("devices", {}).items():
        if "iOS" not in runtime:
            continue
        for device in devices:
            if (device.get("isAvailable", True)
                    and device.get("state") == "Booted"
                    and "iPhone" in device.get("name", "")):
                return device.get("udid", "")
    return ""


def run_ios_simulator(args):
    if sys.platform != "darwin" or not shutil.which("xcrun"):
        skip("the iOS Simulator flow needs macOS with the Xcode command line "
             "tools")
    player_app = os.path.join(args.engine_build, "tools", "player",
                              "OrkigePlayer.app")
    if not os.path.isdir(player_app):
        skip("no built iOS Simulator player at '%s' - build the matching "
             "ios-simulator preset to enable this test" % player_app)
    udid = first_simulator()
    if not udid:
        skip("no BOOTED iPhone simulator (boot one with 'xcrun simctl boot')")
    log("using simulator " + udid)

    name, _exe = project_names(args.project)
    export(args.exporter, args.project, "ios-simulator", args.engine_build,
           args.output)
    app = os.path.join(args.output, name + ".app")
    require(os.path.isdir(app), "the exporter produced an app bundle")
    with open(os.path.join(app, "Info.plist"), "rb") as handle:
        bundle_id = plistlib.load(handle)["CFBundleIdentifier"]

    run(["xcrun", "simctl", "uninstall", udid, bundle_id], timeout=120,
        check=False)
    run(["xcrun", "simctl", "install", udid, app], timeout=300)
    log("installed " + bundle_id)
    try:
        # --console-pty attaches the app's stdout; the frame cap rides in as a
        # child environment variable, so the app ends its own run
        code, output = run(
            ["xcrun", "simctl", "launch", "--console-pty", udid, bundle_id],
            timeout=args.deadline, check=False)
        print(output, end="", flush=True)
        require(BOOT_MARKER.search(output) is not None,
                "the installed export booted its bundled project")
        require("FAILED" not in output,
                "the run reported no failure")
    finally:
        run(["xcrun", "simctl", "terminate", udid, bundle_id], timeout=120,
            check=False)
        run(["xcrun", "simctl", "uninstall", udid, bundle_id], timeout=120,
            check=False)


# --- Android ----------------------------------------------------------------

ANDROID_ACTIVITY_CLASS = "com.orkitec.orkigeplayer.OrkigeActivity"


def adb_path():
    explicit = os.environ.get("ORKIGE_ADB", "")
    if explicit and os.path.isfile(explicit):
        return explicit
    sdk = os.environ.get("ANDROID_HOME",
                         os.environ.get("ANDROID_SDK_ROOT",
                                        os.path.expanduser(
                                            "~/Library/Android/sdk")))
    candidate = os.path.join(sdk, "platform-tools", "adb")
    if os.path.isfile(candidate):
        return candidate
    return shutil.which("adb") or ""


def android_package(project_dir, exe_name):
    manifest = ET.parse(os.path.join(project_dir, "project.orkproj")).getroot()
    settings = manifest.find("Settings")
    if settings is not None:
        for setting in settings.findall("Setting"):
            if setting.get("key") == "export.android.package":
                return setting.get("value", "")
    return "com.orkitec." + exe_name.lower()


def run_android(args):
    adb = adb_path()
    if not adb:
        skip("no adb binary (set ANDROID_HOME or ORKIGE_ADB)")
    if not os.path.isfile(os.path.join(args.engine_build, "tools", "player",
                                       "libmain.so")):
        skip("no built Android player under '%s' - build the matching android "
             "preset to enable this test" % args.engine_build)
    sdk = os.environ.get("ANDROID_HOME",
                         os.path.expanduser("~/Library/Android/sdk"))
    if not os.path.isfile(os.path.join(sdk, "build-tools", "35.0.0", "aapt2")):
        skip("no Android build-tools 35.0.0 under '%s'" % sdk)
    code, devices = run([adb, "devices"], timeout=120, check=False)
    attached = [line.split("\t")[0] for line in devices.splitlines()[1:]
                if line.strip().endswith("\tdevice")]
    if code != 0 or not attached:
        skip("no adb device/emulator connected")
    log("using device " + attached[0])

    _name, exe_name = project_names(args.project)
    package = android_package(args.project, exe_name)
    export(args.exporter, args.project, "android", args.engine_build,
           args.output)
    apk = os.path.join(args.output, exe_name + ".apk")
    require(os.path.isfile(apk), "the exporter produced an APK")

    run([adb, "uninstall", package], timeout=120, check=False)
    run([adb, "install", "-r", apk], timeout=args.deadline)
    log("installed " + package)
    try:
        run([adb, "logcat", "-c"], timeout=120, check=False)
        run([adb, "shell", "am", "start", "-n",
             package + "/" + ANDROID_ACTIVITY_CLASS], timeout=120)
        # the app keeps running (Android has no frame-cap env channel), so the
        # verdict is the boot line arriving in logcat within the deadline
        deadline = time.monotonic() + args.deadline
        transcript = ""
        while time.monotonic() < deadline:
            _code, transcript = run([adb, "logcat", "-d", "-s",
                                     "SDL", "orkige_player", "OrkigePlayer"],
                                    timeout=120, check=False)
            if BOOT_MARKER.search(transcript):
                break
            time.sleep(2.0)
        print(transcript[-4000:], end="", flush=True)
        require(BOOT_MARKER.search(transcript) is not None,
                "the installed export booted its bundled project on the device")
    finally:
        run([adb, "shell", "am", "force-stop", package], timeout=120,
            check=False)
        run([adb, "uninstall", package], timeout=120, check=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--platform", required=True,
                        choices=["ios-simulator", "android"])
    parser.add_argument("--engine-build", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--deadline", type=int, default=300)
    args = parser.parse_args()

    os.environ.setdefault("SIMCTL_CHILD_ORKIGE_DEMO_FRAMES", "120")
    os.environ.setdefault("SIMCTL_CHILD_ALSOFT_DRIVERS", "null")
    if args.platform == "ios-simulator":
        run_ios_simulator(args)
    else:
        run_android(args)
    log("PASS")


if __name__ == "__main__":
    main()
