"""The open-defect gate.

A defect that has been seen and written down but not fixed used to live in
prose -- a paragraph in a pull request, a line in a chat log -- where nothing
could fail because of it.  Twice in a row that let "I looked at every plugin"
end as a catalogue instead of as repairs, which from the outside is
indistinguishable from never having looked.

So the catalogue is a file now, and writing a row into it turns this gate red.
The only ways back to green are to fix the defect or to have the owner accept
it in writing.  Nobody has to remember to re-read anything.

    python3 defect_gate.py            # fails while any row is open
    python3 defect_gate.py --summary  # counts per status, always exits 0
"""

import argparse
import collections
import os
import sys

LEDGER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "DEFECTS.tsv")

COLUMNS = ("id", "plugin", "lane", "status", "evidence", "note")

# open     -- seen, reproduced, not repaired.  This is what turns the gate red.
# fixed    -- repaired; `evidence` names the capture that shows it repaired.
# refuted  -- re-measured and the original report was wrong; `note` says how.
# accepted -- the owner has said in writing to ship it as it is.
OPEN_STATUSES = ("open",)
CLOSED_STATUSES = ("fixed", "refuted", "accepted")
STATUSES = OPEN_STATUSES + CLOSED_STATUSES


def load(path=LEDGER):
    """Read the ledger.  A malformed row is itself a failure, never a skip."""
    assert os.path.exists(path), path
    rows = []
    with open(path, "r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != len(COLUMNS):
                raise SystemExit(
                    "%s:%d: %d columns, want %d -- %r"
                    % (path, number, len(fields), len(COLUMNS), line)
                )
            row = dict(zip(COLUMNS, fields))
            if row["status"] not in STATUSES:
                raise SystemExit(
                    "%s:%d: status %r is not one of %s"
                    % (path, number, row["status"], ", ".join(STATUSES))
                )
            # A closed row has to point at what closed it, or "closed" is just
            # a word somebody typed.
            if row["status"] in CLOSED_STATUSES and not row["evidence"].strip():
                raise SystemExit(
                    "%s:%d: %s with no evidence -- name the capture, the commit, "
                    "or the message that closed it" % (path, number, row["status"])
                )
            row["line"] = number
            rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--ledger", default=LEDGER)
    args = parser.parse_args()

    rows = load(args.ledger)
    counts = collections.Counter(row["status"] for row in rows)

    if args.summary:
        for status in STATUSES:
            print("%-9s %d" % (status, counts[status]))
        print("%-9s %d" % ("total", len(rows)))
        return 0

    open_rows = [row for row in rows if row["status"] in OPEN_STATUSES]
    if not open_rows:
        print(
            "defect gate: PASS -- %d rows, %d fixed, %d refuted, %d accepted"
            % (len(rows), counts["fixed"], counts["refuted"], counts["accepted"])
        )
        return 0

    print("defect gate: FAIL -- %d defects are open" % len(open_rows))
    print()
    width = max(len(row["id"]) for row in open_rows)
    for row in open_rows:
        print(
            "  %-*s  %-20s %-12s %s"
            % (width, row["id"], row["plugin"], row["lane"], row["note"])
        )
    print()
    print("Fix them, or have the owner accept them in writing and say so in the row.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
