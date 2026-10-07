import json,glob,os,collections,statistics as st,sys
D=os.path.dirname(os.path.abspath(__file__))
AT={x['protoId']:x['name'] for x in json.load(open('/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/4ea05e8f-ba2e-4d74-9be0-89660033ebfc/scratchpad/bp/attack.json'))}
MAIDEN=set(range(8360,8366))|set(range(10822,10828))|set(range(10814,10820))
CRAB={8366,10828,10820}
rooms=[]
for f in sorted(glob.glob(D+'/*-*.json')):
    ev=sorted(json.load(open(f)),key=lambda e:e['tick'])
    meta=json.load(open(f+'.meta'))
    if meta['mode']!=11 or meta['scale']!=3: continue
    cr={}  # roomId -> dict
    for e in ev:
        n=e.get('npc')
        if not n or n['id'] not in CRAB: continue
        r=cr.setdefault(n['roomId'],{'pos':{},'spawn':None,'wave':None,'death':None,'dhp':None,'leak':None})
        t=e['tick']
        if e['type']==7: r['spawn']=t; r['wave']=n['maidenCrab']['spawn']
        if e['type'] in(7,8): r['pos'][t]=(e['xCoord'],e['yCoord'])
        if e['type']==9: r['death']=t; r['dhp']=n['hitpoints']>>16
        if e['type']==100: r['leak']=t
    mdeath=[e['tick'] for e in ev if e['type']==9 and e['npc']['id'] in MAIDEN]
    # player attacks at crabs
    pa=collections.defaultdict(lambda:[collections.Counter() for _ in range(3)])
    firstbar=[None]*3
    for e in ev:
        if e['type']!=5: continue
        a=e['attack'];rid=a['target']['roomId']
        if rid in cr and cr[rid]['wave'] is not None:
            w=cr[rid]['wave'];nm=AT.get(a['type'],str(a['type']))
            pa[e['player']['name']][w][(nm,a['weapon']['id'])]+=1
            if 'BARRAGE' in nm and (firstbar[w] is None or e['tick']<firstbar[w]): firstbar[w]=e['tick']
    R={'uuid':meta['uuid'],'maiden_dead':mdeath[0] if mdeath else None,'last':ev[-1]['tick'],'waves':[],'players':{k:[{f"{a}:{b}":c for (a,b),c in w.items()} for w in v] for k,v in pa.items()}}
    for w in range(3):
        cs=[r for r in cr.values() if r['wave']==w]
        if not cs: R['waves'].append(None);continue
        t0=min(r['spawn'] for r in cs)
        def stat(r):
            ts=sorted(r['pos']);end=r['death'] if r['death'] is not None else ts[-1]
            run=best=0;prev=None
            for t in ts:
                if prev is not None and r['pos'][t]==prev: run+=1;best=max(best,run)
                else: run=0
                prev=r['pos'][t]
            return best
        killed=[r for r in cs if r['death'] is not None and (r['dhp']==0) and r['leak'] is None]
        leaked=[r for r in cs if r['leak'] is not None]
        still=[stat(r)>=20 for r in cs]
        endt=max((r['death'] if r['death'] is not None else max(r['pos'])) for r in cs)
        fk=[r['death'] for r in killed]; fl=[r['leak'] for r in leaked]
        R['waves'].append({'t0':t0,'n':len(cs),'killed':len(killed),'leaked':len(leaked),'other':len(cs)-len(killed)-len(leaked),'ambig':sum(1 for r in cs if r['leak'] is None and not(r['dhp']==0) and r['death'] is not None and (not mdeath or r['death']<mdeath[0]-1)),'still20':sum(still),
         'to_barrage':(firstbar[w]-t0) if firstbar[w] is not None else None,'to_kill':(min(fk)-t0) if fk else None,'to_leak':(min(fl)-t0) if fl else None,'dur':endt-t0})
    rooms.append(R)
json.dump(rooms,open(D+'/rooms.json','w'))
print(len(rooms),'rooms')
names=['70','50','30']
def med(v):
    v=[x for x in v if x is not None]
    return f"{st.median(v):g} [{min(v)}-{max(v)}] n={len(v)}" if v else 'none'
for w in range(3):
    ws=[r['waves'][w] for r in rooms if r['waves'][w]]
    print('==',names[w],'rooms with wave',len(ws))
    for k in('n','killed','leaked','other','ambig','still20','to_barrage','to_kill','to_leak','dur'): print(' ',k,med([x[k] for x in ws]))
print('room total (maiden death)',med([r['maiden_dead'] for r in rooms]))
print('leaks per room',collections.Counter(sum(x['leaked'] for x in r['waves'] if x) for r in rooms))
print('rooms with any wave having leaked==0 :',sum(1 for r in rooms if all(x['leaked']==0 for x in r['waves'] if x)))
