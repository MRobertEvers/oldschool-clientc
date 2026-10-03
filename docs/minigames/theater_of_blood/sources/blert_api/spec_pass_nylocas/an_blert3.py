import json,glob,collections
files=sorted(glob.glob('blert_nylo_raw/*.json'))
style=lambda i:(i-8342)%3
first=collections.defaultdict(collections.Counter); agg=collections.defaultdict(collections.Counter)
def lane(x,y):
    if x<=3285: return 'west'
    if x>=3305: return 'east'
    return 'south'
for f in files:
    ev=json.load(open(f))
    seqs=collections.defaultdict(list); spawn={}
    for e in ev:
        n=e.get('npc')
        if not n or not (8342<=n['id']<=8353): continue
        rid=n['roomId']
        if e['type']==7: spawn[rid]=e
        if e['type'] in (7,8): seqs[rid].append((e['tick'],n['id']))
    for rid,s in seqs.items():
        if rid not in spawn or spawn[rid]['npc']['nylo']['spawnType']==0: continue
        sp=spawn[rid]; ln={1:'west',2:'south',3:'east'}[sp['npc']['nylo']['spawnType']]; big=sp['npc']['nylo']['big']
        prev=s[0]; got_style=False;got_agg=False
        for cur in s[1:]:
            if cur[1]!=prev[1]:
                if style(cur[1])!=style(prev[1]) and not got_style:
                    first[(ln,big)][cur[0]-sp['tick']]+=1; got_style=True
                elif style(cur[1])==style(prev[1]) and not got_agg:
                    agg[(ln,big)][cur[0]-sp['tick']]+=1; got_agg=True
            prev=cur
print('FIRST style change (flicker) ticks after spawn',{k:dict(sorted(v.items())) for k,v in first.items()})
print('aggro swap ticks after spawn',{k:dict(sorted(v.items())) for k,v in agg.items()})
