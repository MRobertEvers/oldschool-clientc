#!/usr/bin/env python3
"""Triple Jad (wave 68) from the wave-68 event streams of the cached Blert runs -> TRIPLE_JAD_ANALYSIS.txt.
Input: build/corpus_tmp/blert_api/<uuid>.json (not in git; uuids in FETCH_LOG.tsv). OBSERVED rows only (PROVENANCE.md).
Tick 0 = the `Wave: 68` chat tick. hitpoints packed (current<<16)|base. Attack ids: 84 ranged, 85 magic, 86 melee, 87 healer."""
import json, glob, os, collections
D = os.path.dirname(os.path.abspath(__file__)) + '/'
CACHE = D + '../../../../../build/corpus_tmp/blert_api/'
NAME = {84: 'R', 85: 'M', 86: 'X'}
def srt(a): return sorted(a)
runs = {}
for f in sorted(glob.glob(CACHE + '*.json')):
    d = json.load(open(f))
    if isinstance(d, dict) and d.get('waves', {}).get('68'):
        runs[d['uuid'][:8]] = (d['waves']['68'], d['overview'])
print('runs with wave 68 streams: %d' % len(runs))
spawn_tiles = collections.Counter(); njad = collections.Counter(); first_by_tile = collections.defaultdict(list)
gaps = collections.Counter(); gap_after = collections.defaultdict(collections.Counter)
sty = collections.Counter(); trans = collections.Counter(); mel_adj = collections.Counter(); nonmel_adj = collections.Counter()
hs_per_jad = collections.Counter(); hs_rel = collections.Counter(); hs_hp_prev = []; hs_hp = []; hs_tick = collections.defaultdict(list)
death_ticks = collections.defaultdict(list); death_order = collections.Counter(); death_gap = []
hattack = collections.Counter(); same_tick = collections.Counter(); moved = collections.Counter()
dur = []; pill = 0; healers_alive_at_death = []; hp_zero_to_despawn = []; heal_inc = 0
firsts_all = []; healer_first_group = []
for run, (ev, ov) in runs.items():
    jads = [e for e in ev if e['type'] == 7 and e['npc']['id'] == 7700]
    njad[len(jads)] += 1
    rid_tile = {}
    for e in jads:
        spawn_tiles[(e['xCoord'], e['yCoord'])] += 1; rid_tile[e['npc']['roomId']] = (e['xCoord'], e['yCoord'])
    pill += sum(1 for e in ev if e['type'] == 7 and e['npc']['id'] == 7709)
    att = collections.defaultdict(list)
    for e in ev:
        if e['type'] == 10 and e['npc']['id'] == 7700: att[e['npc']['roomId']].append(e)
    ply = collections.defaultdict(list)
    for e in ev:
        if e['type'] == 4: ply[e['tick']].append((e['xCoord'], e['yCoord']))
    upd = collections.defaultdict(dict)
    for e in ev:
        if e['type'] == 8 and e['npc']['id'] == 7700: upd[e['npc']['roomId']][e['tick']] = e
    atk_ticks = collections.Counter()
    firsts = []
    for rid, lst in att.items():
        lst.sort(key=lambda e: e['tick'])
        t0 = rid_tile.get(rid)
        first_by_tile[t0].append(lst[0]['tick']); firsts.append(lst[0]['tick'])
        prev = None
        for e in lst:
            atk_ticks[e['tick']] += 1
            a = e['npcAttack']['attack']; s = NAME[a]; sty[s] += 1
            if prev is not None:
                g = e['tick'] - prev['tick']; gaps[g] += 1; gap_after[NAME[prev['npcAttack']['attack']]][g] += 1
                if prev['npcAttack']['attack'] != 86 and a != 86: trans[(NAME[prev['npcAttack']['attack']], s)] += 1
            prev = e
            ps = ply.get(e['tick'], [])
            if ps:
                px, py = ps[0]; jx, jy = e['xCoord'], e['yCoord']
                dx = max(jx - px, 0, px - (jx + 4)); dy = max(jy - py, 0, py - (jy + 4))
                (mel_adj if a == 86 else nonmel_adj)[max(dx, dy) <= 1] += 1
    firsts_all.append(sorted(firsts))
    for t, n in atk_ticks.items(): same_tick[n] += 1
    for rid, u in upd.items():
        for k in {(e['xCoord'], e['yCoord']) for e in u.values()}: moved[(rid_tile.get(rid), k)] += 1
        hs = sorted(u.items())
        for (t0, a0), (t1, a1) in zip(hs, hs[1:]):
            if t1 == t0 + 1 and (a1['npc']['hitpoints'] >> 16) > (a0['npc']['hitpoints'] >> 16): heal_inc += 1
    # healers: group by spawn tick; attribute to nearest Jad by tile
    hsp = [e for e in ev if e['type'] == 7 and e['npc']['id'] == 7701]
    groups = collections.defaultdict(list)
    for e in hsp: groups[e['tick']].append(e)
    run_groups = []
    for t, g in sorted(groups.items()):
        run_groups.append((t, len(g)))
    healer_first_group.append(run_groups)
    deaths = sorted((e['tick'], e['npc']['roomId']) for e in ev if e['type'] == 9 and e['npc']['id'] == 7700)
    order = tuple(rid_tile.get(r) for t, r in deaths)
    death_order[order] += 1
    for t, r in deaths: death_ticks[rid_tile.get(r)].append(t)
    for (a, _), (b, _) in zip(deaths, deaths[1:]): death_gap.append(b - a)
    dur.append(deaths[-1][0] if deaths else None)
    for rid, u in upd.items():
        t_sp = min(e['tick'] for e in jads if e['npc']['roomId'] == rid)
        z = [t for t, e in sorted(u.items()) if (e['npc']['hitpoints'] >> 16) == 0]
        dd = [t for t, r in deaths if r == rid]
        if z and dd: hp_zero_to_despawn.append(dd[0] - z[0])
        # healer spawn relative to this Jad: first Jad hp <= 175 tick
        hp_series = [(t, e['npc']['hitpoints'] >> 16) for t, e in sorted(u.items())]
    # healer groups: each spawn group of 3 near a Jad -> hp of nearest Jad on spawn tick and tick before
    jad_pos = {rid: rid_tile[rid] for rid in rid_tile}
    for t, g in sorted(groups.items()):
        cx = sum(e['xCoord'] for e in g) / len(g); cy = sum(e['yCoord'] for e in g) / len(g)
        best = min(upd, key=lambda r: abs(jad_pos[r][0] - cx) + abs(jad_pos[r][1] - cy))
        hs_per_jad[len(g)] += 1
        up = upd[best]
        cur = up.get(t); prv = up.get(t - 1) or up.get(t)
        if cur: hs_hp.append(cur['npc']['hitpoints'] >> 16)
        if prv: hs_hp_prev.append(prv['npc']['hitpoints'] >> 16)
        jx, jy = jad_pos[best]
        for e in g: hs_rel[(e['xCoord'] - jx, e['yCoord'] - jy)] += 1
        hs_tick[jad_pos[best]].append(t)
    # healer attack gaps
    ha = collections.defaultdict(list)
    for e in ev:
        if e['type'] == 10 and e['npc']['id'] == 7701: ha[e['npc']['roomId']].append(e['tick'])
    for r_, tl in ha.items():
        for a, b in zip(tl, tl[1:]): hattack[b - a] += 1
    ov_w = [w for w in ov['inferno']['waves'] if w['stage'] == 267][0]
    healers_alive_at_death.append(ov_w['ticks'])
print('Jads spawned per run: %s' % dict(njad))
print('Jad spawn tiles (x,y: count): %s' % dict(spawn_tiles))
print('pillar spawn rows on wave 68: %d' % pill)
print('first Jad attack tick by spawn tile: %s' % {k: srt(v) for k, v in first_by_tile.items()})
print('first-attack ticks per run (sorted triples): %s' % firsts_all)
print('Jad attack gaps all: %s' % dict(sorted(gaps.items())))
for s in 'RMX': print('  gap after %s: %s' % (s, dict(sorted(gap_after[s].items()))))
print('styles: %s' % dict(sty)); print('style transitions (non-melee pairs): %s' % dict(trans))
print('melee attacks with player adjacent to footprint: %s ; non-melee with player adjacent: %s' % (dict(mel_adj), dict(nonmel_adj)))
print('ticks on which n Jads attacked together (n: ticks): %s' % dict(same_tick))
print('healer spawn groups (tick,count) per run: %s' % healer_first_group)
print('healers per spawn group: %s' % dict(hs_per_jad))
print('owning-Jad hp on the tick before a healer group: %s' % srt(hs_hp_prev)); print('owning-Jad hp on the spawn tick: %s' % srt(hs_hp))
nh = sum(hs_rel.values())
print('healer spawn offset from owning Jad SW tile: n=%d dx %d..%d dy %d..%d' % (nh, min(x for x, y in hs_rel), max(x for x, y in hs_rel), min(y for x, y in hs_rel), max(y for x, y in hs_rel)))
print('healer spawn offsets (dx,dy: count): %s' % dict(sorted(hs_rel.items())))
print('healer spawn ticks by owning Jad tile: %s' % {k: srt(v) for k, v in hs_tick.items()})
print('Jad death ticks by tile: %s' % {k: srt(v) for k, v in death_ticks.items()})
print('Jad death order by tile (order: runs): %s' % dict(death_order)); print('gaps between consecutive Jad deaths: %s' % srt(death_gap))
print('ticks from hp 0 first seen to despawn: %s' % srt(hp_zero_to_despawn))
print('Jad hp increases (heal ticks) in all runs: %d' % heal_inc)
print('healer attack gaps (same healer): %s' % dict(sorted(hattack.items())))
print('Jad tile changes across updates ((spawn tile, seen tile): count): %s' % {k: v for k, v in moved.items() if k[0] != k[1]})
print('wave 68 record ticks: %s' % srt(healers_alive_at_death))

# ---- wave end, next wave, healer cleanup, melee follow-up ----
c_same = collections.Counter(); wend = []; gap69 = []; nextafterx = collections.Counter(); hd_after = collections.Counter()
for f in sorted(glob.glob(CACHE + '*.json')):
    d = json.load(open(f))
    if not isinstance(d, dict) or not d.get('waves', {}).get('68'): continue
    ev = d['waves']['68']; W = {w['stage']: w for w in d['overview']['inferno']['waves']}; w = W[267]
    deaths = sorted((e['tick'], e['npc']['roomId']) for e in ev if e['type'] == 9 and e['npc']['id'] == 7700)
    hd = sorted((e['tick'], e['npc']['roomId']) for e in ev if e['type'] == 9 and e['npc']['id'] == 7701)
    wend.append(w['ticks'] - deaths[-1][0])
    if 268 in W: gap69.append(W[268]['startTick'] - (w['startTick'] + w['ticks']))
    for t, _ in deaths: c_same[sum(1 for ht, _ in hd if ht == t)] += 1
    att = collections.defaultdict(list)
    for e in ev:
        if e['type'] == 10 and e['npc']['id'] == 7700: att[e['npc']['roomId']].append((e['tick'], e['npcAttack']['attack']))
    for r, l in att.items():
        l.sort()
        for (a, x), (b, y), (c, z) in zip(l, l[1:], l[2:]):
            if x == 86: nextafterx[(b - a, c - b)] += 1
print('last Jad death to wave-68 record end (ticks: runs): %s' % dict(collections.Counter(wend)))
print('wave-68 record end to wave-69 record start (ticks): %s' % srt(gap69))
print('healer deaths on the same tick as a Jad death (count per Jad death: Jad deaths): %s' % dict(c_same))
print('(gap after a melee, the gap after that): %s' % dict(nextafterx))

# ---- player side (PLAYER_UPDATE source 0): protection bits 16 magic, 17 missiles, 18 melee ----
BIT = {85: 1 << 16, 84: 1 << 17, 86: 1 << 18}
setk = collections.defaultdict(collections.Counter); firstk = collections.Counter(); nat = collections.Counter()
drop_prot = collections.Counter(); drop_unprot = collections.Counter(); dropamt = collections.defaultdict(list); lost_late = 0; late_n = 0; protamt = []
for run, (ev, ov) in runs.items():
    ps = {}; hpv = {}
    for e in ev:
        if e['type'] == 4 and e['player'].get('source') == 0:
            ps[e['tick']] = e['player'].get('prayerSet', 0) or 0; hpv[e['tick']] = e['player']['hitpoints'] >> 16
    for e in (x for x in ev if x['type'] == 10 and x['npc']['id'] == 7700):
        a = e['npcAttack']['attack']; t = e['tick']; b = BIT[a]; nat[NAME[a]] += 1
        for k in range(-1, 5): setk[NAME[a]][k] += 1 if ps.get(t + k, 0) & b else 0
        k0 = None
        for k in range(-3, 4):
            if all(ps.get(t + j, 0) & b for j in range(k, 4)): k0 = k; break
        firstk[k0] += 1
        if k0 in (2, 3) and a != 86:
            late_n += 1
        prot3 = bool(ps.get(t + 3, 0) & b)
        for k in range(1, 6):
            if t + k in hpv and t + k - 1 in hpv and hpv[t + k - 1] - hpv[t + k] > 0:
                (drop_prot if prot3 else drop_unprot)[(NAME[a], k)] += 1
                (protamt if prot3 else dropamt[NAME[a]]).append(hpv[t + k - 1] - hpv[t + k])
print('--- player side, attacks by style %s' % dict(nat))
for s in 'RMX': print('correct protect bit set at attack+k (k=-1..4) for %s of %d: %s' % (s, nat[s], dict(sorted(setk[s].items()))))
print('earliest k from which the correct bit stays set through attack+3: %s' % dict(sorted(firstk.items(), key=lambda kv: (kv[0] is None, kv[0]))))
print('non-melee swings whose correct bit first came on at attack+2 or +3: %d' % late_n)
print('hp drop tick offsets when the correct bit WAS set at attack+3 (style,k: count): %s' % dict(sorted(drop_prot.items())))
print('hp drop tick offsets when the correct bit was NOT set at attack+3 (style,k: count): %s' % dict(sorted(drop_unprot.items())))
for s in 'RMX':
    if dropamt[s]: print('unprayed drop amounts %s: n=%d %s' % (s, len(dropamt[s]), sorted(dropamt[s])))
print('hp drop amounts when the correct bit WAS set at attack+3 (n=%d): max %d, count above 18 (the healer melee max): %d, amounts above 18: %s' % (len(protamt), max(protamt), sum(1 for x in protamt if x > 18), sorted(x for x in protamt if x > 18)))
