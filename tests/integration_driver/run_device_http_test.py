#!/usr/bin/env python3
"""Run the engine's HTTP client on a REAL mobile runtime (stdlib only).

The desktop suite proves the client's contract in process; this driver proves
the same contract inside an exported app on an Android device/emulator or an
iOS Simulator, talking to a loopback server this process runs on the host:

    run_device_http_test.py --platform android|ios-simulator [--repo <root>]

It serves the endpoints tests/projects/http drives (/ok, /missing, /echo,
/big, /slow, /blob, plus the /config bootstrap and the /report verdict sink),
exports the fixture project with the `orkige_export` binary, installs it, launches
it and reads the app's verdict lines back. Android reaches the host through
'adb reverse'; an iOS Simulator app shares the host loopback directly.

Because a device's stdout is not a dependable transport, the app POSTs each
verdict line to /report - that is the channel the verdict is read from. The
device log (logcat / the simulator console) is captured alongside, parsed for
the same lines as a fallback, and printed on failure.

The one thing the app cannot check itself is the saved file's CONTENT (the
script sandbox has no file access), so after a passing run this driver reads
the downloaded file back off the device and compares it byte for byte with
what it served.

The loopback port is not defined here: it comes from the fixture manifest's
cvar.httpdevice.baseUrl setting, which the app reads as a cvar - one file
names it for both ends.

Exit codes: 0 the app reported RESULT pass, 77 skip (no device/emulator/
simulator, or a missing prerequisite build - the ctest SKIP_RETURN_CODE),
anything else fail. A skip is never used to hide a real failure: once the app
has been launched, no answer is a failure.

    run_device_http_test.py --selftest    # the endpoint handlers, the verdict
                                          # parser and argument handling, with
                                          # no device
"""

import argparse
import contextlib
import http.server
import io
import json
import os
import plistlib
import shutil
import socket
import zipfile
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

SKIP = 77

REPO_DEFAULT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
FIXTURE = os.path.join("tests", "projects", "http")

# the manifest setting that carries the base URL for BOTH ends
BASE_URL_SETTING = "cvar.httpdevice.baseUrl"

# what the fixture script expects of each endpoint (scripts/httpcheck.component.lua)
OK_BODY = b"orkige-http-device-ok"
OK_HEADER = ("X-Orkige-Note", "device-ok")
BLOB_BYTES = 32768
# /slow must answer well past the script's 2s leg timeout
SLOW_SECONDS = 6.0
# /big pacing: enough chunks that progress steps over several frames
BIG_CHUNK = 32768
BIG_CHUNK_DELAY = 0.015
BIG_MAX = 16 * 1024 * 1024

# the file the download leg saves to, inside the app's own writable container
DOWNLOAD_NAME = "http_device_download.bin"
# the Android activity class stays fully qualified whatever the package is
# renamed to (tools/player/android/AndroidManifest.xml)
ANDROID_ACTIVITY_CLASS = "com.orkitec.orkigeplayer.OrkigeActivity"

VERDICT_TAG = "[http-device] "


def log(message):
    print("run_device_http_test: " + message, flush=True)


def fail(message):
    print("run_device_http_test: FAILED - " + message, flush=True)
    sys.exit(1)


def skip(message):
    print("run_device_http_test: SKIP - " + message, flush=True)
    sys.exit(SKIP)


# --- pure helpers (the --selftest surface; no device, no subprocess) --------

def blob_payload(size):
    """The download leg's payload: deterministic and deliberately binary (it
    runs through 0x00, CR and LF), so a truncated or text-mangled readback
    cannot pass a byte comparison."""
    return bytes(index % 251 for index in range(size))


def filler_payload(size):
    """Recognisable filler for the sized /big transfers (their CONTENT is not
    under test - the announced size and the progress steps are)."""
    return bytes(ord("a") + (index % 26) for index in range(size))


def base_url_from_manifest(manifest_text):
    """The one place the endpoint is defined: the fixture manifest's
    cvar.httpdevice.baseUrl setting. Raises ValueError when it is absent, so a
    renamed/removed setting fails loudly instead of silently using a default."""
    root = ET.fromstring(manifest_text)
    for setting in root.iter("Setting"):
        if setting.get("key") == BASE_URL_SETTING:
            value = (setting.get("value") or "").strip()
            if value:
                return value
    raise ValueError("no '" + BASE_URL_SETTING + "' setting in the manifest")


def port_from_base_url(base_url):
    parsed = urllib.parse.urlsplit(base_url)
    if parsed.scheme != "http" or not parsed.port:
        raise ValueError("expected a plain http:// URL with an explicit port, "
                         "got '" + base_url + "'")
    return parsed.port


def route(method, path, body, save_path):
    """Every endpoint's decision, as pure data - the handler below only
    realizes it (writes the bytes, sleeps, records). Returns a dict:

      status        the HTTP status
      content_type  the Content-Type header value
      headers       extra response headers
      body          the exact bytes to send (None when 'stream' is used)
      stream        generate this many paced bytes instead of a fixed body
      delay         answer only after this many seconds
      record        a verdict line the caller must record (None otherwise)
    """
    answer = {"status": 200, "content_type": "text/plain", "headers": [],
              "body": b"", "stream": 0, "delay": 0.0, "record": None}
    parsed = urllib.parse.urlsplit(path)
    route_path = parsed.path

    if route_path == "/config":
        # only the host can name a path the app may write to
        answer["body"] = ("savePath=" + save_path + "\n").encode()
    elif route_path == "/ok":
        answer["body"] = OK_BODY
        answer["headers"] = [OK_HEADER]
    elif route_path == "/missing":
        answer["status"] = 404
        answer["body"] = b"no such thing"
    elif route_path == "/echo":
        if method != "POST":
            answer["status"] = 405
            answer["body"] = b"/echo takes POST"
        else:
            answer["body"] = body
            answer["content_type"] = "application/json"
    elif route_path == "/big":
        query = urllib.parse.parse_qs(parsed.query)
        try:
            size = int(query.get("bytes", ["0"])[0])
        except ValueError:
            size = -1
        if size < 0 or size > BIG_MAX:
            answer["status"] = 400
            answer["body"] = b"bad byte count"
        else:
            answer["stream"] = size
            answer["content_type"] = "application/octet-stream"
    elif route_path == "/slow":
        answer["delay"] = SLOW_SECONDS
        answer["body"] = b"late"
    elif route_path == "/blob":
        answer["body"] = blob_payload(BLOB_BYTES)
        answer["content_type"] = "application/octet-stream"
    elif route_path == "/report":
        answer["record"] = body.decode("utf-8", "replace").strip()
        answer["body"] = b"recorded"
    else:
        answer["status"] = 404
        answer["body"] = b"unknown endpoint"
    return answer


def verdict_lines(text_lines):
    """Keep only this run's verdict lines, in order and without duplicates -
    the same line arrives twice when the device log is readable as well as the
    POST channel."""
    kept = []
    seen = set()
    for raw in text_lines:
        position = raw.find(VERDICT_TAG)
        if position < 0:
            continue
        line = raw[position:].strip()
        if line not in seen:
            seen.add(line)
            kept.append(line)
    return kept


def read_verdict(lines):
    """(outcome, failures, legs) from a run's verdict lines. outcome is
    'pass', 'fail' or None when the app never reached a RESULT line; legs maps
    a leg name to its PASS/FAIL line."""
    outcome = None
    failures = 0
    legs = {}
    for line in verdict_lines(lines):
        rest = line[len(VERDICT_TAG):]
        if rest.startswith("PASS "):
            legs[rest[len("PASS "):].strip()] = line
        elif rest.startswith("FAIL "):
            legs[rest[len("FAIL "):].split(":")[0].strip()] = line
        elif rest.startswith("RESULT "):
            fields = rest.split()
            outcome = fields[1] if len(fields) > 1 else None
            if outcome == "fail" and len(fields) > 2:
                try:
                    failures = int(fields[2])
                except ValueError:
                    failures = -1
    return outcome, failures, legs


def parse_args(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--platform", choices=("android", "ios-simulator"))
    parser.add_argument("--repo", default=REPO_DEFAULT)
    parser.add_argument("--project", default=None,
                        help="the fixture project (default tests/projects/http)")
    parser.add_argument("--engine-build", default=None,
                        help="the platform's build tree (default build/"
                             "android-debug or build/ios-simulator-debug)")
    parser.add_argument("--output", default=None,
                        help="where the export lands (default a scratch dir "
                             "OUTSIDE the repo - a packaged APK/app is build "
                             "output, never source)")
    parser.add_argument("--serial", "--udid", dest="serial", default=None)
    parser.add_argument("--deadline", type=float, default=180.0,
                        help="seconds to wait for the app's verdict")
    parser.add_argument("--selftest", action="store_true")
    return parser.parse_args(argv)


# --- the loopback server ----------------------------------------------------

class TestServer(object):
    """The endpoints the fixture drives, on 127.0.0.1 at the manifest's port,
    served on a background thread. Shut down on every exit path."""

    def __init__(self, port, save_path=""):
        self.save_path = save_path
        self.reports = []
        self.lock = threading.Lock()
        driver = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, fmt, *args):
                pass    # this driver prints its own trail

            def _body(self):
                length = int(self.headers.get("Content-Length", "0") or "0")
                return self.rfile.read(length) if length > 0 else b""

            def _serve(self, method):
                answer = route(method, self.path, self._body(),
                               driver.save_path)
                if answer["record"] is not None:
                    driver.record(answer["record"])
                if answer["delay"] > 0.0:
                    time.sleep(answer["delay"])
                try:
                    self.send_response(answer["status"])
                    for name, value in answer["headers"]:
                        self.send_header(name, value)
                    self.send_header("Content-Type", answer["content_type"])
                    if answer["stream"] > 0:
                        self._stream(answer["stream"])
                    else:
                        self.send_header("Content-Length",
                                         str(len(answer["body"])))
                        self.end_headers()
                        self.wfile.write(answer["body"])
                except (BrokenPipeError, ConnectionResetError, OSError):
                    # the client cancelled or timed out mid-answer: both are
                    # legs under test, not server problems
                    self.close_connection = True

            def _stream(self, size):
                """The announced size up front, then paced chunks - so the
                app's progress callback genuinely steps."""
                self.send_header("Content-Length", str(size))
                self.end_headers()
                chunk = filler_payload(min(BIG_CHUNK, size))
                sent = 0
                while sent < size:
                    piece = chunk[:min(BIG_CHUNK, size - sent)]
                    self.wfile.write(piece)
                    self.wfile.flush()
                    sent += len(piece)
                    if sent < size:
                        time.sleep(BIG_CHUNK_DELAY)

            def do_GET(self):
                self._serve("GET")

            def do_POST(self):
                self._serve("POST")

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port),
                                                     Handler)
        self.httpd.daemon_threads = True
        self.thread = threading.Thread(target=self.httpd.serve_forever,
                                       daemon=True)
        self.thread.start()

    @property
    def port(self):
        return self.httpd.server_address[1]

    def record(self, line):
        with self.lock:
            self.reports.append(line)
            log("app: " + line)

    def lines(self):
        with self.lock:
            return list(self.reports)

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=5)


# --- process plumbing -------------------------------------------------------

def run(argv, timeout=300, check=True):
    """Run a command; returns (exitcode, combined output)."""
    log("$ " + " ".join(argv))
    try:
        result = subprocess.run(argv, timeout=timeout, text=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
    except (OSError, subprocess.TimeoutExpired) as error:
        if check:
            fail("'" + argv[0] + "' failed: " + str(error))
        return 127, str(error)
    output = result.stdout or ""
    if check and result.returncode != 0:
        sys.stdout.write(output[-4000:])
        fail("'" + " ".join(argv[:3]) + "' exited " + str(result.returncode))
    return result.returncode, output


class OutputTail(object):
    """Follow a long-running command's output on a thread (logcat, the
    simulator console), keeping the lines for the verdict + diagnostics."""

    def __init__(self, argv):
        self.lines = []
        self.lock = threading.Lock()
        log("$ " + " ".join(argv) + "   (streaming)")
        self.process = subprocess.Popen(argv, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True,
                                        bufsize=1)
        self.thread = threading.Thread(target=self._pump, daemon=True)
        self.thread.start()

    def _pump(self):
        try:
            for line in self.process.stdout:
                with self.lock:
                    self.lines.append(line.rstrip("\n"))
        except (OSError, ValueError):
            pass

    def snapshot(self):
        with self.lock:
            return list(self.lines)

    def stop(self):
        try:
            self.process.terminate()
            self.process.wait(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            try:
                self.process.kill()
            except OSError:
                pass


# each platform's OWN cleartext exemption, without which a packaged app cannot
# reach a loopback server at all. The plain-http legs would fail if it were
# missing, but they would fail as a confusing transport error - asserting the
# declaration itself names the cause, and pins that it ships in the INSTALLED
# artifact rather than only in the exporter's source.
ATS_KEY = "NSAppTransportSecurity"
ATS_LOCAL_KEY = "NSAllowsLocalNetworking"
ANDROID_NETWORK_CONFIG = "res/xml/orkige_network_security.xml"


def check_ios_cleartext_declaration(app_dir):
    """The iOS half: the installed bundle's Info.plist must allow local
    networking. Returns the evidence line."""
    plist_path = os.path.join(app_dir, "Info.plist")
    try:
        with open(plist_path, "rb") as handle:
            info = plistlib.load(handle)
    except (OSError, ValueError) as error:
        fail("cannot read the installed app's Info.plist at '" + plist_path
             + "': " + str(error))
    ats = info.get(ATS_KEY)
    if not isinstance(ats, dict) or ats.get(ATS_LOCAL_KEY) is not True:
        fail("the installed bundle does not declare " + ATS_KEY + " -> "
             + ATS_LOCAL_KEY + " = true (found " + repr(ats) + ") - iOS "
             "refuses a plain-http load without it, so the loopback legs "
             "below could not pass on a device")
    return (ATS_KEY + " -> " + ATS_LOCAL_KEY + " = true in the installed "
            "bundle (" + plist_path + ")")


def check_android_cleartext_declaration(apk_path):
    """The Android half: the packaged APK must ship the network security
    config that permits cleartext to loopback. Returns the evidence line."""
    try:
        with zipfile.ZipFile(apk_path) as archive:
            names = archive.namelist()
    except (OSError, zipfile.BadZipFile) as error:
        fail("cannot read the packaged APK '" + apk_path + "': " + str(error))
    if ANDROID_NETWORK_CONFIG not in names:
        matches = [name for name in names
                   if name.startswith("res/xml/") and "network" in name]
        fail("the packaged APK does not ship " + ANDROID_NETWORK_CONFIG
             + " (res/xml network entries: " + repr(matches) + ") - Android "
             "blocks cleartext without it, so the loopback legs below could "
             "not pass on a device")
    return ANDROID_NETWORK_CONFIG + " ships in the packaged APK"


def default_output(platform):
    """A packaged app is build output, so it must never land in the source
    tree: ctest passes its own path under the build dir, and a hand run gets a
    stable scratch dir outside the repo (kept, not deleted, so a failed run
    can be inspected)."""
    return os.path.join(tempfile.gettempdir(), "orkige_http_device", platform)


def find_exporter(repo):
    """the `orkige_export` binary: a HOST tool, so it comes from a desktop
    build tree in the repository (never the mobile tree being deployed, which
    cross-compiles a player and builds no host tools). ORKIGE_EXPORTER
    overrides."""
    override = os.environ.get("ORKIGE_EXPORTER", "")
    if override and os.path.isfile(override):
        return override
    suffix = ".exe" if os.name == "nt" else ""
    for preset in ("macos-release", "macos-debug", "macos-release-classic",
                   "macos-debug-classic", "linux-release", "linux-debug"):
        candidate = os.path.join(repo, "build", preset, "tools", "exporter",
                                 "orkige_export" + suffix)
        if os.path.isfile(candidate):
            return candidate
    fail("no orkige_export binary in any desktop build tree - build one "
         "(cmake --build --preset macos-debug --target orkige_export) or set "
         "ORKIGE_EXPORTER")


def export_project(repo, project_dir, platform, engine_build, output):
    exporter = [find_exporter(repo),
                "--project", project_dir, "--platform", platform,
                "--engine-build", engine_build, "--output", output]
    if os.path.exists(output):
        shutil.rmtree(output)
    code, out = run(exporter, timeout=900, check=False)
    if code != 0:
        sys.stdout.write(out[-4000:])
        fail("the exporter exited " + str(code))
    for line in out.splitlines():
        if line.startswith("orkige_export: OK"):
            log(line)


def project_names(project_dir):
    """(display name, executable name) - the exporter's own derivation."""
    manifest = ET.parse(os.path.join(project_dir,
                                     "project.orkproj")).getroot()
    name = (manifest.findtext("Name") or "").strip()
    return name, "".join(ch for ch in name if ch.isalnum())


def manifest_setting(project_dir, key, fallback):
    manifest = ET.parse(os.path.join(project_dir,
                                     "project.orkproj")).getroot()
    for setting in manifest.iter("Setting"):
        if setting.get("key") == key:
            return (setting.get("value") or "").strip() or fallback
    return fallback


SETTLE_SECONDS = 4.0


def await_verdict(server, tail, deadline_seconds):
    """Wait until the app reports a RESULT (on either channel) or the deadline
    passes. Returns (outcome, failures, legs, all lines).

    Each verdict line is its own request, so a RESULT can overtake a leg line
    that was posted just before it. After a RESULT, keep collecting until the
    leg lines stop arriving - otherwise a failing run could report a verdict
    without the FAIL line that explains it."""
    end = time.monotonic() + deadline_seconds
    outcome = None
    while time.monotonic() < end:
        lines = server.lines() + (tail.snapshot() if tail else [])
        outcome, failures, legs = read_verdict(lines)
        if outcome:
            break
        time.sleep(0.5)
    if outcome:
        settle_end = time.monotonic() + SETTLE_SECONDS
        seen = -1
        while time.monotonic() < settle_end and len(legs) != seen:
            seen = len(legs)
            time.sleep(1.0)
            lines = server.lines() + (tail.snapshot() if tail else [])
            outcome, failures, legs = read_verdict(lines)
        return outcome, failures, legs, lines
    lines = server.lines() + (tail.snapshot() if tail else [])
    outcome, failures, legs = read_verdict(lines)
    return outcome, failures, legs, lines


def report(outcome, failures, legs, lines, saved_check):
    """The one place a run's exit code is decided."""
    if saved_check:
        log(saved_check)
    if outcome == "pass":
        log("the app reported RESULT pass over %d legs" % len(legs))
        log("OK")
        return 0
    if outcome == "fail":
        for name in sorted(legs):
            if " FAIL " in legs[name]:
                log(legs[name])
        fail("the app reported RESULT fail %d" % failures)
    # launched, but nothing came back: a failure, never a skip
    tail_text = "\n".join(lines[-60:])
    if tail_text:
        sys.stdout.write("--- device output (tail) ---\n" + tail_text + "\n")
    fail("the app produced no verdict before the deadline (the loopback "
         "server saw " + str(len(legs)) + " leg lines)")
    return 1


# --- Android ----------------------------------------------------------------

def adb_path():
    """adb from ORKIGE_ADB, else the SDK, else PATH - the resolution
    tools/player/android/run_play_test.sh uses."""
    explicit = os.environ.get("ORKIGE_ADB", "")
    if explicit:
        # an unusable override is reported as "no adb", not as "no device"
        return explicit if (os.path.isfile(explicit)
                            or shutil.which(explicit)) else ""
    sdk = os.environ.get("ANDROID_HOME",
                         os.environ.get("ANDROID_SDK_ROOT", ""))
    if not sdk:
        sdk = os.path.join(os.path.expanduser("~"), "Library", "Android",
                           "sdk")
    candidate = os.path.join(sdk, "platform-tools", "adb")
    if os.path.isfile(candidate):
        return candidate
    return shutil.which("adb") or ""


def adb_devices(adb):
    code, output = run([adb, "devices"], timeout=60, check=False)
    if code != 0:
        return []
    serials = []
    for line in output.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            serials.append(fields[0])
    return serials


def run_android(args, base_url, port):
    adb = adb_path()
    if not adb:
        skip("no adb binary (set ANDROID_HOME or ORKIGE_ADB)")
    serial = args.serial
    if not serial:
        serials = adb_devices(adb)
        if not serials:
            skip("no adb device/emulator connected")
        serial = serials[0]
    adb_cmd = [adb, "-s", serial]

    engine_build = args.engine_build or os.path.join(args.repo, "build",
                                                     "android-debug")
    player = os.path.join(engine_build, "tools", "player", "libmain.so")
    if not os.path.isfile(player):
        skip("no built Android player at '" + player + "' - build the "
             "android-debug preset to enable this test")
    sdk = os.environ.get("ANDROID_HOME",
                         os.path.expanduser("~/Library/Android/sdk"))
    if not os.path.isfile(os.path.join(sdk, "build-tools", "35.0.0",
                                       "aapt2")):
        skip("no Android build-tools 35.0.0 under '" + sdk + "'")

    project_dir = args.project
    exe_name = project_names(project_dir)[1]
    package = manifest_setting(project_dir, "export.android.package",
                               "com.orkitec." + exe_name.lower())
    # the app's own writable dir - readable back through run-as on a debug APK
    save_path = "/data/data/" + package + "/files/" + DOWNLOAD_NAME

    server = TestServer(port, save_path)
    tail = None
    reversed_port = False
    installed = False
    try:
        run(adb_cmd + ["reverse", "tcp:%d" % port, "tcp:%d" % port],
            timeout=60)
        reversed_port = True
        log("host loopback %s is reachable from the device over adb reverse"
            % base_url)

        output = args.output or default_output("android")
        export_project(args.repo, project_dir, "android", engine_build, output)
        apk = os.path.join(output, exe_name + ".apk")
        if not os.path.isfile(apk):
            fail("the exporter produced no APK at '" + apk + "'")
        log("cleartext: " + check_android_cleartext_declaration(apk))

        run(adb_cmd + ["uninstall", package], timeout=120, check=False)
        run(adb_cmd + ["install", "-r", apk], timeout=600)
        installed = True

        run(adb_cmd + ["logcat", "-c"], timeout=60, check=False)
        # the player's stdio and engine log reach the platform log under the
        # 'orkige' tag; the two crash tags come along so a native abort is
        # visible in the tail this driver prints when a run produces no verdict
        tail = OutputTail(adb_cmd + ["logcat", "-v", "brief", "-s", "orkige",
                                     "DEBUG", "AndroidRuntime"])
        run(adb_cmd + ["shell", "am", "start", "-W", "-n",
                       package + "/" + ANDROID_ACTIVITY_CLASS], timeout=180)

        outcome, failures, legs, lines = await_verdict(server, tail,
                                                       args.deadline)
        saved = ""
        if outcome == "pass":
            saved = check_saved_file_android(adb_cmd, package, save_path)
        return report(outcome, failures, legs, lines, saved)
    finally:
        if tail:
            tail.stop()
        if installed:
            run(adb_cmd + ["shell", "am", "force-stop", package], timeout=60,
                check=False)
            run(adb_cmd + ["uninstall", package], timeout=120, check=False)
        if reversed_port:
            run(adb_cmd + ["reverse", "--remove", "tcp:%d" % port],
                timeout=60, check=False)
        server.stop()


def check_saved_file_android(adb_cmd, package, save_path):
    """Read the downloaded file back off the device and compare it with what
    was served. exec-out keeps the bytes raw (adb shell would translate line
    endings). A device that refuses run-as is reported, not failed - it says
    nothing about the engine."""
    try:
        result = subprocess.run(adb_cmd + ["exec-out", "run-as", package,
                                           "cat", save_path],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as error:
        return "saved-file readback unavailable (" + str(error) + ")"
    if result.returncode != 0:
        return ("saved-file readback unavailable (run-as exited %d: %s)"
                % (result.returncode,
                   (result.stderr or b"").decode("utf-8", "replace").strip()))
    return compare_saved(result.stdout)


def compare_saved(data):
    expected = blob_payload(BLOB_BYTES)
    if data != expected:
        fail("the downloaded file differs from what was served (%d bytes "
             "read, %d expected)" % (len(data), len(expected)))
    return ("saved file verified byte for byte on the device (%d bytes)"
            % len(expected))


# --- iOS Simulator ----------------------------------------------------------

def simulator_devices():
    """(booted, shutdown) iPhone simulator udids, newest listing order."""
    code, output = run(["xcrun", "simctl", "list", "devices", "available",
                        "-j"], timeout=120, check=False)
    if code != 0:
        return [], []
    try:
        listing = json.loads(output)
    except ValueError:
        return [], []
    booted = []
    shutdown = []
    for runtime, devices in listing.get("devices", {}).items():
        if "iOS" not in runtime:
            continue
        for device in devices:
            if not device.get("isAvailable", True):
                continue
            if "iPhone" not in device.get("name", ""):
                continue
            udid = device.get("udid", "")
            if not udid:
                continue
            if device.get("state") == "Booted":
                booted.append(udid)
            else:
                shutdown.append(udid)
    return booted, shutdown


def wait_for_boot(udid, deadline_seconds=600.0):
    """simctl boot returns before the device finishes coming up; installing
    into a half-booted simulator fails in ways that look like a test bug."""
    end = time.monotonic() + deadline_seconds
    while time.monotonic() < end:
        code, output = run(["xcrun", "simctl", "bootstatus", udid],
                           timeout=630, check=False)
        if code == 0:
            log("simulator " + udid + " finished booting")
            return
        time.sleep(5.0)
    fail("simulator " + udid + " did not finish booting")


def run_ios_simulator(args, base_url, port):
    if sys.platform != "darwin":
        skip("the iOS Simulator flow needs macOS")
    if not shutil.which("xcrun"):
        skip("no xcrun - install the Xcode command line tools")

    engine_build = args.engine_build or os.path.join(args.repo, "build",
                                                     "ios-simulator-debug")
    player_app = os.path.join(engine_build, "tools", "player",
                              "OrkigePlayer.app")
    if not os.path.isdir(player_app):
        skip("no built iOS Simulator player at '" + player_app + "' - build "
             "the ios-simulator-debug preset to enable this test")

    udid = args.serial
    booted_here = False
    if not udid:
        booted, shutdown = simulator_devices()
        if booted:
            udid = booted[0]
        elif shutdown:
            udid = shutdown[0]
            run(["xcrun", "simctl", "boot", udid], timeout=600)
            booted_here = True
            # bring the simulator's UI up as well, so the launched app has a
            # real display session to render into (the same pairing the
            # editor's Play-on-simulator and Util/orkige_device.py use)
            run(["open", "-a", "Simulator"], timeout=120, check=False)
            wait_for_boot(udid)
        else:
            skip("no available iPhone simulator")
    log("using simulator " + udid)

    project_dir = args.project
    name, exe_name = project_names(project_dir)
    bundle_id = manifest_setting(project_dir, "export.ios.bundleId",
                                 "com.orkitec." + exe_name.lower())

    # an app's data container path is per-install, so the save path can only
    # be resolved once the app is installed - hence /config, asked for by the
    # app at startup rather than baked into it
    server = TestServer(port)
    tail = None
    installed = False
    try:
        output = args.output or default_output("ios-simulator")
        export_project(args.repo, project_dir, "ios-simulator", engine_build,
                       output)
        app = os.path.join(output, name + ".app")
        if not os.path.isdir(app):
            fail("the exporter produced no app at '" + app + "'")

        run(["xcrun", "simctl", "uninstall", udid, bundle_id], timeout=120,
            check=False)
        run(["xcrun", "simctl", "install", udid, app], timeout=300)
        installed = True

        # the INSTALLED bundle, not the staged one: what the simulator will
        # actually launch is what has to carry the exemption
        code, installed = run(["xcrun", "simctl", "get_app_container", udid,
                               bundle_id, "app"], timeout=120, check=False)
        if code != 0 or not installed.strip():
            fail("could not resolve the installed app bundle")
        log("cleartext: "
            + check_ios_cleartext_declaration(installed.strip()))

        code, container = run(["xcrun", "simctl", "get_app_container", udid,
                               bundle_id, "data"], timeout=120, check=False)
        if code != 0 or not container.strip():
            fail("could not resolve the installed app's data container")
        server.save_path = os.path.join(container.strip(), "Documents",
                                        DOWNLOAD_NAME)
        log("the app will save its download to " + server.save_path)
        log("host loopback %s is shared with the simulator" % base_url)

        # --console-pty attaches the app's stdout, so its printed lines are
        # visible here as well as arriving over /report
        tail = OutputTail(["xcrun", "simctl", "launch", "--console-pty", udid,
                           bundle_id])

        outcome, failures, legs, lines = await_verdict(server, tail,
                                                       args.deadline)
        saved = ""
        if outcome == "pass":
            saved = check_saved_file_local(server.save_path)
        return report(outcome, failures, legs, lines, saved)
    finally:
        if tail:
            tail.stop()
        run(["xcrun", "simctl", "terminate", udid, bundle_id], timeout=120,
            check=False)
        if installed:
            run(["xcrun", "simctl", "uninstall", udid, bundle_id],
                timeout=120, check=False)
        if booted_here:
            run(["xcrun", "simctl", "shutdown", udid], timeout=300,
                check=False)
        server.stop()


def check_saved_file_local(save_path):
    """A simulator's container lives on the host filesystem, so the saved file
    is read directly."""
    try:
        with open(save_path, "rb") as handle:
            return compare_saved(handle.read())
    except OSError as error:
        fail("the app reported a saved download but '" + save_path
             + "' cannot be read: " + str(error))


# --- selftest ---------------------------------------------------------------

def expect_refusal(what, call, *arguments):
    """Run a check that MUST refuse, swallowing the failure line it prints -
    a passing selftest log must not contain the word FAILED."""
    quiet = io.StringIO()
    try:
        with contextlib.redirect_stdout(quiet):
            call(*arguments)
    except SystemExit as exit_code:
        if exit_code.code == 1:
            return quiet.getvalue()
        raise AssertionError(what + " refused with exit " + str(exit_code.code))
    raise AssertionError(what + " was accepted but must be refused")


def selftest():
    repo = REPO_DEFAULT
    project_dir = os.path.join(repo, FIXTURE)

    # the endpoint definition really does live in ONE file, and the script
    # reads that same key
    with open(os.path.join(project_dir, "project.orkproj")) as handle:
        manifest_text = handle.read()
    base_url = base_url_from_manifest(manifest_text)
    port = port_from_base_url(base_url)
    assert base_url.startswith("http://127.0.0.1:"), base_url
    assert 45000 <= port <= 46000, port
    script = os.path.join(project_dir, "scripts", "httpcheck.component.lua")
    with open(script) as handle:
        script_text = handle.read()
    assert BASE_URL_SETTING[len("cvar."):] in script_text, \
        "the script must read the cvar the manifest defines"
    assert str(port) not in script_text, \
        "the port must not be repeated in the script - the manifest owns it"
    try:
        base_url_from_manifest("<OrkigeProject/>")
        raise AssertionError("a manifest without the setting must raise")
    except ValueError:
        pass
    for bad in ("https://127.0.0.1:45411", "http://127.0.0.1"):
        try:
            port_from_base_url(bad)
            raise AssertionError("'" + bad + "' must be rejected")
        except ValueError:
            pass

    # the endpoint decisions
    answer = route("GET", "/ok", b"", "/tmp/x")
    assert answer["status"] == 200 and answer["body"] == OK_BODY
    assert OK_HEADER in answer["headers"]
    assert route("GET", "/missing", b"", "")["status"] == 404
    assert route("POST", "/echo", b"hello", "")["body"] == b"hello"
    assert route("GET", "/echo", b"", "")["status"] == 405
    assert route("GET", "/config", b"", "/data/x")["body"] == \
        b"savePath=/data/x\n"
    assert route("GET", "/big?bytes=1024", b"", "")["stream"] == 1024
    assert route("GET", "/big?bytes=nope", b"", "")["status"] == 400
    assert route("GET", "/big?bytes=%d" % (BIG_MAX + 1), b"", "")["status"] \
        == 400
    assert route("GET", "/slow", b"", "")["delay"] == SLOW_SECONDS
    assert len(route("GET", "/blob", b"", "")["body"]) == BLOB_BYTES
    assert route("POST", "/report", b" [http-device] PASS ok-200 \n",
                 "")["record"] == "[http-device] PASS ok-200"
    assert route("GET", "/nowhere", b"", "")["status"] == 404

    # payload determinism, and that the blob really is binary (a text-mangling
    # readback must not be able to pass the comparison)
    assert blob_payload(BLOB_BYTES) == blob_payload(BLOB_BYTES)
    assert len(set(blob_payload(1024))) > 200
    assert b"\x00" in blob_payload(BLOB_BYTES)
    assert b"\r" in blob_payload(BLOB_BYTES) and b"\n" in \
        blob_payload(BLOB_BYTES)

    # the verdict parser
    pass_run = [
        "I/stdout: [http-device] PASS ok-200",
        "[http-device] PASS download",
        "[http-device] RESULT pass",
    ]
    outcome, failures, legs = read_verdict(pass_run)
    assert outcome == "pass" and failures == 0
    assert sorted(legs) == ["download", "ok-200"], legs
    fail_run = [
        "[http-device] PASS ok-200",
        "[http-device] FAIL timeout: got ok=true status=200",
        "[http-device] RESULT fail 1",
    ]
    outcome, failures, legs = read_verdict(fail_run)
    assert outcome == "fail" and failures == 1
    assert "timeout" in legs and " FAIL " in legs["timeout"]
    # a run that never reached a RESULT is not a pass
    assert read_verdict(["[http-device] PASS ok-200"])[0] is None
    assert read_verdict([])[0] is None
    assert read_verdict(["nothing to see"])[0] is None
    # the same line on both channels is one line
    assert verdict_lines(["[http-device] PASS a",
                          "I/stdout: [http-device] PASS a"]) == \
        ["[http-device] PASS a"]

    # the cleartext-declaration checks, both directions, on synthetic bundles
    scratch = tempfile.mkdtemp(prefix="orkige_http_selftest_")
    good_app = os.path.join(scratch, "Good.app")
    os.makedirs(good_app)
    with open(os.path.join(good_app, "Info.plist"), "wb") as handle:
        plistlib.dump({ATS_KEY: {ATS_LOCAL_KEY: True}}, handle)
    evidence = check_ios_cleartext_declaration(good_app)
    assert ATS_LOCAL_KEY in evidence, evidence
    bad_app = os.path.join(scratch, "Bad.app")
    os.makedirs(bad_app)
    with open(os.path.join(bad_app, "Info.plist"), "wb") as handle:
        plistlib.dump({"CFBundleName": "Bad"}, handle)
    said = expect_refusal("a bundle without the exemption",
                          check_ios_cleartext_declaration, bad_app)
    assert ATS_LOCAL_KEY in said, "the refusal must name the missing key"
    expect_refusal("a bundle with no Info.plist",
                   check_ios_cleartext_declaration,
                   os.path.join(scratch, "Absent.app"))
    good_apk = os.path.join(scratch, "good.apk")
    with zipfile.ZipFile(good_apk, "w") as archive:
        archive.writestr(ANDROID_NETWORK_CONFIG, "<network-security-config/>")
    assert ANDROID_NETWORK_CONFIG in check_android_cleartext_declaration(
        good_apk)
    bad_apk = os.path.join(scratch, "bad.apk")
    with zipfile.ZipFile(bad_apk, "w") as archive:
        archive.writestr("res/xml/other.xml", "<x/>")
    said = expect_refusal("an APK without the network security config",
                          check_android_cleartext_declaration, bad_apk)
    assert ANDROID_NETWORK_CONFIG in said, "the refusal must name the file"
    shutil.rmtree(scratch, ignore_errors=True)

    # argument handling
    args = parse_args(["--platform", "android"])
    assert args.platform == "android" and args.repo == REPO_DEFAULT
    args = parse_args(["--platform", "ios-simulator", "--udid", "ABC",
                       "--deadline", "5"])
    assert args.serial == "ABC" and args.deadline == 5.0
    assert parse_args(["--selftest"]).selftest is True

    # the real server, end to end on an ephemeral port: the handlers, the
    # announced size, the paced stream and the report sink
    server = TestServer(0, "/tmp/from-config")
    try:
        base = "http://127.0.0.1:%d" % server.port
        with urllib.request.urlopen(base + "/ok", timeout=10) as answer:
            assert answer.read() == OK_BODY
            assert answer.headers.get(OK_HEADER[0]) == OK_HEADER[1]
        with urllib.request.urlopen(base + "/config", timeout=10) as answer:
            assert answer.read() == b"savePath=/tmp/from-config\n"
        with urllib.request.urlopen(base + "/blob", timeout=10) as answer:
            assert answer.read() == blob_payload(BLOB_BYTES)
        posted = urllib.request.Request(base + "/echo", data=b'{"a":1}',
                                        method="POST")
        with urllib.request.urlopen(posted, timeout=10) as answer:
            assert answer.read() == b'{"a":1}'
        with urllib.request.urlopen(base + "/big?bytes=131072",
                                    timeout=30) as answer:
            assert answer.headers.get("Content-Length") == "131072"
            assert len(answer.read()) == 131072
        try:
            urllib.request.urlopen(base + "/missing", timeout=10)
            raise AssertionError("/missing must answer 404")
        except urllib.error.HTTPError as error:
            assert error.code == 404
        line = VERDICT_TAG + "RESULT pass"
        reported = urllib.request.Request(base + "/report",
                                          data=line.encode(), method="POST")
        with urllib.request.urlopen(reported, timeout=10) as answer:
            answer.read()
        assert read_verdict(server.lines())[0] == "pass"
    finally:
        server.stop()
    # the port really was released
    probe = socket.socket()
    try:
        probe.bind(("127.0.0.1", 0))
    finally:
        probe.close()

    print("run_device_http_test: selftest OK")


def main(argv):
    args = parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if not args.platform:
        fail("--platform is required (android or ios-simulator)")
    if args.project is None:
        args.project = os.path.join(args.repo, FIXTURE)
    if not os.path.isdir(args.project):
        fail("no fixture project at '" + args.project + "'")
    with open(os.path.join(args.project, "project.orkproj")) as handle:
        base_url = base_url_from_manifest(handle.read())
    port = port_from_base_url(base_url)

    if args.platform == "android":
        return run_android(args, base_url, port)
    return run_ios_simulator(args, base_url, port)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
