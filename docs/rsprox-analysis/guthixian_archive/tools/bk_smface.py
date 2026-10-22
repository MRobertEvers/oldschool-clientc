"""The Smorks' faces: entity faces on Bork's target or another, tile faces on whose tile.
    python3 bk_smface.py <all-players transcript>:<from>:<to>...
"""
import re,sys
from collections import Counter
tally=Counter()
for spec in sys.argv[1:]:
    p,a,b=spec.rsplit(":",2); a=int(a); b=int(b)
    t=0; cur=None; pl={}; btarget={}; rows=[]
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        if t<a-30 or t>b: continue
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
            if mm: rows.append((t,"coord",(int(mm[1]),int(mm[2]))))
            mm=re.search(r"\[facing\] player=\(index=\d+, name=([^,]+),",line)
            if mm: rows.append((t,"player",mm[1]))
    def at(d,tt):
        ks=[k for k in d if k<=tt]; return d[max(ks)] if ks else None
    for t,kind,v in rows:
        bt=at(btarget,t)
        if kind=="coord":
            on=[n for n,h in pl.items() if at(h,t)==v]; on1=[n for n,h in pl.items() if at(h,t-1)==v]
            who = "btarget" if bt in on+on1 else ("other" if on+on1 else "nobody")
            tally[("coord",who)]+=1
        else:
            tally[("player","btarget" if v==bt else "other")]+=1
print(tally)
