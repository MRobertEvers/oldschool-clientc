#!/usr/bin/env python3
"""Convert a Very Sleepy 0.7 archive's text payload into reviewable tables."""

from __future__ import annotations

import collections
import csv
import json
import re
from pathlib import Path


HERE = Path(__file__).resolve().parent
RAW = HERE / "winxp-soft3d-60s-raw"
SYMBOL_RE = re.compile(
    r'^(sym\d+)\s+"([^"]*)"\s+"([^"]*)"\s+"([^"]*)"\s+(\d+)$'
)
IP_RE = re.compile(r'^(\S+)\s+([0-9.]+)\s+"([^"]*)"\s+(\d+)$')


def load_symbols() -> dict[str, tuple[str, str, str, int]]:
    symbols = {}
    for line in (RAW / "Symbols.txt").read_text(errors="replace").splitlines():
        match = SYMBOL_RE.match(line)
        if match:
            key, module, function, source, line_number = match.groups()
            symbols[key] = (module, function, source, int(line_number))
    return symbols


def symbol_label(value: tuple[str, str, str, int]) -> str:
    module, function, _source, _line = value
    return (module + "!" + function).replace(";", ":")


def main() -> None:
    symbols = load_symbols()
    inclusive = collections.defaultdict(float)
    exclusive = collections.defaultdict(float)
    folded = collections.defaultdict(float)
    stack_rows = []
    total = 0.0

    for raw_line in (RAW / "Callstacks.txt").read_text().splitlines():
        fields = raw_line.split()
        if len(fields) < 2:
            continue
        seconds = float(fields[0])
        stack = [key for key in fields[1:] if key in symbols]
        if not stack:
            continue
        total += seconds
        exclusive[stack[0]] += seconds
        for key in set(stack):
            inclusive[key] += seconds
        root_to_leaf = list(reversed(stack))
        folded[";".join(symbol_label(symbols[key]) for key in root_to_leaf)] += seconds
        stack_rows.append((seconds, " <- ".join(symbol_label(symbols[key]) for key in stack)))

    all_keys = set(inclusive) | set(exclusive)
    hotspot_rows = []
    for key in all_keys:
        module, function, source, line_number = symbols[key]
        hotspot_rows.append(
            (
                module,
                function,
                source,
                line_number,
                exclusive[key],
                100.0 * exclusive[key] / total,
                inclusive[key],
                100.0 * inclusive[key] / total,
            )
        )
    hotspot_rows.sort(key=lambda row: (row[6], row[4]), reverse=True)
    with (HERE / "verysleepy_hotspots.csv").open("w", newline="", encoding="utf-8") as out:
        writer = csv.writer(out)
        writer.writerow(
            [
                "module",
                "function",
                "source",
                "line",
                "exclusive_seconds",
                "exclusive_percent",
                "inclusive_seconds",
                "inclusive_percent",
            ]
        )
        for row in hotspot_rows:
            writer.writerow((*row[:4], *(f"{value:.6f}" for value in row[4:])))

    with (HERE / "verysleepy_top_stacks.csv").open("w", newline="", encoding="utf-8") as out:
        writer = csv.writer(out)
        writer.writerow(["seconds", "percent", "leaf_to_root"])
        for seconds, stack in sorted(stack_rows, reverse=True):
            writer.writerow([f"{seconds:.6f}", f"{100.0 * seconds / total:.6f}", stack])

    with (HERE / "verysleepy_stacks.folded").open("w", encoding="utf-8", newline="\n") as out:
        for stack, seconds in sorted(folded.items(), key=lambda item: item[1], reverse=True):
            out.write("%s %d\n" % (stack, max(1, round(seconds * 1_000_000))))

    source_counts = collections.defaultdict(float)
    ip_lines = (RAW / "IPCounts.txt").read_text(errors="replace").splitlines()[1:]
    for raw_line in ip_lines:
        match = IP_RE.match(raw_line)
        if not match:
            continue
        _address, seconds, source, line_number = match.groups()
        source_counts[(source, int(line_number))] += float(seconds)
    with (HERE / "verysleepy_source_hotspots.csv").open(
        "w", newline="", encoding="utf-8"
    ) as out:
        writer = csv.writer(out)
        writer.writerow(["source", "line", "exclusive_seconds", "exclusive_percent"])
        for (source, line_number), seconds in sorted(
            source_counts.items(), key=lambda item: item[1], reverse=True
        ):
            writer.writerow(
                [source, line_number, f"{seconds:.6f}", f"{100.0 * seconds / total:.6f}"]
            )

    stats = {
        "weighted_seconds": total,
        "symbol_count": len(symbols),
        "stack_record_count": len(stack_rows),
        "archive_stats": (RAW / "Stats.txt").read_text(errors="replace").splitlines(),
    }
    (HERE / "verysleepy_summary.json").write_text(
        json.dumps(stats, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(stats, indent=2))


if __name__ == "__main__":
    main()
