#!/usr/bin/env python3
"""Parse a quest-driver ledger.tsv (header `quest-ledger-v1`).

Written by src/plugin/torirs_plugin_drive.c (drive_ledger_write /
drive_ledger_write_summary): one row per step (index, step, verdict, ticks,
shots, detail), then a trailing SUMMARY row
(SUMMARY, index, PASS|FAIL, total_ticks, exit=<code>, "pass=<n> fail=<m>").
"""

import os

HEADER = "quest-ledger-v1"


def read(path):
    """(rows, summary). rows is None (not []) when the file does not exist
    at all, so a caller can tell "no ledger" apart from "an empty one"."""
    assert path
    if not os.path.isfile(path):
        return None, None
    rows = []
    summary = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line or line == HEADER or line.startswith("index\t"):
                continue
            fields = line.split("\t")
            if fields[0] == "SUMMARY":
                summary = fields
                continue
            while len(fields) < 6:
                fields.append("")
            rows.append({
                "index": fields[0], "step": fields[1], "verdict": fields[2],
                "ticks": fields[3], "shots": fields[4], "detail": fields[5],
            })
    return rows, summary


def shot_names(row):
    """The (unsuffixed) shot names a row's `shots` column lists, e.g.
    "01-cook-quest-offer" out of "01-cook-quest-offer,02-cook-quest-drain"."""
    raw = row.get("shots", "")
    return [name for name in raw.split(",") if name]
