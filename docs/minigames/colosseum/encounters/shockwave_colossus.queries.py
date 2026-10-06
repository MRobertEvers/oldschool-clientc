#!/usr/bin/env python3
"""Queries behind encounters/shockwave_colossus.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/shockwave_colossus.queries.py
Reads sources/blert_api/observed_npc_events.tsv (npc_id 12819; attack_id 108 shockwave_auto = seq 10903;
blert/PROVENANCE.md: NPC_ATTACK is anchored to the npc's animation, tick 0 is the recorder's tick) and the cached run
JSON for positions (NPC_UPDATE type 8, PLAYER_UPDATE type 4, NPC_ATTACK type 10)."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
rows = [r for r in csv.DictReader(open(os.path.join(API, "observed_npc_events.tsv")), delimiter="\t") if r["npc_id"] == "12819"]
inst = collections.defaultdict(list)
for r in rows:
    inst[(r["run"], r["wave"], r["room_id"])].append(r)


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def q_gaps():
    g = collections.Counter(); ids = collections.Counter(); n = 0
    for k, ev in inst.items():
        atk = sorted(int(e["tick"]) for e in ev if e["kind"] == "attack"); n += len(atk)
        for e in ev:
            if e["kind"] == "attack": ids[e["attack_id"]] += 1
        for a, b in zip(atk, atk[1:]): g[b - a] += 1
    print("instances", len(inst), "attacks", n, "attack ids:", dist(ids)); print("gap between consecutive attacks:", dist(g))


def q_first():
    c = collections.Counter(); share = collections.Counter()
    for k, ev in inst.items():
        sp = [int(e["tick"]) for e in ev if e["kind"] == "spawn"]
        at = sorted(int(e["tick"]) for e in ev if e["kind"] == "attack")
        if sp and at: c[(sp[0] == 0, at[0] - sp[0])] += 1
    print("first attack minus spawn tick, (tick-0 spawn?, delta):", dist(c))


def q_spawn():
    t = collections.Counter(); tiles = collections.Counter(); wave = collections.Counter(); perwave = collections.Counter()
    for k, ev in inst.items():
        wave[k[1]] += 1; perwave[(k[0], k[1])] += 1
        for e in ev:
            if e["kind"] == "spawn": t[int(e["tick"])] += 1; tiles[(e["x"], e["y"])] += 1
    print("spawn ticks:", dist(t)); print("distinct spawn tiles:", len(tiles), dist(tiles))
    print("instances by wave:", dist(wave)); print("shockwaves per (run,wave):", dist(collections.Counter(perwave.values())))


def q_death():
    d = collections.Counter(); life = []
    for k, ev in inst.items():
        de = [int(e["tick"]) for e in ev if e["kind"] == "death"]; d["died" if de else "survived"] += 1
    print("deaths:", dist(d))


def _runs():
    return {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}


def q_range():
    c = collections.Counter(); n = 0
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            ppos = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4 and e.get("player")}
            for e in ev:
                if e["type"] == 10 and e["npc"]["id"] == 12819 and e["tick"] in ppos:
                    px, py = ppos[e["tick"]]; nx, ny = e["xCoord"], e["yCoord"]
                    dx = max(nx - px, 0, px - (nx + 2)); dy = max(ny - py, 0, py - (ny + 2)); c[max(dx, dy)] += 1; n += 1
    print("attacks with a player tile:", n, "distance (Chebyshev to the 3x3 footprint):", dist(c))


def q_movement():
    step = collections.Counter(); onatk = collections.Counter(); ticks = 0; moved = 0
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            pos = collections.defaultdict(dict); atk = collections.defaultdict(set)
            for e in ev:
                if e["type"] == 8 and e["npc"]["id"] == 12819: pos[e["npc"]["roomId"]][e["tick"]] = (e["xCoord"], e["yCoord"])
                if e["type"] == 10 and e["npc"]["id"] == 12819: atk[e["npc"]["roomId"]].add(e["tick"])
            for r, tp in pos.items():
                for t, (x, y) in tp.items():
                    if t - 1 in tp:
                        q = tp[t - 1]; st = max(abs(x - q[0]), abs(y - q[1])); step[st] += 1; ticks += 1; moved += st > 0
                        if t in atk[r]: onatk[st] += 1
    print("steps per tick:", dist(step), "moved share %.1f%%" % (100.0 * moved / max(ticks, 1))); print("step on an attack tick:", dist(onatk))


if __name__ == "__main__":
    for q in (q_gaps, q_first, q_spawn, q_death, q_range, q_movement):
        print("--", q.__name__); q()
