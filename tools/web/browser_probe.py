#!/usr/bin/env python3
"""Drive the web client in headless Chrome and watch it boot.

    python3 tools/web/browser_probe.py URL [--seconds 60] [--every 2] [--memtrace out.bin]

Every sample prints the wasm heap size, the page's status bar (the JS5 group
counter lives there) and the last client-log line, so "the game hangs after
login" becomes a number in a minute: a heap that jumps after the JS5 counter
stops moving is decode or a leak, a JS5 counter that keeps climbing one at a
time is a serial read chain. With --memtrace, and a page built from
`make -C src web MEMTRACE=1`, the trace is flushed after the last sample and
pulled out of the page's filesystem; feed it to tools/memtrace/summarize.py.

No Playwright, no npm: Chrome's own DevTools protocol over a raw WebSocket.
That is deliberate -- the one shared browser the MCP tooling drives was held
by another session for a whole afternoon, and this needs nothing but the
Chrome that is installed. It found, on 2026-09-17, the 621 decodes of the
models reference table that were 1.06 GB of the browser's heap.

The URL is the page as the launcher prints it, plus login args if you want to
get past the title screen:
    http://localhost:8088/?args=--manifest,build/manifests/osrs239-web.ini,--user,probe,--pass,x
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

CHROME_CANDIDATES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
]

# What one sample evaluates in the page. Plain DOM reads: the status bar, the
# client log's last interesting line, and the wasm heap's byte length.
SAMPLE_JS = r"""(function () {
  var st = document.querySelector('#status, .status, header');
  var lines = document.body.innerText.split('\n').filter(function (l) {
    return /^(torirs|app|preload|world|scene|rebuild|login|net|renderer|Failed|Abort)/.test(l);
  });
  return JSON.stringify({
    heap: (window.Module && Module.HEAPU8) ? Module.HEAPU8.length : 0,
    status: st ? st.innerText.replace(/\n/g, ' · ').slice(0, 200) : '',
    last: lines.slice(-1)[0] || ''
  });
})()"""

FLUSH_JS = r"""(function () {
  Module._torirs_memtrace_web_flush();
  var a = Module._torirs_memtrace_web_path(); var p = '';
  while (Module.HEAPU8[a]) p += String.fromCharCode(Module.HEAPU8[a++]);
  window.__mt = Module.FS.readFile(p);
  return p + ' ' + window.__mt.length;
})()"""


class Cdp:
    """The smallest DevTools client that works: one page, request/response."""

    def __init__(self, ws_url: str):
        host, port, path = re.match(r"ws://([^:/]+):(\d+)(/.*)", ws_url).groups()
        self.sock = socket.create_connection((host, int(port)), timeout=60)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n".encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.sock.recv(4096)
        self.buf = buf.split(b"\r\n\r\n", 1)[1]
        self.next_id = 0

    def _send(self, obj) -> None:
        payload = json.dumps(obj).encode()
        mask = os.urandom(4)
        head = bytes([0x81])
        if len(payload) < 126:
            head += bytes([0x80 | len(payload)])
        elif len(payload) < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", len(payload))
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", len(payload))
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def _frame(self):
        while True:
            if len(self.buf) >= 2:
                ln = self.buf[1] & 0x7F
                off = 2
                if ln == 126:
                    ln = struct.unpack(">H", self.buf[2:4])[0]
                    off = 4
                elif ln == 127:
                    ln = struct.unpack(">Q", self.buf[2:10])[0]
                    off = 10
                if len(self.buf) >= off + ln:
                    payload = self.buf[off:off + ln]
                    self.buf = self.buf[off + ln:]
                    return json.loads(payload.decode("utf-8", "replace"))
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                raise EOFError("DevTools socket closed")
            self.buf += chunk

    def call(self, method: str, params=None):
        self.next_id += 1
        self._send({"id": self.next_id, "method": method, "params": params or {}})
        while True:
            msg = self._frame()
            if msg.get("id") == self.next_id:
                return msg.get("result", {})

    def evaluate(self, expression: str) -> str:
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True})
        return r.get("result", {}).get("value", "")


def find_chrome() -> str:
    for c in CHROME_CANDIDATES:
        if os.path.exists(c):
            return c
    found = shutil.which("google-chrome") or shutil.which("chromium")
    if not found:
        sys.exit("browser_probe: no Chrome or Chromium found")
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("url")
    ap.add_argument("--seconds", type=int, default=60, help="how long to watch (default 60)")
    ap.add_argument("--every", type=float, default=2.0, help="sample interval in seconds")
    ap.add_argument("--port", type=int, default=9333, help="DevTools port")
    ap.add_argument("--memtrace", help="after the last sample, flush the memtrace and save it here")
    ap.add_argument("--headed", action="store_true", help="show the window (default headless)")
    ap.add_argument("--log", help="after the last sample, save the page's client log (the CLIENT pane) here")
    ap.add_argument("--shots", help="save a PNG of the page at every sample into this directory")
    args = ap.parse_args()

    profile = tempfile.mkdtemp(prefix="torirs-probe-")
    argv = [find_chrome(), "--no-first-run", "--disable-gpu", f"--user-data-dir={profile}",
            f"--remote-debugging-port={args.port}", "--window-size=1280,860", "about:blank"]
    if not args.headed:
        argv.insert(1, "--headless=new")
    chrome = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        pages = None
        for _ in range(60):
            try:
                pages = json.load(urllib.request.urlopen(f"http://127.0.0.1:{args.port}/json"))
                break
            except Exception:
                time.sleep(0.25)
        if not pages:
            sys.exit("browser_probe: Chrome did not open its DevTools port")
        cdp = Cdp([p for p in pages if p["type"] == "page"][0]["webSocketDebuggerUrl"])
        cdp.call("Page.enable")
        cdp.call("Page.navigate", {"url": args.url})
        t0 = time.time()
        while time.time() - t0 < args.seconds:
            time.sleep(args.every)
            try:
                d = json.loads(cdp.evaluate(SAMPLE_JS) or "{}")
            except Exception:
                d = {}
            print(f"t+{int(time.time() - t0):4}s  heap {d.get('heap', 0) // 1048576:5} MB  "
                  f"{d.get('status', '')[:90]}  |  {d.get('last', '')[:80]}", flush=True)
            if args.shots:
                os.makedirs(args.shots, exist_ok=True)
                try:
                    shot = cdp.call("Page.captureScreenshot", {"format": "png"})
                    with open(os.path.join(args.shots, f"t{int(time.time() - t0):04}.png"), "wb") as out:
                        out.write(base64.b64decode(shot["data"]))
                except Exception as err:
                    print(f"shot failed: {err}", flush=True)
        if args.log:
            text = cdp.evaluate("(document.getElementById('log') || {}).textContent || ''")
            trace = cdp.evaluate("(globalThis.__torirs_io_trace || []).join('\\n')")
            with open(args.log, "w") as out:
                out.write(text)
                if trace:
                    out.write("\n=== io trace (TORIRS_IO_TRACE=1) ===\n" + trace + "\n")
            print(f"log: saved {text.count(chr(10))} log lines and {trace.count(chr(10))} trace lines to {args.log}", flush=True)
        if args.memtrace:
            info = cdp.evaluate(FLUSH_JS)
            total = int(info.split()[-1])
            print(f"memtrace: {info}", flush=True)
            chunk = 8 * 1024 * 1024
            with open(args.memtrace, "wb") as out:
                for off in range(0, total, chunk):
                    end = min(off + chunk, total)
                    b64 = cdp.evaluate(
                        f"(function(){{var s=window.__mt.subarray({off},{end});var b='';"
                        f"for(var i=0;i<s.length;i+=32768)b+=String.fromCharCode.apply(null,s.subarray(i,i+32768));"
                        f"return btoa(b);}})()")
                    out.write(base64.b64decode(b64))
            print(f"memtrace: saved {total} bytes to {args.memtrace}; "
                  f"next: python3 tools/memtrace/summarize.py {args.memtrace}", flush=True)
    finally:
        chrome.terminate()
        shutil.rmtree(profile, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
