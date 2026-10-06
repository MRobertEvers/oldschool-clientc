import json,glob,os,collections
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
ATT={7:"M",8:"B",9:"D"}
tot=collections.Counter(); proc_att=collections.Counter()
for mode in ("m11","m10","m12"):
    for f in sorted(glob.glob(D+"/%s_*_13.json"%mode)):
        ev=json.load(open(f))
        att=sorted([(e["tick"],ATT.get(e["npcAttack"]["attack"],"?")) for e in ev if e["type"]==10])
        mz=sorted(e["tick"] for e in ev if e["type"]==130)
        types=collections.Counter(e["type"] for e in ev)
        cnt=0; segstart=None; mazes_in=[]; onproc=False
        for t,a in att:
            if a=="B":
                cnt+=1
                if segstart is None: segstart=t
                if t in mz: onproc=True
            if a in "BM" and t in mz: proc_att[(mode,a)]+=1
            if a=="D":
                inm=[m for m in mz if segstart is not None and segstart<=m<=t]
                key=(mode,cnt,'maze' if inm else 'nomaze', 'ballOnProc' if onproc else '')
                tot[key]+=1
                if cnt!=10: print(os.path.basename(f), 'seg',cnt,'D at',t,'mazes in seg',inm,'onproc',onproc)
                cnt=0; segstart=None; onproc=False
for k,v in sorted(tot.items()): print(k,v)
print('attacks ON proc tick', dict(proc_att))
