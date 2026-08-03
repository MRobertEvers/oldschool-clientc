#!/usr/bin/env python3
"""Diff two torirs_perf CSV reports and flag regressions.

Usage:
  tools/perf/compare.py before.csv after.csv [--threshold 0.05]

Exit 0 if no stage p95 regresses by more than the threshold (default 5%),
or if the after frame p95 is under 20 ms and not worse than before by the
threshold. Exit 1 on regression. Prints a table either way.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


def load(path: Path) -> dict[str, dict[str, float]]:
    rows: dict[str, dict[str, float]] = {}
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            kind = row["kind"]
            name = row["name"]
            key = f"{kind}:{name}"
            entry: dict[str, float] = {}
            for col in ("mean_ns", "p50_ns", "p95_ns", "max_ns", "total", "per_frame"):
                raw = (row.get(col) or "").strip()
                if raw:
                    entry[col] = float(raw)
            rows[key] = entry
    return rows


def fmt_ns(ns: float) -> str:
    if ns >= 1e6:
        return f"{ns / 1e6:.2f}ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f}us"
    return f"{ns:.0f}ns"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--threshold", type=float, default=0.05)
    ap.add_argument("--budget-ns", type=float, default=20_000_000.0)
    args = ap.parse_args()

    before = load(Path(args.before))
    after = load(Path(args.after))

    print(f"{'metric':<28} {'before':>10} {'after':>10} {'delta':>10} {'note'}")
    print("-" * 78)

    regress = False
    keys = sorted(set(before) | set(after))
    for key in keys:
        b = before.get(key, {})
        a = after.get(key, {})
        kind, name = key.split(":", 1)
        if kind == "stage":
            bv = b.get("p95_ns", 0.0)
            av = a.get("p95_ns", 0.0)
            if bv == 0 and av == 0:
                continue
            delta = (av - bv) / bv if bv else (1.0 if av else 0.0)
            note = ""
            if name == "frame" and av > args.budget_ns:
                note = "OVER_BUDGET"
                regress = True
            elif delta > args.threshold and av > bv and bv > 0:
                note = "REGRESS"
                regress = True
            elif delta < -args.threshold:
                note = "improve"
            print(
                f"{key:<28} {fmt_ns(bv):>10} {fmt_ns(av):>10} "
                f"{delta * 100:>+8.1f}% {note}"
            )
        elif kind == "meta":
            bv = b.get("total", 0.0)
            av = a.get("total", 0.0)
            print(f"{key:<28} {bv:>10.3f} {av:>10.3f}")
        elif kind == "counter":
            bv = b.get("per_frame", 0.0)
            av = a.get("per_frame", 0.0)
            if bv == 0 and av == 0:
                continue
            # Only print counters that moved meaningfully.
            if abs(av - bv) < 0.01 and (bv + av) < 1.0:
                continue
            delta = (av - bv) / bv if bv else (1.0 if av else 0.0)
            print(
                f"{key:<28} {bv:>10.2f} {av:>10.2f} {delta * 100:>+8.1f}%"
            )

    if regress:
        print("\nREGRESSION detected", file=sys.stderr)
        return 1
    print("\nOK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
