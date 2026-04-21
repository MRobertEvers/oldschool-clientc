#!/usr/bin/env python3
"""Scan build_release/out2.txt for lines from printf("a,w: %d, %d\\n", a, w); report max a, max w, max_a/max_w."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Same prefix as: printf("a,w: %d, %d\n", a, w);
PREFIX = "a,w: "


def parse_aw_line(line: str) -> tuple[int, int] | None:
    """
    Parse a line in the form ``a,w: <int>, <int>`` (optional spaces around comma,
    optional trailing whitespace). Returns ``(a, w)`` or ``None``.
    """
    s = line.rstrip("\r\n")
    if not s.startswith(PREFIX):
        return None
    i = len(PREFIX)
    n = len(s)

    def skip_spaces() -> None:
        nonlocal i
        while i < n and s[i].isspace():
            i += 1

    def read_int() -> int | None:
        nonlocal i
        skip_spaces()
        if i >= n:
            return None
        start = i
        if s[i] in "+-":
            i += 1
        if i >= n or not s[i].isdigit():
            return None
        while i < n and s[i].isdigit():
            i += 1
        return int(s[start:i])

    a = read_int()
    if a is None:
        return None
    skip_spaces()
    if i >= n or s[i] != ",":
        return None
    i += 1
    w = read_int()
    if w is None:
        return None
    skip_spaces()
    if i != n:
        return None
    return a, w


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        type=Path,
        default=Path("build_release/out2.txt"),
        help="Input file (default: build_release/out2.txt)",
    )
    args = parser.parse_args()
    path: Path = args.path

    if not path.is_file():
        print(f"error: not a file: {path}", file=sys.stderr)
        return 1

    max_a: int | None = None
    max_a_line: int | None = None
    max_w: int | None = None
    max_w_line: int | None = None

    max_aonw: int | None = None
    max_aonw_line: int | None = None

    with path.open(encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, start=1):
            parsed = parse_aw_line(line)
            if parsed is None:
                if line.lstrip().startswith("a,w:"):
                    print(
                        f"warning: line {line_no}: expected 'a,w: <int>, <int>': {line!r}",
                        file=sys.stderr,
                    )
                continue
            a, w = parsed
            if max_a is None or a > max_a:
                max_a = a
                max_a_line = line_no
            if max_w is None or w > max_w:
                max_w = w
                max_w_line = line_no

            aonw = a / w
            if max_aonw is None or aonw > max_aonw:
                max_aonw = aonw
                max_aonw_line = line_no

    if max_a is None or max_w is None:
        print(f"No lines matching {PREFIX!r}<int>, <int> found.")
        return 0

    print(f"largest w: {max_w} (line {max_w_line})")
    print(f"largest a: {max_a} (line {max_a_line})")
    print(f"largest a/w: {max_aonw:.6g} (line {max_aonw_line})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
