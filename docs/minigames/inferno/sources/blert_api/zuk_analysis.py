#!/usr/bin/env python3
"""Wave 69 (TzKal-Zuk, the glyph, the sets, the Zuk Jad, the Jal-MejJak) from the wave-69 event streams of the
cached Blert runs -> ZUK_ANALYSIS.txt. Input: build/corpus_tmp/blert_api/<uuid>.json (not in git; uuids in
FETCH_LOG.tsv). OBSERVED rows only (blert/PROVENANCE.md). Tick 0 = the `Wave: 69` chat tick. hitpoints packed
(current<<16)|base. Attack ids: 88 zuk auto. Types: 4 player, 5 player attack, 7 spawn, 8 update, 9 despawn,
10 npc attack. Run: python3 zuk_analysis.py > ZUK_ANALYSIS.txt"""
import json, glob, os, collections
D = os.path.dirname(os.path.abspath(__file__)) + '/'
CACHE = D + '../../../../../build/corpus_tmp/blert_api/'
C = collections.Counter
def hp(e): return e['npc']['hitpoints'] >> 16
def dist(c): return ' '.join('%s:%d' % (k, v) for k, v in sorted(c.items()))
runs = {}
for f in sorted(glob.glob(CACHE + '*.json')):
    d = json.load(open(f))
    if isinstance(d, dict) and d.get('waves', {}).get('69'):
        runs[d['uuid'][:8]] = d['waves']['69']
print('runs with wave 69 streams: %d' % len(runs))
sh_spawn = C(); sh_first = C(); sh_step = C(); sh_xmin = C(); sh_xmax = C(); sh_dwell = C(); sh_period = C()
sh_dir = C(); sh_death = []; sh_hp_min = []; sh_at_zuk = C(); sh_hp_end = []
z_spawn = C(); z_first = []; z_gap = C(); z_gap_pre = C(); z_gap_post = C(); z_hp_enr = []; z_at = C(); z_death = []
ply_off = C(); ply_off_mov = C(); z_att_n = []; z_first_after_shield = []
set_first = []; set_gap = C(); set_tile = {7702: C(), 7703: C()}; set_delay = C(); set_count = C()
jad_tick = []; jad_tile = C(); jad_hp = C(); jad_n = C(); jh_n = C(); jh_hpjad = []; jad_att_first = []; jad_gap = C()
mj_tick = []; mj_tile = C(); mj_n = C(); mj_zukhp = []; mj_group = C(); heal_amt = C(); heal_gap = C()
zuk_hp_at_death_tick = []; z_hp_series_first = C(); tick_zuk_below = {600: [], 480: [], 240: []}
set_tiles_all = C(); stage_dur = []
for run, ev in runs.items():
    by = lambda t, i: [e for e in ev if e['type'] == t and e['npc']['id'] == i]
    sp = by(7, 7707)
    if not sp: continue
    sh_spawn[(sp[0]['xCoord'], sp[0]['yCoord'], sp[0]['tick'])] += 1
    pos = {e['tick']: e['xCoord'] for e in by(8, 7707)}
    pos[sp[0]['tick']] = sp[0]['xCoord']
    ts = sorted(pos)
    xs = [pos[t] for t in ts]
    steps = [xs[i + 1] - xs[i] for i in range(len(xs) - 1) if ts[i + 1] == ts[i] + 1]
    for s in steps: sh_step[s] += 1
    mv = [t for t in ts[1:] if pos[t] != pos[ts[0]]]
    if mv:
        sh_first[mv[0]] += 1; sh_dir[pos[mv[0]] - pos[ts[0]]] += 1
    sh_xmin[min(xs)] += 1; sh_xmax[max(xs)] += 1
    # dwell: consecutive ticks at the extreme positions
    for ext in (min(xs), max(xs)):
        run_len = 0
        for t in ts:
            if pos[t] == ext: run_len += 1
            else:
                if run_len: sh_dwell[(ext, run_len)] += 1
                run_len = 0
        if run_len: sh_dwell[(ext, run_len)] += 1
    # period: ticks between two successive arrivals at the max
    arr = [t for i, t in enumerate(ts) if pos[t] == max(xs) and (i == 0 or pos[ts[i - 1]] != max(xs))]
    for i in range(len(arr) - 1): sh_period[arr[i + 1] - arr[i]] += 1
    hh = [hp(e) for e in by(8, 7707)]
    if hh: sh_hp_min.append(min(hh)); sh_hp_end.append(hh[-1])
    d = by(9, 7707)
    if d: sh_death.append(d[0]['tick'])
    # zuk
    zs = by(7, 7706)
    zu = {e['tick']: e for e in by(8, 7706)}
    if zs:
        z_spawn[(zs[0]['xCoord'], zs[0]['yCoord'], zs[0]['tick'])] += 1
        z_hp_series_first[hp(zs[0])] += 1
    za = sorted(e['tick'] for e in by(10, 7706))
    z_att_n.append(len(za))
    if za: z_first.append(za[0])
    hp_at = {}
    for t, e in zu.items(): hp_at[t] = hp(e)
    last = None
    for t in sorted(hp_at):
        for th in (600, 480, 240):
            if hp_at[t] <= th and not tick_zuk_below[th] or False: pass
    for th in (600, 480, 240):
        ft = [t for t in sorted(hp_at) if hp_at[t] <= th]
        if ft: tick_zuk_below[th].append(ft[0])
    for i in range(len(za) - 1):
        g = za[i + 1] - za[i]
        z_gap[g] += 1
        h = hp_at.get(za[i + 1])
        if h is None: continue
        (z_gap_pre if h > 240 else z_gap_post)[g] += 1
    for t in za:
        if t in pos: sh_at_zuk[pos[t]] += 1
    ply = {e['tick']: (e['xCoord'], e['yCoord']) for e in ev if e['type'] == 4}
    for t in za:
        if t in ply and t in pos: ply_off[ply[t][0] - pos[t]] += 1
        if t - 1 in ply and t - 1 in pos: ply_off_mov[ply[t - 1][0] - pos[t - 1]] += 1
    d = by(9, 7706)
    if d: z_death.append(d[0]['tick'])
    # sets
    st = sorted(by(7, 7702) + by(7, 7703), key=lambda e: e['tick'])
    set_count[len(st)] += 1
    for e in st:
        set_tile[e['npc']['id']][(e['xCoord'], e['yCoord'])] += 1
    sets = sorted(set(e['tick'] for e in st))
    if sets: set_first.append(sets[0])
    # group by set: spawn ticks within 12 of each other
    groups = []
    for e in st:
        if groups and e['tick'] - groups[-1][0]['tick'] <= 12: groups[-1].append(e)
        else: groups.append([e])
    for i, g in enumerate(groups):
        for e in g: set_delay[(e['npc']['id'], e['tick'] - g[0]['tick'])] += 1
        if i: set_gap[g[0]['tick'] - groups[i - 1][0]['tick']] += 1
    # jad
    jd = by(7, 7704); jad_n[len(jd)] += 1
    for e in jd:
        jad_tick.append(e['tick']); jad_tile[(e['xCoord'], e['yCoord'])] += 1; jad_hp[hp(e)] += 1
        ja = sorted(a['tick'] for a in by(10, 7704))
        if ja: jad_att_first.append(ja[0] - e['tick'])
        for i in range(len(ja) - 1): jad_gap[ja[i + 1] - ja[i]] += 1
    jh = by(7, 7705); jh_n[len(jh)] += 1
    # mejjak
    mj = by(7, 7708); mj_n[len(mj)] += 1
    for e in mj:
        mj_tick.append(e['tick']); mj_tile[(e['xCoord'], e['yCoord'])] += 1
        if e['tick'] in hp_at: mj_zukhp.append(hp_at[e['tick']])
    ts_mj = sorted(e['tick'] for e in mj)
    if ts_mj: mj_group[max(ts_mj) - min(ts_mj)] += 1
    # heals on zuk: positive hp deltas between consecutive ticks while healers alive
    tz = sorted(hp_at)
    last_heal = None
    for i in range(len(tz) - 1):
        if tz[i + 1] == tz[i] + 1:
            dh = hp_at[tz[i + 1]] - hp_at[tz[i]]
            if dh > 0:
                heal_amt[dh] += 1
                if last_heal is not None: heal_gap[tz[i + 1] - last_heal] += 1
                last_heal = tz[i + 1]
def p(t, v): print('%-52s %s' % (t, v))
p('shield spawn (x,y,tick)', dist(sh_spawn))
p('shield first move tick', dist(sh_first)); p('shield first move dx', dist(sh_dir))
p('shield step dx per consecutive tick', dist(sh_step))
p('shield min x / max x', dist(sh_xmin) + ' / ' + dist(sh_xmax))
p('shield dwell at extreme (x, ticks in a row)', dist(sh_dwell))
p('shield ticks between arrivals at the max', dist(sh_period))
p('shield x at zuk attack ticks', dist(sh_at_zuk))
p('player.x - shield.x at the zuk attack tick', dist(ply_off)); p('player.x - shield.x one tick before', dist(ply_off_mov))
p('shield hp min per run', sorted(sh_hp_min)); p('shield death tick', sorted(sh_death))
p('zuk spawn (x,y,tick)', dist(z_spawn)); p('zuk hp at spawn', dist(z_hp_series_first))
p('zuk first attack tick', sorted(z_first)); p('zuk attacks per run', sorted(z_att_n))
p('zuk gap all', dist(z_gap)); p('zuk gap hp>240', dist(z_gap_pre)); p('zuk gap hp<=240', dist(z_gap_post))
p('zuk first tick hp<=600', sorted(tick_zuk_below[600])); p('first tick hp<=480', sorted(tick_zuk_below[480])); p('first tick hp<=240', sorted(tick_zuk_below[240]))
p('zuk despawn tick', sorted(z_death))
p('set spawns per run (npc count)', dist(set_count)); p('first set tick', sorted(set_first)); p('gap between set groups', dist(set_gap))
p('set npc spawn offset in group (id, +ticks)', dist(set_delay))
p('zuk ranger tiles', dist(set_tile[7702])); p('zuk mager tiles', dist(set_tile[7703]))
p('jad per run', dist(jad_n)); p('jad spawn tick', sorted(jad_tick)); p('jad spawn tile', dist(jad_tile)); p('jad hp at spawn', dist(jad_hp))
p('jad first attack - spawn', sorted(jad_att_first)); p('jad attack gaps', dist(jad_gap)); p('jad healers per run', dist(jh_n))
p('mejjak per run', dist(mj_n)); p('mejjak spawn ticks', sorted(mj_tick)); p('mejjak tiles', dist(mj_tile)); p('mejjak group span', dist(mj_group))
p('zuk hp at the mejjak spawn tick', dist(C(mj_zukhp)))
p('zuk heal per tick (positive hp deltas)', dist(heal_amt)); p('ticks between heals', dist(heal_gap))
