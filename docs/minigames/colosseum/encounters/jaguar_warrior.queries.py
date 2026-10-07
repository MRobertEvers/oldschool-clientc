#!/usr/bin/env python3
"""Queries behind encounters/jaguar_warrior.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/jaguar_warrior.queries.py
Reads sources/blert_api/observed_npc_events.tsv (kind spawn/attack/death per npc instance room_id; npc 12810;
blert/PROVENANCE.md: NPC_ATTACK is anchored to the npc's animation; ids in blert/ID_TABLE.md)."""
import collections, csv, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
rows = [r for r in csv.DictReader(open(os.path.join(API, "observed_npc_events.tsv")), delimiter="\t") if r["npc_id"] == "12810"]
inst = collections.defaultdict(list)
for r in rows:
    inst[(r["run"], r["wave"], r["room_id"])].append(r)


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def q_overview():
    """instances, runs, waves, spawn ticks, attack ids."""
    ids = collections.Counter(); st = collections.Counter(); w = collections.Counter(); runs = set()
    for (run, wave, rid), ev in inst.items():
        runs.add(run); w[wave] += 1
        for e in ev:
            if e["kind"] == "spawn": st[e["tick"]] += 1
            if e["kind"] == "attack": ids[e["attack_id"]] += 1
    print("instances", len(inst), "runs", len(runs), "by wave:", dist(w)); print("spawn ticks:", dist(st)); print("attack ids:", dist(ids))


def q_cadence():
    """gaps between consecutive attacks of one instance; attacks per instance."""
    g = collections.Counter(); n = collections.Counter()
    for ev in inst.values():
        at = sorted(int(e["tick"]) for e in ev if e["kind"] == "attack")
        n[len(at)] += 1
        for a, b in zip(at, at[1:]): g[b - a] += 1
    print("attack gap:", dist(g)); print("attacks per instance:", dist(n))


def q_first():
    """first attack tick minus spawn tick."""
    c = collections.Counter()
    for ev in inst.values():
        sp = [int(e["tick"]) for e in ev if e["kind"] == "spawn"]
        at = sorted(int(e["tick"]) for e in ev if e["kind"] == "attack")
        if sp and at: c[at[0] - sp[0]] += 1
        elif sp: c["none"] += 1
    print("first attack minus spawn tick:", dist(c))


def q_spawn():
    """reinforcement spawn tiles and the first position seen after the spawn."""
    t = collections.Counter()
    for ev in inst.values():
        for e in ev:
            if e["kind"] == "spawn": t[(e["x"], e["y"])] += 1
    print("spawn tiles:", dist(t))


def q_life():
    """death tick minus spawn tick; instances with no death."""
    c = collections.Counter(); nodeath = 0; lifes = []
    for ev in inst.values():
        sp = [int(e["tick"]) for e in ev if e["kind"] == "spawn"]
        de = [int(e["tick"]) for e in ev if e["kind"] == "death"]
        if sp and de: lifes.append(de[0] - sp[0])
        else: nodeath += 1
    lifes.sort()
    print("lifetime ticks n=%d min %d median %d max %d; no death %d" % (len(lifes), lifes[0], lifes[len(lifes) // 2], lifes[-1], nodeath))


def q_tick0():
    """any 12810 spawn at tick 0 (M18)."""
    print("jaguar spawns at tick 0:", sum(1 for ev in inst.values() for e in ev if e["kind"] == "spawn" and e["tick"] == "0"))
    t = collections.Counter(e["tick"] for ev in inst.values() for e in ev if e["kind"] == "attack")
    print("attack ticks (by wave tick) min:", min(int(k) for k in t))


import glob, json
RUNS = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}


def _jag():
    """yield (run, wave, roomId, npc tile by tick, player tile by tick, attack ticks) for each jaguar instance."""
    for run, d in RUNS.items():
        for wn, ev in d["waves"].items():
            ids = {e["npc"]["roomId"] for e in ev if e["type"] == 7 and e["npc"]["id"] == 12810}
            pl = {}
            for e in ev:
                if e["type"] == 4: pl.setdefault(e["tick"], []).append((e["xCoord"], e["yCoord"]))
            for rid in ids:
                pos = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] in (7, 8) and e["npc"]["roomId"] == rid}
                att = [int(r["tick"]) for r in inst.get((run, wn, str(rid)), []) if r["kind"] == "attack"]
                yield run, wn, rid, pos, pl, att


def q_move():
    """per-tick step of a jaguar (Chebyshev, NPC_UPDATE tiles on consecutive ticks)."""
    c = collections.Counter(); n = 0
    for run, wn, rid, pos, pl, att in _jag():
        for t in sorted(pos):
            if t - 1 in pos:
                c[max(abs(pos[t][0] - pos[t - 1][0]), abs(pos[t][1] - pos[t - 1][1]))] += 1
    print("tiles moved per tick (consecutive observed ticks):", dist(c))


def q_reach():
    """at each attack tick: gap between the jaguar's 2x2 footprint (anchor = south-west tile) and the player's tile
    on the previous tick (an npc acting on tick T sees the end of T-1); gx, gy = tiles outside the footprint per axis."""
    c = collections.Counter(); n = 0
    for run, wn, rid, pos, pl, att in _jag():
        for t in att:
            tt = max(k for k in pos if k <= t) if any(k <= t for k in pos) else None
            if tt is None or (t - 1) not in pl: continue
            jx, jy = pos[tt]
            for px, py in pl[t - 1][:1]:
                gx = max(jx - px, px - (jx + 1), 0); gy = max(jy - py, py - (jy + 1), 0)
                c[(gx, gy)] += 1
    print("attack-tick gap (gx,gy) of player tile to the footprint:", dist(c))


if __name__ == "__main__":
    for f in (q_overview, q_cadence, q_first, q_spawn, q_life, q_tick0, q_move, q_reach):
        print("--", f.__name__, f.__doc__); f()
