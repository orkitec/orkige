#!/usr/bin/env python3
"""One-command phone-session prep for Orkige - readiness, deploy and run.

Plug in a physical phone (or point at the orkige_test emulator / an iOS
simulator) and this drives the whole loop so a session needs no archaeology:

    python3 Util/orkige_device.py doctor            # honest readiness report
    python3 Util/orkige_device.py android           # build + package + install
                                                     # + launch + stream logcat
    python3 Util/orkige_device.py ios               # signed device deploy-and-run
    python3 Util/orkige_device.py ios --simulator   # simulator fallback

It reuses the existing tooling wholesale rather than reinventing it:
  * the packaging path is Util/orkige_export.py (which itself drives
    tools/player/android/package_apk.sh and the iOS bundle signing seam);
  * the project manifest / package-id logic is orkige_export.Project;
  * device enumeration mirrors tools/editor/EditorDeviceTargets.cpp
    (adb devices -l, xcrun simctl list, security find-identity, the
    ORKIGE_IOS_* env seam of Docs/ios-signing.md).

Stdlib-only (python_stdlib_lint gates the whole Util/ toolchain) and honest:
the doctor never fails on absent developer credentials - it reports them as
ACTION NEEDED with the exact doc pointer. Owner runbook: Docs/device-session.md.

  python3 Util/orkige_device.py --selftest    # pure routing/report/command
                                              # checks, no real devices
"""

import os
import shutil
import subprocess
import sys
import time

UTIL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(UTIL_DIR)
if UTIL_DIR not in sys.path:
    sys.path.insert(0, UTIL_DIR)
import orkige_export  # sibling Util module: Project + the packaging seam

# The Android activity class is named fully qualified in the APK manifest
# regardless of a renamed package (tools/player/android/AndroidManifest.xml),
# so the launch component is always <package>/<this class>.
ANDROID_ACTIVITY_CLASS = "com.orkitec.orkigeplayer.OrkigeActivity"
# The iOS simulator player installs under this bundle id (tools/player).
SIMULATOR_BUNDLE_ID = "com.orkitec.orkige-player"
# The env seam for iOS device signing - machine-local, never committed
# (Docs/ios-signing.md). Mirrors orkige_export's constants.
IOS_IDENTITY_ENV = orkige_export.IOS_SIGNING_IDENTITY_ENV
IOS_PROFILE_ENV = orkige_export.IOS_PROVISIONING_PROFILE_ENV
IOS_SIGNING_DOC = "Docs/ios-signing.md"

DEFAULT_PROJECT = os.path.join("projects", "benchmark")

# Build trees a phone session cares about: preset -> the built binary whose
# mtime dates the tree (staleness anchor).
BUILD_TREES = {
    "android-debug": os.path.join("tools", "player", "libmain.so"),
    "ios-device-debug": os.path.join("tools", "player", "OrkigePlayer.app",
                                     "OrkigePlayer"),
    "ios-device-release": os.path.join("tools", "player", "OrkigePlayer.app",
                                       "OrkigePlayer"),
    "ios-simulator-debug": os.path.join("tools", "player", "OrkigePlayer.app",
                                        "OrkigePlayer"),
}


def log(message):
    print("orkige_device: " + message, flush=True)


def fail(message):
    print("orkige_device: ERROR: " + message, file=sys.stderr, flush=True)
    sys.exit(1)


# --- pure helpers (unit-tested via --selftest; no subprocess/filesystem) ----

def classify_staleness(binary_mtime, commit_time):
    """Coarse, honest freshness of a build tree: 'missing' (never built),
    'stale' (its binary predates the newest source commit) or 'fresh'. Both
    times are epoch seconds; a None binary_mtime means no binary on disk."""
    if binary_mtime is None:
        return "missing"
    if commit_time is not None and binary_mtime < commit_time:
        return "stale"
    return "fresh"


def android_package(project):
    """The launch package id, exactly as orkige_export derives it: the manifest
    override else com.orkitec.<slug> (pure - reads only the parsed Project)."""
    return (project.settings.get("export.android.package", "").strip()
            or "com.orkitec." + project.id_slug)


def ios_signing_status(has_identity, environ):
    """Assemble the iOS device-signing readiness from an injected identity
    probe (has_identity, the result of `security find-identity`) plus the
    environment (the ORKIGE_IOS_* profile seam). Pure - the caller runs the
    probe; this stays cert-free so it is unit-testable. Returns a dict with
    'identity', 'profile', 'profile_path', 'team' and a 'configured' bool."""
    profile_path = (environ.get(IOS_PROFILE_ENV, "") or "").strip()
    profile_present = bool(profile_path) and os.path.isfile(profile_path)
    return {
        "identity": bool(has_identity),
        "profile": profile_present,
        "profile_path": profile_path,
        "configured": bool(has_identity) and profile_present,
    }


def android_steps(cmake, preset, staleness, export_py, project_dir, scene,
                  adb, serial, package, apk_path):
    """The ordered command list an `android` run executes: (re)build the
    player only when the tree is not fresh, package via orkige_export.py,
    install, then launch. Each element is (label, argv). Pure and injectable
    so --selftest can assert construction without a device. The logcat stream
    is set up separately (it needs the launched pid)."""
    steps = []
    if staleness != "fresh":
        steps.append(("build", [cmake, "--build", "--preset", preset,
                                "--target", "orkige_player"]))
    steps.append(("package", [sys.executable, export_py,
                              "--project", project_dir,
                              "--platform", "android",
                              "--engine-build",
                              os.path.join("build", preset)]))
    serial_flag = ["-s", serial] if serial else []
    steps.append(("install", [adb] + serial_flag + ["install", "-r", apk_path]))
    launch = [adb] + serial_flag + ["shell", "am", "start", "-n",
                                    package + "/" + ANDROID_ACTIVITY_CLASS]
    if scene:
        launch += ["--es", "scene", scene]
    steps.append(("launch", launch))
    return steps


def parse_args(argv):
    """Route argv to (command, options-dict). Pure so --selftest can assert the
    routing. Recognises: doctor; android [--project D] [--scene S] [--serial N];
    ios [--project D] [--simulator] [--serial/--udid U]. Unknown -> ('help',...).
    """
    if not argv:
        return ("help", {})
    command = argv[0]
    opts = {"project": None, "scene": None, "serial": None,
            "simulator": False}
    rest = argv[1:]
    index = 0
    while index < len(rest):
        token = rest[index]
        if token in ("--project", "-p") and index + 1 < len(rest):
            opts["project"] = rest[index + 1]
            index += 2
        elif token == "--scene" and index + 1 < len(rest):
            opts["scene"] = rest[index + 1]
            index += 2
        elif token in ("--serial", "--udid") and index + 1 < len(rest):
            opts["serial"] = rest[index + 1]
            index += 2
        elif token == "--simulator":
            opts["simulator"] = True
            index += 1
        else:
            return ("help", {"bad": token})
    if command not in ("doctor", "android", "ios"):
        return ("help", {"bad": command})
    return (command, opts)


def assemble_doctor_report(facts):
    """Turn a facts dict (gathered by the probe layer, or injected by the
    selftest) into a list of report sections. Each section is a dict:
    title, ok (bool|None for informational), lines (list[str]), action
    (str|None - the ACTION NEEDED pointer). Pure - no probing here."""
    sections = []

    # Android toolchain
    adb = facts["adb"]
    if adb["present"]:
        lines = ["adb: " + adb["path"]]
        if adb["devices"]:
            for device in adb["devices"]:
                lines.append("  device: " + device)
        else:
            lines.append("  no devices/emulators connected "
                         "(plug in a phone or boot the orkige_test emulator)")
        sections.append({"title": "Android: adb", "ok": True,
                         "lines": lines, "action": None})
    else:
        sections.append({"title": "Android: adb", "ok": False,
                         "lines": ["adb not found"],
                         "action": "install the Android platform-tools and set "
                                   "ANDROID_HOME (see CLAUDE.md Build)"})

    ndk = facts["ndk"]
    jdk = facts["jdk"]
    pack_lines = [
        ("NDK: " + ndk["path"]) if ndk["present"] else "NDK: not found",
        ("JDK: " + jdk["path"]) if jdk["present"] else "JDK: not found",
    ]
    pack_ok = ndk["present"] and jdk["present"]
    sections.append({
        "title": "Android: packaging (NDK + JDK)",
        "ok": pack_ok, "lines": pack_lines,
        "action": None if pack_ok else
        "set ANDROID_NDK_HOME (NDK 27) and JAVA_HOME - package_apk.sh needs "
        "both (see CLAUDE.md Build)"})

    # iOS toolchain (Apple hosts only)
    if facts["darwin"]:
        xcrun = facts["xcrun"]
        if xcrun["present"]:
            sim_lines = ["xcrun/simctl present"]
            simulators = xcrun["simulators"]
            if simulators:
                for sim in simulators[:8]:
                    sim_lines.append("  simulator: " + sim)
                if len(simulators) > 8:
                    sim_lines.append("  ... and " + str(len(simulators) - 8)
                                     + " more")
            else:
                sim_lines.append("  no iOS simulators listed")
            sections.append({"title": "iOS: xcrun / simulators", "ok": True,
                             "lines": sim_lines, "action": None})
        else:
            sections.append({"title": "iOS: xcrun / simulators", "ok": False,
                             "lines": ["xcrun not found - install Xcode "
                                       "command line tools"],
                             "action": "install Xcode + command line tools for "
                                       "any iOS flow"})

        signing = facts["ios_signing"]
        sign_lines = [
            "signing identity: " + ("present" if signing["identity"]
                                    else "ABSENT"),
            "provisioning profile: " + (signing["profile_path"]
                                        if signing["profile"] else "ABSENT"),
        ]
        if signing["configured"]:
            sections.append({"title": "iOS: device signing", "ok": True,
                             "lines": sign_lines, "action": None})
        else:
            sections.append({
                "title": "iOS: device signing", "ok": False,
                "lines": sign_lines,
                "action": "iOS PHYSICAL-device installs need a signing "
                          "identity + provisioning profile. Set "
                          + IOS_IDENTITY_ENV + " and " + IOS_PROFILE_ENV
                          + " - the one-time Apple-side setup is in "
                          + IOS_SIGNING_DOC + ". (Simulators need none.)"})

    # Build trees + staleness
    tree_lines = []
    for preset in sorted(facts["trees"]):
        tree = facts["trees"][preset]
        if tree["staleness"] == "missing":
            tree_lines.append(preset + ": not built")
        elif tree["staleness"] == "stale":
            tree_lines.append(preset + ": STALE (built before the newest "
                              "source commit - rebuild before deploying)")
        else:
            tree_lines.append(preset + ": fresh")
    sections.append({"title": "Build trees (vs HEAD)", "ok": None,
                     "lines": tree_lines, "action": None})

    return sections


def render_report(sections):
    """Format doctor sections into text: each section with a status marker,
    then a trailing ACTION NEEDED block collecting every section's pointer."""
    out = []
    actions = []
    for section in sections:
        if section["ok"] is True:
            marker = "[ OK ]"
        elif section["ok"] is False:
            marker = "[ !! ]"
        else:
            marker = "[ -- ]"
        out.append(marker + " " + section["title"])
        for line in section["lines"]:
            out.append("       " + line)
        if section["action"]:
            actions.append(section["action"])
    out.append("")
    if actions:
        out.append("ACTION NEEDED:")
        for action in actions:
            out.append("  - " + action)
    else:
        out.append("All checked prerequisites are present.")
    return "\n".join(out)


# --- probe layer (real subprocess/filesystem; never touched by --selftest) --

def _run(argv):
    """Run a short command, capturing stdout+stderr. Returns (exitcode, text);
    (127, '') when the executable is missing."""
    try:
        result = subprocess.run(argv, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        return result.returncode, result.stdout
    except (OSError, ValueError):
        return 127, ""


def adb_path():
    """adb from ANDROID_HOME (default per-user SDK), PATH as last resort -
    mirrors EditorDeviceTargets.cpp::adbPath()."""
    sdk = os.environ.get("ANDROID_HOME", "")
    if not sdk:
        home = os.environ.get("HOME", "")
        if home:
            sdk = os.path.join(home, "Library", "Android", "sdk")
    candidate = os.path.join(sdk, "platform-tools", "adb") if sdk else ""
    if candidate and os.path.exists(candidate):
        return candidate
    return shutil.which("adb") or ""


def probe_adb():
    adb = adb_path()
    if not adb:
        return {"present": False, "path": "", "devices": []}
    code, output = _run([adb, "devices", "-l"])
    devices = []
    if code == 0:
        for line in output.splitlines():
            fields = line.split()
            if len(fields) >= 2 and fields[1] == "device":
                label = fields[0]
                for field in fields[2:]:
                    if field.startswith("model:"):
                        label = field[len("model:"):] + " (" + fields[0] + ")"
                devices.append(label)
    return {"present": True, "path": adb, "devices": devices}


def probe_ndk():
    ndk = os.environ.get("ANDROID_NDK_HOME", "")
    if ndk and os.path.isdir(ndk):
        return {"present": True, "path": ndk}
    sdk = os.environ.get("ANDROID_HOME", "") or os.path.join(
        os.environ.get("HOME", ""), "Library", "Android", "sdk")
    guess = os.path.join(sdk, "ndk")
    if os.path.isdir(guess) and os.listdir(guess):
        return {"present": True, "path": guess + " (auto)"}
    return {"present": False, "path": ""}


def probe_jdk():
    java_home = os.environ.get("JAVA_HOME", "")
    if java_home and os.path.isfile(os.path.join(java_home, "bin", "javac")):
        return {"present": True, "path": java_home}
    javac = shutil.which("javac")
    if javac:
        return {"present": True, "path": javac}
    return {"present": False, "path": ""}


def probe_xcrun():
    if not shutil.which("xcrun"):
        return {"present": False, "simulators": []}
    code, output = _run(["xcrun", "simctl", "list", "devices", "available"])
    sims = []
    in_ios = False
    if code == 0:
        for raw in output.splitlines():
            line = raw.rstrip()
            if line.startswith("-- "):
                in_ios = line.startswith("-- iOS")
                continue
            if not in_ios:
                continue
            for state in (" (Booted)", " (Shutdown)"):
                pos = line.rfind(state)
                if pos > 3:
                    name = line[:line.rfind(" (", 0, pos)].strip()
                    entry = name + " " + state.strip()
                    if name and entry not in sims:
                        sims.append(entry)
                    break
    return {"present": True, "simulators": sims}


def probe_has_codesign_identity():
    if not shutil.which("security"):
        return False
    code, output = _run(["security", "find-identity", "-v", "-p",
                         "codesigning"])
    return code == 0 and "0 valid identities found" not in output \
        and "valid identities found" in output


def probe_head_commit_time():
    code, output = _run(["git", "-C", REPO_ROOT, "log", "-1", "--format=%ct"])
    if code == 0:
        try:
            return int(output.strip())
        except ValueError:
            return None
    return None


def probe_trees(commit_time):
    trees = {}
    for preset, relative in BUILD_TREES.items():
        binary = os.path.join(REPO_ROOT, "build", preset, relative)
        mtime = int(os.path.getmtime(binary)) if os.path.exists(binary) \
            else None
        trees[preset] = {"binary": binary, "mtime": mtime,
                         "staleness": classify_staleness(mtime, commit_time)}
    return trees


def gather_facts():
    commit_time = probe_head_commit_time()
    facts = {
        "darwin": sys.platform == "darwin",
        "adb": probe_adb(),
        "ndk": probe_ndk(),
        "jdk": probe_jdk(),
        "xcrun": probe_xcrun() if sys.platform == "darwin"
        else {"present": False, "simulators": []},
        "ios_signing": ios_signing_status(
            probe_has_codesign_identity() if sys.platform == "darwin"
            else False, os.environ),
        "trees": probe_trees(commit_time),
        "commit_time": commit_time,
    }
    return facts


# --- commands ---------------------------------------------------------------

def run_step(label, argv, cwd=REPO_ROOT):
    log("[" + label + "] " + " ".join(argv))
    result = subprocess.run(argv, cwd=cwd)
    if result.returncode != 0:
        fail("step '" + label + "' failed (exit "
             + str(result.returncode) + ")")


def command_doctor():
    facts = gather_facts()
    print(render_report(assemble_doctor_report(facts)))
    return 0


def resolve_project(opts):
    project_dir = opts.get("project") or DEFAULT_PROJECT
    if not os.path.isabs(project_dir):
        project_dir = os.path.join(REPO_ROOT, project_dir)
    if not os.path.isdir(project_dir):
        fail("no project at '" + project_dir + "'")
    return project_dir


def command_android(opts):
    project_dir = resolve_project(opts)
    project = orkige_export.Project(project_dir)
    if project.native_target():
        fail("project '" + project.name + "' has a native module - native "
             "modules are desktop-only; pick a Lua/scene project for a phone "
             "session (default: projects/benchmark)")
    facts = gather_facts()
    adb = facts["adb"]
    if not adb["present"]:
        fail("adb not found - run 'doctor' for the fix")
    if not adb["devices"] and not opts.get("serial"):
        fail("no adb device/emulator connected - plug in a phone or boot the "
             "orkige_test emulator, then re-run (run 'doctor' to list devices)")

    preset = "android-debug"
    staleness = facts["trees"][preset]["staleness"]
    package = android_package(project)
    # the export names the APK after the project's executable name - a
    # hardcoded player name breaks every project whose name differs
    apk_path = os.path.join(project.root, "builds", "android",
                            project.exe_name + ".apk")
    export_py = os.path.join(UTIL_DIR, "orkige_export.py")

    steps = android_steps(
        shutil.which("cmake") or "cmake", preset, staleness, export_py,
        project_dir, opts.get("scene"), adb["path"], opts.get("serial"),
        package, apk_path)
    for label, argv in steps:
        run_step(label, argv)

    log("launched '" + project.name + "' on the device (package " + package
        + ") - streaming logcat, Ctrl-C to stop")
    return stream_logcat(adb["path"], opts.get("serial"), package)


def stream_logcat(adb, serial, package):
    """Follow the app's logcat until Ctrl-C. Prefer a pid filter (--pid); fall
    back to the SDL/Orkige tags the player logs under."""
    serial_flag = ["-s", serial] if serial else []
    pid = ""
    for _ in range(20):
        code, output = _run([adb] + serial_flag + ["shell", "pidof", package])
        pid = output.strip().split()[0] if code == 0 and output.strip() else ""
        if pid:
            break
        time.sleep(0.5)
    _run([adb] + serial_flag + ["logcat", "-c"])
    if pid:
        argv = [adb] + serial_flag + ["logcat", "--pid=" + pid]
    else:
        log("could not resolve the app pid - filtering by tag instead")
        argv = [adb] + serial_flag + ["logcat", "-s",
                                      "SDL", "SDL/APP", "Orkige", "OrkigePlayer"]
    try:
        subprocess.run(argv)
    except KeyboardInterrupt:
        pass
    return 0


def command_ios(opts):
    if sys.platform != "darwin":
        fail("iOS deploy needs macOS (xcrun/simctl/devicectl)")
    project_dir = resolve_project(opts)
    project = orkige_export.Project(project_dir)
    export_py = os.path.join(UTIL_DIR, "orkige_export.py")

    if opts.get("simulator"):
        return ios_simulator(project, project_dir, export_py, opts)

    signing = ios_signing_status(probe_has_codesign_identity(), os.environ)
    if not signing["configured"]:
        print(ios_unconfigured_message())
        return 1
    return ios_device(project, project_dir, export_py, opts)


def ios_unconfigured_message():
    return (
        "orkige_device: iOS device signing is NOT configured, so a physical "
        "iPhone/iPad install cannot proceed. Installing on real hardware "
        "requires an Apple Developer signing identity and a matching "
        "provisioning profile; set " + IOS_IDENTITY_ENV + " to your keychain "
        "identity and " + IOS_PROFILE_ENV + " to your .mobileprovision path "
        "(only export.ios.teamId is ever committed). The one-time Apple-side "
        "setup - enroll, create a Development certificate, register the device "
        "and download the profile - is documented step by step in "
        + IOS_SIGNING_DOC + ". Meanwhile, run "
        "`python3 Util/orkige_device.py ios --simulator` for the simulator "
        "flow, which needs no certificate.")


def ios_device(project, project_dir, export_py, opts):
    preset = "ios-device-debug"
    facts = gather_facts()
    staleness = facts["trees"][preset]["staleness"]
    if staleness != "fresh":
        run_step("build", [shutil.which("cmake") or "cmake", "--build",
                           "--preset", preset, "--target", "orkige_player"])
    run_step("export", [sys.executable, export_py, "--project", project_dir,
                        "--platform", "ios", "--engine-build",
                        os.path.join("build", preset)])
    app_dir = os.path.join(project.root, "builds", "ios",
                           project.name + ".app")
    udid = opts.get("serial") or ios_first_device_udid()
    if not udid:
        fail("no USB-connected iOS device found (xcrun devicectl list devices)")
    run_step("install", ["xcrun", "devicectl", "device", "install", "app",
                         "--device", udid, app_dir])
    bundle_id = project.settings.get("export.ios.bundleId",
                                     "com.orkitec." + project.id_slug)
    run_step("launch", ["xcrun", "devicectl", "device", "process", "launch",
                        "--device", udid, bundle_id])
    log("launched on device " + udid + " (bundle " + bundle_id + "). The game "
        "runs standalone - USB has no live debug tunnel, see " + IOS_SIGNING_DOC)
    return 0


def ios_first_device_udid():
    json_path = os.path.join(REPO_ROOT, "build", "orkige_devicectl.json")
    code, _ = _run(["xcrun", "devicectl", "list", "devices", "--json-output",
                    json_path])
    if code != 0 or not os.path.isfile(json_path):
        return ""
    try:
        import json
        with open(json_path) as handle:
            data = json.load(handle)
        os.remove(json_path)
        for device in data.get("result", {}).get("devices", []):
            udid = device.get("identifier", "")
            if udid:
                return udid
    except (ValueError, OSError, KeyError):
        pass
    return ""


def ios_simulator(project, project_dir, export_py, opts):
    preset = "ios-simulator-debug"
    facts = gather_facts()
    staleness = facts["trees"][preset]["staleness"]
    if staleness != "fresh":
        run_step("build", [shutil.which("cmake") or "cmake", "--build",
                           "--preset", preset, "--target", "orkige_player"])
    run_step("export", [sys.executable, export_py, "--project", project_dir,
                        "--platform", "ios-simulator", "--engine-build",
                        os.path.join("build", preset)])
    app_dir = os.path.join(project.root, "builds", "ios-simulator",
                           project.name + ".app")
    udid = opts.get("serial") or "booted"
    if udid == "booted":
        _run(["open", "-a", "Simulator"])
        _run(["xcrun", "simctl", "boot", udid])
    run_step("install", ["xcrun", "simctl", "install", udid, app_dir])
    bundle_id = project.settings.get("export.ios.bundleId",
                                     "com.orkitec." + project.id_slug)
    run_step("launch", ["xcrun", "simctl", "launch", udid, bundle_id])
    log("launched on the " + udid + " simulator (bundle " + bundle_id + ")")
    return 0


# --- selftest ---------------------------------------------------------------

def selftest():
    """Pure checks: argument routing, doctor report assembly from injected
    facts, and command construction. No real devices, no subprocess."""
    # staleness classification
    assert classify_staleness(None, 100) == "missing"
    assert classify_staleness(50, 100) == "stale"
    assert classify_staleness(150, 100) == "fresh"
    assert classify_staleness(150, None) == "fresh", "no commit time -> fresh"

    # argument routing
    assert parse_args([]) == ("help", {})
    cmd, opts = parse_args(["doctor"])
    assert cmd == "doctor"
    cmd, opts = parse_args(["android", "--project", "projects/x",
                            "--scene", "a.oscene", "--serial", "emulator-5554"])
    assert cmd == "android"
    assert opts["project"] == "projects/x"
    assert opts["scene"] == "a.oscene"
    assert opts["serial"] == "emulator-5554"
    cmd, opts = parse_args(["ios", "--simulator"])
    assert cmd == "ios" and opts["simulator"] is True
    cmd, opts = parse_args(["ios", "--udid", "ABC"])
    assert opts["serial"] == "ABC", "--udid aliases --serial"
    assert parse_args(["bogus"])[0] == "help"
    assert parse_args(["android", "--nope"])[0] == "help"

    # iOS signing status (env seam, cert probe injected)
    status = ios_signing_status(False, {})
    assert status["configured"] is False and status["identity"] is False
    status = ios_signing_status(True, {})
    assert status["configured"] is False, "identity alone is not enough"
    import tempfile
    with tempfile.NamedTemporaryFile() as profile:
        env = {IOS_PROFILE_ENV: profile.name}
        assert ios_signing_status(True, env)["configured"] is True
        assert ios_signing_status(False, env)["configured"] is False
        assert ios_signing_status(True, {IOS_PROFILE_ENV: "/no/such/file"})[
            "configured"] is False, "a dangling profile path is not present"

    # doctor report assembly + the ACTION NEEDED aggregation
    facts_bad = {
        "darwin": True,
        "adb": {"present": False, "path": "", "devices": []},
        "ndk": {"present": False, "path": ""},
        "jdk": {"present": False, "path": ""},
        "xcrun": {"present": True, "simulators": ["iPhone 16 (Shutdown)"]},
        "ios_signing": {"identity": False, "profile": False,
                        "profile_path": "", "configured": False},
        "trees": {"android-debug": {"staleness": "missing"},
                  "ios-device-debug": {"staleness": "stale"}},
        "commit_time": 1,
    }
    sections = assemble_doctor_report(facts_bad)
    titles = [s["title"] for s in sections]
    assert any("adb" in t for t in titles)
    assert any("packaging" in t for t in titles)
    assert any("device signing" in t for t in titles)
    text = render_report(sections)
    assert "ACTION NEEDED" in text
    assert IOS_SIGNING_DOC in text, "iOS signing points at the doc"
    assert "STALE" in text, "a stale tree is called out"
    # doctor NEVER fails on absent signing - it is an ACTION line, not an error
    assert "device signing" in text

    facts_good = {
        "darwin": True,
        "adb": {"present": True, "path": "/sdk/adb",
                "devices": ["sdk_gphone64_arm64 (emulator-5554)"]},
        "ndk": {"present": True, "path": "/ndk"},
        "jdk": {"present": True, "path": "/jdk"},
        "xcrun": {"present": True, "simulators": []},
        "ios_signing": {"identity": True, "profile": True,
                        "profile_path": "/p.mobileprovision",
                        "configured": True},
        "trees": {"android-debug": {"staleness": "fresh"}},
        "commit_time": 1,
    }
    good_text = render_report(assemble_doctor_report(facts_good))
    assert "All checked prerequisites are present." in good_text
    assert "emulator-5554" in good_text

    # non-Apple host: no iOS sections at all (honest, not a false failure)
    facts_linux = dict(facts_good)
    facts_linux["darwin"] = False
    linux_titles = [s["title"] for s in
                    assemble_doctor_report(facts_linux)]
    assert not any("iOS" in t for t in linux_titles)

    # android command construction: fresh tree skips the build step; a stale
    # one includes it; scene rides as an intent extra; serial disambiguates
    fresh = android_steps("cmake", "android-debug", "fresh", "e.py", "proj",
                          None, "adb", None, "com.orkitec.benchmark", "out.apk")
    labels = [label for label, _ in fresh]
    assert labels == ["package", "install", "launch"], labels
    stale = android_steps("cmake", "android-debug", "stale", "e.py", "proj",
                          "s1.oscene", "adb", "emulator-5554",
                          "com.orkitec.benchmark", "out.apk")
    labels = [label for label, _ in stale]
    assert labels == ["build", "package", "install", "launch"], labels
    install = dict(stale)["install"]
    assert install[:4] == ["adb", "-s", "emulator-5554", "install"]
    launch = dict(stale)["launch"]
    assert launch[-2:] == ["scene", "s1.oscene"], "scene extra appended"
    assert "com.orkitec.benchmark/" + ANDROID_ACTIVITY_CLASS in launch

    # iOS-unconfigured message is one honest paragraph with the doc pointer +
    # the simulator fallback offer
    message = ios_unconfigured_message()
    assert IOS_SIGNING_DOC in message
    assert "--simulator" in message
    assert IOS_IDENTITY_ENV in message and IOS_PROFILE_ENV in message

    print("orkige_device: selftest OK")


USAGE = __doc__


def main(argv):
    command, opts = parse_args(argv)
    if command == "help":
        bad = opts.get("bad")
        if bad:
            print("orkige_device: unrecognized '" + str(bad) + "'\n",
                  file=sys.stderr)
        print(USAGE)
        return 0 if not opts.get("bad") else 2
    if command == "doctor":
        return command_doctor()
    if command == "android":
        return command_android(opts)
    if command == "ios":
        return command_ios(opts)
    return 2


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        selftest()
    else:
        sys.exit(main(sys.argv[1:]))
