#!/usr/bin/env python3
"""The Xarpus P2 stack tour (QD.XARP.TOUR, script/plugins/quest_driver/tob_xarpus.lua).

The step-back stack holds B_k (2+ off his 5x5), steps to A_k (beside him,
Chebyshev 2 from B_k), then runs to B_k+1 (within 2 of A_k, 2+ from B_k).
Each spit leaves a permanent pool on B_k. So the stack needs a path through
the B tiles that never comes back to one: the longest simple path in that
graph. Warnsdorff DFS (fewest onward moves first), seeded, from the west face.
Room-local tiles; the combat form's SW is (32,33).

  python3 tools/xarpus_stack_tour.py   -> the tour and its A's
"""
import sys, random
sys.setrecursionlimit(10000)
SW=(32,33); N=5; AR=(27,28,41,42)
def gap(x,z):
    gx=max(SW[0]-x, x-(SW[0]+N-1),0); gz=max(SW[1]-z, z-(SW[1]+N-1),0); return max(gx,gz)
def beside(x,z):
    gx=max(SW[0]-x, x-(SW[0]+N-1),0); gz=max(SW[1]-z, z-(SW[1]+N-1),0); return (gx==1 and gz==0) or (gx==0 and gz==1)
ch=lambda a,b: max(abs(a[0]-b[0]),abs(a[1]-b[1]))
tiles=[(x,z) for x in range(AR[0],AR[2]+1) for z in range(AR[1],AR[3]+1) if 2<=gap(x,z)<=4]
A=[(x,z) for x in range(AR[0],AR[2]+1) for z in range(AR[1],AR[3]+1) if beside(x,z)]
E={}
for b in tiles:
    E[b]={}
    for a in A:
        if ch(a,b)!=2: continue
        for b2 in tiles:
            if ch(a,b2)<=2 and ch(b,b2)>=2 and b2 not in E[b]: E[b][b2]=a
reach=[b for b in tiles if E[b]]
print("B tiles", len(tiles), "with moves", len(reach))
best=[]
def run(start, seed):
    rnd=random.Random(seed)
    path=[start]; used={start}; nodes=[0]
    def dfs():
        global best
        nodes[0]+=1
        if len(path)>len(best): best=list(path)
        if nodes[0]>200000: return True
        cur=path[-1]
        nx=[b for b in E[cur] if b not in used]
        # Warnsdorff: fewest onward moves first, random tie-break
        nx.sort(key=lambda b:(sum(1 for c in E[b] if c not in used), rnd.random()))
        for b in nx:
            path.append(b); used.add(b)
            if dfs(): return True
            path.pop(); used.discard(b)
        return False
    dfs()
for seed in range(40):
    for st in [(30,35),(30,36),(30,34)]:
        run(st, seed)
print("tour", len(best))
print(best)
# A's
print([E[a][b] for a,b in zip(best,best[1:])])
