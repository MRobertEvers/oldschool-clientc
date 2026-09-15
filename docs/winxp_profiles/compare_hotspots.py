#!/usr/bin/env python3
"""Diff two analyze_verysleepy.py hotspot tables.

    compare_hotspots.py BEFORE_hotspots.csv AFTER_hotspots.csv [TOP]

Both captures are fixed-duration (Very Sleepy /t 60), so their weighted totals
agree to a few milliseconds and exclusive SECONDS are directly comparable --
percentages are not, because a percentage of a fixed 60s only says how the same
60s was redistributed. A function that got faster and a function that got
called more both move the percentage; only the seconds say which.
"""

from __future__ import annotations

import collections
import csv
import sys
from pathlib import Path


def load(path: Path) -> tuple[dict[str, float], dict[str, float], float]:
    exclusive: dict[str, float] = collections.defaultdict(float)
    inclusive: dict[str, float] = collections.defaultdict(float)
    total = 0.0
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            key = row["module"] + "!" + row["function"]
            exclusive[key] += float(row["exclusive_seconds"])
            inclusive[key] = max(inclusive[key], float(row["inclusive_seconds"]))
            total += float(row["exclusive_seconds"])
    return exclusive, inclusive, total


def main() -> None:
    before_path, after_path = Path(sys.argv[1]), Path(sys.argv[2])
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 30

    before, before_incl, before_total = load(before_path)
    after, after_incl, after_total = load(after_path)

    print("exclusive-second totals: before %.3f  after %.3f" % (before_total, after_total))
    print()

    keys = set(before) | set(after)
    rows = [(after[k] - before[k], before[k], after[k], k) for k in keys]

    def table(title: str, ordered: list) -> None:
        print(title)
        print("%10s %10s %10s   %s" % ("before", "after", "delta", "function"))
        for delta, b, a, key in ordered[:top]:
            print("%10.3f %10.3f %+10.3f   %s" % (b, a, delta, key))
        print()

    table("=== biggest exclusive-time WINS (seconds) ===", sorted(rows))
    table("=== biggest exclusive-time REGRESSIONS (seconds) ===", sorted(rows, reverse=True))


if __name__ == "__main__":
    main()
