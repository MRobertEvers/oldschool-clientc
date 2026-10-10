#!/usr/bin/env python3
"""Bloat's stomp, Blert: does it hit by distance, or by line of sight?

  tools/blert_bloat_stomp_reach.py   (reads build/blert/bloat, Normal trio rooms)

Only the recording player carries hitpoints, so the sample is the recorder at
every stomp (npcAttack 3): its tile on the stomp tick, its gap from Bloat's 5x5
(Blert's npc coord is the south-west tile), whether any footprint edge tile
sees it past the 6x6 tank (local 29..34; the content's ~tob_bloat_sees, with
the tank as the only blocker), and whether its hitpoints fell by 30+ within 3
ticks (the splat shows two ticks after the attack's tick)."""
import json, glob, collections
TANK = lambda x, z: 29 <= x <= 34 and 29 <= z <= 34
def los(ax, az, bx, bz):
    # tile line (supercover-ish DDA): blocked if it enters a tank tile
    n = max(abs(bx - ax), abs(bz - az))
    for i in range(1, n):
        x = ax + (bx - ax) * i / n; z = az + (bz - az) * i / n
        for tx in {int(x), int(round(x))}:
            for tz in {int(z), int(round(z))}:
                if TANK(tx, tz): return False
    return True
def sees(bx, bz, px, pz):
    if bx <= px <= bx + 4 and bz <= pz <= bz + 4: return True
    edges = [(bx + i, bz + j) for i in range(5) for j in range(5) if i in (0, 4) or j in (0, 4)]
    return any(los(ex, ez, px, pz) for ex, ez in edges)
out = collections.defaultdict(lambda: [0, 0])
rows = []
for f in sorted(glob.glob("build/blert/bloat/*-*.json")):
    ev = json.load(open(f))
    stomps = [e["tick"] for e in ev if e["type"] == 10 and (e.get("npcAttack") or {}).get("attack") == 3]
    bpos = {e["tick"]: (e["xCoord"] % 64, e["yCoord"] % 64) for e in ev if e["type"] in (7, 8) and (e.get("npc") or {}).get("id") == 8359}
    rec = collections.defaultdict(dict)
    for e in ev:
        if e["type"] == 4 and "hitpoints" in e["player"]:
            rec[e["tick"]] = (e["xCoord"] % 64, e["yCoord"] % 64, e["player"]["hitpoints"] >> 16)
    for s in stomps:
        if s not in bpos or (s - 1) not in rec or not any((s + k) in rec for k in (1, 2, 3)): continue
        bx, bz = bpos[s]
        px, pz, _ = rec[s] if s in rec else rec[s - 1]
        hp0 = rec[s - 1][2]
        hp1 = min(rec[t][2] for t in (s, s + 1, s + 2, s + 3) if t in rec)
        gap = max(max(bx - px, px - (bx + 4), 0), max(bz - pz, pz - (bz + 4), 0))
        seen = sees(bx, bz, px, pz)
        hit = hp0 - hp1 >= 30
        key = (min(gap, 6), seen)
        out[key][0] += hit; out[key][1] += 1
        rows.append((gap, seen, hit, hp0 - hp1))
print("gap seen  hit/n")
for k in sorted(out): print("%3s %5s  %d/%d" % (k[0], k[1], out[k][0], out[k][1]))
far_seen = [r for r in rows if r[0] >= 4 and r[1]]
print("far (>=4) and seen, drops:", [r[3] for r in far_seen][:30])
