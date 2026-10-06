import collections,sys
path=sys.argv[1]
rows=[l.rstrip("\n").split("\t") for l in open(path)][1:]
bytick=collections.defaultdict(set)
rawcount=collections.Counter()
for r in rows:
    if r[2]=="npc_anim" and r[5] in ("8138","8139"):
        bytick[int(r[1])].add(int(r[5])); rawcount[int(r[1])]+=1
hits=[int(r[1]) for r in rows if r[2]=="hit_player"]
proj=[(int(r[1]),r[6]) for r in rows if r[2]=="projectile"]
ticks=sorted(bytick)
seq="".join("M" if 8138 in v else "B" for t,v in sorted(bytick.items()))
print("attacks",len(ticks),"melee",seq.count("M"),"ball",seq.count("B"))
print("gaps",dict(collections.Counter(b-a for a,b in zip(ticks,ticks[1:]))))
print(seq)
md=collections.Counter()
for t,v in bytick.items():
    if 8138 in v:
        for d in range(0,4):
            if (t+d) in hits: md[d]+=1; break
        else: md["none"]+=1
print("melee -> hit_player delay",dict(md))
d=[t for t,s in proj if s=="1604"]
print("1604 launches",d)
print("ticks with two npc_anim rows (death ball tick)",[t for t,c in sorted(rawcount.items()) if c>1])
