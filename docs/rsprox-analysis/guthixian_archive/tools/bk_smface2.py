"""The Smorks' tile faces relative to Bork's tile (lag 0..2).
    python3 bk_smface2.py <all-players transcript>:<from>:<to>...
"""
import re,sys
from collections import Counter
rel=Counter(); n=0
for spec in sys.argv[1:]:
    p,a,b=spec.rsplit(":",2); a=int(a); b=int(b)
    t=0; cur=None; boss={}; rows=[]
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        if t<a-30 or t>b: continue
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m:
            cur=m[2]
            if cur=="tfa_bork": boss[t]=(int(m[5] or m[3]),int(m[6] or m[4]))
            continue
        if re.match(r"^ {8}\[",line): cur=None
        if cur=="tfa_smork" and t>=a:
            mm=re.search(r"\[facing\] coord=\((\d+), (\d+)",line)
            if mm: rows.append((t,(int(mm[1]),int(mm[2]))))
    def at(d,tt):
        ks=[k for k in d if k<=tt]; return d[max(ks)] if ks else None
    for t,c in rows:
        for lag in (0,1,2):
            sw=at(boss,t-lag)
            if sw: rel[(lag,c[0]-sw[0],c[1]-sw[1])]+=1
        n+=1
print(n, rel.most_common(12))
