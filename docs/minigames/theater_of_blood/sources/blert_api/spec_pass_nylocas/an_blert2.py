import json,glob,collections
files=sorted(glob.glob('blert_nylo_raw/*.json'))
hold=collections.Counter(); firstsw=collections.Counter(); nchg=collections.Counter()
splitlag=collections.Counter(); splitcount=collections.Counter(); splitoff=collections.Counter()
natsplit=collections.Counter()
aggroswap=collections.Counter()
style=lambda i:(i-8342)%3
for f in files:
    ev=json.load(open(f))
    seqs=collections.defaultdict(list); spawn={}; death={}
    for e in ev:
        n=e.get('npc')
        if not n or not (8342<=n['id']<=8353): continue
        rid=n['roomId']
        if e['type']==7: spawn[rid]=e
        if e['type'] in (7,8): seqs[rid].append((e['tick'],n['id'],e['xCoord'],e['yCoord']))
        if e['type']==9: death[rid]=e
    for rid,s in seqs.items():
        if rid not in spawn: continue
        sp=spawn[rid]['npc']['nylo']
        # style changes
        ch=[]
        prev=s[0]
        for cur in s[1:]:
            if style(cur[1])!=style(prev[1]): ch.append((cur[0],prev,cur))
            prev=cur
        nchg[len(ch)]+=1
        if len(ch)>=2: hold[ch[1][0]-ch[0][0]]+=1
        # aggro swap incoming->fighting (same style, id range change)
    # splits
    kids=collections.defaultdict(list)
    for rid,e in spawn.items():
        ny=e['npc']['nylo']
        if ny['parentRoomId']: kids[ny['parentRoomId']].append(e)
    for par,k in kids.items():
        if par in death:
            lag=k[0]['tick']-death[par]['tick']; splitlag[lag]+=1
            splitcount[len(k)]+=1
            # is parent natural? lifetime
            if par in spawn:
                life=death[par]['tick']-spawn[par]['tick']
                if life>=54: natsplit[len(k)]+=1
print('style-change counts per nylo',dict(nchg))
print('flicker hold (1st style change -> 2nd)',dict(hold))
print('split spawn tick - parent despawn tick',dict(splitlag))
print('splits per parent',dict(splitcount))
print('splits of NATURALLY exploded bigs (parent life>=54): count of splits',dict(natsplit))
