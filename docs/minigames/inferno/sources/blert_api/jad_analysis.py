#!/usr/bin/env python3
"""Single Jad (wave 67) from the full wave-67 event streams of the 14 cached Blert runs -> JAD_ANALYSIS.txt.
Input: build/corpus_tmp/blert_api/<uuid>.json (not in git; the 14 uuids are in FETCH_LOG.tsv, stage=267).
OBSERVED rows only (PROVENANCE.md). Tick 0 = the `Wave: 67` chat tick. npc hitpoints are packed (current<<16)|base
(decoded here: 22937950 -> 350/350). Attack ids: 84 ranged, 85 magic, 86 melee, 87 healer auto."""
import json, glob, os, collections, statistics as st
D = os.path.dirname(os.path.abspath(__file__)) + '/'
CACHE = D + '../../../../../build/corpus_tmp/blert_api/'
def pct(a, q): a = sorted(a); return a[min(len(a)-1, int(q*len(a)))]
runs = {}
for f in sorted(glob.glob(CACHE + '*.json')):
    d = json.load(open(f))
    if isinstance(d, dict) and '67' in d.get('waves', {}) and any(e['type'] == 7 and e['npc']['id'] == 7700 for e in d['waves']['67']):
        runs[d['uuid'][:8]] = (d['waves']['67'], d['overview'])
print('runs with wave 67 streams: %d' % len(runs))
first = []; gaps = collections.Counter(); gap_after = collections.defaultdict(collections.Counter)
sty = collections.Counter(); trans = collections.Counter(); seqlen = []
hs_tick = []; hs_n = collections.Counter(); hs_hp = []; hs_rel = collections.Counter(); hs_hpprev = []
death = []; lastdmg_to_death = []; dur = []
mel_adj = collections.Counter(); nonmel_adj = collections.Counter()
heal_amt = collections.Counter(); heal_gap = collections.Counter(); hattack_gap = collections.Counter(); hfirst = []
jad_move = collections.Counter()
healer_per_run_heal = []
NAME = {84: 'R', 85: 'M', 86: 'X'}
for run, (ev, ov) in runs.items():
    jad = [e for e in ev if e['type'] == 10 and e['npc']['id'] == 7700]
    jad.sort(key=lambda e: e['tick'])
    upd = {e['tick']: e for e in ev if e['type'] == 8 and e['npc']['id'] == 7700}
    ply = collections.defaultdict(list)
    for e in ev:
        if e['type'] == 4: ply[e['tick']].append((e['xCoord'], e['yCoord']))
    ticks = [e['tick'] for e in jad]
    first.append(ticks[0])
    prev = None
    for e in jad:
        a = e['npcAttack']['attack']; s = NAME[a]; sty[s] += 1
        if prev is not None:
            g = e['tick'] - prev['tick']; gaps[g] += 1; gap_after[NAME[prev['npcAttack']['attack']]][g] += 1
            if prev['npcAttack']['attack'] != 86 and a != 86: trans[(NAME[prev['npcAttack']['attack']], s)] += 1
        prev = e
        # player distance (chebyshev from player tile to the 5x5 footprint SW tile + 0..4)
        jx, jy = e['xCoord'], e['yCoord']
        ps = ply.get(e['tick'], [])
        if ps:
            px, py = ps[0]
            dx = max(jx - px, 0, px - (jx + 4)); dy = max(jy - py, 0, py - (jy + 4))
            adj = max(dx, dy) <= 1
            (mel_adj if a == 86 else nonmel_adj)[adj] += 1
    sp = [e for e in ev if e['type'] == 7 and e['npc']['id'] == 7701]
    if sp:
        t = sp[0]['tick']; hs_tick.append(t); hs_n[len(sp)] += 1
        pv = upd.get(t - 1) or upd.get(t)
        hp = pv['npc']['hitpoints'] >> 16 if pv else None
        cur = upd.get(t)
        hs_hp.append(cur['npc']['hitpoints'] >> 16 if cur else None); hs_hpprev.append(hp)
        jx, jy = (upd.get(t) or upd.get(t-1))['xCoord'], (upd.get(t) or upd.get(t-1))['yCoord']
        for s_ in sp: hs_rel[(s_['xCoord'] - jx, s_['yCoord'] - jy)] += 1
    dd = [e for e in ev if e['type'] == 9 and e['npc']['id'] == 7700]
    if dd:
        death.append(dd[0]['tick'])
        z = [t for t, e in sorted(upd.items()) if (e['npc']['hitpoints'] >> 16) == 0]
        if z: lastdmg_to_death.append(dd[0]['tick'] - z[0])
    # heals: hp increases in Jad updates
    hp_series = [(t, e['npc']['hitpoints'] >> 16) for t, e in sorted(upd.items())]
    ht = []
    for (t0, h0), (t1, h1) in zip(hp_series, hp_series[1:]):
        if h1 > h0 and t1 == t0 + 1: heal_amt[h1 - h0] += 1; ht.append(t1)
    healer_per_run_heal.append(len(ht))
    for a, b in zip(ht, ht[1:]): heal_gap[b - a] += 1
    ha = collections.defaultdict(list)
    for e in ev:
        if e['type'] == 10 and e['npc']['id'] == 7701: ha[e['npc']['roomId']].append(e['tick'])
    for r_, tl in ha.items():
        for a, b in zip(tl, tl[1:]): hattack_gap[b - a] += 1
    dur.append(ov['inferno']['waves'][-3]['ticks'] if False else None)
    for e in ev:
        if e['type'] == 8 and e['npc']['id'] == 7700: pass
    xs = {(e['xCoord'], e['yCoord']) for e in upd.values()}
    for k in xs: jad_move[k] += 1
print('first Jad attack tick (n=%d): %s min %d p50 %d max %d' % (len(first), sorted(first), min(first), pct(first, .5), max(first)))
print('Jad attack gaps all: %s' % dict(sorted(gaps.items())))
for s in 'RMX': print('  gap after %s: %s' % (s, dict(sorted(gap_after[s].items()))))
print('styles: %s ; ranged+magic only: R %d M %d' % (dict(sty), sty['R'], sty['M']))
print('style transitions (non-melee pairs): %s' % dict(trans))
print('melee attacks with player adjacent to footprint: %s ; non-melee with player adjacent: %s' % (dict(mel_adj), dict(nonmel_adj)))
print('healer spawn tick (n=%d): %s' % (len(hs_tick), sorted(hs_tick)))
print('healers spawned per run: %s' % dict(hs_n))
print('Jad hp on the tick before healers appear: %s' % sorted(hs_hpprev))
print('Jad hp on the spawn tick: %s' % sorted(hs_hp))
nh = sum(hs_rel.values()); win = sum(v for (x, y), v in hs_rel.items() if 1 <= x <= 3 and 6 <= y <= 8)
print('healer spawn offset from Jad SW tile: n=%d dx %d..%d dy %d..%d; inside our window dx 1..3 dz 6..8: %d (%.0f percent); modal (0,8) %d' % (nh, min(x for x, y in hs_rel), max(x for x, y in hs_rel), min(y for x, y in hs_rel), max(y for x, y in hs_rel), win, 100.0*win/nh, hs_rel[(0, 8)]))
print('healer spawn offsets (dx,dy: count): %s' % dict(sorted(hs_rel.items())))
print('Jad death tick: %s' % sorted(death)); print('ticks from hp 0 first seen to despawn: %s' % sorted(lastdmg_to_death))
print('Jad hp increases (heal ticks) per run: %s' % healer_per_run_heal)
print('Jad heal amounts observed per tick: %s' % dict(sorted(heal_amt.items())))
print('gap between consecutive Jad heal ticks: %s' % dict(sorted(heal_gap.items())))
print('healer attack gaps (same healer): %s' % dict(sorted(hattack_gap.items())))
print('Jad tiles seen across updates (tile: run count): %s' % dict(jad_move))

# ---- player side (PLAYER_UPDATE source 0): protection bits 16 magic, 17 missiles, 18 melee (read off the data: the modal bit at attack+3) ----
BIT = {85: 1 << 16, 84: 1 << 17, 86: 1 << 18}
setk = collections.defaultdict(collections.Counter)   # style -> {k: count of attacks with the correct bit set at attack+k}
firstk = collections.Counter(); dropk = collections.Counter(); drop_unprot = collections.Counter(); drop_prot = collections.Counter(); drop_amt = collections.defaultdict(list)
pstart = []; hpstart = []; nat = 0
for run, (ev, ov) in runs.items():
    ps = {}; hpv = {}
    for e in ev:
        if e['type'] == 4 and e['player'].get('source') == 0:
            ps[e['tick']] = e['player'].get('prayerSet', 0) or 0
            hpv[e['tick']] = e['player']['hitpoints'] >> 16
            if e['tick'] <= 1: pstart.append((e['xCoord'], e['yCoord']))
    for e in sorted((x for x in ev if x['type'] == 10 and x['npc']['id'] == 7700), key=lambda x: x['tick']):
        a = e['npcAttack']['attack']; t = e['tick']; b = BIT[a]; nat += 1
        for k in range(-1, 5): setk[NAME[a]][k] += 1 if ps.get(t + k, 0) & b else 0
        # first tick offset from which the correct bit stays set through t+3
        k0 = None
        for k in range(-3, 4):
            if all(ps.get(t + j, 0) & b for j in range(k, 4)): k0 = k; break
        firstk[k0] += 1
        # hp drop in t..t+5 (any positive drop on one tick), only if prayers at t+3 are right / wrong
        drops = [(k, hpv.get(t + k - 1, 0) - hpv.get(t + k, 0)) for k in range(1, 6) if t + k in hpv and t + k - 1 in hpv and hpv[t + k - 1] - hpv[t + k] > 0]
        prot3 = bool(ps.get(t + 3, 0) & b)
        for k, amt in drops:
            (drop_prot if prot3 else drop_unprot)[(NAME[a], k)] += 1
            if not prot3: drop_amt[NAME[a]].append(amt)
print('--- player side, %d Jad attacks, PLAYER_UPDATE source 0' % nat)
print('player tile on tick 0/1 (x,y: count): %s' % dict(collections.Counter(pstart)))
for s in 'RMX': print('correct protect bit set at attack+k (k=-1..4) for %s of %d: %s' % (s, sty[s], dict(sorted(setk[s].items()))))
print('earliest k from which the correct bit stays set through attack+3: %s' % dict(sorted(firstk.items(), key=lambda kv: (kv[0] is None, kv[0]))))
print('hp drop tick offsets when the correct bit WAS set at attack+3 (style,k: count): %s' % dict(sorted(drop_prot.items())))
print('hp drop tick offsets when the correct bit was NOT set at attack+3 (style,k: count): %s' % dict(sorted(drop_unprot.items())))
for s in 'RMX':
    if drop_amt[s]: print('unprayed drop amounts %s (n=%d): min %d max %d median %d' % (s, len(drop_amt[s]), min(drop_amt[s]), max(drop_amt[s]), st.median(drop_amt[s])))

# ---- from observed_npc_events.tsv + wave_records.tsv (all cached runs) ----
import csv
tev = list(csv.DictReader(open(D + 'observed_npc_events.tsv'), delimiter='\t'))
twr = {(r['run'], r['wave']): r for r in csv.DictReader(open(D + 'wave_records.tsv'), delimiter='\t')}
print('--- tsv-side')
pc = collections.Counter()
for r in tev:
    if r['npc_id'] == '7709' and r['kind'] == 'spawn' and r['wave'] in ('65', '66', '67', '68'): pc[r['wave']] += 1
print('pillar (7709) spawn rows by wave: %s' % dict(sorted(pc.items())))
byrun = collections.defaultdict(list)
for r in tev:
    if r['wave'] == '67': byrun[r['run']].append(r)
off = collections.Counter(); wend = collections.Counter()
for run, rs in byrun.items():
    jd = [int(r['tick']) for r in rs if r['kind'] == 'death' and r['npc_id'] == '7700']
    if not jd: continue
    wend[jd[0] - int(twr[(run, '67')]['ticks'])] += 1
    for r in rs:
        if r['kind'] == 'death' and r['npc_id'] == '7701': off[int(r['tick']) - jd[0]] += 1
print('healer despawn tick minus Jad despawn tick (healers: count): %s' % dict(sorted(off.items())))
print('Jad despawn tick minus wave-67 record ticks (runs: count): %s' % dict(sorted(wend.items())))
gap = collections.Counter()
for (run, w), r in twr.items():
    if w == '67' and (run, '68') in twr:
        n = twr[(run, '68')]; gap[int(n['start_tick']) - int(r['start_tick']) - int(r['ticks'])] += 1
print('wave 68 start minus wave 67 end (runs: count): %s' % dict(sorted(gap.items())))
