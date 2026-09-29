#!/usr/bin/env python3
"""Fake emblem for working on the phone UI without the board.

    python3 tools/mock_server.py      # then open http://localhost:8000

Serves web/index.html and implements the same /api endpoints as the firmware
(state is kept in memory only).
"""
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from embed_web import build_html  # noqa: E402

IMAGE_BYTES = 480 * 480 * 2
ANIM_MAX = int(os.environ.get("MOCK_ANIM_MAX", 8 * 1024 * 1024))
SAVE_DIR = os.environ.get("MOCK_SAVE_DIR")  # keep uploaded files here, if set
state = {
    "mode": 0, "brightness": 80, "speed": 30, "imageSlot": 0, "angle": 0,
    "quadA": "#2C8BD6", "quadB": "#FFFFFF", "ring": "#000000", "rim": "#B8BCC2", "label": "#FFFFFF",
    "labelText": "BMW", "spacing": 12,
    "stripe1": "#3FA9F5", "stripe2": "#1B3D8F", "stripe3": "#E22718", "stripeBg": "#16181B",
    "textBg": "#000000", "textFg": "#FFFFFF", "text": "M|POWER",
    "apSsid": "BMW-Emblem", "staSsid": "", "staIp": "", "slots": [0] * 10,
    "storage": "flash", "fsUsedKB": 0, "fsTotalKB": 12 * 1024, "animMax": ANIM_MAX,
    "panel": 0, "panels": ["18 MHz (recommended)", "30 MHz (Waveshare demo)", "12 MHz (lightest)"], "display": True,
    "startupAnim": 1, "powerMode": 0, "autoOffMin": 0, "showHours": 0, "showBrightness": 60,
    "lowVoltOn": 0, "cutoff": 12.0, "voltSource": 0, "volts": None, "offIn": -1, "lowFor": 0,
}
LIMITS = {"mode": (0, 4), "brightness": (5, 100), "speed": (-100, 100), "imageSlot": (0, 9),
          "angle": (-180, 180), "spacing": (0, 30), "startupAnim": (0, 1), "powerMode": (0, 1),
          "autoOffMin": (0, 720), "showHours": (0, 48), "showBrightness": (5, 100), "lowVoltOn": (0, 1),
          "voltSource": (0, 2)}
TEXT = {"labelText": 16, "text": 32}


class Handler(BaseHTTPRequestHandler):
    def reply(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def state(self):
        self.reply(200, json.dumps(state))

    def form(self):
        n = int(self.headers.get("Content-Length", 0))
        return {k: v[0] for k, v in parse_qs(self.rfile.read(n).decode(), keep_blank_values=True).items()}

    def multipart_file(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        boundary = self.headers["Content-Type"].split("boundary=")[-1].strip('"').encode()
        for part in body.split(b"--" + boundary):
            head, _, content = part.partition(b"\r\n\r\n")
            if b"filename=" in head:
                return content[:-2] if content.endswith(b"\r\n") else content
        return b""

    def store(self, slot, kind, data):
        if SAVE_DIR:
            with open(os.path.join(SAVE_DIR, "slot%d.%s" % (slot, "mjp" if kind == 2 else "bin")), "wb") as f:
                f.write(data)
        state["slots"][slot] = kind
        state["fsUsedKB"] += len(data) // 1024
        state["mode"], state["imageSlot"] = 3, slot
        self.state()

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/":
            self.reply(200, build_html(ROOT), "text/html")
        elif path == "/api/state":
            self.state()
        else:
            self.send_response(302)
            self.send_header("Location", "/")
            self.end_headers()

    def do_POST(self):
        url = urlparse(self.path)
        query = {k: v[0] for k, v in parse_qs(url.query).items()}
        if url.path == "/api/set":
            for k, v in self.form().items():
                if k in LIMITS:
                    lo, hi = LIMITS[k]
                    state[k] = max(lo, min(hi, int(v)))
                elif k == "cutoff":
                    state["cutoff"] = float(v)
                elif k in TEXT:
                    state[k] = v[:TEXT[k]]
                elif k in state and isinstance(state[k], str) and len(v) == 7 and v.startswith("#"):
                    state[k] = v.upper()
            # Fake readings so the power tab can be tried out.
            state["volts"] = {0: None, 1: 3.92, 2: 12.64}[state["voltSource"]]
            if state["powerMode"] == 1:
                state["offIn"] = state["showHours"] * 3600 if state["showHours"] else -1
            else:
                state["offIn"] = state["autoOffMin"] * 60 if state["autoOffMin"] else -1
            self.state()
        elif url.path == "/api/image":
            data = self.multipart_file()
            slot = int(query.get("slot", 0))
            if len(data) != IMAGE_BYTES or not 0 <= slot < 10:
                self.reply(400, "Expected a 480x480 RGB565 image (%d bytes)" % IMAGE_BYTES, "text/plain")
                return
            self.store(slot, 1, data)
        elif url.path == "/api/anim":
            data = self.multipart_file()
            slot = int(query.get("slot", 0))
            if len(data) > ANIM_MAX or data[:4] != b"EMJ1" or not 0 <= slot < 10:
                self.reply(400, "Not a valid animation", "text/plain")
                return
            self.store(slot, 2, data)
        elif url.path == "/api/image/delete":
            state["slots"][int(query.get("slot", 0))] = 0
            self.state()
        elif url.path == "/api/wifi":
            f = self.form()
            if len(f.get("apPass", "x" * 8)) not in range(8, 65) and f.get("apPass"):
                self.reply(400, "Wi-Fi password must be at least 8 characters", "text/plain")
                return
            state["apSsid"] = f.get("apSsid") or "BMW-Emblem"
            state["staSsid"] = f.get("staSsid", state["staSsid"])
            self.reply(200, "Saved. Restarting...", "text/plain")
        elif url.path == "/api/panel":
            state["panel"] = int(self.form().get("panel", 0))
            self.reply(200, "Restarting...", "text/plain")
        elif url.path in ("/api/reboot", "/update", "/api/test", "/api/power/off"):
            self.rfile.read(int(self.headers.get("Content-Length", 0)))
            self.reply(200, "Restarting...", "text/plain")
        else:
            self.reply(404, "not found", "text/plain")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    print("Mock emblem on http://localhost:%d" % port)
    ThreadingHTTPServer(("", port), Handler).serve_forever()
