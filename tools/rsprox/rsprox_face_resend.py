"""rsprox_face_resend.py <transcript>... : does an npc facing the recorder re-send that face
when the angle to him turns?

For every npc whose latched face is the recorder, each tick after its first face: the angle
from the npc's centre (south-west tile + size*64, after its step this tick) to the recorder's
tile as it stood the tick before (npcs move before players), the engine's npc_face_follow
rule. Counts, per npc type:
    turned+sent   the angle changed and a face block naming the recorder went out
    turned-quiet  the angle changed and nothing went out
    same+sent     a face block went out with the angle unchanged
A face is latched until a reset, a face on something else, or the npc's [del].
Reads `[facing] player=` (rev 238+) and `[face_pathingentity] player=` (older).
"""
import math
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
NPC_CFG = os.path.join(HERE, "..", "..", "OSRS-Content", "osrs-content", "configs", "all.npc")

SIZE = {}
cur = None
with open(NPC_CFG, encoding="latin-1") as f:
    for line in f:
        if line.startswith("["):
            cur = line.strip()[1:-1]
        elif line.startswith("size=") and cur:
            v = line.strip()[5:]
            if v.isdigit():
                SIZE[cur] = int(v)

TICK = re.compile(r"^\[(\d+)\]")
PLAYER = re.compile(r"\[(?:run|walk|idle|teleport|crawl)\] player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
NPC = re.compile(r"\[(add|walk|run|idle|teleport|crawl|telejump|del)\] npc=\(index=(\d+), id=([a-z0-9_]+) \(\d+\)(?:, multinpc=[a-z0-9_]+ \(\d+\))?, coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
FACE_PLAYER = re.compile(r"\[(?:facing|face_pathingentity)\] player=\(index=(\d+)")
FACE_OTHER = re.compile(r"\[(?:facing|face_pathingentity|face_coord|face_angle)\]")


def angle(nx, nz, size, px, pz):
    dx = (px * 128 + 64) - (nx * 128 + size * 64)
    dz = (pz * 128 + 64) - (nz * 128 + size * 64)
    return int(math.atan2(dx, dz) * 1024.0 / math.pi) & 2047


def run(path, stats):
    recorder = None
    ptile = None            # recorder tile at the end of the current tick
    pprev = None            # ... of the previous tick
    npcs = {}               # index -> dict(type, x, z, faced, sent_angle)
    tick_faces = set()      # npc indices that sent a face naming the recorder this tick
    tick_seen = set()       # npc indices with a block this tick
    cur_npc = None
    in_player = False

    def settle():
        if pprev is None:
            return
        for idx, n in npcs.items():
            if not n.get("faced"):
                continue
            size = SIZE.get(n["type"], 1)
            a = angle(n["x"], n["z"], size, pprev[0], pprev[1])
            sent = idx in tick_faces
            if n.get("just_faced"):
                n["just_faced"] = False
                n["angle"] = a
                continue
            key = n["type"]
            if a != n.get("angle"):
                stats[key]["turned+sent" if sent else "turned-quiet"] += 1
            elif sent:
                stats[key]["same+sent"] += 1
            n["angle"] = a

    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TICK.match(line)
            if m:
                settle()
                tick_faces.clear()
                tick_seen.clear()
                pprev = ptile
                cur_npc = None
                continue
            m = PLAYER.search(line)
            if m:
                if recorder is None:
                    recorder = m.group(1)
                if m.group(1) == recorder:
                    x, z = (m.group(5), m.group(6)) if m.group(5) else (m.group(2), m.group(3))
                    ptile = (int(x), int(z))
                    in_player = True
                else:
                    in_player = False
                cur_npc = None
                continue
            m = NPC.search(line)
            if m:
                kind, idx, typ = m.group(1), m.group(2), m.group(3)
                in_player = False
                if kind == "del":
                    npcs.pop(idx, None)
                    cur_npc = None
                    continue
                x, z = (m.group(7), m.group(8)) if m.group(7) else (m.group(4), m.group(5))
                n = npcs.setdefault(idx, {"type": typ})
                if kind == "add":
                    n.clear()
                    n["type"] = typ
                n["type"] = typ
                n["x"], n["z"] = int(x), int(z)
                cur_npc = idx
                tick_seen.add(idx)
                continue
            if cur_npc is not None and not in_player:
                fm = FACE_PLAYER.search(line)
                if fm:
                    n = npcs[cur_npc]
                    if fm.group(1) == recorder:
                        if not n.get("faced"):
                            n["faced"] = True
                            n["just_faced"] = True
                        tick_faces.add(cur_npc)
                    else:
                        n["faced"] = False
                    continue
                if FACE_OTHER.search(line) or "type=reset" in line:
                    npcs[cur_npc]["faced"] = False


def main():
    total = defaultdict(lambda: defaultdict(int))
    for path in sys.argv[1:]:
        stats = defaultdict(lambda: defaultdict(int))
        run(path, stats)
        t = defaultdict(int)
        for typ, s in stats.items():
            for k, v in s.items():
                t[k] += v
                total[typ][k] += v
        print(f"{os.path.basename(path)}  turned+sent {t['turned+sent']}  turned-quiet {t['turned-quiet']}  same+sent {t['same+sent']}")
    rows = sorted(total.items(), key=lambda kv: -(kv[1]["turned+sent"] + kv[1]["turned-quiet"]))
    for typ, s in rows[:25]:
        print(f"  {typ:40s} turned+sent {s['turned+sent']:5d}  turned-quiet {s['turned-quiet']:5d}  same+sent {s['same+sent']:4d}")


if __name__ == "__main__":
    main()
