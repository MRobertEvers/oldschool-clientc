import json,glob,os
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
ATT={7:"M",8:"B",9:"D"}
for f in sorted(glob.glob(D+"/m1?_*_13.json")):
    ev=json.load(open(f))
    att=sorted([(e["tick"],ATT.get(e["npcAttack"]["attack"],"?")) for e in ev if e["type"]==10])
    mz=sorted(e["tick"] for e in ev if e["type"]==130)
    # reactivation: type? print segments with maze: index of balls before proc and after
    cnt=0; seq=[]
    for t,a in att:
        if a=="B": cnt+=1; seq.append(('B%d'%cnt,t))
        if a=="D": seq.append(('D',t)); cnt=0
    for m in mz:
        before=[s for s in seq if s[1]<=m][-1:]
        after=[s for s in seq if s[1]>m][:1]
        print(os.path.basename(f)[:12], 'proc',m,'last<=proc',before,'first>proc',after)
