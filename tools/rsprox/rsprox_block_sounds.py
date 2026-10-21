#!/usr/bin/env python3
"""The armour block sound: which synth family a player's block splat sounds with, by what
the recorder wears. Needs transcripts made with PLAYER_APPEARANCE_EQUIPMENT on (the
default filter strips equipment; recipe in docs/rsprox-analysis/ancients/README.md).

    python3 tools/rsprox/rsprox_block_sounds.py <home>/.rsprox/binary/archive/*.txt

Prints, per off-hand item, the families heard with it ("(torso)" = it decided nothing),
and per torso the families heard when no shield decided. Only synths with delay 20 count:
that is how the block sound is sent. docs/rsprox-analysis/combat/block_sounds.md.
"""
import os
import re
import sys
from collections import Counter,defaultdict
CONTENT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'OSRS-Content', 'osrs-content')
objn={}
for line in open(os.path.join(CONTENT, 'configs', 'all.obj.compack'),encoding='latin-1'):
    k,_,v=line.strip().partition('=')
    if k.isdigit(): objn[int(k)]=v
snd={}
for line in open(os.path.join(CONTENT, 'pack', '4_soundeffects.pack'),encoding='latin-1'):
    k,_,v=line.strip().partition('=')
    if k.isdigit(): snd[int(k)]=v
FAMS=['human_block','leather_hit','blade2','torag_breastplate','dragon_shield','crystal_armour','granite_shield','wood_armour3']
SHIELDFAM={'dragon_shield','crystal_armour','granite_shield','wood_armour3'}
pair=Counter()
for f in sys.argv[1:]:
    me=None; cur=None; equip={}; inequip=False
    for line in open(f):
        m=re.search(r'local player index: (\d+)',line)
        if m: me=m.group(1)
        m=re.match(r'^ {8}\[\w+\] player=\(index=(\d+)',line)
        if m: cur=m.group(1); inequip=False; continue
        if re.match(r'^ {16}\[equipment\]',line) and cur==me: inequip=True; equip={}; continue
        if inequip:
            e=re.match(r'^ {20}- wearpos=(\w+), id=(?:([a-z0-9_+]+) \((\d+)\)|(\d+))',line)
            if e: equip[e.group(1)]=e.group(2) or objn.get(int(e.group(4)),e.group(4)); continue
            inequip=False
        s=re.match(r'^ {4}\[synth_sound\] id=(?:([a-z0-9_]+) \((\d+)\)|(\d+))(.*)',line)
        if s and 'delay=20' in s.group(4):
            nm=s.group(1) or snd.get(int(s.group(3)),'')
            for fam in FAMS:
                if re.match(r'^%s_?\d$'%fam,nm):
                    pair[(equip.get('lefthand','-'),equip.get('torso','-'),fam)]+=1
shield=defaultdict(Counter); torso=defaultdict(Counter)
for (sh,to,fam),n in pair.items():
    if fam in SHIELDFAM: shield[sh][fam]+=n
    else:
        torso[to][fam]+=n
        shield[sh]['(torso)']+=n
print('== shields'); [print(k,dict(v)) for k,v in sorted(shield.items())]
print('== torsos'); [print(k,dict(v)) for k,v in sorted(torso.items())]
