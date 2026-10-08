#!/usr/bin/env python3
"""Why did they die?  One line per death in a survey's ticklogs.

    python3 tools/raid_agent/deaths.py build/raid_agent/verzik/*.tsv

A death is the tick the raider row's hitpoints read 0 (the Theatre's
death line follows a few ticks later). Printed: the boss form at the time,
and every hit that raider took in the ten ticks before, as
damage@tick/<npc type or -1 for none>.
"""
import collections
import sys

def main(paths):
    causes = collections.Counter()
    for path in paths:
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0]
        hits = collections.defaultdict(list)
        form = {}
        dead = set()
        boss = "?"
        with open(path) as f:
            for line in f:
                c = line.rstrip("\n").split("\t")
                if len(c) < 10 or not c[1].isdigit():
                    continue
                tick, kind = int(c[1]), c[2]
                if kind == "npc_retype" and c[5] in ("8370", "8371", "8372", "8373", "8374"):
                    boss = {"8370": "P1", "8371": "T12", "8372": "P2", "8373": "T23", "8374": "P3"}[c[5]]
                elif kind == "hit_player" and int(c[5]) > 0:
                    hits[c[3]].append((tick, int(c[5]), c[8]))
                elif kind == "raider" and c[4] == "0" and c[3] not in dead:
                    dead.add(c[3])
                    recent = [h for h in hits[c[3]] if tick - 10 <= h[0] <= tick]
                    top = max(recent, key=lambda h: h[1]) if recent else None
                    cause = "%s npc%s" % (boss, top[2]) if top else "%s ?" % boss
                    causes[cause] += 1
                    print("%-6s p%s t%-5d %-4s %s" % (name, c[3], tick, boss,
                          " ".join("%d@%d/%s" % (h[1], h[0], h[2]) for h in recent)))
    print("\n".join("%3d  %s" % (n, k) for k, n in causes.most_common()))

if __name__ == "__main__":
    main(sys.argv[1:])
