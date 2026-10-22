"""The Smorks' tile faces: the offset to the nearest raider's tile.
    python3 bk_smface4.py <all-players transcript>:<from>:<to>...
"""
import re,sys
from collections import Counter
off=Counter(); n=0
for spec in sys.argv[1:]:
    p,a,b=spec.rsplit(":",2); a=int(a); b=int(b)
    t=0; cur=None; pl={}; rows=[]
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        if t<a-40 or t>b: continue
        m=re.match(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m: pl.setdefault(m[2],{})[t]=(int(m[5] or m[3]),int(m[6] or m[4])); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) ",line)
        if m: cur=m[2]; continue
        if re.match(r"^ {8}\[",line): cur=None
        if cur=="tfa_smork" and t>=a:
            mm=re.search(r"\[facing\] coord=\((\d+), (\d+)",line)
            if mm: rows.append((t,(int(mm[1]),int(mm[2]))))
    def at(d,tt):
        ks=[k for k in d if k<=tt]; return d[max(ks)] if ks else None
    for t,c in rows:
        best=None
        for nm,h in pl.items():
            q=at(h,t)
            if not q: continue
            d=(c[0]-q[0],c[1]-q[1])
            if best is None or max(abs(d[0]),abs(d[1]))<max(abs(best[0]),abs(best[1])): best=d
        if best: off[best]+=1; n+=1
print(n, off.most_common(15))
