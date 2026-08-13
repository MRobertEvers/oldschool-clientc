#!/usr/bin/env python3
"""
wiki_npc_roster — which npcs actually stand in the rev-239 world and can fight.

    tools/wiki_npc_roster.py --write     # OSRS-Content/osrs239-content/wiki/npc_roster.csv
    tools/wiki_npc_roster.py             # same report, stdout only

See docs/NPC_WIKI_STATS_PLAN.md §1. This is the roster the rest of the wiki
pipeline (`wiki_fetch.py`, `wiki_infobox.py`, `gen_npc_stats.py`) works from, so
it is written to disk rather than recomputed by each of them: the join has to
be the *same* 1,700 ids every time or the validation counts in the plan stop
meaning anything.

## What counts as "spawned"

The first column of every `==== NPC ====` row in every `*.spawn` file under
`server/scripts` — the same files `tools/gen_spawns.py` writes and
`docs/ITEM_AND_NPCS.md` describes. 5,941 distinct gameval symbols, last
measured 2026-08-12.

## What counts as "attackable"

Two cache facts, both from the npc's own dat2 record:
  - one of its five right-click ops is literally "Attack"
  - its `combat_level` is greater than 0

Both come from `tools/dump_stats`, which this script shells out to build and
run if `--npc-csv` is not given an existing file. `combat_level` alone is not
enough — plenty of non-combat npcs the wiki still describes (bankers with a
level for display) would slip through on that alone; the Attack op is the
gate the client itself uses to draw the option.
"""

from __future__ import annotations

import argparse
import csv
import glob
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
CACHE = os.path.join(REPO, "cache.osrs239")
DUMP_STATS_BIN = os.path.join(REPO, "tools", "dump_stats", "dump_stats")
OUT_CSV = os.path.join(CONTENT, "wiki", "npc_roster.csv")

NPC_HEADER_RE = re.compile(r"^====\s*NPC\s*====\s*$")


def spawned_names() -> set[str]:
    names = set()
    for path in glob.glob(os.path.join(CONTENT, "server", "scripts", "**", "*.spawn"), recursive=True):
        in_npc = False
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                stripped = line.strip()
                if not stripped or stripped.startswith("//"):
                    continue
                if stripped.startswith("===="):
                    in_npc = bool(NPC_HEADER_RE.match(stripped))
                    continue
                if in_npc:
                    names.add(stripped.split()[0])
    return names


def load_name_to_id() -> dict[str, int]:
    out = {}
    with open(os.path.join(CONTENT, "configs", "all.npc.compack"), encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("//") or "=" not in line:
                continue
            id_str, name = line.split("=", 1)
            out[name] = int(id_str)
    return out


def ensure_npc_csv(npc_csv: str) -> str:
    if npc_csv and os.path.exists(npc_csv):
        return npc_csv
    if not os.path.exists(DUMP_STATS_BIN):
        subprocess.run(["make", "-C", os.path.join(REPO, "tools", "dump_stats")], check=True)
    dest = npc_csv or os.path.join(REPO, "out", "wiki", "npc_stats.csv")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    subprocess.run(
        [DUMP_STATS_BIN, "--rev", "osrs239", CACHE, "--npc-only", "--npc-csv", dest],
        check=True,
        cwd=REPO,
    )
    return dest


def load_cache_rows(npc_csv: str) -> dict[int, dict]:
    with open(npc_csv, encoding="latin-1", newline="") as f:
        return {int(row["id"]): row for row in csv.DictReader(f)}


def is_attackable(row: dict) -> bool:
    ops = [row.get(f"op{i}", "") or "" for i in range(1, 6)]
    has_attack = any(op.strip().lower() == "attack" for op in ops)
    combat_level = int(row.get("combat_level") or 0)
    return has_attack and combat_level > 0


def build_roster(npc_csv: str | None) -> list[dict]:
    resolved_csv = ensure_npc_csv(npc_csv)
    name_to_id = load_name_to_id()
    cache_rows = load_cache_rows(resolved_csv)
    spawned = spawned_names()

    resolved = 0
    rows = []
    for gameval in sorted(spawned):
        npc_id = name_to_id.get(gameval)
        if npc_id is None:
            continue
        resolved += 1
        row = cache_rows.get(npc_id)
        if row is None or not is_attackable(row):
            continue
        rows.append(
            {
                "id": npc_id,
                "gameval": gameval,
                "display_name": row["name"],
                "combat_level": row["combat_level"],
                "size": row["size"],
            }
        )

    print(
        f"spawned symbols: {len(spawned)}; resolved to a cache id: {resolved}; "
        f"attackable: {len(rows)}",
        file=sys.stderr,
    )
    return sorted(rows, key=lambda r: -int(r["combat_level"]))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--npc-csv", help="reuse an existing dump_stats --npc-csv output instead of rebuilding one")
    ap.add_argument("--write", action="store_true", help=f"write {OUT_CSV}")
    args = ap.parse_args()

    rows = build_roster(args.npc_csv)

    if args.write:
        os.makedirs(os.path.dirname(OUT_CSV), exist_ok=True)
        with open(OUT_CSV, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=["id", "gameval", "display_name", "combat_level", "size"])
            w.writeheader()
            w.writerows(rows)
        print(f"wrote {len(rows)} rows -> {OUT_CSV}", file=sys.stderr)
    else:
        w = csv.DictWriter(sys.stdout, fieldnames=["id", "gameval", "display_name", "combat_level", "size"])
        w.writeheader()
        w.writerows(rows)


if __name__ == "__main__":
    main()
