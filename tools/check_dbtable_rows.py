#!/usr/bin/env python3
"""Flag authored dbtables that have no rows, and say whether the cache has them.

Written after nearly generating 329 duplicate rows for `slayer_master_task`.

A `.dbtable` file in this tree is a SCHEMA OVERLAY: it names the columns so
ServerScript can address them. Whether the ROWS are authored here or shipped in
the dat2 cache is a separate question, and the file does not have to say. An
overlay with no matching `.dbrow` therefore means one of two very different
things:

  * the cache carries the rows and the overlay exists to read them -- nothing to
    do; or
  * the table is genuinely empty and every `db_find` against it returns nothing.

Reading "no authored rows" as the second is how a whole afternoon goes into
regenerating data the cache already had. This prints which is which.

Note the table NAME is the `[section]` header inside the file, not the
filename: `gem.dbtable` declares `gem_cutting_table`. Keying on the filename is
what made my first pass report 47 false positives.
"""
import argparse
import glob
import io
import os
import re
import sys

SCRIPTS = "OSRS-Content/osrs239-content/server/scripts"

# Tables that legitimately have no rows anywhere, with the reason. A NEW name
# appearing outside this list is the finding; these are known and documented at
# the point of use.
KNOWN_EMPTY = {
    "legends_gem_data":
        "Legends' Quest gem shrine is a documented soft-skip -- no carved-rock "
        "locs exist in all.loc, so the per-rock puzzle cannot be built yet. "
        "See quest_legends/scripts/legends_gem_shrine.rs2's header.",
}
CACHE_INDEX = "OSRS-Content/osrs239-content/configs/all.dbtable.compack"


def declared_tables():
    out = {}
    for path in glob.glob(SCRIPTS + "/**/*.dbtable", recursive=True):
        text = io.open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"^\[([a-z0-9_]+)\]", text, re.M):
            out.setdefault(m.group(1), []).append(path)
    return out


def tables_with_rows():
    out = {}
    for path in glob.glob(SCRIPTS + "/**/*.dbrow", recursive=True):
        text = io.open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"^table=([a-z0-9_]+)", text, re.M):
            out.setdefault(m.group(1), set()).add(path)
    return out


def cache_tables():
    names = set()
    if os.path.exists(CACHE_INDEX):
        for line in io.open(CACHE_INDEX, encoding="utf-8", errors="replace"):
            if "=" in line:
                names.add(line.strip().split("=", 1)[1])
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if a table has neither rows nor a "
                         "cache entry")
    args = ap.parse_args()

    declared = declared_tables()
    with_rows = tables_with_rows()
    cached = cache_tables()

    cache_backed = []
    orphan = []
    for name in sorted(declared):
        if name in with_rows:
            continue
        if name in cached:
            cache_backed.append(name)
        elif name not in KNOWN_EMPTY:
            orphan.append(name)

    print("check_dbtable_rows: %d declared table(s), %d with authored rows"
          % (len(declared), len(with_rows)))
    print("  cache-backed, no authored rows needed (%d): %s"
          % (len(cache_backed), " ".join(cache_backed) or "-"))
    print("  no rows anywhere, unexplained (%d): %s"
          % (len(orphan), " ".join(orphan) or "-"))
    print("  no rows anywhere, known and explained (%d): %s"
          % (len(KNOWN_EMPTY), " ".join(sorted(KNOWN_EMPTY)) or "-"))

    if args.check and orphan:
        print("check_dbtable_rows: %d table(s) declare columns but have no "
              "rows in the tree and none in the cache -- every db_find against "
              "them returns nothing" % len(orphan), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
