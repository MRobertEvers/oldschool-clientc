#!/usr/bin/env python3
"""Npcs whose cache record states combat levels (stat1..6: attack, defence, strength, hitpoints,
ranged, magic) that `_explicit/configs/server_fields.generated.npc` overrides with a different
value while no other .npc block states that level.

That file froze, on 2026-10-11, what the server resolved for every unstated field: the engine
seeded attack/defence/strength 1, hitpoints 10, ranged/magic 0, and never read the cache's
levels. ToA's 74 npcs were corrected by hand on 2026-10-21 (docs/rsprox-analysis/toa/damage.md).

Usage: tools/check_npc_cache_levels.py [--list] [npc ...]
"""
import glob
import os
import re
import sys
from collections import defaultdict

CONTENT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "OSRS-Content", "osrs-content")
KEYS = ["attack", "defence", "strength", "hitpoints", "ranged", "magic"]


def main():
    cache = {}
    cur = None
    for line in open(os.path.join(CONTENT, "configs", "all.npc"), "rb"):
        line = line.decode("latin-1")
        m = re.match(r"^\[([^\]]+)\]", line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"^stat([1-6])=(\d+)", line)
        if m and cur:
            cache.setdefault(cur, {})[KEYS[int(m.group(1)) - 1]] = int(m.group(2))
    stated = defaultdict(lambda: defaultdict(list))
    for path in sorted(glob.glob(os.path.join(CONTENT, "server", "scripts", "**", "*.npc"), recursive=True)):
        cur = None
        for line in open(path, errors="replace"):
            m = re.match(r"^\[([^\]]+)\]", line)
            if m:
                cur = m.group(1)
                continue
            m = re.match(r"^(%s)=(\d+)" % "|".join(KEYS), line)
            if m and cur:
                stated[cur][m.group(1)].append((path, int(m.group(2))))
    bad = []
    for npc, levels in cache.items():
        for key, value in levels.items():
            src = stated.get(npc, {}).get(key, [])
            authored = [x for x in src if "server_fields.generated" not in x[0]]
            explicit = [x for x in src if "server_fields.generated" in x[0]]
            if not authored and explicit and explicit[0][1] != value:
                bad.append((npc, key, value, explicit[0][1]))
    names = set(a for a in sys.argv[1:] if not a.startswith("--"))
    print("npcs with an overridden cache level: %d (levels: %d)" % (len({b[0] for b in bad}), len(bad)))
    for npc, key, value, ours in bad:
        if "--list" in sys.argv or npc in names:
            print("%s %s cache=%d explicit=%d" % (npc, key, value, ours))
    return 0


if __name__ == "__main__":
    sys.exit(main())
