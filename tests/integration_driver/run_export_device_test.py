#!/usr/bin/env python3
"""ctest driver for RUNNING a mobile export on a real device: package the
project, install the artifact on an iOS Simulator / Android device, launch it
and wait for the bundled project to boot and render its frames.

    run_export_device_test.py --repo <root> --project <dir>
                              --exporter <orkige_export>
                              --platform ios-simulator|android
                              --engine-build <dir> --output <dir>
                              [--engine-source build-tree|payload]
                              [--host-build <dir>] [--deadline 300]

The structural export tests (run_export_test.py) assert what a package
CONTAINS. This one asserts that what it contains is enough: a payload missing a
scene, a shader tree or a texture is a package that installs perfectly and then
boots into nothing, which no amount of file-list checking catches.

The ENGINE SOURCE is the second axis, because the same app is packaged three
ways. `build-tree` is the developer case (the platform's preset tree). `payload`
is what a DOWNLOADED editor does: it carries no phone player, so it packages
from a FETCHED device payload beside its own staged resources
(Docs/device-payloads.md). Byte-comparing the two packages proves they agree;
only running one proves either is a game.

`pack` is the third, and it is a different KIND of app: a project whose game
code is compiled C++ ships the module itself, so there is no player to copy at
all. The engine is an installed SDK pack (Docs/sdk-pack.md) - installed from the
platform's preset tree here, then RELOCATED by renaming it, exactly as a
downloaded one would be - and the export line names no build tree, so what runs
on the device was built from the pack and nothing else.

Exit codes: 0 pass, 77 skip (no built player, no device, no SDK tool - the
ctest SKIP_RETURN_CODE), anything else fail.
"""

import argparse
import importlib.util
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

SKIP = 77


#: logcat tags the player's own lines arrive under. SDL routes an app's
#: SDL_Log through "SDL/APP" rather than "SDL", so a filter that names only
#: the latter reads a perfect run as a failure - the boot and frame markers
#: below are exactly those lines.
ANDROID_LOG_TAGS = ("SDL", "SDL/APP", "Orkige", "orkige_player",
                    "OrkigePlayer")


def boot_marker(tag):
    """the line a runtime prints once its project's scene is live - the proof
    the payload was complete enough to reach a running game. The TAG is the
    runtime's own name: the generic player for a Lua project, the project's own
    module where the game code is compiled."""
    return re.compile(tag + r": scene '.*' loaded \((\d+) GameObjects\)")


def frame_marker(tag):
    """...and the line the frame-capped run prints when it ENDS. Booting proves
    the payload resolves; this proves the app RENDERED the frames asked for."""
    return re.compile(tag + r": frame stats - (\d+) frames")


#: the device payload an iOS-simulator package is fetched from, and the ordered
#: version its install directory is named for (any release identity will do -
#: nothing here is published)
IOS_PAYLOAD_ID = "player-ios-simulator"
PAYLOAD_VERSION = "2.0.0-nightly.20260802+abcdef123"


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


def load_module(name, path):
    """import a tool by path (the packaging tool is not on sys.path)"""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def stage_device_payload(args, work):
    """compose the platform's device payload from its preset build tree, plus
    the host resource root a distributed editor carries beside it.

    Both halves come from the PACKAGING tool's own functions, so what is under
    test is the shipped composition rather than a second one written here.
    Returns (payload dir, bundle resources dir), or ("", "") when this machine
    built no such player."""
    sys.path.insert(0, os.path.join(args.repo, "Util"))
    packaging = load_module("orkige_nightly_package",
                            os.path.join(args.repo, "Util",
                                         "orkige_nightly_package.py"))
    payload = os.path.join(work, "payloads", IOS_PAYLOAD_ID)
    if not packaging.compose_device_payload(payload, IOS_PAYLOAD_ID,
                                            args.engine_build,
                                            PAYLOAD_VERSION,
                                            commit="abcdef123"):
        return "", ""
    problems = packaging.device_payload_problems(payload, IOS_PAYLOAD_ID)
    if problems:
        fail("the composed %s payload is incomplete: %s"
             % (IOS_PAYLOAD_ID, ", ".join(problems)))
    # the editor's own staged resources - the OTHER engine source of the two a
    # downloaded editor packages a phone build from
    bundle = os.path.join(work, "bundle")
    packaging.stage_engine_media(args.host_build, os.path.join(bundle, "Media"))
    log("composed the %s payload the way a release publishes one (%s)"
        % (IOS_PAYLOAD_ID, payload))
    return payload, bundle


def install_pack(args, work):
    """install the platform's SDK pack from its preset tree and RELOCATE it.

    A pack a user downloads is never at the path it was built at, so renaming
    it is the cheapest honest proof; everything after reads only the renamed
    copy. Returns the pack root, or "" when the tree carries no install rules
    (a preset that predates them)."""
    staged = os.path.join(work, "installed")
    code, output = run([shutil.which("cmake") or "cmake", "--install",
                        args.engine_build, "--prefix", staged,
                        "--component", "sdk"], timeout=1800, check=False)
    if code != 0:
        print(output[-4000:], flush=True)
        fail("installing the SDK pack from '%s' failed" % args.engine_build)
    pack = os.path.join(work, "unpacked-elsewhere", "orkige-sdk")
    os.makedirs(os.path.dirname(pack), exist_ok=True)
    os.rename(staged, pack)
    log("installed the SDK pack and relocated it to " + pack)
    return pack


def stage_project(args, work):
    """a COPY of the project to package, so the module builds in scratch space
    and the repository's own trees stay untouched.

    A project written against a distributed engine carries its own sources; the
    reference project shares two headers with the C++ sample in the tree, and a
    pack has no checkout to reach them through - so they are copied in beside
    its module, which is what any self-contained project does."""
    project = os.path.join(work, os.path.basename(args.project.rstrip("/")))
    shutil.rmtree(project, ignore_errors=True)
    shutil.copytree(args.project, project,
                    ignore=shutil.ignore_patterns("build*", "builds",
                                                  ".orkige"))
    if args.shared_headers:
        native = os.path.join(project, "native")
        for name in sorted(os.listdir(args.shared_headers)):
            if name.endswith(".h") and os.path.isdir(native):
                shutil.copy2(os.path.join(args.shared_headers, name),
                             os.path.join(native, name))
    return project


def export(args, output, payload="", bundle="", pack="", project=""):
    if os.path.exists(output):
        shutil.rmtree(output)
    argv = [args.exporter, "--project", project or args.project,
            "--platform", args.platform]
    if pack:
        # the SDK pack is the WHOLE engine source here: the app IS the
        # project's compiled module, so no build tree and no player payload
        # are named at all
        argv += ["--sdk-pack", pack]
    elif payload:
        # the two-source shape a downloaded editor uses: its own staged
        # resources plus the fetched device player, and NO repository
        argv += ["--engine-bundle", bundle, "--device-payload", payload]
    else:
        argv += ["--engine-build", args.engine_build]
    argv += ["--output", output]
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


class ConsoleTail(object):
    """follow `simctl launch --console-pty` on a thread, keeping the app's own
    output for the verdict.

    The launch command is deliberately NOT waited on. An iOS app whose main
    function returns stays alive in UIKit's run loop, so the console command
    outlives the run it is streaming - waiting for it to return would time out
    on a run that finished in seconds. The verdict is therefore what the APP
    printed, and the driver stops both when it has it."""

    def __init__(self, argv, environment):
        self.lines = []
        self.lock = threading.Lock()
        log("$ " + " ".join(argv) + "   (streaming)")
        self.process = subprocess.Popen(argv, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True,
                                        errors="replace", bufsize=1,
                                        env=environment)
        self.thread = threading.Thread(target=self._pump, daemon=True)
        self.thread.start()

    def _pump(self):
        try:
            for line in self.process.stdout:
                with self.lock:
                    self.lines.append(line.rstrip("\n"))
        except (OSError, ValueError):
            pass

    def text(self):
        with self.lock:
            return "\n".join(self.lines)

    def finished(self):
        """has the console command itself ended? It outlives a healthy run, so
        this means the app is GONE - a crash, not a completion."""
        return self.process.poll() is not None

    def stop(self):
        try:
            self.process.terminate()
            self.process.wait(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            try:
                self.process.kill()
            except OSError:
                pass


def await_marker(tail, pattern, deadline, what):
    """wait for one of the app's own lines, or fail with its transcript"""
    end = time.monotonic() + deadline
    ended = 0.0
    reason = "within %ds" % deadline
    while time.monotonic() < end:
        match = pattern.search(tail.text())
        if match:
            return match
        if tail.finished():
            # the console outlives a healthy run, so its end means the app
            # died: drain the last lines, then report instead of waiting out
            # the whole deadline on a process that is never coming back
            if ended == 0.0:
                ended = time.monotonic()
            elif time.monotonic() - ended > 3.0:
                reason = "- it stopped running first"
                break
        time.sleep(1.0)
    print(tail.text()[-8000:], flush=True)
    fail("the installed export never %s %s" % (what, reason))
    return None


def run_ios_simulator(args):
    if sys.platform != "darwin" or not shutil.which("xcrun"):
        skip("the iOS Simulator flow needs macOS with the Xcode command line "
             "tools")
    # what the tree must carry to package at all: a built player for the two
    # player-shaped sources, an installable SDK pack for the module-shaped one
    if args.engine_source == "pack":
        if not os.path.isfile(os.path.join(args.engine_build,
                                           "OrkigeConfig.cmake")):
            skip("'%s' carries no Orkige package to install an SDK pack from - "
                 "build the matching ios-simulator preset to enable this test"
                 % args.engine_build)
        if not shutil.which("cmake") or not shutil.which("ninja"):
            skip("building a native module needs cmake and ninja on this "
                 "machine")
    else:
        player_app = os.path.join(args.engine_build, "tools", "player",
                                  "OrkigePlayer.app")
        if not os.path.isdir(player_app):
            skip("no built iOS Simulator player at '%s' - build the matching "
                 "ios-simulator preset to enable this test" % player_app)
    udid = first_simulator()
    if not udid:
        skip("no BOOTED iPhone simulator (boot one with 'xcrun simctl boot')")
    log("using simulator " + udid)

    payload = bundle = pack = project = ""
    if args.engine_source == "payload":
        payload, bundle = stage_device_payload(
            args, os.path.join(args.output, "engine-source"))
        if not payload:
            skip("no iOS Simulator player to compose a device payload from "
                 "under '%s'" % args.engine_build)
    elif args.engine_source == "pack":
        work = os.path.join(args.output, "engine-source")
        shutil.rmtree(work, ignore_errors=True)
        pack = install_pack(args, work)
        project = stage_project(args, work)

    name, _exe = project_names(args.project)
    export(args, os.path.join(args.output, "package"), payload, bundle, pack,
           project)
    app = os.path.join(args.output, "package", name + ".app")
    require(os.path.isdir(app), "the exporter produced an app bundle")
    with open(os.path.join(app, "Info.plist"), "rb") as handle:
        bundle_id = plistlib.load(handle)["CFBundleIdentifier"]

    run(["xcrun", "simctl", "uninstall", udid, bundle_id], timeout=120,
        check=False)
    run(["xcrun", "simctl", "install", udid, app], timeout=300)
    log("installed " + bundle_id)
    tail = None
    try:
        # --console-pty attaches the app's stdout; the frame cap and the
        # end-of-run measurement line ride in as child environment variables,
        # so the app itself says when it is done
        tail = ConsoleTail(["xcrun", "simctl", "launch", "--console-pty",
                            udid, bundle_id], os.environ.copy())
        await_marker(tail, boot_marker(args.log_tag), args.deadline,
                     "booted its bundled project")
        log("ok: the installed export booted its bundled project")
        frames = await_marker(tail, frame_marker(args.log_tag), args.deadline,
                              "reached its frame cap")
        require(int(frames.group(1)) > 0,
                "the installed export RENDERED frames (%s measured)"
                % frames.group(1))
        require("FAILED" not in tail.text(), "the run reported no failure")
    finally:
        if tail is not None:
            tail.stop()
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
    export(args, args.output)
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
        boot = boot_marker(args.log_tag)
        transcript = ""
        while time.monotonic() < deadline:
            _code, transcript = run([adb, "logcat", "-d", "-s"]
                                    + list(ANDROID_LOG_TAGS),
                                    timeout=120, check=False)
            if boot.search(transcript):
                break
            time.sleep(2.0)
        print(transcript[-4000:], end="", flush=True)
        require(boot.search(transcript) is not None,
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
    parser.add_argument("--engine-build", required=True,
                        help="the platform's preset build tree - the engine "
                             "source itself, or (payload mode) the tree the "
                             "device payload is composed from")
    parser.add_argument("--engine-source", default="build-tree",
                        choices=["build-tree", "payload", "pack"],
                        help="package from the preset build tree (the "
                             "developer case), from a fetched device payload "
                             "beside staged editor resources (what a "
                             "downloaded editor does), or - for a project "
                             "whose game code is compiled - from an SDK pack "
                             "installed off that tree and relocated")
    parser.add_argument("--log-tag", default="orkige_player",
                        help="the runtime's own log tag, which is what its "
                             "boot and frame lines are prefixed with: the "
                             "generic player, or a project's own module")
    parser.add_argument("--shared-headers", default="",
                        help="pack mode: headers copied beside the staged "
                             "project's module, standing in for the ones a "
                             "self-contained project carries itself")
    parser.add_argument("--host-build", default="",
                        help="payload mode: the host build tree the staged "
                             "editor resources are composed from")
    parser.add_argument("--output", required=True)
    parser.add_argument("--deadline", type=int, default=300)
    args = parser.parse_args()
    if args.engine_source == "payload" and not os.path.isdir(args.host_build):
        fail("payload mode needs --host-build <configured build tree> to "
             "compose the staged editor resources from")

    os.environ.setdefault("SIMCTL_CHILD_ORKIGE_DEMO_FRAMES", "120")
    # the end-of-run measurement line is what says the frames HAPPENED
    os.environ.setdefault("SIMCTL_CHILD_ORKIGE_DEMO_FPS_LOG", "1")
    # the simulator's CoreAudio device is not usable from a test run: opening
    # one times out inside CoreAudio and aborts the app before it ever boots a
    # scene. The null backend keeps the audio system real without a device.
    os.environ.setdefault("SIMCTL_CHILD_ALSOFT_DRIVERS", "null")
    if args.platform == "ios-simulator":
        run_ios_simulator(args)
    else:
        run_android(args)
    log("PASS")


if __name__ == "__main__":
    main()
