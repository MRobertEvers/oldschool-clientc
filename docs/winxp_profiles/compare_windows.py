#!/usr/bin/env python3
"""Diff the steady-state stage costs of two TORIRS_PERF window CSVs.

    compare_windows.py BEFORE.windows.csv AFTER.windows.csv [FIRST_STEADY_WINDOW]

Windows 0 and 1 are dropped by default. They are not steady state: with a
sparse cache hydrating over JS5 they carry the login, the world load, the
model and texture faults and the first paint, and on the XP box a single frame
in there legitimately runs for seconds. Averaging them in buries the frame loop
under boot cost -- the recorded baseline in analysis.md is windows 2-9 for the
same reason, so this holds that protocol.

Reported per stage: the mean over windows of each window's mean_ns, and the
same for p50. The mean is what regressions show up in; the p50 says whether a
change moved the typical frame or just the tail, and the two disagreeing is
itself the finding (a stage whose mean fell but whose p50 did not moved a
spike, not the frame loop).
"""

from __future__ import annotations

import collections
import csv
import sys
from pathlib import Path


def load(path: Path, first: int) -> tuple[dict[str, tuple[float, float]], list[int]]:
    means: dict[str, list[float]] = collections.defaultdict(list)
    p50s: dict[str, list[float]] = collections.defaultdict(list)
    windows: set[int] = set()
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["kind"] != "window_stage":
                continue
            window = int(row["window"])
            if window < first:
                continue
            windows.add(window)
            means[row["name"]].append(float(row["mean_ns"]) / 1e6)
            p50s[row["name"]].append(float(row["p50_ns"]) / 1e6)
    stats = {
        name: (
            sum(values) / len(values),
            sum(p50s[name]) / len(p50s[name]),
        )
        for name, values in means.items()
    }
    return stats, sorted(windows)


def main() -> None:
    before_path, after_path = Path(sys.argv[1]), Path(sys.argv[2])
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 2

    before, before_windows = load(before_path, first)
    after, after_windows = load(after_path, first)

    print("before: %s  windows %s" % (before_path.name, before_windows))
    print("after : %s  windows %s" % (after_path.name, after_windows))
    print()
    print(
        "%-14s %9s %9s %9s %8s   %9s %9s %9s"
        % ("stage", "mean_b", "mean_a", "delta", "pct", "p50_b", "p50_a", "delta")
    )

    rows = []
    for name in set(before) | set(after):
        mean_b, p50_b = before.get(name, (0.0, 0.0))
        mean_a, p50_a = after.get(name, (0.0, 0.0))
        rows.append((mean_b, name, mean_b, mean_a, p50_b, p50_a))
    rows.sort(reverse=True)

    for _sort, name, mean_b, mean_a, p50_b, p50_a in rows:
        pct = (100.0 * (mean_a - mean_b) / mean_b) if mean_b else float("nan")
        print(
            "%-14s %9.3f %9.3f %+9.3f %+7.1f%%   %9.3f %9.3f %+9.3f"
            % (name, mean_b, mean_a, mean_a - mean_b, pct, p50_b, p50_a, p50_a - p50_b)
        )


if __name__ == "__main__":
    main()
