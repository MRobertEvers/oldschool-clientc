# -*- coding: utf-8 -*-
"""
Windows XP listener (Python 3.2+, stdlib only).
Subscribes + heartbeats to build host; serves POST /notify; runs package with a
max-runtime argument, captures all console output to ci_runner.log, uploads log + screenshot.
"""
from __future__ import print_function

import base64
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import tempfile
import zipfile

_DIR = os.path.dirname(os.path.abspath(__file__))
_TOOLS_CI = os.path.abspath(os.path.join(_DIR, ".."))
if _TOOLS_CI not in sys.path:
    sys.path.insert(0, _TOOLS_CI)

import env_load

env_load.load_dotenv(os.path.join(_DIR, ".env.xp_runner"))

import urllib.error
import urllib.parse
import urllib.request

from http.server import BaseHTTPRequestHandler, HTTPServer

# 1x1 transparent PNG (minimal)
_PLACEHOLDER_PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAFgwJ/lVGRVQAAAABJRU5ErkJggg=="
)


def _env(key, default):
    v = os.environ.get(key)
    if v is None:
        return default
    v = v.strip()
    return v if v else default


BUILD_HOST_BASE_URL = _env("BUILD_HOST_BASE_URL", "http://127.0.0.1:8765").rstrip("/")
LISTENER_ID = _env("LISTENER_ID", "xp1")
NOTIFY_HOST = _env("NOTIFY_HOST", "0.0.0.0")
NOTIFY_PORT = int(_env("NOTIFY_PORT", "8777"))
HEARTBEAT_INTERVAL_SEC = float(_env("HEARTBEAT_INTERVAL_SEC", "45"))
# Max seconds to run package run.bat (passed as %%1); process is killed after this.
RUN_TIMEOUT_SEC = float(_env("RUN_TIMEOUT_SEC", "300"))
# Log file name inside extract dir (stdout+stderr from run.bat and screenshot.cmd)
LOG_FILE_NAME = _env("LOG_FILE_NAME", "ci_runner.log")
# LAN IP or hostname the build host must use in POST /notify (not 0.0.0.0)
NOTIFY_PUBLIC_HOST = _env("NOTIFY_PUBLIC_HOST", "")


def _notify_url():
    pub = NOTIFY_PUBLIC_HOST.strip()
    host = pub if pub else NOTIFY_HOST
    if host in ("", "0.0.0.0", "::"):
        host = "127.0.0.1"
    return "http://{0}:{1}/notify".format(host, NOTIFY_PORT)


def _post_json(url, obj):
    data = json.dumps(obj).encode("utf-8")
    req = urllib.request.Request(url, data=data)
    req.add_header("Content-Type", "application/json; charset=utf-8")
    try:
        resp = urllib.request.urlopen(req, timeout=120)
        code = resp.getcode()
        resp.read()
        return code
    except urllib.error.HTTPError as e:
        try:
            e.read()
        except Exception:
            pass
        return e.code
    except Exception:
        return 0


def _http_get_bytes(url):
    req = urllib.request.Request(url)
    resp = urllib.request.urlopen(req, timeout=300)
    return resp.read()


def _post_bytes(url, body, content_type):
    req = urllib.request.Request(url, data=body)
    req.add_header("Content-Type", content_type)
    resp = urllib.request.urlopen(req, timeout=120)
    resp.read()


def _win_cmd_quote_path(path):
    """Double-quote a path for cmd.exe, escaping embedded quotes."""
    return '"' + path.replace('"', '""') + '"'


def _wait_proc(proc, timeout_sec):
    """Wait for proc up to timeout_sec; kill if still running. Returns 'timeout' or 'ok'."""
    start = time.time()
    while proc.poll() is None:
        if time.time() - start >= timeout_sec:
            try:
                proc.kill()
            except Exception:
                try:
                    proc.terminate()
                except Exception:
                    pass
            try:
                proc.wait()
            except Exception:
                pass
            return "timeout"
        time.sleep(0.15)
    try:
        proc.wait()
    except Exception:
        pass
    return "ok"


def _append_log_bytes(log_path, data):
    try:
        with open(log_path, "ab") as lf:
            lf.write(data)
    except Exception:
        pass


def _post_result(base, job_id, artifact, body, content_type):
    q = urllib.parse.urlencode({"listener_id": LISTENER_ID, "artifact": artifact})
    up_url = "{0}/v1/jobs/{1}/result?{2}".format(
        base, urllib.parse.quote(job_id, safe=""), q
    )
    print("upload {0} -> {1}".format(artifact, up_url))
    _post_bytes(up_url, body, content_type)


def do_subscribe():
    url = "{0}/v1/subscribe".format(BUILD_HOST_BASE_URL)
    body = {"id": LISTENER_ID, "url": _notify_url()}
    code = _post_json(url, body)
    print("subscribe -> HTTP {0}".format(code))
    return code == 200


def do_ping():
    url = "{0}/v1/ping".format(BUILD_HOST_BASE_URL)
    code = _post_json(url, {"id": LISTENER_ID})
    return code


def heartbeat_loop():
    while True:
        time.sleep(HEARTBEAT_INTERVAL_SEC)
        code = do_ping()
        if code == 404:
            print("ping 404, re-subscribe")
            do_subscribe()
        elif code != 200:
            print("ping failed: HTTP {0}".format(code))


def _run_job(payload):
    job_id = payload.get("job_id")
    git_hash = payload.get("git_hash")
    base = (payload.get("build_host_base_url") or BUILD_HOST_BASE_URL).rstrip("/")
    if not job_id:
        print("notify: missing job_id")
        return
    zip_url = "{0}/v1/jobs/{1}/payload".format(base, urllib.parse.quote(job_id, safe=""))
    print("fetch {0}".format(zip_url))
    try:
        zdata = _http_get_bytes(zip_url)
    except Exception as e:
        print("download failed: {0}".format(e))
        return
    tmp = tempfile.mkdtemp(prefix="ci_job_")
    zpath = os.path.join(tmp, "package.zip")
    try:
        with open(zpath, "wb") as zf:
            zf.write(zdata)
        extract_dir = os.path.join(tmp, "x")
        os.mkdir(extract_dir)
        with zipfile.ZipFile(zpath, "r") as zf:
            zf.extractall(extract_dir)
        runner = os.path.join(extract_dir, "run.bat")
        if not os.path.isfile(runner):
            runner = os.path.join(extract_dir, "run.cmd")
        if not os.path.isfile(runner):
            print("no run.bat or run.cmd in package")
            return
        log_path = os.path.join(extract_dir, LOG_FILE_NAME)
        timeout_int = int(max(1, RUN_TIMEOUT_SEC))
        header = (
            "---- ci_runner job_id=%s git_hash=%s listener=%s run_timeout_sec=%s ----\r\n"
            % (job_id, git_hash or "", LISTENER_ID, str(timeout_int))
        )
        try:
            with open(log_path, "wb") as hf:
                hf.write(header.encode("utf-8", "replace"))
        except Exception as e:
            print("log header failed: {0}".format(e))
        runner_abs = os.path.abspath(runner)
        log_abs = os.path.abspath(log_path)
        cmd_run = "cmd /c {0} {1} 1>>{2} 2>&1".format(
            _win_cmd_quote_path(runner_abs),
            str(timeout_int),
            _win_cmd_quote_path(log_abs),
        )
        try:
            proc = subprocess.Popen(cmd_run, cwd=extract_dir, shell=True)
        except Exception as e:
            _append_log_bytes(
                log_path,
                ("[ci_runner] Popen failed: %s\r\n" % (e,)).encode("utf-8", "replace"),
            )
            proc = None
        if proc is not None:
            why = _wait_proc(proc, float(RUN_TIMEOUT_SEC))
            if why == "timeout":
                _append_log_bytes(
                    log_path,
                    (
                        "\r\n[ci_runner] killed subprocess after RUN_TIMEOUT_SEC=%s\r\n"
                        % (str(int(RUN_TIMEOUT_SEC)),)
                    ).encode("utf-8", "replace"),
                )
        shot = os.path.join(_DIR, "screenshot.cmd")
        if os.path.isfile(shot):
            _append_log_bytes(log_path, b"\r\n---- screenshot.cmd (stdout/stderr) ----\r\n")
            shot_abs = os.path.abspath(shot)
            cmd_shot = "cmd /c {0} 1>>{1} 2>&1".format(
                _win_cmd_quote_path(shot_abs),
                _win_cmd_quote_path(log_abs),
            )
            try:
                pshot = subprocess.Popen(cmd_shot, cwd=extract_dir, shell=True)
                _wait_proc(pshot, 120.0)
            except Exception as e:
                _append_log_bytes(
                    log_path,
                    ("[ci_runner] screenshot.cmd failed: %s\r\n" % (e,)).encode("utf-8", "replace"),
                )
        try:
            with open(log_path, "rb") as rf:
                log_bytes = rf.read()
        except Exception:
            log_bytes = b"(ci_runner could not read log file)\r\n"
        png_path = os.path.join(extract_dir, "screenshot.png")
        if os.path.isfile(png_path):
            with open(png_path, "rb") as rf:
                png_bytes = rf.read()
        else:
            png_bytes = _PLACEHOLDER_PNG
        _post_result(base, job_id, "screenshot", png_bytes, "application/octet-stream")
        _post_result(base, job_id, "log", log_bytes, "text/plain; charset=utf-8")
        print("upload ok (screenshot + log)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


class NotifyHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("%s - - [%s] %s\n" % (self.address_string(), self.log_date_time_string(), fmt % args))

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path not in ("/notify", "/notify/"):
            self.send_response(404)
            self.end_headers()
            return
        try:
            length = int(self.headers.get("Content-Length", "0") or "0")
        except ValueError:
            length = 0
        body = self.rfile.read(length) if length > 0 else b""
        try:
            payload = json.loads(body.decode("utf-8"))
        except Exception:
            self.send_response(400)
            self.end_headers()
            return

        t = threading.Thread(target=_run_job, args=(payload,))
        t.daemon = True
        t.start()
        self.send_response(202)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        self.send_response(404)
        self.end_headers()


def main():
    if not do_subscribe():
        print("warning: initial subscribe did not return 200")
    hb = threading.Thread(target=heartbeat_loop)
    hb.daemon = True
    hb.start()
    bind_host = NOTIFY_HOST
    if not bind_host:
        bind_host = "0.0.0.0"
    print("notify server on http://{0}:{1}/notify".format(bind_host, NOTIFY_PORT))
    httpd = HTTPServer((bind_host, NOTIFY_PORT), NotifyHandler)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
