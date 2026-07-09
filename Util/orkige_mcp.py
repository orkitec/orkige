#!/usr/bin/env python3
"""orkige_mcp.py - a Model Context Protocol (MCP) server bridging an AI agent
to a running Orkige editor's control port (work package #80).

The editor (tools/editor) opens an opt-in control port with
`--control-port <N> --control-token-file <path>`: a second core_debugnet
DebugServer, in the EDITOR process, speaking line-delimited JSON over loopback
TCP (one flat JSON object per line, '\\n' terminated - the DebugMessage wire
format, see orkige_core/core_debugnet). This script is the other end: a small
MCP server (stdio transport, the official `mcp` Python SDK) exposing a focused,
Blender-MCP-sized set of tools that translate an agent's calls into control-port
requests and relay the replies.

Two additive conventions ride on top of the transport:
  * request/response CORRELATION - each request carries a unique "req" id the
    editor echoes in its "ok"/"err" reply, so replies match requests even when
    unsolicited notifications (log lines) interleave.
  * an AUTH TOKEN - the editor writes a secret to the token file; this bridge
    reads it and presents it in a "hello" before any mutation is accepted.

Register with Claude Code:

    claude mcp add orkige -- python3 Util/orkige_mcp.py

Configuration (environment):
  ORKIGE_CONTROL_TOKEN_FILE  the editor's --control-token-file (holds
                             "<port>\\n<token>\\n"); the bridge reads both the
                             port and the token from it. Preferred.
  ORKIGE_CONTROL_HOST        control host (default 127.0.0.1)
  ORKIGE_CONTROL_PORT        control port (when no token file, or the token
                             file has no port line)
  ORKIGE_CONTROL_TOKEN       auth token (when no token file)
  ORKIGE_EDITOR_BIN          if set AND no editor is reachable, spawn this
                             editor binary with --control-port 0
                             --control-token-file <temp> and connect to it

Run `python3 Util/orkige_mcp.py --selftest` to exercise the bridge's framing,
correlation and auth against a stub control server - stdlib only, no editor and
no `mcp` SDK required (this is the mcp_bridge_selftest ctest).
"""

import json
import os
import socket
import sys
import tempfile
import threading
import time

# transport constants mirrored from core_debugnet/DebugProtocol
PROTOCOL_VERSION = 1
MAX_LINE_LENGTH = 64 * 1024


class ControlError(RuntimeError):
    """An "err" reply from the editor (or a transport failure)."""


class ControlLink:
    """The client end of the editor control port: framing + request/response
    correlation + auth. Single connection, synchronous request/reply."""

    def __init__(self, host, port, token=""):
        self.host = host
        self.port = int(port)
        self.token = token or ""
        self._sock = None
        self._inbuf = b""
        self._pending = []          # decoded messages read ahead of their turn
        self._req_counter = 0

    # --- connection ---------------------------------------------------------

    def connect(self, timeout=10.0):
        """Open the TCP link and authenticate with a hello. Retries the connect
        for `timeout` seconds so a just-launched editor has time to listen."""
        deadline = time.time() + timeout
        last_error = None
        while time.time() < deadline:
            try:
                self._sock = socket.create_connection(
                    (self.host, self.port), timeout=5.0)
                self._sock.settimeout(10.0)
                break
            except OSError as error:
                last_error = error
                time.sleep(0.1)
        if self._sock is None:
            raise ControlError(
                "could not connect to the editor control port at "
                "%s:%d (%s)" % (self.host, self.port, last_error))
        reply = self.request("hello", {"token": self.token})
        if reply.get("authenticated") != "1":
            raise ControlError("authentication failed")
        return reply

    def close(self):
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    # --- framing ------------------------------------------------------------

    def _encode(self, mtype, fields):
        """Build one wire line: a flat JSON object {"v":1,"type":...,...}."""
        message = {"v": PROTOCOL_VERSION, "type": mtype}
        for key, value in (fields or {}).items():
            message[key] = value
        line = json.dumps(message, separators=(",", ":"))
        if "\n" in line or "\r" in line:
            raise ControlError("encoded message contains a newline")
        data = (line + "\n").encode("utf-8")
        if len(data) > MAX_LINE_LENGTH:
            raise ControlError("message exceeds the transport line cap")
        return data

    def _send_raw(self, data):
        if self._sock is None:
            raise ControlError("not connected")
        self._sock.sendall(data)

    def _read_message(self):
        """Read and decode the next complete line; buffers partial reads."""
        while b"\n" not in self._inbuf:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise ControlError("the editor closed the control connection")
            self._inbuf += chunk
            if len(self._inbuf) > 2 * MAX_LINE_LENGTH:
                raise ControlError("oversized reply (framing lost)")
        line, self._inbuf = self._inbuf.split(b"\n", 1)
        return json.loads(line.decode("utf-8"))

    # --- request/response with correlation ----------------------------------

    def send(self, mtype, fields=None):
        """Send a request; returns its unique correlation id."""
        self._req_counter += 1
        req = "m%d" % self._req_counter
        message = dict(fields or {})
        message["req"] = req
        self._send_raw(self._encode(mtype, message))
        return req

    def await_reply(self, req):
        """Read until the reply correlated with `req` arrives, buffering (and
        for notifications, dropping) anything else. Raises on an "err"."""
        # a matching reply may already be buffered from an earlier call
        for index, message in enumerate(self._pending):
            if message.get("req") == req:
                del self._pending[index]
                return self._unwrap(message, req)
        while True:
            message = self._read_message()
            mtype = message.get("type")
            if message.get("req") == req and mtype in ("ok", "err"):
                return self._unwrap(message, req)
            if mtype in ("ok", "err") and "req" in message:
                # a reply for a different outstanding request - keep it
                self._pending.append(message)
            # notifications (log/error without our req) are dropped

    def _unwrap(self, message, req):
        if message.get("type") == "err":
            raise ControlError(message.get("message", "unspecified error"))
        return message

    def request(self, mtype, fields=None):
        """Send one request and return its correlated "ok" reply dict."""
        req = self.send(mtype, fields)
        return self.await_reply(req)


# --- spawn-or-connect -------------------------------------------------------


def _read_token_file(path):
    """Parse the editor's token file ("<port>\\n<token>\\n"); returns
    (port_or_None, token_or_None)."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            lines = handle.read().splitlines()
    except OSError:
        return None, None
    port = None
    token = None
    if len(lines) >= 2:
        try:
            port = int(lines[0].strip())
        except ValueError:
            port = None
        token = lines[1].strip()
    elif len(lines) == 1:
        token = lines[0].strip()
    return port, token


def connect_or_spawn():
    """Resolve a ControlLink: connect to a configured/running editor, or spawn
    ORKIGE_EDITOR_BIN with a fresh control port when nothing is reachable."""
    host = os.environ.get("ORKIGE_CONTROL_HOST", "127.0.0.1")
    token_file = os.environ.get("ORKIGE_CONTROL_TOKEN_FILE", "")
    port = None
    token = os.environ.get("ORKIGE_CONTROL_TOKEN", "")

    if token_file:
        file_port, file_token = _read_token_file(token_file)
        if file_port is not None:
            port = file_port
        if file_token:
            token = file_token
    if port is None and os.environ.get("ORKIGE_CONTROL_PORT"):
        port = int(os.environ["ORKIGE_CONTROL_PORT"])

    if port is not None:
        link = ControlLink(host, port, token)
        link.connect()
        return link

    editor_bin = os.environ.get("ORKIGE_EDITOR_BIN", "")
    if editor_bin:
        import subprocess
        spawn_token = os.path.join(
            tempfile.gettempdir(), "orkige_control_%d.token" % os.getpid())
        try:
            os.remove(spawn_token)
        except OSError:
            pass
        subprocess.Popen([editor_bin, "--control-port", "0",
                          "--control-token-file", spawn_token])
        deadline = time.time() + 60.0
        while time.time() < deadline:
            file_port, file_token = _read_token_file(spawn_token)
            if file_port is not None and file_token:
                link = ControlLink(host, file_port, file_token)
                link.connect()
                return link
            time.sleep(0.25)
        raise ControlError("spawned editor never opened its control port")

    raise ControlError(
        "no editor control port configured: set ORKIGE_CONTROL_TOKEN_FILE "
        "(or ORKIGE_CONTROL_PORT [+ ORKIGE_CONTROL_TOKEN]), or "
        "ORKIGE_EDITOR_BIN to spawn one")


# --- the MCP server ---------------------------------------------------------


def run_server():
    """Stand up the stdio MCP server exposing the editor tools."""
    from mcp.server.fastmcp import FastMCP

    mcp = FastMCP("orkige")
    # one shared link, established lazily on the first tool call so the server
    # starts even before the editor is up
    state = {"link": None}

    def link():
        if state["link"] is None:
            state["link"] = connect_or_spawn()
        return state["link"]

    def _lines(reply, key):
        value = reply.get(key, [])
        return value if isinstance(value, list) else []

    @mcp.tool()
    def get_state() -> str:
        """Report the editor's current state: open project, current scene,
        dirty flag, selection, object count, undo/redo availability and the
        play mode."""
        r = link().request("get_state")
        return json.dumps(r, indent=2)

    @mcp.tool()
    def open_project(path: str) -> str:
        """Open an Orkige project (a directory or a .orkproj manifest). Refused
        with unsaved changes unless the scene is clean."""
        r = link().request("open_project", {"path": path})
        return "opened project at %s (scene: %s)" % (
            r.get("project_root", ""), r.get("scene_path", "(none)"))

    @mcp.tool()
    def new_scene(force: bool = False) -> str:
        """Clear the world to a fresh untitled scene. Pass force=true to
        discard unsaved changes."""
        link().request("new_scene", {"force": "1" if force else "0"})
        return "new scene"

    @mcp.tool()
    def open_scene(path: str, force: bool = False) -> str:
        """Open a .oscene file. force=true discards unsaved changes."""
        link().request("open_scene",
                       {"scene": path, "force": "1" if force else "0"})
        return "opened scene %s" % path

    @mcp.tool()
    def save_scene(path: str = "") -> str:
        """Save the current scene. With no path, saves to the current file
        (fails if the scene is untitled)."""
        r = link().request("save_scene", {"scene": path})
        return "saved scene to %s" % r.get("scene", path)

    @mcp.tool()
    def list_hierarchy() -> str:
        """List every GameObject in the scene with its parent, own active flag
        and effective active-in-hierarchy flag."""
        r = link().request("list_hierarchy")
        ids = _lines(r, "ids")
        parents = _lines(r, "parents")
        active = _lines(r, "active")
        rows = []
        for index, oid in enumerate(ids):
            parent = parents[index] if index < len(parents) else ""
            act = active[index] if index < len(active) else "1"
            rows.append("%s\tparent=%s\tactive=%s" % (
                oid, parent or "(root)", act))
        rows.append("selected: %s" % (r.get("selected") or "(none)"))
        return "\n".join(rows) if rows else "(empty scene)"

    @mcp.tool()
    def get_component(object_id: str, component: str) -> str:
        """Read one component's typed property bundle. component is one of
        TransformComponent, ModelComponent, ScriptComponent,
        RigidBodyComponent, CameraComponent, SpriteComponent."""
        r = link().request("get_component",
                           {"id": object_id, "component": component})
        return json.dumps({k: v for k, v in r.items()
                           if k not in ("v", "type", "req")}, indent=2)

    @mcp.tool()
    def set_component(object_id: str, component: str,
                      properties: dict) -> str:
        """Set properties on a component (undoable). `properties` maps field
        names to string values, e.g. {"position": "1 2 3"} for a
        TransformComponent, {"mesh": "cube.mesh"} for a ModelComponent,
        {"script": "scripts/player.lua", "enabled": "1"} for a
        ScriptComponent."""
        fields = {"id": object_id, "component": component}
        for key, value in (properties or {}).items():
            fields[key] = str(value)
        link().request("set_component", fields)
        return "set %s on %s" % (component, object_id)

    @mcp.tool()
    def create_object(object_id: str = "", mesh: str = "cube",
                      position: str = "0 0 0") -> str:
        """Create a GameObject carrying a mesh (default a cube) at a position
        ("x y z"). Returns the created object's id."""
        r = link().request("create_object",
                           {"id": object_id, "mesh": mesh,
                            "position": position})
        return "created object %s" % r.get("id", "")

    @mcp.tool()
    def delete_object(object_id: str) -> str:
        """Delete a GameObject (undoable)."""
        link().request("delete_object", {"id": object_id})
        return "deleted %s" % object_id

    @mcp.tool()
    def reparent_object(object_id: str, parent: str = "") -> str:
        """Re-parent a GameObject (world transform preserved). An empty parent
        makes it a root."""
        link().request("reparent_object", {"id": object_id, "parent": parent})
        return "reparented %s under %s" % (object_id, parent or "(root)")

    @mcp.tool()
    def add_component(object_id: str, component: str) -> str:
        """Add a component (by type name) to a GameObject (undoable)."""
        link().request("add_component",
                       {"id": object_id, "component": component})
        return "added %s to %s" % (component, object_id)

    @mcp.tool()
    def remove_component(object_id: str, component: str) -> str:
        """Remove a component from a GameObject (undoable; refused while
        another component depends on it)."""
        link().request("remove_component",
                       {"id": object_id, "component": component})
        return "removed %s from %s" % (component, object_id)

    @mcp.tool()
    def play() -> str:
        """Enter play mode: the editor spawns the player on the current scene.
        Returns immediately; poll get_state for the play mode."""
        r = link().request("play")
        return "play accepted (mode: %s)" % r.get("play_mode", "?")

    @mcp.tool()
    def stop() -> str:
        """Stop play mode and revert the editor to edit mode."""
        link().request("stop")
        return "stopped"

    @mcp.tool()
    def screenshot(path: str, window: bool = False) -> str:
        """Write a screenshot to `path` (.png). By default captures the
        chrome-free scene viewport; window=true captures the whole editor
        window. Returns the written path for the agent to read."""
        r = link().request("screenshot",
                           {"path": path, "window": "1" if window else "0"})
        return r.get("path", path)

    @mcp.tool()
    def list_assets() -> str:
        """List the open project's assets (id + path) and scenes."""
        r = link().request("list_assets")
        ids = _lines(r, "asset_ids")
        paths = _lines(r, "asset_paths")
        rows = ["%s\t%s" % (paths[i] if i < len(paths) else "?", ids[i])
                for i in range(len(ids))]
        rows.append("scenes: " + ", ".join(_lines(r, "scenes")))
        return "\n".join(rows) if rows else "(no assets)"

    @mcp.tool()
    def console_tail(count: int = 50) -> str:
        """Return the last `count` editor console lines (engine/editor/remote
        log)."""
        r = link().request("console_tail", {"count": str(count)})
        return "\n".join(_lines(r, "lines")) or "(console empty)"

    mcp.run()


# --- selftest (stdlib only; no editor, no mcp SDK) --------------------------


class _StubControlServer:
    """A minimal editor-control-port stand-in for the selftest: speaks the
    DebugMessage framing and, to prove the bridge's correlation, prefixes every
    reply with a decoy notification AND a mis-correlated reply that the bridge
    must skip."""

    def __init__(self, token):
        self.token = token
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self.port = self._listener.getsockname()[1]
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _send(self, conn, obj):
        conn.sendall((json.dumps(obj, separators=(",", ":")) + "\n")
                     .encode("utf-8"))

    def _serve(self):
        # serial multi-client: handle one connection to completion, then accept
        # the next (the selftest connects a rejected client before the real one)
        while True:
            try:
                conn, _ = self._listener.accept()
            except OSError:
                return
            buf = b""
            try:
                while True:
                    while b"\n" not in buf:
                        chunk = conn.recv(4096)
                        if not chunk:
                            raise OSError("closed")
                        buf += chunk
                    line, buf = buf.split(b"\n", 1)
                    request = json.loads(line.decode("utf-8"))
                    self._reply(conn, request)
            except OSError:
                conn.close()
                continue

    def _reply(self, conn, request):
        mtype = request.get("type")
        req = request.get("req", "")
        if mtype == "hello":
            ok = {"v": 1, "type": "ok", "req": req}
            ok["authenticated"] = "1" if request.get("token") == self.token \
                else "0"
            self._send(conn, ok)
            return
        # a decoy notification (no req) and a mis-correlated reply: the bridge
        # must ignore both and still return the correctly-correlated reply
        self._send(conn, {"v": 1, "type": "log", "message": "decoy"})
        self._send(conn, {"v": 1, "type": "ok", "req": "WRONG-" + req,
                          "note": "mis-correlated"})
        reply = {"v": 1, "type": "ok", "req": req}
        if mtype == "list_hierarchy":
            reply["ids"] = ["Cube1", "Cube2", "TestMesh1"]
            reply["parents"] = ["", "", ""]
            reply["active"] = ["1", "1", "1"]
        elif mtype == "create_object":
            reply["id"] = request.get("id") or "Object1"
        elif mtype == "get_component":
            reply["position"] = "1 2 3"
        elif mtype == "boom":
            reply = {"v": 1, "type": "err", "req": req,
                     "message": "intentional failure"}
        self._send(conn, reply)


def _selftest():
    token = "5f2c9ab1deadbeef5f2c9ab1deadbeef"
    server = _StubControlServer(token)
    server.start()

    # (1) auth with the WRONG token must be rejected
    bad = ControlLink("127.0.0.1", server.port, token="wrong")
    try:
        bad.connect(timeout=5.0)
        print("SELFTEST FAILED: wrong token was accepted", file=sys.stderr)
        return 1
    except ControlError:
        pass
    finally:
        bad.close()

    # (2) auth with the right token, then a representative verb flow - every
    # reply is preceded by a decoy + a mis-correlated reply the bridge must
    # skip, proving framing AND request/response correlation
    link = ControlLink("127.0.0.1", server.port, token=token)
    link.connect(timeout=5.0)

    hierarchy = link.request("list_hierarchy")
    assert hierarchy["ids"] == ["Cube1", "Cube2", "TestMesh1"], hierarchy
    created = link.request("create_object", {"id": "Probe"})
    assert created["id"] == "Probe", created
    transform = link.request("get_component",
                             {"id": "Probe", "component": "TransformComponent"})
    assert transform["position"] == "1 2 3", transform

    # (3) interleaved correlation: send two requests, THEN read - the bridge
    # must return each reply matched to its own req id
    req_a = link.send("get_component",
                      {"id": "Probe", "component": "TransformComponent"})
    req_b = link.send("list_hierarchy")
    reply_a = link.await_reply(req_a)
    reply_b = link.await_reply(req_b)
    assert reply_a["req"] == req_a and reply_a["position"] == "1 2 3"
    assert reply_b["req"] == req_b and "ids" in reply_b

    # (4) an "err" reply surfaces as a ControlError
    try:
        link.request("boom")
        print("SELFTEST FAILED: err was not raised", file=sys.stderr)
        return 1
    except ControlError as error:
        assert "intentional failure" in str(error), error

    link.close()
    print("orkige_mcp bridge selftest OK (framing, correlation, auth)")
    return 0


def main():
    if "--selftest" in sys.argv[1:]:
        sys.exit(_selftest())
    run_server()


if __name__ == "__main__":
    main()
