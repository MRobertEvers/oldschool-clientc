import json,glob,collections,os
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
gaps=collections.Counter(); dmg=collections.Counter(); n=0
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f)); name=os.path.basename(f)[:30]
    hp={};under={};rec=None
    for e in ev:
        if e["type"]==4:
            if "hitpoints" in e["player"]: hp[e["tick"]]=e["player"]["hitpoints"]>>16; rec=e["player"]["name"]
            if e["player"]["name"] is not None: under[(e["tick"],e["player"]["name"])]=(e["xCoord"],e["yCoord"])
    ticks=sorted(under_t for (under_t,nm) in under if nm==rec)
    ut=[t for t in ticks if under[(t,rec)][0]>=3328]
    if not ut: continue
    # contiguous runs
    runs=[]; s=ut[0]; p=ut[0]
    for t in ut[1:]:
        if t!=p+1: runs.append((s,p)); s=t
        p=t
    runs.append((s,p))
    for a,b in runs:
        drops=[(t,hp[t-1]-hp[t]) for t in range(a,b+1) if t in hp and (t-1) in hp and hp[t]<hp[t-1]]
        # prayer/other healing excluded: only drops
        print(name,"under",a,b,"len",b-a+1,"drops",drops)
        n+=1
        dt=[drops[i+1][0]-drops[i][0] for i in range(len(drops)-1)]
        for g in dt: gaps[g]+=1
        for t,d in drops: dmg[d]+=1
print("gaps",dict(sorted(gaps.items()))); print("dmg",dict(sorted(dmg.items())))
print("---- first chip relative to maze proc / arrival")
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f)); name=os.path.basename(f)[:30]
    proc=sorted(e["tick"] for e in ev if e["type"]==130)
    hp={};pos={};rec=None
    for e in ev:
        if e["type"]==4:
            pos[(e["tick"],e["player"]["name"])]=(e["xCoord"],e["yCoord"])
            if "hitpoints" in e["player"]: hp[e["tick"]]=e["player"]["hitpoints"]>>16; rec=e["player"]["name"]
    ut=sorted(t for (t,nm) in pos if nm==rec and pos[(t,nm)][0]>=3328)
    if not ut: continue
    runs=[];s=ut[0];p=ut[0]
    for t in ut[1:]:
        if t!=p+1: runs.append((s,p)); s=t
        p=t
    runs.append((s,p))
    for a,b in runs:
        pr=max([m for m in proc if m<=a] or [None],key=lambda x:-1 if x is None else x)
        first=[t for t in range(a,b+1) if t in hp and t-1 in hp and hp[t]<hp[t-1]]
        print(name,"proc",pr,"arrive",a,"(+%s)"%(a-pr if pr else "?"),"first chip",first[0] if first else None,"(+%s from proc, +%s from arrival)"%(first[0]-pr, first[0]-a) if first and pr else "")
