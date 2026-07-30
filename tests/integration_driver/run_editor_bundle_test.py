#!/usr/bin/env python3
"""Prove a COPIED editor is self-sufficient: distribution readiness as a test.

The editor ships as an app someone copies onto a machine that has no clone of
this repository, no engine build tree and no Python. This driver stages exactly
that situation and drives the copy over its own MCP endpoint:

  1. STAGE  the built app (macOS: Orkige.app; elsewhere: the executable plus the
     share/orkige tree beside it) into a scratch directory in the build tree,
     with a copy of a project beside it.
  2. DETACH it from the tree. Two mechanisms, both applied where available:
     ORKIGE_EDITOR_BUNDLE_ONLY=1 makes the editor refuse every developer-tree
     fallback (so only what the app CARRIES can answer), and on macOS the run
     also happens inside a sandbox that denies reads of the repository and the
     machine's tool directories - which additionally catches anything that
     reaches for a baked path without going through the resource resolver. The
     environment is scrubbed (PATH of /usr/bin:/bin only - no python3 - a
     scratch HOME, a scratch writable state dir and a cwd outside the tree).
  3. ASSERT over MCP that the copy boots with a rendering window, opens the
     copied project, renders a scene screenshot and PLAYS - the bundled player
     spawning and reporting a running session.
  4. ASSERT the unreadable-media leg: with the staged shader media made
     unreadable, the resource resolver says so out loud and nothing throws out
     of engine setup (the error_code probes). Skipped as root, where a mode of
     000 denies nothing.

Every assertion is about the STAGED COPY, never the build tree, so a new baked
developer path that boot or Play depends on fails here instead of on a user's
machine.

The staged copy runs under a TEST-ONLY bundle identifier and with window-state
restoration off: this test stops its copies by signal, which the window system
counts as an abnormal exit, and the real editor's identity must not inherit that
history. Stdlib only, per the toolchain policy.
"""

import argparse
import json
import os
import plistlib
import shutil
import signal
import socket
import stat
import subprocess
import sys
import time


SKIP = 77


def log(message):
    print("[bundle] " + message, flush=True)


def fail(message):
    print("[bundle] FAILED: " + message, flush=True)
    sys.exit(1)


def skip(message):
    print("[bundle] SKIP: " + message, flush=True)
    sys.exit(SKIP)


# --- MCP over the endpoint's plain HTTP -------------------------------------

class McpClient:
    """the minimum Streamable-HTTP MCP client this test needs: one POST /mcp
    per JSON-RPC call, bearer token for the mutating verbs"""

    def __init__(self, port, token):
        self.port = port
        self.token = token
        self.next_id = 1

    def call(self, method, params=None, timeout=30.0):
        body = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        self.next_id += 1
        if params is not None:
            body["params"] = params
        payload = json.dumps(body).encode()
        headers = ("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                   "Content-Type: application/json\r\n"
                   "Authorization: Bearer %s\r\n"
                   "Content-Length: %d\r\nConnection: close\r\n\r\n"
                   % (self.token, len(payload))).encode()
        with socket.create_connection(("127.0.0.1", self.port), timeout) as sock:
            sock.settimeout(timeout)
            sock.sendall(headers + payload)
            chunks = []
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk)
        raw = b"".join(chunks)
        split = raw.find(b"\r\n\r\n")
        if split < 0:
            raise RuntimeError("no HTTP header terminator in the reply")
        return json.loads(raw[split + 4:].decode("utf-8", "replace"))

    def tool(self, name, arguments=None, timeout=60.0):
        reply = self.call("tools/call",
                          {"name": name, "arguments": arguments or {}},
                          timeout=timeout)
        result = reply.get("result")
        if result is None:
            raise RuntimeError("%s: %s" % (name, reply.get("error")))
        if result.get("isError"):
            texts = [c.get("text", "") for c in result.get("content", [])]
            raise RuntimeError("%s refused: %s" % (name, " ".join(texts)))
        return result.get("structuredContent", {})


# --- staging ----------------------------------------------------------------

# The identity the STAGED copy runs under. A macOS app's window-restoration and
# crash history are keyed on the bundle identifier, and this test stops its copies
# by signal - which the window system counts as an abnormal exit. Renaming the
# copy keeps every trace of that off the real editor's identity, so a staged run
# can never make an unrelated editor session prompt "reopen windows?".
STAGED_BUNDLE_ID = "com.orkitec.orkige-editor.bundlecheck"


def stage_app(app_path, stage_dir):
    """copy the built app (bundle directory or bare executable + its share
    tree) into the scratch stage; returns the staged executable"""
    if os.path.isdir(app_path):		# macOS .app bundle
        target = os.path.join(stage_dir, os.path.basename(app_path))
        shutil.copytree(app_path, target, symlinks=True)
        plist_path = os.path.join(target, "Contents", "Info.plist")
        if os.path.isfile(plist_path):
            with open(plist_path, "rb") as handle:
                plist = plistlib.load(handle)
            plist["CFBundleIdentifier"] = STAGED_BUNDLE_ID
            with open(plist_path, "wb") as handle:
                plistlib.dump(plist, handle)
        name = os.path.splitext(os.path.basename(app_path))[0]
        return os.path.join(target, "Contents", "MacOS", name)
    # a bare executable: it plus everything the build staged beside it
    source_dir = os.path.dirname(os.path.abspath(app_path))
    target_dir = os.path.join(stage_dir, "orkige")
    os.makedirs(target_dir)
    for entry in ("share", os.path.basename(app_path), "orkige_player",
                  "orkige_player.exe", "texcook", "texcook.exe"):
        source = os.path.join(source_dir, entry)
        if os.path.isdir(source):
            shutil.copytree(source, os.path.join(target_dir, entry))
        elif os.path.isfile(source):
            shutil.copy2(source, os.path.join(target_dir, entry))
    return os.path.join(target_dir, os.path.basename(app_path))


def staged_media_dir(executable):
    """the staged app's engine-media directory (the layout the resolver reads)"""
    exe_dir = os.path.dirname(executable)
    if sys.platform == "darwin" and exe_dir.endswith(os.path.join("Contents",
                                                                 "MacOS")):
        return os.path.join(os.path.dirname(exe_dir), "Resources", "Media")
    return os.path.join(exe_dir, "share", "orkige", "Media")


def write_sandbox_profile(path, denied, allowed):
    """a macOS sandbox profile denying every path in `denied` - the clean room:
    the repository, the vcpkg tree and Homebrew simply are not there - and then
    re-allowing `allowed` (the staging directory, which lives inside the build
    tree). SBPL evaluates rules in order and the LAST match decides, so the
    allow must come after the deny."""
    with open(path, "w") as profile:
        profile.write("(version 1)\n(allow default)\n(deny file-read* file-write*\n")
        for entry in denied:
            profile.write('    (subpath "%s")\n' % os.path.realpath(entry))
        profile.write(")\n(allow file-read* file-write*\n")
        for entry in allowed:
            profile.write('    (subpath "%s")\n' % os.path.realpath(entry))
        profile.write(")\n")


# Environment the MACHINE provides, as opposed to the developer tree: the
# display/driver plumbing a windowed run genuinely needs (a headless CI display,
# a software Vulkan ICD) and the audio-driver choice the suite pins. Scrubbing
# these would test a broken machine, not a clean one.
PASSTHROUGH_ENV = ("DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
                   "VK_DRIVER_FILES", "VK_ICD_FILENAMES",
                   "LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER", "ALSOFT_DRIVERS",
                   "SYSTEMROOT", "WINDIR")


def scrubbed_env(stage_dir):
    """the clean-room environment: no repository, no developer PATH (and so no
    python3), a scratch HOME, a scratch writable state directory, and every
    developer-tree resource fallback refused"""
    if os.name == "nt":
        system_root = os.environ.get("SYSTEMROOT", r"C:\Windows")
        path = os.pathsep.join([os.path.join(system_root, "system32"),
                                system_root])
    else:
        path = "/usr/bin:/bin"
    env = {
        "PATH": path,
        "HOME": os.path.join(stage_dir, "home"),
        "TMPDIR": os.path.join(stage_dir, "tmp"),
        "TEMP": os.path.join(stage_dir, "tmp"),
        # every developer-tree fallback refused: only the copy may answer
        "ORKIGE_EDITOR_BUNDLE_ONLY": "1",
        # the writable state (settings inis, engine log) into the scratch: the
        # platform's app-support directory follows the user account, not HOME
        "ORKIGE_EDITOR_STATE_DIR": os.path.join(stage_dir, "state"),
        # no IDE lock file, no .mcp.json reconciliation into the copied project
        "ORKIGE_CLAUDE_IDE": "0",
    }
    for name in PASSTHROUGH_ENV:
        if name in os.environ:
            env[name] = os.environ[name]
    return env


def launch(executable, argv, cwd, env, sandbox_profile):
    command = list(argv)
    if sys.platform == "darwin":
        # opt the run out of window-state restoration entirely: this test stops
        # its copies by signal, and a restored/prompting app would block the
        # next launch on a modal. The editor's argument parser ignores flags it
        # does not know, so this reaches only the window system.
        command += ["-ApplePersistenceIgnoreState", "YES"]
    if sandbox_profile:
        command = ["/usr/bin/sandbox-exec", "-f", sandbox_profile] + command
    return subprocess.Popen(command, cwd=cwd, env=env,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            errors="replace")


def read_endpoint(token_file):
    """the endpoint's token file: line 1 is the port it actually bound, line 2
    the secret. Reading the port back is what lets the editor pick an EPHEMERAL
    one (--mcp-port 0) - a fixed port would collide with a previous run's socket
    still in TIME_WAIT and the endpoint would silently never come up."""
    lines = open(token_file).read().splitlines()
    if len(lines) < 2 or not lines[1].strip():
        return (0, "")
    try:
        return (int(lines[0].strip()), lines[1].strip())
    except ValueError:
        return (0, "")


def wait_for_endpoint(process, token_file, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if process.poll() is not None:
            return (0, "")
        if os.path.exists(token_file):
            port, token = read_endpoint(token_file)
            if port and token:
                return (port, token)
        time.sleep(0.25)
    return (0, "")


def stop(process, grace=15.0):
    """ask the staged editor to quit, then make sure it is gone; returns its
    merged stdout/stderr"""
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=grace)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=grace)
    return process.stdout.read()


# --- the legs ---------------------------------------------------------------

def run_session_leg(args, stage_dir, sandbox_profile):
    """boot the staged copy detached from the tree and drive it over MCP"""
    executable = stage_app(args.editor_app, stage_dir)
    if not os.path.isfile(executable):
        fail("staging produced no executable at " + executable)
    project = os.path.join(stage_dir, "project")
    shutil.copytree(args.project, project,
                    ignore=shutil.ignore_patterns("builds", ".orkige",
                                                  "build*", ".mcp.json"))
    for name in ("home", "cwd", "state", "tmp", "out"):
        os.makedirs(os.path.join(stage_dir, name), exist_ok=True)

    token_file = os.path.join(stage_dir, "endpoint.token")
    env = scrubbed_env(stage_dir)
    # --mcp-port 0: the editor binds an ephemeral loopback port and publishes it
    # on line 1 of the token file, so repeated runs never fight over one number
    process = launch(executable,
                     [executable, "--mcp-port", "0",
                      "--mcp-token-file", token_file],
                     os.path.join(stage_dir, "cwd"), env, sandbox_profile)
    try:
        port, token = wait_for_endpoint(process, token_file, args.boot_timeout)
        if not port:
            output = stop(process)
            print(output[-8000:], flush=True)
            fail("the copied editor never opened its MCP endpoint "
                 "(it did not finish booting)")
        client = McpClient(port, token)
        client.call("initialize", {"protocolVersion": "2025-03-26",
                                  "capabilities": {},
                                  "clientInfo": {"name": "bundle-selfcheck",
                                                 "version": "1"}})
        log("the copied editor booted and answers MCP")

        # a rendering window: get_state answers, and the editor's own UI draws
        # through the same Hlms path the scene does - a boot without the shader
        # media never reaches this point (it dies in engine setup)
        state = client.tool("get_state")
        if not state:
            fail("get_state returned nothing")

        opened = client.tool("open_project", {"path": project})
        log("opened the copied project: %s" % opened.get("project", project))
        scene = os.path.join(project, args.scene)
        client.tool("open_scene", {"scene": scene, "force": True})
        log("opened scene " + args.scene)

        shot = os.path.join(stage_dir, "out", "scene.png")
        client.tool("screenshot", {"path": shot, "window": False,
                                  "inline": False})
        if not os.path.isfile(shot) or os.path.getsize(shot) < 1024:
            fail("the copied editor wrote no scene screenshot at " + shot)
        log("rendered a scene screenshot (%d bytes)" % os.path.getsize(shot))

        # PLAY: the player that must spawn is the one INSIDE the app
        client.tool("play", {"force": True}, timeout=120.0)
        deadline = time.time() + args.play_timeout
        mode = ""
        while time.time() < deadline:
            mode = str(client.tool("get_state").get("play_mode", ""))
            if mode in ("playing", "paused", "edit"):
                break
            time.sleep(0.5)
        if mode != "playing":
            output = stop(process)
            print(output[-8000:], flush=True)
            fail("the bundled player never reported a running session "
                 "(play_mode ended at '%s')" % mode)
        log("the bundled player runs: play_mode=playing")
        client.tool("stop")
        output = stop(process)
    except Exception as error:		# noqa: BLE001 - report and stop the app
        output = stop(process)
        print(output[-8000:], flush=True)
        fail("MCP session failed: %r" % (error,))

    # the copy wrote its state into the writable directory, NEVER into itself
    state_dir = os.path.join(stage_dir, "state")
    written = sorted(os.listdir(state_dir))
    if not any(name.endswith(".log") for name in written):
        fail("no engine log in the writable state directory (%s)" % written)
    app_root = os.path.dirname(os.path.dirname(executable))
    for root, _dirs, files in os.walk(app_root):
        for name in files:
            if name.endswith(".ini"):
                fail("the copied app wrote %s INSIDE itself (%s)"
                     % (name, root))
    log("writable state stayed outside the app: %s" % written)
    # a baked developer path must not be what made this work
    if "developer tree" in output:
        fail("the copied editor resolved resources from the developer tree")
    return output


def run_unreadable_media_leg(args, stage_dir, sandbox_profile):
    """an UNREADABLE shader-media directory must degrade honestly, not abort"""
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        log("skipping the unreadable-media leg: a mode of 000 denies nothing "
            "as root")
        return
    executable = stage_app(args.editor_app, stage_dir)
    media = staged_media_dir(executable)
    if not os.path.isdir(media):
        fail("no staged engine media at " + media)
    for name in ("home", "cwd", "state", "tmp"):
        os.makedirs(os.path.join(stage_dir, name), exist_ok=True)
    original = stat.S_IMODE(os.stat(media).st_mode)
    os.chmod(media, 0)
    try:
        env = scrubbed_env(stage_dir)
        env["ORKIGE_MCP_PORT"] = "off"	# this leg only reads the boot log
        # a frame-capped run EXITS by itself, so its whole output is flushed and
        # readable (a run we had to signal would lose the buffered tail)
        env["ORKIGE_DEMO_FRAMES"] = "2"
        process = launch(executable, [executable],
                         os.path.join(stage_dir, "cwd"), env, sandbox_profile)
        deadline = time.time() + args.boot_timeout
        while process.poll() is None and time.time() < deadline:
            time.sleep(0.25)
        exited = process.poll() is not None
        output = stop(process)
    finally:
        os.chmod(media, original or 0o755)
    if not exited:
        fail("the frame-capped run never exited, so its boot log is not "
             "readable - the leg cannot judge the degradation")
    for abort in ("engine setup failed with an exception",
                  "terminating due to uncaught exception"):
        if abort in output:
            log("boot output tail:\n" + output[-3000:])
            fail("an unreadable media directory ABORTED the boot ('%s') - a "
                 "probe still throws instead of degrading" % abort)
    # the honest line every flavor produces: the resolver says out loud that it
    # found no engine media, before the render boot fails on the consequence.
    # (On Ogre-Next the backend additionally logs its own "no Hlms templates"
    # line for whatever media directory it was still pointed at.)
    if "NO engine media found" not in output:
        log("boot output tail:\n" + output[-3000:])
        fail("an unreadable media directory produced no honest warning")
    log("an unreadable media directory degrades with a warning, no abort")


def clean_staged_identity_state():
    """remove what the window system may have recorded for the STAGED identity.
    Same hygiene rule the crash-marker test follows: WE provoked these artifacts
    (the copies are stopped by signal), so we clean them up - and only ever ones
    named after the test-only bundle identifier."""
    if sys.platform != "darwin":
        return
    for base in ("~/Library/Saved Application State",
                 "~/Library/Preferences"):
        directory = os.path.expanduser(base)
        for suffix in (".savedState", ".plist"):
            path = os.path.join(directory, STAGED_BUNDLE_ID + suffix)
            if os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
            elif os.path.isfile(path):
                os.remove(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor-app", required=True,
                       help="the built editor app (bundle dir or executable)")
    parser.add_argument("--project", required=True,
                       help="a project to copy beside the staged app")
    parser.add_argument("--scene", required=True,
                       help="the project-relative scene to open and play")
    parser.add_argument("--stage-root", required=True,
                       help="scratch directory (inside the build tree)")
    parser.add_argument("--repo-root", required=True,
                       help="the tree the staged app must NOT reach into")
    parser.add_argument("--boot-timeout", type=float, default=120.0)
    parser.add_argument("--play-timeout", type=float, default=120.0)
    args = parser.parse_args()
    # everything is spawned with a cwd OUTSIDE the tree, so every path the
    # driver hands on must be absolute
    for name in ("editor_app", "project", "stage_root", "repo_root"):
        setattr(args, name, os.path.abspath(getattr(args, name)))

    if not os.path.exists(args.editor_app):
        skip("no built editor app at %s" % args.editor_app)

    shutil.rmtree(args.stage_root, ignore_errors=True)
    os.makedirs(args.stage_root)

    sandbox_profile = ""
    if sys.platform == "darwin" and os.path.exists("/usr/bin/sandbox-exec"):
        # the repository (and with it the vcpkg tree and every build output) is
        # what a copied app must not need. Homebrew's TOOL directories go too -
        # a copied editor has no python3 and must never reach for the developer
        # machine's - but the rest of Homebrew stays readable, because MoltenVK
        # installs there and is the platform's Vulkan DRIVER: system-tier, like
        # a GPU driver on any other platform, and not something an app carries.
        denied = [args.repo_root]
        for extra in ("/opt/homebrew/bin", "/opt/homebrew/sbin",
                      "/usr/local/bin", "/usr/local/sbin"):
            if os.path.isdir(extra):
                denied.append(extra)
        sandbox_profile = os.path.join(args.stage_root, "cleanroom.sb")
        write_sandbox_profile(sandbox_profile, denied, [args.stage_root])
        log("clean room: the repository and the machine's tool directories are "
            "denied (only the staged copy is reachable)")
    else:
        log("clean room: ORKIGE_EDITOR_BUNDLE_ONLY only (no path sandbox on "
            "this platform)")

    clean_staged_identity_state()
    try:
        session_stage = os.path.join(args.stage_root, "session")
        os.makedirs(session_stage)
        run_session_leg(args, session_stage, sandbox_profile)

        media_stage = os.path.join(args.stage_root, "unreadable")
        os.makedirs(media_stage)
        run_unreadable_media_leg(args, media_stage, sandbox_profile)
    finally:
        clean_staged_identity_state()

    log("PASSED: the copied editor boots, renders, opens a project and plays "
        "with nothing but what it carries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
