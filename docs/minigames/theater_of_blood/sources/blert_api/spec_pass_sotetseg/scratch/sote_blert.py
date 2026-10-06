import json,glob,collections,sys,os
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
ATT={7:"M",8:"B",9:"D"}
files=sorted(glob.glob(D+"/m*_13.json"))
tot_gaps=collections.Counter()
for f in files:
    name=os.path.basename(f); mode,scale=name.split("_")[:2]
    ev=json.load(open(f))
    att=sorted([(e["tick"],ATT.get(e["npcAttack"]["attack"],"?")) for e in ev if e["type"]==10])
    mz=sorted([(e["tick"],e["soteMaze"]["maze"]) for e in ev if e["type"]==130])
    seq="".join(a for _,a in att)
    gaps=collections.Counter(att[i+1][0]-att[i][0] for i in range(len(att)-1))
    # balls between death balls (reset at start) with maze marks
    out=[];cnt=0
    for t,a in att:
        if a=="B": cnt+=1
        elif a=="D": out.append(cnt); cnt=0
    print(name[:34],"att",len(att),"gaps",dict(sorted(gaps.items())),"balls_before_D",out,"mazes",mz)
    print("   ",seq)
    print("    ticks",[t for t,_ in att][:30])
