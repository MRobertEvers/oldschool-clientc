#!/usr/bin/env python3
"""Queries behind encounters/fremennik_trio.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/fremennik_trio.queries.py
Reads sources/blert_api/*.json. Event types: 4 PLAYER_UPDATE, 7 NPC_SPAWN, 8 NPC_UPDATE (position every tick), 9 NPC_DEATH,
10 NPC_ATTACK (anchored to the npc's animation; npcAttack.attack 100 berserker_auto, 101 seer_auto, 102 archer_auto).
Trio ids: 12814 archer, 12815 seer, 12816 berserker. Tick 0 is the recorder's tick (blert/PROVENANCE.md)."""
import collections, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
runs = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}
NAME = {12814: "archer", 12815: "seer", 12816: "berserker"}


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=lambda v: (str(type(v)), v)))


def trio_waves():
    for run, d in runs.items():
        for wn, ev in d["waves"].items():
            if int(wn) < 12:
                yield run, int(wn), ev


def q_first_attack():
    """M6: first NPC_ATTACK of each trio member, ticks from recorder tick 0; and the member order."""
    first = collections.defaultdict(collections.Counter); order = collections.Counter(); first_any = collections.Counter(); n = 0
    for run, wn, ev in trio_waves():
        f = {}
        for e in ev:
            if e["type"] == 10 and e["npc"]["id"] in NAME and e["npc"]["id"] not in f:
                f[e["npc"]["id"]] = e["tick"]
        for k, t in f.items(): first[NAME[k]][t] += 1
        if f: first_any[min(f.values())] += 1; n += 1
        if len(f) == 3: order[tuple(NAME[k] for k in sorted(f, key=lambda k: (f[k], k)))] += 1
    for k in NAME.values(): print("Q1 first attack tick", k, dist(first[k]), "n=", sum(first[k].values()))
    print("Q1 earliest trio attack per wave", dist(first_any), "n=", n)
    print("Q1 order when all three attack", dict(order))


def q_phase():
    """M7: attack tick mod 6 per member, and the offset of each member's first attack from the wave's first trio attack."""
    mod = collections.defaultdict(collections.Counter); off = collections.defaultdict(collections.Counter); gaps = collections.defaultdict(collections.Counter)
    for run, wn, ev in trio_waves():
        att = collections.defaultdict(list)
        for e in ev:
            if e["type"] == 10 and e["npc"]["id"] in NAME: att[e["npc"]["roomId"]].append((e["tick"], e["npc"]["id"]))
        if not att: continue
        t0 = min(v[0][0] for v in att.values())
        for rid, v in att.items():
            k = NAME[v[0][1]]
            mod[k][v[0][0] % 6] += 1; off[k][v[0][0] - t0] += 1
            for a, b in zip(v, v[1:]): gaps[k][b[0] - a[0]] += 1
    for k in NAME.values():
        print("Q2 first attack tick mod 6", k, dist(mod[k])); print("Q2 offset from earliest trio attack", k, dist(off[k])); print("Q2 gaps", k, dist(gaps[k]))


def q_geometry():
    """Where each member stands against the player on an attack tick (npc tile minus nearest-by-tick player tile) and chebyshev distance."""
    rel = collections.defaultdict(collections.Counter); cheb = collections.defaultdict(collections.Counter)
    for run, wn, ev in trio_waves():
        pl = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4}
        for e in ev:
            if e["type"] == 10 and e["npc"]["id"] in NAME and e["tick"] in pl:
                p = pl[e["tick"]]; dx, dy = e["xCoord"] - p[0], e["yCoord"] - p[1]
                rel[NAME[e["npc"]["id"]]][(dx, dy)] += 1; cheb[NAME[e["npc"]["id"]]][max(abs(dx), abs(dy))] += 1
    for k in NAME.values():
        print("Q3 chebyshev distance on attack", k, dist(cheb[k]), "n=", sum(cheb[k].values()))
        print("Q3 top offsets (dx,dy) npc minus player", k, rel[k].most_common(6))


def q_move():
    """Tiles moved per tick by a trio member (NPC_UPDATE positions), and whether it moves on the tick it attacks."""
    step = collections.Counter(); atk_move = collections.Counter()
    for run, wn, ev in trio_waves():
        pos = collections.defaultdict(dict); att = set()
        for e in ev:
            if e["type"] in (7, 8) and e["npc"]["id"] in NAME: pos[e["npc"]["roomId"]][e["tick"]] = (e["xCoord"], e["yCoord"])
            if e["type"] == 10 and e["npc"]["id"] in NAME: att.add((e["npc"]["roomId"], e["tick"]))
        for rid, p in pos.items():
            for t in sorted(p):
                if t - 1 in p:
                    s = max(abs(p[t][0] - p[t - 1][0]), abs(p[t][1] - p[t - 1][1])); step[s] += 1
                    if (rid, t) in att: atk_move[s] += 1
    print("Q4 tiles moved per tick (all trio updates)", dist(step), "n=", sum(step.values()))
    print("Q4 tiles moved on the tick of an attack", dist(atk_move))


def q_spawn_hp():
    """Spawn tick and the highest hitpoints value ever recorded per member (damage is observed, the start is not)."""
    sp = collections.defaultdict(collections.Counter); top = collections.defaultdict(collections.Counter)
    for run, wn, ev in trio_waves():
        hp = {}
        for e in ev:
            if e["type"] in (7, 8, 9) and e["npc"]["id"] in NAME:
                h = e["npc"]["hitpoints"]; k = e["npc"]["roomId"]
                hp[k] = max(hp.get(k, 0), h >> 16)
                if e["type"] == 7: sp[NAME[e["npc"]["id"]]][e["tick"]] += 1
        for e in ev:
            if e["type"] == 7 and e["npc"]["id"] in NAME: top[NAME[e["npc"]["id"]]][e["npc"]["hitpoints"] & 0xFFFF] += 0
        for e in ev:
            if e["type"] == 7 and e["npc"]["id"] in NAME: top[NAME[e["npc"]["id"]]][("recorded_max", hp[e["npc"]["roomId"]])] += 1
    for k in NAME.values(): print("Q5 spawn tick", k, dist(sp[k]), "| recorded-max hp (asserted start)", {a: b for a, b in top[k].items() if b})


def q_all_mod_and_quartet():
    """Every trio attack tick mod 6 (not just the first); and under Quartet (two room ids of one kind in a wave) whether the pair attacks on the same ticks."""
    mod = collections.defaultdict(collections.Counter); same = collections.Counter()
    for run, wn, ev in trio_waves():
        att = collections.defaultdict(list); kind = {}
        for e in ev:
            if e["type"] == 10 and e["npc"]["id"] in NAME:
                att[e["npc"]["roomId"]].append(e["tick"]); kind[e["npc"]["roomId"]] = e["npc"]["id"]; mod[NAME[e["npc"]["id"]]][e["tick"] % 6] += 1
        byk = collections.defaultdict(list)
        for r, k in kind.items(): byk[k].append(r)
        for k, rs in byk.items():
            if len(rs) == 2:
                a, b = att[rs[0]], att[rs[1]]; same[(NAME[k], "all attack ticks shared" if set(a) & set(b) == set(a) | set(b) else "shared %d of %d/%d" % (len(set(a) & set(b)), len(a), len(b)))] += 1
    for k in NAME.values(): print("Q6 every attack tick mod 6", k, dist(mod[k]))
    print("Q6 two members of one kind in a wave (Quartet)", dict(same))


def q_side():
    """Which side of the player each member stands on at its attack tick (npc tile minus player tile, both at the attack tick; (0,1) north, y grows north).
    'other' is mostly a diagonal tile the player moved off after the npc committed (the cardinal check against either tick is 360 of 360)."""
    side = collections.defaultdict(collections.Counter)
    for run, wn, ev in trio_waves():
        pl = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4}
        pos = {(e["npc"]["roomId"], e["tick"]): (e["xCoord"], e["yCoord"]) for e in ev if e["type"] in (7, 8) and e["npc"]["id"] in NAME}
        att = [e for e in ev if e["type"] == 10 and e["npc"]["id"] in NAME]
        for e in att:
            t = e["tick"]; r = e["npc"]["roomId"]
            if t in pl and (r, t) in pos:
                d = (pos[(r, t)][0] - pl[t][0], pos[(r, t)][1] - pl[t][1])
                side[NAME[e["npc"]["id"]]][{(0, 1): "north", (0, -1): "south", (1, 0): "east", (-1, 0): "west"}.get(d, "other")] += 1
    for k in NAME.values(): print("Q7 side of the player at the attack tick", k, dist(side[k]))


if __name__ == "__main__":
    print("runs", len(runs))
    for q in (q_first_attack, q_phase, q_geometry, q_move, q_spawn_hp, q_all_mod_and_quartet, q_side): q()
