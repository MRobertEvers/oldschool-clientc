import json,glob,collections,os,sys
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
pat=sys.argv[1] if len(sys.argv)>1 else "m11"
rows=[]
for f in sorted(glob.glob(D+"/%s_*_13.json"%pat)):
    ev=json.load(open(f)); name=os.path.basename(f)[:30]
    hp={}; pos={}; rec=None
    for e in ev:
        if e["type"]==4:
            pos[(e["tick"],e["player"]["name"])]=(e["xCoord"],e["yCoord"])
            if "hitpoints" in e["player"]: hp[e["tick"]]=e["player"]["hitpoints"]>>16; rec=e["player"]["name"]
    if not hp: continue
    att=sorted([e for e in ev if e["type"]==10],key=lambda e:e["tick"])
    ticks=sorted(hp)
    for e in att:
        if e["npcAttack"].get("target")!=rec: continue
        a=e["npcAttack"]["attack"]
        if a not in (8,9): continue
        t0=e["tick"]; sx,sy=e["xCoord"],e["yCoord"]
        p=pos.get((t0-1,rec)) 
        d=None if p is None else max(abs(p[0]-(sx+2)),abs(p[1]-(sy+2)))
        # first hp drop after t0 (within 25 ticks)
        drop=None
        for t in range(t0+1,t0+26):
            if t in hp and (t-1) in hp and hp[t]<hp[t-1]:
                drop=(t-t0,hp[t-1]-hp[t]); break
        rows.append((name,{8:"ball",9:"DEATH"}[a],t0,d,drop))
for r in rows:
    if r[1]=="DEATH": print(*r)
print("balls:",collections.Counter((r[4][0] if r[4] else None) for r in rows if r[1]=="ball"))
# ball delay vs distance
bd=collections.defaultdict(collections.Counter)
for r in rows:
    if r[1]=="ball" and r[3] is not None: bd[r[3]][r[4][0] if r[4] else None]+=1
for k in sorted(bd): print("dist from centre",k,dict(bd[k]))
