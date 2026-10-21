"""Chase-time clock between Dreadnoth's target switches: ticks from the engage /
the previous switch, counting only ticks outside a slam phase ([first J, last J+10))
and before the enrage say."""
import re, sys, collections
TICK = re.compile(r'^\[(\d+)\]')
NPC = re.compile(r'^ {8}\[(\w+)\] npc=\(index=(\d+), id=(\w+) \((\d+)\)')
PLINE = re.compile(r'^ {8}\[\w+\] player=')
FACEP = re.compile(r'^ {12}\[facing\] player=\(index=(\d+), name=([^,]+)')
SEQ = re.compile(r'^ {12}\[sequence\] id=(\w+)')
hist = collections.Counter()
TOT = 0
RES = [0, 0]
for path in sys.argv[1:]:
    sess = re.search(r'idx(\d+)', path).group(1)
    tick = 0; inb = False; lives = []; L = None
    for line in open(path, errors='replace'):
        m = TICK.match(line)
        if m: tick = int(m.group(1)); inb = False; continue
        if PLINE.match(line): inb = False; continue
        m = NPC.match(line)
        if m:
            inb = m.group(3) == 'tfa_jelly_boss'
            if inb and m.group(1) == 'add': L = {'sw': [], 'J': [], 'say': None, 'del': None}; lives.append(L)
            if inb and m.group(1) == 'del': L['del'] = tick
            continue
        if not inb or L is None: continue
        m = FACEP.match(line)
        if m:
            if not L['sw'] or L['sw'][-1][1] != m.group(1): L['sw'].append((tick, m.group(1)))
            continue
        m = SEQ.match(line)
        if m and m.group(1) == 'dreadnoth_jump01': L['J'].append(tick)
        if '[say]' in line and L['say'] is None: L['say'] = tick
    global_ = None
    for L in lives:
        if not L['sw']: continue
        # slam phases: groups of J within 7 ticks
        ph = []
        for j in L['J']:
            if ph and j - ph[-1][1] <= 7: ph[-1][1] = j
            else: ph.append([j, j])
        hold = set()
        for a, b in ph: hold.update(range(a, b + 10))
        end = L['say'] or L['del'] or 10**9
        out = []
        for (t0, _), (t1, who) in zip(L['sw'], L['sw'][1:]):
            if t1 >= end: break
            act = sum(1 for t in range(t0, t1) if t not in hold)
            tag = 'resume' if any(t1 == b + 10 for a, b in ph) else ''
            out.append(f"{t1}:{t1-t0}/{act}{'R' if tag else ''}")
            if not tag: hist[act] += 1
        TOT += sum(1 for t in range(L['sw'][0][0], min(end, L['del'] or end)) if t not in hold)
        RES[1] += len(ph); RES[0] += sum(1 for o in out if o.endswith('R'))
        print(sess, 'engage', L['sw'][0][0], 'phases', ph, ' '.join(out))
print(sorted(hist.items()))
print('chase ticks', TOT, 'non-resume switches', sum(hist.values()), 'resumes', RES)
