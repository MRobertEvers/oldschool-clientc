#!/usr/bin/env python3
"""delays.py: every recorded ride's per-step delay2 and tick gap against the rules
emit.py writes (a step's delay, and the ticks before the next step). Must print
"timing matched 20 of 20" after any change to gen.py or emit.py."""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen
path = os.path.expanduser('~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx4646.txt')

def rec_rides():
    tick = 0; jv = {}; stage = 0; rides = []; cur = None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'^\[(\d+)\]', line)
        if m: tick = int(m.group(1)); continue
        m = re.search(r'forget_if(\d+) \(\d+\), old=\d+, new=(\d+)', line)
        if m: jv[int(m.group(1))] = int(m.group(2)); continue
        m = re.search(r'forget_quest \(822\), old=\d+, new=(\d+)', line)
        if m: stage = int(m.group(1))
        if 'oploc1_v3] id=forget_train_cart' in line:
            if cur: rides.append(cur)
            cur = {'tick': tick, 'stage': stage, 'lay': ''.join(str(jv.get(i, 0)) for i in range(1, 20)), 'steps': []}
            continue
        if cur is None: continue
        m = re.search(r'\[teleport\] player=.*newcoord=\((\d+), (\d+), (\d)\)', line)
        if m:
            cur['steps'].append([tick, (int(m.group(1)), int(m.group(2)), int(m.group(3))), None, 0]); continue
        if 'synth_sound] id=forget_cart_turn' in line and cur['steps'] and cur['steps'][-1][0] == tick:
            cur['steps'][-1][3] += 1; continue
        m = re.search(r'exactmove\] to1=\((\d+), (\d+), \d\), delay1=(\d+), to2=\((\d+), (\d+), (\d)\), delay2=(\d+)', line)
        if m and cur['steps']:
            cur['steps'][-1][2] = int(m.group(7)); continue
        if re.search(r'message_game\] type=(dialogue|mesbox)', line) and cur['steps']:
            rides.append(cur); cur = None
    if cur: rides.append(cur)
    return rides

def tiles(a, b):
    if a == 'PORTAL' or b == 'PORTAL': return 0
    return abs(a[0] - b[0]) + abs(a[1] - b[1])
def is_turn(st):
    if st[2] == 'PORTAL' or st[2][2] != st[4][2] or tiles(st[2], st[4]) > 4:
        return False
    return tuple(st[2][:2]) != tuple(st[4][:2])
def slow_jump(st):
    return not is_turn(st) and st[0] >= 90 and (st[2] == 'PORTAL' or st[2][2] != st[4][2] or tiles(st[2], st[4]) > 4)
def rules(out):
    # the start: 135, then 90 only over three tiles
    if out and out[0][0] == 135 and len(out) > 1 and out[1][0] == 90 and tiles(out[1][3], out[1][4]) != 3:
        out[1][0] = 60
    for i, st in enumerate(out):
        if st[5] == 'fixed':
            continue
        if st[5] == 'stop':
            st[0] = 80 if tiles(st[3], st[4]) <= 1 else 130
        elif i + 1 < len(out) and tiles(st[3], st[4]) == 3 and (is_turn(out[i + 1]) or slow_jump(out[i + 1])) and st[0] not in (135,):
            st[0] = 90
    for i in range(1, len(out)):
        if out[i - 1][0] in (90,) and out[i][1] == 1:
            out[i][1] = 2
    return [(a[0], a[1], a[2], a[6]) for a in out]

def sim(b, lay):
    """(delay, gap-before) per generated step, following emit.py."""
    out = []
    def arrive(idx, x0, z0, start=False):
        jx, jz = b.order[idx]
        st = int(lay[idx])
        tx = jx if st else jx - 1
        segs = gen.Build.runsegs(x0, z0, tx, jz, gen.E)
        if not segs and st and out:
            out[-1][6] += 1
        for k, sg in enumerate(segs):
            d = sg[2]
            if start and k == 0: d = 135
            elif start and k == 1: d = 90
            if not st and k == len(segs) - 1: d = 80
            gap = 1
            if start and k == 1: gap = 3
            if start and k == 2: gap = 2
            out.append([d, gap, sg[4], sg[0], sg[1], 'stop' if (not st and k == len(segs) - 1) else '', sg[5] + (1 if (st and k == len(segs) - 1) else 0)])
        return st
    cur = b.hub
    st = arrive(0, gen.HUB_START[0], gen.HUB_START[1], True)
    while True:
        if st == 0: return rules(out)
        info = b.nodes[cur]
        d = info.get('dir') if info['type'] == 'B' else ('N' if st == 1 else 'S')
        segs, term = b.edges[(cur, d)]
        for k, sg in enumerate(segs):
            dl = sg[2]
            if term[0] == 'X' and k == len(segs) - 1: dl = 130
            out.append([dl, 1, sg[4], sg[0], sg[1], 'stop' if (term[0] == 'X' and k == len(segs) - 1) else '', sg[5] + (gen.MOUTH_TURNS.get((term[1][0], b.g, term[1][1]), 0) if sg[4] == 'PORTAL' else 0)])
        if term[0] == 'J':
            cur = term[1]
            lx, lz = (segs[-1][4][0], segs[-1][4][1])
            st = arrive(b.idx[cur], lx, lz); continue
        if term[0] == 'NODE':
            cur = term[1]; st = 1; continue
        if term[0] == 'MOUTH':
            pk, num, tail = term[1]
            for ti, t in enumerate(gen.TAILS.get((pk, b.g, num), [])):
                out.append([t[2], 1, t[4], t[0], t[1], 'fixed', gen.TAIL_TURNS.get((pk, b.g, num), [0] * 9)[ti]])
            if pk == 'BRIDGE':
                cur = gen.BRIDGE_EXIT[b.g]; st = int(lay[b.idx[cur]]); continue
        return rules(out)

B = {}
for g in (1, 2, 3):
    B[g] = gen.Build(g); B[g].build()
ok = n = sok = 0
for r in rec_rides():
    if len(r['steps']) < 2: continue
    n += 1
    g = 1 if r['stage'] <= 105 else (2 if r['stage'] <= 115 else 3)
    s = sim(B[g], r['lay'])
    rec = r['steps'][1:]
    diffs = []
    sdiffs = []
    prev = r['steps'][0][0]
    for i, (t, land, dl, snd) in enumerate(rec):
        gap = t - prev; prev = t
        if i >= len(s): break
        sd, sg, sl, st = s[i]
        if land[:2] != sl[:2] if isinstance(sl, tuple) else False:
            diffs.append(f'#{i} land {land} vs {sl}')
            break
        if dl != sd or gap != sg:
            diffs.append(f'#{i} [{t}] {land[0]},{land[1]} rec d{dl} gap{gap} / ours d{sd} gap{sg}')
        if snd != st:
            sdiffs.append(f'#{i} [{t}] {land[0]},{land[1]} rec turns {snd} / ours {st}')
    ok += not diffs
    sok += not sdiffs
    for d in sdiffs: print('    sound ' + d)
    print(f'g{g} [{r["tick"]}] lay={r["lay"][:12]}: ' + ('ok' if not diffs else ''))
    for d in diffs: print('    ' + d)
print('timing matched', ok, 'of', n)
print('turn sounds matched', sok, 'of', n)
