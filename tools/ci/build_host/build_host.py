#!/usr/bin/env python3
"""
LAN-only CI build host: subscribers (TTL + ping), git checkout + stub zip,
fan-out notify to XP runners, artifact + screenshot + log storage, HTML dashboard.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlparse
from urllib.request import Request, urlopen

_TOOLS_CI = Path(__file__).resolve().parent.parent
if str(_TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(_TOOLS_CI))
from env_load import load_dotenv

_CI_PKG = Path(__file__).resolve().parent
load_dotenv(_CI_PKG / ".env.build_host")

# Repo root: .../tools/ci/build_host/build_host.py -> parents[3]
_DEFAULT_REPO = Path(__file__).resolve().parents[3]
# Default full WinXP build: .cursor/plans/winxp_sse2_package_build_5c026799.plan.md (MINGW64 + bash)
_DEFAULT_WINXP_SH = _DEFAULT_REPO / "tools" / "ci" / "build_host" / "build_winxp_package.sh"


def _env_path(key: str, default: Path) -> Path:
    v = os.environ.get(key, "").strip()
    return Path(v).resolve() if v else default.resolve()


LISTEN_HOST = (os.environ.get("LISTEN_HOST", "0.0.0.0") or "0.0.0.0").strip()
LISTEN_PORT = int(os.environ.get("LISTEN_PORT", "8765"))
REPO_ROOT = _env_path("REPO_ROOT", _DEFAULT_REPO)
ARTIFACTS_ROOT = _env_path("ARTIFACTS_ROOT", _CI_PKG / "artifacts")
RESULTS_ROOT = _env_path("RESULTS_ROOT", _CI_PKG / "results")
SUBSCRIBER_TTL_SEC = float(os.environ.get("SUBSCRIBER_TTL_SEC", "180"))
BUILD_HOST_PUBLIC_BASE = (os.environ.get("BUILD_HOST_PUBLIC_BASE", "") or "").rstrip("/")

if sys.platform == "win32":
    _DEFAULT_STUB = _DEFAULT_WINXP_SH if _DEFAULT_WINXP_SH.is_file() else (_CI_PKG / "build_stub.bat")
else:
    _DEFAULT_STUB = _CI_PKG / "build_stub.py"
BUILD_STUB_CMD = (os.environ.get("BUILD_STUB_CMD", "") or "").strip() or str(_DEFAULT_STUB)


class State:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.subscribers: dict[str, dict] = {}
        self.jobs: dict[str, dict] = {}


STATE = State()


def _now() -> float:
    return time.time()


def prune_and_list_subscribers() -> list[dict]:
    """Remove stale subscribers; return remaining with alive=true."""
    now = _now()
    rows: list[dict] = []
    with STATE.lock:
        dead = [
            sid
            for sid, rec in STATE.subscribers.items()
            if (now - rec["last_seen"]) > SUBSCRIBER_TTL_SEC
        ]
        for sid in dead:
            del STATE.subscribers[sid]
        for sid, rec in STATE.subscribers.items():
            rows.append(
                {
                    "id": sid,
                    "notify_url": rec["url"],
                    "last_seen": rec["last_seen"],
                    "alive": True,
                }
            )
    return rows


def alive_subscribers() -> list[tuple[str, str]]:
    """Prune stale; return [(id, url), ...] for fan-out."""
    now = _now()
    out: list[tuple[str, str]] = []
    with STATE.lock:
        dead = [
            sid
            for sid, rec in STATE.subscribers.items()
            if (now - rec["last_seen"]) > SUBSCRIBER_TTL_SEC
        ]
        for sid in dead:
            del STATE.subscribers[sid]
        for sid, rec in STATE.subscribers.items():
            out.append((sid, rec["url"]))
    return out


def _read_json_body(handler: BaseHTTPRequestHandler) -> dict:
    n = int(handler.headers.get("Content-Length", "0") or "0")
    if n <= 0:
        return {}
    raw = handler.rfile.read(n)
    if not raw:
        return {}
    return json.loads(raw.decode("utf-8"))


def _send_json(handler: BaseHTTPRequestHandler, code: int, obj: object) -> None:
    data = json.dumps(obj).encode("utf-8")
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(data)))
    handler.end_headers()
    handler.wfile.write(data)


def _send_bytes(handler: BaseHTTPRequestHandler, code: int, data: bytes, content_type: str) -> None:
    handler.send_response(code)
    handler.send_header("Content-Type", content_type)
    handler.send_header("Content-Length", str(len(data)))
    handler.end_headers()
    handler.wfile.write(data)


def _public_base_url() -> str:
    if BUILD_HOST_PUBLIC_BASE:
        return BUILD_HOST_PUBLIC_BASE
    if LISTEN_HOST in ("0.0.0.0", "", "::"):
        return f"http://127.0.0.1:{LISTEN_PORT}"
    return f"http://{LISTEN_HOST}:{LISTEN_PORT}"


def _run_git(args: list[str]) -> None:
    subprocess.run(
        ["git", *args],
        cwd=str(REPO_ROOT),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def _run_build(branch: str) -> tuple[str, str]:
    _run_git(["fetch", "--all", "--prune"])
    _run_git(["checkout", branch])
    rev = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=str(REPO_ROOT),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    git_hash = rev.stdout.strip()
    if not git_hash:
        raise RuntimeError("empty git hash")

    ARTIFACTS_ROOT.mkdir(parents=True, exist_ok=True)
    build_dir = ARTIFACTS_ROOT / git_hash
    build_dir.mkdir(parents=True, exist_ok=True)
    out_zip = build_dir / "package.zip"
    if out_zip.is_file():
        out_zip.unlink()

    env = os.environ.copy()
    env["CI_REPO_ROOT"] = str(REPO_ROOT)
    env["CI_OUT_ZIP"] = str(out_zip)

    raw = Path(BUILD_STUB_CMD)
    stub = raw.resolve() if raw.is_absolute() else (REPO_ROOT / raw).resolve()
    if not stub.is_file():
        raise RuntimeError(f"build stub not found: {stub}")
    if stub.suffix.lower() == ".py":
        subprocess.run(
            [sys.executable, str(stub)],
            cwd=str(REPO_ROOT),
            env=env,
            check=True,
        )
    elif stub.suffix.lower() == ".sh":
        bash_exe = os.environ.get("BASH_EXE", "bash")
        subprocess.run(
            [bash_exe, str(stub)],
            cwd=str(REPO_ROOT),
            env=env,
            check=True,
        )
    else:
        subprocess.run(
            [str(stub)],
            cwd=str(REPO_ROOT),
            env=env,
            check=True,
            shell=True,
        )

    if not out_zip.is_file():
        raise RuntimeError(f"stub did not produce package.zip: {out_zip}")

    job_id = uuid.uuid4().hex
    with STATE.lock:
        STATE.jobs[job_id] = {"git_hash": git_hash, "zip_path": str(out_zip)}

    base = _public_base_url()
    _notify_subscribers(job_id, git_hash, base)
    return job_id, git_hash


def _notify_subscribers(job_id: str, git_hash: str, public_base: str) -> None:
    payload = json.dumps(
        {
            "job_id": job_id,
            "git_hash": git_hash,
            "build_host_base_url": public_base,
        }
    ).encode("utf-8")
    for _sid, url in alive_subscribers():
        req = Request(url, data=payload)
        req.add_header("Content-Type", "application/json; charset=utf-8")
        try:
            urlopen(req, timeout=60)
        except (OSError, HTTPError, URLError) as e:
            sys.stderr.write("notify %s failed: %s\n" % (url, str(e)))


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stderr.write("%s - - [%s] %s\n" % (self.address_string(), self.log_date_time_string(), fmt % args))

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path or "/"
        if path in ("/", ""):
            dash = _CI_PKG / "dashboard.html"
            if not dash.is_file():
                self.send_error(404, "dashboard missing")
                return
            _send_bytes(self, 200, dash.read_bytes(), "text/html; charset=utf-8")
            return
        if path == "/v1/subscribers":
            rows = prune_and_list_subscribers()
            _send_json(self, 200, rows)
            return
        m = re.match(r"^/v1/jobs/([^/]+)/payload$", path)
        if m:
            job_id = m.group(1)
            with STATE.lock:
                job = STATE.jobs.get(job_id)
            if not job:
                self.send_error(404, "job")
                return
            zp = Path(job["zip_path"])
            if not zp.is_file():
                self.send_error(404, "zip")
                return
            _send_bytes(self, 200, zp.read_bytes(), "application/zip")
            return
        self.send_error(404)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path or "/"
        if path == "/v1/subscribe":
            body = _read_json_body(self)
            sid = body.get("id")
            url = body.get("url")
            if not sid or not url:
                _send_json(self, 400, {"error": "id and url required"})
                return
            with STATE.lock:
                STATE.subscribers[sid] = {"url": url, "last_seen": _now()}
            _send_json(self, 200, {"ok": True})
            return
        if path == "/v1/ping":
            body = _read_json_body(self)
            sid = body.get("id")
            if not sid:
                _send_json(self, 400, {"error": "id required"})
                return
            with STATE.lock:
                if sid not in STATE.subscribers:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                STATE.subscribers[sid]["last_seen"] = _now()
            _send_json(self, 200, {"ok": True})
            return
        if path == "/v1/build":
            body = _read_json_body(self)
            branch = body.get("branch") or "HEAD"
            try:
                job_id, git_hash = _run_build(branch)
            except subprocess.CalledProcessError as e:
                out = e.stdout if isinstance(e.stdout, str) else (e.stdout or b"").decode("utf-8", "replace")
                _send_json(self, 500, {"error": "git/build failed", "detail": out})
                return
            except Exception as e:
                _send_json(self, 500, {"error": str(e)})
                return
            _send_json(self, 200, {"job_id": job_id, "git_hash": git_hash})
            return
        m = re.match(r"^/v1/jobs/([^/]+)/result$", path)
        if m:
            job_id = m.group(1)
            qs = parse_qs(parsed.query or "")
            lid = (qs.get("listener_id") or [None])[0]
            if not lid:
                _send_json(self, 400, {"error": "listener_id required"})
                return
            artifact = (qs.get("artifact") or ["screenshot"])[0] or "screenshot"
            if artifact not in ("screenshot", "log"):
                _send_json(self, 400, {"error": "artifact must be screenshot or log"})
                return
            with STATE.lock:
                job = STATE.jobs.get(job_id)
            if not job:
                _send_json(self, 404, {"error": "job"})
                return
            n = int(self.headers.get("Content-Length", "0") or "0")
            raw = self.rfile.read(n) if n > 0 else b""
            gh = job["git_hash"]
            out_dir = RESULTS_ROOT / gh
            out_dir.mkdir(parents=True, exist_ok=True)
            if artifact == "log":
                out_path = out_dir / f"{lid}.log"
            else:
                out_path = out_dir / f"{lid}.png"
            out_path.write_bytes(raw)
            _send_json(self, 200, {"ok": True, "path": str(out_path), "artifact": artifact})
            return
        self.send_error(404)


def main() -> None:
    ARTIFACTS_ROOT.mkdir(parents=True, exist_ok=True)
    RESULTS_ROOT.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer((LISTEN_HOST, LISTEN_PORT), Handler)
    print(f"build_host listening on http://{LISTEN_HOST}:{LISTEN_PORT}")
    print(f"REPO_ROOT={REPO_ROOT}")
    print(f"public base for XP: {_public_base_url()}")
    server.serve_forever()


if __name__ == "__main__":
    main()
