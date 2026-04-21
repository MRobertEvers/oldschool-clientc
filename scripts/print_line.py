#!/usr/bin/env python3
"""Print one 1-based line number from a text file (stdout, no added newline beyond file's)."""

from __future__ import annotations

import argparse
import sys
from itertools import islice
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="File to read")
    parser.add_argument(
        "line",
        type=int,
        help="1-based line number",
    )
    args = parser.parse_args()

    if args.line < 1:
        print("error: line must be >= 1", file=sys.stderr)
        return 1

    path: Path = args.path
    if not path.is_file():
        print(f"error: not a file: {path}", file=sys.stderr)
        return 1

    with path.open(encoding="utf-8", errors="replace") as f:
        chunk = list(islice(f, args.line - 1, args.line))

    if not chunk:
        print(f"error: line {args.line} is past end of file", file=sys.stderr)
        return 1

    sys.stdout.write(chunk[0])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
