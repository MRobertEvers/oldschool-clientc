#!/usr/bin/env python3
"""Queries behind encounters/javelin_colossus.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/javelin_colossus.queries.py
Reads sources/blert_api/observed_npc_events.tsv (kind spawn/attack/death per npc instance room_id; attack_id
105 javelin_auto = seq 10892, 106 javelin_toss = seq 10893; blert/PROVENANCE.md: NPC_ATTACK is anchored to the
npc's animation, tick 0 is the recorder's tick). M8: autos between tosses; cadence; first attack; spawn tile."""
import collections, csv, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
rows = [r for r in csv.DictReader(open(os.path.join(API, "observed_npc_events.tsv")), delimiter="\t") if r["npc_id"] == "12817"]
inst = collections.defaultdict(list)
for r in rows:
    inst[(r["run"], r["wave"], r["room_id"])].append(r)


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def q_m8():
    """autos between consecutive tosses, autos before the first toss, total attacks (auto+toss) per toss cycle."""
    between = collections.Counter(); first = collections.Counter(); attacks_per = collections.Counter()
    tail = collections.Counter(); nt = 0; na = 0; consec = collections.Counter()
    for k, ev in inst.items():
        atk = sorted((int(e["tick"]), e["attack_id"]) for e in ev if e["kind"] == "attack")
        seq = [a for _, a in atk]; na += seq.count("105"); nt += seq.count("106")
        idx = [i for i, a in enumerate(seq) if a == "106"]
        if idx: first[idx[0]] += 1
        for a, b in zip(idx, idx[1:]):
            between[b - a - 1] += 1
        tail[len(seq) - (idx[-1] + 1) if idx else len(seq)] += 1
    print("instances", len(inst), "autos", na, "tosses", nt)
    print("autos before first toss (instances with a toss):", dist(first))
    print("autos between consecutive tosses:", dist(between))
    print("autos after last toss (or all, if none):", dist(tail))


def q_gaps():
    """tick gaps between consecutive attacks of one instance, by the pair (previous, next)."""
    g = collections.defaultdict(collections.Counter)
    for k, ev in inst.items():
        atk = sorted((int(e["tick"]), e["attack_id"]) for e in ev if e["kind"] == "attack")
        for (t0, a0), (t1, a1) in zip(atk, atk[1:]):
            g[(a0, a1)][t1 - t0] += 1
    for p in sorted(g): print("gap %s->%s:" % p, dist(g[p]))


def q_first():
    """first attack tick of an instance, minus its spawn tick, split by a tick-0 spawn and a reinforcement."""
    c = collections.Counter(); lone = collections.Counter(); fa = collections.Counter()
    for k, ev in inst.items():
        sp = [int(e["tick"]) for e in ev if e["kind"] == "spawn"]
        at = sorted((int(e["tick"]), e["attack_id"]) for e in ev if e["kind"] == "attack")
        if sp and at:
            c[(sp[0] == 0, at[0][0] - sp[0])] += 1; fa[at[0][1]] += 1
    print("first attack minus spawn tick, (tick-0 spawn?, delta):", dist(c)); print("first attack id:", dist(fa))


def q_spawn():
    """spawn tick and tile of javelin instances; a reinforcement javelin would carry a tick > 0."""
    t = collections.Counter(); tiles = collections.Counter()
    for k, ev in inst.items():
        for e in ev:
            if e["kind"] == "spawn": t[int(e["tick"])] += 1; tiles[(e["x"], e["y"])] += 1
    print("spawn ticks:", dist(t)); print("spawn tiles:", dist(tiles))


def q_death():
    """instances that died; ticks of life from spawn."""
    d = collections.Counter()
    for k, ev in inst.items():
        de = [e for e in ev if e["kind"] == "death"]
        d["died" if de else "survived_wave_end"] += 1
    print("deaths:", dist(d))




def _runs():
    import glob, json
    return {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}


def q_pool_delay():
    """M9 (landing delay): under Reentry the toss leaves a pool (type 203 primarySpawned, game object 50743; it appears
    where the javelin lands). For each pool spawn, the nearest EARLIER javelin toss tick in the wave (other tosses
    in the wave may pair with a pool outside the sample), and the player's tile at the toss tick and at the pool tick
    versus the pool tile (Chebyshev). Pools pre-existing at tick 0 are skipped."""
    delta = collections.Counter(); at_toss = collections.Counter(); at_pool = collections.Counter(); unmatched = 0; n = 0
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            tosses = sorted(int(e["tick"]) for e in ev if e["type"] == 10 and e["npc"]["id"] == 12817 and e["npcAttack"]["attack"] == 106)
            ppos = {}
            for e in ev:
                if e["type"] == 4 and e.get("player"): ppos[e["tick"]] = (e["xCoord"], e["yCoord"])
            for e in ev:
                if e["type"] != 203 or e["tick"] == 0: continue
                for p in e["colosseumReentryPools"]["primarySpawned"]:
                    n += 1
                    prev = [t for t in tosses if t < e["tick"]]
                    if not prev: unmatched += 1; continue
                    t = prev[-1]; delta[e["tick"] - t] += 1
                    for tag, tk, c in (("toss", t, at_toss), ("pool", e["tick"], at_pool)):
                        q = ppos.get(tk)
                        if q: c[max(abs(q[0] - p["x"]), abs(q[1] - p["y"]))] += 1
    print("pool spawns (tick>0):", n, "unmatched:", unmatched)
    print("pool tick minus nearest earlier toss tick:", dist(delta))
    print("player tile vs pool tile at toss tick (Chebyshev):", dist(at_toss))
    print("player tile vs pool tile at pool tick (Chebyshev):", dist(at_pool))




def q_pool_pairing():
    """Per wave: pool spawns (tick>0) for which a toss of ANY javelin sits at exactly pool tick - k, k=2..9; and pools by wave.
    A wave with no javelin cannot pair (Sol and other sources spawn pools too)."""
    byk = collections.Counter(); perwave = collections.Counter(); nj = collections.Counter(); n = 0
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            tosses = set(int(e["tick"]) for e in ev if e["type"] == 10 and e["npc"]["id"] == 12817 and e["npcAttack"]["attack"] == 106)
            for e in ev:
                if e["type"] != 203 or e["tick"] == 0: continue
                for p in e["colosseumReentryPools"]["primarySpawned"]:
                    perwave[(int(wn), bool(tosses))] += 1
                    if tosses:
                        n += 1
                        for k in range(2, 10):
                            if e["tick"] - k in tosses: byk[k] += 1
    print("pools by (wave, wave has a toss):", dist(perwave)); print("pools in waves with a toss:", n)
    print("pool with a toss exactly k ticks earlier:", dist(byk))



def q_pool_target():
    """Where the sky javelin lands: for pools exactly 6 ticks after a toss, the Chebyshev distance from the pool tile to the
    player's tile at the toss tick, at toss+1 .. toss+5 and at the pool tick (a PLAYER_UPDATE every tick)."""
    c = collections.defaultdict(collections.Counter); n = 0
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            tosses = set(int(e["tick"]) for e in ev if e["type"] == 10 and e["npc"]["id"] == 12817 and e["npcAttack"]["attack"] == 106)
            ppos = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4 and e.get("player")}
            for e in ev:
                if e["type"] != 203 or e["tick"] == 0: continue
                for p in e["colosseumReentryPools"]["primarySpawned"]:
                    if e["tick"] - 6 not in tosses: continue
                    n += 1
                    for off in range(0, 7):
                        q = ppos.get(e["tick"] - 6 + off)
                        if q: c[off][max(abs(q[0] - p["x"]), abs(q[1] - p["y"]))] += 1
    print("pools exactly 6 ticks after a toss:", n)
    for off in sorted(c): print("player tile vs pool tile at toss+%d:" % off, dist(c[off]))


def q_range():
    """Distance at the moment of an attack: the NPC_ATTACK event carries the npc's tile (xCoord, yCoord = the size-3 npc's
    south-west tile); the player's tile is the same tick's PLAYER_UPDATE. Distance = Chebyshev from the player to the nearest
    tile of the 3x3 footprint. Reports the distribution for autos and for tosses (max is a lower bound on the range, never
    proof of it: the recorder sees no range rule)."""
    c = {"105": collections.Counter(), "106": collections.Counter()}; n = 0
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            ppos = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4 and e.get("player")}
            for e in ev:
                if e["type"] == 10 and e["npc"]["id"] == 12817 and e["tick"] in ppos:
                    px, py = ppos[e["tick"]]; nx, ny = e["xCoord"], e["yCoord"]
                    dx = max(nx - px, 0, px - (nx + 2)); dy = max(ny - py, 0, py - (ny + 2))
                    c[str(e["npcAttack"]["attack"])][max(dx, dy)] += 1
    for k in c: print("attack", k, "distance (Chebyshev to the 3x3 footprint):", dist(c[k]))


def q_movement():
    """From NPC_UPDATE (position every tick, size-3 npc's south-west tile): per-tick step of one javelin, the share of ticks
    it moves, and the steps taken on a tick it attacks and on the tick after. Gaps in the stream (a missing tick) are skipped."""
    step = collections.Counter(); onatk = collections.Counter(); after = collections.Counter(); ticks = 0; moved = 0; hp = collections.Counter()
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            pos = collections.defaultdict(dict); atk = collections.defaultdict(set)
            for e in ev:
                if e["type"] == 8 and e["npc"]["id"] == 12817: pos[e["npc"]["roomId"]][e["tick"]] = (e["xCoord"], e["yCoord"])
                if e["type"] == 10 and e["npc"]["id"] == 12817: atk[e["npc"]["roomId"]].add(e["tick"])
            for r, tp in pos.items():
                for t, (x, y) in tp.items():
                    if t - 1 in tp:
                        q = tp[t - 1]; st = max(abs(x - q[0]), abs(y - q[1])); step[st] += 1; ticks += 1; moved += st > 0
                        if t in atk[r]: onatk[st] += 1
                        if t - 1 in atk[r]: after[st] += 1
    print("javelin steps per tick:", dist(step), "moved share %.1f%%" % (100.0 * moved / ticks))
    print("step on an attack tick:", dist(onatk)); print("step on the tick after an attack:", dist(after))


if __name__ == "__main__":
    for q in (q_m8, q_gaps, q_first, q_spawn, q_death, q_pool_delay, q_pool_pairing, q_pool_target, q_range, q_movement):
        print("--", q.__name__); q()
