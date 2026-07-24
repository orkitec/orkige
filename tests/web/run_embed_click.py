#!/usr/bin/env python3
"""Web EMBED regression check: the benchmark export must render and be
CLICKABLE inside a small iframe-sized viewport, the way the public site embeds
it (a ~58rem 16:9 column, not a full browser tab). python3 stdlib only.

Two failure classes this guards, both invisible to the full-tab boot test:
  * the fixed-size canvas is CSS-scaled into the smaller container and CROPPED
    - the HUD title and the results-card Restart button fall out of view;
  * the results screen never enabled input events, so the Restart button is
    visible but DEAD to a real click.

The check drives a headless Chrome over the DevTools Protocol (a tiny stdlib
websocket) at the exact embed viewport: it asserts the HUD title renders
(pixel-visible, i.e. not cropped), then reaches the results card, drives a REAL
mouse click on the Restart button's on-screen position, and asserts the tour
restarts from scene 1 (the click reached the button's hit rect). No
autoRestart seam is used - only the browser click exercises the wiring.

SKIPs (exit 77) on a machine without a headless browser. Exit 0 pass, 1 fail.
"""
import base64
import json
import os
import re
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run_export_web import export, find_browser, serve, REPO_ROOT  # noqa: E402
sys.path.insert(0, os.path.join(REPO_ROOT, "Util"))
import orkige_png  # noqa: E402

BENCHMARK = os.path.join(REPO_ROOT, "projects", "benchmark")
# a 58rem column at 16px, 16:9 - the site's Live Benchmark iframe content size
VIEW_W, VIEW_H = 928, 522


def log(m):
    print("run_embed_click: " + m, flush=True)


def fail(m, cdp=None):
    # on a tour-progress failure the browser console tail says WHERE the run
    # stalled (which director scene last reported) - without it a timeout is
    # undiagnosable from the harness log alone
    if cdp is not None:
        with cdp.lock:
            tail = cdp.console[-25:]
        for line in tail:
            print("run_embed_click: console| " + line, flush=True)
    print("run_embed_click: FAILED - " + m, flush=True)
    sys.exit(1)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class WS:
    """the tiniest websocket client that can carry CDP JSON."""

    def __init__(self, host, port, path):
        self.sock = socket.create_connection((host, port))
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
            % (path, host, port, key)).encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.sock.recv(4096)
        if b"101" not in buf.split(b"\r\n")[0]:
            raise RuntimeError("ws upgrade failed")
        self.buf = buf.split(b"\r\n\r\n", 1)[1]

    def send(self, text):
        data = text.encode()
        mask = os.urandom(4)
        header = bytearray([0x81])
        n = len(data)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        header += mask
        self.sock.sendall(bytes(header)
                          + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("ws closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self):
        b0, b1 = self._read(2)
        opcode = b0 & 0x0f
        length = b1 & 0x7f
        if length == 126:
            length = struct.unpack(">H", self._read(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self._read(8))[0]
        payload = self._read(length)
        if opcode == 0x8:
            raise RuntimeError("ws close")
        if opcode == 0x9:
            return None
        return payload.decode("utf-8", "replace")


class CDP:
    def __init__(self, ws):
        self.ws = ws
        self.id = 0
        self.results = {}
        self.console = []
        self.lock = threading.Lock()
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        while True:
            try:
                msg = self.ws.recv()
            except Exception:
                return
            if not msg:
                continue
            obj = json.loads(msg)
            if "id" in obj:
                with self.lock:
                    self.results[obj["id"]] = obj
            method = obj.get("method")
            if method == "Runtime.consoleAPICalled":
                for arg in obj["params"].get("args", []):
                    if isinstance(arg.get("value"), str):
                        with self.lock:
                            self.console.append(arg["value"])
            elif method == "Log.entryAdded":
                with self.lock:
                    self.console.append(obj["params"]["entry"].get("text", ""))

    def call(self, method, params=None, timeout=20):
        self.id += 1
        mid = self.id
        self.ws.send(json.dumps({"id": mid, "method": method,
                                 "params": params or {}}))
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                if mid in self.results:
                    return self.results.pop(mid).get("result", {})
            time.sleep(0.01)
        raise RuntimeError("timeout on " + method)

    def wait_console(self, pattern, timeout, after=0):
        rx = re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                for line in self.console[after:]:
                    if rx.search(line):
                        return line
            time.sleep(0.05)
        return None


def hud_band_light_pixels(png_bytes, out_dir):
    """count clearly-light pixels (min channel high, i.e. the white-ish title
    glyphs, not the darker sky/scrim) in the top-left HUD band."""
    path = os.path.join(out_dir, "embed_hud.png")
    with open(path, "wb") as f:
        f.write(png_bytes)
    img = orkige_png.decode_png(path)     # pixels are RGBA
    bright = 0
    for y in range(0, int(img.height * 0.16), 2):
        for x in range(0, int(img.width * 0.45), 2):
            off = (y * img.width + x) * 4
            r, g, b = img.pixels[off], img.pixels[off + 1], img.pixels[off + 2]
            if min(r, g, b) > 140:
                bright += 1
    return bright


def assert_hud_title(cdp, out_dir):
    """the HUD title band (top-left) must render light text - it is CROPPED off
    the top of the frame without the responsive-canvas fix. The first scene
    FADES IN from black and the HUD lights a frame or two after its 'ready'
    log, so poll a few captures until it appears; a real crop never lights up,
    so the loop still fails honestly."""
    best = 0
    for _ in range(20):                   # ~8s at 0.4s cadence
        best = max(best, hud_band_light_pixels(screenshot(cdp), out_dir))
        if best >= 30:
            log("HUD title band light-text pixels: %d" % best)
            return
        time.sleep(0.4)
    fail("the HUD title band stayed blank (best %d light px over ~8s) - the "
         "top of the frame is cropped at the embed size" % best)


def screenshot(cdp):
    return base64.b64decode(cdp.call("Page.captureScreenshot",
                                     {"format": "png"})["data"])


def main():
    browser = find_browser()
    if not browser:
        print("run_embed_click: SKIPPED - no headless Chrome/Chromium (set "
              "ORKIGE_CHROME to override)", flush=True)
        sys.exit(77)

    engine_build = os.path.abspath(sys.argv[sys.argv.index("--engine-build")
                                            + 1])
    out = os.path.abspath(sys.argv[sys.argv.index("--output") + 1])
    export(engine_build, out, BENCHMARK)
    if not os.path.isfile(os.path.join(out, "index.html")):
        fail("no export produced at " + out)

    srv, port = serve(out)
    dbg = free_port()
    import shutil
    profile = os.path.join(out, "chrome_profile")
    shutil.rmtree(profile, ignore_errors=True)
    proc = subprocess.Popen(
        [browser, "--headless=new", "--no-first-run",
         "--no-default-browser-check", "--user-data-dir=" + profile,
         "--enable-unsafe-swiftshader", "--remote-debugging-port=%d" % dbg,
         "--remote-allow-origins=*", "--window-size=%d,%d" % (VIEW_W, VIEW_H),
         "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        ws_url = None
        for _ in range(150):
            try:
                data = urllib.request.urlopen(
                    "http://127.0.0.1:%d/json" % dbg, timeout=1).read()
                for t in json.loads(data):
                    if t.get("type") == "page":
                        ws_url = t["webSocketDebuggerUrl"]
                        break
                if ws_url:
                    break
            except Exception:
                time.sleep(0.2)
        if not ws_url:
            fail("could not reach Chrome DevTools")
        m = re.match(r"ws://([^:/]+):(\d+)(/.*)", ws_url)
        cdp = CDP(WS(m.group(1), int(m.group(2)), m.group(3)))
        cdp.call("Runtime.enable")
        cdp.call("Log.enable")
        cdp.call("Page.enable")
        # pin the exact embed content viewport (Chrome's --window-size leaves
        # innerHeight short of the window)
        cdp.call("Emulation.setDeviceMetricsOverride",
                 {"width": VIEW_W, "height": VIEW_H, "deviceScaleFactor": 1,
                  "mobile": False})
        # reach the results card fast; NO autoRestart - the ONLY restart path
        # is the browser click below. Shadows run OFF here: the classic PSSM
        # pass loses the WebGL2 context under a software-rasterized browser
        # (the only browser CI has; hardware browsers render the tour fine),
        # killing the tour at the first scene - bisected via these cvars, and
        # this test asserts embed sizing/clicks/tour completion, not shadow
        # content. Drop the override once the shadow pass is rasterizer-safe.
        cvars = urllib.parse.quote(
            "benchmark.sceneScale=0.05,benchmark.wipe=0,r.shadowQuality=off")
        cdp.call("Page.navigate", {"url":
                 "http://127.0.0.1:%d/index.html?env.ORKIGE_CVARS=%s"
                 % (port, cvars)})

        if not cdp.wait_console(r"director\[vista\]: 'Terrace Vista' ready",
                                180):
            fail("the vista scene never initialised in the browser", cdp)
        assert_hud_title(cdp, out)

        # the whole tour must complete before the tally card appears; hosted
        # CI Chrome renders WebGL through a software rasterizer several times
        # slower than local hardware, so this wall covers the worst measured
        # CI pace (the 180s local-derived budget starved there every run)
        btn = cdp.wait_console(r"director\[tally\]: restart button ready", 600)
        if not btn:
            fail("the results card never built the Restart button", cdp)
        cdp.wait_console(r"director\[tally\]: 'Tally' ready", 30)
        time.sleep(0.5)

        rect = re.search(r"rect=\((-?\d+),(-?\d+),(\d+),(\d+)\)", btn)
        bx, by, bw, bh = (int(v) for v in rect.groups())
        cxb, cyb = bx + bw / 2.0, by + bh / 2.0
        # the canvas' real displayed rect + backing size -> exact click mapping
        r = cdp.call("Runtime.evaluate", {"returnByValue": True, "expression":
            "(function(){var c=document.getElementById('canvas');"
            "var b=c.getBoundingClientRect();"
            "return [b.left,b.top,b.width,b.height,c.width,c.height];})()"
            })["result"]["value"]
        rl, rt, rw, rh, cw, ch = r
        # the button must be fully inside the displayed canvas (not cropped)
        if bx < 0 or by < 0 or bx + bw > cw or by + bh > ch:
            fail("the Restart button is not inside the render frame: "
                 "rect=(%d,%d,%d,%d) frame=%dx%d" % (bx, by, bw, bh, cw, ch))
        sx = rl + (cxb / cw) * rw
        sy = rt + (cyb / ch) * rh
        log("button backing centre=(%.0f,%.0f) -> screen (%.0f,%.0f); "
            "frame=%dx%d displayed=%.0fx%.0f" % (cxb, cyb, sx, sy, cw, ch,
                                                 rw, rh))

        before = len(cdp.console)
        cdp.call("Input.dispatchMouseEvent",
                 {"type": "mouseMoved", "x": sx, "y": sy})
        time.sleep(0.1)
        cdp.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": sx,
                 "y": sy, "button": "left", "buttons": 1, "clickCount": 1})
        cdp.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": sx,
                 "y": sy, "button": "left", "buttons": 0, "clickCount": 1})
        if not cdp.wait_console(r"director\[tally\]: restart -> loadLevel 0",
                                30, after=before):
            fail("the browser click did NOT restart the tour - the Restart "
                 "button is not clickable at the embed size (crop or missing "
                 "input events)", cdp)
        if not cdp.wait_console(r"director\[vista\]: 'Terrace Vista' ready",
                                180, after=before):
            fail("restart fired but the tour did not replay from scene 1", cdp)
        log("OK: HUD title visible + Restart button clickable at the embed "
            "size, tour replayed from scene 1")
    finally:
        proc.terminate()
        srv.shutdown()


if __name__ == "__main__":
    main()
