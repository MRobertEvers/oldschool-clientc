import re,json
from collections import Counter,defaultdict
A='/Users/matthewevers/.rsprox-index-cache/home-default/.rsprox/binary/archive/'
FIGHTS=[(3268,2261,2352,31),(1144,3947,4034,23),(3259,10635,10967,23),(3259,12082,12440,17),
        (3268,3200,3417,25),(1144,4918,5019,17),(1144,5557,5667,17),(1144,6185,6303,17),(1144,6732,6844,31),(1144,7378,7453,31),(1144,8598,8659,17),(1144,9101,9224,23),(1144,9589,9688,25)]
TICK=re.compile(r"^\[(\d+)\]$")
LOC=re.compile(r"\[loc_add_change_v2\] id=(prif_gauntlet_floor_tile_01[a-z_]*) \(\d+\), coord=\((\d+), (\d+)")
ROT={31:0,25:1,17:2,23:3}
def unrot(x,z,r,n=12):
    # raw placed = rotate(template, r) cw; invert
    for _ in range((4-r)%4):
        x,z = z, n-1-x
    return x,z
pats=defaultdict(Counter); timeline={}
for s,t0,t1,start in FIGHTS:
    raw=A.replace('home-default','home-raw')+f'idx{s}.txt'
    tick=0; warn=defaultdict(set)
    for line in open(raw,errors='replace'):
        m=TICK.match(line)
        if m: tick=int(m.group(1)); continue
        if tick<t0: continue
        if tick>t1: break
        m=LOC.search(line)
        if m and m.group(1).replace('_hm','').endswith('warning'):
            warn[tick].add((int(m.group(2)),int(m.group(3))))
    allx=[c[0] for v in warn.values() for c in v]; allz=[c[1] for v in warn.values() for c in v]
    x0,z0=min(allx),min(allz)
    if max(allx)-x0!=11 or max(allz)-z0!=11: print('arena bounds?',s,t0,x0,max(allx),z0,max(allz))
    seq=[]
    for t in sorted(warn):
        tiles=frozenset(unrot(x-x0,z-z0,ROT[start]) for x,z in warn[t])
        key=tuple(sorted(tiles))
        pats[key][(s,t0)]+=1
        seq.append((t-t0,len(tiles),key))
    timeline[f'{s}@{t0}']=seq
print('distinct patterns',len(pats))
def show(key):
    g=[['.']*12 for _ in range(12)]
    for x,z in key: g[11-z][x]='#'
    return [''.join(r) for r in g]
plist=sorted(pats, key=lambda k:(len(k),k))
ids={k:i for i,k in enumerate(plist)}
for k in plist:
    print('P%d n=%d tiles=%d seen=%d'%(ids[k],len(pats[k]),len(k),sum(pats[k].values())))
    for r in show(k): print('   ',r)
for f,seq in timeline.items():
    print(f, ' '.join(f"{t}:P{ids[k]}" for t,n,k in seq))
