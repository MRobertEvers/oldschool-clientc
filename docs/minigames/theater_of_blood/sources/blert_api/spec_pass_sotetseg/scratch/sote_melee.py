import json,glob,collections,os,sys
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
pat=sys.argv[1] if len(sys.argv)>1 else "m11"
tab=collections.Counter(); dists=collections.Counter(); first=[]
for f in sorted(glob.glob(D+"/%s_*_13.json"%pat)):
    ev=json.load(open(f))
    pos={}  # (tick,name)->(x,y)
    for e in ev:
        if e["type"]==4: pos[(e["tick"],e["player"]["name"])]=(e["xCoord"],e["yCoord"])
    att=sorted([e for e in ev if e["type"]==10],key=lambda e:e["tick"])
    if att: first.append(att[0]["tick"])
    for e in att:
        a=e["npcAttack"]["attack"]; tgt=e["npcAttack"].get("target")
        sx,sy=e["xCoord"],e["yCoord"]
        # T-1 scan: position at tick-1
        p=pos.get((e["tick"]-1,tgt)) if tgt else None
        if p is None: tab[("nopos",a)]+=1; continue
        dx=max(sx-p[0],0,p[0]-(sx+4)); dy=max(sy-p[1],0,p[1]-(sy+4))
        cheb=max(dx,dy); card=(dx+dy)
        kind="adj" if (cheb<=1) else "far"
        cardinal = (cheb==1 and (dx==0 or dy==0)) or cheb==0
        tab[(kind, "card" if cardinal else "diag/none", {7:"melee",8:"ball",9:"death"}.get(a,a))]+=1
print("first attack ticks",sorted(first))
for k,v in sorted(tab.items(),key=str): print(k,v)
