import re
from collections import Counter
A='/Users/matthewevers/.rsprox-index-cache/home-raw/.rsprox/binary/archive/'
FIGHTS=[(3268,2261,2352),(1144,3947,4034),(3259,10635,10967),(3259,12082,12440),(3268,3200,3417),(1144,4918,5019),(1144,5557,5667),(1144,6185,6303),(1144,6732,6844),(1144,7378,7453),(1144,8598,8659),(1144,9101,9224),(1144,9589,9688)]
TICK=re.compile(r"^\[(\d+)\]$")
ME=re.compile(r"\[(?:idle|walk|run|teleport)\] player=\(index=\d+, name=Z-Kris, coord=\((\d+), (\d+), \d+\)\)(?:, newcoord=\((\d+), (\d+), \d+\))?")
ADD=re.compile(r"\[add\] npc=\(index=(\d+), id=(crystal_hunllef[a-z_]*) \(\d+\), coord=\((\d+), (\d+), \d+\)")
HUN=re.compile(r"\] npc=\(index=\d+, id=crystal_hunllef_(?:melee|ranged|magic)[a-z_]* \(\d+\), coord=\((\d+), (\d+), \d+\)\)(?:, newcoord=\((\d+), (\d+), \d+\))?")
dist=Counter(); bossd=Counter(); rel=Counter()
for s,t0,t1 in FIGHTS:
    tick=0; me=None; boss=None
    for line in open(A+f'idx{s}.txt',errors='replace'):
        m=TICK.match(line)
        if m: tick=int(m.group(1)); continue
        if tick<t0: continue
        if tick>t1: break
        m=ME.search(line)
        if m: g=m.groups(); me=(int(g[2]),int(g[3])) if g[2] else (int(g[0]),int(g[1]))
        m=HUN.search(line)
        if m: g=m.groups(); boss=(int(g[2]),int(g[3])) if g[2] else (int(g[0]),int(g[1]))
        m=ADD.search(line)
        if m and 'crystals' in m.group(2) and me:
            x,z=int(m.group(3)),int(m.group(4))
            dist[max(abs(x-me[0]),abs(z-me[1]))]+=1
            rel[(x-me[0],z-me[1])]+=1
            if boss: bossd[(x-boss[0],z-boss[1])]+=1
print('cheb dist from player',sorted(dist.items()))
print('rel to boss sw',sorted(bossd.items()))
