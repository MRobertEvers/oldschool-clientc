#!/usr/bin/env python3
"""Olm's area hits against distance, from RSProx transcripts (tools/cox/rsprox_fetch.py
+ RSProx `transcribe`): every hit the recorded raider took on the tick a crystal
bomb blew (map anim olm_crystal_bomb_explode, 40) or a falling crystal landed
(olm_crystal_explode, 1358), with the Chebyshev distance from the raider's tile to
each impact on that tick (or the one before).

    python3 tools/cox/rsprox_olm.py ~/.rsprox/binary/archive/{3389,3282,843,3285}.txt

The Olm chamber is not turned, so the raider's tile and the map anims share a
frame there (checked: the raider stands beside the hands' tiles). A map anim's
delay (client cycles, 30 a tick) moves it to the tick it plays.
"""
import re
import sys
from collections import defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
PLAYER = re.compile(r"player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), \d+\)\)(?:, newcoord=\((\d+), (\d+), \d+\))?")
NPC = re.compile(r"npc=\(index=\d+, id=")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
MAPANIM = re.compile(r"\[map_anim\] id=(olm_crystal_bomb_explode|olm_crystal_explode) \(\d+\)(.*?)coord=\((\d+), (\d+), \d+\)")
DELAY = re.compile(r"delay=(\d+)")


def scan(path):
    me, tick, section, actor = None, 0, None, None
    pos = {}
    impacts = defaultdict(list)       # tick -> [(kind, x, z)]
    hits = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = ME.search(line)
            if m:
                me = m.group(1)
            s = line.strip()
            m = TICK.match(s)
            if m and line.startswith("["):
                tick = int(m.group(1))
                continue
            if s.startswith("[player_info]"):
                section = "player"
            elif s.startswith("[npc_info"):
                section = "npc"
            m = MAPANIM.search(line)
            if m:
                d = DELAY.search(m.group(2))
                impacts[tick + (int(d.group(1)) // 30 if d else 0)].append((m.group(1), int(m.group(3)), int(m.group(4))))
            m = PLAYER.search(line)
            if m and section == "player":
                actor = m.group(1)
                if actor == me:
                    x = int(m.group(4) or m.group(2))
                    z = int(m.group(5) or m.group(3))
                    pos[tick] = (x, z)
                continue
            if NPC.search(line) and section == "npc":
                actor = None
                continue
            m = HIT.search(line)
            if m and actor == me and section == "player":
                hits.append((tick, int(m.group(2)), int(m.group(1))))
    return pos, impacts, hits


def where(pos, tick):
    best = None
    for t in sorted(pos):
        if t > tick:
            break
        best = pos[t]
    return best


def main(paths):
    table = defaultdict(list)          # (kind, distance) -> values
    for path in paths:
        pos, impacts, hits = scan(path)
        for tick, value, splat in hits:
            if splat == 65:            # poison/venom splat: not an impact
                continue
            me = where(pos, tick - 1)
            if me is None:
                continue
            for t in (tick, tick - 1):
                for kind, x, z in impacts.get(t, []):
                    d = max(abs(x - me[0]), abs(z - me[1]))
                    if d <= 4:
                        table[(kind, d)].append(value)
    for (kind, d), values in sorted(table.items()):
        print("%-26s dist %d  n=%3d  max %3d  values %s" % (kind, d, len(values), max(values), sorted(values)))


if __name__ == "__main__":
    main(sys.argv[1:])
