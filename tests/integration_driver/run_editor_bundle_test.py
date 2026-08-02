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
  3b. ASSERT the EXTENSION path: a copied editor authors and runs an editor
     tool (`scripts/<name>.editor.lua`, the Tools-menu command) with no
     toolchain of any kind - the path someone who never writes C++ extends the
     editor through. The tool must have effects the driver can read back from
     outside the copy (a scene object and a project file it wrote through the
     jailed verbs), and a tool that raises must be reported with its file:line
     and leave nothing behind.
  4. ASSERT that the copy can PACKAGE a game: asked over MCP to export the
     copied project, it MUST produce a runnable .app out of the engine payload
     it carries. There is no acceptable second answer - the exporter is code
     the editor links, so nothing about it can be missing on a user's machine
     - and the leg also asserts the staged copy carries no script at all, with
     no interpreter reachable to run one. The same leg asks for an iOS
     package, which a copy genuinely cannot produce, and asserts the refusal
     SAYS so.
  4b. ASSERT the BROWSER package (`--leg web`, its own ctest): with the browser
     payload staged into the copied app exactly as the packaging pipeline
     stages it, the copy must produce a web export - the shell page, the wasm
     player and the sealed game.pak - out of that payload alone. The browser
     target is flavor-independent (the wasm player is the classic flavor
     whatever the editor is), so this is what proves a downloaded editor of
     EITHER flavor can ship a browser build. Skips (77) on a machine with no
     wasm build tree to stage from, which is why it is a test of its own: a
     missing toolchain must never turn the legs above into a skip.
  4c. ASSERT COMPILED GAME CODE (`--leg native`, its own ctest): a copied
     editor plus an installed SDK pack builds, plays and packages a project
     whose behaviour is C++. It runs in a clean room of its OWN - the
     repository, the engine build tree and the machine's vcpkg root stay
     denied, but cmake and ninja are handed back as individual files, because
     a native build genuinely needs a toolchain while everything above proves
     the opposite. The leg asserts all three outcomes: no pack -> a refusal
     naming the SDK, a pack with no build programs -> a DIFFERENT refusal
     naming the toolchain, both present -> it builds against the pack and runs.
  5. ASSERT the unreadable-media leg: with the staged shader media made
     unreadable, the resource resolver says so out loud and nothing throws out
     of engine setup (the error_code probes). Skipped as root, where a mode of
     000 denies nothing.
  6. ASSERT the packaged-changelog leg: the About box shows what the build
     shipped with, resolved through the same locator. A staged copy with no
     CHANGELOG.md at its resource root says so in one line; drop the file the
     packaging pipeline writes there and the copy reads THAT back - the two
     states an About box has, both proven on a real copy.

Every assertion is about the STAGED COPY, never the build tree, so a new baked
developer path that boot or Play depends on fails here instead of on a user's
machine.

The staged copy runs under a TEST-ONLY bundle identifier and with window-state
restoration off: this test stops its copies by signal, which the window system
counts as an abnormal exit, and the real editor's identity must not inherit that
history. Stdlib only, per the toolchain policy.
"""

import argparse
import hashlib
import importlib.util
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

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def load_module(name, path):
    """import a repository script by path (the drivers are files, not a
    package). Used to reuse the packaging pipeline's OWN payload staging and
    the web suite's OWN artifact expectations rather than restating either."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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
        accepted, structured, text = self.attempt(name, arguments, timeout)
        if not accepted:
            raise RuntimeError("%s refused: %s" % (name, text))
        return structured

    def attempt(self, name, arguments=None, timeout=60.0):
        """like tool(), but a REFUSAL is an answer rather than an exception:
        (accepted, structuredContent, text). What a copied editor says when it
        cannot do something is exactly what this test reads."""
        reply = self.call("tools/call",
                          {"name": name, "arguments": arguments or {}},
                          timeout=timeout)
        result = reply.get("result")
        if result is None:
            raise RuntimeError("%s: %s" % (name, reply.get("error")))
        text = " ".join(c.get("text", "") for c in result.get("content", []))
        return (not result.get("isError"), result.get("structuredContent", {}),
                text)


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


def staged_resource_dir(executable):
    """the staged app's RESOURCE root - what SDL_GetBasePath resolves to and
    what the editor's locator reads relative to (the bundle's Resources on
    macOS, share/orkige beside the executable elsewhere)"""
    exe_dir = os.path.dirname(executable)
    if sys.platform == "darwin" and exe_dir.endswith(os.path.join("Contents",
                                                                 "MacOS")):
        return os.path.join(os.path.dirname(exe_dir), "Resources")
    return os.path.join(exe_dir, "share", "orkige")


def staged_media_dir(executable):
    """the staged app's engine-media directory (the layout the resolver reads)"""
    return os.path.join(staged_resource_dir(executable), "Media")


def write_sandbox_profile(path, denied, allowed, allow_files=()):
    """a macOS sandbox profile denying every path in `denied` - the clean room:
    the repository, the vcpkg tree and Homebrew simply are not there - and then
    re-allowing `allowed` (the staging directory, which lives inside the build
    tree). SBPL evaluates rules in order and the LAST match decides, so the
    allow must come after the deny.

    The last rule is the narrow one that makes the allowed area REACHABLE: some
    tools (codesign, which signs every exported macOS app) stat each ancestor
    of the file they touch, and a subpath deny covers those ancestors too. The
    allowance is per-DIRECTORY, never a subpath: the chain from a denied root
    down to the staging directory becomes stat-able, and every other path under
    a denied root stays invisible to stat as well as to read - which matters,
    because the resource locator's developer-tree fallbacks are decided by an
    existence probe, and a blanket metadata allowance would hand them back.

    `allow_files` re-allows INDIVIDUAL files that sit inside a denied directory
    (a build tool's symlink in a package manager's bin directory). Per file on
    purpose: a leg that needs one program back must not be handed the whole
    directory, or the denial it depends on stops meaning anything."""
    with open(path, "w") as profile:
        profile.write("(version 1)\n(allow default)\n(deny file-read* file-write*\n")
        for entry in denied:
            profile.write('    (subpath "%s")\n' % os.path.realpath(entry))
        profile.write(")\n(allow file-read* file-write*\n")
        for entry in allowed:
            profile.write('    (subpath "%s")\n' % os.path.realpath(entry))
        profile.write(")\n")
        if allow_files:
            profile.write("(allow file-read* process-exec\n")
            for entry in allow_files:
                profile.write('    (literal "%s")\n' % entry)
            profile.write(")\n")
        profile.write("(allow file-read-metadata\n")
        traversable = set()
        for entry in allowed:
            directory = os.path.realpath(entry)
            while True:
                traversable.add(directory)
                parent = os.path.dirname(directory)
                if parent == directory:
                    break
                directory = parent
        for directory in sorted(traversable):
            profile.write('    (literal "%s")\n' % directory)
        profile.write(")\n")


# Environment the MACHINE provides, as opposed to the developer tree: the
# display/driver plumbing a windowed run genuinely needs (a headless CI display,
# a software Vulkan ICD) and the audio-driver choice the suite pins. Scrubbing
# these would test a broken machine, not a clean one.
PASSTHROUGH_ENV = ("DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
                   "VK_DRIVER_FILES", "VK_ICD_FILENAMES",
                   "LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER", "ALSOFT_DRIVERS",
                   "SYSTEMROOT", "WINDIR")


def scrubbed_env(stage_dir, extra=None):
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
    env.update(extra or {})
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
    merged stdout/stderr.

    Safe to call twice, which the legs rely on: each stops the copy explicitly
    to capture its output for a failure message, and again from a `finally` so
    that no exit path can leave one running. `fail()` raises SystemExit, which
    is NOT an Exception - an `except Exception` handler alone would let every
    assertion failure leak a windowed editor onto the developer's screen."""
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
        # the verb writes whatever the scene render target holds when it runs,
        # so right after open_scene the freshly loaded scene may not have been
        # drawn yet - and an undrawn target compresses below the floor or is not
        # written at all. Ask again until a real frame is in it; the size floor
        # stays the assertion (a genuinely blank scene view never clears it).
        shot_deadline = time.time() + 30.0
        shot_size = -1
        while True:
            client.tool("screenshot", {"path": shot, "window": False,
                                      "inline": False})
            shot_size = (os.path.getsize(shot) if os.path.isfile(shot) else -1)
            if shot_size >= 1024 or time.time() >= shot_deadline:
                break
            time.sleep(0.5)
        if shot_size < 1024:
            fail("the copied editor wrote no scene screenshot at %s (%s)"
                 % (shot, "no file" if shot_size < 0
                    else "%d bytes, below the 1024-byte floor" % shot_size))
        log("rendered a scene screenshot (%d bytes)" % shot_size)

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
    finally:
        stop(process)	# no exit path leaves a staged editor running

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


# --- the editor-tool leg ----------------------------------------------------

# The extension path for someone who never touches C++: a project script named
# <name>.editor.lua is a one-shot editor TOOL, listed in the Tools menu and
# runnable over MCP, whose editor.* calls ride the same verb handler an agent
# uses. Nothing about it may need a toolchain - which is exactly what a copied
# editor in a clean room can prove.
#
# The tool is authored the way its author writes one, through the copy's OWN
# jailed write_project_file verb, so the whole path is under test (author,
# discover, run) rather than a runner handed a fixture from outside. The shipped
# sample (projects/roller) frames a tile grid from a scene LevelComponent, which
# the project staged here does not have, so the fixture is purpose-made.
#
# It leaves TWO effects behind - one in the live scene, one on disk - and the
# driver reads both back from outside the copy. A run that returned "ok" and did
# nothing fails this leg.
EDITOR_TOOL_NAME = "bundle_probe"
EDITOR_TOOL_OBJECT = "BundleToolCube"
EDITOR_TOOL_FILE = "tool_wrote_this.txt"
EDITOR_TOOL_TEXT = "a copied editor ran an editor tool"
EDITOR_TOOL = """\
-- tool: Bundle Probe
editor.create_object{ id = "%s", mesh = "cube" }
editor.write_project_file{ path = "%s", content = "%s" }
editor.log("bundle probe authored %s")
""" % (EDITOR_TOOL_OBJECT, EDITOR_TOOL_FILE, EDITOR_TOOL_TEXT, EDITOR_TOOL_OBJECT)

# A tool that edits and then RAISES. Running someone's tool is only safe because
# a failed run is reported with the tool's own file:line and rolled back whole,
# so a copy has to do both.
FAILING_TOOL_NAME = "bundle_probe_fails"
FAILING_TOOL_OBJECT = "BundleToolGhost"
FAILING_TOOL = """\
-- tool: Bundle Probe Fails
editor.create_object{ id = "%s", mesh = "cube" }
local broken = definitely_not_a_function()
""" % FAILING_TOOL_OBJECT

# What a build WITHOUT scripting says when asked to run a tool. Keying the
# no-scripting outcome on this exact sentence keeps the leg unskippable on every
# build that HAS scripting: any other refusal is a failure.
NOSCRIPT_REFUSAL = "scripting is disabled in this build"


def hierarchy_ids(client):
    return [str(entry)
            for entry in (client.tool("list_hierarchy").get("ids") or [])]


def run_editor_tool_leg(args, stage_dir, sandbox_profile):
    """a copied editor asked to run an EDITOR TOOL.

    Editor tools and script components are the two ways a project extends the
    editor without compiling anything, and both must work on a machine that has
    only the app: the tool is a project file, the Lua runtime is linked in, and
    the tool's editor.* calls are the editor's own verbs. So there is exactly
    one acceptable answer here - the tool runs and its edits land - unless the
    build was made without scripting, which the copy must SAY."""
    executable = stage_app(args.editor_app, stage_dir)
    project = os.path.join(stage_dir, "project")
    shutil.copytree(args.project, project,
                    ignore=shutil.ignore_patterns("builds", ".orkige",
                                                  "build*", ".mcp.json"))
    for name in ("home", "cwd", "state", "tmp"):
        os.makedirs(os.path.join(stage_dir, name), exist_ok=True)
    token_file = os.path.join(stage_dir, "endpoint.token")
    env = scrubbed_env(stage_dir)
    process = launch(executable,
                     [executable, "--mcp-port", "0",
                      "--mcp-token-file", token_file],
                     os.path.join(stage_dir, "cwd"), env, sandbox_profile)
    try:
        port, token = wait_for_endpoint(process, token_file, args.boot_timeout)
        if not port:
            output = stop(process)
            print(output[-8000:], flush=True)
            fail("the copied editor never opened its MCP endpoint for the "
                 "editor-tool leg")
        client = McpClient(port, token)
        client.call("initialize", {"protocolVersion": "2025-03-26",
                                  "capabilities": {},
                                  "clientInfo": {"name": "bundle-selfcheck",
                                                 "version": "1"}})
        client.tool("open_project", {"path": project})
        client.tool("open_scene", {"scene": os.path.join(project, args.scene),
                                  "force": True})

        # (1) author the tool through the copy itself - a jailed write into the
        # open project's scripts/ folder, which is also what makes it discovered
        client.tool("write_project_file",
                    {"path": "scripts/%s.editor.lua" % EDITOR_TOOL_NAME,
                     "content": EDITOR_TOOL})

        # (2) run it. A build with no scripting refuses in one honest sentence;
        # any other refusal means the extension path is broken in a bundle
        accepted, structured, text = client.attempt(
            "run_editor_script", {"name": EDITOR_TOOL_NAME})
        if not accepted:
            if NOSCRIPT_REFUSAL in text:
                log("this build carries no scripting and says so: " + text)
                stop(process)
                return
            fail("the copied editor could not run an editor tool: " + text)

        # (3) the effects, read back from OUTSIDE the copy
        ids = hierarchy_ids(client)
        if EDITOR_TOOL_OBJECT not in ids:
            fail("the tool reported success but its object '%s' is not in the "
                 "scene (%s)" % (EDITOR_TOOL_OBJECT, ", ".join(ids[:10])))
        written = os.path.join(project, EDITOR_TOOL_FILE)
        if not os.path.isfile(written):
            fail("the tool wrote no %s into the copied project" % written)
        if open(written, encoding="utf-8").read().strip() != EDITOR_TOOL_TEXT:
            fail("the file the tool wrote does not carry what it wrote: " +
                 open(written, encoding="utf-8").read()[:200])
        if int(str(structured.get("command_count", "0")) or "0") < 1:
            fail("the run folded no undoable command, so nothing was edited "
                 "(command_count=%r)" % structured.get("command_count"))
        log("the copied editor ran an editor tool: it authored '%s' and wrote "
            "%s" % (EDITOR_TOOL_OBJECT, EDITOR_TOOL_FILE))

        # (4) a tool that raises: reported with the tool's own file:line, and
        # nothing of its run survives
        client.tool("write_project_file",
                    {"path": "scripts/%s.editor.lua" % FAILING_TOOL_NAME,
                     "content": FAILING_TOOL})
        accepted, _, text = client.attempt("run_editor_script",
                                           {"name": FAILING_TOOL_NAME})
        if accepted:
            fail("a tool that raises was reported as a clean run")
        # the file:line is the STAGED tool's own - no other copy of this file
        # exists, so naming it is also where the error came from
        if "%s.editor.lua:3" % FAILING_TOOL_NAME not in text:
            fail("the failing tool's error does not name its own file and "
                 "line: " + text)
        if FAILING_TOOL_OBJECT in hierarchy_ids(client):
            fail("a failed tool left '%s' behind instead of rolling back"
                 % FAILING_TOOL_OBJECT)
        log("a failing tool is reported with its file:line and rolls back: " +
            text)
        output = stop(process)
    except Exception as error:		# noqa: BLE001 - report and stop the app
        output = stop(process)
        print(output[-8000:], flush=True)
        fail("the editor-tool leg failed: %r" % (error,))
    finally:
        stop(process)	# no exit path leaves a staged editor running
    if "developer tree" in output:
        fail("the copied editor resolved resources from the developer tree")
    return output


def reject_build_machine_paths(args, text, what):
    """the failure this whole seam exists to prevent: a copied editor must
    never answer with a path from the machine that built it.

    The STAGED copy is exempt, and has to be: ctest stages it inside the build
    tree, so its own paths are legitimately under the repository - a copy
    naming its own writable state directory is saying where a user's files go,
    not reaching into a developer tree."""
    scrubbed = text.replace(args.stage_root, "<stage>")
    for marker in (args.repo_root, "CMakeCache.txt"):
        if marker and marker in scrubbed:
            fail("%s names the build machine ('%s'): %s" % (what, marker, text))


def poll_export(client, job_id, timeout):
    """wait out an export_project job; returns its structured result"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = client.tool("get_export_results", {"jobId": job_id})
        if str(result.get("status", "")) == "done":
            return result
        time.sleep(1.0)
    fail("the export never finished within %.0fs" % timeout)


def check_exported_app(args, app_dir, stage_dir, sandbox_profile):
    """the packaged game, as a person would find it: an app that carries a
    runnable executable, the engine media, and the project"""
    if not os.path.isdir(app_dir):
        fail("the export reported '%s', which is not an app bundle" % app_dir)
    contents = os.path.join(app_dir, "Contents")
    macos_dir = os.path.join(contents, "MacOS")
    resources = os.path.join(contents, "Resources")
    executables = [name for name in sorted(os.listdir(macos_dir))
                   if os.access(os.path.join(macos_dir, name), os.X_OK)]
    if not executables:
        fail("the exported app carries no executable in " + macos_dir)
    media = os.path.join(resources, "Media")
    # the flavor's shader tree is what makes the app able to render at all
    if not any(os.path.isdir(os.path.join(media, marker))
               for marker in ("Hlms", "Main")):
        fail("the exported app carries no engine shader media under " + media)
    for relative in (os.path.join("project", "project.orkproj"),
                     "orkige_project.txt", "AppIcon.icns"):
        if not os.path.isfile(os.path.join(resources, relative)):
            fail("the exported app is missing " + relative)
    # every dylib the game loads must live inside the app: the copy the editor
    # packaged from is one the user could delete tomorrow
    executable = os.path.join(macos_dir, executables[0])
    otool = subprocess.run(["otool", "-L", executable], capture_output=True,
                           text=True, check=True).stdout
    for line in otool.splitlines()[1:]:
        dep = line.strip().split(" (")[0]
        if not dep or dep.startswith(("/usr/lib/", "/System/")):
            continue
        if dep.startswith("@rpath/"):
            if not os.path.isfile(os.path.join(contents, "Frameworks",
                                               dep[len("@rpath/"):])):
                fail("the exported app's dylib '%s' was not bundled" % dep)
        else:
            fail("the exported app references the machine path " + dep)
    # THE proof: it runs, from a neutral cwd and inside the same clean room,
    # on nothing but what it carries (ORKIGE_DEMO_FRAMES caps the run)
    environment = scrubbed_env(stage_dir)
    environment["ORKIGE_DEMO_FRAMES"] = "3"
    command = [executable]
    if sandbox_profile:
        command = ["/usr/bin/sandbox-exec", "-f", sandbox_profile] + command
    result = subprocess.run(command, cwd=os.path.dirname(app_dir),
                            env=environment, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            errors="replace")
    if result.returncode != 0:
        print(result.stdout[-4000:], flush=True)
        fail("the exported game exited %d instead of running" %
             result.returncode)
    log("the exported game runs standalone (%s)" % os.path.basename(app_dir))


def assert_no_scripts(app_root):
    """the STRUCTURAL half of "a copy needs no interpreter": the staged app
    carries no script at all.

    The exporter and both asset cooks are code the editor links, so a `.py`
    anywhere under the app is a staging regression - either a retired tool
    crept back into the payload, or something started spawning one again.
    Checking the bytes on disk catches that even on a machine where an
    interpreter happens to be reachable, which a message-based check cannot."""
    found = []
    for parent, _, files in os.walk(app_root):
        for name in files:
            if name.endswith(".py"):
                found.append(os.path.relpath(os.path.join(parent, name),
                                             app_root))
    if found:
        fail("the staged editor carries scripts it would need an interpreter "
             "for: " + ", ".join(sorted(found)[:10]))
    log("the staged copy carries no script of any kind")


def run_export_leg(args, stage_dir, sandbox_profile):
    """a copied editor asked to PACKAGE the project it has open.

    There is exactly ONE acceptable answer: it exports a runnable app out of
    the engine payload it carries. The exporter is a library the editor LINKS,
    so there is no tool to find, no interpreter to preflight and nothing that
    could be missing on a user's machine - a refusal here means the payload
    staging regressed, and a missing-file error naming a directory from the
    machine that built the binary means a baked path escaped the resolver.

    The leg also asks for an iOS package, which a copy genuinely cannot
    produce (that needs the iOS player, which only a source build carries), and
    asserts the refusal says exactly that instead of failing generically."""
    executable = stage_app(args.editor_app, stage_dir)
    # nothing an interpreter would run rode along - asserted on the bytes
    assert_no_scripts(stage_dir)
    project = os.path.join(stage_dir, "project")
    shutil.copytree(args.project, project,
                    ignore=shutil.ignore_patterns("builds", ".orkige",
                                                  "build*", ".mcp.json"))
    for name in ("home", "cwd", "state", "tmp"):
        os.makedirs(os.path.join(stage_dir, name), exist_ok=True)
    token_file = os.path.join(stage_dir, "endpoint.token")
    # NO interpreter is handed in: PATH is scrubbed to /usr/bin:/bin and the
    # sandbox denies the machine's tool directories, so the export below runs
    # on nothing but the app's own code
    env = scrubbed_env(stage_dir)
    process = launch(executable,
                     [executable, "--mcp-port", "0",
                      "--mcp-token-file", token_file],
                     os.path.join(stage_dir, "cwd"), env, sandbox_profile)
    try:
        port, token = wait_for_endpoint(process, token_file, args.boot_timeout)
        if not port:
            output = stop(process)
            print(output[-8000:], flush=True)
            fail("the copied editor never opened its MCP endpoint for the "
                 "export leg")
        client = McpClient(port, token)
        client.call("initialize", {"protocolVersion": "2025-03-26",
                                  "capabilities": {},
                                  "clientInfo": {"name": "bundle-selfcheck",
                                                 "version": "1"}})
        client.tool("open_project", {"path": project})

        # (1) a platform a copy cannot produce: the refusal must SAY why
        accepted, _, text = client.attempt("export_project",
                                           {"platform": "ios-simulator"})
        if accepted:
            fail("the copied editor accepted an iOS export it has no player "
                 "for")
        reject_build_machine_paths(args, text, "the iOS export refusal")
        for needle in ("iOS", "player", "build Orkige from source"):
            if needle not in text:
                fail("the iOS export refusal does not say %r: %s"
                     % (needle, text))
        log("an iOS export is refused with what is missing: " + text)

        # (2) the desktop app: exported from the payload the copy carries.
        # A refusal is a FAILURE - the exporter is linked into this binary, so
        # there is nothing left for it to be missing.
        accepted, structured, text = client.attempt("export_project",
                                                    {"platform": "macos"})
        if not accepted:
            reject_build_machine_paths(args, text, "the export refusal")
            fail("the copied editor refused to package a game it carries "
                 "everything for: " + text)
        payload = str(structured.get("engineBuild", ""))
        if not payload.startswith(os.path.realpath(stage_dir)) and \
                not payload.startswith(stage_dir):
            fail("the export packaged from '%s', which is not inside the "
                 "copied app" % payload)
        result = poll_export(client, str(structured.get("jobId", "")),
                             args.export_timeout)
        if str(result.get("ok", "")) != "1":
            error = str(result.get("error", ""))
            reject_build_machine_paths(args, error, "the export failure")
            fail("the export failed: " + error)
        artifact = str(result.get("artifactPath", ""))
        log("the copied editor exported '%s' from its own payload (%s)"
            % (artifact, payload))
        check_exported_app(args, artifact, stage_dir, sandbox_profile)
        output = stop(process)
    except Exception as error:		# noqa: BLE001 - report and stop the app
        output = stop(process)
        print(output[-8000:], flush=True)
        fail("the export leg failed: %r" % (error,))
    finally:
        stop(process)	# no exit path leaves a staged editor running
    if "developer tree" in output:
        fail("the copied editor resolved resources from the developer tree")
    return output


# The browser payload is staged by the PACKAGING pipeline, never by the build,
# so a copy taken straight out of a build tree carries none. This leg stages it
# the way a released editor gets it - through the packaging tool's own function,
# so what is under test is the shipped mechanism and not a second one written
# here - and then asks the copy for a browser package.
PACKAGING_TOOL = os.path.join(REPO_ROOT, "Util", "orkige_nightly_package.py")
# what a web export must contain, from the web suite's own expectations
WEB_SUITE = os.path.join(REPO_ROOT, "tests", "web", "run_export_web.py")


def project_title(project_root):
    """the manifest <Name> the exported shell page carries as its title"""
    manifest = os.path.join(project_root, "project.orkproj")
    text = open(manifest, encoding="utf-8", errors="replace").read()
    start = text.find("<Name>")
    end = text.find("</Name>", start + 1)
    if start < 0 or end < 0:
        fail("no <Name> in " + manifest)
    return text[start + len("<Name>"):end].strip()


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def stage_browser_payload(args, executable):
    """put the browser payload inside the staged copy, exactly as the packaging
    pipeline puts it inside a release. Returns the payload directory, or ""
    when this machine has no wasm build tree to compose one from."""
    sys.path.insert(0, os.path.join(REPO_ROOT, "Util"))
    packaging = load_module("orkige_nightly_package", PACKAGING_TOOL)
    resources = staged_resource_dir(executable)
    if not os.path.isdir(resources):
        fail("the staged copy has no resource root at " + resources)
    if not packaging.WebPayload(build_dir=args.web_build).stage(resources):
        return ""
    payload = os.path.join(resources, packaging.WEB_PAYLOAD_DIR)
    problems = packaging.web_payload_problems(payload)
    if problems:
        fail("the staged browser payload is incomplete: " + ", ".join(problems))
    return payload


def check_web_export(args, artifact_dir, payload, stage_dir):
    """the packaged browser build, as a person would serve it: the artifact set
    the web suite defines, and a wasm player that is byte-for-byte the one the
    copied app carried - the proof it was packaged out of the bundle rather
    than out of a build tree that happens to be on this machine."""
    if not os.path.isdir(artifact_dir):
        fail("the export reported '%s', which is not a directory"
             % artifact_dir)
    web_suite = load_module("run_export_web", WEB_SUITE)
    # the web suite's own assertions: every artifact file present and non-empty,
    # the project's title substituted into the shell page, no placeholder left
    # unexpanded, and a payload archive of plausible size
    web_suite.assert_structure(artifact_dir, project_title(
        os.path.join(stage_dir, "project")))
    carried = os.path.join(payload, "orkige_player.wasm")
    shipped = os.path.join(artifact_dir, "orkige_player.wasm")
    if sha256(carried) != sha256(shipped):
        fail("the exported wasm player is not the one the app carries (%s vs "
             "%s)" % (carried, shipped))
    log("the browser export carries the app's own wasm player (%d bytes) and "
        "the whole artifact set" % os.path.getsize(shipped))


def run_web_export_leg(args, stage_dir, sandbox_profile):
    """a copied editor asked to package the open project FOR THE BROWSER.

    A web export compiles nothing - the wasm player is a prebuilt artifact and
    everything else is bytes the exporter arranges - so this is the one target a
    copy can package for on any host, whatever render flavor the editor itself
    is: the browser player is the classic flavor and rides inside the app.

    Skipped where no wasm build tree exists to stage a payload from. It is NOT
    passed silently: a released editor carries the payload, and a run that
    cannot stage one has proven nothing about it."""
    executable = stage_app(args.editor_app, stage_dir)
    payload = stage_browser_payload(args, executable)
    if not payload:
        skip("no browser payload to stage into the copy - build the "
             "web-release preset (or point --web-build / ORKIGE_WEB_BUILD at a "
             "wasm tree) to run this leg")
    log("staged the browser payload into the copied app (%s)" % payload)
    project = os.path.join(stage_dir, "project")
    shutil.copytree(args.project, project,
                    ignore=shutil.ignore_patterns("builds", ".orkige",
                                                  "build*", ".mcp.json"))
    for name in ("home", "cwd", "state", "tmp"):
        os.makedirs(os.path.join(stage_dir, name), exist_ok=True)
    token_file = os.path.join(stage_dir, "endpoint.token")
    env = scrubbed_env(stage_dir)
    process = launch(executable,
                     [executable, "--mcp-port", "0",
                      "--mcp-token-file", token_file],
                     os.path.join(stage_dir, "cwd"), env, sandbox_profile)
    try:
        port, token = wait_for_endpoint(process, token_file, args.boot_timeout)
        if not port:
            output = stop(process)
            print(output[-8000:], flush=True)
            fail("the copied editor never opened its MCP endpoint for the web "
                 "export leg")
        client = McpClient(port, token)
        client.call("initialize", {"protocolVersion": "2025-03-26",
                                  "capabilities": {},
                                  "clientInfo": {"name": "bundle-selfcheck",
                                                 "version": "1"}})
        client.tool("open_project", {"path": project})
        accepted, structured, text = client.attempt("export_project",
                                                    {"platform": "web"})
        if not accepted:
            reject_build_machine_paths(args, text, "the web export refusal")
            fail("the copied editor refused to package for the browser with "
                 "the payload it carries: " + text)
        packaged_from = str(structured.get("engineBuild", ""))
        if not packaged_from.startswith(os.path.realpath(stage_dir)) and \
                not packaged_from.startswith(stage_dir):
            fail("the web export packaged from '%s', which is not inside the "
                 "copied app" % packaged_from)
        result = poll_export(client, str(structured.get("jobId", "")),
                             args.export_timeout)
        if str(result.get("ok", "")) != "1":
            error = str(result.get("error", ""))
            reject_build_machine_paths(args, error, "the web export failure")
            fail("the web export failed: " + error)
        artifact = str(result.get("artifactPath", ""))
        log("the copied editor exported '%s' from its own payload (%s)"
            % (artifact, packaged_from))
        check_web_export(args, artifact, payload, stage_dir)
        output = stop(process)
    except Exception as error:		# noqa: BLE001 - report and stop the app
        output = stop(process)
        print(output[-8000:], flush=True)
        fail("the web export leg failed: %r" % (error,))
    finally:
        stop(process)	# no exit path leaves a staged editor running
    if "developer tree" in output:
        fail("the copied editor resolved resources from the developer tree")
    return output


# --- the native-module leg --------------------------------------------------
#
# A copied editor plus an INSTALLED SDK PACK must build and play a project's
# compiled C++ game code (Docs/sdk-pack.md). That needs a DIFFERENT clean room
# from every other leg here, and the difference is the whole point of this
# comment:
#
#   * the legs above prove a copied editor needs no developer TOOLING at all,
#     so they scrub PATH to /usr/bin:/bin and deny the machine's tool
#     directories outright.
#   * a native build genuinely needs a toolchain - we ship the engine, never a
#     compiler - so this leg hands back exactly two programs, cmake and ninja,
#     as INDIVIDUAL files plus the package directory each resolves into. The
#     platform's own compiler (/usr/bin/clang++ and the Xcode Command Line
#     Tools it dispatches to) was never denied: it is the machine's, like a GPU
#     driver.
#
# Everything that makes the leg meaningful stays denied: the REPOSITORY (and
# with it the engine build tree, every preset output and the engine sources),
# and the machine's vcpkg root. Those are what a stale-tree leak would come
# from - a build tree at its usual absolute path silently satisfying a
# configure that should have failed - so they are denied here exactly as the
# SDK pack test denies them.
#
# The leg asserts all three outcomes, because they must differ:
#   1. NO pack installed          -> the refusal names the SDK
#   2. a pack, NO cmake/ninja     -> the refusal names the build toolchain
#   3. a pack AND the toolchain   -> the module builds against the pack and
#                                    PLAYS, and the copy packages it too

def resolve_tool(path, name):
    """the absolute program the leg hands into the clean room"""
    resolved = path or shutil.which(name)
    if not resolved or not os.path.isfile(resolved):
        fail("no %s to build a native module with (pass --%s)" % (name, name))
    return os.path.abspath(resolved)


def tool_allowances(tool):
    """what the sandbox must allow for `tool` to run: the file itself (it may
    be a symlink inside an otherwise denied bin directory) and the package
    directory it resolves into, since a tool reads its own support files
    (cmake's Modules/ tree above all). Returns (files, subpaths)."""
    real = os.path.realpath(tool)
    # <prefix>/bin/<tool> -> <prefix>: the tool's own installation, and nothing
    # of the bin directory it was reached through
    prefix = os.path.dirname(os.path.dirname(real))
    return ([tool, real], [prefix])


def native_clean_room(args, stage_root, tools):
    """the two clean rooms this leg needs, as a (strict, tooled) pair.

    BOTH deny the same things - the repository (and with it the engine build
    tree, every preset output and the engine sources), the machine's vcpkg root
    and its tool directories. That denial is what the leg rests on: a build
    tree at its usual absolute path would silently satisfy a configure that
    should have failed, and a reachable tool directory would let a refusal that
    is meant to fire pass instead.

    They differ in ONE way: the tooled profile hands back cmake and ninja as
    individual files plus the package directory each resolves into (a tool
    reads its own support files - cmake's Modules/ tree above all). Nothing
    else is added, and the platform's own compiler was never denied: it is the
    machine's, like a GPU driver."""
    if sys.platform != "darwin" or not os.path.exists("/usr/bin/sandbox-exec"):
        log("clean room: ORKIGE_EDITOR_BUNDLE_ONLY only (no path sandbox on "
            "this platform)")
        return ("", "")
    denied = [args.repo_root]
    for extra in ("/opt/homebrew/bin", "/opt/homebrew/sbin", "/usr/local/bin",
                  "/usr/local/sbin"):
        if os.path.isdir(extra):
            denied.append(extra)
    vcpkg = os.environ.get("VCPKG_ROOT") or os.path.expanduser(
        "~/Development/vcpkg")
    if os.path.isdir(vcpkg):
        denied.append(vcpkg)
    strict = os.path.join(stage_root, "cleanroom-strict.sb")
    write_sandbox_profile(strict, denied, [stage_root])
    allow_files = []
    allowed = [stage_root]
    for tool in tools:
        files, subpaths = tool_allowances(tool)
        allow_files.extend(files)
        allowed.extend(subpaths)
    tooled = os.path.join(stage_root, "cleanroom-native.sb")
    write_sandbox_profile(tooled, denied, allowed, allow_files)
    log("clean room: the repository, the machine's vcpkg root and its tool "
        "directories are denied; the build room additionally reaches %s and "
        "nothing else"
        % ", ".join(os.path.basename(tool) for tool in tools))
    return (strict, tooled)


def install_sdk_pack(args, state_dir):
    """install the engine build tree as an SDK pack where a downloaded editor
    looks for one: <writable state>/sdk/<flavor> (core_project/NativeModule.h).
    Installed elsewhere and MOVED there, so nothing may depend on the path the
    install ran with."""
    staged = os.path.join(args.stage_root, "pack-install")
    shutil.rmtree(staged, ignore_errors=True)
    started = time.time()
    result = subprocess.run([args.cmake, "--install", args.engine_build,
                             "--prefix", staged, "--component", "sdk"],
                            capture_output=True, text=True, timeout=1800)
    if result.returncode != 0:
        print(result.stdout[-4000:], flush=True)
        print(result.stderr[-4000:], flush=True)
        fail("installing the SDK pack failed (%d)" % result.returncode)
    pack = os.path.join(state_dir, "sdk", args.flavor)
    os.makedirs(os.path.dirname(pack), exist_ok=True)
    shutil.rmtree(pack, ignore_errors=True)
    os.rename(staged, pack)
    if not os.path.isfile(os.path.join(pack, "cmake", "OrkigeSdkPack.cmake")):
        fail("the installed pack carries no cmake/OrkigeSdkPack.cmake")
    log("installed the SDK pack into the copy's state directory (%s, %.0fs)"
        % (pack, time.time() - started))
    return pack


def stage_native_project(args, stage_dir):
    """the project as a DISTRIBUTED project looks - its own tree, carrying its
    own sources. The in-tree module reads a header out of the engine's jumper
    sample; a project built against a pack has no checkout to read it from, so
    the copy travels with the project (the same staging the SDK pack test
    does, and what any self-contained project does with its own sources)."""
    project = os.path.join(stage_dir, "project")
    shutil.copytree(args.project, project,
                    ignore=shutil.ignore_patterns("builds", ".orkige",
                                                  "build*", ".mcp.json"))
    native = os.path.join(project, "native")
    for name in sorted(os.listdir(args.shared_headers)):
        if name.endswith(".h"):
            shutil.copy2(os.path.join(args.shared_headers, name), native)
    return project


def native_session(args, executable, stage_dir, sandbox_profile, env):
    """launch the staged copy and open the staged project over MCP"""
    token_file = os.path.join(stage_dir, "endpoint.token")
    if os.path.exists(token_file):
        os.remove(token_file)
    process = launch(executable,
                     [executable, "--mcp-port", "0",
                      "--mcp-token-file", token_file],
                     os.path.join(stage_dir, "cwd"), env, sandbox_profile)
    port, token = wait_for_endpoint(process, token_file, args.boot_timeout)
    if not port:
        output = stop(process)
        print(output[-8000:], flush=True)
        fail("the copied editor never opened its MCP endpoint for the native "
             "leg")
    client = McpClient(port, token)
    client.call("initialize", {"protocolVersion": "2025-03-26",
                              "capabilities": {},
                              "clientInfo": {"name": "bundle-selfcheck",
                                             "version": "1"}})
    client.tool("open_project", {"path": os.path.join(stage_dir, "project")})
    return (process, client)


def assert_play_refused(args, executable, stage_dir, sandbox_profile, env,
                        needles, forbidden, what):
    """press Play on the native project and assert it is refused with the
    sentence this prerequisite has - the two prerequisites must not produce one
    generic message, because they have different fixes"""
    process, client = native_session(args, executable, stage_dir,
                                     sandbox_profile, env)
    try:
        accepted, _, text = client.attempt("play", {"force": True},
                                           timeout=120.0)
        if accepted:
            fail("%s: Play was accepted, so nothing checked the prerequisite"
                 % what)
        reject_build_machine_paths(args, text, "the %s refusal" % what)
        for needle in needles:
            if needle.lower() not in text.lower():
                fail("the %s refusal does not say %r: %s"
                     % (what, needle, text))
        for needle in forbidden:
            if needle.lower() in text.lower():
                fail("the %s refusal talks about %r, which is the OTHER "
                     "prerequisite: %s" % (what, needle, text))
        log("%s: %s" % (what, text))
        stop(process)
    finally:
        stop(process)	# no exit path leaves a staged editor running


def assert_module_built_against_pack(args, project, pack):
    """the module tree the build produced must name the PACK - and nothing of
    the machine that built the editor. This is where a stale-tree leak would
    show: a configure that quietly found the engine build tree at its usual
    absolute path."""
    build = os.path.join(project, "native", "build-sdk-%s" % args.flavor)
    cache = os.path.join(build, "CMakeCache.txt")
    if not os.path.isfile(cache):
        fail("no module build tree at %s - compile-on-Play did not configure "
             "against the pack" % build)
    text = open(cache, encoding="utf-8", errors="replace").read()
    if pack not in text:
        fail("the module build tree does not name the pack (%s)" % pack)
    # the staged copy is exempt (ctest stages it inside the build tree, so the
    # pack's own path is legitimately under the repository); what must NOT
    # appear is the engine tree itself - a configure that found the build tree
    # at its usual absolute path is exactly the leak this leg exists to catch
    scrubbed = text.replace(args.stage_root, "<stage>")
    for marker in ("ORKIGE_ENGINE_BUILD_DIR", os.path.realpath(args.repo_root),
                   args.repo_root):
        if marker in scrubbed:
            fail("the module was configured against %r, not against the pack "
                 "alone" % marker)
    log("the module was configured against the pack alone (%s)" % build)


def run_native_leg(args, stage_dir):
    """a copied editor plus an installed SDK pack builds and plays a project's
    compiled C++ game code - and reports its two prerequisites as two."""
    tools = [resolve_tool(args.cmake, "cmake"),
             resolve_tool(args.ninja, "ninja")]
    strict_profile, sandbox_profile = native_clean_room(args, args.stage_root,
                                                        tools)
    executable = stage_app(args.editor_app, stage_dir)
    stage_native_project(args, stage_dir)
    for name in ("home", "cwd", "state", "tmp", "out"):
        os.makedirs(os.path.join(stage_dir, name), exist_ok=True)
    state_dir = os.path.join(stage_dir, "state")

    # (1) no SDK installed: the refusal names the SDK, never the toolchain
    toolless_env = scrubbed_env(stage_dir)
    assert_play_refused(args, executable, stage_dir, strict_profile,
                        toolless_env, needles=["SDK"],
                        forbidden=["cmake", "ninja"], what="missing-SDK")

    pack = install_sdk_pack(args, state_dir)

    # (2) the SDK is there, the machine has no build programs: a DIFFERENT
    # refusal, naming what to install. This runs in the STRICT room with the
    # scrubbed PATH - which is what a machine with no developer toolchain looks
    # like, and the reason the two rooms exist.
    assert_play_refused(args, executable, stage_dir, strict_profile,
                        toolless_env, needles=["cmake", "ninja", "toolchain"],
                        forbidden=["not installed"], what="missing-toolchain")

    # (3) both prerequisites met: compile-on-Play against the pack, then PLAY.
    # The two tool directories join the PATH - and nothing else does.
    env = scrubbed_env(stage_dir, {
        "PATH": os.pathsep.join(
            [os.path.dirname(tool) for tool in tools] + ["/usr/bin", "/bin"]),
    })
    process, client = native_session(args, executable, stage_dir,
                                     sandbox_profile, env)
    try:
        started = time.time()
        client.tool("play", {"force": True}, timeout=180.0)
        deadline = time.time() + args.native_timeout
        mode = ""
        state = {}
        while time.time() < deadline:
            state = client.tool("get_state")
            mode = str(state.get("play_mode", ""))
            if mode == "playing" or str(state.get("build_status")) == "failed":
                break
            time.sleep(2.0)
        if mode != "playing":
            errors = str(state.get("build_errors", ""))
            reject_build_machine_paths(args, errors, "the module build")
            output = stop(process)
            print(output[-8000:], flush=True)
            fail("the module never played (play_mode '%s', build_status '%s'): "
                 "%s" % (mode, state.get("build_status"), errors[-3000:]))
        log("the copied editor built the project's C++ game code against the "
            "pack and PLAYS it (%.0fs)" % (time.time() - started))
        client.tool("stop")
        assert_module_built_against_pack(args, os.path.join(stage_dir,
                                                            "project"), pack)

        # ...and it PACKAGES it: the same resolution stands behind export, so a
        # copy that can play compiled game code can ship it
        accepted, structured, text = client.attempt("export_project",
                                                    {"platform": "macos"})
        if not accepted:
            reject_build_machine_paths(args, text, "the native export refusal")
            fail("the copied editor refused to package a native project it "
                 "just built and played: " + text)
        result = poll_export(client, str(structured.get("jobId", "")),
                             args.native_timeout)
        if str(result.get("ok", "")) != "1":
            error = str(result.get("error", ""))
            reject_build_machine_paths(args, error, "the native export failure")
            fail("the native export failed: " + error)
        artifact = str(result.get("artifactPath", ""))
        log("the copied editor exported the native project (%s)" % artifact)
        check_exported_app(args, artifact, stage_dir, sandbox_profile)
        output = stop(process)
    except Exception as error:		# noqa: BLE001 - report and stop the app
        output = stop(process)
        print(output[-8000:], flush=True)
        fail("the native leg failed: %r" % (error,))
    finally:
        stop(process)	# no exit path leaves a staged editor running
    if "developer tree" in output:
        fail("the copied editor resolved resources from the developer tree")
    # the pack is several gigabytes; keep it only when something failed
    shutil.rmtree(pack, ignore_errors=True)
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


# What the packaging pipeline writes beside a released editor
# (Util/orkige_nightly_package.py's CHANGELOG.md, at the resource root the
# editor's locator reads). A synthetic one keeps this leg about the RESOLUTION
# rather than about whatever the real repository's history happens to say.
STAGED_CHANGELOG = """\
# Changelog

Orkige editor 2.0.0-nightly.20260731+abcdef123, built 2026-07-31 from
`abcdef123`.

## Changes since `0123456789`

- A packaged editor shows the changelog it shipped with (`abcdef123`)
- The staged copy reads it from its own resource root (`0123456789`)
"""


def run_changelog_leg(args, stage_dir, sandbox_profile):
    """the About box's two states, on a real copy. `--changelog` prints the
    SAME text the box draws (EditorBuildInfo.h), display-free, so both states
    are readable headlessly: a copy with no packaged changelog says so in one
    line, and a copy carrying the file the packaging pipeline writes shows
    THAT - never the repository's own history, which is not what a binary
    shipped with."""
    executable = stage_app(args.editor_app, stage_dir)
    for name in ("home", "cwd", "state", "tmp"):
        os.makedirs(os.path.join(stage_dir, name), exist_ok=True)
    env = scrubbed_env(stage_dir)
    env["ORKIGE_MCP_PORT"] = "off"

    def probe():
        process = launch(executable, [executable, "--changelog"],
                         os.path.join(stage_dir, "cwd"), env, sandbox_profile)
        try:
            output, _ = process.communicate(timeout=args.boot_timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            output = process.communicate()[0]
            fail("`--changelog` never exited - it must answer without opening "
                 "a window")
        if process.returncode != 0:
            print(output[-4000:], flush=True)
            fail("`--changelog` exited %d" % process.returncode)
        return output

    # a copy staged straight out of a build tree was never packaged
    packaged = os.path.join(staged_resource_dir(executable), "CHANGELOG.md")
    if os.path.exists(packaged):
        os.remove(packaged)
    absent = probe()
    if "carries no changelog" not in absent:
        print(absent[-4000:], flush=True)
        fail("an unpackaged copy must say it carries no changelog, not stay "
             "silent or invent one")
    log("an unpackaged copy says it carries no changelog")

    # ...and with the file a release carries, the copy reads it back
    with open(packaged, "w") as handle:
        handle.write(STAGED_CHANGELOG)
    present = probe()
    for expected in ("## Changes since `0123456789`",
                     "A packaged editor shows the changelog it shipped with",
                     "2.0.0-nightly.20260731+abcdef123"):
        if expected not in present:
            print(present[-4000:], flush=True)
            fail("the packaged changelog is missing %r from what the copy "
                 "reports" % expected)
    if "carries no changelog" in present:
        fail("a copy WITH a packaged changelog still reported having none")
    log("a packaged copy shows the changelog it shipped with")


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
    parser.add_argument("--leg", choices=("bundle", "web", "native"),
                       default="bundle",
                       help="'bundle' (default) runs the session, packaging, "
                            "unreadable-media and changelog legs; 'web' runs "
                            "the browser-package leg alone, which skips where "
                            "no wasm build tree exists - so a machine without "
                            "one never turns the others into a skip; 'native' "
                            "runs the compiled-game-code leg, which needs a "
                            "clean room of its own (a build toolchain is "
                            "reachable, the repository still is not)")
    parser.add_argument("--web-build", default="",
                       help="the wasm build tree the browser payload is "
                            "composed from (else ORKIGE_WEB_BUILD, else the "
                            "repository's build/web-release)")
    parser.add_argument("--engine-build", default="",
                       help="native leg: the engine build tree the SDK pack is "
                            "installed from")
    parser.add_argument("--flavor", default="next",
                       help="native leg: the render flavor of this build (the "
                            "installed pack is per flavor)")
    parser.add_argument("--shared-headers", default="",
                       help="native leg: headers the module reads out of the "
                            "engine tree, copied beside the staged project the "
                            "way a distributed project carries its own sources")
    parser.add_argument("--cmake", default="",
                       help="native leg: the cmake the clean room may run")
    parser.add_argument("--ninja", default="",
                       help="native leg: the ninja the clean room may run")
    parser.add_argument("--boot-timeout", type=float, default=120.0)
    parser.add_argument("--play-timeout", type=float, default=120.0)
    parser.add_argument("--export-timeout", type=float, default=300.0,
                       help="how long the packaging leg's export may take "
                            "(it copies the engine payload and cooks the "
                            "project's textures)")
    parser.add_argument("--native-timeout", type=float, default=1800.0,
                       help="how long the native leg's module build may take "
                            "(a cold compile of the game code plus the link "
                            "against the pack's engine archives)")
    args = parser.parse_args()
    # everything is spawned with a cwd OUTSIDE the tree, so every path the
    # driver hands on must be absolute
    for name in ("editor_app", "project", "stage_root", "repo_root"):
        setattr(args, name, os.path.abspath(getattr(args, name)))
    for name in ("web_build", "engine_build", "shared_headers"):
        if getattr(args, name):
            setattr(args, name, os.path.abspath(getattr(args, name)))

    if not os.path.exists(args.editor_app):
        skip("no built editor app at %s" % args.editor_app)

    shutil.rmtree(args.stage_root, ignore_errors=True)
    os.makedirs(args.stage_root)

    sandbox_profile = ""
    if args.leg == "native":
        # the native leg writes its OWN profile: a compiled-code build needs a
        # toolchain, which the clean room below denies wholesale (see
        # run_native_leg for the difference and why it is drawn there)
        pass
    elif sys.platform == "darwin" and os.path.exists("/usr/bin/sandbox-exec"):
        # the repository (and with it the vcpkg tree and every build output) is
        # what a copied app must not need. Homebrew's TOOL directories go too -
        # a copied editor spawns no tool at all and must never reach for the
        # developer machine's - but the rest of Homebrew stays readable,
        # because MoltenVK
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
        if args.leg == "native":
            # its own clean room, written by the leg: a native build needs a
            # toolchain, so the profile above (which denies every tool
            # directory) is the wrong one - see run_native_leg
            if not args.engine_build or not os.path.isdir(args.engine_build):
                fail("the native leg needs --engine-build (the tree the SDK "
                     "pack is installed from)")
            if not args.shared_headers or not os.path.isdir(
                    args.shared_headers):
                fail("the native leg needs --shared-headers")
            native_stage = os.path.join(args.stage_root, "native")
            os.makedirs(native_stage)
            run_native_leg(args, native_stage)
            log("PASSED: a copied editor plus an installed SDK pack builds, "
                "plays and packages compiled C++ game code - and reports a "
                "missing SDK and a missing toolchain as two different things")
            return 0

        if args.leg == "web":
            web_stage = os.path.join(args.stage_root, "web")
            os.makedirs(web_stage)
            run_web_export_leg(args, web_stage, sandbox_profile)
            log("PASSED: the copied editor packages a browser build out of the "
                "payload it carries, on a host with no wasm toolchain")
            return 0

        session_stage = os.path.join(args.stage_root, "session")
        os.makedirs(session_stage)
        run_session_leg(args, session_stage, sandbox_profile)

        tool_stage = os.path.join(args.stage_root, "editortool")
        os.makedirs(tool_stage)
        run_editor_tool_leg(args, tool_stage, sandbox_profile)

        export_stage = os.path.join(args.stage_root, "export")
        os.makedirs(export_stage)
        run_export_leg(args, export_stage, sandbox_profile)

        media_stage = os.path.join(args.stage_root, "unreadable")
        os.makedirs(media_stage)
        run_unreadable_media_leg(args, media_stage, sandbox_profile)

        changelog_stage = os.path.join(args.stage_root, "changelog")
        os.makedirs(changelog_stage)
        run_changelog_leg(args, changelog_stage, sandbox_profile)
    finally:
        clean_staged_identity_state()

    log("PASSED: the copied editor boots, renders, opens a project, plays, "
        "runs an editor tool, packages a game and reports what it shipped "
        "with, using nothing but what it carries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
