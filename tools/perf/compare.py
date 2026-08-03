#!/usr/bin/env python3
"""Diff two torirs_perf CSV reports and flag regressions.

Usage:
  tools/perf/compare.py before.csv after.csv [--threshold 0.05]
  tools/perf/compare.py --drift path.csv.windows.csv [--threshold 0.05]

Exit 0 if no stage p95 regresses by more than the threshold (default 5%),
or if the after frame p95 is under 20 ms and not worse than before by the
threshold. Exit 1 on regression. Prints a table either way.

`--drift` reads a windowed CSV (kind=window_stage) and fails when the last
steady-state window's frame p95 exceeds the first steady-state window's by
more than the threshold. Window 0 is skipped (boot skew).
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


def compare_pair(before: dict, after: dict, threshold: float, budget_ns: float) -> int:
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
            if name == "frame" and av > budget_ns:
                note = "OVER_BUDGET"
                regress = True
            elif delta > threshold and av > bv and bv > 0:
                note = "REGRESS"
                regress = True
            elif delta < -threshold:
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


def compare_drift(path: Path, threshold: float, budget_ns: float) -> int:
    """Fail when last steady window frame p95 exceeds first by threshold."""
    windows: dict[int, dict[str, float]] = {}
    gauges: dict[int, dict[str, float]] = {}
    with path.open() as f:
        for row in csv.DictReader(f):
            wraw = (row.get("window") or "").strip()
            if not wraw:
                continue
            w = int(float(wraw))
            if row["kind"] == "window_stage":
                windows.setdefault(w, {})
                p95 = (row.get("p95_ns") or "").strip()
                if p95:
                    windows[w][row["name"]] = float(p95)
            elif row["kind"] == "window_gauge":
                gauges.setdefault(w, {})
                total = (row.get("total") or "").strip()
                if total:
                    gauges[w][row["name"]] = float(total)

    ws = sorted(windows.keys())
    # Skip boot: window 0 is login skew; window 1 often still settling. Prefer
    # the first window whose frame p95 is within 2x of the late-run median.
    if len(ws) < 3:
        print(f"drift: need >=3 windows, got {len(ws)}", file=sys.stderr)
        return 1
    late_vals = [windows[w].get("frame", 0.0) for w in ws[-5:]]
    late_vals = [v for v in late_vals if v > 0]
    if not late_vals:
        print("drift: no frame p95 samples", file=sys.stderr)
        return 1
    late_med = sorted(late_vals)[len(late_vals) // 2]
    steady = [
        w
        for w in ws
        if w >= 2 and windows[w].get("frame", 0.0) > 0 and windows[w]["frame"] <= 2.0 * late_med
    ]
    if len(steady) < 2:
        steady = [w for w in ws if w >= 2]
    if len(steady) < 2:
        print(f"drift: need >=2 steady windows, got {len(steady)}", file=sys.stderr)
        return 1

    first_w, last_w = steady[0], steady[-1]
    bv = windows[first_w].get("frame", 0.0)
    av = windows[last_w].get("frame", 0.0)
    delta = (av - bv) / bv if bv else (1.0 if av else 0.0)

    print(f"drift file: {path}")
    print(f"windows: {len(ws)} total, steady first={first_w} last={last_w}")
    print(f"{'metric':<28} {'first':>10} {'last':>10} {'delta':>10} {'note'}")
    print("-" * 78)

    note = ""
    regress = False
    if av > budget_ns:
        note = "OVER_BUDGET"
        regress = True
    elif delta > threshold and av > bv and bv > 0:
        note = "DRIFT"
        regress = True
    elif delta < -threshold:
        note = "improve"
    print(
        f"{'window_stage:frame':<28} {fmt_ns(bv):>10} {fmt_ns(av):>10} "
        f"{delta * 100:>+8.1f}% {note}"
    )

    for stage in ("server", "render", "logic", "paint"):
        sb = windows[first_w].get(stage, 0.0)
        sa = windows[last_w].get(stage, 0.0)
        if sb == 0 and sa == 0:
            continue
        sd = (sa - sb) / sb if sb else (1.0 if sa else 0.0)
        print(
            f"{'window_stage:' + stage:<28} {fmt_ns(sb):>10} {fmt_ns(sa):>10} "
            f"{sd * 100:>+8.1f}%"
        )

    for gname in (
        "scene_elements",
        "scene_anim_list",
        "zone_map_count",
        "uitree_components",
        "cache_model_size",
        "npc_slot_max",
    ):
        gb = gauges.get(first_w, {}).get(gname)
        ga = gauges.get(last_w, {}).get(gname)
        if gb is None and ga is None:
            continue
        gb = gb or 0.0
        ga = ga or 0.0
        gd = (ga - gb) / gb if gb else (1.0 if ga else 0.0)
        print(f"{'window_gauge:' + gname:<28} {gb:>10.0f} {ga:>10.0f} {gd * 100:>+8.1f}%")

    if regress:
        print("\nDRIFT detected", file=sys.stderr)
        return 1
    print("\nOK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("before", nargs="?")
    ap.add_argument("after", nargs="?")
    ap.add_argument("--drift", metavar="WINDOWS_CSV")
    ap.add_argument("--threshold", type=float, default=0.05)
    ap.add_argument("--budget-ns", type=float, default=20_000_000.0)
    args = ap.parse_args()

    if args.drift:
        return compare_drift(Path(args.drift), args.threshold, args.budget_ns)

    if not args.before or not args.after:
        ap.error("before/after CSVs required unless --drift is set")

    before = load(Path(args.before))
    after = load(Path(args.after))
    return compare_pair(before, after, args.threshold, args.budget_ns)


if __name__ == "__main__":
    sys.exit(main())
