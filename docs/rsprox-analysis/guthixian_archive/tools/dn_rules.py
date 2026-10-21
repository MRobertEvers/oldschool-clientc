"""Fit Dreadnoth's (or a jelly's) chase step rule and target switches over --all-players transcripts.
usage: dn_rules.py [--npc tfa_jelly_boss --size 5] <txt>..."""
import re, argparse, collections
ap = argparse.ArgumentParser(); ap.add_argument('txts', nargs='+')
ap.add_argument('--npc', default='tfa_jelly_boss'); ap.add_argument('--size', type=int, default=5)
ap.add_argument('--show', default='', help='comma list: miss,switch,melee,gap')
a = ap.parse_args(); N = a.size; show = set(a.show.split(','))
TICK = re.compile(r'^\[(\d+)\]')
PL = re.compile(r'^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]+), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+), name=([^,]+)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
DIRS = {(0,1):'north',(0,-1):'south',(1,0):'east',(-1,0):'west',(1,1):'north-east',(-1,1):'north-west',(1,-1):'south-east',(-1,-1):'south-west',(0,0):'none'}
sg = lambda v: (v > 0) - (v < 0)
def r_sw(sw, p): return (sg(p[0]-sw[0]), sg(p[1]-sw[1]))
def r_box(sw, p):
    f = lambda v, lo: -1 if v < lo else (1 if v > lo + N - 1 else 0)
    return (f(p[0], sw[0]), f(p[1], sw[1]))
def r_ctr(sw, p): c = N // 2; return (sg(p[0]-sw[0]-c), sg(p[1]-sw[1]-c))
def inside(sw, p): return sw[0] <= p[0] < sw[0]+N and sw[1] <= p[1] < sw[1]+N
def boxd(sw, p):
    f = lambda v, lo: lo - v if v < lo else (v - (lo + N - 1) if v > lo + N - 1 else 0)
    return max(f(p[0], sw[0]), f(p[1], sw[1]))
RULES = {'sw': r_sw, 'box': r_box, 'ctr': r_ctr}
score = collections.Counter(); total = 0; geo = collections.Counter(); idle = collections.Counter()
gaps = collections.Counter(); meleegap = collections.Counter()
for path in a.txts:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; start = {}; end = {}; names = {}; cur = None
    npc = {}  # idx -> state
    pend = []
    def close(t):
        for idx, e in list(npc.items()):
            ev = e.get('ev')
            if not ev: continue
            e['ev'] = None
            tg = e.get('target')
            ps = start.get(tg); pe = end.get(tg)
            if ev['kind'] in ('crawl', 'walk') and ps:
                d = ev['step']
                global total
                total += 1
                hits = []
                for rn, rf in RULES.items():
                    for pn, p in (('start', ps), ('end', pe)):
                        r = rf(ev['from'], p)
                        if r == d: score[(rn, pn)] += 1; hits.append(rn + '/' + pn)
                bd = boxd(ev['from'], ps)
                dx = 0 if ev['from'][0] <= ps[0] <= ev['from'][0] + N - 1 else 1
                dz = 0 if ev['from'][1] <= ps[1] <= ev['from'][1] + N - 1 else 1
                geo[(bd if bd < 3 else '3+', 'diag' if dx and dz else 'card')] += 1
                if e.get('lastcrawl') is not None: gaps[t - e['lastcrawl']] += 1
                e['lastcrawl'] = t
                if 'miss' in show and 'sw/start' not in hits:
                    print(f"{sess} [{t}] MISS step={DIRS[d]} sw={ev['from']} tgt start={ps} end={pe} sw-rule={DIRS[r_sw(ev['from'],ps)]} box={DIRS[r_box(ev['from'],ps)]} boxd={boxd(ev['from'],ps)} hits={hits}")
            if ev['kind'] == 'idle' and ps and e.get('lastcrawl') is not None and t - e['lastcrawl'] >= 2 and t - e.get('lastseq', -99) > 12:
                bd = boxd(ev['from'], ps)
                dx = 0 if ev['from'][0] <= ps[0] <= ev['from'][0] + N - 1 else 1
                dz = 0 if ev['from'][1] <= ps[1] <= ev['from'][1] + N - 1 else 1
                idle[(bd if bd < 3 else '3+', 'diag' if dx and dz else 'card')] += 1
            if ev.get('seq') and 'jump' in ev['seq']: e['lastseq'] = t
            if ev.get('seq') and 'melee' in ev['seq'] and ps:
                near = sorted((boxd(ev['from'], start[k]), names[k]) for k in start)
                if 'melee' in show:
                    print(f"{sess} [{t}] MELEE sw={ev['from']} target={names.get(tg)} d={boxd(ev['from'],ps)} all={near}")
                if e.get('lastmelee') is not None: meleegap[t - e['lastmelee']] += 1
                e['lastmelee'] = t
            if ev.get('switch') and 'switch' in show:
                near = sorted((boxd(ev['from'], start[k]), names[k]) for k in start)
                print(f"{sess} [{t}] SWITCH {ev['switch'][0]} -> {ev['switch'][1]} since={t - e.get('lastswitch', t)} sw={ev['from']} dists={near}")
                e['lastswitch'] = t
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m:
            close(tick); tick = int(m.group(1)); start = dict(end); cur = None; continue
        m = PL.match(line)
        if m:
            kind, idx = m.group(1), int(m.group(2)); names[idx] = m.group(3)
            c = (int(m.group(4)), int(m.group(5))); n = (int(m.group(7)), int(m.group(8))) if m.group(7) else c
            if kind == 'del': end.pop(idx, None); start.pop(idx, None)
            else: start[idx] = c; end[idx] = n
            cur = None; continue
        m = NPC.match(line)
        if m:
            kind, idx, sym = m.group(1), int(m.group(2)), m.group(3)
            if sym != a.npc: cur = None; continue
            cur = idx; e = npc.setdefault(idx, {})
            if kind == 'add': npc[idx] = e = {}
            c = (int(m.group(5)), int(m.group(6))); n = (int(m.group(8)), int(m.group(9))) if m.group(8) else c
            e['ev'] = {'kind': kind, 'from': c, 'to': n, 'step': (n[0]-c[0], n[1]-c[1])}
            if kind == 'del': npc.pop(idx); cur = None
            continue
        if cur is None: continue
        e = npc[cur]
        m = FACEP.match(line)
        if m:
            t = int(m.group(1))
            if e.get('target') != t:
                if e.get('target') is not None: e['ev']['switch'] = (names.get(e['target']), m.group(2))
                e['target'] = t
            continue
        m = SEQ.match(line)
        if m: e['ev']['seq'] = m.group(1)
print('crawls', total)
for k, v in score.most_common(): print(f"  {k[0]:4s} {k[1]:6s} {v}")
print('crawl by target geometry', sorted(geo.items(), key=str))
print('idle-eligible by geometry', sorted(idle.items(), key=str))
print('crawl gaps', sum(gaps.values()), sorted(gaps.items()))
print('melee gaps', sum(meleegap.values()), sorted(meleegap.items()))
