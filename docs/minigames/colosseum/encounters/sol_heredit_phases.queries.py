#!/usr/bin/env python3
"""Queries behind encounters/sol_heredit_phases.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/sol_heredit_phases.queries.py > build/logs/sol_phases_q.txt
Wave '12' streams of sources/blert_api/*.json. Sol hitpoints = NPC_UPDATE(8) npc.hitpoints of 12821, packed
(current << 16 | base), the damage taken is observed (PROVENANCE:53) and the 1500 base is the cache's.
Phase transitions = a pools event (206) listing >= 5 tiles more than 8 ticks after the previous one.
Questions: M22 thresholds, M23 laser cadence, M24 pools, M27 enrage, M38 wave start."""
import collections, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
streams = []
for f in sorted(glob.glob(os.path.join(API, "*.json"))):
    d = json.load(open(f)); ev = (d.get("waves") or {}).get("12")
    if ev:
        streams.append((os.path.basename(f)[:8], ev))
def dist(c): return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))
THR = [1350, 1125, 750, 375, 150]
print("streams", len(streams))
hpat = collections.defaultdict(list)       # hp at transition k
first_below = collections.defaultdict(list) # tick-to-trans lag
trans_tiles = collections.defaultdict(collections.Counter)
laser_phase_first = collections.defaultdict(list)
scan_shot = collections.defaultdict(collections.Counter)
shot_gap = collections.defaultdict(collections.Counter)
final_gaps = collections.Counter(); final_n = []
single_pool_per_phase = collections.defaultdict(collections.Counter)
lasers_per_phase = collections.defaultdict(collections.Counter)
pool_dmg = collections.Counter()
death = []; last_hp = []
prism_ids = collections.Counter(); sol_tick = []
retype = collections.Counter()
for sid, ev in streams:
    hp = {}
    for e in ev:
        if e["type"] in (7, 8) and e["npc"]["id"] == 12821:
            hp[e["tick"]] = e["npc"]["hitpoints"] >> 16
        if e["type"] == 8 and e["npc"]["id"] in (12827,): retype["12827 update"] += 1
    ticks = sorted(hp)
    def hp_at(t):
        c = [x for x in ticks if x <= t]; return hp[c[-1]] if c else None
    pools = sorted((e["tick"], len(e["colosseumSolPools"]["pools"])) for e in ev if e["type"] == 206)
    trans = []; last = -99
    for t, n in pools:
        if n >= 5 and t - last > 8: trans.append(t)
        if n >= 5: last = t
    for k, t in enumerate(trans):
        hpat[k + 1].append(hp_at(t - 1))
        trans_tiles[k + 1][[n for tt, n in pools if tt == t][0]] += 1
        # tick hp first <= threshold
        if k < 5:
            below = [x for x in ticks if hp[x] <= THR[k]]
            if below: first_below[k + 1].append(t - below[0])
    ph = lambda t: sum(1 for x in trans if x <= t)
    lasers = sorted((e["tick"], e["colosseumSolLasers"]["phase"]) for e in ev if e["type"] == 207)
    for t, p in lasers: lasers_per_phase[ph(t)][p] += 1
    scans = [t for t, p in lasers if p == 0]; shots = [t for t, p in lasers if p == 1]
    for s in scans:
        nx = [x for x in shots if x > s]
        if nx: scan_shot[ph(s)][nx[0] - s] += 1
    # shot to next shot / scan to next scan
    prev = None
    for s in scans:
        if prev is not None and s - prev > 1: shot_gap[ph(s)][s - prev] += 1
        prev = s
    if lasers: laser_phase_first[ph(lasers[0][0])].append(lasers[0][0])
    # final phase pools: gaps after the 5th transition
    if len(trans) >= 5:
        fp = [t for t, n in pools if t > trans[4] + 8]
        g = [b - a for a, b in zip(fp, fp[1:])]
        final_gaps.update(g); final_n.append(len(fp))
    for t, n in pools:
        if n < 5 and not (trans and any(t - x == 0 for x in trans)):
            single_pool_per_phase[ph(t)][n] += 1
    if hp: last_hp.append((hp[ticks[-1]], ticks[-1]))
    for e in ev:
        if e["type"] == 9 and e["npc"]["id"] == 12821: death.append(e["tick"])
    sol_tick.append(min(ticks) if ticks else None)
    first7 = [e["tick"] for e in ev if e["type"] == 7]
    for e in ev:
        if e["type"] == 7: prism_ids[e["npc"]["id"]] += 1
print("transitions per stream", [len([1 for _ in []])] and "")
for k in sorted(hpat): print("trans", k, "hp the tick before the pools event:", sorted(x for x in hpat[k] if x is not None), "tiles", dist(trans_tiles[k]))
for k in sorted(first_below): print("trans", k, "lag pools tick minus first tick hp<=%d:" % THR[k - 1], sorted(first_below[k]))
print("laser events per phase index (0 scan 1 shot)", {k: dict(v) for k, v in sorted(lasers_per_phase.items())})
print("scan->shot by phase", {k: dist(v) for k, v in sorted(scan_shot.items())})
print("scan->scan gap by phase", {k: dist(v) for k, v in sorted(shot_gap.items())})
print("final phase (after 5th transition) pool gaps", dist(final_gaps), "events per stream", final_n)
print("non-transition pool events by phase: tiles per event", {k: dist(v) for k, v in sorted(single_pool_per_phase.items())})
print("sol last hp,tick", last_hp, "death", death)
print("first sol tick", sol_tick)
print("12827 updates", retype["12827 update"], "spawned npc ids", dict(prism_ids))
print("---- part 2: single-tile pool cadence per phase, crystal spawn lag, wave end")
gp = collections.defaultdict(collections.Counter); nper = collections.defaultdict(list)
crys = collections.defaultdict(list); crys_pos = []; first_pool_after = collections.defaultdict(list)
endlag = []; modlast = []
for sid, ev in streams:
    pools = sorted((e["tick"], len(e["colosseumSolPools"]["pools"])) for e in ev if e["type"] == 206)
    trans = []; last = -99
    for t, n in pools:
        if n >= 5 and t - last > 8: trans.append(t)
        if n >= 5: last = t
    ph = lambda t: sum(1 for x in trans if x <= t)
    singles = [(t, n) for t, n in pools if n < 5]
    for k in range(0, 6):
        s = [t for t, n in singles if ph(t) == k]
        nper[k].append(len(s))
        for a, b in zip(s, s[1:]): gp[k][b - a] += 1
        if k >= 1 and s and k <= len(trans): first_pool_after[k].append(s[0] - trans[k - 1])
    for e in ev:
        if e["type"] == 7 and e["npc"]["id"] == 12824:
            crys[ph(e["tick"])].append(e["tick"] - (trans[ph(e["tick"]) - 1] if ph(e["tick"]) else 0))
    ends = [e["tick"] for e in ev if e["type"] in (1, 2, 3)]
    endlag.append((max(e["tick"] for e in ev), trans[-1] if trans else None))
print("single-tile pool gaps by phase", {k: dist(v) for k, v in sorted(gp.items())})
print("single-tile pool events per stream per phase", {k: v for k, v in sorted(nper.items())})
print("first single pool tick minus its phase's transition tick", {k: sorted(v) for k, v in sorted(first_pool_after.items())})
print("crystal 12824 spawn tick minus phase's transition pools tick (phase index)", {k: sorted(v) for k, v in sorted(crys.items())})
print("last event tick, last transition tick", endlag)
print("---- part 3: laser details")
rows = []; firstlag = []; shotsper = collections.Counter(); perfrom = collections.defaultdict(list)
for sid, ev in streams:
    pools = sorted((e["tick"], len(e["colosseumSolPools"]["pools"])) for e in ev if e["type"] == 206)
    trans = []; last = -99
    for t, n in pools:
        if n >= 5 and t - last > 8: trans.append(t)
        if n >= 5: last = t
    ph = lambda t: sum(1 for x in trans if x <= t)
    hpm = {}
    for e in ev:
        if e["type"] == 8 and e["npc"]["id"] == 12821: hpm[e["tick"]] = e["npc"]["hitpoints"] >> 16
    L = sorted((e["tick"], e["colosseumSolLasers"]["phase"]) for e in ev if e["type"] == 207)
    sc = [t for t, p in L if p == 0]; sh = [t for t, p in L if p == 1]
    if sc and trans: firstlag.append(sc[0] - trans[0])
    # distinct scan events (runs of consecutive ticks) and shots
    for s in sc:
        nx = [x for x in sh if x > s]
        if nx:
            k = ph(s)
            if k in (3, 4, 5):
                tt = max([x for x in hpm if x <= s] or [0]); rows.append((k, nx[0] - s, s - trans[k - 1], hpm.get(tt)))
    for t, p in L: perfrom[ph(t)].append(t - trans[ph(t) - 1] if ph(t) else None)
print("first scan tick minus first transition tick", sorted(firstlag))
print("(phase, scan->shot, ticks since this phase's transition, sol hp at scan) for phases 3-5:", sorted(rows)[:40])
print("---- part 4: by Sol hitpoints (hp<=150 = enrage), no transition detection")
ss = collections.defaultdict(collections.Counter); ssgap = collections.defaultdict(collections.Counter)
epool = collections.Counter(); egap = collections.Counter(); efirst = []; enr_scans = collections.Counter(); first_enrage_event = []
first_pool_tiles = collections.Counter(); crys_enr = 0; crys_after_enrage = 0; ev_before_enr = []
for sid, ev in streams:
    hpm = {}
    for e in ev:
        if e["type"] == 8 and e["npc"]["id"] == 12821: hpm[e["tick"]] = e["npc"]["hitpoints"] >> 16
    def H(t):
        c = [x for x in hpm if x <= t]; return hpm[max(c)] if c else 1500
    cross = [t for t in sorted(hpm) if hpm[t] <= 150]
    t150 = cross[0] if cross else None
    L = sorted((e["tick"], e["colosseumSolLasers"]["phase"]) for e in ev if e["type"] == 207)
    sc = [t for t, p in L if p == 0]; sh = [t for t, p in L if p == 1]
    prev = None
    for s in sc:
        nx = [x for x in sh if x > s]
        k = "enrage" if H(s) <= 150 else "above"
        if nx: ss[k][nx[0] - s] += 1
        if prev is not None and s - prev > 1: ssgap[k][s - prev] += 1
        prev = s
    pools = sorted((e["tick"], len(e["colosseumSolPools"]["pools"])) for e in ev if e["type"] == 206)
    pe = [(t, n) for t, n in pools if t150 is not None and t >= t150]
    if pe:
        efirst.append(pe[0][0] - t150); first_pool_tiles[pe[0][1]] += 1
        rest = [t for t, n in pe[1:]]
        for a, b in zip(rest, rest[1:]): egap[b - a] += 1
        for t, n in pe[1:]: epool[n] += 1
        ev_before_enr.append(len(pe))
print("scan->shot by hp", {k: dist(v) for k, v in ss.items()})
print("scan->next scan (gap>1) by hp", {k: dist(v) for k, v in ssgap.items()})
print("enrage: first pools event minus first tick hp<=150", sorted(efirst), "tiles of first", dist(first_pool_tiles))
print("enrage: later pools events tiles", dist(epool), "gaps between them", dist(egap), "events per stream", ev_before_enr)
