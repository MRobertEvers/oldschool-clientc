#!/usr/bin/env python3
"""Our tick log's NPC_INFO for some npc types as the same compact timeline
npc_timeline.py prints for a recording: spawn (with the angle sent), moves,
seq, spotanim, facing (coord / entity / reset / angle), say, retype, the
npc_info rows (ops, level, transparency, exact move, timed bar), free.

    python3 tools/rsprox/ticklog_npc_timeline.py <ticklog.tsv> --ids 16652,16653 [--from T0 --to T1]
"""
import argparse

ap = argparse.ArgumentParser()
ap.add_argument("ticklog")
ap.add_argument("--ids", required=True)
ap.add_argument("--from", dest="t0", type=int, default=0)
ap.add_argument("--to", dest="t1", type=int, default=10 ** 9)
a = ap.parse_args()
ids = {int(x) for x in a.ids.split(",")}
slots = {}
tiles = {}
for line in open(a.ticklog, errors="replace"):
    f = line.rstrip("\n").split("\t")
    if len(f) < 9 or not f[1].isdigit():
        continue
    t, kind, v = int(f[1]), f[2], f[3:]
    label = f[9] if len(f) > 9 else ""
    if kind == "npc_spawn":
        if int(v[1]) in ids:
            slots[v[0]] = v[1]
            p = int(v[2])
            tiles[v[0]] = ((p >> 14) & 0x3FFF, p & 0x3FFF)
            if a.t0 <= t <= a.t1:
                print(f"[{t}] #{v[0]} add {v[1]} {tiles[v[0]][0]},{tiles[v[0]][1]} angle={v[3]}")
        else:
            slots.pop(v[0], None)
        continue
    if kind == "npc_retype" and int(v[2]) in ids:
        slots[v[0]] = v[2]
    if v[0] not in slots or not (a.t0 <= t <= a.t1):
        continue
    s = None
    if kind == "npc_tile":
        s = f"move -> {v[1]},{v[2]}"
    elif kind == "npc_anim":
        s = f"  seq {v[2]} delay={v[3]}"
    elif kind == "npc_spotanim":
        s = f"  spot {v[2]} slot={v[5]} delay={v[4]} height={v[3]}"
    elif kind == "npc_face":
        s = "  face " + ("reset" if v[2] == "-1" and v[3] == "-1" else ("player" if int(v[3]) >= 32768 else f"npc #{v[3]}") if v[2] == "-1" else f"angle={v[3]}" if v[2] == "-2" else f"coord={v[2]},{v[3]}")
    elif kind == "npc_say":
        s = f"  say {label}"
    elif kind == "npc_retype":
        s = f"  retype {v[1]} -> {v[2]}"
    elif kind == "npc_info":
        s = f"  {label} {' '.join(v[3:7])}"
    elif kind == "npc_free":
        s = "del"
        slots.pop(v[0], None)
    if s:
        print(f"[{t}] #{v[0]} {s}")
