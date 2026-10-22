# Ocellus phase 4 (oc_p4.py): each deflect target against party order and distance.
# Run from this directory: python3 oc_p4_target.py
import sys, re; sys.path.insert(0, '.')
import oc_p4
fs = oc_p4.load()
for f in fs:
    st = oc_p4.state(f)
    for D in f.D:
        b0 = f.t[D].npc.get(f.boss)
        if not b0 or not b0.faces: continue
        tgt = re.sub(r'.*name=([^,]*),.*', r'\1', b0.faces[0])
        # his face just before D (from the last face block in D-60..D-1)
        prev = None
        for t in range(D - 40, D):
            b = f.t.get(t) and f.t[t].npc.get(f.boss)
            for x in (b.faces if b else []):
                prev = re.sub(r'.*name=([^,]*),.*', r'\1', x) if 'player=' in x else x[:20]
        dist = {n: oc_p4.box(b0.before, 3, st[D][0][n]) for n in f.raiders if n in st[D][0]}
        print(f.sid, D, 'target', tgt, '| last face before', prev, '| dist', dist)
