import json,glob,collections,os,statistics
C=collections.Counter
raids=[]
for f in sorted(glob.glob('blert_verzik/*.json')):
    mode,scale=map(int,os.path.basename(f).split('_')[:2]); raids.append((mode,scale,os.path.basename(f)[5:13],json.load(open(f))))
P1=(8370,10831,10848); P2=(8372,10833,10850); P3=(8374,10835,10852)
REDS=(8385,10845,10862); PURPLE=(8384,10844,10861); CRABS=(8381,8382,8383,10841,10842,10843,10858,10859,10860)
TORN=(8386,10846,10863); WEB=(8376,10837,10854); PILLAR=(8379,10840,10857)
def cur_base(e): v=e['npc']['hitpoints']; return v>>16, v&0xFFFF
out=collections.defaultdict(list)
for mode,scale,u,d in raids:
    if mode==10: tag='entry'
    else: tag='n' if mode==11 else 'h'
    ev=sorted(d,key=lambda e:e['tick'])
    atk=[(e['tick'],e['npc']['id'],e['npcAttack']['attack']) for e in ev if e['type']==10]
    # P1
    p1=[t for t,i,a in atk if i in P1]
    if p1:
        out['p1_first_'+tag].append(p1[0]); out['p1_gaps_'+tag]+=[b-a for a,b in zip(p1,p1[1:])]
    p2=[(t,a) for t,i,a in atk if i in P2]
    p3=[(t,a) for t,i,a in atk if i in P3]
    ph=[e['tick'] for e in ev if e['type']==150]
    # id appear ticks
    first_id={}
    for e in ev:
        if e['type'] in (7,8) and 'npc' in e:
            i=e['npc']['id']
            for nm,grp in (('p2',P2),('p3',P3)):
                if i in grp and nm not in first_id: first_id[nm]=e['tick']
    if len(ph)>=2 and p2 and 'p2' in first_id:
        out['p2_event_to_id_'+tag].append(first_id['p2']-ph[0]); out['p2_id_to_first_'+tag].append(p2[0][0]-first_id['p2']); out['p2_event_to_first_'+tag].append(p2[0][0]-ph[0])
    if len(ph)>=2 and p3 and 'p3' in first_id:
        out['p3_event_to_id_'+tag].append(first_id['p3']-ph[1]); out['p3_id_to_first_'+tag].append(p3[0][0]-first_id['p3']); out['p3_event_to_first_'+tag].append(p3[0][0]-ph[1])
    # P2 gaps and sequences
    t2=[t for t,a in p2]; out['p2_gaps_'+tag]+=[b-a for a,b in zip(t2,t2[1:])]
    t3=[(t,a) for t,a in p3]
    # P3 autos only (19,20,21), specials 22,23,24
    seq=[a for t,a in p2]
    out['p2_seq_'+tag].append(seq)
    # zap spacing: count of attacks between consecutive 15
    idx=[k for k,a in enumerate(seq) if a==15]
    out['zap_between_'+tag]+=[b-a-1 for a,b in zip(idx,idx[1:])]
    idxp=[k for k,a in enumerate(seq) if a==16]
    out['purple_between_'+tag]+=[b-a-1 for a,b in zip(idxp,idxp[1:])]
    if idxp: out['first_purple_idx_'+tag].append(idxp[0])
    # reds: NPC_SPAWN of reds
    rs=[e['tick'] for e in ev if e['type']==7 and e['npc']['id'] in REDS]
    rt=sorted(set(rs)); out['reds_spawn_ticks_'+tag].append(rt)
    out['reds_count_per_tick_'+tag]+=[C(rs)[t] for t in rt]
    # hp pct at first reds tick (last known P2 update <= tick)
    if rt:
        last=None
        for e in ev:
            if e['tick']>rt[0]: break
            if e['type'] in (7,8) and e['npc']['id'] in P2: last=cur_base(e)
        if last and last[1]: out['reds_hp_pct_'+tag].append(round(100*last[0]/last[1],1))
        # attacks between reds sets
        if len(rt)>=2:
            for a,b in zip(rt,rt[1:]):
                n=len([1 for t,x in p2 if a<t<=b]); out['p2_attacks_between_reds_'+tag].append(n)
        # first attack after reds
        nxt=[t for t,a in p2 if t>rt[0]]
        if nxt: out['reds_to_first_attack_'+tag].append(nxt[0]-rt[0])
    # crabs spawned: groups per tick
    cs=[e['tick'] for e in ev if e['type']==7 and e['npc']['id'] in CRABS]
    cc=C(cs); out['crab_groups_'+tag]+=[(scale,cc[t]) for t in sorted(cc)]
    # purple spawn
    ps=[e['tick'] for e in ev if e['type']==7 and e['npc']['id'] in PURPLE]
    out['purple_spawns_'+tag].append(len(ps))
    # pillars at tick 0
    pil=[e for e in ev if e['type']==7 and e['npc']['id'] in PILLAR and e['tick']==0]
    out['pillars_t0_'+tag].append(len(pil))
    # P3
    t3t=[t for t,a in p3 if a in (19,20,21)]
    # gaps between consecutive auto attacks overall (including after specials)
    allt=[t for t,a in p3]
    g=[b-a for a,b in zip(allt,allt[1:])]
    out['p3_gaps_all_'+tag]+=g
    out['p3_seq_'+tag].append([a for t,a in p3])
    # gaps after specials
    for (t1,a1),(t2_,a2) in zip(p3,p3[1:]):
        if a1 in (22,23,24): out['p3_after_%d_%s'%(a1,tag)].append(t2_-t1)
        if a2 in (22,23,24) and a1 not in (22,23,24): out['p3_before_%d_%s'%(a2,tag)].append(t2_-t1)
    # tornado spawn hp pct
    ts=[e['tick'] for e in ev if e['type']==7 and e['npc']['id'] in TORN]
    if ts:
        t0=min(ts); out['tornado_first_tick_'+tag].append(t0)
        out['tornado_count_'+tag].append((scale,len([1 for e in ev if e['type']==7 and e['npc']['id'] in TORN and e['tick']==t0])))
        last=None
        for e in ev:
            if e['tick']>t0: break
            if e['type'] in (7,8) and e['npc']['id'] in P3: last=cur_base(e)
        if last and last[1]: out['enrage_hp_pct_'+tag].append(round(100*last[0]/last[1],1))
        # attack gaps after enrage
        late=[t for t,a in p3 if t>t0]; out['p3_gaps_enraged_'+tag]+=[b-a for a,b in zip(late,late[1:])]
    ws=[e['tick'] for e in ev if e['type']==7 and e['npc']['id'] in WEB]
    wc=C(ws); out['web_groups_'+tag]+=[(scale,wc[t]) for t in sorted(wc)]
    # web lifetime: spawn to death/despawn by roomId
    spawn={}
    for e in ev:
        if e['type']==7 and e['npc']['id'] in WEB: spawn[e['npc']['roomId']]=e['tick']
        if e['type']==9 and e['npc']['id'] in WEB and e['npc']['roomId'] in spawn:
            out['web_life_'+tag].append(e['tick']-spawn.pop(e['npc']['roomId']))
    yl=[(e['tick'],len(e['verzikYellows'])) for e in ev if e['type']==153]
    out['yellows_'+tag].append((scale,[n for t,n in yl][:3], len(yl), (yl[0][0],yl[-1][0]) if yl else None))
def dist(l):
    c=C(l); return dict(sorted(c.items()))
for k in sorted(out):
    v=out[k]
    if k.startswith(('p2_seq','p3_seq','reds_spawn_ticks')):
        continue
    print(k, dist(v) if v and not isinstance(v[0],(tuple,list)) else v)
