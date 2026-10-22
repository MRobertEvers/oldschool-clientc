# Ocellus phase 4 (oc_p4.py): the 2-tick window edges: short gaps at given D (args: D,D,..).
# Run from this directory: python3 oc_p4_fast2.py
import sys; sys.path.insert(0, '.')
import oc_p4
fs = oc_p4.load()
ATT = ('scythe_of_vitur_attack', 'obligator_multihit', 'human_bow', 'zcb_attack', 'xbows_', 'mace', 'glaive', 'blowpipe', 'tumeken', 'staff', 'sang', 'shadow', 'trident', 'whip', 'rapier', 'fang', 'claw')
want = set(int(x) for x in sys.argv[1].split(','))
for f in fs:
    last = {}
    for t in sorted(f.t):
        if t > (f.K or f.last): break
        for n, p in f.t[t].pl.items():
            if n not in f.raiders: continue
            a = [x for x, dl in p.seqs if any(k in x for k in ATT) and 'block' not in x and 'def' not in x]
            if not a: continue
            if t >= f.E and n in last:
                d = ([t - D for D in f.D if t >= D] or [None])[-1]
                if d in want and t - last[n][0] <= 3:
                    print(f.sid, t, 'D+%d' % d, n, 'gap', t - last[n][0], last[n][1], '->', a[0])
            last[n] = (t, a[0])
