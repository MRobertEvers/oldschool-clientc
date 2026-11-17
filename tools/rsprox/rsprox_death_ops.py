"""rsprox_death_ops.py <transcript>... : when does a dying npc lose its menu?

For every npc whose headbar goes to fill=0 (the killing splat) and that later sends
`[enabled_ops] opflags=0b00000`, prints the offset (ops tick - kill tick) per npc type,
and the offset of its death seq (`[sequence]`) from the kill, per transcript.
"""
import os
import re
import sys
from collections import defaultdict, Counter

TICK = re.compile(r"^\[(\d+)\]")
NPC = re.compile(r"\[(?:add|walk|run|idle|teleport|crawl|telejump)\] npc=\(index=(\d+), id=([a-z0-9_]+)")
DEL = re.compile(r"\[del\] npc=\(index=(\d+)")
OTHER = re.compile(r"\] (?:player)=\(")


def run(path):
    out = defaultdict(Counter)
    t = 0
    cur = None
    killed = {}   # idx -> (tick, type)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TICK.match(line)
            if m:
                t = int(m.group(1))
                cur = None
                continue
            m = NPC.search(line)
            if m:
                cur = (m.group(1), m.group(2))
                continue
            m = DEL.search(line)
            if m:
                killed.pop(m.group(1), None)
                cur = None
                continue
            if OTHER.search(line):
                cur = None
                continue
            if cur is None:
                continue
            if "[headbar]" in line and "fill=0" in line and cur[0] not in killed:
                killed[cur[0]] = (t, cur[1])
            elif "[enabled_ops] opflags=0b00000" in line and cur[0] in killed:
                kt, typ = killed.pop(cur[0])
                out[typ][t - kt] += 1
    return out


def main():
    total = defaultdict(Counter)
    for path in sys.argv[1:]:
        res = run(path)
        agg = Counter()
        for typ, c in res.items():
            agg.update(c)
            total[typ].update(c)
        print(f"{os.path.basename(path)}  ops-0 at kill+n: {dict(sorted(agg.items()))}")
    for typ, c in sorted(total.items(), key=lambda kv: -sum(kv[1].values()))[:20]:
        print(f"  {typ:40s} {dict(sorted(c.items()))}")


if __name__ == "__main__":
    main()
