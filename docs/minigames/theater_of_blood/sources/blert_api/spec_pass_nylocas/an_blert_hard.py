import json,glob,collections
PR={10803,10804,10805,10806}
gapat=collections.defaultdict(collections.Counter); stall_counts=collections.Counter(); pr_sw=collections.Counter(); pr_first=collections.Counter(); pr_atk=collections.Counter(); pr_life=[]
pr_spawn_wave=collections.Counter(); pr_xy=collections.Counter(); pr_per=collections.Counter(); pr_off=collections.Counter()
for f in sorted(glob.glob('blert_nylo_raw/*.m12.*.json')):
    ev=json.load(open(f))
    first={}
    for e in ev:
        n=e.get('npc')
        if e['type']==7 and n and 'nylo' in n and not n['nylo']['parentRoomId'] and n['nylo']['spawnType']!=0:
            w=n['nylo']['wave']; first[w]=min(first.get(w,e['tick']),e['tick'])
    for w in (9,10,19,20,29,30,5,12):
        if w in first and w+1 in first: gapat[w][first[w+1]-first[w]]+=1
    # prince
    pe=[e for e in ev if e.get('npc') and e['npc']['id'] in PR]
    for e in pe:
        if e['type']==7:
            pr_xy[(e['xCoord'],e['yCoord'])]+=1
            pr_spawn_wave[e['npc'].get('nylo',{}).get('wave') if 'nylo' in e['npc'] else None]+=1
    byroom=collections.defaultdict(list)
    for e in pe: byroom[e['npc']['roomId']].append(e)
    for rid,es in byroom.items():
        es.sort(key=lambda e:e['tick'])
        cur=None;sw=[]
        for e in es:
            if e['type'] in (7,8) and e['npc']['id']!=cur and e['npc']['id'] in (10804,10805,10806):
                cur=e['npc']['id']; sw.append(e['tick'])
        d=[b-a for a,b in zip(sw,sw[1:])]
        for x in d: pr_sw[x]+=1
        atk=[e['tick'] for e in ev if e['type']==10 and e['npc']['roomId']==rid]
        for a,b in zip(atk,atk[1:]): pr_atk[b-a]+=1
        for a,b in zip(sw,sw[1:]):
            o=tuple(t-a for t in atk if a<=t<b); pr_per[len(o)]+=1; pr_off[o]+=1
        dt=[e['tick'] for e in es if e['type']==9]
        if dt: pr_life.append((es[0]['tick'],dt[0]))
print('wave gaps by wave (hard)',{w:dict(c) for w,c in gapat.items()})
print('prince spawn tiles',dict(pr_xy)); print('prince spawn wave tags',dict(pr_spawn_wave))
print('prince switch intervals',sorted(pr_sw.items())); print('prince attack gaps',sorted(pr_atk.items()))
print('prince attacks per window',sorted(pr_per.items()),pr_off.most_common(5)); print('prince spawn->death',pr_life)
