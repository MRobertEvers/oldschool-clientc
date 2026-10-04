#!/usr/bin/env python3
"""Queries behind encounters/serpent_shaman.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/serpent_shaman.queries.py
Reads sources/blert_api/observed_npc_events.tsv (kind spawn/attack/death per npc instance room_id; npc 12811;
blert/PROVENANCE.md: NPC_ATTACK is anchored to the npc's animation; ids in blert/ID_TABLE.md) and the run JSONs
(NPC_UPDATE / PLAYER_UPDATE tiles, observed). An npc acting on tick T sees the world as it stood at the end of T-1."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
rows = [r for r in csv.DictReader(open(os.path.join(API, "observed_npc_events.tsv")), delimiter="\t") if r["npc_id"] == "12811"]
inst = collections.defaultdict(list)
for r in rows:
    inst[(r["run"], r["wave"], r["room_id"])].append(r)


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def spawn_tick(ev):
    s = [int(e["tick"]) for e in ev if e["kind"] == "spawn"]
    return s[0] if s else None


def q_overview():
    """instances, runs, waves, spawn ticks, attack ids; tick-0 versus reinforcement split."""
    ids = collections.Counter(); st = collections.Counter(); w = collections.Counter(); runs = set(); wr = collections.Counter()
    for (run, wave, rid), ev in inst.items():
        runs.add(run); w[wave] += 1
        s = spawn_tick(ev)
        wr[(wave, "t0" if s == 0 else "reinf")] += 1
        for e in ev:
            if e["kind"] == "spawn": st[e["tick"]] += 1
            if e["kind"] == "attack": ids[e["attack_id"]] += 1
    print("instances", len(inst), "runs", len(runs), "by wave:", dist(w)); print("by wave and kind:", dist(wr))
    print("spawn ticks:", dist(st)); print("attack ids:", dist(ids))


def q_cadence():
    """gaps between consecutive attacks of one instance; attacks per instance."""
    g = collections.Counter(); n = collections.Counter()
    for ev in inst.values():
        at = sorted(int(e["tick"]) for e in ev if e["kind"] == "attack")
        n[len(at)] += 1
        for a, b in zip(at, at[1:]): g[b - a] += 1
    tot = sum(g.values())
    print("attack gap:", dist(g), "total gaps", tot, "share 5: %.3f" % (g[5] / tot)); print("attacks per instance:", dist(n))


def q_first():
    """first attack tick minus spawn tick (reinforcements); first attack tick (tick-0 shamans, M6)."""
    c = collections.Counter(); t0 = collections.Counter(); none = collections.Counter()
    for ev in inst.values():
        sp = spawn_tick(ev)
        at = sorted(int(e["tick"]) for e in ev if e["kind"] == "attack")
        if sp is None: continue
        if sp == 0:
            if at: t0[at[0]] += 1
            else: none["t0"] += 1
        else:
            if at: c[at[0] - sp] += 1
            else: none["reinf"] += 1
    print("reinforcement first attack minus spawn:", dist(c)); print("tick-0 shaman first attack tick:", dist(t0)); print("never attacked:", dist(none))


def q_spawn():
    """spawn tiles by kind."""
    t = collections.Counter()
    for ev in inst.values():
        for e in ev:
            if e["kind"] == "spawn": t[("t0" if e["tick"] == "0" else "reinf", e["x"], e["y"])] += 1
    print("spawn tiles:", dist(t))


def q_life():
    """death tick minus spawn tick; instances with no death."""
    lifes = []; nodeath = 0
    for ev in inst.values():
        sp = spawn_tick(ev); de = [int(e["tick"]) for e in ev if e["kind"] == "death"]
        if sp is not None and de: lifes.append(de[0] - sp)
        else: nodeath += 1
    lifes.sort()
    print("lifetime ticks n=%d min %d median %d max %d; no death %d" % (len(lifes), lifes[0], lifes[len(lifes) // 2], lifes[-1], nodeath))


RUNS = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}


def _sh():
    """yield (run, wave, roomId, npc tile by tick, player tiles by tick, attack ticks, spawn tick)."""
    for run, d in RUNS.items():
        for wn, ev in d["waves"].items():
            ids = {e["npc"]["roomId"] for e in ev if e["type"] == 7 and e["npc"]["id"] == 12811}
            pl = {}
            for e in ev:
                if e["type"] == 4: pl.setdefault(e["tick"], []).append((e["xCoord"], e["yCoord"]))
            for rid in ids:
                pos = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] in (7, 8) and e["npc"]["roomId"] == rid}
                evs = inst.get((run, wn, str(rid)), [])
                att = [int(r["tick"]) for r in evs if r["kind"] == "attack"]
                yield run, wn, rid, pos, pl, att, spawn_tick(evs)


def q_move():
    """per-tick step (Chebyshev) between consecutive observed ticks; ticks from spawn to the first tile change."""
    c = collections.Counter(); fm = collections.Counter()
    for run, wn, rid, pos, pl, att, sp in _sh():
        ts = sorted(pos)
        for t in ts:
            if t - 1 in pos:
                c[max(abs(pos[t][0] - pos[t - 1][0]), abs(pos[t][1] - pos[t - 1][1]))] += 1
        if sp is not None and sp > 0 and ts:
            first = ts[0]; ch = [t for t in ts if pos[t] != pos[first]]
            fm[(ch[0] - first) if ch else "never"] += 1
    print("tiles moved per tick:", dist(c)); print("reinforcement ticks from first observation to first tile change:", dist(fm))


def q_reach():
    """Chebyshev distance from the shaman's tile to the player's tile on the tick BEFORE each attack."""
    c = collections.Counter(); n = 0
    for run, wn, rid, pos, pl, att, sp in _sh():
        for t in att:
            tt = max((k for k in pos if k <= t), default=None)
            if tt is None or (t - 1) not in pl: continue
            sx, sy = pos[tt]
            for px, py in pl[t - 1][:1]:
                c[max(abs(sx - px), abs(sy - py))] += 1; n += 1
    print("attack-tick Chebyshev distance to the player (n=%d):" % n, dist(c))


if __name__ == "__main__":
    for f in (q_overview, q_cadence, q_first, q_spawn, q_life, q_move, q_reach):
        print("--", f.__name__, f.__doc__); f()


def q_reinf_gate():
    """reinforcement shaman spawn tile against the player's tile on the tick before the spawn (waves 4-6, 10, 11), and the pair's other spawn tile (jaguar/minotaur) in the same wave."""
    c = collections.Counter()
    for run, wn, rid, pos, pl, att, sp in _sh():
        if sp != 66: continue
        d = RUNS[run]["waves"][wn]
        pair = [(e["xCoord"], e["yCoord"]) for e in d if e["type"] == 7 and e["tick"] == 66 and e["npc"]["id"] in (12810, 12812)]
        p = pl.get(65, [None])[0]
        c[("shaman", pos[66], "pair", tuple(pair), "player65", p)] += 1
    for k in sorted(c, key=str): print(k, c[k])
