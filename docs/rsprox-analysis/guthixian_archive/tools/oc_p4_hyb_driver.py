# Ocellus phase 4 (oc_p4.py): each hybrid's prayed value against the cast
# number, his HUD health (varbits 6099/6100) and the raiders standing.
# Run from this directory: python3 oc_p4_hyb_driver.py
import sys; sys.path.insert(0, '.')
import oc_p4
for f in oc_p4.load():
    hp = base = None; hps = {}
    for t in sorted(f.ticks):
        if t > (f.K or f.last) + 6: break
        v = f.ticks[t].var
        if 'hpbar_hud_hp' in v: hp = v['hpbar_hud_hp'][1]
        if 'hpbar_hud_basehp' in v: base = v['hpbar_hud_basehp'][1]
        hps[t] = (hp, base)
    n = 0; out = []
    for D in f.D:
        for c in (D + 24, D + 52):
            T = f.t.get(c); b = T and T.npc.get(f.boss)
            if not b or not any('melee_hybrid' in s[1] for s in b.spots): continue
            TL = f.t.get(c + 5)
            vals = [min(v for ty, v in p.hits if ty in (16, 17)) for nm, p in (TL.pl.items() if TL else []) if nm in f.raiders and any(ty in (16, 17) for ty, v in p.hits)]
            h, bs = hps.get(c, (None, None))
            pct = round(100 * h / bs, 1) if h and bs else None
            out.append('n%d:%s %s%% x%d' % (n, min(vals) if vals else '-', pct, len(vals)))
            n += 1
    print(f.sid, f.E, ' | '.join(out))
