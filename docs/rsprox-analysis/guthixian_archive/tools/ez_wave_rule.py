"""Our melee waves against the cleave rule (§4): 13 ring tiles, the 3 opposite
the tank safe, delays 5..7.

    python3 ez_wave_rule.py <ticklog.tsv>
"""
import sys
from collections import defaultdict
boss={}; slot=None; ptile={}; wave=defaultdict(dict)
for line in open(sys.argv[1]):
    f=line.rstrip("\n").split("\t")
    if len(f)<9 or not f[1].isdigit(): continue
    t=int(f[1]); k=f[2]; v=f[3:]
    if k=="npc_spawn" and v[1]=="16652": slot=v[0]; c=int(v[2]); boss[t]=((c>>14)&16383,c&16383)
    if k=="npc_tile" and v[0]==slot: boss[t]=(int(v[1]),int(v[2]))
    if k=="player_tile": ptile[t]=(int(v[1]),int(v[2]))
    if k=="map_spotanim" and v[1]=="4394":
        c=int(v[0]); wave[t][((c>>14)&16383,c&16383)]=int(v[3])
def at(d,t):
    b=None
    for k in sorted(d):
        if k<=t: b=d[k]
    return b
ok=0
for t,tiles in sorted(wave.items()):
    sx,sz=at(boss,t); px,pz=at(ptile,t-1) if at(ptile,t-1) else at(ptile,t)
    ring=[(sx+dx,sz+dz) for dx in range(-1,4) for dz in range(-1,4) if not(0<=dx<=2 and 0<=dz<=2)]
    tx,tz=px-sx,pz-sz
    mx,mz=2-tx,2-tz
    # safe: mirror tile and its two neighbours along the same side
    if mx in(-1,3): safe={(mx,z) for z in(mz-1,mz,mz+1)}
    else: safe={(x,mz) for x in(mx-1,mx,mx+1)}
    # corner mirror: neighbours along... keep side rule ambiguous; report
    exp={(sx+a,sz+b) for a,b in [(r[0]-sx,r[1]-sz) for r in ring] if (a,b) not in safe}
    got=set(tiles)
    dl=sorted(set(tiles.values()))
    good= got==exp and len(got)==13 and set(dl)<= {5,6,7}
    ok+=good
    if not good: print(t,"boss",(sx,sz),"tank rel",(tx,tz),"n",len(got),"missing",sorted((a-sx,b-sz) for a,b in exp-got),"extra",sorted((a-sx,b-sz) for a,b in got-exp),dl)
print("waves",len(wave),"ok",ok)
