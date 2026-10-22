"""The special's star against its rule (ezekiel_plan.md §5): the ring at 34, the
3-wide arms at 36, then 38 and 40, clipped to the 9x9 arena.

    python3 ez_star_rule.py rec <transcript>...    (42/42 of 4300..4315)
    python3 ez_star_rule.py ours <ticklog.tsv>
"""
import re,sys
from collections import defaultdict
def expected(sx,sz,x0,z0):
    # arena 9x9 at (x0..x0+8, z0..z0+8)
    e={}
    for dx in range(-1,4):
        for dz in range(-1,4):
            if 0<=dx<=2 and 0<=dz<=2: continue
            e[(sx+dx,sz+dz)]=34
    for k in range(3):
        for a,b in ((k,-2),(k,4),(-2,k),(4,k)):
            e.setdefault((sx+a,sz+b),36)
    for a,b in ((1,-3),(1,5),(-3,1),(5,1)): e[(sx+a,sz+b)]=38
    for a,b in ((1,-4),(1,6),(-4,1),(6,1)): e[(sx+a,sz+b)]=40
    return {t:d for t,d in e.items() if x0<=t[0]<=x0+8 and z0<=t[1]<=z0+8}
def check(spec, boss, x0, z0, label):
    ok=bad=0
    for t,tiles in sorted(spec.items()):
        b=None
        for k in sorted(boss):
            if k<=t: b=boss[k]
        e=expected(b[0],b[1],x0,z0)
        if e==tiles: ok+=1
        else:
            bad+=1
            print(label,t,"boss",b,"missing",sorted(set(e)-set(tiles))[:6],"extra",sorted(set(tiles)-set(e))[:6],
                  "delay-diff",[(k,e[k],tiles[k]) for k in e if k in tiles and e[k]!=tiles[k]][:4])
    print(label,"ok",ok,"bad",bad)
mode=sys.argv[1]
if mode=="rec":
    for p in sys.argv[2:]:
        spec=defaultdict(dict); boss={}; t=0
        for line in open(p,errors="replace"):
            m=re.match(r"^\[(\d+)\]",line)
            if m: t=int(m.group(1)); continue
            m=re.search(r"npc=\(index=\d+, id=tfa_ezekiel \(16652\), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
            if m: boss[t]=(int(m.group(3) or m.group(1)),int(m.group(4) or m.group(2)))
            m=re.search(r"vfx_ezekiel_star_explosion_spawn01 \(4170\), delay=(\d+), coord=\((\d+), (\d+)",line)
            if m: spec[t][(int(m[2]),int(m[3]))]=int(m[1])
        check(spec,boss,4059,5531,p.split('/')[-1])
else:
    spec=defaultdict(dict); boss={}; slot=None
    for line in open(sys.argv[2]):
        f=line.rstrip("\n").split("\t")
        if len(f)<9 or not f[1].isdigit(): continue
        t=int(f[1]); k=f[2]; v=f[3:]
        if k=="npc_spawn" and v[1]=="16652": slot=v[0]; c=int(v[2]); boss[t]=((c>>14)&16383,c&16383)
        if k=="npc_tile" and v[0]==slot: boss[t]=(int(v[1]),int(v[2]))
        if k=="map_spotanim" and v[1]=="4170":
            c=int(v[0]); spec[t][((c>>14)&16383,c&16383)]=int(v[3])
    check(spec,boss,6427,155,"ours")
