import json,glob,collections,os
D="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid/build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_sote_raw"
first_move=collections.Counter(); arr=collections.Counter(); arena=collections.Counter()
for f in sorted(glob.glob(D+"/m1*_13.json")):
    ev=json.load(open(f)); name=os.path.basename(f)[:30]
    proc=sorted(e["tick"] for e in ev if e["type"]==130)
    pos=collections.defaultdict(dict)
    for e in ev:
        if e["type"]==4: pos[e["player"]["name"]][e["tick"]]=(e["xCoord"],e["yCoord"])
    for nm,d in pos.items():
        ts=sorted(d)
        ut=[t for t in ts if d[t][0]>=3328]
        if not ut: continue
        runs=[];s=ut[0];p=ut[0]
        for t in ut[1:]:
            if t!=p+1: runs.append((s,p)); s=t
            p=t
        runs.append((s,p))
        for a,b in runs:
            pr=max([m for m in proc if m<=a] or [None],key=lambda x:-1 if x is None else x)
            if pr is None: continue
            start=d[a]; mv=None
            for t in range(a+1,b+1):
                if d.get(t)!=start: mv=t; break
            seq=[(t-a,d[t][0]-start[0],d[t][1]-start[1]) for t in range(a,min(b,a+9)+1) if t in d]
            if mv: first_move[mv-a]+=1
            arr[a-pr]+=1
            print(name,nm,"proc",pr,"arrive +%d"%(a-pr),"first move +%d after arrival"%((mv-a) if mv else -1),seq[:8])
    # overworld teammates: when do they appear in the arena far end? first tick with y<4315? after proc
print("arrival offsets",dict(arr),"first move after arrival",dict(first_move))
