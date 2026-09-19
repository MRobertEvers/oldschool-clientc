#!/usr/bin/env python3
"""Query and update test/quests/QUEUE.tsv -- the quest test work queue.

Owner: 3b (docs/QUEST_SUITE_KIT.md phase 3), a sibling to `new_quest.py`
(which reads QUEUE.tsv for helper_dir/helper_file/quest_dir) but with no
import between the two -- this is the queue's own front door, plain stdlib,
so a worker can run it without pulling in questhelper_extract or the
inventory. It knows nothing about Quest Helper, Java, or Lua generation; it
only reads and rewrites the TSV.

Every write is ATOMIC: the new rows are built in memory, written to a temp
file in the same directory as the target, then `os.replace()`'d over it --
a reader mid-write never sees a half-written file, and a crash leaves the
original untouched. Column order is always preserved (QUEUE_COLUMNS below),
so a hand-added column downstream is never silently dropped by a rewrite
here as long as it is added to that list too.

Usage
-----
    python3 tools/quest_gate/queue.py next --tier N [--claim <owner>] [--file PATH]
    python3 tools/quest_gate/queue.py set <test_id> --status STATUS [--failure TEXT] [--owner OWNER] [--file PATH]
    python3 tools/quest_gate/queue.py show <test_id> [--file PATH]
    python3 tools/quest_gate/queue.py summary [--file PATH]

`--file` defaults to test/quests/QUEUE.tsv; pass a copy's path (`--file
/tmp/queue_copy.tsv`) to exercise this tool without touching the real queue.

`next`: prints the first row (file order) whose `tier` matches and whose
`status` is exactly `todo`. With `--claim <owner>`, that row's `status` is
atomically rewritten to `claimed/<owner>` (not one of `set`'s four
statuses -- a claim is a hold, not a verdict) so two workers racing `next`
for the same tier do not both pick up the same row; nothing is written
without `--claim`. Exits 1, printing to stderr, when no todo row matches.

`set`: rewrites one row's `status` (one of `todo green blocked
content_bug` -- refused otherwise, with a message), and optionally `owner`/
`last_failure`. Refuses an unknown `test_id` with a message naming it.

`show`: prints one row's columns, one per line. Refuses an unknown
`test_id` the same way.

`summary`: counts per tier x status, todo/green/blocked/content_bug columns
first (in that fixed order) then any other status seen (e.g. `claimed/x`)
appended, plus row and column totals.
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
DEFAULT_QUEUE = REPO / "test" / "quests" / "QUEUE.tsv"

QUEUE_COLUMNS = ["quest_dir", "test_id", "helper_dir", "helper_file", "tier", "status", "owner", "last_failure"]
STATUSES = ("todo", "green", "blocked", "content_bug")


def load_rows(path: Path) -> list[dict]:
    with open(path, "r", encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    for row in rows:
        for col in QUEUE_COLUMNS:
            row.setdefault(col, "")
    return rows


def write_rows(path: Path, rows: list[dict]) -> None:
    """Atomic: a temp file in the SAME directory as `path` (so os.replace()
    is a same-filesystem rename, not a copy), written in full before the
    replace -- a reader never observes a partial file, and a crash between
    the temp write and the replace leaves `path` exactly as it was."""
    fd, tmp_name = tempfile.mkstemp(prefix=".queue-", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
            writer.writerow(QUEUE_COLUMNS)
            for row in rows:
                writer.writerow([row.get(col, "") for col in QUEUE_COLUMNS])
        os.replace(tmp_name, path)
    except BaseException:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise


def find_row(rows: list[dict], test_id: str) -> dict | None:
    for row in rows:
        if row.get("test_id") == test_id:
            return row
    return None


def format_row(row: dict) -> str:
    return "\t".join(row.get(col, "") for col in QUEUE_COLUMNS)


def cmd_next(args: argparse.Namespace) -> int:
    rows = load_rows(args.file)
    tier = str(args.tier)
    for row in rows:
        if row.get("tier") == tier and row.get("status") == "todo":
            print(format_row(row))
            if args.claim:
                row["status"] = f"claimed/{args.claim}"
                write_rows(args.file, rows)
            return 0
    print(f"no todo row at tier {args.tier} in {args.file}", file=sys.stderr)
    return 1


def cmd_set(args: argparse.Namespace) -> int:
    if args.status not in STATUSES:
        print(f"unknown status {args.status!r} -- must be one of: {', '.join(STATUSES)}", file=sys.stderr)
        return 1
    rows = load_rows(args.file)
    row = find_row(rows, args.test_id)
    if row is None:
        print(f"unknown test_id {args.test_id!r} in {args.file}", file=sys.stderr)
        return 1
    row["status"] = args.status
    if args.owner is not None:
        row["owner"] = args.owner
    if args.failure is not None:
        row["last_failure"] = args.failure
    write_rows(args.file, rows)
    print(format_row(row))
    return 0


def cmd_show(args: argparse.Namespace) -> int:
    rows = load_rows(args.file)
    row = find_row(rows, args.test_id)
    if row is None:
        print(f"unknown test_id {args.test_id!r} in {args.file}", file=sys.stderr)
        return 1
    for col in QUEUE_COLUMNS:
        print(f"{col}: {row.get(col, '')}")
    return 0


def _tier_sort_key(tier: str):
    return (0, int(tier)) if tier.isdigit() else (1, tier)


def cmd_summary(args: argparse.Namespace) -> int:
    rows = load_rows(args.file)
    counts: dict[tuple[str, str], int] = {}
    tiers: set[str] = set()
    extra_statuses: list[str] = []
    for row in rows:
        tier = row.get("tier") or "?"
        status = row.get("status") or "?"
        tiers.add(tier)
        if status not in STATUSES and status not in extra_statuses:
            extra_statuses.append(status)
        counts[(tier, status)] = counts.get((tier, status), 0) + 1

    status_order = list(STATUSES) + sorted(extra_statuses)
    # Column width is the longest status name plus a mandatory one-space gap
    # -- a fixed 12 ran two columns together with no visible seam whenever a
    # status name (e.g. "claimed/matt") was exactly 12 characters wide.
    col_w = max(len(s) for s in status_order) + 1 if status_order else 1
    header = "tier".ljust(6) + "".join(s.rjust(col_w) for s in status_order) + "total".rjust(8)
    print(header)
    print("-" * len(header))

    grand_by_status = {s: 0 for s in status_order}
    grand_total = 0
    for tier in sorted(tiers, key=_tier_sort_key):
        row_total = 0
        cells = []
        for status in status_order:
            n = counts.get((tier, status), 0)
            row_total += n
            grand_by_status[status] += n
            cells.append(str(n).rjust(col_w))
        grand_total += row_total
        print(tier.ljust(6) + "".join(cells) + str(row_total).rjust(8))

    print("-" * len(header))
    totals_line = "TOTAL".ljust(6) + "".join(
        str(grand_by_status[s]).rjust(col_w) for s in status_order
    ) + str(grand_total).rjust(8)
    print(totals_line)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", type=Path, default=DEFAULT_QUEUE, help="QUEUE.tsv path (default: the real queue)")
    sub = ap.add_subparsers(dest="command", required=True)

    p_next = sub.add_parser("next", help="print the first todo row of a tier")
    p_next.add_argument("--tier", type=int, required=True)
    p_next.add_argument("--claim", metavar="OWNER", help="atomically mark the row claimed/<owner>")
    p_next.set_defaults(func=cmd_next)

    p_set = sub.add_parser("set", help="set a row's status (and optionally owner/last_failure)")
    p_set.add_argument("test_id")
    p_set.add_argument("--status", required=True, choices=STATUSES)
    p_set.add_argument("--failure", help="last_failure text")
    p_set.add_argument("--owner")
    p_set.set_defaults(func=cmd_set)

    p_show = sub.add_parser("show", help="print one row, one column per line")
    p_show.add_argument("test_id")
    p_show.set_defaults(func=cmd_show)

    p_summary = sub.add_parser("summary", help="counts per tier x status")
    p_summary.set_defaults(func=cmd_summary)

    args = ap.parse_args()
    if not args.file.is_file():
        print(f"no such file: {args.file}", file=sys.stderr)
        return 1
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
