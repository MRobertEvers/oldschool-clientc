"""Every map anim (id, delays, heights), projectile and loc of Bork's arena, recordings against our tick logs.
    python3 bk_sigs.py <ticklog.tsv>[,<ticklog.tsv>] <transcript>...
"""
import re,sys
from collections import Counter,defaultdict
inbox=lambda x,z: 4180<=x<=4202 and 5586<=z<=5612
ANIM=re.compile(r"\[map_anim\] id=(\w+) \((\d+)\)(?:, delay=(\d+))?(?:, height=(\d+))?, coord=\((\d+), (\d+), 0\)")
PROJ=re.compile(r"\[map_projanim\w*\] id=(\w+) \((\d+)\), starttime=(\d+), endtime=(\d+), angle=(\d+), progress=(\d+), startheight=(\d+), endheight=(\d+)")
LOC=re.compile(r"\[(loc_add_change\w*|loc_del)\] (?:id=(\w+) \((\d+)\), )?coord=\((\d+), (\d+), 0\), shape=(\d+), rotation=(\d+)")
ra=defaultdict(Counter); rp=Counter(); rl=Counter(); names={}
for p in sys.argv[2:]:
    for line in open(p,errors="replace"):
        m=ANIM.search(line)
        if m and inbox(int(m[5]),int(m[6])):
            ra[int(m[2])][(int(m[3] or 0),int(m[4] or 0))]+=1; names[int(m[2])]=m[1]; continue
        m=PROJ.search(line)
        if m:
            rp[(int(m[2]),int(m[3]),int(m[4]),int(m[5]),int(m[6]),int(m[7]),int(m[8]))]+=1; names[int(m[2])]=m[1]; continue
        m=LOC.search(line)
        if m and inbox(int(m[4]),int(m[5])):
            rl[(int(m[3] or -1),int(m[6]))]+=1; names[int(m[3] or -1)]=m[2] or "del"
oa=defaultdict(Counter); op=Counter(); ol=Counter()
for f in sys.argv[1].split(","):
    for line in open(f):
        v=line.rstrip("\n").split("\t")
        if len(v)<9 or not v[1].isdigit(): continue
        k=v[2]
        def tin(c):
            c=int(c); return inbox(((c>>14)&16383)-2240, (c&16383)+5440)
        if k in("map_spotanim","loc_set") and not tin(v[3]): continue
        if k=="map_spotanim": oa[int(v[4])][(int(v[6]),int(v[5]))]+=1
        elif k=="projectile": op[(int(v[6]),int(v[7]),int(v[8]))]+=1
        elif k=="loc_set": ol[(int(v[4]),int(v[5]))]+=1
print("== map anims (id: delays,heights)")
for i in sorted(set(ra)|set(oa)):
    r=sorted(ra[i]); o=sorted(oa[i])
    rd=sorted({d for d,h in r}); od=sorted({d for d,h in o}); rh=sorted({h for d,h in r}); oh=sorted({h for d,h in o})
    flag="" if (rd==od and rh==oh) else "  <<"
    print(f"{i} {names.get(i,'?')[:40]:40s} R n={sum(ra[i].values())} d={rd} h={rh} | O n={sum(oa[i].values())} d={od} h={oh}{flag}")
print("== projectiles (id, start, end, angle, progress, heights)")
for i in sorted({k[0] for k in rp}|{k[0] for k in op}):
    r=sorted({k[1:] for k in rp if k[0]==i}); o=sorted({k[1:] for k in op if k[0]==i})
    print(i, names.get(i,'?')[:40], "R", r[:8], "n", sum(v for k,v in rp.items() if k[0]==i), "| O", o[:8])
print("== locs (id, shape)")
for k in sorted(set(rl)|set(ol)):
    print(k, names.get(k[0],'?'), "R", rl.get(k,0), "O", ol.get(k,0))
