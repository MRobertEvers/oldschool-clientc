#!/usr/bin/env python3
"""nibbler_pillar_analysis -- what Blert's cached Inferno recordings OBSERVE about the nibblers and
the rocky supports (spec table nibblers_and_pillars.tsv). Reads build/corpus_tmp/blert_api/*.json (the
big cache verify_blert.py fills; 19 completed or late-failed challenges) and
sources/blert_api/observed_npc_events.tsv. Observed only (blert/PROVENANCE.md section 1): npc 7709
spawn/update rows (position, packed hitpoints = current<<16 | base), npc 7691 rows, npc deaths (despawns).
    python3 docs/minigames/inferno/sources/blert_api/nibbler_pillar_analysis.py > <log>
"""
import collections, glob, json, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), *[".."] * 5))
files = [f for f in sorted(glob.glob(os.path.join(ROOT, "build/corpus_tmp/blert_api/*.json")))
         if "waves" in json.load(open(f))]
D = collections.Counter(); gaps = collections.Counter(); firsts = []; npill = collections.Counter()
step = collections.Counter(); adj = collections.Counter(); posset = collections.Counter(); hp0 = collections.Counter()
nwave = 0; end_delay = []; share = collections.Counter(); despawn = collections.Counter(); moved = 0; collapse_state = collections.Counter()
HOME = ((2257, 5349), (2267, 5335), (2274, 5351))
for f in files:
    d = json.load(open(f))
    for w, ev in d["waves"].items():
        nwave += 1
        pill = {}; nibs = collections.defaultdict(dict); hp = collections.defaultdict(dict)
        for e in ev:
            if e["type"] in (7, 8):
                i = e["npc"]["id"]
                if i == 7709:
                    pill[e["npc"]["roomId"]] = (e["xCoord"], e["yCoord"]); posset[(e["xCoord"], e["yCoord"])] += 1
                    hp[e["npc"]["roomId"]][e["tick"]] = e["npc"]["hitpoints"]
                elif i == 7691:
                    nibs[e["npc"]["roomId"]][e["tick"]] = (e["xCoord"], e["yCoord"])
        ft = None; dm = set()
        for rid, tk in hp.items():
            ts = sorted(tk); hp0[tk[ts[0]] >> 16] += 1 if ts[0] == 0 else 0; pt = None
            for a, b in zip(ts, ts[1:]):
                if (tk[b] >> 16) < (tk[a] >> 16):
                    D[(tk[a] >> 16) - (tk[b] >> 16)] += 1; dm.add(rid)
                    ft = b if ft is None or b < ft else ft
                    if pt is not None: gaps[b - pt] += 1
                    pt = b
                    best = 99
                    for n, nt in nibs.items():
                        for tt in (a, b):
                            if tt in nt:
                                x, y = nt[tt]; px, py = pill[rid]
                                best = min(best, max(max(px - x, 0, x - (px + 2)), max(py - y, 0, y - (py + 2))))
                    adj[best] += 1
        okp = [r for r in pill if all(p in HOME for p in [pill[r]])]
        for rid, tk in hp.items():
            if len({p for p in [pill[rid]]}) > 1: moved += 1
        if len(okp) == 3:
            for rid in okp:
                ts = sorted(hp[rid])
                if any((hp[rid][b] >> 16) < (hp[rid][a] >> 16) for a, b in zip(ts, ts[1:])): share[pill[rid]] += 1
        dth = {}
        nhp = collections.defaultdict(dict)
        for e in ev:
            if e["type"] in (7, 8) and e["npc"]["id"] == 7691: nhp[e["npc"]["roomId"]][e["tick"]] = e["npc"]["hitpoints"] >> 16
            if e["type"] == 9 and e["npc"]["id"] == 7691: dth[e["npc"]["roomId"]] = e["tick"]
        for rid, t0 in dth.items():
            z = [x for x in sorted(nhp[rid]) if x <= t0 and nhp[rid][x] == 0]
            if z: despawn[t0 - z[0]] += 1
        if ft is not None: firsts.append(ft)
        if dm: npill[len(dm)] += 1
        for rid, tk in nibs.items():
            ts = sorted(tk)
            for a, b in zip(ts, ts[1:]):
                if b == a + 1: step[max(abs(tk[b][0] - tk[a][0]), abs(tk[b][1] - tk[a][1]))] += 1
        if w == "66":
            pd = [e["tick"] for e in ev if e["type"] == 9 and e["npc"]["id"] == 7709]
            od = [e["tick"] for e in ev if e["type"] == 9 and e["npc"]["id"] != 7709]
            last = max(od) if od else None
            if pd and last is not None and max(pd) == last: end_delay.append(0)
            elif pd and last is not None and len(pd) >= 1 and max(pd) != last: end_delay.append(last - max(pd))
print("challenges", len(files), "waves", nwave)
print("pillar positions seen (updates)", dict(posset))
print("pillar hp per hitsplat tick-delta (1..4 each ~25%; 5+ = several nibblers in one tick)", sorted(D.items()))
print("gap in ticks between hp-loss ticks of one pillar", sorted(gaps.items()))
print("first hp-loss tick since the wave message (n=%d)" % len(firsts), sorted(collections.Counter(firsts).items()))
print("waves by number of DISTINCT pillars damaged", sorted(npill.items()))
print("nibbler step per tick, Chebyshev", sorted(step.items()))
print("min Chebyshev distance nibbler to the damaged pillar's 3x3 footprint at an hp-loss tick", sorted(adj.items()))
print("wave 66: pillar-death tick minus last despawn tick of the wave (0 = same tick; blank if no pillar alive at the end)", sorted(collections.Counter(end_delay).items()))
print("pillar chosen (waves where all three stood, n=%d) by tile W(2257,5349) S(2267,5335) E(2274,5351)" % sum(share.values()), dict(share))
print("nibbler despawn tick minus first tick with hp 0", sorted(despawn.items()))
