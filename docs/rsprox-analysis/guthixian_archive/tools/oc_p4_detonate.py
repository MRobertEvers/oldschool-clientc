# Ocellus phase 4 (oc_p4.py): an unkilled rift's detonation: when, and every
# raider's hits on that tick (with their overhead and distance from it).
# Run from this directory: python3 oc_p4_detonate.py
import sys; sys.path.insert(0, '.')
import oc_p4
for f in oc_p4.load():
    born = {}
    for t in sorted(f.ticks):
        if t > (f.K or f.last) or t < f.E - 600: continue
        T = f.ticks[t]
        for i, n in T.npc.items():
            if 'bombportal' not in n.type: continue
            if n.move == 'add': born[i] = (t, n.after)
            if any('detonate' in s[0] for s in n.seqs):
                b = born.get(i, (None, None))
                n_det = sum(1 for j, m in T.npc.items() if any('detonate' in s[0] for s in m.seqs))
                hits = {nm: [v for ty, v in p.hits if ty in (16, 17, 12, 13)] for nm, p in T.pl.items() if nm in f.raiders}
                d = ([t - D for D in f.D if t >= D] or ['E%+d' % (t - f.E)])[-1]
                print(f.sid, t, 'D+%s' % d, 'rift', i % 1000, 'age', t - b[0] if b[0] else '?', 'detonating', n_det,
                      {nm[:8]: h for nm, h in hits.items()})
