#!/usr/bin/env python3
"""POST /v1/build on the LAN CI build host (optional .env TRIGGER_BUILD_HOST)."""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

_CI_CLIENT = Path(__file__).resolve().parent
_TOOLS_CI = _CI_CLIENT.parent
if str(_TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(_TOOLS_CI))

from env_load import load_dotenv

load_dotenv(_CI_CLIENT / ".env.trigger")

DEFAULT_HOST = "http://127.0.0.1:8765"


def main() -> int:
    p = argparse.ArgumentParser(description="Trigger a remote WinXP CI build on the build host.")
    p.add_argument("--branch", default=None, help="Git branch or ref to build (default: HEAD)")
    p.add_argument(
        "--host",
        default=None,
        help="Override build host base URL (default: TRIGGER_BUILD_HOST or " + DEFAULT_HOST + ")",
    )
    args = p.parse_args()

    base = (args.host or os.environ.get("TRIGGER_BUILD_HOST") or DEFAULT_HOST).rstrip("/")
    url = f"{base}/v1/build"
    body: dict[str, object] = {}
    if args.branch:
        body["branch"] = args.branch
    data = json.dumps(body).encode("utf-8")
    req = Request(url, data=data)
    req.add_header("Content-Type", "application/json; charset=utf-8")
    try:
        resp = urlopen(req, timeout=3600)
        out = resp.read().decode("utf-8")
    except HTTPError as e:
        sys.stderr.write("HTTP %s: %s\n" % (e.code, e.read().decode("utf-8", "replace")))
        return 1
    except URLError as e:
        sys.stderr.write("Request failed: %s\n" % (e,))
        return 1
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
