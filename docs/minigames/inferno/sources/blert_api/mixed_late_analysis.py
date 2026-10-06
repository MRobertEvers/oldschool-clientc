#!/usr/bin/env python3
"""Mixed late waves (35-66: every wave with a Jal-Zek) from observed_npc_events.tsv + wave_records.tsv -> MIXED_LATE_ANALYSIS.txt.
OBSERVED rows only. Tick 0 = the `Wave: N` chat tick. Style of an attack id: 79 ranged,80 melee(ranger),70 ranged(bat),77/78 melee,81 magic,82 melee,83 resurrect."""
import csv, collections, os, statistics as st
D=os.path.dirname(os.path.abspath(__file__))+'/'
ev=list(csv.DictReader(open(D+'observed_npc_events.tsv'),delimiter='\t'))
wr=list(csv.DictReader(open(D+'wave_records.tsv'),delimiter='\t'))
W=range(35,67)
STYLE={'79':'R','80':'M','70':'R','77':'M','78':'M','81':'G','82':'M','83':'X','71':'R','72':'G'}
KIND={'7691':'nib','7692':'bat','7693':'blob','7697':'mel','7698':'ran','7699':'mag'}
def p(a,q): a=sorted(a); return a[min(len(a)-1,int(q*len(a)))]
byrw=collections.defaultdict(list)
for r in ev:
    if r['wave'].isdigit() and int(r['wave']) in W: byrw[(r['run'],int(r['wave']))].append(r)
print('mixed-wave (35-66) run-waves with events: %d; runs %d'%(len(byrw),len({k[0] for k in byrw})))
dur=[int(r['ticks']) for r in wr if r['wave'].isdigit() and int(r['wave']) in W and r['ticks'].isdigit() and r['status']!='status']
print('wave duration ticks (wave_records.ticks) n=%d min %d p10 %d p50 %d p90 %d max %d'%(len(dur),min(dur),p(dur,.1),p(dur,.5),p(dur,.9),max(dur)))
for lo,hi in ((35,44),(45,56),(57,66)):
    d=[int(r['ticks']) for r in wr if r['wave'].isdigit() and lo<=int(r['wave'])<=hi and r['ticks'].isdigit()]
    print('duration waves %d-%d n=%d p10 %d p50 %d p90 %d'%(lo,hi,len(d),p(d,.1),p(d,.5),p(d,.9)))
# tick-0 composition
tab={}
for l in list(csv.DictReader(open(D+'../../encounters/wave_table.tsv'),delimiter='\t')):
    m=l['mechanic_id']
    if m.endswith('_composition'): tab[int(m.split('.')[1][1:3])]=[int(x) for x in l['spec_value'].split(',')]
ok=bad=0; nonnibshare=0; tick0=collections.Counter(); lateall=collections.Counter(); share=0; tot=0; maxsame=0
mixsw=[];simul=0;simulticks=0;attackticks=0;firstany=[];gap_first=[]
for (run,w),rows in sorted(byrw.items()):
    sp=[r for r in rows if r['kind']=='spawn' and r['npc_id'] in KIND]
    s0=[r for r in sp if r['tick']=='0']
    c=collections.Counter(KIND[r['npc_id']] for r in s0)
    got=[c['nib'],c['bat'],c['blob'],c['mel'],c['ran'],c['mag'],0]
    if got==tab[w]: ok+=1
    else: bad+=1
    tiles=collections.Counter((r['x'],r['y']) for r in s0)
    tot+=1
    if any(v>1 for v in tiles.values()): share+=1
    tn=collections.Counter((r['x'],r['y']) for r in s0 if r['npc_id']!='7691')
    if any(v>1 for v in tn.values()): nonnibshare+=1
    # attacks merged
    at=sorted((int(r['tick']),STYLE.get(r['attack_id'],'?'),r['room_id']) for r in rows if r['kind']=='attack' and r['npc_id'] in KIND)
    if not at: continue
    firstany.append(at[0][0])
    tk=collections.defaultdict(set)
    for t,s,rid in at: tk[t].add((s,rid))
    for t,v in tk.items():
        attackticks+=1
        if len({x[0] for x in v if x[0] in 'RMG'})>=2: simulticks+=1
    seq=[]
    for t in sorted(tk):
        for s in sorted({x[0] for x in tk[t] if x[0] in 'RMG'}): seq.append(s)
    sw=sum(1 for i in range(1,len(seq)) if seq[i]!=seq[i-1])
    mixsw.append((w,sw,len(seq)))
print('tick-0 non-pillar composition equals the wave table: %d of %d run-waves (%d differ)'%(ok,ok+bad,bad))
print('run-waves where two tick-0 monsters share a spawn tile: %d of %d'%(share,tot))
print('run-waves where two NON-nibbler tick-0 monsters share a spawn tile: %d of %d'%(nonnibshare,tot))
print('first attack of ANY monster, ticks from wave message n=%d p10 %d p50 %d p90 %d'%(len(firstany),p(firstany,.1),p(firstany,.5),p(firstany,.9)))
print('attack ticks with two or more different styles (R/M/G) at once: %d of %d attack ticks (%.1f%%)'%(simulticks,attackticks,100*simulticks/attackticks))
sws=[a[1] for a in mixsw]; print('distinct-style changes in the merged attack stream per wave n=%d p10 %d p50 %d p90 %d'%(len(sws),p(sws,.1),p(sws,.5),p(sws,.9)))
for lo,hi in ((35,44),(45,56),(57,66)):
    a=[x[1] for x in mixsw if lo<=x[0]<=hi]; print('  style changes waves %d-%d n=%d p50 %d p90 %d'%(lo,hi,len(a),p(a,.5),p(a,.9)))
# count spawns after tick 0 (revives and splits) per wave
late=collections.Counter()
for (run,w),rows in byrw.items():
    n=sum(1 for r in rows if r['kind']=='spawn' and r['npc_id'] in KIND and r['tick']!='0')
    late[n]+=1
print('non-tick-0 spawns (revives; blob splits not counted: bloblet ids excluded) per run-wave: %s'%dict(sorted(late.items())))
# deaths: order by kind
dk=collections.Counter(); first_dead=collections.Counter()
for (run,w),rows in byrw.items():
    d=sorted((int(r['tick']),r['npc_id']) for r in rows if r['kind']=='death' and r['npc_id'] in KIND and r['npc_id']!='7691')
    if d: first_dead[KIND[d[0][1]]]+=1
print('kind that dies first (non-nibbler) in a mixed wave: %s'%dict(first_dead))
# nibbler kill vs last death
last=[]; 
for (run,w),rows in byrw.items():
    d=[int(r['tick']) for r in rows if r['kind']=='death' and r['npc_id'] in KIND]
    if d: last.append(max(d))
print('tick of the last non-pillar death n=%d p10 %d p50 %d p90 %d'%(len(last),p(last,.1),p(last,.5),p(last,.9)))
# order of first attack: which kind attacks first
fk=collections.Counter()
for (run,w),rows in byrw.items():
    a=sorted((int(r['tick']),r['npc_id']) for r in rows if r['kind']=='attack' and r['npc_id'] in KIND)
    if a:
        t0=a[0][0]; fk.update({KIND[i] for t,i in a if t==t0})
print('kinds attacking on the first attack tick of the wave (counts): %s'%dict(fk))
# wave 66 magers: two
w66=collections.Counter(); 
for (run,w),rows in byrw.items():
    if w==66: w66[sum(1 for r in rows if r['kind']=='spawn' and r['npc_id']=='7699' and r['tick']=='0')]+=1
print('wave 66 tick-0 mager spawns per run: %s'%dict(w66))
# --- phase of simultaneous attackers: the first attack tick of each npc, difference mod its cadence (4 for mager, ranger, melee)
print('--- phase')
def firsts(rows,nid):
    d={}
    for r in rows:
        if r['kind']=='attack' and r['npc_id']==nid:
            t=int(r['tick']); d[r['room_id']]=min(d.get(r['room_id'],10**9),t)
    return sorted(d.values())
for a,b,na,nb,lo in (('7699','7698','mager','ranger',50),('7699','7697','mager','meleer',42),('7698','7697','ranger','meleer',57)):
    c=collections.Counter(); n=0; same=0; allsame=0
    for (run,w),rows in byrw.items():
        if w<lo: continue
        fa=firsts(rows,a); fb=firsts(rows,b)
        if len(fa)==1 and len(fb)==1:
            n+=1; d=(fb[0]-fa[0])%4; c[d]+=1
            if fb[0]==fa[0]: same+=1
    print('%s vs %s (waves >= %d, one of each, both attacked) n=%d first-attack phase difference mod 4 %s; same first tick %d'%(na,nb,lo,n,dict(sorted(c.items())),same))
# attack ticks where mager and ranger attack on the same tick, waves with both
sm=tot=0
for (run,w),rows in byrw.items():
    m={int(r['tick']) for r in rows if r['kind']=='attack' and r['npc_id']=='7699'}
    g={int(r['tick']) for r in rows if r['kind']=='attack' and r['npc_id']=='7698'}
    if m and g and w>=50:
        tot+=len(g); sm+=len(g&m)
print('ranger attack ticks that are also a mager attack tick (waves 50-66): %d of %d'%(sm,tot))
# --- cadence in company: modal gap between one npc's consecutive auto attacks inside mixed waves
print('--- cadence')
AUTO={'7699':{'81'},'7698':{'79'},'7697':{'77'},'7692':{'70'}}
for nid,name in (('7699','mager'),('7698','ranger'),('7697','meleer'),('7692','bat')):
    g=collections.Counter()
    for (run,w),rows in byrw.items():
        by=collections.defaultdict(list)
        for r in rows:
            if r['kind']=='attack' and r['npc_id']==nid and r['attack_id'] in AUTO[nid]: by[r['room_id']].append(int(r['tick']))
        for v in by.values():
            v.sort()
            for i in range(1,len(v)): g[v[i]-v[i-1]]+=1
    n=sum(g.values()); m=g.most_common(1)[0]
    print('%s auto gap in waves 35-66: n=%d modal %d (%d = %.1f%%), below modal %d'%(name,n,m[0],m[1],100*m[1]/n,sum(v for k,v in g.items() if k<m[0])))
