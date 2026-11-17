import re, sys
import os
from collections import defaultdict
p=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'OSRS-Content', 'osrs-content',
               'server', 'scripts', 'minigames', 'minigame_zulrah', 'configs', 'zulrah_phase.dbrow')
POS={0:'middle',1:'south',2:'east',3:'west'}
FORM={0:'green',1:'crimson',2:'tanzanite'}
MODE={}
rows=defaultdict(dict)
cur=None
for line in open(p):
    m=re.match(r'\[zulrah_phase_r(\d)_p(\d+)\]',line)
    if m: cur=(int(m.group(1)),int(m.group(2))); rows[cur]['steps']=[]; continue
    if cur is None: continue
    m=re.match(r'data=pos,(\d+)',line)
    if m: rows[cur]['pos']=int(m.group(1))
    m=re.match(r'data=form,(\d+)',line)
    if m: rows[cur]['form']=int(m.group(1))
    m=re.match(r'data=step,(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)\s*(//.*)?',line)
    if m:
        k,a,b,g,ang,src=map(int,m.groups()[:6]); c=(m.group(7) or '').lstrip('/ ').strip()
        rows[cur]['steps'].append((k,a,b,g,ang,src,c))
for r in range(1,5):
    print(f'\nRotation {r}\n')
    print('| phase | surfaces | does (gap to the next) |')
    print('|---|---|---|')
    ph=1
    while (r,ph) in rows:
        d=rows[(r,ph)]
        parts=[]
        for k,a,b,g,ang,src,c in d['steps']:
            parts.append(f'{c} ({g})' if c else f'k{k} {a},{b} ({g})')
        print(f"| {ph} | {FORM[d['form']]} {POS[d['pos']]} | {'; '.join(parts)} |")
        ph+=1
