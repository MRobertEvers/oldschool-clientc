#!/usr/bin/env python3
"""Turn `cs2 roundtrip` output into a complete, stable TSV failure inventory."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


FAILURE = re.compile(r"^(DECOMPILE|COMPILE|DIFF) (\d+):\s*(.*)$")
SUMMARY = re.compile(
    r"^round-trip: (\d+)/(\d+) decompiled, (\d+) compiled, "
    r"(\d+) same-length, (\d+) exact$"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    args = parser.parse_args()

    rows: list[tuple[int, str, str]] = []
    summary = ""
    for raw in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = FAILURE.match(raw)
        if match:
            stage, script_id, detail = match.groups()
            rows.append((int(script_id), stage.lower(), detail.replace("\t", " ")))
        elif SUMMARY.match(raw):
            summary = raw

    if not summary:
        parser.error("input has no round-trip summary")

    print(f"# {summary}")
    print("script_id\tstage\tdetail")
    for script_id, stage, detail in sorted(rows):
        print(f"{script_id}\t{stage}\t{detail}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
