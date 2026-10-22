"""The Smorks' tile faces: how many ticks ago a raider last stood there.
    python3 bk_smface3.py <all-players transcript>:<from>:<to>...
"""
import re,sys
from collections import Counter
lagc=Counter(); n=0; whoc=Counter()
for spec in sys.argv[1:]:
    p,a,b=spec.rsplit(":",2); a=int(a); b=int(b)
    t=0; cur=None; pl={}; rows=[]; btarget={}; last_ent=[None]
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        if t<a-40 or t>b: continue
        m=re.match(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m: pl.setdefault(m[2],{})[t]=(int(m[5] or m[3]),int(m[6] or m[4])); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) ",line)
        if m: cur=m[2]; continue
        if re.match(r"^ {8}\[",line): cur=None
        if cur=="tfa_bork":
            mm=re.search(r"\[facing\] player=\(index=\d+, name=([^,]+),",line)
            if mm: btarget[t]=mm[1]
        if cur=="tfa_smork" and t>=a:
            mm=re.search(r"\[facing\] coord=\((\d+), (\d+)",line)
            if mm: rows.append((t,(int(mm[1]),int(mm[2]))))
    def at(d,tt):
        ks=[k for k in d if k<=tt]; return d[max(ks)] if ks else None
    for t,c in rows:
        n+=1; best=None
        for k in range(0,12):
            on=[nm for nm,h in pl.items() if at(h,t-k)==c]
            if on: best=(k,on); break
        lagc[best[0] if best else None]+=1
        if best:
            bt=at(btarget,t)
            whoc["btarget" if bt in best[1] else "other"]+=1
print(n, sorted(lagc.items(), key=lambda x:(x[0] is None, x[0] or 0)), whoc)
