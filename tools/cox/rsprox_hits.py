#!/usr/bin/env python3
"""Every hit the recorded raider took, matched to the tells that land with it,
from an RSProx transcript (tools/cox/rsprox_fetch.py + RSProx `transcribe`).

    python3 tools/cox/rsprox_hits.py ~/.rsprox/binary/archive/3389.txt --match olm
    python3 tools/cox/rsprox_hits.py <txt> --match olm --summary

A projectile (map_projanim_v2) lands at its tick + ceil(endtime / 30) (client
cycles); a map anim plays at its tick + delay / 30. A hit at tick T is matched
to every tell whose name holds --match and that lands at T-1..T. Coordinates
are not used: inside an instance the transcript's frames disagree.
Output per hit: tick, value, hitsplat id, protection prayers lit, the tells.
--summary: per tell set, the count and the values (landed hits), split by
whether the matching protection prayer was on.
"""
import argparse
import math
import re
import sys
from collections import defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
PLAYER = re.compile(r"player=\(index=(\d+), name=")
NPC = re.compile(r"npc=\(index=\d+, id=([a-z0-9_]+) \(\d+\)")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
PROJ = re.compile(r"\[map_projanim_v2\] id=([a-z0-9_]+) \(\d+\), starttime=(\d+), endtime=(\d+)")
MAPANIM = re.compile(r"\[map_anim\] id=([a-z0-9_]+) \(\d+\), delay=(\d+)")
SPOTANIM = re.compile(r"\[spotanim\] id=([a-z0-9_]+) \(\d+\)(?:, delay=(\d+))?")
PRAYER = re.compile(r"\[varbit\] id=prayer_protectfrom(melee|missiles|magic) \(\d+\), old=\d+, new=(\d+)")
ME = re.compile(r"local player index: (\d+)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("transcript")
    parser.add_argument("--match", default="")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    me = None
    tick = 0
    section = None
    actor = None
    prayers = {"melee": 0, "missiles": 0, "magic": 0}
    lands = defaultdict(list)    # tick -> [tell names]
    hits = []                    # (tick, value, splat, prayers)
    with open(args.transcript, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = ME.search(line)
            if m:
                me = m.group(1)
            m = TICK.match(line.strip())
            if m and line.startswith("["):
                tick = int(m.group(1))
                continue
            s = line.strip()
            if s.startswith("[player_info]"):
                section = "player"
            elif s.startswith("[npc_info"):
                section = "npc"
            m = PRAYER.search(line)
            if m:
                prayers[m.group(1)] = int(m.group(2))
            m = PROJ.search(line)
            if m and args.match in m.group(1):
                lands[tick + math.ceil(int(m.group(3)) / 30)].append(m.group(1))
            m = MAPANIM.search(line)
            if m and args.match in m.group(1):
                lands[tick + int(m.group(2)) // 30].append(m.group(1))
            m = SPOTANIM.search(line)
            if m and args.match in m.group(1):
                lands[tick + int(m.group(2) or 0) // 30].append(m.group(1) + "@" + (actor[0] if actor else "?"))
            m = PLAYER.search(line)
            if m and section == "player":
                actor = ("player", m.group(1))
                continue
            m = NPC.search(line)
            if m and section == "npc":
                actor = ("npc", m.group(1))
                continue
            m = HIT.search(line)
            if m and actor == ("player", me):
                hits.append((tick, int(m.group(2)), int(m.group(1)), dict(prayers)))
    groups = defaultdict(list)
    for t, value, splat, pr in hits:
        tells = sorted(set(lands.get(t, []) + lands.get(t - 1, [])))
        if args.match and not tells:
            continue
        lit = ",".join(k for k, v in pr.items() if v) or "-"
        if not args.summary:
            print("%6d %4d  splat %-3d  pray %-14s %s" % (t, value, splat, lit, " ".join(tells)))
        groups[(" ".join(tells), lit, splat)].append(value)
    if args.summary:
        for (tells, lit, splat), values in sorted(groups.items(), key=lambda kv: -len(kv[1])):
            landed = sorted(v for v in values if v > 0)
            print("%4d hits  splat %-3d pray %-14s max %3s  %-60s %s" % (len(values), splat, lit,
                  max(values), tells[:60], landed[-12:]))


if __name__ == "__main__":
    sys.exit(main())
