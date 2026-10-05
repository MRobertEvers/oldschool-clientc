#!/usr/bin/env python3
"""Queries behind encounters/arena_and_spawn_tiles.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/arena_and_spawn_tiles.queries.py
Reads sources/blert_api/*.json (type 4 PLAYER_UPDATE, 7 NPC_SPAWN, with xCoord/yCoord/tick) and
sources/blert_api/observed_npc_events.tsv. Q1 player/npc occupancy of the four cache pillar footprints and the
wiki marker boxes; Q2 observed walkable bounds; Q3 tick-0 tiles per kind and wave; Q4 spawn tiles after tick 0;
Q5 player tile at wave tick 0 / 1; Q6 tick-0 tile reuse inside one wave (draw without repeats?).
A tick-0 tile is where the npc stood when the client first saw it, not a spawn time (B:PROVENANCE.md)."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
runs = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}
rows = list(csv.DictReader(open(API + "/observed_npc_events.tsv"), delimiter="\t"))
BX0, BX1, BY0, BY1 = 1800, 1850, 3085, 3130  # drop the few (0,0)-type outliers


def inside(x, y): return BX0 <= x <= BX1 and BY0 <= y <= BY1


occ, npcocc = collections.Counter(), collections.Counter()
for d in runs.values():
    for ev in d["waves"].values():
        for e in ev:
            if "xCoord" in e and inside(e["xCoord"], e["yCoord"]):
                (occ if e["type"] == 4 else npcocc)[(e["xCoord"], e["yCoord"])] += 1


def box(b, S): return sum(c for (x, y), c in S.items() if b[0] <= x <= b[1] and b[2] <= y <= b[3])


print("Q1 occupancy (player events, npc events) per 3x3 box; cache pillar origins local (24,26) (24,41) (39,26) (39,41), world = local + (1792,3072)")
for nm, b in [("cache pillar SW", (1816, 1818, 3098, 3100)), ("cache pillar NW", (1816, 1818, 3113, 3115)),
              ("cache pillar SE", (1831, 1833, 3098, 3100)), ("cache pillar NE", (1831, 1833, 3113, 3115)),
              ("wiki-marker box interior NW", (1820, 1822, 3109, 3111)), ("wiki-marker box interior NE", (1827, 1829, 3109, 3111)),
              ("wiki-marker box interior SW", (1820, 1822, 3102, 3104)), ("wiki-marker box interior SE", (1827, 1829, 3102, 3104))]:
    print("  %-30s player %5d npc %5d" % (nm, box(b, occ), box(b, npcocc)))
for nm, S in (("player", occ), ("npc", npcocc)):
    xs = [p[0] for p in S]; ys = [p[1] for p in S]
    print("Q2 %s tiles seen: %d distinct, x %d-%d y %d-%d (local x %d-%d y %d-%d)" % (nm, len(S), min(xs), max(xs), min(ys), max(ys), min(xs) - 1792, max(xs) - 1792, min(ys) - 3072, max(ys) - 3072))

t0 = collections.defaultdict(collections.Counter); t0w = collections.defaultdict(list)
for r in rows:
    if r["kind"] == "spawn" and int(r["tick"]) == 0:
        t0[r["npc"]][(int(r["x"]), int(r["y"]))] += 1
        t0w[(r["run"], int(r["wave"]), r["npc"])].append((int(r["x"]), int(r["y"])))
alltiles = set()
print("Q3 tick-0 spawn tiles per kind (distinct tiles, spawns):")
for k, c in sorted(t0.items()):
    alltiles |= set(c)
    xs = [p[0] for p in c]; ys = [p[1] for p in c]
    print("  %-22s %3d tiles %4d spawns  x %d-%d y %d-%d" % (k, len(c), sum(c.values()), min(xs), max(xs), min(ys), max(ys)))
print("Q3 distinct tick-0 tiles over all kinds:", len(alltiles))
w1 = collections.Counter()
for r in rows:
    if r["kind"] == "spawn" and int(r["tick"]) == 0 and int(r["wave"]) == 1: w1[(r["npc"], int(r["x"]), int(r["y"]))] += 1
print("Q3 wave 1 tick-0 tiles (kind,x,y):count", sorted(w1.items()))

late = collections.Counter(); lt = collections.Counter()
for r in rows:
    if r["kind"] == "spawn" and int(r["tick"]) > 0:
        late[(int(r["tick"]) == 66, r["npc"], int(r["x"]), int(r["y"]))] += 1; lt[int(r["tick"])] += 1
print("Q4 spawns after tick 0 by tick (top):", lt.most_common(8))
rein = collections.Counter(); rk = collections.Counter()
for (is66, n, x, y), c in late.items():
    if is66: rein[(x, y)] += c; rk[n] += c
print("Q4 tick-66 spawn tiles (%d distinct, %d spawns); kinds %s" % (len(rein), sum(rein.values()), dict(rk)))
print("   by tile:", sorted(rein.items(), key=lambda kv: -kv[1]))
nl = collections.Counter()
for (is66, n, x, y), c in late.items():
    if not is66: nl[(n, x, y)] += c
print("Q4 non-66 post-0 spawn tiles (kind,x,y):count", sorted(nl.items())[:20], "total", sum(nl.values()))

pl = collections.Counter(); plall = {}
for run, d in runs.items():
    for wn, ev in d["waves"].items():
        p = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4}
        if 0 in p: pl[p[0]] += 1
        elif 1 in p: pl[("t1",) + p[1]] += 1
print("Q5 player tile on wave tick 0 (else tick 1):", sorted(pl.items(), key=lambda kv: -kv[1])[:6], "streams", sum(pl.values()))

rep = collections.Counter(); tot = collections.Counter()
for (run, w, n), L in t0w.items():
    if len(L) > 1:
        tot[n] += 1
        if len(set(L)) < len(L): rep[n] += 1
print("Q6 same-kind multi-spawn at tick 0 with a repeated tile (kind: repeats of streams-with-multiple):", {n: (rep[n], tot[n]) for n in tot})
