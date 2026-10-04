#!/usr/bin/env python3
"""Queries behind encounters/manticore.tsv: cached Blert sample only, no network.
    python3 docs/minigames/colosseum/encounters/manticore.queries.py [q ...]
Reads sources/blert_api/observed_npc_events.tsv (npc 12818; attack_id 107 mage, 114 range, 115 melee; a burst is up
to three NPC_ATTACK events: the first is anchored to seq 10869, the next two are the plugin's assertion,
blert/PROVENANCE.md section ASSERTED) and the per-wave handicap in the run overviews. A burst start = an attack
event whose instance has no attack event one tick earlier. Mantimayhem = enum 5312 index 0 (struct 915); the
handicap id is index + 30 * (tier - 1) (ids 0, 30, 60 are tiers 1, 2, 3)."""
import collections, csv, glob, json, os, sys
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
rows = [r for r in csv.DictReader(open(os.path.join(API, "observed_npc_events.tsv")), delimiter="\t") if r["npc_id"] == "12818"]
inst = collections.defaultdict(list)
for r in rows:
    inst[(r["run"], int(r["wave"]), r["room_id"])].append(r)
runs = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}
ST = {"107": "M", "114": "R", "115": "L"}


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def held_tier(run, wave):
    """Mantimayhem tier held in force on `wave` (0 = none): choices made up to and including the wave's own record."""
    tier = 0
    for w in runs[run]["overview"]["colosseum"]["waves"]:
        n = w["stage"] - 99
        h = w.get("handicap")
        if n <= wave and h is not None and h % 30 == 0:
            tier = max(tier, h // 30 + 1)
    return tier


def bursts(ev):
    """list of (start_tick, [styles], tile) per burst of one instance."""
    atk = sorted((int(e["tick"]), e["attack_id"], (e["x"], e["y"])) for e in ev if e["kind"] == "attack")
    out = []
    for t, a, xy in atk:
        if out and t - out[-1][3] <= 2 and len(out[-1][1]) < 3:
            out[-1][1].append(ST[a]); out[-1][3] = t
        else:
            out.append([t, [ST[a]], xy, t])
    return out


def q_overview():
    """manticore instances per wave, spawn ticks, bursts."""
    c = collections.Counter(); sp = collections.Counter(); tiles = collections.Counter(); nb = 0
    for (run, w, rid), ev in inst.items():
        c[w] += 1
        for e in ev:
            if e["kind"] == "spawn": sp[(w, int(e["tick"]))] += 1; tiles[(w, e["x"], e["y"])] += 1
        nb += len(bursts(ev))
    print("instances per wave:", dist(c), "bursts", nb)
    print("spawn (wave,tick):", dist(sp)); print("spawn (wave,x,y):", dist(tiles))
    cnt = collections.Counter()
    for run, d in runs.items():
        for w in d["overview"]["colosseum"]["waves"]:
            n = w["stage"] - 99
            k = sum(1 for v in w["npcs"].values() if v["spawnNpcId"] == 12818)
            if k: cnt[(n, k)] += 1
    print("manticores per run-wave (wave,count):", dist(cnt))


def q_gap():
    """gap between consecutive burst STARTS of one instance, all and split by pair/single and Mantimayhem."""
    g = collections.Counter(); g_single = collections.Counter(); g_pair = collections.Counter(); inner = collections.Counter()
    waves = collections.defaultdict(set)
    for (run, w, rid) in inst: waves[(run, w)].add(rid)
    for (run, w, rid), ev in inst.items():
        b = bursts(ev)
        for x, y in zip(b, b[1:]):
            d = y[0] - x[0]; g[d] += 1
            (g_pair if len(waves[(run, w)]) > 1 else g_single)[d] += 1
        for x in b:
            inner[len(x[1])] += 1
    print("burst-start gaps all (n=%d):" % sum(g.values()), dist(g))
    print("single-manticore waves:", dist(g_single)); print("pair waves:", dist(g_pair))
    print("events per burst:", dist(inner))
    print("gap 10 share all: %d/%d" % (g[10], sum(g.values())))


def q_orders():
    """style sequence of each burst: all, by first burst vs later, by wave, by Mantimayhem tier."""
    allc = collections.Counter(); first = collections.Counter(); later = collections.Counter(); wv = collections.defaultdict(collections.Counter)
    tier = collections.defaultdict(collections.Counter); last = collections.Counter(); single_first = collections.Counter()
    waves = collections.defaultdict(set)
    for (run, w, rid) in inst: waves[(run, w)].add(rid)
    for (run, w, rid), ev in inst.items():
        b = bursts(ev)
        for i, x in enumerate(b):
            s = "".join(x[1]); allc[s] += 1; (first if i == 0 else later)[s] += 1
            wv[w][s] += 1; tier[held_tier(run, w)][s] += 1
            if len(x[1]) == 3: last[x[1][2]] += 1
            if i == 0 and len(waves[(run, w)]) == 1 and 4 <= w <= 8: single_first[s] += 1
    print("all bursts:", dist(allc)); print("first burst of an instance:", dist(first)); print("later bursts:", dist(later))
    print("third orb of complete bursts:", dist(last))
    print("first burst, single-manticore waves 4-8:", dist(single_first))
    for w in sorted(wv): print(" wave", w, dist(wv[w]))
    for t in sorted(tier): print(" mantimayhem tier in force", t, dist(tier[t]))


def q_first():
    """first burst start tick of an instance (tick-0 spawn) and tile of the burst."""
    f = collections.Counter(); tiles = collections.Counter(); fw = collections.defaultdict(list)
    for (run, w, rid), ev in inst.items():
        b = bursts(ev)
        sp = [int(e["tick"]) for e in ev if e["kind"] == "spawn"]
        if b and sp: f[b[0][0] - sp[0]] += 1; fw[w].append(b[0][0] - sp[0])
    vals = sorted(f.elements())
    print("first burst start minus spawn tick: n=%d min=%d median=%d max=%d" % (len(vals), vals[0], vals[len(vals) // 2], vals[-1]))
    print(dist(f))
    for w in sorted(fw): print(" wave", w, "n=%d min=%d max=%d" % (len(fw[w]), min(fw[w]), max(fw[w])))
    ph = collections.Counter()
    for (run, w, rid), ev in inst.items():
        b = bursts(ev)
        for x in b: ph[x[0] % 10] += 1
    print("burst start tick mod 10:", dist(ph))


def q_pair():
    """waves with two manticores: offset between each burst start of A and the nearest burst of B, and pattern copy."""
    off = collections.Counter(); same = collections.Counter(); ties = collections.Counter(); n_pairs = 0
    pair_ids = collections.defaultdict(dict)
    for (run, w, rid), ev in inst.items(): pair_ids[(run, w)][rid] = bursts(ev)
    for (run, w), d in sorted(pair_ids.items()):
        if len(d) != 2: continue
        n_pairs += 1
        (ra, ba), (rb, bb) = sorted(d.items())
        allb = sorted([(x[0], ra, "".join(x[1])) for x in ba] + [(x[0], rb, "".join(x[1])) for x in bb])
        for (t0, i0, s0), (t1, i1, s1) in zip(allb, allb[1:]):
            if i0 != i1:
                off[t1 - t0] += 1
                same[(t1 - t0 <= 8, s0 == s1)] += 1
            else:
                ties[t1 - t0] += 1
    print("pair run-waves", n_pairs)
    print("gap between a burst start and the next burst start of the OTHER manticore:", dist(off))
    print("(within 8 ticks?, same order as the previous other-manticore burst?):", dist(same))
    print("gap between same-manticore starts with the other in between excluded (consecutive same id):", dist(ties))


def q_tiles():
    tl = collections.Counter()
    for (run, w, rid), ev in inst.items():
        for x in bursts(ev): tl[x[2]] += 1
    print("distinct burst tiles", len(tl), "top", tl.most_common(8))


def q_persist():
    """does an instance keep one order or redraw each burst? distinct complete orders per instance, and the order
    after a complete burst against the one before (first sample: the manticore's own previous complete burst)."""
    per = collections.Counter(); trans = collections.Counter(); by_wave = collections.defaultdict(collections.Counter)
    for (run, w, rid), ev in inst.items():
        comp = ["".join(x[1]) for x in bursts(ev) if len(x[1]) == 3]
        if len(comp) < 2: continue
        d = len(set(comp)); per[(min(len(comp), 5), d)] += 1
        for a, b in zip(comp, comp[1:]): trans[(a, b)] += 1
        by_wave[w][d] += 1
    print("(complete bursts capped at 5, distinct orders):", dist(per))
    print("previous -> next complete order:", dist(trans))
    for w in sorted(by_wave): print(" wave", w, "instances with 1 / 2 distinct orders:", dist(by_wave[w]))



def q_paircopy():
    """per pair run-wave: the one order of each manticore (an instance keeps one order, q_persist) -- equal or not;
    independence would give half. Also single-instance first orders by wave group (draw odds per INSTANCE)."""
    c = collections.Counter(); per = collections.Counter(); who = collections.Counter()
    pair = collections.defaultdict(dict)
    for (run, w, rid), ev in inst.items():
        comp = ["".join(x[1]) for x in bursts(ev) if len(x[1]) == 3]
        if comp: per[("single" if False else w >= 9, comp[0])] += 1
        pair[(run, w)][rid] = comp[0] if comp else None
    for (run, w), d in pair.items():
        if len(d) == 2 and all(d.values()):
            a, b = sorted(d)
            c[(d[a] == d[b], d[a], d[b])] += 1
    print("pair run-waves (equal?, order A, order B):", dist(c))
    print("instances by (wave>=9, order):", dist(per))


def _player_tiles():
    out = {}
    for run, d in runs.items():
        for w, evs in d["waves"].items():
            for e in evs:
                if e["type"] == 4: out[(run, int(w), e["tick"])] = (e["xCoord"], e["yCoord"])
    return out


def q_range():
    """Chebyshev distance from the manticore's 3x3 footprint (the recorder's tile is its south-west tile) to the
    player's tile on each burst-start tick (PLAYER_UPDATE is observed): range 15 (wiki, SUPA)."""
    pt = _player_tiles(); dc = collections.Counter(); miss = 0; ge = collections.Counter()
    for (run, w, rid), ev in inst.items():
        for x in bursts(ev):
            p = pt.get((run, w, x[0]))
            if p is None: miss += 1; continue
            mx, my = int(x[2][0]), int(x[2][1])
            dx = max(mx - p[0], p[0] - (mx + 2), 0); dy = max(my - p[1], p[1] - (my + 2), 0)
            d = max(dx, dy); dc[d] += 1
    print("burst-start distance footprint->player (n=%d, no player row: %d):" % (sum(dc.values()), miss), dist(dc))


def q_cross():
    """gaps between consecutive burst starts of two different manticores of one pair wave; totals and shares;
    own-gap totals in pair waves; first-burst offset between the two instances."""
    cross = collections.Counter(); own = collections.Counter(); first = collections.Counter()
    pair = collections.defaultdict(dict)
    for (run, w, rid), ev in inst.items(): pair[(run, w)][rid] = bursts(ev)
    for (run, w), d in pair.items():
        if len(d) != 2: continue
        allb = sorted((x[0], rid) for rid, b in d.items() for x in b)
        for (t0, i0), (t1, i1) in zip(allb, allb[1:]):
            (cross if i0 != i1 else own)[t1 - t0] += 1
        for rid, b in d.items():
            for x, y in zip(b, b[1:]): pass
        fs = sorted(b[0][0] for b in d.values() if b)
        if len(fs) == 2: first[fs[1] - fs[0]] += 1
    print("cross gaps n=%d, =5: %d, 5 or 10: %d" % (sum(cross.values()), cross[5], cross[5] + cross[10]))
    print("cross <=15:", dist({k: v for k, v in cross.items() if k <= 15}))
    print("first bursts of the two manticores, start offset:", dist(first))
    own_gap = collections.Counter()
    for (run, w), d in pair.items():
        if len(d) != 2: continue
        for rid, b in d.items():
            for x, y in zip(b, b[1:]): own_gap[y[0] - x[0]] += 1
    print("own-gap in pair waves n=%d: =10 %d, =15 %d, <10 %d" % (sum(own_gap.values()), own_gap[10], own_gap[15], sum(v for k, v in own_gap.items() if k < 10)))


def q_copy_rule():
    """pairs where both manticores burst: split by whether the later-charging one fired its first burst while the
    earlier one was still alive (its despawn tick is later than the second's first burst). Wiki: the second copies
    the first's pattern when it sees the player while the first is alive; if the first is killed first the second
    picks its own pattern (Fortis_Colosseum_Strategies:824). NPC_DEATH is a despawn, so a death tick is an upper
    bound on the kill."""
    c = collections.Counter(); pair = collections.defaultdict(dict)
    for (run, w, rid), ev in inst.items(): pair[(run, w)][rid] = ev
    for (run, w), d in sorted(pair.items()):
        if len(d) != 2: continue
        info = []
        for rid, ev in sorted(d.items()):
            b = bursts(ev); comp = ["".join(x[1]) for x in b if len(x[1]) == 3]
            de = [int(e["tick"]) for e in ev if e["kind"] == "death"]
            if comp: info.append((b[0][0], comp[0], de[0] if de else 10 ** 6))
        if len(info) != 2: continue
        info.sort(); (t1, o1, d1), (t2, o2, d2) = info
        c[("overlap" if t2 < d1 else "first_dead_before_second_burst", o1 == o2)] += 1
    print("(overlapping lives?, same order?):", dist(c))


def q_move():
    """movement of the manticore from NPC_UPDATE (observed, every tick, type 8, npc 12818): per tick, did the
    south-west tile change; step size; and by phase relative to the nearest earlier burst start (0-2 the three
    orb ticks, 3-9 the recharge, none = before the first burst). Distance moved in a tick is Chebyshev."""
    mv = collections.Counter(); step = collections.Counter(); ph = collections.defaultdict(collections.Counter)
    starts = collections.defaultdict(list)
    for (run, w, rid), ev in inst.items(): starts[(run, w, int(rid))] = [x[0] for x in bursts(ev)]
    for run, d in runs.items():
        last = {}
        for w, evs in d["waves"].items():
            last = {}
            for e in evs:
                if e["type"] != 8 or e.get("npc", {}).get("id") != 12818: continue
                rid = e["npc"]["roomId"]; t = e["tick"]; xy = (e["xCoord"], e["yCoord"])
                if rid in last and last[rid][0] == t - 1:
                    dd = max(abs(xy[0] - last[rid][1][0]), abs(xy[1] - last[rid][1][1]))
                    st = [b for b in starts.get((run, int(w), rid), []) if b <= t]
                    k = (t - st[-1]) if st else -1
                    key = "pre" if k < 0 else ("burst" if k <= 2 else "recharge")
                    ph[key][dd > 0] += 1; step[dd] += 1
                last[rid] = (t, xy)
    print("step size per tick (Chebyshev):", dist(step))
    for k in ph: print(" phase", k, "moved %d of %d" % (ph[k][True], ph[k][True] + ph[k][False]))

if __name__ == "__main__":
    want = sys.argv[1:] or ["overview", "gap", "orders", "first", "pair", "tiles", "persist", "paircopy", "range", "cross", "copy_rule", "move"]
    for q in want:
        print("== " + q); globals()["q_" + q]()
