"""Every area sound of the room (id, delay, range, size, loops) over the
recordings against our tick log.

    python3 ez_sounds.py <ticklog.tsv> <transcript>...
"""
import re,sys
from collections import Counter
SND=re.compile(r"\[sound_area\] id=(\d+), loops=(\d+)(?:, delay=(\d+))?, range=(\d+)(?:, size=(\d+))?, coord=\((\d+), (\d+), (\d+)\)")
rec=Counter()
for p in sys.argv[2:]:
    for line in open(p,errors="replace"):
        m=SND.search(line)
        if not m: continue
        x,z,l=int(m[6]),int(m[7]),int(m[8])
        if l!=0 or not(4050<=x<=4075 and 5525<=z<=5545): continue
        if m[1] in ("211","212"): continue
        rec[(int(m[1]),int(m[3] or 0),int(m[4]),int(m[5] or 0),int(m[2]))]+=1
ours=Counter()
for line in open(sys.argv[1]):
    f=line.rstrip("\n").split("\t")
    if len(f)<10 or f[2]!="sound" or "area" not in f[9]: continue
    s=re.search(r"size=(\d+)",f[9])
    ours[(int(f[4]),int(f[6]),int(f[8]),int(s.group(1)) if s else 0,int(f[5]))]+=1
ids=sorted({k[0] for k in rec}|{k[0] for k in ours})
for i in ids:
    r={k:v for k,v in rec.items() if k[0]==i}; o={k:v for k,v in ours.items() if k[0]==i}
    rs=sorted({(k[1],k[2],k[3],k[4]) for k in r}); os_=sorted({(k[1],k[2],k[3],k[4]) for k in o})
    flag="" if rs==os_ else "  <<"
    print(i, "R", [(a,b,c,d,r[(i,a,b,c,d)]) for a,b,c,d in rs], "| O", [(a,b,c,d) for a,b,c,d in os_], flag)
