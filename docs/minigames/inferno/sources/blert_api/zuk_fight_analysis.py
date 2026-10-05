"""Zuk fight rows from the cached wave-69 Blert recordings (zuk_fight unit).
Reads build/corpus_tmp/blert_api/*.json (13 wave-69 runs). Hitpoints are packed: value >> 16."""
import json, glob, collections
C = collections.Counter
CACHE = 'build/corpus_tmp/blert_api/'
def hp(e): return e['npc']['hitpoints'] >> 16
def dist(c): return ' '.join('%s:%d' % (k, v) for k, v in sorted(c.items(), key=lambda kv: str(kv[0])))
def p(t, v): print('%-58s %s' % (t, v))
runs = []
for f in sorted(glob.glob(CACHE + '*.json')):
    d = json.load(open(f))
    if isinstance(d, dict) and d.get('waves', {}).get('69'): runs.append((f, d['waves']['69']))
p('runs', len(runs))
gap_by_enrage = C(); first7_vs_enrage = []; gaps_after = C(); gaps_before = C(); gap_seq_ex = None
add_gap = {7702: C(), 7703: C()}; add_first = {7702: C(), 7703: C()}; add_att_n = {7702: [], 7703: []}
set_expect = []; runlen = []
jad_att = C(); jad_style_seq = []; hurkot_gap = C(); hk_first = C(); hk_spawn_tick_rel = []; hk_tiles = C(); jad_hp_at_hk = []
zuk_death_tick = []; zuk_last_hp = []; add_dead_with_zuk = []; mej_att = C()
for f, ev in runs:
    last = max(e['tick'] for e in ev); runlen.append(last)
    zhp = {e['tick']: hp(e) for e in ev if e['type'] in (7, 8) and e['npc']['id'] == 7706}
    za = sorted(e['tick'] for e in ev if e['type'] == 10 and e['npc']['id'] == 7706)
    enr = None
    for t in sorted(zhp):
        if zhp[t] <= 240: enr = t; break
    # latch: the first 7-gap
    g7 = None
    for a, b in zip(za, za[1:]):
        if b - a == 7 and g7 is None: g7 = b
    for a, b in zip(za, za[1:]):
        gap = b - a
        phase = 'before_first_hp<=240' if (enr is None or a < enr) else 'after'
        gap_by_enrage[(phase, gap)] += 1
    first7_vs_enrage.append((enr, g7, za[0] if za else None))
    if gap_seq_ex is None and enr: gap_seq_ex = [b - a for a, b in zip(za, za[1:])][:40]
    # paused ticks in the 479..599 band and expected second set tick
    paused = sum(1 for t in range(0, last + 1) if t in zhp and 479 <= zhp[t] <= 599)
    jad_sp = [e['tick'] for e in ev if e['type'] == 7 and e['npc']['id'] == 7704]
    set_expect.append(72 + 350 + paused + (175 if jad_sp else 0))
    # adds
    for nid in (7702, 7703):
        at = sorted(e['tick'] for e in ev if e['type'] == 10 and e['npc']['id'] == nid)
        sp = sorted(e['tick'] for e in ev if e['type'] == 7 and e['npc']['id'] == nid)
        if sp and at: add_first[nid][at[0] - sp[0]] += 1
        for a, b in zip(at, at[1:]): add_gap[nid][b - a] += 1
        add_att_n[nid].append(len(at))
    # jad
    ja = sorted((e['tick'], e['npcAttack']['attack']) for e in ev if e['type'] == 10 and e['npc']['id'] == 7704)
    for t, a in ja: jad_att[a] += 1
    jad_style_seq.append([a for t, a in ja][:12])
    hk = [e for e in ev if e['type'] == 7 and e['npc']['id'] == 7705]
    if jad_sp and hk:
        hk_spawn_tick_rel.append(sorted(set(e['tick'] - jad_sp[0] for e in hk)))
        for e in hk: hk_tiles[(e['xCoord'], e['yCoord'])] += 1
        jh = {e['tick']: hp(e) for e in ev if e['type'] in (7, 8) and e['npc']['id'] == 7704}
        t0 = hk[0]['tick']
        jad_hp_at_hk.append(jh.get(t0, jh.get(t0 - 1)))
    for nid in (7705,):
        at = sorted(e['tick'] for e in ev if e['type'] == 10 and e['npc']['id'] == nid)
        for a, b in zip(at, at[1:]): hurkot_gap[b - a] += 1
    # zuk death
    zd = [e['tick'] for e in ev if e['type'] == 9 and e['npc']['id'] == 7706]
    if zd:
        zuk_death_tick.append(zd[0]); zuk_last_hp.append(zhp.get(max(zhp)))
        same = [e['npc']['id'] for e in ev if e['type'] == 9 and e['tick'] == zd[0] and e['npc']['id'] != 7706]
        add_dead_with_zuk.append(sorted(C(same).items()))
p('zuk gap by hp phase (phase relative to first hp<=240 tick)', dist(gap_by_enrage))
p('(first tick hp<=240, first 7-gap arrival, first attack) per run', first7_vs_enrage)
p('example gap sequence of one run', gap_seq_ex)
p('run lengths (last recorded tick)', sorted(runlen))
p('expected 2nd set tick = 422 + paused band ticks + 175', sorted(set_expect))
p('runs that outlast their expected 2nd set tick', sum(1 for a, b in zip(runlen, set_expect) if a > b))
for nid, nm in ((7702, 'ranger'), (7703, 'mager')):
    p(nm + ' first attack - spawn', dist(add_first[nid])); p(nm + ' attack gap', dist(add_gap[nid])); p(nm + ' attacks per run', sorted(add_att_n[nid]))
p('jad attack ids (84/85 = the two styles)', dist(jad_att))
p('jad style sequences (first 12 per run)', jad_style_seq[:6])
p('yt-hurkot spawn tick - jad spawn tick, per run', hk_spawn_tick_rel)
p('yt-hurkot spawn tiles', dist(hk_tiles)); p('jad hp at healers spawn', sorted(x for x in jad_hp_at_hk if x))
p('yt-hurkot attack gap', dist(hurkot_gap))
p('zuk death tick', zuk_death_tick); p('zuk last hp', zuk_last_hp); p('npc deaths on the zuk death tick', add_dead_with_zuk)
