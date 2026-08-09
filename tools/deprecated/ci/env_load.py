# -*- coding: utf-8 -*-
"""Minimal .env loader (Python 3.2+). Sets os.environ from KEY=value lines."""

import os


def load_dotenv(path):
    """Load KEY=value pairs from path into os.environ. Missing file is OK."""
    if path is None:
        return
    path = os.path.abspath(path)
    if not os.path.isfile(path):
        return
    with open(path, "r") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                continue
            key, val = line.split("=", 1)
            key = key.strip()
            val = val.strip()
            if len(val) >= 2 and val[0] == val[-1] and val[0] in ("'", '"'):
                val = val[1:-1]
            if key:
                os.environ[key] = val
