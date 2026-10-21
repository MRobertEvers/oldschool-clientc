#!/usr/bin/env python3
"""One npc type's NPC_INFO as a compact timeline: movement (not idle), every
[sequence] / [spotanim] / [facing] (an entity face only when it changes) /
[say] / [transformation] / [exact_move] / [enabled_ops] / [level_change] /
[transparency], one line each.

    python3 tools/rsprox/npc_timeline.py <transcript> --match 'regex on npc name' [--from T0 --to T1]
        [--skip 'regex on the line']   (e.g. a raider's kit: thrall_magic_impact)
"""
import argparse
import re

ap = argparse.ArgumentParser()
ap.add_argument("transcript")
ap.add_argument("--match", required=True)
ap.add_argument("--from", dest="t0", type=int, default=0)
ap.add_argument("--to", dest="t1", type=int, default=10 ** 9)
ap.add_argument("--skip", default=r"thrall_magic_impact|vfx_glaive_01_impact|sanguinesti_staff_heal|xbows_(blood_sacrifice|life_leach)|\[spotanim\] id=-1, delay|\[headbar\]|\[hit\]")
a = ap.parse_args()
want = re.compile(a.match)
skip = re.compile(a.skip) if a.skip else None
TICK = re.compile(r"^\[(\d+)\]")
ENT = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\)(?:, multinpc=[^)]*\))?, coord=\((\d+), (\d+), \d+\)\)(.*)")
t = 0
cur = None
last_face = {}
for line in open(a.transcript, errors="replace"):
    m = TICK.match(line)
    if m:
        t = int(m.group(1))
        cur = None
        continue
    if t < a.t0 or t > a.t1:
        continue
    m = ENT.match(line)
    if m:
        cur = m.group(2) if want.search(m.group(3)) else None
        if cur and m.group(1) != "idle":
            nm = re.search(r"newcoord=\((\d+), (\d+)", m.group(7))
            sa = re.search(r"spawnangle=([a-z-]+)", m.group(7))
            extra = f"-> {nm.group(1)},{nm.group(2)}" if nm else (f"spawnangle={sa.group(1)}" if sa else "")
            print(f"[{t}] #{cur} {m.group(1)} {m.group(3)} {m.group(5)},{m.group(6)} {extra}")
        continue
    if re.match(r"^ {4,8}\[", line):
        cur = None
        continue
    if not cur:
        continue
    s = line.strip()
    if skip and skip.search(s):
        continue
    if s.startswith("[facing]"):
        v = re.sub(r"player=\(index=\d+, name=[^,]*, coord=\([^)]*\)\), fallbackangle=\d+", "player", s)
        v = re.sub(r"npc=\(index=\d+, id=([a-z0-9_]+) \(\d+\), coord=\([^)]*\)\), fallbackangle=\d+", r"npc \1", v)
        if v == last_face.get(cur) and ("player" in v or "npc " in v):
            continue
        last_face[cur] = v
        print(f"[{t}] #{cur}   {v[:100]}")
    elif s.startswith(("[sequence]", "[spotanim]", "[say]", "[transformation]", "[exact_move]", "[enabled_ops]", "[level_change]", "[transparency]")):
        print(f"[{t}] #{cur}   {s[:110]}")
