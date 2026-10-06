"""Zuk sets and healers rows from the cached wave-69 Blert recordings (zuk_sets_and_healers unit).
Reads build/corpus_tmp/blert_api/*.json. Events: 7 spawn, 8 update, 9 death, 10 npc attack, 5 player attack."""
import json, glob, collections
C = collections.Counter
CACHE = 'build/corpus_tmp/blert_api/'
def hp(e): return e['npc']['hitpoints'] >> 16
def dist(c): return ' '.join('%s:%d' % (k, v) for k, v in sorted(c.items(), key=lambda kv: str(kv[0])))
def p(t, v): print('%-60s %s' % (t, v))
runs = []
for f in sorted(glob.glob(CACHE + '*.json')):
    d = json.load(open(f))
    if isinstance(d, dict) and d.get('waves', {}).get('69'): runs.append(d['waves']['69'])
p('runs', len(runs))
SET = (7702, 7703)
life = {7702: [], 7703: []}; moved = {7702: C(), 7703: C()}; at_tiles = {7702: C(), 7703: C()}
first_hit = {7702: [], 7703: []}; died_by_zukdeath = {7702: 0, 7703: 0}; died_early = {7702: 0, 7703: 0}
att_before_tag = {7702: C(), 7703: C()}; att_id = {7702: C(), 7703: C()}; hpseq = {}
pa_target = C(); hk_life = []; hk_move = C(); hk_dead = C(); hk_att_id = C()
jad_heal = C(); jad_heal_gap = C(); jad_heal_by_alive = collections.defaultdict(C)
mej_att_id = C(); mej_dead = []; mej_first_att = []; mej_att_gap = C(); mej_tagged_before_death = 0
for ev in runs:
    last = max(e['tick'] for e in ev)
    zdeath = [e['tick'] for e in ev if e['type'] == 9 and e['npc']['id'] == 7706]
    zd = zdeath[0] if zdeath else None
    room = {}
    for e in ev:
        if e['type'] == 7 and 'npc' in e: room[e['npc']['roomId']] = e['npc']['id']
    pat = [(e['tick'], room.get(e['attack']['target']['roomId'], -1) if 'attack' in e and 'target' in e['attack'] else -2, e['attack']['target']['roomId'] if 'attack' in e and 'target' in e['attack'] else 0) for e in ev if e['type'] == 5]
    for t, nid, rid in pat: pa_target[nid] += 1
    for nid in SET:
        for sp in [e for e in ev if e['type'] == 7 and e['npc']['id'] == nid]:
            rid = sp['npc']['roomId']; t0 = sp['tick']
            ups = sorted([e for e in ev if e['type'] in (7, 8) and e['npc']['roomId'] == rid], key=lambda e: e['tick'])
            dth = [e['tick'] for e in ev if e['type'] == 9 and e['npc']['roomId'] == rid]
            tiles = set((e['xCoord'], e['yCoord']) for e in ups)
            moved[nid][len(tiles)] += 1
            if dth: life[nid].append(dth[0] - t0)
            elif zd is not None: died_by_zukdeath[nid] += 1
            hits = [t for t, n, r in pat if r == rid]
            if hits: first_hit[nid].append(hits[0] - t0)
            ats = sorted(e['tick'] for e in ev if e['type'] == 10 and e['npc']['roomId'] == rid)
            for e in ev:
                if e['type'] == 10 and e['npc']['roomId'] == rid: att_id[nid][e['npcAttack']['attack']] += 1
            ft = hits[0] if hits else 10**9
            att_before_tag[nid][sum(1 for a in ats if a < ft)] += 1
    # healers
    hks = [e for e in ev if e['type'] == 7 and e['npc']['id'] == 7705]
    jad = [e for e in ev if e['type'] in (7, 8) and e['npc']['id'] == 7704]
    jh = {e['tick']: hp(e) for e in jad}
    for sp in hks:
        rid = sp['npc']['roomId']; t0 = sp['tick']
        dth = [e['tick'] for e in ev if e['type'] == 9 and e['npc']['roomId'] == rid]
        hk_dead['died' if dth else 'alive_at_end'] += 1
        if dth: hk_life.append(dth[0] - t0)
        ups = sorted([e for e in ev if e['type'] in (7, 8) and e['npc']['roomId'] == rid], key=lambda e: e['tick'])
        hk_move[len(set((e['xCoord'], e['yCoord']) for e in ups))] += 1
        for e in ev:
            if e['type'] == 10 and e['npc']['roomId'] == rid: hk_att_id[e['npcAttack']['attack']] += 1
    if hks:
        t0 = hks[0]['tick']; ts = sorted(jh)
        prev = None; lastheal = None
        for t in ts:
            if t < t0: prev = jh[t]; continue
            if prev is not None and jh[t] > prev:
                jad_heal[jh[t] - prev] += 1
                if lastheal is not None: jad_heal_gap[t - lastheal] += 1
                lastheal = t
            prev = jh[t]
    # mejjak
    for sp in [e for e in ev if e['type'] == 7 and e['npc']['id'] == 7708]:
        rid = sp['npc']['roomId']; t0 = sp['tick']
        dth = [e['tick'] for e in ev if e['type'] == 9 and e['npc']['roomId'] == rid]
        mej_dead.append((dth[0] - t0) if dth else None)
        ats = sorted(e['tick'] for e in ev if e['type'] == 10 and e['npc']['roomId'] == rid)
        for e in ev:
            if e['type'] == 10 and e['npc']['roomId'] == rid: mej_att_id[e['npcAttack']['attack']] += 1
        if ats: mej_first_att.append(ats[0] - t0)
        for a, b in zip(ats, ats[1:]): mej_att_gap[b - a] += 1
        hits = [t for t, n, r in pat if r == rid]
        if hits: mej_tagged_before_death += 1
p('player attack targets by npc id (-1 = unknown, 7706 zuk)', dist(pa_target))
for nid in SET:
    p('set %d distinct tiles seen per npc (1 = never moved)' % nid, dist(moved[nid]))
    p('set %d spawn-to-death lifetimes' % nid, sorted(life[nid]))
    p('set %d died with Zuk (no death event, run has Zuk death)' % nid, died_by_zukdeath[nid])
    p('set %d ticks from spawn to first player attack on it' % nid, sorted(first_hit[nid]))
    p('set %d attacks before the first player attack on it' % nid, dist(att_before_tag[nid]))
    p('set %d attack ids' % nid, dist(att_id[nid]))
p('hurkot deaths vs alive', dist(hk_dead)); p('hurkot spawn-to-death', sorted(hk_life))
p('hurkot distinct tiles seen', dist(hk_move)); p('hurkot attack ids', dist(hk_att_id))
p('jad hp rise per tick after healers spawn', dist(jad_heal)); p('ticks between jad hp rises', dist(jad_heal_gap))
p('mejjak spawn-to-death (None = lived to end)', sorted(mej_dead, key=lambda x: (x is None, x)))
p('mejjak attack ids', dist(mej_att_id)); p('mejjak first attack - spawn', dist(C(mej_first_att)))
p('mejjak attack gaps', dist(mej_att_gap)); p('mejjak players tagged', mej_tagged_before_death)
