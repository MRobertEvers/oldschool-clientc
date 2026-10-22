# Ocellus phase 4 (oc_p4.py): his heals in phase 4 (the unclaimed orbs) and nex2021_nex_heal.
# Run from this directory: python3 oc_p4_bheal.py
import sys; sys.path.insert(0, '.')
import oc_p4
fs = oc_p4.load()
for f in fs:
    for t in range(f.E, (f.K or f.last) + 1):
        T = f.t.get(t)
        if not T: continue
        b = T.npc.get(f.boss)
        heals = [v for ty, v in (b.hits if b else []) if ty == 6]
        snd = [s for k, s in T.top if 'id=5281' in s]
        if heals or snd:
            d = [t - D for D in f.D if t >= D][-1]
            print(f.sid, t, 'D+%d' % d, 'heals', heals, 'sound', [s[s.find('delay'):s.find('delay')+10] if 'delay' in s else 'd0' for s in snd], [s[s.find('range'):] for s in snd])
