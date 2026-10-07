import json,glob,collections,sys
MODES={'m11':(8354,{8355,8356,8357}),'m10':(10786,{10787,10788,10789}),'m12':(10807,{10808,10809,10810})}
for mk,(spawnid,forms) in MODES.items():
    files=sorted(glob.glob('blert_nylo_raw/*.%s.*.json'%mk))
    gap=collections.Counter(); per=collections.Counter(); first=collections.Counter(); intervals=collections.Counter(); phase=collections.Counter(); firstint=collections.Counter()
    rep=0;n=0;tot=0
    for f in files:
        ev=json.load(open(f))
        allids=forms|{spawnid}
        atk=[e['tick'] for e in ev if e['type']==10 and e['npc']['id'] in allids]
        cur=None; sw=[]
        for e in ev:
            nn=e.get('npc')
            if nn and nn['id'] in forms and e['type'] in (7,8) and nn['id']!=cur:
                if cur is not None and False: pass
                cur=nn['id']; sw.append((e['tick'],cur))
        if not sw or not atk: continue
        n+=1
        for (a,i),(c,j) in zip(sw,sw[1:]):
            intervals[c-a]+=1
            if i==j: rep+=1
        if len(sw)>1: firstint[sw[1][0]-sw[0][0]]+=1
        for a,c in zip(atk,atk[1:]): gap[c-a]+=1
        first[atk[0]-sw[0][0]]+=1
        for (a,i),(c,j) in zip(sw,sw[1:]):
            offs=tuple(t-a for t in atk if a<=t<c); per[len(offs)]+=1; phase[offs]+=1
    print(mk,'raids',n,'files',len(files))
    print('  attack gaps',sorted(gap.items())); print('  first attack - first form',sorted(first.items()))
    print('  switch intervals',sorted(intervals.items()),'repeats',rep,'first interval',sorted(firstint.items()))
    print('  attacks per window',sorted(per.items()),'offsets',phase.most_common(8))
