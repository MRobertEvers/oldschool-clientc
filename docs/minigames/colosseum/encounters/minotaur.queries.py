#!/usr/bin/env python3
"""Queries behind encounters/minotaur.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/minotaur.queries.py
NPC_UPDATE (type 8) carries every tracked npc's position and hitpoints each tick; hitpoints are packed
(high 16 bits current, low 16 bits base: 3276850 = 50<<16|50). A rise of current between two ticks of one
roomId is a HEAL hitsplat (blert/PROVENANCE.md section 1: boost(amount)). The minotaur's heal animation 10844
is NOT in the recorder's table, so the heal TICK is the hitsplat's, never the cast animation's. Sizes from
sources/cache_npc.txt: minotaur 3, jaguar 2, others here 1 (javelin, manticore, shockwave 3)."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
SIZE = {12812: 3, 12813: 3, 12810: 2, 12811: 1, 12814: 1, 12815: 1, 12816: 1, 12817: 3, 12818: 3, 12819: 3, 12821: 5, 12824: 1, 12825: 1}
MINO = (12812, 12813)
rows = [r for r in csv.DictReader(open(os.path.join(API, "observed_npc_events.tsv")), delimiter="\t") if r["npc_id"] in ("12812", "12813")]
inst = collections.defaultdict(list)
for r in rows:
    inst[(r["run"], r["wave"], r["room_id"])].append(r)


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def _runs():
    return {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}


def q_gaps():
    g = collections.Counter(); ids = collections.Counter(); n = 0
    for k, ev in inst.items():
        atk = sorted(int(e["tick"]) for e in ev if e["kind"] == "attack"); n += len(atk)
        for e in ev:
            if e["kind"] == "attack": ids[(e["npc_id"], e["attack_id"])] += 1
        for a, b in zip(atk, atk[1:]): g[b - a] += 1
    print("instances", len(inst), "attacks", n, "attack ids:", dist(ids)); print("gap between consecutive attacks:", dist(g))


def q_spawn():
    t = collections.Counter(); tiles = collections.Counter(); wave = collections.Counter(); per = collections.Counter(); ids = collections.Counter()
    for k, ev in inst.items():
        wave[k[1]] += 1; per[(k[0], k[1])] += 1
        for e in ev:
            if e["kind"] == "spawn": t[int(e["tick"])] += 1; tiles[(e["x"], e["y"])] += 1; ids[e["npc_id"]] += 1
    print("spawn ticks:", dist(t)); print("spawn tiles:", dist(tiles)); print("instances by wave:", dist(wave))
    print("ids:", dist(ids)); print("minotaurs per (run,wave):", dist(collections.Counter(per.values())))


def q_movement():
    step = collections.Counter(); ticks = 0; first = collections.Counter()
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            pos = collections.defaultdict(dict)
            for e in ev:
                if e["type"] in (7, 8) and e["npc"]["id"] in MINO: pos[e["npc"]["roomId"]][e["tick"]] = (e["xCoord"], e["yCoord"])
            for r, tp in pos.items():
                t0 = min(tp)
                if t0 + 1 in tp:
                    a, b = tp[t0], tp[t0 + 1]; first[max(abs(a[0] - b[0]), abs(a[1] - b[1]))] += 1
                for t, (x, y) in tp.items():
                    if t - 1 in tp: q = tp[t - 1]; step[max(abs(x - q[0]), abs(y - q[1]))] += 1; ticks += 1
    print("step per tick (all ticks):", dist(step), "ticks", ticks); print("step on the tick after the spawn tick:", dist(first))


def q_range():
    c = collections.Counter(); n = 0; shape = collections.Counter()
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            ppos = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4 and e.get("player")}
            for e in ev:
                if e["type"] == 10 and e["npc"]["id"] in MINO and e["tick"] in ppos:
                    px, py = ppos[e["tick"]]; nx, ny = e["xCoord"], e["yCoord"]
                    dx = max(nx - px, 0, px - (nx + 2)); dy = max(ny - py, 0, py - (ny + 2)); c[max(dx, dy)] += 1; n += 1
                    if max(dx, dy) == 1: shape["diagonal" if (dx and dy) else "cardinal"] += 1
    print("attacks with a player tile:", n, "distance (Chebyshev to the 3x3 footprint):", dist(c)); print("at distance 1:", dist(shape))


def q_heal():
    """Heals on non-minotaur tracked npcs while a minotaur is alive. A heal = current hitpoints rose between ticks."""
    pre = []; post_full = collections.Counter(); dis = collections.Counter(); plr = collections.Counter()
    wavec = collections.Counter(); amount = collections.Counter(); gaps = collections.Counter(); target = collections.Counter()
    nheal = 0; totem_ticks = 0; fulls = []; tick_after = []
    for run, d in _runs().items():
        for wn, ev in d["waves"].items():
            if int(wn) not in (7, 8, 9, 10, 11): continue
            totem = {e["tick"] for e in ev if e["type"] == 202}
            hp = collections.defaultdict(dict); pos = {}; nid = {}; ppos = {}
            minos = collections.defaultdict(dict)
            for e in ev:
                if e["type"] == 4 and e.get("player"): ppos[e["tick"]] = (e["xCoord"], e["yCoord"])
                if e["type"] in (7, 8, 9):
                    n = e["npc"]; r = n["roomId"]; nid[r] = n["id"]
                    if n["id"] in MINO and e["type"] != 9: minos[e["tick"]][r] = (e["xCoord"], e["yCoord"])
                    if e["type"] != 9 or True:
                        if "hitpoints" in n: hp[r][e["tick"]] = n["hitpoints"]
                    pos[(r, e["tick"])] = (e["xCoord"], e["yCoord"])
            last_heal = {}
            for r, th in hp.items():
                if nid[r] in MINO: continue
                for t, v in sorted(th.items()):
                    if t - 1 not in th: continue
                    cur, base = v >> 16, v & 0xFFFF; pc = th[t - 1] >> 16
                    if cur <= pc or not base: continue
                    if t not in minos and (t - 1) not in minos: continue
                    if (t in totem) or (t - 1 in totem): totem_ticks += 1; continue
                    m = minos.get(t) or minos.get(t - 1)
                    mr, (mx, my) = min(m.items(), key=lambda kv: max(abs(kv[1][0] + 1 - (pos[(r, t)][0] + (SIZE.get(nid[r], 1) - 1) / 2.0)), abs(kv[1][1] + 1 - (pos[(r, t)][1] + (SIZE.get(nid[r], 1) - 1) / 2.0))))
                    cx = pos[(r, t)][0] + (SIZE.get(nid[r], 1) - 1) / 2.0; cy = pos[(r, t)][1] + (SIZE.get(nid[r], 1) - 1) / 2.0
                    cd = max(abs(mx + 1 - cx), abs(my + 1 - cy))
                    nheal += 1; wavec[wn] += 1; target[nid[r]] += 1
                    pre.append(round(pc / base, 3)); post_full["full" if cur == base else "partial"] += 1; fulls.append(cur == base); tick_after.append(t - 66)
                    dis[int(cd) if cd == int(cd) else cd] += 1; amount[cur - pc] += 1
                    if t in ppos:
                        px, py = ppos[t]; dx = max(mx - px, 0, px - (mx + 2)); dy = max(my - py, 0, py - (my + 2)); plr[max(dx, dy)] += 1
                    key = mr
                    if key in last_heal: gaps[t - last_heal[key]] += 1
                    last_heal[key] = t
    print("heal rises on non-minotaur npcs (totem ticks excluded: %d):" % totem_ticks, nheal)
    print("healed npc id:", dist(target)); print("by wave:", dist(wavec))
    full = [x for x in pre]; print("pre-heal fraction of base, sorted:", sorted(pre))
    print("pre-heal fraction, heals that reached full: max %.3f of %d" % (max(f for f, ok in zip(pre, fulls) if ok), sum(fulls)))
    print("heal tick minus spawn tick (66):", dist(collections.Counter(tick_after)))
    print("heals with the player 1 tile or less from the footprint: %d of %d" % (sum(plr[k] for k in plr if k <= 1), sum(plr.values()))); print("post-heal:", dist(post_full))
    print("centre-to-centre Chebyshev distance to nearest minotaur:", dist(dis))
    print("player Chebyshev distance to nearest minotaur footprint:", dist(plr)); print("heal amounts (hp):", dist(amount))
    print("gaps between heals attributed to one minotaur:", dist(gaps))


def q_redflag():
    """Red Flag is handicap 13 (index in cache enum 5312, struct_907). Handicaps persist, so a run holds Red Flag from the
    wave it was picked (wave_records.tsv handicap_id, the chosen one) to the end. Count minotaur spawns by id and Red Flag held."""
    chosen = collections.defaultdict(dict)
    for r in csv.DictReader(open(os.path.join(API, "wave_records.tsv")), delimiter="\t"): chosen[r["run"]][int(r["wave"])] = r["handicap_id"]
    c = collections.Counter()
    for k, ev in inst.items():
        held = "13" in [chosen[k[0]].get(w) for w in range(1, int(k[1]) + 1)]
        for e in ev:
            if e["kind"] == "spawn": c[("red_flag_held" if held else "no_red_flag", e["npc_id"])] += 1
    print("minotaur spawns by (Red Flag held, npc id):", dist(c))


if __name__ == "__main__":
    for q in (q_gaps, q_spawn, q_movement, q_range, q_heal, q_redflag):
        print("--", q.__name__); q()
