# seam15: what is left of Verzik's attacks and tornadoes after her last hitpoint, from the 62 Blert
# P3 recordings (fetch_verzik.py output). Z = first tick her P3 npc (8374/10835/10852) reads 0
# hitpoints; K = the death-form change (blert emits NPC_DEATH for her death id and every tornado
# there, VerzikDataTracker.java:446-456). Recordings end at K+6.
import json, glob, os, sys, collections
D = sys.argv[1]
P3 = {8374, 10835, 10852}; TOR = {8386, 10846, 10863}; DEATH = {8375, 10836, 10853}
tot = collections.Counter(); gapk = collections.Counter(); dropgap = collections.Counter()
for f in sorted(glob.glob(D + '/*.json')):
    e = json.load(open(f)); n = os.path.basename(f)[:13]
    v = [(x['tick'], x['npc']['hitpoints'] >> 16) for x in e if x['type'] in (7, 8) and x.get('npc', {}).get('id') in P3]
    z = [t for t, h in v if h == 0]
    if not z: continue
    Z = z[0]; K = [x['tick'] for x in e if x['type'] == 9 and x['npc']['id'] in DEATH][0]
    gapk[K - Z] += 1; tot['rooms'] += 1
    tor_moves = len({(x['tick'], x['xCoord'], x['yCoord']) for x in e if x['type'] == 8 and x.get('npc', {}).get('id') in TOR and Z < x['tick'] < K})
    tor_after_k = sum(1 for x in e if x.get('npc', {}).get('id') in TOR and x['tick'] > K)
    tot['tornado_rows_between_Z_and_K'] += tor_moves; tot['tornado_rows_after_K'] += tor_after_k
    atk = [x['tick'] for x in e if x['type'] == 10 and x.get('npc', {}).get('id') in P3 and x['tick'] <= Z]
    hp = collections.defaultdict(dict)
    for x in e:
        if x['type'] == 4 and 'hitpoints' in x['player']: hp[x['player']['name']][x['tick']] = x['player']['hitpoints'] >> 16
    rows = []
    for nm, d in hp.items():
        ts = sorted(t for t in d if Z - 1 <= t <= K + 6)
        for a, b in zip(ts, ts[1:]):
            if d[b] < d[a] and b > Z:
                g = b - atk[-1] if atk else None; dropgap[g] += 1
                rows.append('Z+%d %d->%d gap%s' % (b - Z, d[a], d[b], g))
    pd = [x['tick'] - Z for x in e if x['type'] == 6 and x['tick'] > Z]
    if rows: tot['rooms_with_drop_after_Z'] += 1
    print(n, 'Z', Z, 'K', K, 'tornado_rows_Z_K', tor_moves, 'drops', rows, 'player_deaths_after_Z', pd)
print('TOTAL', dict(tot)); print('K-Z', sorted(gapk.items())); print('drop gap after her last launch', sorted(dropgap.items(), key=lambda p: (p[0] is None, p[0])))
